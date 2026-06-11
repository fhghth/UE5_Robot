<template>
  <div class="param-panel">
    <div class="panel-header">
      <img class="panel-icon" :src="group.icon" :alt="group.name" />
      <h3 class="panel-title">{{ group.name }}</h3>
    </div>

    <div class="param-grid">
      <ParamInput
        v-for="param in group.params"
        :key="param.key"
        :param="param"
        :model-value="params[param.key]"
        @update:model-value="updateParam(param.key, $event)" />
    </div>
  </div>
</template>

<script setup>
import { defineProps, defineEmits } from 'vue'
import ParamInput from './ParamInput.vue'

const props = defineProps({
  group: {
    type: Object,
    required: true
  },
  params: {
    type: Object,
    required: true
  }
})

const emit = defineEmits(['update'])

const updateParam = (key, value) => {
  emit('update', { key, value })
}
</script>

<style scoped>
.param-panel {
  background: white;
  border: 2px dashed var(--color-dashed);
  border-radius: 8px;
  padding: 24px;
  margin-bottom: 24px;
  animation: slideDown 0.3s ease-out;
}

@keyframes slideDown {
  from {
    opacity: 0;
    transform: translateY(-20px);
    max-height: 0;
  }
  to {
    opacity: 1;
    transform: translateY(0);
    max-height: 2000px;
  }
}

.panel-header {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 24px;
  padding-bottom: 16px;
  border-bottom: 1px solid var(--color-border);
}

.panel-icon {
  width: 32px;
  height: 32px;
  object-fit: contain;
}

.panel-title {
  font-family: var(--font-display);
  font-size: 20px;
  font-weight: 600;
  color: var(--color-dark);
  margin: 0;
}

.param-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 20px;
}

/* 响应式调整 */
@media (max-width: 768px) {
  .param-grid {
    grid-template-columns: 1fr;
  }
}
</style>
