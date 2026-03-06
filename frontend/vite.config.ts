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
  define: {
    __BUILD_INFO__: JSON.stringify(getBuildInfo()),
    __BUILD_TIME__: JSON.stringify(new Date().toISOString()),
  },
  server: {
    proxy: {
      '/api': `http://localhost:${backendPort}`,
      '/ws': {
        target: `ws://localhost:${backendPort}`,
        ws: true,
      },
    },
  },
})
