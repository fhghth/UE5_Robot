<template>
  <div class="type-selector">
    <Transition name="fade-up">
      <div v-if="!selectedType" class="title-section">
        <h1 class="main-title">{{ props.title }}</h1>
        <p class="subtitle">{{ props.subtitle }}</p>
      </div>
    </Transition>

    <div class="type-buttons" :class="{ compact: selectedType }">
      <button
        v-for="type in props.typeOptions"
        :key="type.id"
        class="btn-type"
        :class="{ active: selectedType === type.id }"
        @click="selectType(type.id)"
      >
        <img class="type-icon" :src="type.icon" :alt="type.name" />
        <span class="type-name">{{ type.name }}</span>
        <span v-if="selectedType === type.id" class="check-mark">✓</span>
      </button>
    </div>
  </div>
</template>

<script setup>
import { defineProps, defineEmits } from 'vue'
import RobotIcon from '@/assets/icons/Robot.gif'
import NavigationIcon from '@/assets/icons/Navigation.png'

const props = defineProps({
  selectedType: {
    type: String,
    default: null,
  },
  title: {
    type: String,
    default: '训练类型',
  },
  subtitle: {
    type: String,
    default: 'Training Type',
  },
  typeOptions: {
    type: Array,
    default: () => [
      {
        id: 'orangerobot',
        name: '双足机器人',
        icon: RobotIcon,
      },
      {
        id: 'navigation',
        name: '导航',
        icon: NavigationIcon,
      },
    ],
  },
})

const emit = defineEmits(['select'])

const selectType = (typeId) => {
  emit('select', typeId)
}
</script>

<style scoped>
.type-selector {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-height: 60vh;
  transition: all 0.4s ease;
}

.title-section {
  text-align: center;
  margin-bottom: 48px;
}

.main-title {
  font-family: var(--font-display);
  font-size: clamp(36px, 6vw, 56px);
  font-weight: 800;
  color: var(--color-dark);
  letter-spacing: -0.02em;
  margin: 0 0 12px 0;
}

.subtitle {
  font-family: var(--font-body);
  font-size: clamp(18px, 2.5vw, 24px);
  color: var(--color-text-secondary);
  font-style: italic;
  font-weight: 400;
  letter-spacing: 0.01em;
  margin: 0;
}

.fade-up-leave-active {
  transition: all 0.4s ease-out;
}

.fade-up-leave-to {
  transform: translateY(-50px);
  opacity: 0;
}

.type-buttons {
  display: flex;
  gap: 16px;
  flex-wrap: wrap;
  justify-content: center;
  transition: all 0.4s ease;
}

.type-buttons.compact {
  transform: translateY(-100px);
  margin-bottom: 40px;
}

.btn-type {
  background-color: var(--color-dark);
  color: var(--color-white);
  border: none;
  border-radius: var(--radius-button);
  padding: 20px 32px;
  font-family: var(--font-display);
  font-size: 16px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.25s ease;
  min-width: 160px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  position: relative;
}

.btn-type:hover {
  background-color: #2c2c2b;
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.btn-type.active {
  background-color: var(--color-brand);
  transform: translateY(-2px);
}

.type-icon {
  width: 48px;
  height: 48px;
  object-fit: contain;
}

.type-name {
  font-size: 16px;
}

.check-mark {
  position: absolute;
  top: 8px;
  right: 8px;
  font-size: 18px;
  color: white;
}
</style>
