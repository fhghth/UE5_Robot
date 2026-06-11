<template>
  <div class="page">
    <!-- ========== 顶部导航栏 ========== -->
    <header class="navbar">
      <div class="navbar__inner">
        <h1 class="navbar__title">部署测试</h1>
        <button class="btn-back" @click="goBack">← 返回首页</button>
      </div>
    </header>

    <!-- ========== 主内容区 ========== -->
    <main class="content">
      <div class="content-inner">
        <!-- Hero 区域 -->
        <section class="hero-panel">
          <h1 class="hero-title">部署模型测试</h1>
          <p class="hero-subtitle">Deploy Model Testing</p>

          <div class="hero-row">
            <div class="hero-logo-wrap">
              <img class="hero-logo" src="@/assets/logo.gif" alt="Orange Robot Logo" />
            </div>

            <div class="hero-content">
              <p class="hero-description">
                完成高层导航模型与低层步态模型配置后，可继续选择部署地图并启动测试流程。
              </p>

              <div class="hero-button-group">
                <button class="btn-primary" :disabled="!hasModelConfig" @click="scrollToMapSection">
                  选择部署地图
                </button>
                <button class="btn-secondary" @click="scrollToControlsSection">查看运行控制</button>
              </div>

              <div class="hero-status-row">
                <div class="hero-status-item">
                  <span class="hero-status-label">模型配置</span>
                  <span class="hero-status-value">
                    {{ hasModelConfig ? '已完成' : '待配置' }}
                  </span>
                </div>
                <div class="hero-status-item">
                  <span class="hero-status-label">当前状态</span>
                  <span class="hero-status-value">{{ statusText }}</span>
                </div>
                <div class="hero-status-item">
                  <span class="hero-status-label">地图选择</span>
                  <span class="hero-status-value">
                    {{ state.selectedMap?.name || '未选择' }}
                  </span>
                </div>
              </div>
            </div>
          </div>
        </section>

        <!-- 模型设置 -->
        <section class="section-card">
          <div class="section-head">
            <h2 class="section-title">模型设置</h2>
            <span class="section-subtitle">请选择部署所需的 ONNX 模型文件</span>
          </div>

          <div class="model-grid">
            <div class="field-group">
              <label class="field-label">高层导航模型</label>
              <div class="path-row">
                <input
                  v-model="state.highLevelOnnxPath"
                  class="path-input"
                  type="text"
                  placeholder="请选择高层导航 .onnx 文件"
                />
                <button
                  class="btn-secondary action-btn"
                  :disabled="isBusy"
                  @click="selectOnnxFile('high')"
                >
                  选择文件
                </button>
              </div>
            </div>

            <div class="field-group">
              <label class="field-label">低层步态模型</label>
              <div class="path-row">
                <input
                  v-model="state.lowLevelOnnxPath"
                  class="path-input"
                  type="text"
                  placeholder="请选择低层步态 .onnx 文件"
                />
                <button
                  class="btn-secondary action-btn"
                  :disabled="isBusy"
                  @click="selectOnnxFile('low')"
                >
                  选择文件
                </button>
              </div>
            </div>
          </div>

          <div class="model-actions">
            <div class="model-actions__hint">
              <span class="hint-badge">{{ hasModelConfig ? '已准备' : '待完成' }}</span>
              <span class="hint-text">完成模型配置后，继续选择地图并准备运行测试。</span>
            </div>
            <div class="model-actions__buttons">
              <button class="btn-primary" :disabled="!hasModelConfig" @click="scrollToMapSection">
                配置完成，选择地图
              </button>
              <button class="btn-secondary" @click="scrollToControlsSection">查看运行控制</button>
            </div>
          </div>
        </section>

        <!-- 地图选择 -->
        <section ref="mapSectionRef" class="section-card">
          <div class="section-head">
            <h2 class="section-title">地图选择</h2>
            <span class="section-subtitle">选择当前部署测试所使用的地图</span>
          </div>

          <MapSelector
            fetch-url="/api/deploy/maps"
            title="选择部署地图"
            :selected-map="state.selectedMap"
            @select="handleMapSelect"
          />

          <div class="map-footer">
            <div class="map-selected">
              <span class="map-selected__label">当前已选地图</span>
              <span class="map-selected__value">{{ state.selectedMap?.name || '未选择地图' }}</span>
            </div>
            <button
              class="btn-primary"
              :disabled="!state.selectedMap"
              @click="scrollToControlsSection"
            >
              前往运行控制
            </button>
          </div>
        </section>

        <!-- 运行控制 -->
        <section ref="controlsSectionRef" class="section-card controls-card">
          <div class="section-head">
            <h2 class="section-title">运行控制</h2>
            <span class="section-subtitle">查看当前会话状态并执行启动或停止操作</span>
          </div>

          <div class="runtime-board">
            <div class="runtime-main">
              <div class="runtime-state">
                <span class="runtime-state__label">运行状态</span>
                <span class="runtime-state__value" :class="`is-${state.runtimeStatus}`">
                  {{ statusText }}
                </span>
              </div>

              <div class="runtime-actions">
                <button
                  v-if="state.runtimeStatus !== 'running'"
                  class="btn-primary runtime-btn"
                  :disabled="
                    !canStartDeploy ||
                    state.runtimeStatus === 'starting' ||
                    state.runtimeStatus === 'stopping'
                  "
                  @click="startDeployTest"
                >
                  {{ state.runtimeStatus === 'starting' ? '启动中...' : '开始运行' }}
                </button>

                <button
                  v-else
                  class="btn-danger runtime-btn"
                  :disabled="state.runtimeStatus === 'stopping'"
                  @click="stopDeployTest"
                >
                  {{ state.runtimeStatus === 'stopping' ? '停止中...' : '停止运行' }}
                </button>
              </div>
            </div>

            <div class="runtime-summary">
              <div class="summary-item">
                <span class="summary-label">当前地图</span>
                <span class="summary-value">{{ state.selectedMap?.name || '未选择' }}</span>
              </div>
              <div class="summary-item">
                <span class="summary-label">高层模型</span>
                <span class="summary-value">{{ state.highLevelOnnxPath || '未配置' }}</span>
              </div>
              <div class="summary-item">
                <span class="summary-label">低层模型</span>
                <span class="summary-value">{{ state.lowLevelOnnxPath || '未配置' }}</span>
              </div>
              <div class="summary-item">
                <span class="summary-label">会话 ID</span>
                <span class="summary-value">{{ state.sessionId || '无' }}</span>
              </div>
            </div>
          </div>

          <div v-if="state.errorMessage" class="error-banner">
            {{ state.errorMessage }}
          </div>
        </section>
      </div>
    </main>
  </div>
