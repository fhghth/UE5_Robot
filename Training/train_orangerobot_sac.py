import signal
import sys
from pathlib import Path
from datetime import datetime
import json

import torch
from stable_baselines3 import SAC
from stable_baselines3.common.callbacks import BaseCallback
from stable_baselines3.common.callbacks import CheckpointCallback
from schola.core.protocols.protobuf.gRPC import gRPCProtocol
from schola.core.simulators.unreal.editor import UnrealEditor
from schola.sb3.env import VecEnv

# 全局变量，用于信号处理器访问
_model = None
_env = None
_output_dir = None
_training_id = None
_start_time = None


class TensorboardMetricsCallback(BaseCallback):
    """记录更全面的 episode 指标，便于在 TensorBoard 分析训练过程。"""

    def __init__(self, sac_log_freq: int = 200, verbose: int = 0):
        super().__init__(verbose)
        self._sac_log_freq = max(1, sac_log_freq)
        self._episode_idx = 0
        self._episode_reward = 0.0
        self._episode_length = 0
        self._recent_rewards: list[float] = []
        self._recent_lengths: list[int] = []
        self._recent_success: list[float] = []

    @staticmethod
    def _to_scalar(value):
        if hasattr(value, "item"):
            value = value.item()
        if isinstance(value, bool):
            return float(value)
        if isinstance(value, (int, float)):
            return float(value)
        return None

    @staticmethod
    def _extract_success(info: dict) -> float | None:
        for key in ("is_success", "success", "task_success", "goal_achieved"):
            if key in info:
                return TensorboardMetricsCallback._to_scalar(info[key])
        return None

    def _record_window_stats(self, window_size: int = 100) -> None:
        if not self._recent_rewards:
            return
        reward_window = self._recent_rewards[-window_size:]
        length_window = self._recent_lengths[-window_size:]
        self.logger.record(
            "custom/episode_reward_mean_100",
            float(sum(reward_window) / len(reward_window)),
        )
        self.logger.record(
            "custom/episode_length_mean_100",
            float(sum(length_window) / len(length_window)),
        )
        if self._recent_success:
            success_window = self._recent_success[-window_size:]
            self.logger.record(
                "custom/success_rate_100",
                float(sum(success_window) / len(success_window)),
            )

    def _on_step(self) -> bool:
        rewards = self.locals.get("rewards")
        dones = self.locals.get("dones")
        infos = self.locals.get("infos")
        if rewards is None or dones is None:
            return True

        if infos is None:
            infos = [{} for _ in range(len(rewards))]

        for reward, done, info in zip(rewards, dones, infos):
            reward_scalar = self._to_scalar(reward)
            if reward_scalar is None:
                continue

            self._episode_reward += reward_scalar
            self._episode_length += 1
            self.logger.record("custom/reward_step", reward_scalar)

            if done:
                self._episode_idx += 1
                self._recent_rewards.append(self._episode_reward)
                self._recent_lengths.append(self._episode_length)

                self.logger.record("custom/episode_reward", self._episode_reward)
                self.logger.record("custom/episode_length", self._episode_length)
                self.logger.record("custom/episode_index", self._episode_idx)
                self.logger.record(
                    "custom/reward_step_mean",
                    self._episode_reward / max(self._episode_length, 1),
                )

                success = self._extract_success(info)
                if success is not None:
                    self._recent_success.append(success)
                    self.logger.record("custom/episode_success", success)

                self._record_window_stats(window_size=100)
                self._episode_reward = 0.0
                self._episode_length = 0

        self._record_sac_metrics()
        return True

    def _record_sac_metrics(self) -> None:
        if self.n_calls % self._sac_log_freq != 0:
            return

        model = self.model
        if model is None or not hasattr(model, "actor") or not hasattr(model, "critic"):
            return

        obs = model._last_obs
        if obs is None:
            return

        with torch.no_grad():
            obs_tensor, _ = model.policy.obs_to_tensor(obs)
            actions_pi, log_prob = model.actor.action_log_prob(obs_tensor)
            q_values = model.critic(obs_tensor, actions_pi)
            q_min = torch.min(torch.cat(q_values, dim=1), dim=1, keepdim=True)[0]

            entropy_value = float((-log_prob).mean().item())
            self.logger.record("train/entropy", entropy_value)
            self.logger.record("train/q_value_mean", float(q_min.mean().item()))
            self.logger.record("train/q_value_min", float(q_min.min().item()))
            self.logger.record("train/q_value_max", float(q_min.max().item()))

            ent_coef_value = None
            if getattr(model, "log_ent_coef", None) is not None:
                ent_coef_value = float(torch.exp(model.log_ent_coef.detach()).mean().item())
            elif getattr(model, "ent_coef_tensor", None) is not None:
                ent_coef_value = float(model.ent_coef_tensor.detach().mean().item())
            if ent_coef_value is not None:
                self.logger.record("train/ent_coef_value", ent_coef_value)


