// What every spec needs: the ids the server is serving, a locator scoped to the screen on show,
// and the two waits that are not a sleep.
import { expect, type Locator, type Page } from '@playwright/test';

/** The two synthetic packs, as `scripts/e2e-server.sh` lays them out. */
export const PACKS = ['Synthetic', 'synthetic-b'] as const;
export type Pack = (typeof PACKS)[number];

/**
 * The screen currently on show.
 *
 * Every screen keeps its own `div` and its state between visits, so all four are in the DOM at
 * once and only one is not `hidden` (web/src/main.ts). A bare `p.note` therefore matches notes on
 * screens nobody is looking at — which is a thing about this app worth knowing when reading these
 * tests, and the reason they scope everything through here.
 */
export function panel(page: Page): Locator {
  return page.locator('div.panel:not([hidden])');
}

/** Opens a screen by its hash route and waits for its heading. */
export async function openScreen(
  page: Page,
  screen: 'config' | 'run' | 'results' | 'history',
  heading: string,
): Promise<Locator> {
  await page.goto(`/#${screen}`);
  await expect(page.getByRole('heading', { name: heading, level: 2 })).toBeVisible();
  return panel(page);
}

/**
 * A field of the configuration form, by the DOM id the editor derives from its JSON pointer, with
 * the section it lives in opened.
 *
 * The sections are `<details>` and are closed unless something in them is wrong, so a field has to
 * be revealed before it can be typed into.
 */
export async function field(page: Page, id: string): Promise<Locator> {
  const section = panel(page).locator(`details.group:has(#${id})`);
  await expect(section).toHaveCount(1);
  await section.evaluate((node) => {
    (node as HTMLDetailsElement).open = true;
  });
  return panel(page).locator(`#${id}`);
}

/** Types into a field and lets its `change` handler run, which is what updates the document. */
export async function setField(page: Page, id: string, value: string): Promise<void> {
  const control = await field(page, id);
  await control.fill(value);
  await control.blur();
}

/**
 * Starts a run from the Run screen and waits for it to reach a terminal state.
 *
 * The wait is on what the page shows, not on the API: the point of these tests is that the event
 * stream reaches the DOM, so polling the server behind the page's back would test the wrong half.
 */
export async function runToCompletion(page: Page, pack: Pack): Promise<string> {
  const screen = await openScreen(page, 'run', 'Run');
  await screen.locator('#run-example').selectOption(pack);

  // The id comes from the server's own answer rather than from the screen. Reading it off the page
  // is what the first version did, and it read the *previous* run's id back: the screen showed the
  // finished run before until the new one's first event arrived, so a second run in the same test
  // appeared to have the same id as the first. The page no longer does that, and this no longer
  // depends on it not doing it.
  const [response] = await Promise.all([
    page.waitForResponse((r) => r.url().endsWith('/api/runs') && r.request().method() === 'POST'),
    screen.getByRole('button', { name: 'Start', exact: true }).click(),
  ]);
  const id = ((await response.json()) as { id: string }).id;
  expect(id).not.toEqual('');

  await expect(screen.locator('code').first()).toHaveText(id);
  await expect(screen.locator('span.state')).toHaveText('completed', { timeout: 45_000 });
  return id;
}
