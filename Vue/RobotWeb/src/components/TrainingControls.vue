<template>
  <div class="training-controls">
    <button
      class="btn-control btn-primary"
      :class="{ 'btn-stop': trainingStatus === 'running' }"
      :disabled="!canClickPrimary"
      @click="handleTrainingAction">
      {{ buttonText }}
    </button>

    <button
      class="btn-control btn-secondary"
      @click="handleViewLogs">
      查看训练日志
    </button>
  </div>
</template>

<script setup>
import { computed, defineProps, defineEmits } from 'vue'

const props = defineProps({
  trainingStatus: {
    type: String,
    default: 'idle' // 'idle' | 'running'
  },
  canStart: {
    type: Boolean,
    default: false
  }
})

const emit = defineEmits(['start', 'stop', 'view-logs'])

const buttonText = computed(() => {
  return props.trainingStatus === 'running' ? '停止训练' : '开始训练'
})

const canClickPrimary= computed(() => {
  if (props.trainingStatus === 'running') {
    return true
  }
  return props.canStart
})

const handleTrainingAction = () => {
  if (props.trainingStatus === 'running') {
    emit('stop')
  } else {
    emit('start')
  }
}

const handleViewLogs = () => {
  emit('view-logs')
}
</script>

<style scoped>
.training-controls {
  display: flex;
  gap: 16px;
  justify-content: center;
  margin-top: 32px;
  padding: 24px;
}

.btn-control {
  padding: 14px 32px;
  font-family: var(--font-display);
  font-size: 16px;
  font-weight: 600;
  border: none;
  border-radius: var(--radius-button);
  cursor: pointer;
  transition: all 0.25s ease;
  min-width: 160px;
}

.btn-control:disabled {
  opacity: 0.5;
  cursor: not-allowed;
  transform: none !important;
}

.btn-primary {
  background-color: var(--color-brand);
  color: white;
}

.btn-primary:hover:not(:disabled) {
  background-color: var(--color-brand-hover);
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(217, 119, 87, 0.3);
}

.btn-stop {
  background-color: #e74c3c;
}

.btn-stop:hover:not(:disabled) {
  background-color: #c0392b;
}

.btn-secondary {
  background-color: var(--color-dark);
  color: white;
}

.btn-secondary:hover:not(:disabled) {
  background-color: #2c2c2b;
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.btn-control:active:not(:disabled) {
  transform: translateY(0);
}
</style>
