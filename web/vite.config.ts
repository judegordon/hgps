/// <reference types="vitest/config" />
import { defineConfig } from 'vite';

// The dev server proxies /api to `hgps serve`, so the frontend is developed against the real
// engine rather than a mock — which is the only way the types in src/api/types.ts stay honest
// (docs/decisions/0043-a-plain-typescript-frontend.md). scripts/dev.sh starts both.
const ENGINE = process.env.HGPS_SERVER ?? 'http://127.0.0.1:8080';

export default defineConfig({
  server: {
    port: 5173,
    strictPort: true,
    proxy: {
      '/api': {
        target: ENGINE,
        changeOrigin: false,
        // Server-sent events must not be buffered, or the progress screen updates once at the end.
        configure: (proxy) => {
          proxy.on('proxyRes', (proxyRes) => {
            if (proxyRes.headers['content-type']?.includes('text/event-stream')) {
              proxyRes.headers['cache-control'] = 'no-cache, no-transform';
            }
          });
        },
      },
    },
  },
  build: {
    // Served by `hgps serve --web`, from the directory root.
    outDir: 'dist',
    emptyOutDir: true,
    target: 'es2022',
    sourcemap: true,
  },
  test: {
    environment: 'node',
    include: ['src/**/*.test.ts'],
  },
});
