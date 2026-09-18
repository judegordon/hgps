// Screen 1, the configuration editor: the form comes from the schema the engine publishes, and a
// diagnostic lands on the field it names.
//
// That second half is the reason the editor exists rather than a textarea, and it is the one thing
// no unit test here can check: `groupDiagnostics` is tested over a list of pointers, and this is
// whether the pointer it groups by is the pointer the DOM actually used.
import { expect, test } from '@playwright/test';

import { field, openScreen, panel, PACKS, setField } from './support.js';

test.describe('the configuration screen', () => {
  test('lists every configuration the server can see', async ({ page }) => {
    const screen = await openScreen(page, 'config', 'Configuration');

    const options = screen.locator('#example option');
    await expect(options).toHaveCount(PACKS.length);
    for (const pack of PACKS) {
      await expect(screen.locator(`#example option[value="${pack}"]`)).toHaveCount(1);
    }
  });

  test('generates the form from the published schema', async ({ page }) => {
    await openScreen(page, 'config', 'Configuration');

    // The fields are the ones the loader reads, at the pointers a diagnostic will name.
    await expect(await field(page, 'f_running_seed')).toBeVisible();
    await expect(await field(page, 'f_running_stop_time')).toBeVisible();
    await expect(await field(page, 'f_inputs_dataset_name')).toBeVisible();

    // The values are the chosen configuration's, not blanks: the form opened a document.
    await expect(await field(page, 'f_running_seed')).not.toHaveValue('');
  });

  for (const pack of PACKS) {
    test(`validates ${pack} as it stands`, async ({ page }) => {
      const screen = await openScreen(page, 'config', 'Configuration');
      await screen.locator('#example').selectOption(pack);
      await expect(await field(page, 'f_running_seed')).not.toHaveValue('');

      await screen.getByRole('button', { name: 'Validate' }).click();
      await expect(screen.locator('div.diagnostic.warning').first()).toContainText('valid');
    });
  }

  test('puts a located diagnostic on the field that caused it', async ({ page }) => {
    const screen = await openScreen(page, 'config', 'Configuration');

    // A stop time before the start time. The loader reports it at `/running/stop_time`, and the
    // editor's whole claim is that the message appears there rather than in a list at the top.
    const start = await (await field(page, 'f_running_start_time')).inputValue();
    await setField(page, 'f_running_stop_time', String(Number(start) - 1));

    await screen.getByRole('button', { name: 'Validate' }).click();

    const broken = panel(page).locator('div.field:has(#f_running_stop_time)');
    await expect(broken).toHaveAttribute('data-worst', 'error');
    await expect(broken.locator('div.diagnostic.error')).toContainText('/running/stop_time');

    // And the section it lives in was opened for the reader rather than left closed.
    await expect(panel(page).locator('details.group:has(#f_running_stop_time)'))
      .toHaveAttribute('open', '');
  });

  test('can be told not to insist the files exist yet', async ({ page }) => {
    const screen = await openScreen(page, 'config', 'Configuration');
    await setField(page, 'f_inputs_dataset_name', 'not-written-yet.csv');

    await screen.getByRole('button', { name: 'Validate' }).click();
    await expect(screen.locator('div.diagnostic.error').first()).toContainText('error');

    await screen.locator('#require-files').uncheck();
    await screen.getByRole('button', { name: 'Validate' }).click();
    await expect(screen.locator('div.diagnostic.warning').first()).toContainText('valid');
  });
});
