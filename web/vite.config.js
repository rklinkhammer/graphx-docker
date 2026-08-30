import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

const websocketPath = process.env.GRAPHX_WEBSOCKET_PATH || '/ws'

export default defineConfig({
  plugins: [react()],
  server: { proxy: {
    '/api': 'http://localhost:8080',
    [websocketPath]: { target: 'ws://localhost:8080', ws: true },
  } },
})
