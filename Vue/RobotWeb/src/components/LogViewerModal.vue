<template>
  <Transition name="modal">
    <div v-if="show" class="modal-overlay" @click="handleClose">
      <div class="modal-content" @click.stop>
        <div class="modal-header">
          <div>
            <h2 class="modal-title">查看训练日志</h2>
            <p class="modal-subtitle">请选择 TensorBoard 日志目录</p>
          </div>
          <button class="btn-close" @click="handleClose">✕</button>
        </div>

        <div class="modal-body">
          <div class="field-group">
            <label class="field-label">日志目录 (tensorboard)</label>
            <div class="field-row">
              <input v-model.trim="localLogDir" class="field-input" type="text" placeholder="请选择日志目录路径" />
              <button class="btn-pick" :disabled="loading" @click="pickLogDir">选择目录</button>
            </div>
          </div>

          <p v-if="errorMessage" class="error-message">{{ errorMessage }}</p>
        </div>

        <div class="modal-footer">
          <button class="btn-secondary" :disabled="loading" @click="handleClose">取消</button>
          <button class="btn-primary" :disabled="loading" @click="handleConfirm">
            {{ loading ? '启动中...' : '查看日志' }}
          </button>
        </div>
      </div>
    </div>
  </Transition>
</template>

<script setup>
import { ref, watch } from 'vue'

const props = defineProps({
  show: {
    type: Boolean,
    default: false,
  },
  loading: {
    type: Boolean,
    default: false,
  },
})

const emit = defineEmits(['close', 'confirm'])

const localLogDir = ref('')
const errorMessage = ref('')

const resetState = () => {
  localLogDir.value = ''
  errorMessage.value = ''
}

const handleClose = () => {
  if (props.loading) return
  emit('close')
}

const pickLogDir = async () => {
  try {
    errorMessage.value = ''
    const response = await fetch('/api/dialog/select-directory', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
    })

    const data = await response.json()
    if (!response.ok) {
      throw new Error(data.detail || '选择目录失败')
    }

    if (data.path) {
      localLogDir.value = data.path
    }
  } catch (error) {
    errorMessage.value = error.message
  }
}

const handleConfirm = () => {
  errorMessage.value = ''

  if (!localLogDir.value) {
    errorMessage.value = '请先选择日志目录'
    return
  }

  emit('confirm', {
    logDir: localLogDir.value,
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
