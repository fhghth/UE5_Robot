<template>
  <div class="param-input">
    <label class="param-label">
      {{ param.label }}
      <span
        v-if="param.description"
        class="help-icon"
        :title="param.description"
        @mouseenter="showTooltip = true"
        @mouseleave="showTooltip = false"
      >
        ?
      </span>
    </label>

    <input
      v-if="param.type === 'number'"
      type="number"
      :value="modelValue"
      :step="param.step"
      :min="param.min"
      :max="param.max"
      @input="emit('update:modelValue', Number($event.target.value))"
      class="param-control"
    />

    <input
      v-else-if="param.type === 'text'"
      type="text"
      :value="modelValue"
      @input="emit('update:modelValue', $event.target.value)"
      class="param-control"
    />

    <select
      v-else-if="param.type === 'select'"
      :value="modelValue"
      @change="emit('update:modelValue', $event.target.value)"
      class="param-control"
    >
      <option v-for="option in param.options" :key="option.value" :value="option.value">
        {{ option.label }}
      </option>
    </select>

    <label v-else-if="param.type === 'checkbox'" class="checkbox-wrapper">
      <input
        type="checkbox"
        :checked="modelValue"
        @change="emit('update:modelValue', $event.target.checked)"
        class="checkbox-input"
      />
      <span class="checkbox-label">{{ param.checkboxLabel || '启用' }}</span>
    </label>

    <span v-if="param.hint" class="param-hint">
      {{ param.hint }}
    </span>

    <Transition name="fade">
      <div v-if="showTooltip && param.description" class="tooltip">
        {{ param.description }}
      </div>
    </Transition>
  </div>
</template>

<script setup>
import { ref } from 'vue'

defineProps({
  param: {
    type: Object,
    required: true,
  },
  modelValue: {
    type: [String, Number, Boolean],
    default: null,
  },
})

const emit = defineEmits(['update:modelValue'])
const showTooltip = ref(false)
</script>

<style scoped>
.param-input {
  display: flex;
  flex-direction: column;
  position: relative;
}

.param-label {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 14px;
  font-weight: 500;
  color: var(--color-text);
  margin-bottom: 8px;
  font-family: var(--font-display);
}

.help-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: var(--color-text-secondary);
  color: white;
  font-size: 11px;
  cursor: help;
  transition: background 0.2s;
  flex-shrink: 0;
}

.help-icon:hover {
  background: var(--color-brand);
}

.param-control {
  width: 100%;
  padding: 10px 14px;
  border: 1.5px dashed var(--color-dashed);
  border-radius: var(--radius-button);
  font-family: var(--font-body);
  font-size: 14px;
  transition: all 0.2s;
  background: white;
  color: var(--color-text);
}

.param-control:focus {
  outline: none;
  border-color: var(--color-brand);
  border-style: solid;
  box-shadow: 0 0 0 3px rgba(217, 119, 87, 0.1);
}

.param-control:hover {
  border-color: var(--color-brand);
}

.checkbox-wrapper {
  display: flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
  padding: 10px 0;
}

.checkbox-input {
  width: 18px;
  height: 18px;
  cursor: pointer;
  accent-color: var(--color-brand);
}

.checkbox-label {
  font-family: var(--font-body);
  font-size: 14px;
  color: var(--color-text);
}

.param-hint {
  font-size: 12px;
  color: var(--color-text-secondary);
  margin-top: 6px;
  font-style: italic;
  font-family: var(--font-body);
}

.tooltip {
  position: absolute;
  top: 100%;
  left: 0;
  right: 0;
  margin-top: 4px;
  padding: 8px 12px;
  background: var(--color-dark);
  color: white;
  font-size: 12px;
  border-radius: 4px;
  z-index: 10;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
  font-family: var(--font-body);
  line-height: 1.4;
}

.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.2s;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}
</style>
