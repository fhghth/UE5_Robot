<template>
  <div class="page">
    <header class="navbar">
      <div class="navbar__inner">
        <h1 class="navbar__title">导出ONNX模型</h1>
        <button class="btn-back" @click="goBack">← 返回首页</button>
      </div>
    </header>

    <main class="content">
      <div v-if="state.step > 1" class="step-actions">
        <button class="btn-step-back" @click="goToPreviousStep">← 上一步</button>
      </div>

      <TrainingTypeSelector
        v-if="state.step === 1"
        :selected-type="null"
        title="导出类型"
        subtitle="Export Type"
        @select="handleTypeSelect" />

      <div v-if="state.step === 2" class="step-container">
        <div class="selection-summary">
          <span class="summary-item"><strong>导出类型:</strong> {{ typeName }}</span>
        </div>

        <div class="export-panel">
          <div class="export-panel__content">
            <h2 class="export-panel__title">准备导出{{ typeName }}ONNX模型</h2>
            <p class="export-panel__desc">
              点击下方按钮后，将弹窗选择模型地址和保存地址，并调用对应导出脚本完成 ONNX 导出。
            </p>
          </div>

          <div class="export-actions">
            <button class="btn-export" :disabled="state.exporting" @click="openExportModal">
              {{ state.exporting ? '导出中...' : exportButtonLabel }}
            </button>
          </div>

          <div v-if="state.lastResult" class="export-result success">
            <p><strong>导出成功</strong></p>
            <p>ONNX: {{ state.lastResult.onnx_path }}</p>
            <p>Metadata: {{ state.lastResult.metadata_path }}</p>
            <p>Log: {{ state.lastResult.log_path }}</p>
          </div>
        </div>
      </div>
    </main>

    <ExportOnnxModal
      :show="state.showExportModal"
      :export-type="state.exportType"
      :loading="state.exporting"
      @close="state.showExportModal = false"
      @confirm="handleExportConfirm" />
  </div>
</template>

<script setup>
import { computed, reactive } from 'vue'
import { useRouter } from 'vue-router'
import TrainingTypeSelector from '@/components/TrainingTypeSelector.vue'
import ExportOnnxModal from '@/components/ExportOnnxModal.vue'

const router = useRouter()

const state = reactive({
  step: 1,
  exportType: null,
  showExportModal: false,
  exporting: false,
  lastResult: null,
})

const typeName = computed(() => {
  if (state.exportType === 'navigation') return '导航'
  if (state.exportType === 'orangerobot') return '双足机器人'
  return ''
})

const exportButtonLabel = computed(() => {
  return state.exportType === 'navigation' ? '导出导航' : '导出机器人'
})

const handleTypeSelect = (type) => {
  state.exportType = type
  state.lastResult = null
  state.step = 2
}

const goToPreviousStep = () => {
  if (state.step > 1) {
    state.step = 1
  }
}

const openExportModal = () => {
  state.showExportModal = true
}

const handleExportConfirm = async ({ modelPath, onnxPath }) => {
  try {
    state.exporting = true
    state.lastResult = null

    const response = await fetch('/api/export_onnx', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        export_type: state.exportType,
        model_path: modelPath,
        onnx_path: onnxPath,
      }),
    })

    const data = await response.json()
    if (!response.ok) {
      throw new Error(data.detail || data.message || '导出失败')
    }

    state.lastResult = data
    state.showExportModal = false
    alert('ONNX 导出成功')
  } catch (error) {
    console.error('导出 ONNX 失败:', error)
    alert('导出 ONNX 失败: ' + error.message)
  } finally {
    state.exporting = false
  }
}

const goBack = () => {
  router.push('/')
}
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

.export-panel {
  max-width: 880px;
  margin: 0 auto;
  background: white;
  border: 2px dashed var(--color-dashed);
  border-radius: 16px;
  padding: 32px;
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.export-panel__title {
  margin: 0 0 12px;
  font-family: var(--font-display);
  font-size: 28px;
  color: var(--color-dark);
}

.export-panel__desc {
  margin: 0;
  font-size: 16px;
  line-height: 1.8;
  color: var(--color-text-secondary);
}

.export-actions {
  display: flex;
  justify-content: center;
}

.btn-export {
  min-width: 180px;
  border: none;
  border-radius: var(--radius-button);
  padding: 14px 32px;
  font-family: var(--font-display);
  font-size: 16px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.25s ease;
  background-color: var(--color-brand);
  color: white;
}

.btn-export:hover:not(:disabled) {
  background-color: var(--color-brand-hover);
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(217, 119, 87, 0.3);
}

.btn-export:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.export-result {
  border-radius: 12px;
  padding: 18px 20px;
  line-height: 1.7;
  word-break: break-all;
}

.export-result.success {
  background: #f6fbf7;
  border: 1.5px solid #b8e0c3;
  color: #205c31;
}

.export-result p {
  margin: 0;
}
</style>