</template>

<script setup>
import { computed, onMounted, reactive, ref } from 'vue'
import { useRouter } from 'vue-router'
import MapSelector from '@/components/MapSelector.vue'

const router = useRouter()
const STORAGE_KEY = 'deploy_test_state'

const mapSectionRef = ref(null)
const controlsSectionRef = ref(null)

const state = reactive({
  highLevelOnnxPath: '',
  lowLevelOnnxPath: '',
  selectedMap: null,
  runtimeStatus: 'idle',
  sessionId: null,
  errorMessage: '',
})

const statusTextMap = {
  idle: '未运行',
  starting: '启动中',
  running: '运行中',
  stopping: '停止中',
  stopped: '已停止',
}

const statusText = computed(() => statusTextMap[state.runtimeStatus] || state.runtimeStatus)
const isBusy = computed(() => ['starting', 'running', 'stopping'].includes(state.runtimeStatus))

const hasModelConfig = computed(() => {
  return !!state.highLevelOnnxPath && !!state.lowLevelOnnxPath
})

const canStartDeploy = computed(() => {
  return hasModelConfig.value && !!state.selectedMap
})

const getErrorMessage = (error, fallback) => {
  return error instanceof Error ? error.message : fallback
}

const saveLocalState = () => {
  localStorage.setItem(
    STORAGE_KEY,
    JSON.stringify({
      highLevelOnnxPath: state.highLevelOnnxPath,
      lowLevelOnnxPath: state.lowLevelOnnxPath,
      selectedMap: state.selectedMap,
    }),
  )
}