def signal_handler(sig, frame):
    """处理 Ctrl+C 中断信号，保存模型并安全退出"""
    print("\n⚠️ 收到中断信号，正在保存模型并关闭环境...")
    try:
        if _model is not None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            save_path = _output_dir / f"orange_robot_sac_interrupted_{_training_id}_{timestamp}.zip"

            # 保存模型
            _model.save(str(save_path))

            # 保存训练元数据
            metadata = {
                "training_id": _training_id,
                "timestamp": timestamp,
                "status": "interrupted",
                "interruption_time": _start_time.strftime("%Y-%m-%d %H:%M:%S") if _start_time else None,
                "interruption_reason": "SIGINT" if sig == signal.SIGINT else "SIGTERM"
            }
            metadata_path = _output_dir / f"training_metadata_{_training_id}.json"
            with open(metadata_path, 'w') as f:
                json.dump(metadata, f, indent=2)

            print(f"✅ 模型已保存至 {save_path}")
            print(f"📄 训练元数据已保存至 {metadata_path}")

        if _env is not None:
            _env.close()
            print("✅ 环境已关闭")
    except Exception as e:
        print(f"❌ 保存或关闭时出错: {e}")
    sys.exit(0)


def get_next_training_id(output_dir: Path) -> str:
    """获取下一个可用的训练ID（自动递增）"""
    # 查找已存在的训练文件夹
    training_dirs = [d for d in output_dir.iterdir() if d.is_dir()]
    training_nums = []

    for d in training_dirs:
        if d.name.startswith("training_"):
            try:
                num = int(d.name.split("_")[1])
                training_nums.append(num)
            except (ValueError, IndexError):
                pass

    if training_nums:
        next_num = max(training_nums) + 1
    else:
        next_num = 1

    return f"training_{next_num:03d}"


def make_env(verbosity: int = 1) -> VecEnv:
    simulator = UnrealEditor()
    protocol = gRPCProtocol(
        url="localhost",
        port=50051,
        environment_start_timeout=180,
    )
    return VecEnv(simulator, protocol, verbosity=verbosity)


def prompt_training_mode(base_output_dir: Path) -> tuple[str, Path | None]:
    print("请选择训练模式：")
    print("  1) 开始新训练")
    print("  2) 继续训练")

    while True:
        choice = input("请输入 1 或 2: ").strip()
        if choice == "1":
            return "new", None
        if choice == "2":
            while True:
                raw_path = input("请输入继续训练模型路径: ").strip().strip('"')
                if not raw_path:
                    print("⚠️ 路径不能为空，请重新输入。")
                    continue

                resume_model_path = Path(raw_path)
                if not resume_model_path.is_absolute():
                    resume_model_path = (base_output_dir.parent.parent / resume_model_path).resolve()

                if not resume_model_path.exists() or not resume_model_path.is_file():
                    print(f"❌ 文件不存在: {resume_model_path}")
                    continue

                return "resume", resume_model_path

        print("⚠️ 无效输入，请输入 1 或 2。")


