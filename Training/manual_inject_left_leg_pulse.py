import time
from pathlib import Path

import numpy as np
from schola.core.protocols.protobuf.gRPC import gRPCProtocol
from schola.core.simulators.unreal.editor import UnrealEditor
from schola.sb3.env import VecEnv

# ============================================================
# OrangeRobot 单轴脉冲注入测试脚本
#
# 目的：
# 1. 不走策略网络，直接向环境注入手工动作
# 2. 按用户修正后的约束顺序，优先扫描左腿相关动作维度
# 3. 通过正负脉冲观察哪一个维度会触发抬左腿
#
# 当前假设：
# - DriveConstraints 顺序为：
#   0=颈部
#   1=右锁骨(完全固定)
#   2=右肩
#   3=右肘
#   4=右手腕
#   5=右胯跟(全固定)
#   6=右髋
#   7=右膝
#   8=右踝
#   9=左锁骨(完全固定)
#   10=左肩
#   11=左肘
#   12=左手腕
#   13=左胯跟(全固定)
#   14=左髋
#   15=左膝
#   16=左踝
# - 动作展开顺序为：每个关节内部按 Twist -> Swing1 -> Swing2
# - 由于动作空间会跳过“完全固定/无驱动轴”的约束，无法直接从约束编号
#   精确映射到动作索引，因此这里采用“扫描动作向量末尾一段”的策略。
# - 左髋 / 左膝 / 左踝位于 DriveConstraints 末尾，因此它们在压缩后的动作向量里
#   大概率也靠近末尾。
# ============================================================

# -------- 连接参数 --------
HOST = "localhost"
PORT = 50051
ENV_START_TIMEOUT = 180
VERBOSITY = 1

# -------- 时序参数 --------
RESET_SETTLE_STEPS = 90      # reset 后先静置若干步
PRE_PULSE_ZERO_STEPS = 20   # 每个脉冲前先打 0 动作，确保前一轮影响尽量消退
PULSE_STEPS = 60            # 单次脉冲持续步数，拉长以便肉眼观察
HOLD_STEPS = 35             # 脉冲结束后维持 0 动作，方便观察回落
IDLE_BETWEEN_AXES = 30      # 不同维度之间额外静置步数
SLEEP_PER_STEP = 0.04       # 每步之间稍微暂停，方便在 UE 里肉眼观察
MAX_SCAN_AXES = 8           # 最多扫描多少个左腿候选维度
ASK_ENTER_BETWEEN_AXES = True

# -------- 幅值参数 --------
POSITIVE_PULSE = -1.0
NEGATIVE_PULSE = 1.0

# -------- 左腿候选动作维度 --------
#
# 重要：ApplyAction 使用的是“压缩后的动作向量”，完全固定的约束不会占动作维度。
# 你刚刚给出的顺序里，左腿在 DriveConstraints 末端：
#   14=左髋，15=左膝，16=左踝
# 因此这里不再假设它们一定对应 9~17，而是改成：
#   优先扫描动作向量末尾 8 个维度
# 这样即便前面的颈部、肩、肘、腕等也占掉了部分动作轴，仍然更容易命中左腿。
#
# 例如 action_dim=20 时，会扫描 [12,13,14,15,16,17,18,19]
# 例如 action_dim=14 时，会扫描 [6,7,8,9,10,11,12,13]
TAIL_SCAN_AXIS_COUNT = 9


def make_env(verbosity: int = VERBOSITY) -> VecEnv:
    simulator = UnrealEditor()
    protocol = gRPCProtocol(
        url=HOST,
        port=PORT,
        environment_start_timeout=ENV_START_TIMEOUT,
    )
    return VecEnv(simulator, protocol, verbosity=verbosity)


def zero_action(action_dim: int) -> np.ndarray:
    return np.zeros(action_dim, dtype=np.float32)


def step_idle(env: VecEnv, action_dim: int, steps: int, note: str) -> None:
    action = zero_action(action_dim)
    for step_idx in range(steps):
        env.step([action])
        if step_idx == 0:
            print(f"  -> {note}，持续 {steps} 步")
        time.sleep(SLEEP_PER_STEP)


def wait_for_enter(prompt: str) -> None:
    if ASK_ENTER_BETWEEN_AXES:
        input(prompt)


