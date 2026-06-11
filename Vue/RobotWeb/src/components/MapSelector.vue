<template>
  <div class="map-selector">
    <h2 class="section-title">{{ title }}</h2>

    <div v-if="loading" class="loading">
      <span>加载地图列表中...</span>
    </div>

    <div v-else-if="error" class="error">
      <span>{{ error }}</span>
    </div>

    <div v-else class="map-grid">
      <div
        v-for="map in maps"
        :key="map.id"
        class="map-card"
        :class="{ active: selectedMap?.id === map.id }"
        @click="selectMap(map)">
        <div class="map-thumbnail">
          <img
            v-if="map.thumbnail"
            :src="map.thumbnail"
            :alt="map.name"
            @error="handleImageError" />
          <div v-else class="placeholder">
            <span class="placeholder-icon">🗺️</span>
          </div>
        </div>
        <div class="map-info">
          <h3 class="map-name">{{ map.name }}</h3>
          <p v-if="map.description" class="map-description">
            {{ map.description }}
          </p>
        </div>
        <div v-if="selectedMap?.id === map.id" class="check-mark">✓</div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, defineProps, defineEmits, watch } from 'vue'

const props = defineProps({
  trainingType: {
    type: String,
    default: '',
  },
  fetchUrl: {
    type: String,
    default: '',
  },
  title: {
    type: String,
    default: '选择训练地图',
  },
  selectedMap: {
    type: Object,
    default: null,
  },
})

const emit = defineEmits(['select'])

const maps = ref([])
const loading = ref(false)
const error = ref(null)

const getRequestUrl = () => {
  if (props.fetchUrl) return props.fetchUrl
  return `/api/maps?type=${props.trainingType}`
}

const fetchMaps = async () => {
  loading.value = true
  error.value = null

  try {
    const response = await fetch(getRequestUrl())
    if (!response.ok) {
      throw new Error('获取地图列表失败')
    }
    maps.value = await response.json()
  } catch (err) {
    error.value = err.message
    console.error('获取地图列表失败:', err)
  } finally {
    loading.value = false
  }
}

const selectMap = (map) => {
  emit('select', map)
}

const handleImageError = (event) => {
  event.target.style.display = 'none'
}

watch(
  () => [props.trainingType, props.fetchUrl],
  () => {
    fetchMaps()
  },
)

onMounted(() => {
  fetchMaps()
})
</script>

<style scoped>
.map-selector {
  width: 100%;
  max-width: 1200px;
  margin: 0 auto;
  padding: 20px;
}

.section-title {
  font-family: var(--font-display);
  font-size: 24px;
  font-weight: 600;
  color: var(--color-dark);
  text-align: center;
  margin-bottom: 32px;
}

.loading,
.error {
  text-align: center;
  padding: 40px;
  font-family: var(--font-body);
  color: var(--color-text-secondary);
}

.error {
  color: #e74c3c;
}

.map-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 24px;
}

.map-card {
  background: white;
  border: 2px dashed var(--color-dashed);
  border-radius: 8px;
  overflow: hidden;
  cursor: pointer;
  transition: all 0.25s ease;
  position: relative;
}

.map-card:hover {
  border-color: var(--color-brand);
  transform: translateY(-4px);
  box-shadow: 0 8px 16px rgba(0, 0, 0, 0.1);
}

.map-card.active {
  border-color: var(--color-brand);
  border-style: solid;
  box-shadow: 0 8px 16px rgba(217, 119, 87, 0.2);
}

.map-thumbnail {
  width: 100%;
  height: 180px;
  background: var(--color-bg);
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
}

.map-thumbnail img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.placeholder {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 100%;
  height: 100%;
  background: linear-gradient(135deg, #f5f5f5 0%, #e8e4df 100%);
}

.placeholder-icon {
  font-size: 48px;
  opacity: 0.5;
}

.map-info {
  padding: 16px;
}

.map-name {
  font-family: var(--font-display);
  font-size: 18px;
  font-weight: 600;
  color: var(--color-dark);
  margin: 0 0 8px 0;
}

.map-description {
  font-family: var(--font-body);
  font-size: 14px;
  color: var(--color-text-secondary);
  margin: 0;
  line-height: 1.5;
}

.check-mark {
  position: absolute;
  top: 12px;
  right: 12px;
  width: 32px;
  height: 32px;
  background: var(--color-brand);
  color: white;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 18px;
  font-weight: bold;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
}
</style>