const loadLocalState = () => {
  const raw = localStorage.getItem(STORAGE_KEY)
  if (!raw) return

  try {
    const parsed = JSON.parse(raw)
    state.highLevelOnnxPath = parsed.highLevelOnnxPath || ''
    state.lowLevelOnnxPath = parsed.lowLevelOnnxPath || ''
    state.selectedMap = parsed.selectedMap || null
  } catch (error) {
    console.error('加载部署测试页面状态失败:', error)
  }
}

const scrollToTarget = (targetRef) => {
  if (!targetRef.value) return
  targetRef.value.scrollIntoView({
    behavior: 'smooth',
    block: 'start',
  })
}

const scrollToMapSection = () => {
  if (!hasModelConfig.value) return
  scrollToTarget(mapSectionRef)
}

const scrollToControlsSection = () => {
  scrollToTarget(controlsSectionRef)
}

const syncStatus = async () => {
  try {
    const response = await fetch('/api/deploy_test/status')
    if (!response.ok) return

    const data = await response.json()
    state.runtimeStatus = data.status === 'stopped' ? 'idle' : data.status
    state.sessionId = data.sessionId || null

    if (data.config) {
      state.highLevelOnnxPath = data.config.high_level_onnx_path || state.highLevelOnnxPath
      state.lowLevelOnnxPath = data.config.low_level_onnx_path || state.lowLevelOnnxPath
      if (data.config.map_name && data.config.map_path) {
        state.selectedMap = {
          id: data.config.map_name,
          name: data.config.map_name,
          path: data.config.map_path,
          thumbnail: data.config.thumbnail || '',
          description: data.config.description || '',
        }
      }
      saveLocalState()
    }
  } catch (error) {
    console.error('同步部署测试状态失败:', error)
  }
}

const selectOnnxFile = async (type) => {
  state.errorMessage = ''
  try {
    const response = await fetch('/api/dialog/select-onnx-file', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({}),
    })

    if (!response.ok) {
      throw new Error('选择 ONNX 文件失败')
    }

    const data = await response.json()
    if (type === 'high') {
      state.highLevelOnnxPath = data.path || ''
    } else {
      state.lowLevelOnnxPath = data.path || ''
    }
    saveLocalState()
  } catch (error) {
    state.errorMessage = getErrorMessage(error, '选择 ONNX 文件失败')
  }
}

const handleMapSelect = (map) => {
  state.selectedMap = map
  saveLocalState()
}

const startDeployTest = async () => {
  state.errorMessage = ''
  state.runtimeStatus = 'starting'
  saveLocalState()

  try {
    const response = await fetch('/api/start_deploy_test', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        high_level_onnx_path: state.highLevelOnnxPath,
        low_level_onnx_path: state.lowLevelOnnxPath,
        map_name: state.selectedMap?.name || '',
      }),
    })

    const data = await response.json()
    if (!response.ok) {
      throw new Error(data.detail || '启动部署测试失败')
    }

    state.sessionId = data.session_id
    state.runtimeStatus = 'running'
    alert(`部署测试已启动！\n会话ID: ${data.session_id}`)
  } catch (error) {
    state.runtimeStatus = 'idle'
    state.errorMessage = getErrorMessage(error, '启动部署测试失败')
  }
}

const stopDeployTest = async () => {
  if (!state.sessionId) return

  state.errorMessage = ''
  state.runtimeStatus = 'stopping'
  try {
    const response = await fetch('/api/stop_deploy_test', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        session_id: state.sessionId,
      }),
    })

    const data = await response.json()
    if (!response.ok) {
      throw new Error(data.detail || '停止部署测试失败')
    }

    state.runtimeStatus = 'idle'
    state.sessionId = null
    alert('部署测试已停止')
  } catch (error) {
    state.runtimeStatus = 'running'
    state.errorMessage = getErrorMessage(error, '停止部署测试失败')
  }
}

const goBack = () => {
  router.push('/')
}

onMounted(async () => {
  loadLocalState()
  await syncStatus()
})
</script>

