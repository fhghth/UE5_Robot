import signal
import sys
import os
import json
import argparse
import subprocess
import traceback
from pathlib import Path
from datetime import datetime

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


class ClippedAdam(Adam):

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

    def __init__(
        self,
        observation_space,
        net_arch=None,
        activation_fn=nn.ReLU,
    ):
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


# 全局变量，供信号处理器和清理使用
_model = None
_env = None
_output_dir = None
_job_dir = None
_training_id = None
_training_type = None
_model_prefix = "orange_robot_sac"
_start_time = None
_connection = None  # standalone 模式下为 subprocess.Popen 对象，editor 模式为 None


# 按训练类型区分的 SAC 默认超参数
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
    "navigation": {
        "buffer_size": 200_000,
        "learning_starts": 5_000,
        "batch_size": 512,
        "tau": 0.01,
        "gradient_steps": 2,
        "target_entropy": -2.0,
        "net_arch": None,
        "reward_scale": None,
        "checkpoint_freq": 25_000,
        "learning_rate": 1e-4,
    },
}


def _resolve_sac_defaults(config: dict) -> dict:
    """根据 training_type 解析 SAC 参数，config 中的显式值可覆盖默认"""
    training_type = config.get("training_type", "orangerobot")
    defaults = SAC_DEFAULTS.get(training_type, SAC_DEFAULTS["orangerobot"]).copy()

    for key in ("buffer_size", "learning_starts", "batch_size", "tau",
                 "gradient_steps", "checkpoint_freq", "learning_rate", "reward_scale",
                 "gamma", "ent_coef"):
        if key in config:
            defaults[key] = config[key]

    return defaults


