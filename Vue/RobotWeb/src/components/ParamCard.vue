<template>
  <button
    class="param-card"
    :class="{ active: isActive }"
    @click="handleClick">
    <img class="card-icon" :src="group.icon" :alt="group.name" />
    <h3 class="card-title">{{ group.name }}</h3>
    <span class="card-count">{{ group.count }} 项</span>
    <span v-if="isActive" class="expand-icon">▼</span>
  </button>
</template>

<script setup>
import { defineProps, defineEmits } from 'vue'

const props = defineProps({
  group: {
    type: Object,
    required: true
  },
  isActive: {
    type: Boolean,
    default: false
  }
})

const emit = defineEmits(['click'])

const handleClick = () => {
  emit('click', props.group.id)
}
</script>

<style scoped>
.param-card {
  background-color: var(--color-dark);
  color: var(--color-white);
  border: none;
  border-radius: var(--radius-button);
  padding: 20px 24px;
  min-width: 160px;
  cursor: pointer;
  transition: all 0.25s ease;
  text-align: center;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  position: relative;
}

.param-card:hover {
  background-color: #2c2c2b;
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.param-card.active {
  background-color: var(--color-brand);
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(217, 119, 87, 0.3);
}

.card-icon {
  width: 36px;
  height: 36px;
  object-fit: contain;
}

.card-title {
  font-family: var(--font-display);
  font-size: 16px;
  font-weight: 600;
  margin: 0;
  color: inherit;
}

.card-count {
  font-size: 13px;
  opacity: 0.8;
  font-family: var(--font-body);
}

.expand-icon {
  position: absolute;
  bottom: 8px;
  font-size: 12px;
  opacity: 0.8;
}
</style>
