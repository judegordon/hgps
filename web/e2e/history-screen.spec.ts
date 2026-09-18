// Screen 4, the history: every run this server knows about, read from the manifests on disk.
//
// There is no database, which is the claim this screen makes in its own lede. What a browser can
// check is the half it can see: a run that has just finished is in the list, its manifest opens,
// and its Results button reaches the run it names.
import { expect, test } from '@playwright/test';

import { openScreen, panel, runToCompletion } from './support.js';

test.describe('the history screen', () => {
  test('shows a run that has just finished, with its manifest', async ({ page }) => {
    const id = await runToCompletion(page, 'Synthetic');

    const screen = await openScreen(page, 'history', 'History');
    const row = screen.locator('tbody tr', { has: page.locator(`code:text-is("${id}")`) });
    await expect(row).toHaveCount(1);
    await expect(row.locator('span.state')).toHaveText('completed');
    await expect(row).toContainText('Synthetic');

    await row.getByRole('button', { name: 'Manifest' }).click();
    const json = screen.locator('pre.json');
    await expect(json).toBeVisible();
    await expect(json).toContainText('"engine"');
    await expect(json).toContainText('"scenarios"');
  });

  test('its Results button opens that run', async ({ page }) => {
    const id = await runToCompletion(page, 'synthetic-b');

    const screen = await openScreen(page, 'history', 'History');
    const row = screen.locator('tbody tr', { has: page.locator(`code:text-is("${id}")`) });
    await row.getByRole('button', { name: 'Results' }).click();

    await expect(page.getByRole('heading', { name: 'Results', level: 2 })).toBeVisible();
    await expect(panel(page).locator('#result-run')).toHaveValue(id);
  });

  test('a second run appears alongside the first', async ({ page }) => {
    const first = await runToCompletion(page, 'Synthetic');
    const second = await runToCompletion(page, 'synthetic-b');
    expect(second).not.toEqual(first);

    const screen = await openScreen(page, 'history', 'History');
    await expect(screen.locator(`tbody tr code:text-is("${first}")`)).toHaveCount(1);
    await expect(screen.locator(`tbody tr code:text-is("${second}")`)).toHaveCount(1);
  });
});
