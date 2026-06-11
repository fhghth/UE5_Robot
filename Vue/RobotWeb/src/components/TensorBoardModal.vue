<template>
  <Transition name="modal">
    <div v-if="show" class="modal-overlay" @click="handleClose">
      <div class="modal-content" @click.stop>
        <div class="modal-header">
          <h2 class="modal-title">训练日志 - TensorBoard</h2>
          <button class="btn-close" @click="handleClose">✕</button>
        </div>

        <div v-if="loading" class="modal-body loading">
          <span>正在启动 TensorBoard...</span>
        </div>

        <div v-else-if="error" class="modal-body error">
          <span>{{ error }}</span>
          <button class="btn-retry" @click="retry">重试</button>
        </div>

        <iframe
          v-else
          :src="tensorboardUrl"
          class="tensorboard-frame"
          frameborder="0"
          title="TensorBoard">
        </iframe>
      </div>
    </div>
  </Transition>
</template>

<script setup>
import { ref, watch, defineProps, defineEmits } from 'vue'

const props = defineProps({
  show: {
    type: Boolean,
    default: false
  },
  jobId: {
    type: String,
    default: null
  },
  logDir: {
    type: String,
    default: null
  }
})

const emit = defineEmits(['close'])

const tensorboardUrl = ref(null)
const loading = ref(false)
const error = ref(null)

const waitForReady = async (url, timeoutMs = 15000) => {
  const start = Date.now()
  while (Date.now() - start < timeoutMs) {
    try {
      const resp = await fetch(url, { mode: 'no-cors' })
      return
    } catch {
      await new Promise((r) => setTimeout(r, 500))
    }
  }
}

const fetchTensorBoardUrl = async () => {
  loading.value = true
  error.value = null

  try {
    let response
    if (props.logDir) {
      response = await fetch('/api/tensorboard/start', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ log_dir: props.logDir }),
      })
    } else if (props.jobId) {
      response = await fetch(`/api/tensorboard/${props.jobId}`)
    } else {
      error.value = '缺少训练任务ID或日志目录'
      return
    }

    if (!response.ok) {
      throw new Error('启动 TensorBoard 失败')
    }
    const data = await response.json()
    await waitForReady(data.url)
    tensorboardUrl.value = data.url
  } catch (err) {
    error.value = err.message
    console.error('获取 TensorBoard URL 失败:', err)
  } finally {
    loading.value = false
  }
}

const handleClose = () => {
  emit('close')
}

const retry = () => {
  fetchTensorBoardUrl()
}

watch(() => props.show, (newVal) => {
  if (newVal && (props.jobId || props.logDir)) {
    fetchTensorBoardUrl()
  }
})
</script>

<style scoped>
.modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.7);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
  padding: 20px;
}

.modal-content {
  background: white;
  border-radius: 8px;
  width: 90vw;
  height: 85vh;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
}

.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 20px 24px;
  border-bottom: 2px dashed var(--color-dashed);
  background: var(--color-bg);
}

.modal-title {
  font-family: var(--font-display);
  font-size: 20px;
  font-weight: 600;
  color: var(--color-dark);
  margin: 0;
}

.btn-close {
  background: transparent;
  border: none;
  font-size: 24px;
  color: var(--color-text-secondary);
  cursor: pointer;
  padding: 4px 8px;
  transition: color 0.2s;
  line-height: 1;
}

.btn-close:hover {
  color: var(--color-brand);
}

.modal-body {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 40px;
  font-family: var(--font-body);
}

.modal-body.loading {
  color: var(--color-text-secondary);
  font-size: 16px;
}

.modal-body.error {
  color: #e74c3c;
  font-size: 16px;
  gap: 16px;
}

.btn-retry {
  padding: 10px 24px;
  background: var(--color-brand);
  color: white;
  border: none;
  border-radius: var(--radius-button);
  font-family: var(--font-display);
  font-size: 14px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s;
}

.btn-retry:hover {
  background: var(--color-brand-hover);
}

.tensorboard-frame {
  flex: 1;
  width: 100%;
  border: none;
  background: white;
}

/* 模态框动画 */
.modal-enter-active,
.modal-leave-active {
  transition: opacity 0.3s ease;
}

.modal-enter-from,
.modal-leave-to {
  opacity: 0;
}

.modal-enter-active .modal-content,
.modal-leave-active .modal-content {
  transition: transform 0.3s ease;
}

.modal-enter-from .modal-content,
.modal-leave-to .modal-content {
  transform: scale(0.9);
}
</style>
