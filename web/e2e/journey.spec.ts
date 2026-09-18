// The whole thing in one go, in the order a person does it: edit a configuration, see the
// diagnostic land on the field, start the run, watch it finish, open the results, download the
// CSV, find it in the history.
//
// The per-screen specs check each screen properly. This one checks that they join up — the hand-off
// from the editor to the launcher, from the launcher to the results, from the history back to the
// results — which is where a single-page app with no framework is most likely to be wrong and where
// nothing else looks.
import { expect, test } from '@playwright/test';

import { field, panel, setField } from './support.js';

test('a whole session, from a configuration to a downloaded result', async ({ page }) => {
  await page.goto('/#config');
  await expect(page.getByRole('heading', { name: 'Configuration', level: 2 })).toBeVisible();

  // The engine this page is talking to, which is the masthead's job.
  await expect(page.locator('#engine')).not.toHaveText('');
  await expect(page.locator('#engine')).not.toHaveText('engine unreachable');

  await panel(page).locator('#example').selectOption('synthetic-b');
  await expect(await field(page, 'f_running_seed')).not.toHaveValue('');

  // Break it, and the message arrives on the field.
  await setField(page, 'f_running_trial_runs', '0');
  await panel(page).getByRole('button', { name: 'Validate' }).click();

  const broken = panel(page).locator('div.field:has(#f_running_trial_runs)');
  await expect(broken).toHaveAttribute('data-worst', 'error');
  await expect(broken.locator('div.diagnostic.error')).toContainText('/running/trial_runs');

  // Put it back, and it validates.
  await setField(page, 'f_running_trial_runs', '1');
  await panel(page).getByRole('button', { name: 'Validate' }).click();
  await expect(panel(page).locator('div.diagnostic.warning').first()).toContainText('valid');

  // "Run this" hands the example to the launcher and starts it.
  await panel(page).getByRole('button', { name: 'Run this' }).click();
  await expect(page.getByRole('heading', { name: 'Run', level: 2 })).toBeVisible();
  await expect(panel(page).locator('span.state')).toHaveText('completed', { timeout: 45_000 });
  const id = await panel(page).locator('code').first().innerText();

  // Into the results, by the button the launcher offers.
  await panel(page).getByRole('button', { name: 'See the results' }).click();
  await expect(page.getByRole('heading', { name: 'Results', level: 2 })).toBeVisible();
  await expect(panel(page).locator('#result-run')).toHaveValue(id);
  await expect(panel(page).locator('div.chart-card').first().locator('svg')).toBeVisible();

  // Download the CSV the run wrote, whatever it is called.
  const link = panel(page).locator('a.action[download]').first();
  const [download] = await Promise.all([page.waitForEvent('download'), link.click()]);
  const { readFileSync } = await import('node:fs');
  const text = readFileSync((await download.path())!, 'utf8');
  expect(text.startsWith('source,run,time')).toBe(true);

  // And it is in the history, with its manifest.
  await page.getByRole('button', { name: 'History' }).click();
  await expect(page.getByRole('heading', { name: 'History', level: 2 })).toBeVisible();
  const row = panel(page).locator('tbody tr', { has: page.locator(`code:text-is("${id}")`) });
  await expect(row).toHaveCount(1);
  await row.getByRole('button', { name: 'Manifest' }).click();
  await expect(panel(page).locator('pre.json')).toContainText('"engine"');
});
