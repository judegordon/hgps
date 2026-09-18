// Screen 4: every run this server knows about, from the manifests in its runs directory.
//
// There is no database. A completed run is its manifest, which is why this list survives a restart
// and why a runs directory copied from another machine shows up correctly (ADR 0034, ADR 0042).
import { api } from '../api/client.js';
import type { RunInfo } from '../api/types.js';
import { clear, el, failureNode, formatDuration, formatTime } from '../ui/dom.js';

interface State {
  runs: RunInfo[];
  active: string | null;
  expanded: string | null;
  detail: RunInfo | null;
  error: unknown;
  busy: boolean;
}

export function historyScreen(
  root: HTMLElement,
  onResults: (runId: string) => void,
): { load: () => void } {
  const state: State = {
    runs: [], active: null, expanded: null, detail: null, error: null, busy: false,
  };

  async function load(): Promise<void> {
    state.busy = true;
    render();
    try {
      const listing = await api.runs();
      state.runs = listing.runs;
      state.active = listing.active;
      state.error = null;
    } catch (error) {
      state.error = error;
    } finally {
      state.busy = false;
      render();
    }
  }

  async function expand(id: string): Promise<void> {
    if (state.expanded === id) {
      state.expanded = null;
      state.detail = null;
      render();
      return;
    }
    state.expanded = id;
    state.detail = null;
    render();
    try {
      state.detail = await api.run(id);
    } catch (error) {
      state.error = error;
    }
    render();
  }

  function render(): void {
    clear(root);
    root.append(el('h2', { text: 'History' }));
    root.append(
      el('p', { class: 'lede' },
        'Read from each run’s manifest, not from a database — so this list survives a ',
        'restart, and a runs directory copied from another machine shows up here.'),
    );

    const refresh = el('button', { class: 'action quiet', type: 'button', text: 'Refresh' });
    refresh.disabled = state.busy;
    refresh.addEventListener('click', () => void load());
    root.append(el('div', { class: 'row' }, refresh,
      state.active
        ? el('span', { class: 'note', text: `run ${state.active} is going now` })
        : null));

    if (state.error) root.append(failureNode(state.error));

    if (state.runs.length === 0) {
      root.append(el('p', { class: 'empty', text: state.busy ? 'Loading…' : 'No runs yet.' }));
      return;
    }

    const table = el('table');
    table.append(
      el('thead', {}, el('tr', {},
        el('th', { text: 'run' }), el('th', { text: 'example' }), el('th', { text: 'state' }),
        el('th', { text: 'started' }), el('th', { text: 'took' }),
        el('th', { text: 'years' }), el('th', { text: '' }))),
    );

    const body = el('tbody');
    for (const run of state.runs) {
      const open = el('button', { class: 'action quiet', type: 'button', text: 'Manifest' });
      open.addEventListener('click', () => void expand(run.id));

      const results = el('button', { class: 'action quiet', type: 'button', text: 'Results' });
      results.disabled = run.state !== 'completed' && run.state !== 'cancelled';
      results.addEventListener('click', () => onResults(run.id));

      body.append(
        el('tr', {},
          el('td', {}, el('code', { text: run.id })),
          el('td', { text: run.example || '—' }),
          el('td', {}, el('span', { class: `state ${run.state}`, text: run.state })),
          el('td', { text: formatTime(run.started_utc) }),
          el('td', { class: 'number' }, formatDuration(run.elapsed_ms)),
          el('td', { class: 'number' },
            run.total_years > 0
              ? `${run.years_completed} / ${run.total_years}`
              : String(run.years_completed)),
          el('td', {}, open, ' ', results)),
      );

      if (state.expanded === run.id) {
        const cell = el('td', { colspan: '7' });
        if (state.detail?.manifest) {
          cell.append(el('pre', { class: 'json',
            text: JSON.stringify(state.detail.manifest, null, 2) }));
        } else if (state.detail) {
          cell.append(el('p', { class: 'note',
            text: 'This run wrote no manifest — it was cancelled before it could, or it failed.' }));
        } else {
          cell.append(el('p', { class: 'note', text: 'Loading…' }));
        }
        body.append(el('tr', {}, cell));
      }
    }
    table.append(body);
    root.append(el('div', { class: 'scroll' }, table));
  }

  render();
  return { load: () => void load() };
}