def append_training_log(message: str):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    line = f"[{timestamp}] {message}"
    print(line)
    if _output_dir is None:
        return
    log_path = _output_dir / "training_runtime.log"
    with open(log_path, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def write_error_report(exc: Exception, env=None):
    if _output_dir is None:
        return None

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    error_report_path = _output_dir / f"training_error_{_training_id}_{timestamp}.log"
    traceback_text = traceback.format_exc()

    context = {
        "training_id": _training_id,
        "error_time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "exception_type": type(exc).__name__,
        "exception_message": str(exc),
        "connection_mode": "standalone" if _connection is not None else "editor",
        "ue_pid": _connection.pid if _connection is not None else None,
        "training_pid": os.getpid(),
        "start_time": _start_time.strftime("%Y-%m-%d %H:%M:%S") if _start_time else None,
        "output_dir": str(_output_dir),
        "job_dir": str(_job_dir) if _job_dir else None,
        "model_num_timesteps": getattr(_model, "num_timesteps", None) if _model is not None else None,
        "env_num_envs": getattr(env, "num_envs", None) if env is not None else None,
    }

    with open(error_report_path, "w", encoding="utf-8") as f:
        f.write("=== Training Error Context ===\n")
        json.dump(context, f, indent=2, ensure_ascii=False)
        f.write("\n\n=== Traceback ===\n")
        f.write(traceback_text)

    return error_report_path


def write_runtime_info(status: str = "running"):
    if _job_dir is None:
        return

    runtime_info = {
        "training_pid": os.getpid(),
        "ue_pid": _connection.pid if _connection is not None else None,
        "connection_mode": "standalone" if _connection is not None else "editor",
        "status": status,
        "training_id": _training_id,
        "updated_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
    }
    runtime_info_path = _job_dir / "runtime_process.json"
    with open(runtime_info_path, "w", encoding="utf-8") as f:
        json.dump(runtime_info, f, indent=2)



class RewardScaledVecEnv(BaseVecEnv):
    """
    向量化环境的奖励缩放包装器
    将每一步的奖励乘以 scale 系数
    """

    def __init__(self, venv, scale=0.05):
        self.venv = venv
        self.scale = scale
        super().__init__(
            num_envs=venv.num_envs,
            observation_space=venv.observation_space,
            action_space=venv.action_space,
        )

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
        if "venv" in self.__dict__:
            return getattr(self.venv, name)
        raise AttributeError(name)


class TensorboardMetricsCallback(BaseCallback):
    """记录更全面的 episode 指标，便于在 TensorBoard 分析训练过程"""

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
            q1, q2 = model.critic(obs_tensor, actions_pi)
            q1_mean = q1.mean().item()
            q2_mean = q2.mean().item()
            q_diff = (q1 - q2).abs().mean().item()

            self.logger.record("train/q1_mean", q1_mean)
            self.logger.record("train/q2_mean", q2_mean)
            self.logger.record("train/q1_q2_diff", q_diff)

            q_min = torch.min(torch.cat([q1, q2], dim=1), dim=1, keepdim=True)[0]

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
            save_path = _output_dir / f"{_model_prefix}_interrupted_{_training_id}_{timestamp}.zip"
            _model.save(str(save_path))

            metadata = {
                "training_id": _training_id,
                "timestamp": timestamp,
                "status": "interrupted",
                "interruption_time": _start_time.strftime("%Y-%m-%d %H:%M:%S") if _start_time else None,
                "interruption_reason": "SIGINT" if sig == signal.SIGINT else "SIGTERM",
            }
            metadata_path = _output_dir / f"training_metadata_{_training_id}.json"
            with open(metadata_path, "w") as f:
                json.dump(metadata, f, indent=2)

            print(f"✅ 模型已保存至 {save_path}")
            print(f"📄 训练元数据已保存至 {metadata_path}")

        write_runtime_info("stopping")
        if _env is not None:
            _env.close()
            append_training_log("环境已关闭")
        if _connection is not None:
            append_training_log(f"准备关闭 UE5 进程 | pid={_connection.pid}")
            _connection.terminate()
            try:
                _connection.wait(timeout=5)
            except subprocess.TimeoutExpired:
                _connection.kill()
            append_training_log("UE5 进程已终止")
    except Exception as e:
        print(f"❌ 保存或关闭时出错: {e}")
    sys.exit(0)


def main():
    global _model, _env, _output_dir, _job_dir, _training_id, _training_type, _model_prefix, _start_time, _connection

    # ------- 解析命令行参数 -------
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True, help="训练配置 JSON 文件路径")
    args = parser.parse_args()

    with open(args.config, "r") as f:
        config = json.load(f)

    # ------- 从配置中提取参数，设置默认值 -------
    _training_id = config.get("training_id", "training_001")
    _training_type = config.get("training_type", "orangerobot")
    training_type = _training_type
    _model_prefix = "navigation_cube_sac" if training_type == "navigation" else "orange_robot_sac"
    total_timesteps = config.get("total_timesteps", 1_500_000)
    sac = _resolve_sac_defaults(config)
    learning_rate = sac["learning_rate"]
    batch_size = sac["batch_size"]
    device = config.get("device", "cuda" if torch.cuda.is_available() else "cpu")
    connection_mode = config.get("connection_mode", "editor")
    training_mode = config.get("training_mode", "new")
    resume_model_path = config.get("resume_model_path", None)
    map_path = config.get("map_path", "/Game/Maps/Train")
    ue5_exe = config.get("ue5_exe", "D:/UE_5.5/Engine/Binaries/Win64/UnrealEditor.exe")
    ue5_project = config.get("ue5_project", None)
    env_config_path = config.get("env_config_path", None)
    port = config.get("port", 50051)
    reward_scale = sac["reward_scale"]
    headless_mode = config.get("headless_mode", False)
    display_logs = config.get("display_logs", True)
    set_fps = config.get("set_fps", None)
    disable_script = config.get("disable_script", True)

    # 输出目录（server 已经传入最终训练产物目录）
    _output_dir = Path(config.get("output_dir", "."))
    _job_dir = Path(config.get("job_dir", _output_dir))
    _output_dir.mkdir(parents=True, exist_ok=True)
    _job_dir.mkdir(parents=True, exist_ok=True)

    _start_time = datetime.now()
    append_training_log(
        f"训练启动 | training_id={_training_id} | mode={training_mode} | connection_mode={connection_mode} | device={device}"
    )

    # 保存一份实际使用的配置到输出目录
    effective_config = {
        "training_id": _training_id,
        "training_type": training_type,
        "start_time": _start_time.strftime("%Y-%m-%d %H:%M:%S"),
        "total_timesteps": total_timesteps,
        "learning_rate": learning_rate,
        "batch_size": batch_size,
        "device": device,
        "training_mode": training_mode,
        "resume_model_path": resume_model_path,
        "connection_mode": connection_mode,
        "map_path": map_path,
        "reward_scale": reward_scale,
        "sac_defaults": sac,
    }
    with open(_output_dir / "effective_config.json", "w") as f:
        json.dump(effective_config, f, indent=2)

    # ------- 根据连接模式创建模拟器与环境 -------
    if connection_mode == "standalone":
        if not ue5_project:
            raise RuntimeError("standalone 模式需要提供 ue5_project 路径")
        append_training_log(f"standalone 模式启动 UE5 | map={map_path}")
        # 手动构造命令行
        cmd_args = [
            ue5_exe,
            ue5_project,
            "-game",                     # 以游戏模式运行（无编辑器界面）
            "-UNATTENDED",
        ]
        if headless_mode:
            cmd_args.append("-nullRHI")
        else:
            cmd_args.append("-WINDOWED")
        cmd_args.append(map_path)
        if display_logs:
            cmd_args.append("-LOG")
        if set_fps is not None:
            cmd_args.extend(["-BENCHMARK", f"-FPS={set_fps}"])
        if disable_script:
            cmd_args.append("-ScholaDisableScript")
        if env_config_path:
            cmd_args.append(f"-EnvConfig={env_config_path}")
        cmd_args.append(f"-ScholaPort={port}")

        append_training_log(f"UE5 启动命令: {' '.join(cmd_args)}")
        _connection = subprocess.Popen(cmd_args)
        append_training_log(f"UE5 进程已启动 | pid={_connection.pid} | port={port}")
        # 使用 UnrealEditor 作为模拟器（仅占位，实际进程已由我们手动启动）
        simulator = UnrealEditor()
    else:
        append_training_log(f"editor 模式连接 UE | host=localhost | port={port} | map={map_path}")
        simulator = UnrealEditor()
        _connection = None

    write_runtime_info()
    append_training_log("运行时进程信息已写入")

    # 构建 gRPC 协议及向量化环境
    protocol = gRPCProtocol(
        url="localhost",
        port=port,
        environment_start_timeout=180,
    )
    env = VecEnv(simulator, protocol, verbosity=1)
    if reward_scale is not None:
        env = RewardScaledVecEnv(env, scale=reward_scale)
    _env = env

    print(f"🚀 使用设备: {device}")
    if device == "cuda":
        print(f"    GPU 型号: {torch.cuda.get_device_name(0)}")

    # ------- 注册信号处理器 -------
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # ------- 构建 SAC 模型 -------
    action_dim = int(env.action_space.shape[0])
    target_entropy = sac["target_entropy"] if sac["target_entropy"] != "auto" else -float(action_dim)
    net_arch = sac["net_arch"]
    print(f"    training_type={training_type} action_dim={action_dim} target_entropy={target_entropy} net_arch={net_arch}")
    print(f"    buffer_size={sac['buffer_size']} tau={sac['tau']} gradient_steps={sac['gradient_steps']}")

    checkpoint_prefix = _model_prefix

    checkpoint_dir = _output_dir / "checkpoints"
    checkpoint_dir.mkdir(exist_ok=True)

    checkpoint_callback = CheckpointCallback(
        save_freq=sac["checkpoint_freq"],
        save_path=str(checkpoint_dir),
        name_prefix=checkpoint_prefix,
        save_replay_buffer=True,
        save_vecnormalize=True,
        verbose=1,
    )
    metrics_callback = TensorboardMetricsCallback()
    callbacks = [checkpoint_callback, metrics_callback]

    if training_mode == "resume":
        if not resume_model_path:
            raise RuntimeError("resume 模式需要提供 resume_model_path")
        model = SAC.load(
            str(resume_model_path),
            env=env,
            device=device,
            tensorboard_log=str(_output_dir / "tensorboard"),
        )
        print(f"♻️ 已载入继续训练模型: {Path(resume_model_path).name}")
    else:
        sac_kwargs = dict(
            policy="MlpPolicy",
            env=env,
            verbose=1,
            learning_rate=learning_rate,
            buffer_size=sac["buffer_size"],
            learning_starts=sac["learning_starts"],
            batch_size=batch_size,
            tau=sac["tau"],
            gamma=sac.get("gamma", 0.95),
            train_freq=1,
            gradient_steps=sac["gradient_steps"],
            ent_coef=sac.get("ent_coef", 0.2),
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
        model = SAC(**sac_kwargs)
        print("🆕 已创建新的 SAC 模型")
    _model = model

    # ------- 开始训练 -------
    try:
        append_training_log(
            f"开始训练 | total_timesteps={total_timesteps} | reset_num_timesteps={training_mode != 'resume'}"
        )
        model.learn(
            total_timesteps=total_timesteps,
            log_interval=10,
            callback=callbacks,
            reset_num_timesteps=(training_mode != "resume"),
        )

        append_training_log("model.learn 执行完成")
        end_time = datetime.now()
        timestamp = end_time.strftime("%Y%m%d_%H%M%S")
        final_model_path = _output_dir / f"{_model_prefix}_final_{timestamp}.zip"
        model.save(str(final_model_path))

        metadata = {
            "training_id": _training_id,
            "start_time": _start_time.strftime("%Y-%m-%d %H:%M:%S"),
            "end_time": end_time.strftime("%Y-%m-%d %H:%M:%S"),
            "duration_seconds": (end_time - _start_time).total_seconds(),
            "status": "completed",
            "total_timesteps": total_timesteps,
            "final_model": final_model_path.name,
            "device_used": device,
        }
        with open(_output_dir / f"training_metadata_{_training_id}.json", "w") as f:
            json.dump(metadata, f, indent=2)

        print(f"🎉 训练完成!")
        print(f"📦 最终模型已保存至 {final_model_path}")
        print(f"⏱️  训练时长: {metadata['duration_seconds']:.2f} 秒")

    except KeyboardInterrupt:
        append_training_log("KeyboardInterrupt 捕获，准备退出")
    except Exception as e:
        error_report_path = write_error_report(e, env=env)
        append_training_log(f"训练过程中发生错误: {type(e).__name__}: {e}")
        if error_report_path is not None:
            append_training_log(f"错误详情已写入: {error_report_path}")
        if _model is not None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            error_model_path = _output_dir / f"{_model_prefix}_error_{timestamp}.zip"
            _model.save(str(error_model_path))
            append_training_log(f"错误模型已保存至: {error_model_path}")

    finally:
        append_training_log("进入 finally 清理阶段")
        write_runtime_info("stopped")
        if env is not None:
            env.close()
            append_training_log("环境已关闭")
        if _connection is not None:
            append_training_log(f"准备关闭 UE5 进程 | pid={_connection.pid}")
            _connection.terminate()
            try:
                _connection.wait(timeout=5)
            except subprocess.TimeoutExpired:
                _connection.kill()
            append_training_log("UE5 进程已终止")


if __name__ == "__main__":
    main()