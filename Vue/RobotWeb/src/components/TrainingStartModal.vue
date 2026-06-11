<template>
  <Transition name="modal">
    <div v-if="show" class="modal-overlay" @click="handleClose">
      <div class="modal-content" @click.stop>
        <div class="modal-header">
          <h2 class="modal-title">选择{{ trainingTypeLabel }}启动方式</h2>
          <button class="btn-close" @click="handleClose">✕</button>
        </div>

        <div class="modal-body">
          <div class="mode-toggle">
            <button class="mode-btn" :class="{ active: localMode === 'new' }" @click="localMode = 'new'">
              新训练
            </button>
            <button class="mode-btn" :class="{ active: localMode === 'resume' }" @click="localMode = 'resume'">
              继续训练
            </button>
          </div>

          <div v-if="localMode === 'resume'" class="resume-form">
            <label class="field-label" for="resume-model-file">继续训练模型文件 (.zip)</label>
            <input id="resume-model-file" class="field-input" type="file" accept=".zip" @change="handleFileChange" />

            <label class="field-label" for="resume-model-path">或手动输入模型路径</label>
            <input
              id="resume-model-path"
              v-model.trim="localResumeModelPath"
              class="field-input"
              type="text"
              placeholder="D:/uePro/OrangeRobot/Training/.../model.zip" />

            <label class="field-label" for="global-training-step">GlobalTrainingStep</label>
            <input
              id="global-training-step"
              v-model.number="localGlobalTrainingStep"
              class="field-input"
              type="number"
              min="0"
              step="1"
              placeholder="请输入累计训练步数" />

            <p class="resume-hint">
              支持 final / interrupted / error / checkpoint 生成的 .zip 模型。请填写与当前{{ trainingTypeLabel }}模型实际训练进度一致的累计步数。
            </p>
          </div>

          <p v-if="errorMessage" class="error-message">{{ errorMessage }}</p>
        </div>

        <div class="modal-footer">
          <button class="btn-secondary" @click="handleClose">取消</button>
          <button class="btn-primary" @click="handleConfirm">确认启动</button>
        </div>
      </div>
    </div>
  </Transition>
</template>

<script setup>
import { ref, watch, computed } from 'vue'

const props = defineProps({
  show: {
    type: Boolean,
    default: false,
  },
  trainingType: {
    type: String,
    default: 'orangerobot',
  },
})

const emit = defineEmits(['close', 'confirm'])
const trainingTypeLabel = computed(() => (props.trainingType === 'navigation' ? '导航训练' : '双足机器人训练'))
const localMode = ref('new')
const localResumeModelPath = ref('')
const localGlobalTrainingStep = ref(null)
const localResumeModelFile = ref(null)
const errorMessage = ref('')

const resetState = () => {
  localMode.value = 'new'
  localResumeModelPath.value = ''
  localGlobalTrainingStep.value = null
  localResumeModelFile.value = null
  errorMessage.value = ''
}

const handleFileChange = (event) => {
  const file = event.target.files?.[0] || null
  localResumeModelFile.value = file
}

const handleClose = () => {
  emit('close')
}

const handleConfirm = () => {
  errorMessage.value = ''

  if (localMode.value === 'resume') {
    const hasFile = Boolean(localResumeModelFile.value)
    const hasPath = Boolean(localResumeModelPath.value)
    const numericGlobalStep = Number(localGlobalTrainingStep.value)

    if (!hasFile && !hasPath) {
      errorMessage.value = '继续训练必须选择模型文件或填写模型路径'
      return
    }

    if (localGlobalTrainingStep.value === null || Number.isNaN(numericGlobalStep)) {
      errorMessage.value = '继续训练必须填写 GlobalTrainingStep'
      return
    }

    if (!Number.isInteger(numericGlobalStep) || numericGlobalStep < 0) {
      errorMessage.value = 'GlobalTrainingStep 必须是大于等于 0 的整数'
      return
    }
  }

  emit('confirm', {
    trainingMode: localMode.value,
    resumeModelPath: localResumeModelPath.value,
    globalTrainingStep: localMode.value === 'resume' ? Number(localGlobalTrainingStep.value) : 0,
    resumeModelFile: localResumeModelFile.value,
  })
}

watch(
  () => localMode.value,
  (mode) => {
    if (mode === 'new') {
      localResumeModelPath.value = ''
      localGlobalTrainingStep.value = null
      localResumeModelFile.value = null
      errorMessage.value = ''
    }
  },
)

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
  width: min(640px, 92vw);
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

.btn-close {
  background: transparent;
  border: none;
  font-size: 24px;
  cursor: pointer;
  color: var(--color-text-secondary);
}

.modal-body {
  padding: 24px;
}

.mode-toggle {
  display: flex;
  gap: 12px;
  margin-bottom: 24px;
}

.mode-btn,
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
}

.mode-btn {
  background: #f2ece7;
  color: var(--color-text);
}

.mode-btn.active,
.btn-primary {
  background: var(--color-brand);
  color: white;
}

.btn-secondary {
  background: var(--color-dark);
  color: white;
}

.resume-form {
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

.resume-hint {
  margin: 8px 0 0;
  color: var(--color-text-secondary);
  font-size: 13px;
  line-height: 1.6;
}

.error-message {
  margin-top: 16px;
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
</style>