<style scoped>
/* ========== 设计变量（暖橙体系） ========== */
.page {
  --color-brand: #d97757;
  --color-brand-hover: #c06845;
  --color-brand-light: #fdf0ea;
  --color-dark: #141413;
  --color-bg: #fbf9f7;
  --color-white: #ffffff;
  --color-text: #1a1a19;
  --color-text-secondary: #6b6b6b;
  --color-border: #e8e4df;
  --color-dashed: #cccccc;
  --color-danger: #b84a4a;
  --font-display: 'Poppins', -apple-system, BlinkMacSystemFont, sans-serif;
  --font-body: 'Lora', Georgia, 'Times New Roman', serif;
  --radius-sm: 8px;
  --radius-md: 12px;
  --radius-lg: 16px;
  --radius-button: 0.5em;
  --shadow-card: 0 2px 16px rgba(0, 0, 0, 0.06);
  --shadow-hover: 0 8px 24px rgba(0, 0, 0, 0.08);
  --transition: 0.25s ease;

  min-height: 100vh;
  background-color: var(--color-bg);
  font-family: var(--font-body);
  color: var(--color-text);
  display: flex;
  flex-direction: column;
}

/* ========== 导航栏 ========== */
.navbar {
  background: rgba(255, 255, 255, 0.8);
  backdrop-filter: blur(8px);
  -webkit-backdrop-filter: blur(8px);
  border-bottom: 2px dashed var(--color-dashed);
  padding: 0 24px;
  position: sticky;
  top: 0;
  z-index: 100;
}

