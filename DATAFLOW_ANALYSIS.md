# 数据传输流程分析

## 📊 完整数据流

```
前端 (TrainView.vue)
    ↓ [HTTP POST /api/start_training]
    ↓ FormData: 48个参数 + level + training_type
    ↓
后端 (Services/server.py)
    ↓ 1. 创建 job_dir: Services/training_jobs/{job_id}/
    ↓ 2. 生成 env_config.json (48个UE5参数)
    ↓ 3. 生成 config.json (训练配置)
    ↓ 4. 启动 subprocess: conda run python Training/train_headless.py --config {config_path}
    ↓
训练脚本 (Training/train_headless.py)
    ↓ 1. 读取 config.json
    ↓ 2. 解析 output_dir (❌ 问题：重复添加 training_id)
    ↓ 3. 启动 UE5 (standalone 模式)
    ↓ 4. 连接 gRPC (端口 50051)
    ↓ 5. 开始训练
    ↓ 6. 保存模型到 _output_dir
    ↓
UE5 (OrangeRobotEnvComponent)
    ↓ 1. 读取命令行参数 -EnvConfig={env_config_path}
    ↓ 2. EnvConfigLoader 加载 env_config.json
    ↓ 3. 应用参数到组件
    ↓ 4. 通过 gRPC 与训练脚本通信
```

---

## 🔍 关键路径分析

### 1. server.py 路径配置

```python
# 第 19-24 行
BASE_DIR = Path(__file__).parent  # Services/
TRAIN_SCRIPT = str(BASE_DIR / "train_headless.py")  # ❌ 错误！
OUTPUT_DIR = Path(__file__).parent / "training_jobs"  # Services/training_jobs/

# 问题：TRAIN_SCRIPT 指向 Services/train_headless.py
# 实际位置：Training/train_headless.py
```

**修复：**
```python
TRAIN_SCRIPT = str(BASE_DIR.parent / "Training" / "train_headless.py")
```

---

### 2. server.py 创建任务目录

```python
# 第 169-171 行
job_id = datetime.now().strftime("training_%Y%m%d_%H%M%S")
job_dir = OUTPUT_DIR / job_id  # Services/training_jobs/training_20260503_115213/
job_dir.mkdir(parents=True, exist_ok=True)
```

**生成的目录结构：**
```
Services/training_jobs/training_20260503_115213/
├── config.json
└── env_config.json
```

**config.json 内容：**
```json
{
  "training_id": "training_20260503_115213",
  "output_dir": "d:\\uePro\\OrangeRobot\\Services\\training_jobs\\training_20260503_115213",
  "env_config_path": "d:\\uePro\\OrangeRobot\\Services\\training_jobs\\training_20260503_115213\\env_config.json",
  ...
}
```

---

### 3. train_headless.py 路径处理

```python
# 第 290-292 行 (❌ 问题代码)
output_root = Path(config.get("output_dir", "."))
# output_root = Path("d:\\...\\training_20260503_115213")

_output_dir = output_root / _training_id
# _output_dir = Path("d:\\...\\training_20260503_115213\\training_20260503_115213")  ❌ 重复！

_output_dir.mkdir(parents=True, exist_ok=True)
```

**实际创建的目录：**
```
Services/training_jobs/training_20260503_115213/training_20260503_115213/
├── effective_config.json
├── checkpoints/
└── tensorboard/
```

**问题：**
- 模型保存在错误的嵌套目录中
- TensorBoard 日志路径错误
- 后端无法找到正确的日志目录

---

## 🐛 问题汇总

### 问题 1：TRAIN_SCRIPT 路径错误

**位置：** `Services/server.py` 第 21 行

**当前代码：**
```python
TRAIN_SCRIPT = str(BASE_DIR / "train_headless.py")
# 结果：Services/train_headless.py ❌
```

**实际路径：**
```
Training/train_headless.py ✅
```

---

### 问题 2：输出目录重复嵌套

**位置：** `Training/train_headless.py` 第 290-292 行

**当前代码：**
```python
output_root = Path(config.get("output_dir", "."))
# output_root = "Services/training_jobs/training_20260503_115213"

_output_dir = output_root / _training_id
# _output_dir = "Services/training_jobs/training_20260503_115213/training_20260503_115213" ❌
```

**期望结果：**
```python
_output_dir = Path(config.get("output_dir", "."))
# _output_dir = "Services/training_jobs/training_20260503_115213" ✅
```

---

### 问题 3：停止训练按钮禁用

**位置：** `Vue/RobotWeb/src/components/TrainingControls.vue` 第 6 行

