import signal
import sys
from pathlib import Path
from datetime import datetime
import json

import torch
import torch.nn as nn
from stable_baselines3 import SAC
from stable_baselines3.common.torch_layers import BaseFeaturesExtractor
from stable_baselines3.common.callbacks import BaseCallback, CheckpointCallback
from stable_baselines3.common.vec_env import VecEnv as BaseVecEnv
from schola.core.protocols.protobuf.gRPC import gRPCProtocol
from schola.core.simulators.unreal.editor import UnrealEditor
from schola.sb3.env import VecEnv

import gym
from torch.optim import Adam

gym.__version__ = gym.__version__ if hasattr(gym, '__version__') else "0.21.0"

# 全局变量，用于信号处理器访问
_model = None
_env = None
_output_dir = None
_training_id = None
_start_time = None


# ====================================================================
# SAC 默认超参（与 train_headless.py 保持同步）
# ====================================================================
SAC_DEFAULTS = {
    "orangerobot": {
        "buffer_size": 1_000_000,
        "learning_starts": 5_000,
        "batch_size": 1024,
        "tau": 0.005,
        "gradient_steps": 4,
        "target_entropy": "auto",
        "net_arch": dict(pi=[512, 512, 256], qf=[512, 512, 256]),
        "reward_scale": 0.05,
        "checkpoint_freq": 50_000,
        "learning_rate": 5e-5,
        "gamma": 0.95,
        "ent_coef": 0.2,
    },
}


# ====================================================================
# 自定义组件
# ====================================================================

class ClippedAdam(torch.optim.Adam):
    """Adam optimizer with gradient clipping applied before each step."""

    def __init__(self, *args, max_grad_norm: float = 10.0, **kwargs):
        super().__init__(*args, **kwargs)
        self.max_grad_norm = max_grad_norm

    def step(self, closure=None):
        if self.max_grad_norm is not None and self.max_grad_norm > 0:
            for group in self.param_groups:
                torch.nn.utils.clip_grad_norm_(
                    group["params"], self.max_grad_norm
                )
        super().step(closure)


class LayerNormMlpExtractor(BaseFeaturesExtractor):
    """MLP feature extractor with LayerNorm after each hidden layer."""

    def __init__(self, observation_space, net_arch=None, activation_fn=nn.ReLU):
        if net_arch is None:
            net_arch = [512, 512, 256]
        super().__init__(observation_space, net_arch[-1])

        layers = []
        prev_dim = int(observation_space.shape[0])
        for hidden_dim in net_arch:
            layers.append(nn.Linear(prev_dim, hidden_dim))
            layers.append(nn.LayerNorm(hidden_dim))
            layers.append(activation_fn())
            prev_dim = hidden_dim
        self.mlp = nn.Sequential(*layers)

    def forward(self, obs):
        return self.mlp(obs)


class RewardScaledVecEnv(BaseVecEnv):
    """向量化环境的奖励缩放包装器。"""

    def __init__(self, venv, scale=0.05):
        self.venv = venv
        self.scale = scale
        super().__init__(num_envs=venv.num_envs,
                         observation_space=venv.observation_space,
                         action_space=venv.action_space)

    def step_async(self, actions):
        self.venv.step_async(actions)

    def step_wait(self):
        obs, rewards, dones, infos = self.venv.step_wait()
        return obs, rewards * self.scale, dones, infos

    def reset(self):
        return self.venv.reset()

    def close(self):
        self.venv.close()

    def env_method(self, *args, **kwargs):
        return self.venv.env_method(*args, **kwargs)

    def get_attr(self, *args, **kwargs):
        return self.venv.get_attr(*args, **kwargs)

    def set_attr(self, *args, **kwargs):
        return self.venv.set_attr(*args, **kwargs)

    def env_is_wrapped(self, *args, **kwargs):
        return self.venv.env_is_wrapped(*args, **kwargs)

    def seed(self, seed=None):
        return self.venv.seed(seed)

    def render(self, *args, **kwargs):
        return self.venv.render(*args, **kwargs)

    def __getattr__(self, name):
        if 'venv' in self.__dict__:
            return getattr(self.venv, name)
        raise AttributeError(name)


class TensorboardMetricsCallback(BaseCallback):
    """记录 episode 指标 + SAC 诊断，含双 Q 网络独立性验证。"""

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

    def _record_reward_components(self, info: dict) -> None:
        candidates = info.get("reward_components") or info.get("LastRewardComponents")
        if isinstance(candidates, dict):
            for key, value in candidates.items():
                scalar = self._to_scalar(value)
                if scalar is not None:
                    self.logger.record(f"reward_components/{key}", scalar)
            return
        for key, value in info.items():
            if not isinstance(key, str):
                continue
            if key.startswith("reward/") or key.startswith("reward_components/"):
                scalar = self._to_scalar(value)
                if scalar is not None:
                    self.logger.record(key, scalar)

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
            self._record_reward_components(info)

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

            # 双 Q 网络独立性记录
            q1, q2 = model.critic(obs_tensor, actions_pi)
            q_min = torch.min(torch.cat([q1, q2], dim=1), dim=1, keepdim=True)[0]

            entropy_value = float((-log_prob).mean().item())
            self.logger.record("train/entropy", entropy_value)
            self.logger.record("train/q_value_mean", float(q_min.mean().item()))
            self.logger.record("train/q_value_min", float(q_min.min().item()))
            self.logger.record("train/q_value_max", float(q_min.max().item()))
            self.logger.record("train/q1_mean", float(q1.mean().item()))
            self.logger.record("train/q2_mean", float(q2.mean().item()))
            self.logger.record("train/q1_q2_diff", float((q1 - q2).abs().mean().item()))

            ent_coef_value = None
            if getattr(model, "log_ent_coef", None) is not None:
                ent_coef_value = float(torch.exp(model.log_ent_coef.detach()).mean().item())
            elif getattr(model, "ent_coef_tensor", None) is not None:
                ent_coef_value = float(model.ent_coef_tensor.detach().mean().item())
            if ent_coef_value is not None:
                self.logger.record("train/ent_coef_value", ent_coef_value)


