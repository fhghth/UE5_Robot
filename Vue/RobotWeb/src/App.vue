<template>
  <div class="router-container">
    <router-view v-slot="{ Component, route }">
      <transition :name="route.meta.transition || 'fade'">
        <component :is="Component" :key="route.path" />
      </transition>
    </router-view>
  </div>
</template>

<script setup>
</script>

<style>
/* 路由容器：固定高度，相对定位，设置背景色 */
.router-container {
  position: relative;
  width: 100%;
  min-height: 100vh;
  background-color: #fbf9f7;
  overflow: hidden;
}

/* 默认淡入淡出 */
.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.3s;
}
.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}

/* 左滑动画：新页面从右侧滑入，旧页面向左滑出 */
.slide-left-enter-active,
.slide-left-leave-active {
  transition: transform 0.35s ease-out;
  position: absolute;
  width: 100%;
  top: 0;
  left: 0;
}

.slide-left-enter-from {
  transform: translateX(100%);   /* 新页面从右侧进入 */
}

.slide-left-leave-to {
  transform: translateX(-100%);  /* 旧页面向左离开 */
}

.slide-left-enter-active {
  z-index: 2;  /* 新页面在上层 */
}

.slide-left-leave-active {
  z-index: 1;  /* 旧页面在下层 */
}
</style>
