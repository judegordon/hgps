// Screen 3, the results browser: the reduction the server computes, drawn, tabulated and
// downloadable.
//
// The download is the part worth an end-to-end test. `api.resultUrl` builds a path and the anchor
// carries `download`; whether the bytes that arrive are the bytes the engine wrote is a question
// only a browser can answer, and the server's own byte-identity test stops at the HTTP response.
import { expect, test } from '@playwright/test';

import { panel, PACKS, runToCompletion } from './support.js';

test.describe('the results screen', () => {
  for (const pack of PACKS) {
    test(`charts and tabulates a ${pack} run`, async ({ page }) => {
      await runToCompletion(page, pack);
      await panel(page).getByRole('button', { name: 'See the results' }).click();

      await expect(page.getByRole('heading', { name: 'Results', level: 2 })).toBeVisible();
      const screen = panel(page);

      // The reduction says what it did, in the words docs/equivalence-method.md uses.
      await expect(screen.locator('p.note').first()).toContainText('count-weighted');

      // A chart per selected variable, each an SVG with a line in it.
      const charts = screen.locator('div.chart-card');
      await expect(charts.first()).toBeVisible();
      await expect(charts.first().locator('svg path')).not.toHaveCount(0);

      // The same numbers as a table.
      await screen.getByRole('button', { name: 'Show the table' }).click();
      const table = screen.locator('table').first();
      await expect(table.locator('tbody tr').first()).toBeVisible();

      // One row per year of the run's horizon, and the first cell is the first year.
      const years = await table.locator('tbody tr td:first-child').allInnerTexts();
      expect(years.length).toBeGreaterThan(1);
      expect(Number(years[0])).toBeGreaterThan(1900);
    });
  }

  test('narrowing to one sex asks the server again and redraws', async ({ page }) => {
    await runToCompletion(page, 'Synthetic');
    await panel(page).getByRole('button', { name: 'See the results' }).click();
    const screen = panel(page);
    await expect(screen.locator('div.chart-card').first()).toBeVisible();

    await screen.locator('#sex').selectOption('male');
    await expect(panel(page).locator('div.chart-card').first()).toBeVisible();
    await expect(panel(page).locator('p.empty')).toHaveCount(0);
  });

  test('charts an income-stratified file through the family selector', async ({ page }) => {
    // The second pack, and only the second: it is the one whose `StaticLinear` model gives a
    // person an income category, so it is the only fixture that writes stratum files at all
    // (ADR 0047). Until this run the summary endpoint could reduce the whole-population file and
    // nothing else, so a stratified series could be downloaded and not looked at — which is part
    // of how 45 of its columns stayed empty for as long as they did.
    await runToCompletion(page, 'synthetic-b');
    await panel(page).getByRole('button', { name: 'See the results' }).click();

    const screen = panel(page);
    const family = screen.locator('#family');
    await expect(family).toBeEnabled();

    // One option per CSV the run wrote: the whole population and the three income categories the
    // pack's configuration declares.
    const options = await family.locator('option').allInnerTexts();
    expect(options).toEqual(['HighIncome', 'LowIncome', 'MiddleIncome', 'result']);

    const whole = await screen.locator('p#reduction').innerText();

    await family.selectOption('LowIncome');
    const stratified = panel(page).locator('p#reduction');
    // The note names the file that was reduced, so the screen says which one is on show rather
    // than leaving the reader to trust the selector.
    await expect(stratified).toContainText('LowIncome.csv');
    expect(await stratified.innerText()).not.toBe(whole);

    // And it is a chart of that file, drawn, rather than an empty state.
    await expect(panel(page).locator('div.chart-card').first()).toBeVisible();
    await expect(panel(page).locator('div.chart-card svg path').first()).toBeVisible();
    await expect(panel(page).locator('p.empty')).toHaveCount(0);
  });

  test('the family selector is disabled for a run with only one CSV', async ({ page }) => {
    // The first pack has no income model, so there is one file and nothing to choose between. A
    // selector offering one option would be a control that does nothing.
    await runToCompletion(page, 'Synthetic');
    await panel(page).getByRole('button', { name: 'See the results' }).click();

    const family = panel(page).locator('#family');
    await expect(family).toBeDisabled();
    await expect(family.locator('option')).toHaveCount(1);
  });

  test('downloads the result CSV the run actually wrote', async ({ page }) => {
    // The second pack, deliberately: its output is not called `result.csv` and is not called the
    // same thing twice, so a link built from a remembered name would fail here and nowhere else.
    await runToCompletion(page, 'synthetic-b');
    await panel(page).getByRole('button', { name: 'See the results' }).click();

    const link = panel(page).locator('a.action[download]').first();
    await expect(link).toBeVisible();
    const name = (await link.getAttribute('download')) ?? '';
    expect(name.endsWith('.csv')).toBe(true);
    expect(name).not.toBe('result.csv');

    const [download] = await Promise.all([page.waitForEvent('download'), link.click()]);
    expect(download.suggestedFilename()).toBe(name);

    const path = await download.path();
    expect(path).not.toBeNull();

    const { readFileSync } = await import('node:fs');
    const text = readFileSync(path!, 'utf8');
    // The engine's own header, and both scenarios in the rows: the file that arrived is a result
    // file rather than an error page with a 200.
    expect(text.startsWith('source,run,time')).toBe(true);
    expect(text).toContain('Baseline,');
    expect(text).toContain('Intervention,');
  });
});
