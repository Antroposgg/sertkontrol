import react from '@vitejs/plugin-react';
import { defineConfig } from 'vitest/config';

// Мини-приложение раздаёт certd с того же origin, что и /api/v1 (АРХ §8): CORS не нужен.
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: { '/api': 'http://localhost:8080', '/healthz': 'http://localhost:8080' },
  },
  test: {
    globals: true,
    environment: 'jsdom',
    setupFiles: ['./src/test/setup.ts'],
    coverage: {
      provider: 'v8',
      include: ['src/**/*.{ts,tsx}'],
      exclude: ['src/**/*.test.{ts,tsx}', 'src/test/**', 'src/main.tsx', 'src/vite-env.d.ts'],
      reporter: ['text', 'html', 'cobertura'],
      // Порог ворот-проверки (CLAUDE.md): не ниже 70% строк.
      thresholds: { lines: 70 },
    },
  },
});
