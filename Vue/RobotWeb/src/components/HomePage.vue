<template>
  <div class="home">
    <!-- 顶部导航栏 -->
    <header class="navbar">
      <div class="navbar__inner">
        <img src="@/assets/logo.gif" alt="Logo" class="navbar__logo" />
        <!-- <button class="btn-exit" @click="handleExit">退出</button> -->
      </div>
    </header>

    <!-- 主内容区 -->
    <main class="hero">
      <h1 class="hero__title">双足机器人智能导航训练平台</h1>
      <p class="hero__subtitle">
        Welcome to the UE5-based biped robot intelligent navigation training platform
      </p>

      <div class="hero__actions">
        <button
          class="btn-dark"
          v-for="(btn, idx) in mainButtons"
          :key="idx"
          @click="handleNavigate(btn.path)"
        >
          {{ btn.label }}
        </button>
      </div>
    </main>
  </div>
</template>

<script setup>
import { useRouter } from 'vue-router'

const router = useRouter()

// 三个黑色按钮的数据
const mainButtons = [
  { label: '开始训练', path: '/train' },
  { label: '导出ONNX模型', path: '/export-onnx' },
  { label: '部署测试', path: '/deploy-test' },
]

const handleExit = () => {
  // 退出逻辑，如清除token跳转登录页
  console.log('退出登录')
  // 例如: router.push('/login')
}

const handleNavigate = (path) => {
  router.push(path)
}
</script>

<style scoped>
/* ========== 沿用之前提取的色彩变量 ========== */
.home {
  --color-brand: #d97757;
  --color-brand-hover: #c06845;
  --color-dark: #141413;
  --color-bg: #fbf9f7;
  --color-white: #ffffff;
  --color-text: #1a1a19;
  --color-text-secondary: #6b6b6b;
  --color-border: #e8e4df;
  --color-dashed: #cccccc; /* 虚线灰色 */
  --font-display: 'Poppins', -apple-system, BlinkMacSystemFont, sans-serif;
  --font-body: 'Lora', Georgia, 'Times New Roman', serif;
  --radius-button: 0.5em;

  min-height: 100vh;
  background-color: var(--color-bg);
  font-family: var(--font-body);
  color: var(--color-text);
  display: flex;
  flex-direction: column;
}

/* ========== 导航栏 ========== */
.navbar {
  border-bottom: 2px dashed var(--color-dashed);
  padding: 0 24px;
}

.navbar__inner {
  max-width: 1200px;
  margin: 0 auto;
  height: 64px;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.navbar__logo {
  height: 56px;
  width: auto;
}

/* 退出按钮：透明背景、灰色字体、灰色虚线外框、圆角0.5em */
.btn-exit {
  background: transparent;
  color: var(--color-text-secondary);
  border: 1.5px dashed var(--color-dashed);
  border-radius: var(--radius-button);
  padding: 8px 20px;
  font-family: var(--font-display);
  font-size: 15px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.25s ease;
}

.btn-exit:hover {
  color: var(--color-brand);
  border-color: var(--color-brand);
  background: rgba(217, 119, 87, 0.04);
}

/* ========== 主区域 ========== */
.hero {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  text-align: center;
  padding: 40px 24px;
}

.hero__title {
  font-family: var(--font-display);
  font-size: clamp(36px, 6vw, 56px);
  font-weight: 800;
  color: var(--color-dark);
  letter-spacing: -0.02em;
  margin-bottom: 12px;
}

.hero__subtitle {
  font-family: var(--font-body);
  font-size: clamp(18px, 2.5vw, 24px);
  color: var(--color-text-secondary);
  margin-bottom: 48px;
  font-style: italic;
  font-weight: 400;
  letter-spacing: 0.01em;
}

/* 三个按钮容器 */
.hero__actions {
  display: flex;
  gap: 16px;
  flex-wrap: wrap;
  justify-content: center;
}

/* 黑色按钮：黑底白字、圆角0.5em */
.btn-dark {
  background-color: var(--color-dark);
  color: var(--color-white);
  border: none;
  border-radius: var(--radius-button);
  padding: 12px 28px;
  font-family: var(--font-display);
  font-size: 16px;
  font-weight: 600;
  cursor: pointer;
  transition:
    background-color 0.25s,
    transform 0.2s;
  min-width: 120px;
}

.btn-dark:hover {
  background-color: #2c2c2b;
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.btn-dark:active {
  transform: translateY(0);
}
</style>
