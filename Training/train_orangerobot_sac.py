import signal
import sys
from pathlib import Path

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

def signal_handler(sig, frame):
    """处理 Ctrl+C 中断信号，保存模型并安全退出"""
    print("\n⚠️ 收到中断信号，正在保存模型并关闭环境...")
    try:
        if _model is not None:
            save_path = _output_dir / "orange_robot_sac_interrupted.zip"
            _model.save(str(save_path))
            print(f"✅ 模型已保存至 {save_path}")
        if _env is not None:
            _env.close()
            print("✅ 环境已关闭")
    except Exception as e:
        print(f"❌ 保存或关闭时出错: {e}")
    sys.exit(0)

def make_env(verbosity: int = 1) -> VecEnv:
    simulator = UnrealEditor()
    protocol = gRPCProtocol(url="localhost", port=50051, environment_start_timeout=180)
    return VecEnv(simulator, protocol, verbosity=verbosity)

def main() -> None:
    global _model, _env, _output_dir

    total_timesteps = 1_000_000
    output_dir = Path("Training/checkpoints")
    output_dir.mkdir(parents=True, exist_ok=True)
    _output_dir = output_dir

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

    # 定期保存检查点（每 50000 步保存一次）
    checkpoint_callback = CheckpointCallback(
        save_freq=50_000,
        save_path=output_dir,
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
            device=device,          # 关键：指定训练设备
        )
        _model = model

        # 开始训练
        model.learn(
            total_timesteps=total_timesteps,
            log_interval=10,
            callback=checkpoint_callback,
        )
        # 训练正常结束，保存最终模型
        model.save(str(output_dir / "orange_robot_sac_final.zip"))
        print("🎉 训练完成，模型已保存。")

    except KeyboardInterrupt:
        print("\n⚠️ KeyboardInterrupt 捕获，退出中...")
    finally:
        if env is not None:
            env.close()
            print("🔒 环境已关闭。")

if __name__ == "__main__":
    main()