from pathlib import Path

from stable_baselines3 import SAC

from schola.core.protocols.protobuf.gRPC import gRPCProtocol
from schola.core.simulators.unreal.editor import UnrealEditor
from schola.sb3.env import VecEnv


def make_env(verbosity: int =1) -> VecEnv:
 simulator = UnrealEditor()
 protocol = gRPCProtocol(url="localhost", port=50051, environment_start_timeout=180)
 return VecEnv(simulator, protocol, verbosity=verbosity)

def main() -> None:
 total_timesteps =1_000_000
 output_dir = Path("Training/checkpoints")
 output_dir.mkdir(parents=True, exist_ok=True)

 env = make_env(verbosity=1)
 try:
    model = SAC(
    policy="MlpPolicy",
    env=env,
    verbose=1,
    learning_rate=3e-4,
    buffer_size=500_000,
    learning_starts=10_000,
    batch_size=256,
    tau=0.005,
    gamma=0.99,
    train_freq=1,
    gradient_steps=1,
    ent_coef="auto",
    target_update_interval=1,
    target_entropy="auto",
    )

    model.learn(total_timesteps=total_timesteps, log_interval=10)
    model.save(str(output_dir / "orange_robot_sac_final"))
 finally:
    env.close()


if __name__ == "__main__":
 main()