**当前代码：**
```html
<button :disabled="!canStart" ...>
```

**问题：**
- `trainingStatus === 'running'` 时，按钮文字变成"停止训练"
- 但 `canStart` 可能为 `false`，导致按钮被禁用

---

### 问题 4：模型保存路径错误

**位置：** `Training/train_headless.py` 第 430 行附近

**当前代码：**
```python
"final_model": str(final_model_path.relative_to(output_root))
```

**问题：**
- `output_root` 是错误的路径（包含重复的 training_id）
- `relative_to()` 会失败或返回错误的相对路径

---

## ✅ 修复方案

### 修复 1：更新 server.py 的 TRAIN_SCRIPT 路径

```python
# Services/server.py 第 21 行
# 修改前
TRAIN_SCRIPT = str(BASE_DIR / "train_headless.py")

# 修改后
TRAIN_SCRIPT = str(BASE_DIR.parent / "Training" / "train_headless.py")
```

---

### 修复 2：修复 train_headless.py 的输出目录

```python
# Training/train_headless.py 第 290-292 行
# 修改前
output_root = Path(config.get("output_dir", "."))
_output_dir = output_root / _training_id
_output_dir.mkdir(parents=True, exist_ok=True)

# 修改后
_output_dir = Path(config.get("output_dir", "."))
_output_dir.mkdir(parents=True, exist_ok=True)
# 不需要再添加 _training_id，server.py 已经创建了完整路径
```

同时修复模型保存路径（第 430 行附近）：

```python
# 修改前
"final_model": str(final_model_path.relative_to(output_root)),

# 修改后
"final_model": final_model_path.name,  # 只保存文件名
```

---

### 修复 3：修复停止训练按钮

```javascript
// Vue/RobotWeb/src/components/TrainingControls.vue

// 添加新的计算属性
const canClickButton = computed(() => {
  if (props.trainingStatus === 'running') {
    return true  // 训练中时，停止按钮应该可点击
  }
  return props.canStart  // 其他状态使用 canStart
})

// 修改模板
<button :disabled="!canClickButton" ...>
```

---

### 修复 4：添加 UE5 进程监控

```python
# Training/train_headless.py

# 添加新的 Callback 类
class UE5MonitorCallback(BaseCallback):
    def __init__(self, ue5_process, verbose=0):
        super().__init__(verbose)
        self.ue5_process = ue5_process
    
    def _on_step(self) -> bool:
        if self.ue5_process and self.ue5_process.poll() is not None:
            print("⚠️ UE5 进程已退出，停止训练并保存模型")
            return False  # 停止训练
        return True

# 在创建 callbacks 时添加
if _connection:
    ue5_monitor = UE5MonitorCallback(_connection)
    callbacks.append(ue5_monitor)
```

---

## 📁 修复后的目录结构

```
OrangeRobot/
├── Services/
│   ├── server.py  ✅ 修复 TRAIN_SCRIPT 路径
│   └── training_jobs/
│       └── training_20260503_115213/  ✅ 正确的输出目录
│           ├── config.json
│           ├── env_config.json
│           ├── effective_config.json
│           ├── checkpoints/
│           │   └── orange_robot_sac_50000_steps.zip
│           ├── tensorboard/
│           │   └── SAC_1/
│           └── orange_robot_sac_final_20260503_120000.zip
│
├── Training/
│   └── train_headless.py  ✅ 修复输出目录逻辑
│
└── Vue/RobotWeb/
    └── src/components/
        └── TrainingControls.vue  ✅ 修复按钮禁用逻辑
```

---

## 🎯 修复优先级

1. **高优先级（必须立即修复）**
   - ✅ 修复 TRAIN_SCRIPT 路径
   - ✅ 修复输出目录重复嵌套
   - ✅ 修复停止按钮禁用

2. **中优先级（建议修复）**
   - ✅ 添加 UE5 进程监控
   - ✅ 优化模型保存频率

3. **低优先级（可选）**
   - ⭐ 添加训练进度显示
   - ⭐ 优化 UE5 启动参数

---

## 📝 修复检查清单

- [ ] 修改 `Services/server.py` 第 21 行
- [ ] 修改 `Training/train_headless.py` 第 290-292 行
- [ ] 修改 `Training/train_headless.py` 第 430 行
- [ ] 修改 `Vue/RobotWeb/src/components/TrainingControls.vue`
- [ ] 添加 UE5MonitorCallback 类
- [ ] 测试完整训练流程
- [ ] 验证模型保存路径
- [ ] 验证 TensorBoard 日志路径

---

**准备好开始修复了吗？**
