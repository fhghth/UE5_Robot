# OrangeRobot 训练前端重构 - 进度报告

## ✅ 已完成的工作

### 1. 前端组件开发 (Vue 3)

#### 核心组件
- ✅ **TrainingTypeSelector.vue** - 训练类型选择组件（双足机器人/导航）
- ✅ **MapSelector.vue** - 地图选择组件，支持缩略图显示
- ✅ **ParamCard.vue** - 参数组卡片（黑色按钮样式）
- ✅ **ParamInput.vue** - 参数输入组件（支持 number/select/checkbox）
- ✅ **ParamPanel.vue** - 参数面板（展开显示参数列表）
- ✅ **TrainingControls.vue** - 训练控制按钮（开始/停止/查看日志）
- ✅ **TensorBoardModal.vue** - TensorBoard 日志查看模态框

#### 主视图
- ✅ **TrainView.vue** - 主视图，集成所有组件
  - 步骤流程控制（选类型 → 选地图 → 配置参数）
  - 动画过渡效果
  - 参数卡片互斥展开逻辑
  - 训练状态管理
  - 参数持久化（localStorage）

### 2. 配置文件
- ✅ **paramGroups.js** - 完整的参数配置
  - 8个参数组，共 48 个参数
  - 包含参数类型、默认值、范围、说明
  - 分组：基础训练、环境、核心奖励、步态、正则化、躯干稳定、动作、观测

### 3. 功能实现
- ✅ 训练类型选择（带动画）
- ✅ 地图选择（支持缩略图）
- ✅ 参数配置（卡片式折叠布局）
- ✅ 开始/停止训练
- ✅ 查看训练日志（TensorBoard）
- ✅ 参数持久化

---

## 🔄 待完成的工作

### 1. 后端 API 开发

#### 需要添加的接口

**a. 地图列表接口**
```python
@app.get("/api/maps")
async def get_maps(type: str = Query(...)):
    """
    返回指定训练类型的地图列表
    
    参数:
        type: 'biped' | 'navigation'
    
    返回:
        [
            {
                "id": "map1",
                "name": "基础竞技场",
                "path": "/Game/地图/OrangeRobotTrain",
                "thumbnail": "/maps/map1.jpg",
                "description": "适合初学者的基础训练场地"
            }
        ]
    """
```

**b. TensorBoard 接口**
```python
@app.get("/api/tensorboard/{job_id}")
async def get_tensorboard_url(job_id: str):
    """
    启动 TensorBoard 并返回访问 URL
    
    返回:
        {
            "url": "http://localhost:6006"
        }
    """
```

**c. 更新 start_training 接口**
- 接收所有 48 个参数
- 生成 env_config.json
- 传递给 train_headless.py

### 2. C++ 配置加载器

**需要更新 EnvConfigLoader.cpp**，添加以下参数的加载：
- DualFootShufflePenaltyScale
- SwingFootMinHeight
- SwingFootHeightPenaltyScale
- SingleSupportBonusReward
- SameLegDominancePenaltyScale
- ActionMagnitudePenaltyScale
- TrunkSupportOffsetPenaltyScale
- TrunkSupportOffsetNormalizeDistance
- TrunkAngVelXYPenaltyScale
- TrunkVerticalVelocityPenaltyScale
- TrunkVerticalVelocityDeadzone
- HeightDropPenaltyScale
- MinStepFrequencyHz
- MaxStepFrequencyHz
- FootImpactVelocityThreshold
- TrunkHeightNormalization
- DesiredStepPeriod

### 3. 地图缩略图准备

**需要在 UE5 中截图并保存到：**
```
Vue/RobotWeb/public/maps/
├── orangerobot_train.jpg  (基础竞技场)
├── main.jpg               (复杂竞技场)
└── ...
```

**截图建议：**
- 分辨率：800x600 或 1280x720
- 格式：JPG 或 PNG
- 视角：俯视或斜视，能看清地图全貌

### 4. 测试与优化

**需要测试的流程：**
1. 选择训练类型 → 动画是否流畅
2. 选择地图 → 缩略图是否正确显示
3. 配置参数 → 卡片展开/收起是否正常
4. 开始训练 → 是否成功提交到后端
5. 停止训练 → 按钮状态是否正确切换
6. 查看日志 → TensorBoard 是否正常打开

**需要优化的功能：**
- 参数验证（范围检查）
- 错误提示（Toast 通知）
- 加载状态（Loading 动画）
- 成功提示

---

## 📋 后续开发步骤

### 第一步：后端 API 开发（优先级：高）

1. 修改 `Test/server.py`
   ```python
   # 添加 /api/maps 接口
   # 添加 /api/tensorboard/{job_id} 接口
   # 更新 /api/start_training 接口，接收所有参数
   ```

2. 更新 `Test/train_headless.py`
   ```python
   # 确保能正确读取 env_config_path
   # 传递给 UE5 命令行
   ```

### 第二步：C++ 配置加载器（优先级：高）

1. 更新 `Source/OrangeRobot/Private/EnvConfigLoader.cpp`
   ```cpp
   // 添加所有缺失参数的 HasField 检查和赋值
   ```

### 第三步：准备地图资源（优先级：中）

1. 在 UE5 中截取地图缩略图
2. 保存到 `Vue/RobotWeb/public/maps/` 目录
3. 更新后端地图列表数据

### 第四步：测试与调试（优先级：中）

1. 启动前端：`npm run dev`
2. 启动后端：`python Test/server.py`
3. 测试完整流程
4. 修复发现的问题

### 第五步：优化用户体验（优先级：低）

1. 添加参数验证
2. 添加 Toast 通知
3. 添加 Loading 动画
4. 优化错误提示

---

## 🎯 快速启动指南

### 前端开发
```bash
cd Vue/RobotWeb
npm install
npm run dev
```

### 后端开发
```bash
cd Test
python server.py
```

### 访问地址
- 前端：http://localhost:5173
- 后端：http://localhost:8000

---

## 📝 注意事项

1. **参数命名一致性**
   - 前端使用驼峰命名（如 `MaxSteps`）
   - 后端接收时需要保持一致
   - C++ 中的属性名也要匹配

2. **地图路径格式**
   - UE5 资产路径：`/Game/地图/OrangeRobotTrain`
   - 缩略图路径：`/maps/orangerobot_train.jpg`

3. **TensorBoard 端口管理**
   - 每个训练任务使用独立端口
   - 建议范围：6006-6020
   - 需要在后端维护端口映射表

4. **参数持久化**
   - 使用 localStorage 保存用户配置
   - 页面刷新后自动恢复
   - 可以添加"重置为默认值"功能

---

## 🐛 已知问题

1. **高级参数折叠面板未实现**
   - 当前所有参数都在主界面显示
   - 可以添加一个折叠面板放置不常用参数

2. **参数验证未实现**
   - 当前只有前端的 min/max 限制
   - 需要添加更严格的验证逻辑

3. **错误提示不够友好**
   - 当前使用 alert 弹窗
   - 建议使用 Toast 通知组件

---

## 📚 相关文档

- Vue 3 文档：https://vuejs.org/
- FastAPI 文档：https://fastapi.tiangolo.com/
- TensorBoard 文档：https://www.tensorflow.org/tensorboard

---

**最后更新：** 2025-05-03
**状态：** 前端开发完成 80%，后端 API 待开发
