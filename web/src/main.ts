// The shell: four screens and the hash router between them.
//
// No framework, because there are four screens and one of them is a form
// (docs/decisions/0043-a-plain-typescript-frontend.md).
import { api } from './api/client.js';
import { editorScreen } from './screens/editor.js';
import { historyScreen } from './screens/history.js';
import { launcherScreen } from './screens/launcher.js';
import { resultsScreen } from './screens/results.js';
import { clear, el } from './ui/dom.js';

type ScreenName = 'config' | 'run' | 'results' | 'history';

const SCREENS: Array<{ name: ScreenName; label: string }> = [
  { name: 'config', label: 'Configuration' },
  { name: 'run', label: 'Run' },
  { name: 'results', label: 'Results' },
  { name: 'history', label: 'History' },
];

const host = document.getElementById('screen');
const nav = document.getElementById('nav');
const engine = document.getElementById('engine');
if (!host || !nav || !engine) throw new Error('the page is missing its shell');

// Each screen owns a div of its own and keeps its state between visits — so switching to Results
// and back does not lose a half-edited configuration.
const panels = new Map<ScreenName, HTMLElement>();
for (const { name } of SCREENS) {
  const panel = el('div', { class: 'panel', hidden: true });
  panels.set(name, panel);
  host.append(panel);
}

const results = resultsScreen(panels.get('results')!);
const launcher = launcherScreen(panels.get('run')!, (runId) => {
  show('results');
  results.load(runId);
});
const editor = editorScreen(panels.get('config')!, (exampleId) => {
  show('run');
  launcher.runFor(exampleId);
});
const history = historyScreen(panels.get('history')!, (runId) => {
  show('results');
  results.load(runId);
});

const loaders: Record<ScreenName, () => void> = {
  config: () => editor.load(),
  run: () => launcher.load(),
  results: () => results.load(),
  history: () => history.load(),
};

let current: ScreenName | null = null;

function show(name: ScreenName): void {
  if (current === name) return;
  current = name;
  for (const [key, panel] of panels) panel.hidden = key !== name;
  if (window.location.hash !== `#${name}`) window.location.hash = name;
  renderNav();
  loaders[name]();
}

function renderNav(): void {
  clear(nav!);
  for (const { name, label } of SCREENS) {
    const button = el('button', { type: 'button', text: label });
    if (current === name) button.setAttribute('aria-current', 'page');
    button.addEventListener('click', () => show(name));
    nav!.append(button);
  }
}

function fromHash(): ScreenName {
  const name = window.location.hash.replace(/^#/, '') as ScreenName;
  return SCREENS.some((s) => s.name === name) ? name : 'config';
}

window.addEventListener('hashchange', () => show(fromHash()));

// Which engine this page is talking to, and which commit built it — so a result on screen can be
// tied to the binary that produced it without opening the manifest.
void api
  .version()
  .then((version) => {
    engine.textContent =
      `${version.engine_version} · ${version.git_describe || version.git_commit.slice(0, 8)}` +
      `${version.git_dirty ? ' (dirty)' : ''} · ${version.platform}`;
  })
  .catch(() => {
    engine.textContent = 'engine unreachable';
  });

show(fromHash());
