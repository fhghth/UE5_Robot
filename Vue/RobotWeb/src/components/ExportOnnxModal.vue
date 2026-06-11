<template>
  <Transition name="modal">
    <div v-if="show" class="modal-overlay" @click="handleClose">
      <div class="modal-content" @click.stop>
        <div class="modal-header">
          <div>
            <h2 class="modal-title">导出{{ exportTypeLabel }}ONNX模型</h2>
            <p class="modal-subtitle">请选择源模型文件和 ONNX 保存位置</p>
          </div>
          <button class="btn-close" @click="handleClose">✕</button>
        </div>

        <div class="modal-body">
          <div class="field-group">
            <label class="field-label">模型地址 (.zip)</label>
            <div class="field-row">
              <input v-model.trim="localModelPath" class="field-input" type="text" placeholder="请选择模型文件路径" />
              <button class="btn-pick" :disabled="loading" @click="pickModelPath">选择模型</button>
            </div>
          </div>

          <div class="field-group">
            <label class="field-label">保存地址 (.onnx)</label>
            <div class="field-row">
              <input v-model.trim="localOnnxPath" class="field-input" type="text" placeholder="请选择 ONNX 输出路径" />
              <button class="btn-pick" :disabled="loading" @click="pickOnnxPath">选择保存位置</button>
            </div>
          </div>

          <p v-if="errorMessage" class="error-message">{{ errorMessage }}</p>
        </div>

        <div class="modal-footer">
          <button class="btn-secondary" :disabled="loading" @click="handleClose">取消</button>
          <button class="btn-primary" :disabled="loading" @click="handleConfirm">
            {{ loading ? '导出中...' : '确认导出' }}
          </button>
        </div>
      </div>
    </div>
  </Transition>
</template>

<script setup>
import { computed, ref, watch } from 'vue'

const props = defineProps({
  show: {
    type: Boolean,
    default: false,
  },
  exportType: {
    type: String,
    default: 'orangerobot',
  },
  loading: {
    type: Boolean,
    default: false,
  },
})

const emit = defineEmits(['close', 'confirm'])

const localModelPath = ref('')
const localOnnxPath = ref('')
const errorMessage = ref('')

const exportTypeLabel = computed(() => (props.exportType === 'navigation' ? '导航' : '机器人'))

const resetState = () => {
  localModelPath.value = ''
  localOnnxPath.value = ''
  errorMessage.value = ''
}

const handleClose = () => {
  if (props.loading) return
  emit('close')
}

const pickModelPath = async () => {
  try {
    errorMessage.value = ''
    const response = await fetch('/api/dialog/select-model-file', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({ export_type: props.exportType }),
    })

    const data = await response.json()
    if (!response.ok) {
      throw new Error(data.detail || '选择模型路径失败')
    }

    if (data.path) {
      localModelPath.value = data.path
    }
  } catch (error) {
    errorMessage.value = error.message
  }
}

const pickOnnxPath = async () => {
  try {
    errorMessage.value = ''
    const response = await fetch('/api/dialog/select-export-path', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        export_type: props.exportType,
        model_path: localModelPath.value,
      }),
    })

    const data = await response.json()
    if (!response.ok) {
      throw new Error(data.detail || '选择保存地址失败')
    }

    if (data.path) {
      localOnnxPath.value = data.path
    }
  } catch (error) {
    errorMessage.value = error.message
  }
}

const handleConfirm = () => {
  errorMessage.value = ''

  if (!localModelPath.value) {
    errorMessage.value = '请先选择模型地址'
    return
  }

  if (!localOnnxPath.value) {
    errorMessage.value = '请先选择保存地址'
    return
  }

  emit('confirm', {
    modelPath: localModelPath.value,
    onnxPath: localOnnxPath.value,
  })
}

watch(
  () => props.show,
  (visible) => {
    if (visible) {
      resetState()
    }
  },
)
</script>

<style scoped>
.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.55);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1200;
  padding: 20px;
}

.modal-content {
  width: min(720px, 94vw);
  background: white;
  border-radius: 12px;
  box-shadow: 0 24px 80px rgba(0, 0, 0, 0.24);
  overflow: hidden;
}

.modal-header,
.modal-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 20px 24px;
}

.modal-header {
  border-bottom: 2px dashed var(--color-dashed);
}

.modal-footer {
  border-top: 2px dashed var(--color-dashed);
  justify-content: flex-end;
  gap: 12px;
}

.modal-title {
  margin: 0;
  font-family: var(--font-display);
  font-size: 20px;
  color: var(--color-dark);
}

.modal-subtitle {
  margin: 6px 0 0;
  color: var(--color-text-secondary);
  font-size: 14px;
}

.btn-close {
  background: transparent;
  border: none;
  font-size: 24px;
  cursor: pointer;
  color: var(--color-text-secondary);
}

.modal-body {
  padding: 24px;
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.field-group {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.field-label {
  font-family: var(--font-display);
  font-size: 14px;
  font-weight: 600;
  color: var(--color-dark);
}

.field-row {
  display: flex;
  gap: 12px;
}

.field-input {
  width: 100%;
  border: 1.5px solid var(--color-border);
  border-radius: 8px;
  padding: 12px 14px;
  font-size: 14px;
  font-family: var(--font-body);
  color: var(--color-text);
  background: white;
  box-sizing: border-box;
}

.field-input:focus {
  outline: none;
  border-color: var(--color-brand);
  box-shadow: 0 0 0 3px rgba(217, 119, 87, 0.12);
}

.btn-pick,
.btn-primary,
.btn-secondary {
  border: none;
  border-radius: var(--radius-button);
  padding: 12px 20px;
  font-family: var(--font-display);
  font-size: 15px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s ease;
  white-space: nowrap;
}

.btn-pick,
.btn-secondary {
  background: var(--color-dark);
  color: white;
}

.btn-primary {
  background: var(--color-brand);
  color: white;
}

.btn-primary:disabled,
.btn-secondary:disabled,
.btn-pick:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.error-message {
  margin: 0;
  color: #d33;
  font-size: 14px;
  font-weight: 600;
}

.modal-enter-active,
.modal-leave-active {
  transition: opacity 0.2s ease;
}

.modal-enter-active .modal-content,
.modal-leave-active .modal-content {
  transition: transform 0.2s ease, opacity 0.2s ease;
}

.modal-enter-from,
.modal-leave-to {
  opacity: 0;
}

.modal-enter-from .modal-content,
.modal-leave-to .modal-content {
  transform: translateY(10px) scale(0.98);
  opacity: 0;
}

@media (max-width: 720px) {
  .field-row {
    flex-direction: column;
  }
}
</style>
