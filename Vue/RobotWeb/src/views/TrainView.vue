<template>
  <div class="page">
    <header class="navbar">
      <div class="navbar__inner">
        <h1 class="navbar__title">开始训练</h1>
        <button class="btn-back" @click="goBack">← 返回首页</button>
      </div>
    </header>

    <main class="content">
      <div v-if="state.step > 1" class="step-actions">
        <button class="btn-step-back" @click="goToPreviousStep">← 上一步</button>
      </div>

      <TrainingTypeSelector v-if="state.step === 1" :selected-type="null" @select="handleTypeSelect" />

      <div v-if="state.step === 2" class="step-container">
        <MapSelector :training-type="state.trainingType" :selected-map="state.selectedMap" @select="handleMapSelect" />
      </div>

      <div v-if="state.step === 3" class="step-container">
        <div class="selection-summary">
          <span class="summary-item"><strong>训练类型:</strong> {{ getTypeName(state.trainingType) }}</span>
          <span class="summary-item"><strong>地图:</strong> {{ state.selectedMap?.name }}</span>
        </div>

        <div class="param-cards-grid">
          <ParamCard
            v-for="group in currentParamGroups"
            :key="group.id"
            :group="group"
            :is-active="state.activeParamGroup === group.id"
            @click="toggleParamGroup" />
        </div>

        <Transition name="slide-down">
          <ParamPanel v-if="state.activeParamGroup" :group="getCurrentGroup()" :params="state.params" @update="handleParamUpdate" />
        </Transition>

        <TrainingControls
          :training-status="state.trainingStatus"
          :can-start="canStartTraining"
          :job-id="state.jobId"
          @start="handleStartTraining"
          @stop="handleStopTraining"
          @view-logs="handleViewLogs" />

        <LogViewerModal
          :show="state.showLogViewerModal"
          :loading="state.logViewerLoading"
          @close="state.showLogViewerModal = false"
          @confirm="handleLogViewerConfirm" />
      </div>
    </main>

    <TrainingStartModal
      :show="state.showTrainingStartModal"
      :training-type="state.trainingType"
      @close="state.showTrainingStartModal = false"
      @confirm="handleStartTrainingConfirm" />

    <TensorBoardModal :show="state.showLogsModal" :job-id="state.jobId" :log-dir="state.selectedLogDir" @close="state.showLogsModal = false; state.selectedLogDir = null" />
  </div>
</template>

