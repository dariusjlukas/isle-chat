import { defineConfig } from 'vite'
import { execSync } from 'child_process'
import { randomUUID } from 'crypto'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

function getBuildInfo(): string {
  try {
    const tag = execSync('git describe --tags --exact-match 2>/dev/null', {
      encoding: 'utf-8',
    }).trim()
    if (tag) return tag
  } catch {
    // No exact tag on this commit
  }
  return randomUUID().split('-')[0]
}

const backendPort = process.env.VITE_BACKEND_PORT ?? '9001'

export default defineConfig({
  plugins: [react(), tailwindcss()],
  build: {
    chunkSizeWarningLimit: 700,
    rollupOptions: {
      output: {
        manualChunks: {
          'vendor-heroui': ['@heroui/react'],
          'vendor-motion': ['framer-motion'],
          'vendor-emoji': ['@emoji-mart/react', '@emoji-mart/data', 'emoji-mart'],
          'vendor-icons': [
            '@fortawesome/fontawesome-svg-core',
            '@fortawesome/free-solid-svg-icons',
            '@fortawesome/react-fontawesome',
          ],
          'vendor-react': ['react', 'react-dom'],
          'vendor-three': ['three', '@react-three/fiber', '@react-three/drei'],
        },
      },
    },
  },
  define: {
    __BUILD_INFO__: JSON.stringify(getBuildInfo()),
    __BUILD_TIME__: JSON.stringify(new Date().toISOString()),
  },
  server: {
    watch: {
      ignored: ['**/v86-assets/**'],
    },
    proxy: {
      '/api': `http://localhost:${backendPort}`,
      '/ws': {
        target: `ws://localhost:${backendPort}`,
        ws: true,
      },
    },
  },
})
