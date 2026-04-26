import torch
import numpy as np
from stable_baselines3 import SAC

# ========== 配置路径 ==========
MODEL_PATH = "Navigation/training_029/navigation_cube_sac_final_20260412_232936.zip"

# ========== 1. 加载模型 ==========
print("=" * 50)
print("加载模型:", MODEL_PATH)
model = SAC.load(MODEL_PATH, device="cpu")
print("✅ 模型加载成功\n")

# ========== 2. 观测与动作空间维度 ==========
obs_space = model.observation_space.shape
act_space = model.action_space.shape
print("=" * 50)
print("【空间维度】")
print(f"观测空间: {obs_space}  期望: (16,)")
print(f"动作空间: {act_space}  期望: (2,)")

# ========== 3. 检查 VecNormalize ==========
print("\n" + "=" * 50)
print("【归一化状态】")
vec_norm = model.get_vec_normalize_env()
if vec_norm is None:
    print("未使用 VecNormalize 包装")
else:
    print("检测到 VecNormalize，统计量：")
    print(f"  均值: {vec_norm.obs_rms.mean}")
    print(f"  方差: {vec_norm.obs_rms.var}")
    print("⚠️ 导出 ONNX 时必须固化归一化层！")

# ========== 4. 探测 actor 接口 ==========
print("\n" + "=" * 50)
print("【Actor 接口探测】")
actor = model.policy.actor
print(f"Actor 类型: {type(actor)}")
print(f"Actor 模块: \n{actor}\n")

# 构造 dummy 输入 (batch=1, dim=obs_space[0])
dummy_obs = torch.randn(1, obs_space[0])
print(f"dummy_obs.shape: {dummy_obs.shape}")

# 调用 actor 并分析返回值
with torch.no_grad():
    output = actor(dummy_obs)

print(f"\nactor(dummy_obs) 返回值类型: {type(output)}")
if isinstance(output, tuple):
    print(f"返回值是元组，长度: {len(output)}")
    for i, item in enumerate(output):
        print(f"  output[{i}] shape: {item.shape}, dtype: {item.dtype}")
else:
    print(f"返回值 shape: {output.shape}, dtype: {output.dtype}")

# 进一步尝试提取确定性动作
print("\n【确定性动作提取】")
try:
    # SB3 SAC 常用方式：直接取 mean 后 tanh
    mean, log_std = output if isinstance(output, tuple) else (output, None)
    deterministic_action = torch.tanh(mean)
    print(f"✅ 通过 tanh(mean) 获得确定性动作，shape: {deterministic_action.shape}")
    print(f"   动作范围: min={deterministic_action.min().item():.4f}, max={deterministic_action.max().item():.4f}")
except Exception as e:
    print(f"❌ 提取失败: {e}")
    # 备选：尝试 model.predict 方式（仅作参考，不用于 ONNX）
    print("\n备选：model.predict() 输出（numpy）:")
    pred_action, _ = model.predict(dummy_obs.numpy(), deterministic=True)
    print(f"   action shape: {pred_action.shape}, range: [{pred_action.min():.4f}, {pred_action.max():.4f}]")

# ========== 5. 最终结论 ==========
print("\n" + "=" * 50)
print("【诊断结论】")
if obs_space[0] == 16 and act_space[0] == 2:
    print("✅ 模型维度正确，可进行 16→2 ONNX 导出")
else:
    print(f"❌ 维度异常！观测应为 16，实际 {obs_space[0]}；动作应为 2，实际 {act_space[0]}")
if vec_norm is not None:
    print("⚠️ 存在 VecNormalize，导出时必须将归一化逻辑嵌入模型")