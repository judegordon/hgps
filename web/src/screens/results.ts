// Screen 3: the results browser — a table and a line chart per variable, baseline against
// intervention, with a CSV download.
//
// The reduction is the server's, deliberately: it is the same count-weighted rule the equivalence
// harness reduces by, so a chart here and a comparison there mean the same thing by `mean_bmi`
// (ADR 0042).
import { api } from '../api/client.js';
import type { ResultSummary, RunInfo } from '../api/types.js';
import { colourFor, renderChart, type Line } from '../ui/chart.js';
import { clear, el, failureNode } from '../ui/dom.js';

interface State {
  runs: RunInfo[];
  runId: string | null;
  run: RunInfo | null;
  summary: ResultSummary | null;
  sex: string;
  selected: Set<string>;
  view: 'charts' | 'table';
  error: unknown;
  busy: boolean;
}

/** Variables worth offering first: the ones a reader asks about before the other ninety. */
const PREFERRED = [
  'mean_bmi', 'count', 'deaths', 'mean_energy', 'mean_yll', 'mean_daly',
];

export function resultsScreen(root: HTMLElement): { load: (runId?: string) => void } {
  const state: State = {
    runs: [],
    runId: null,
    run: null,
    summary: null,
    sex: 'all',
    selected: new Set(),
    view: 'charts',
    error: null,
    busy: false,
  };

  async function load(runId?: string): Promise<void> {
    state.busy = true;
    render();
    try {
      const listing = await api.runs();
      state.runs = listing.runs.filter((r) => r.state === 'completed' || r.state === 'cancelled');
      const chosen = runId ?? state.runId ?? state.runs[0]?.id ?? null;
      state.runId = chosen;

      if (chosen) {
        state.run = await api.run(chosen);
        state.summary = await api.summary(chosen, { sex: state.sex });
        if (state.selected.size === 0) {
          const offered = PREFERRED.filter((v) => state.summary?.variables.includes(v));
          state.selected = new Set(offered.length > 0 ? offered.slice(0, 3)
                                                      : state.summary.variables.slice(0, 3));
        }
      } else {
        state.run = null;
        state.summary = null;
      }
      state.error = null;
    } catch (error) {
      state.error = error;
    } finally {
      state.busy = false;
      render();
    }
  }

  async function reloadSummary(): Promise<void> {
    if (!state.runId) return;
    state.busy = true;
    render();
    try {
      state.summary = await api.summary(state.runId, { sex: state.sex });
      state.error = null;
    } catch (error) {
      state.error = error;
    } finally {
      state.busy = false;
      render();
    }
  }

  function chartFor(variable: string): HTMLElement {
    const summary = state.summary!;
    const lines: Line[] = summary.scenarios.map((scenario, index) => {
      const series = summary.series.find(
        (s) => s.scenario === scenario && s.variable === variable,
      );
      return {
        label: scenario,
        values: series?.values ?? summary.years.map(() => null),
        colour: colourFor(index),
        // The intervention dashed as well as coloured, so the two are distinguishable in print
        // and to a reader who cannot tell the colours apart.
        dashed: index > 0,
      };
    });

    const card = el('div', { class: 'chart-card' });
    card.append(el('h4', { text: variable }));
    card.append(renderChart(summary.years, lines, { yLabel: variable }));
    card.append(
      el('div', { class: 'legend' },
        ...lines.map((line) => {
          const swatch = el('span', { class: 'swatch' });
          swatch.style.background = line.colour;
          if (line.dashed) swatch.style.backgroundImage =
            'repeating-linear-gradient(90deg, currentColor 0 5px, transparent 5px 8px)';
          return el('span', {}, swatch, line.label);
        })),
    );
    return card;
  }

  function table(): HTMLElement {
    const summary = state.summary!;
    const chosen = [...state.selected];
    const wrapper = el('div', { class: 'scroll' });
    const node = el('table');

    const head = el('tr', {}, el('th', { text: 'year' }));
    for (const scenario of summary.scenarios) {
      for (const variable of chosen) {
        head.append(el('th', { text: `${scenario} · ${variable}` }));
      }
    }
    node.append(el('thead', {}, head));

    const body = el('tbody');
    summary.years.forEach((year, index) => {
      const row = el('tr', {}, el('td', { text: String(year) }));
      for (const scenario of summary.scenarios) {
        for (const variable of chosen) {
          const series = summary.series.find(
            (s) => s.scenario === scenario && s.variable === variable,
          );
          const value = series?.values[index];
          row.append(
            el('td', { class: 'number' },
              value === null || value === undefined ? '—' : formatValue(value)),
          );
        }
      }
      body.append(row);
    });
    node.append(body);
    wrapper.append(node);
    return wrapper;
  }

  function render(): void {
    clear(root);
    root.append(el('h2', { text: 'Results' }));

    const chooser = el('select', { id: 'result-run' });
    for (const run of state.runs) {
      const option = el('option', {
        value: run.id,
        text: `${run.id} — ${run.example}${run.cancelled ? ' (cancelled)' : ''}`,
      });
      if (run.id === state.runId) option.selected = true;
      chooser.append(option);
    }
    chooser.addEventListener('change', () => void load(chooser.value));

    const sex = el('select', { id: 'sex' });
    for (const value of ['all', 'male', 'female']) {
      const option = el('option', { value, text: value });
      if (value === state.sex) option.selected = true;
      sex.append(option);
    }
    sex.addEventListener('change', () => {
      state.sex = sex.value;
      void reloadSummary();
    });

    const toggle = el('button', { class: 'action quiet', type: 'button' });
    toggle.textContent = state.view === 'charts' ? 'Show the table' : 'Show the charts';
    toggle.addEventListener('click', () => {
      state.view = state.view === 'charts' ? 'table' : 'charts';
      render();
    });

    const row = el('div', { class: 'row' },
      el('label', { for: 'result-run', text: 'Run' }), chooser,
      el('label', { for: 'sex', text: 'Sex' }), sex,
      toggle);

    for (const name of state.run?.results ?? []) {
      if (!name.endsWith('.csv')) continue;
      row.append(el('a', { class: 'action quiet', href: api.resultUrl(state.run!.id, name),
                           download: name, text: `Download ${name}` }));
    }
    root.append(row);

    if (state.error) root.append(failureNode(state.error));

    if (!state.summary) {
      root.append(el('p', { class: 'empty',
        text: state.busy ? 'Loading…' : 'No completed run to show.' }));
      return;
    }

    root.append(el('p', { class: 'note', text: state.summary.reduction }));

    const picker = el('div', { class: 'row' });
    for (const variable of state.summary.variables) {
      const box = el('input', { type: 'checkbox', id: `v-${variable}` });
      box.checked = state.selected.has(variable);
      box.addEventListener('change', () => {
        if (box.checked) state.selected.add(variable);
        else state.selected.delete(variable);
        render();
      });
      picker.append(el('label', { for: `v-${variable}` }, box, ` ${variable}`));
    }
    const details = el('details', { class: 'group' });
    details.append(el('summary', {}, 'Variables',
      el('span', { class: 'count', text: `${state.selected.size} of ${state.summary.variables.length} shown` })));
    details.append(el('div', { class: 'group-body' }, picker));
    root.append(details);

    if (state.selected.size === 0) {
      root.append(el('p', { class: 'empty', text: 'Choose a variable.' }));
      return;
    }

    if (state.view === 'charts') {
      for (const variable of state.selected) root.append(chartFor(variable));
    } else {
      root.append(table());
    }
  }

  render();
  return { load: (id) => void load(id) };
}

/** Enough digits to be useful, without pretending to more precision than a mean has. */
function formatValue(value: number): string {
  if (!Number.isFinite(value)) return '—';
  if (value === 0) return '0';
  const magnitude = Math.abs(value);
  if (magnitude >= 1000) return value.toFixed(0);
  if (magnitude >= 1) return value.toFixed(3);
  return value.toPrecision(3);
}
