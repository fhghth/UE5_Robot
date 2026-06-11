import { createRouter, createWebHistory } from 'vue-router'
import HomePage from '../components/HomePage.vue'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    {
      path: '/',
      name: 'home',
      component: HomePage,
      meta: { transition: 'fade' },
    },
    {
      path: '/train',
      name: 'train',
      component: () => import('../views/TrainView.vue'),
      meta: { transition: 'slide-left' },
    },
    {
      path: '/export-onnx',
      name: 'exportOnnx',
      component: () => import('../views/ExportOnnxView.vue'),
      meta: { transition: 'slide-left' },
    },
    {
      path: '/deploy-test',
      name: 'deployTest',
      component: () => import('../views/DeployTestView.vue'),
      meta: { transition: 'slide-left' },
    },
  ],
})

export default router