.navbar__inner {
  max-width: 1400px;
  margin: 0 auto;
  height: 64px;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.navbar__title {
  font-family: var(--font-display);
  font-size: 20px;
  font-weight: 600;
  color: var(--color-dark);
  margin: 0;
}

.btn-back {
  background: transparent;
  color: var(--color-text-secondary);
  border: 1.5px dashed var(--color-dashed);
  border-radius: var(--radius-button);
  padding: 8px 20px;
  font-family: var(--font-display);
  font-size: 15px;
  font-weight: 500;
  cursor: pointer;
  transition: all var(--transition);
}

.btn-back:hover {
  color: var(--color-brand);
  border-color: var(--color-brand);
  background: rgba(217, 119, 87, 0.05);
}

/* ========== 主内容区 ========== */
.content {
  flex: 1;
  padding: 48px 24px 80px;
}

.content-inner {
  max-width: 1400px;
  margin: 0 auto;
  display: flex;
  flex-direction: column;
  gap: 32px;
}

/* ========== 卡片通用样式 ========== */
.hero-panel,
.section-card {
  background: var(--color-white);
  border: 1px solid var(--color-border);
  border-radius: var(--radius-lg);
  box-shadow: var(--shadow-card);
  transition: box-shadow var(--transition);
}

.section-card:hover {
  box-shadow: var(--shadow-hover);
}

.hero-panel {
  padding: 48px 32px 40px;
}

.section-card {
  padding: 36px 48px;
}

/* ========== Hero 区域（标题大幅左对齐） ========== */
.hero-title {
  font-family: var(--font-display);
  font-size: clamp(36px, 6vw, 72px);
  font-weight: 800;
  line-height: 1;
  text-align: left;
  color: var(--color-dark);
  margin: 0 0 12px;
  letter-spacing: -0.03em;
  word-break: break-word;
}

.hero-subtitle {
  font-family: var(--font-body);
  font-size: clamp(18px, 2.5vw, 22px);
  color: var(--color-text-secondary);
  text-align: left;
  margin: 0 0 40px;
  font-style: italic;
  font-weight: 400;
}

.hero-row {
  display: flex;
  align-items: flex-start;
  gap: 40px;
}

.hero-logo-wrap {
  flex: 0 0 180px;
  display: flex;
  justify-content: center;
}

.hero-logo {
  width: 160px;
  max-width: 100%;
  height: auto;
  display: block;
}

.hero-content {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.hero-description {
  font-size: 17px;
  line-height: 1.8;
  color: var(--color-text-secondary);
  margin: 0;
}

.hero-button-group {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 12px;
}

.hero-status-row {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 16px;
}

.hero-status-item {
  background: var(--color-bg);
  border: 1px solid var(--color-border);
  border-radius: var(--radius-md);
  padding: 18px 20px;
  display: flex;
  flex-direction: column;
  gap: 8px;
  transition: box-shadow var(--transition);
}

.hero-status-item:hover {
  box-shadow: var(--shadow-card);
}

.hero-status-label {
  font-size: 13px;
  color: var(--color-text-secondary);
  font-family: var(--font-display);
  font-weight: 500;
}

.hero-status-value {
  font-family: var(--font-display);
  font-size: 17px;
  font-weight: 700;
  color: var(--color-dark);
  word-break: break-word;
}

/* ========== 区块标题 ========== */
.section-head {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 16px;
  margin-bottom: 28px;
  padding-bottom: 16px;
  border-bottom: 1px solid var(--color-border);
}

.section-title {
  font-family: var(--font-display);
  font-size: 26px;
  font-weight: 700;
  color: var(--color-dark);
  margin: 0;
  letter-spacing: -0.01em;
}

.section-subtitle {
  font-size: 15px;
  color: var(--color-text-secondary);
  text-align: right;
  font-family: var(--font-body);
}

/* ========== 模型设置 ========== */
.model-grid {
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.field-group {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.field-label {
  font-family: var(--font-display);
  font-size: 15px;
  font-weight: 600;
  color: var(--color-text);
}

.path-row {
  display: flex;
  gap: 12px;
}

.path-input {
  flex: 1;
  min-width: 0;
  border: 1.5px solid var(--color-border);
  border-radius: var(--radius-sm);
  padding: 12px 16px;
  font-size: 14px;
  color: var(--color-text);
  background: #fff;
  transition: border-color var(--transition);
}

.path-input:focus {
  outline: none;
  border-color: var(--color-brand);
  box-shadow: 0 0 0 3px rgba(217, 119, 87, 0.1);
}

.model-actions {
  margin-top: 28px;
  padding-top: 20px;
  border-top: 1px dashed var(--color-border);
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 20px;
  flex-wrap: wrap;
}

.model-actions__hint {
  display: flex;
  align-items: center;
  gap: 12px;
}

.hint-badge {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 72px;
  padding: 6px 14px;
  border-radius: 999px;
  background: var(--color-brand-light);
  color: var(--color-brand);
  font-family: var(--font-display);
  font-size: 13px;
  font-weight: 600;
}

.hint-text {
  font-size: 14px;
  color: var(--color-text-secondary);
}

.model-actions__buttons {
  display: flex;
  align-items: center;
  gap: 12px;
}

/* ========== 地图选择 ========== */
.map-footer {
  margin-top: 28px;
  padding-top: 20px;
  border-top: 1px dashed var(--color-border);
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
}

.map-selected {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.map-selected__label {
  font-size: 13px;
  color: var(--color-text-secondary);
}

.map-selected__value {
  font-family: var(--font-display);
  font-size: 17px;
  font-weight: 700;
  color: var(--color-dark);
}

/* ========== 运行控制 ========== */
.controls-card {
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.runtime-board {
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.runtime-main {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 24px;
  background: var(--color-bg);
  border: 1px solid var(--color-border);
  border-radius: var(--radius-md);
  padding: 28px 32px;
  transition: box-shadow var(--transition);
}

.runtime-main:hover {
  box-shadow: var(--shadow-card);
}

.runtime-state {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.runtime-state__label {
  font-size: 15px;
  color: var(--color-text-secondary);
}

.runtime-state__value {
  font-family: var(--font-display);
  font-size: 32px;
  font-weight: 800;
  color: var(--color-dark);
  letter-spacing: -0.02em;
}

.runtime-state__value.is-running {
  color: var(--color-brand);
}

.runtime-state__value.is-starting,
.runtime-state__value.is-stopping {
  color: #8a6a1f;
}

.runtime-state__value.is-idle,
.runtime-state__value.is-stopped {
  color: var(--color-dark);
}

.runtime-actions {
  flex-shrink: 0;
}

.runtime-btn {
  min-width: 160px;
}

.runtime-summary {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 16px;
}

.summary-item {
  background: var(--color-white);
  border: 1px solid var(--color-border);
  border-radius: var(--radius-md);
  padding: 18px 20px;
  display: flex;
  flex-direction: column;
  gap: 8px;
  transition: box-shadow var(--transition);
}

.summary-item:hover {
  box-shadow: var(--shadow-card);
}

.summary-label {
  font-size: 13px;
  color: var(--color-text-secondary);
  font-family: var(--font-display);
  font-weight: 500;
}

.summary-value {
  font-family: var(--font-display);
  font-size: 15px;
  color: var(--color-dark);
  word-break: break-all;
  line-height: 1.5;
}

/* ========== 错误提示 ========== */
.error-banner {
  background: rgba(184, 74, 74, 0.08);
  color: #a13a2d;
  border: 1px solid rgba(184, 74, 74, 0.2);
  border-radius: var(--radius-md);
  padding: 16px 20px;
  font-size: 14px;
}

/* ========== 按钮体系 ========== */
.btn-primary,
.btn-secondary,
.btn-danger {
  border-radius: var(--radius-button);
  padding: 12px 24px;
  font-family: var(--font-display);
  font-size: 15px;
  font-weight: 600;
  cursor: pointer;
  transition: all var(--transition);
  display: inline-flex;
  align-items: center;
  justify-content: center;
  white-space: nowrap;
}

.btn-secondary {
  background: transparent;
  color: var(--color-text-secondary);
  border: 1.5px dashed var(--color-dashed);
}

.btn-secondary:hover:not(:disabled) {
  color: var(--color-brand);
  border-color: var(--color-brand);
  background: rgba(217, 119, 87, 0.04);
  transform: translateY(-1px);
}

.btn-primary {
  border: none;
  background: var(--color-dark);
  color: var(--color-white);
}

.btn-primary:hover:not(:disabled) {
  background: #2c2c2b;
  transform: translateY(-2px);
  box-shadow: 0 8px 20px rgba(20, 20, 19, 0.3);
}

.btn-danger {
  border: none;
  background: var(--color-danger);
  color: var(--color-white);
}

.btn-danger:hover:not(:disabled) {
  background: #a13f3f;
  transform: translateY(-2px);
  box-shadow: 0 8px 20px rgba(184, 74, 74, 0.3);
}

.btn-primary:disabled,
.btn-secondary:disabled,
.btn-danger:disabled {
  opacity: 0.5;
  cursor: not-allowed;
  transform: none;
  box-shadow: none;
}

.action-btn {
  white-space: nowrap;
}

/* ========== MapSelector 渗透样式 ========== */
:deep(.map-selector) {
  max-width: none;
  margin: 0;
  padding: 0;
}

:deep(.map-selector .section-title) {
  display: none;
}

:deep(.map-grid) {
  gap: 24px;
}

:deep(.map-card) {
  border-radius: var(--radius-md);
  transition: all var(--transition);
}

:deep(.map-card:hover) {
  transform: translateY(-4px);
  box-shadow: var(--shadow-hover);
}

:deep(.map-thumbnail) {
  background: var(--color-bg);
  border-radius: var(--radius-sm);
}

:deep(.placeholder) {
  background: var(--color-bg);
  border-radius: var(--radius-sm);
}

/* ========== 响应式 ========== */
@media (max-width: 1024px) {
  .hero-row {
    flex-direction: column;
    align-items: flex-start;
  }

  .hero-logo-wrap {
    margin: 0 auto;
  }

  .hero-button-group {
    justify-content: flex-start;
  }

  .hero-status-row {
    width: 100%;
  }

  .model-actions,
  .map-footer {
    flex-direction: column;
    align-items: stretch;
  }

  .model-actions__buttons {
    justify-content: flex-start;
  }

  .runtime-main {
    flex-direction: column;
    align-items: stretch;
  }

  .runtime-actions {
    display: flex;
    justify-content: stretch;
  }

  .runtime-btn {
    width: 100%;
  }
}

@media (max-width: 768px) {
  .content {
    padding: 32px 16px 64px;
  }

  .navbar {
    padding: 0 16px;
  }

  .hero-panel {
    padding: 36px 24px;
  }

  .section-card {
    padding: 28px 24px;
  }

  .section-head {
    flex-direction: column;
    align-items: flex-start;
  }

  .section-subtitle {
    text-align: left;
  }

  .path-row,
  .model-actions__buttons {
    flex-direction: column;
    align-items: stretch;
  }

  .hero-status-row,
  .runtime-summary {
    grid-template-columns: 1fr;
  }

  .btn-primary,
  .btn-secondary,
  .btn-danger {
    width: 100%;
  }

  .runtime-btn {
    width: 100%;
  }
}
</style>
