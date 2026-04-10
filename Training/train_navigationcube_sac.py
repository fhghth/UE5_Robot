import signal
import sys
from pathlib import Path
from datetime import datetime
import json

import torch
from stable_baselines3 import SAC
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


def signal_handler(sig, frame):
    """处理 Ctrl+C 中断信号，保存模型并安全退出"""
    print("\n⚠️ 收到中断信号，正在保存模型并关闭环境...")
    try:
        if _model is not None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            save_path = _output_dir / f"navigation_cube_sac_interrupted_{_training_id}_{timestamp}.zip"

            _model.save(str(save_path))

            metadata = {
                "training_id": _training_id,
                "timestamp": timestamp,
                "status": "interrupted",
                "interruption_time": _start_time.strftime("%Y-%m-%d %H:%M:%S") if _start_time else None,
                "interruption_reason": "SIGINT" if sig == signal.SIGINT else "SIGTERM",
            }
            metadata_path = _output_dir / f"training_metadata_{_training_id}.json"
            with open(metadata_path, "w", encoding="utf-8") as f:
                json.dump(metadata, f, indent=2, ensure_ascii=False)

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


def main() -> None:
    global _model, _env, _output_dir, _training_id, _start_time

    total_timesteps = 300_000
    base_output_dir = Path("Training") / "NavigationCube"
    base_output_dir.mkdir(parents=True, exist_ok=True)

    _training_id = get_next_training_id(base_output_dir)
    _output_dir = base_output_dir / _training_id
    _output_dir.mkdir(parents=True, exist_ok=True)

    _start_time = datetime.now()

    config = {
        "training_id": _training_id,
        "start_time": _start_time.strftime("%Y-%m-%d %H:%M:%S"),
        "total_timesteps": total_timesteps,
        "algorithm": "SAC",
        "environment": "NavigationCube",
        "observation_dim": 4,
        "action_dim": 2,
    }

    config_path = _output_dir / "training_config.json"
    with open(config_path, "w", encoding="utf-8") as f:
        json.dump(config, f, indent=2, ensure_ascii=False)

    print(f"🎯 开始训练 NavigationCube: {_training_id}")
    print(f"📁 输出目录: {_output_dir}")

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    env = make_env(verbosity=1)
    _env = env

    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"🚀 使用设备: {device}")
    if device == "cuda":
        print(f"    GPU 型号: {torch.cuda.get_device_name(0)}")
        batch_size = 512
        print(f"    调整 batch_size 为 {batch_size}")
    else:
        batch_size = 256
        print("    未检测到 GPU，使用 CPU 训练，batch_size=256")

    checkpoint_dir = _output_dir / "checkpoints"
    checkpoint_dir.mkdir(exist_ok=True)

    checkpoint_callback = CheckpointCallback(
        save_freq=25_000,
        save_path=str(checkpoint_dir),
        name_prefix="navigation_cube_sac",
        save_replay_buffer=True,
        save_vecnormalize=True,
        verbose=1,
    )

    try:
        model = SAC(
            policy="MlpPolicy",
            env=env,
            verbose=1,
            learning_rate=3e-4,
            buffer_size=200_000,
            learning_starts=5_000,
            batch_size=batch_size,
            tau=0.005,
            gamma=0.99,
            train_freq=1,
            gradient_steps=1,
            ent_coef="auto",
            target_update_interval=1,
            target_entropy="auto",
            device=device,
        )
        _model = model

        model.learn(
            total_timesteps=total_timesteps,
            log_interval=10,
            callback=checkpoint_callback,
        )

        end_time = datetime.now()
        timestamp = end_time.strftime("%Y%m%d_%H%M%S")

        final_model_path = _output_dir / f"navigation_cube_sac_final_{timestamp}.zip"
        model.save(str(final_model_path))

        metadata = {
            "training_id": _training_id,
            "start_time": _start_time.strftime("%Y-%m-%d %H:%M:%S"),
            "end_time": end_time.strftime("%Y-%m-%d %H:%M:%S"),
            "duration_seconds": (end_time - _start_time).total_seconds(),
            "status": "completed",
            "total_timesteps": total_timesteps,
            "final_model": str(final_model_path.relative_to(base_output_dir)),
            "device_used": device,
            "environment": "NavigationCube",
        }

        metadata_path = _output_dir / f"training_metadata_{_training_id}.json"
        with open(metadata_path, "w", encoding="utf-8") as f:
            json.dump(metadata, f, indent=2, ensure_ascii=False)

        print("🎉 训练完成!")
        print(f"📦 最终模型已保存至: {final_model_path}")
        print(f"📄 训练元数据已保存至: {metadata_path}")
        print(f"⏱️  训练时长: {metadata['duration_seconds']:.2f} 秒")

    except KeyboardInterrupt:
        print("\n⚠️ KeyboardInterrupt 捕获，退出中...")
    except Exception as e:
        print(f"\n❌ 训练过程中发生错误: {e}")

        if _model is not None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            error_model_path = _output_dir / f"navigation_cube_sac_error_{timestamp}.zip"
            _model.save(str(error_model_path))
            print(f"⚠️ 错误模型已保存至: {error_model_path}")

    finally:
        if env is not None:
            env.close()
            print("🔒 环境已关闭。")


if __name__ == "__main__":
    main()

