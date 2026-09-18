// Screen 2, the run launcher: start a run, watch it over the event stream, read its manifest.
//
// This is the test that most needed writing. The progress state machine has unit tests; what it
// has never had is a check that the server's server-sent events reach it — that the stream is not
// buffered on the way, that the run's own record arrives at the end, that the bar reaches the end
// because `total_years` agrees with the number of year events.
import { expect, test } from '@playwright/test';

import { openScreen, panel, PACKS, runToCompletion } from './support.js';

test.describe('the run screen', () => {
  test('lists the compatibility flags the build reports', async ({ page }) => {
    const screen = await openScreen(page, 'run', 'Run');

    // Listed by the server so a client need not hard-code them (ADR 0041).
    await expect(screen.locator('#flag-B-24')).toHaveCount(1);
  });

  for (const pack of PACKS) {
    test(`runs ${pack} and reports every year over the stream`, async ({ page }) => {
      const screen = await openScreen(page, 'run', 'Run');
      await screen.locator('#run-example').selectOption(pack);
      await screen.getByRole('button', { name: 'Start', exact: true }).click();

      // The progress line comes only from the event stream, so seeing it is seeing SSE work.
      await expect(screen.locator('p.status')).toBeVisible();
      await expect(screen.locator('span.state')).toHaveText('completed', { timeout: 45_000 });

      // The cohort line comes from `run_started`, the population from the last `year_completed`.
      await expect(screen.locator('p.note').first()).toContainText('people, seed');
      await expect(screen.locator('p.note').first())
        .toContainText('alive at the last year reported');

      // The bar reached the end, which is `total_years` agreeing with the year events counted.
      const width = await screen.locator('div.bar > div').evaluate((node) => node.style.width);
      expect(width).toBe('100%');

      // And the manifest, fetched once at the end rather than polled for.
      const manifest = screen.locator('details.group', { hasText: 'Run manifest' });
      await expect(manifest).toBeVisible();
      await manifest.locator('summary').click();
      await expect(manifest.locator('pre.json')).toContainText('"manifest_version"');
      await expect(manifest.locator('pre.json')).toContainText('"config"');
    });
  }

  test('the run announces the scenarios its configuration asked for', async ({ page }) => {
    // The first pack has one scenario and the second has two. The line comes from `run_started`,
    // so this is the event stream carrying what the configuration decided.
    await runToCompletion(page, 'Synthetic');
    await expect(panel(page).locator('p.note').first()).toContainText('scenarios Baseline');
    await expect(panel(page).locator('p.note').first()).not.toContainText('Intervention');

    await page.reload();
    await runToCompletion(page, 'synthetic-b');
    await expect(panel(page).locator('p.note').first())
      .toContainText('Baseline then Intervention');
  });

  test('offers the results when the run has finished', async ({ page }) => {
    await runToCompletion(page, 'Synthetic');
    await expect(panel(page).getByRole('button', { name: 'See the results' })).toBeEnabled();
  });
});