<script setup>
import { reactive, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import TrainingTypeSelector from '@/components/TrainingTypeSelector.vue'
import MapSelector from '@/components/MapSelector.vue'
import ParamCard from '@/components/ParamCard.vue'
import ParamPanel from '@/components/ParamPanel.vue'
import TrainingControls from '@/components/TrainingControls.vue'
import TrainingStartModal from '@/components/TrainingStartModal.vue'
import TensorBoardModal from '@/components/TensorBoardModal.vue'
import LogViewerModal from '@/components/LogViewerModal.vue'
import { orangeRobotParamGroups, orangeRobotDefaultParams } from '@/config/orangeRobotParamGroup.js'
import { navigationParamGroups, navigationDefaultParams } from '@/config/navigationParamGroups.js'

const router = useRouter()
const STORAGE_SELECTION_KEY = 'train_selection_state'
const STORAGE_KEYS = {
  orangerobot: 'train_params_orangerobot',
  navigation: 'train_params_navigation',
}

const state = reactive({
  step: 1,
  trainingType: null,
  selectedMap: null,
  activeParamGroup: null,
  trainingStatus: 'idle',
  jobId: null,
  showLogsModal: false,
  showLogViewerModal: false,
  logViewerLoading: false,
  selectedLogDir: null,
  showTrainingStartModal: false,
  params: {},
})

const getTypeName = (type) => {
  const types = {
    orangerobot: '双足机器人',
    navigation: '导航',
  }
  return types[type] || type
}

const getDefaultParamsByType = (type) => {
  return type === 'navigation' ? { ...navigationDefaultParams } : { ...orangeRobotDefaultParams }
}

const currentParamGroups = computed(() => {
  return state.trainingType === 'navigation' ? navigationParamGroups : orangeRobotParamGroups
})

const getStorageKeyByType = (type) => STORAGE_KEYS[type] || STORAGE_KEYS.orangerobot

const saveSelectionState = () => {
  localStorage.setItem(
    STORAGE_SELECTION_KEY,
    JSON.stringify({
      trainingType: state.trainingType,
      selectedMap: state.selectedMap,
    }),
  )
}

const saveParamsToLocalStorage = () => {
  if (!state.trainingType) return
  localStorage.setItem(getStorageKeyByType(state.trainingType), JSON.stringify(state.params))
}

const loadParamsFromLocalStorage = (type) => {
  const defaultParams = getDefaultParamsByType(type)
  const saved = localStorage.getItem(getStorageKeyByType(type))
  if (!saved) return defaultParams

  try {
    return { ...defaultParams, ...JSON.parse(saved) }
  } catch (error) {
    console.error('加载保存的参数失败:', error)
    return defaultParams
  }
}

const loadSelectionState = () => {
  const saved = localStorage.getItem(STORAGE_SELECTION_KEY)
  if (!saved) return

  try {
    const parsed = JSON.parse(saved)
    state.trainingType = parsed.trainingType || null
    state.selectedMap = parsed.selectedMap || null
    state.step = 1
    if (state.trainingType) {
      state.params = loadParamsFromLocalStorage(state.trainingType)
    }
  } catch (error) {
    console.error('加载训练选择状态失败:', error)
  }
}

const handleTypeSelect = (type) => {
  const typeChanged = state.trainingType !== type
  state.trainingType = type
  state.selectedMap = typeChanged ? null : state.selectedMap
  state.activeParamGroup = null
  state.params = loadParamsFromLocalStorage(type)
  state.step = 2
  saveSelectionState()
}

const handleMapSelect = (map) => {
  state.selectedMap = map
  state.step = 3
  saveSelectionState()
}

const goToPreviousStep = () => {
  if (state.step === 3) {
    state.step = 2
    state.activeParamGroup = null
  } else if (state.step === 2) {
    state.step = 1
  }
}

const toggleParamGroup = (groupId) => {
  state.activeParamGroup = state.activeParamGroup === groupId ? null : groupId
}

const getCurrentGroup = () => {
  return currentParamGroups.value.find((g) => g.id === state.activeParamGroup)
}

const handleParamUpdate = ({ key, value }) => {
  state.params[key] = value
  saveParamsToLocalStorage()
}

const canStartTraining = computed(() => {
  return state.trainingType && state.selectedMap && state.trainingStatus !== 'running'
})

const submitTraining = async ({ trainingMode, resumeModelPath, globalTrainingStep, resumeModelFile }) => {
  try {
    const formData = new FormData()
    formData.append('level', state.selectedMap.name)
    formData.append('training_type', state.trainingType)
    formData.append('training_mode', trainingMode)
    formData.append('resume_model_path', resumeModelPath || '')
    formData.append('GlobalTrainingStep', String(globalTrainingStep ?? 0))

    if (resumeModelFile) {
      formData.append('resume_model_file', resumeModelFile)
    }

    Object.entries(state.params).forEach(([key, value]) => {
      formData.append(key, value)
    })

    const response = await fetch('/api/start_training', {
      method: 'POST',
      body: formData,
    })

    if (!response.ok) {
      const errorText = await response.text()
      throw new Error(errorText || '启动训练失败')
    }

    const data = await response.json()
    state.jobId = data.job_id
    state.trainingStatus = 'running'
    state.showTrainingStartModal = false
    alert(`训练已启动！\n任务ID: ${data.job_id}`)
  } catch (error) {
    console.error('启动训练失败:', error)
    alert('启动训练失败: ' + error.message)
  }
}

const handleStartTraining = async () => {
  state.showTrainingStartModal = true
}

const handleStartTrainingConfirm = async (payload) => {
  await submitTraining(payload)
}

const handleStopTraining = async () => {
  try {
    const formData = new FormData()
    formData.append('job_id', state.jobId)

    const response = await fetch('/api/stop_training', {
      method: 'POST',
      body: formData,
    })

    if (!response.ok) {
      throw new Error('停止训练失败')
    }

    state.trainingStatus = 'idle'
    alert('训练已停止')
  } catch (error) {
    console.error('停止训练失败:', error)
    alert('停止训练失败: ' + error.message)
  }
}

const handleViewLogs = () => {
  state.showLogViewerModal = true
}

const handleLogViewerConfirm = async ({ logDir }) => {
  state.logViewerLoading = true
  state.selectedLogDir = logDir
  state.showLogViewerModal = false
  state.showLogsModal = true
  state.logViewerLoading = false
}

const goBack = () => {
  router.push('/')
}

onMounted(() => {
  loadSelectionState()
  if (!state.trainingType) {
    state.params = getDefaultParamsByType('orangerobot')
  }
})
</script>

<style scoped>
.page {
  --color-brand: #d97757;
  --color-brand-hover: #c06845;
  --color-dark: #141413;
  --color-bg: #fbf9f7;
  --color-white: #ffffff;
  --color-text: #1a1a19;
  --color-text-secondary: #6b6b6b;
  --color-border: #e8e4df;
  --color-dashed: #cccccc;
  --font-display: 'Poppins', -apple-system, BlinkMacSystemFont, sans-serif;
  --font-body: 'Lora', Georgia, 'Times New Roman', serif;
  --radius-button: 0.5em;
  min-height: 100vh;
  background-color: var(--color-bg);
  font-family: var(--font-body);
  color: var(--color-text);
  display: flex;
  flex-direction: column;
}

.navbar {
  border-bottom: 2px dashed var(--color-dashed);
  padding: 0 24px;
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

.btn-back,
.btn-step-back {
  background: transparent;
  color: var(--color-text-secondary);
  border: 1.5px dashed var(--color-dashed);
  border-radius: var(--radius-button);
  padding: 8px 20px;
  font-family: var(--font-display);
  font-size: 15px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.25s ease;
}

.btn-back:hover,
.btn-step-back:hover {
  color: var(--color-brand);
  border-color: var(--color-brand);
  background: rgba(217, 119, 87, 0.04);
}

.content {
  flex: 1;
  padding: 40px 24px;
  max-width: 1400px;
  width: 100%;
  margin: 0 auto;
}

.step-actions {
  display: flex;
  justify-content: flex-start;
  margin-bottom: 24px;
}

.step-container {
  animation: fadeIn 0.4s ease-out;
}

@keyframes fadeIn {
  from {
    opacity: 0;
    transform: translateY(20px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.selection-summary {
  display: flex;
  gap: 24px;
  justify-content: center;
  margin-bottom: 32px;
  padding: 16px;
  background: white;
  border: 2px dashed var(--color-dashed);
  border-radius: 8px;
}

.summary-item {
  font-family: var(--font-body);
  font-size: 15px;
  color: var(--color-text);
}

.summary-item strong {
  color: var(--color-dark);
  font-weight: 600;
}

.param-cards-grid {
  display: flex;
  flex-wrap: wrap;
  gap: 16px;
}
</style>
