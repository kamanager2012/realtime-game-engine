import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    port: 3000,
    proxy: {
      '/table': {
        target: 'http://localhost:9001',
        ws: true,
        changeOrigin: true,
      },
      '/api': {
        target: 'http://localhost:9001',
      },
    },
  },
});
