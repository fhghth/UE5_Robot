import signal
import sys
from pathlib import Path
from datetime import datetime
import json

import torch
from stable_baselines3 import SAC
from stable_baselines3.common.callbacks import CheckpointCallback
from schola.core.protocols.base import AutoResetType
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
    # protocol.send_startup_msg(auto_reset_type=AutoResetType.SAME_STEP)
    return VecEnv(simulator, protocol, verbosity=verbosity)


def main() -> None:
    global _model, _env, _output_dir, _training_id, _start_time

    total_timesteps = 1_000_000
    base_output_dir = Path("Training")
    base_output_dir.mkdir(parents=True, exist_ok=True)

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
        "algorithm": "SAC"
    }

    config_path = _output_dir / "training_config.json"
    with open(config_path, 'w') as f:
        json.dump(config, f, indent=2)

    print(f"🎯 开始训练: {_training_id}")
    print(f"📁 输出目录: {_output_dir}")

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

    try:
        model = SAC(
            policy="MlpPolicy",
            env=env,
            verbose=1,
            learning_rate=3e-4,
            buffer_size=500_000,
            learning_starts=10_000,
            batch_size=batch_size,
            tau=0.005,
            gamma=0.99,
            train_freq=1,
            gradient_steps=1,
            ent_coef="auto",
            target_update_interval=1,
            target_entropy="auto",
            device=device,  # 关键：指定训练设备
        )
        _model = model

        # 开始训练
        model.learn(
            total_timesteps=total_timesteps,
            log_interval=10,
            callback=checkpoint_callback,
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