def main() -> None:
    global _model, _env, _output_dir, _training_id, _start_time

    total_timesteps = 1_000_000
    project_root = Path(__file__).resolve().parent.parent
    base_output_dir = project_root / "Training" / "Robot"
    base_output_dir.mkdir(parents=True, exist_ok=True)
    training_mode, resume_model_path = prompt_training_mode(base_output_dir)

    # 创建本次训练的唯一目录
    _training_id = get_next_training_id(base_output_dir)
    _output_dir = base_output_dir / _training_id
    _output_dir.mkdir(parents=True, exist_ok=True)

    # 记录训练开始时间
    _start_time = datetime.now()

    # 保存训练配置
    config = {
        "training_id": _training_id,
        "start_time": _start_time.strftime("%Y-%m-%d %H:%M:%S"),
        "total_timesteps": total_timesteps,
        "algorithm": "SAC",
        "training_mode": training_mode,
        "resume_from": str(resume_model_path) if resume_model_path is not None else None,
    }

    config_path = _output_dir / "training_config.json"
    with open(config_path, 'w') as f:
        json.dump(config, f, indent=2)

    print(f"🎯 开始训练: {_training_id}")
    print(f"📁 输出目录: {_output_dir}")
    if training_mode == "resume" and resume_model_path is not None:
        print(f"📦 继续训练权重: {resume_model_path}")
    else:
        print("🆕 本次为全新训练")

    # 注册信号处理器
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    env = make_env(verbosity=1)
    _env = env

    # 检查 GPU 可用性
    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"🚀 使用设备: {device}")
    if device == "cuda":
        print(f"    GPU 型号: {torch.cuda.get_device_name(0)}")
        # 适当增大 batch_size 以利用 GPU 并行能力
        batch_size = 512
        print(f"    调整 batch_size 为 {batch_size}")
    else:
        batch_size = 256
        print("    未检测到 GPU，使用 CPU 训练，batch_size=256")

    # 检查点保存配置
    checkpoint_dir = _output_dir / "checkpoints"
    checkpoint_dir.mkdir(exist_ok=True)

    # 定期保存检查点（每 50000 步保存一次）
    checkpoint_callback = CheckpointCallback(
        save_freq=50_000,
        save_path=str(checkpoint_dir),
        name_prefix="orange_robot_sac",
        save_replay_buffer=True,
        save_vecnormalize=True,
        verbose=1,
    )
    metrics_callback = TensorboardMetricsCallback()

    try:
        if training_mode == "resume":
            model = SAC.load(
                str(resume_model_path),
                env=env,
                device=device,
                tensorboard_log=str(_output_dir / "tensorboard"),
            )
            print(f"♻️ 已载入继续训练模型: {Path(resume_model_path).name}")
        else:
            model = SAC(
                policy="MlpPolicy",
                env=env,
                verbose=1,
                policy_kwargs=dict(
                    net_arch=dict(
                        pi=[512, 512, 256],  # 策略网络：三层
                        qf=[512, 512, 256]  # Q 网络：三层
                    )
                ),
                learning_rate=1e-4,
                buffer_size=1_000_000,
                learning_starts=20_000,
                batch_size=batch_size,
                tau=0.005,
                gamma=0.99,
                train_freq=1,
                gradient_steps=1,
                ent_coef="auto",
                # ent_coef=0.005,
                target_update_interval=1,
                # target_entropy="auto",
                target_entropy=-14,
                device=device,
                tensorboard_log=str(_output_dir / "tensorboard"),
            )
            print("🆕 已创建新的 SAC 模型")

        _model = model

        # 开始训练
        model.learn(
            total_timesteps=total_timesteps,
            log_interval=10,
            callback=[checkpoint_callback, metrics_callback],
            reset_num_timesteps=(training_mode != "resume"),
        )

        # 训练正常结束，保存最终模型（带时间戳）
        end_time = datetime.now()
        timestamp = end_time.strftime("%Y%m%d_%H%M%S")

        # 保存最终模型
        final_model_path = _output_dir / f"orange_robot_sac_final_{timestamp}.zip"
        model.save(str(final_model_path))

        # 更新训练元数据
        metadata = {
            "training_id": _training_id,
            "start_time": _start_time.strftime("%Y-%m-%d %H:%M:%S"),
            "end_time": end_time.strftime("%Y-%m-%d %H:%M:%S"),
            "duration_seconds": (end_time - _start_time).total_seconds(),
            "status": "completed",
            "total_timesteps": total_timesteps,
            "final_model": str(final_model_path.relative_to(base_output_dir)),
            "device_used": device
        }

        metadata_path = _output_dir / f"training_metadata_{_training_id}.json"
        with open(metadata_path, 'w') as f:
            json.dump(metadata, f, indent=2)

        print(f"🎉 训练完成!")
        print(f"📦 最终模型已保存至: {final_model_path}")
        print(f"📄 训练元数据已保存至: {metadata_path}")
        print(f"⏱️  训练时长: {metadata['duration_seconds']:.2f} 秒")

    except KeyboardInterrupt:
        print("\n⚠️ KeyboardInterrupt 捕获，退出中...")
    except Exception as e:
        print(f"\n❌ 训练过程中发生错误: {e}")

        # 保存错误模型
        if _model is not None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            error_model_path = _output_dir / f"orange_robot_sac_error_{timestamp}.zip"
            _model.save(str(error_model_path))
            print(f"⚠️  错误模型已保存至: {error_model_path}")

    finally:
        if env is not None:
            env.close()
            print("🔒 环境已关闭。")


if __name__ == "__main__":
    main()