def pulse_axis(
    env: VecEnv,
    action_dim: int,
    axis_index: int,
    amplitude: float,
    pulse_steps: int,
    hold_steps: int,
    label: str,
) -> None:
    step_idle(env, action_dim, PRE_PULSE_ZERO_STEPS, f"维度 {axis_index:02d} 脉冲前归零")

    action = zero_action(action_dim)
    action[axis_index] = amplitude

    print(f"\n[脉冲] 维度 {axis_index:02d} | {label} | 幅值 {amplitude:+.2f} | 持续 {pulse_steps} 步")
    print(f"         非零动作: action[{axis_index}] = {amplitude:+.2f}")
    for step_idx in range(pulse_steps):
        env.step([action])
        if step_idx == 0:
            print("         已开始施加持续脉冲，请观察该轴是否带来持续位移/转动，而不是一闪而过。")
        time.sleep(SLEEP_PER_STEP)

    if hold_steps > 0:
        step_idle(env, action_dim, hold_steps, f"维度 {axis_index:02d} 脉冲结束后回零")


def build_left_leg_candidate_indices(action_dim: int) -> list[int]:
    start_index = max(0, action_dim - TAIL_SCAN_AXIS_COUNT)
    return list(range(start_index, action_dim))[:MAX_SCAN_AXES]
    # 测试单个维度
    # target_index = action_dim - 6
    # if target_index < 0:
    #     raise ValueError(f"action_dim={action_dim}，倒数第6个维度索引为负数，请检查动作空间大小")
    # return [target_index]


def get_action_dim(env: VecEnv) -> int:
    shape = env.action_space.shape
    if not shape:
        raise RuntimeError("未能从环境动作空间读取 shape")
    return int(shape[0])


def get_observation_dim(env: VecEnv) -> int:
    shape = env.observation_space.shape
    if not shape:
        raise RuntimeError("未能从环境观测空间读取 shape")
    return int(shape[0])


def main() -> None:
    project_root = Path(__file__).resolve().parent.parent
    print("=" * 70)
    print("OrangeRobot 左腿单轴脉冲注入测试")
    print(f"项目目录: {project_root}")
    print("说明：脚本不会加载 SAC 模型，而是直接给环境注入手工动作。")
    print("=" * 70)

    env = None
    try:
        env = make_env()
        obs = env.reset()

        action_dim = get_action_dim(env)
        obs_dim = get_observation_dim(env)
        print(f"环境连接成功，observation_dim={obs_dim}, action_dim={action_dim}")
        print(f"reset 返回类型: {type(obs)}")

        candidate_indices = build_left_leg_candidate_indices(action_dim)
        if not candidate_indices:
            raise RuntimeError(
                "action_dim 过小，未生成任何候选维度。"
            )

        print("\n将按以下候选维度扫描左腿动作（动作向量末尾优先）：")
        for idx in candidate_indices:
            print(f"  - {idx:02d}: 左腿候选轴(末尾扫描)")

        print("\n[阶段 1] reset 后静置")
        step_idle(env, action_dim, RESET_SETTLE_STEPS, "初始静置")

        print("\n[阶段 2] 开始逐轴扫描左腿")
        print("提示：现在每个维度都会先等待你确认，再进行正向/反向持续脉冲。")
        for scan_idx, axis_index in enumerate(candidate_indices, start=1):
            label = "左腿候选轴(末尾扫描)"
            print("\n" + "-" * 70)
            print(f"第 {scan_idx}/{len(candidate_indices)} 个候选维度 -> {axis_index:02d} | {label}")
            print("建议观察：是否出现左髋抬起、左膝收缩、左踝摆动、身体失衡方向。")

            wait_for_enter(f"按回车开始测试维度 {axis_index:02d} 的正向脉冲...")
            pulse_axis(
                env=env,
                action_dim=action_dim,
                axis_index=axis_index,
                amplitude=POSITIVE_PULSE,
                pulse_steps=PULSE_STEPS,
                hold_steps=HOLD_STEPS,
                label=label + " 正向",
            )

            wait_for_enter(f"按回车开始测试维度 {axis_index:02d} 的反向脉冲...")
            pulse_axis(
                env=env,
                action_dim=action_dim,
                axis_index=axis_index,
                amplitude=NEGATIVE_PULSE,
                pulse_steps=PULSE_STEPS,
                hold_steps=HOLD_STEPS,
                label=label + " 反向",
            )

            step_idle(env, action_dim, IDLE_BETWEEN_AXES, f"维度 {axis_index:02d} 扫描完成，进入下一维度前静置")

        print("\n[完成] 左腿候选维度扫描结束。")
        print("如果你观察到某个维度能明显触发左腿抬起，就把该维度索引和方向记下来。")
        print("下一步就可以基于该索引写“定向抬左腿模板动作脚本”。")

    finally:
        if env is not None:
            env.close()
            print("环境已关闭。")


if __name__ == "__main__":
    main()