# ====================================================================
# 工具函数
# ====================================================================

def signal_handler(sig, frame):
    """处理 Ctrl+C 中断信号，保存模型并安全退出"""
    print("\n⚠️ 收到中断信号，正在保存模型并关闭环境...")
    try:
        if _model is not None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            save_path = _output_dir / f"orange_robot_sac_interrupted_{_training_id}_{timestamp}.zip"
            _model.save(str(save_path))

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
    training_dirs = [d for d in output_dir.iterdir() if d.is_dir()]
    training_nums = []
    for d in training_dirs:
        if d.name.startswith("training_"):
            try:
                num = int(d.name.split("_")[1])
                training_nums.append(num)
            except (ValueError, IndexError):
                pass
    next_num = max(training_nums) + 1 if training_nums else 1
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


# ====================================================================
# 主训练流程
# ====================================================================

def main() -> None:
    global _model, _env, _output_dir, _training_id, _start_time

    total_timesteps = 1_500_000
    project_root = Path(__file__).resolve().parent.parent
    base_output_dir = project_root / "Training" / "Robot"
    base_output_dir.mkdir(parents=True, exist_ok=True)
    training_mode, resume_model_path = prompt_training_mode(base_output_dir)

    _training_id = get_next_training_id(base_output_dir)
    _output_dir = base_output_dir / _training_id
    _output_dir.mkdir(parents=True, exist_ok=True)
    _start_time = datetime.now()

    sac = SAC_DEFAULTS["orangerobot"].copy()

    config = {
        "training_id": _training_id,
        "start_time": _start_time.strftime("%Y-%m-%d %H:%M:%S"),
        "total_timesteps": total_timesteps,
        "algorithm": "SAC",
        "training_mode": training_mode,
        "resume_from": str(resume_model_path) if resume_model_path is not None else None,
        "sac_params": sac,
    }
    config_path = _output_dir / "training_config.json"
    with open(config_path, 'w') as f:
        json.dump(config, f, indent=2)

    print(f"🎯 开始训练: {_training_id}")
    print(f"📁 输出目录: {_output_dir}")
    print(f"⚙️  SAC 参数: lr={sac['learning_rate']}, γ={sac['gamma']}, "
          f"ent_coef={sac['ent_coef']}, grad_steps={sac['gradient_steps']}, "
          f"fall_penalty_horizon=5")
    if training_mode == "resume" and resume_model_path is not None:
        print(f"📦 继续训练权重: {resume_model_path}")
    else:
        print("🆕 本次为全新训练")

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    env = make_env(verbosity=1)
    reward_scale = sac["reward_scale"]
    if reward_scale is not None:
        env = RewardScaledVecEnv(env, scale=reward_scale)
    _env = env

    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"🚀 使用设备: {device}")
    if device == "cuda":
        print(f"    GPU 型号: {torch.cuda.get_device_name(0)}")

    action_dim = int(env.action_space.shape[0])
    target_entropy = sac["target_entropy"] if sac["target_entropy"] != "auto" else -float(action_dim)
    print(f"    action_dim={action_dim}, target_entropy={target_entropy:.1f}")

    checkpoint_dir = _output_dir / "checkpoints"
    checkpoint_dir.mkdir(exist_ok=True)

    checkpoint_callback = CheckpointCallback(
        save_freq=sac["checkpoint_freq"],
        save_path=str(checkpoint_dir),
        name_prefix="orange_robot_sac",
        save_replay_buffer=True,
        save_vecnormalize=True,
        verbose=1,
    )
    metrics_callback = TensorboardMetricsCallback()
    callbacks = [checkpoint_callback, metrics_callback]

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
                learning_rate=sac["learning_rate"],
                buffer_size=sac["buffer_size"],
                learning_starts=sac["learning_starts"],
                batch_size=sac["batch_size"],
                tau=sac["tau"],
                gamma=sac["gamma"],
                train_freq=1,
                gradient_steps=sac["gradient_steps"],
                ent_coef=sac["ent_coef"],
                target_update_interval=1,
                target_entropy=target_entropy,
                device=device,
                tensorboard_log=str(_output_dir / "tensorboard"),
                policy_kwargs=dict(
                    features_extractor_class=LayerNormMlpExtractor,
                    features_extractor_kwargs=dict(net_arch=[512, 512, 256]),
                    net_arch=[],
                    optimizer_class=ClippedAdam,
                    optimizer_kwargs=dict(max_grad_norm=10.0),
                ),
            )
            print("🆕 已创建新的 SAC 模型")
            print("    LayerNorm ✓ | ClippedAdam(max_grad_norm=10.0) ✓ | ent_coef=0.2 ✓")

        _model = model

        model.learn(
            total_timesteps=total_timesteps,
            log_interval=10,
            callback=callbacks,
            reset_num_timesteps=(training_mode != "resume"),
        )

        end_time = datetime.now()
        timestamp = end_time.strftime("%Y%m%d_%H%M%S")

        final_model_path = _output_dir / f"orange_robot_sac_final_{timestamp}.zip"
        model.save(str(final_model_path))

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
