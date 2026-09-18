// End-to-end: a real browser, the built frontend, the real `hgps serve`, the real engine.
//
// Everything else in web/ is a unit test over a pure function — the schema walk, the SSE parser,
// the progress state machine, the chart's scaling. They cover the places a mistake is silent, and
// between them they cover none of the wiring: the previous run found three defects by a person
// opening the page in a browser and none by a test (docs/SUMMARY.md). This is that person,
// written down.
//
// The engine is not mocked. `scripts/e2e-server.sh` lays out the two synthetic fixture packs and
// starts the binary over them; a run of one of those packs takes about a fifth of a second, so a
// test can start a run and wait for it to finish without the suite becoming something nobody runs.
import { defineConfig, devices } from '@playwright/test';

const PORT = Number(process.env.HGPS_E2E_PORT ?? 8099);
const BASE_URL = `http://127.0.0.1:${PORT}`;

export default defineConfig({
  testDir: './e2e',
  // One run at a time is the server's contract, not an accident of this configuration
  // (docs/server-api.md), so the tests are serial and there is one worker. Running them in
  // parallel would produce 409s that say nothing about the frontend.
  fullyParallel: false,
  workers: 1,
  forbidOnly: !!process.env.CI,
  // A retry in CI only, and one: a flaky end-to-end test is worth knowing about locally rather
  // than papering over, and worth not failing a whole workflow for on a loaded runner.
  retries: process.env.CI ? 1 : 0,
  timeout: 60_000,
  expect: { timeout: 15_000 },
  reporter: process.env.CI ? [['list'], ['html', { open: 'never' }]] : [['list']],

  use: {
    baseURL: BASE_URL,
    trace: 'retain-on-failure',
    video: process.env.CI ? 'retain-on-failure' : 'off',
  },

  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],

  webServer: {
    command: `../scripts/e2e-server.sh --port ${PORT}`,
    // `/api/version` rather than `/`: the static assets are served by the same process, so if the
    // API answers the whole host is up.
    url: `${BASE_URL}/api/version`,
    reuseExistingServer: !process.env.CI,
    timeout: 60_000,
    stdout: 'pipe',
    stderr: 'pipe',
  },
});
