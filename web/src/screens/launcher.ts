// Screen 2: start a run, watch it, cancel it, and read its manifest when it stops.
//
// The progress comes from the engine's own event stream over SSE. The state machine is in
// src/api/run-progress.ts and is tested there; this file is the part that puts it on screen.
import { api, ApiFailure } from '../api/client.js';
import { openRunStream, type Stream } from '../api/events.js';
import {
  applyEvent,
  fractionDone,
  initialProgress,
  isFinished,
  progressLabel,
  type ProgressState,
} from '../api/run-progress.js';
import type { CompatFlag, RunInfo } from '../api/types.js';
import { clear, diagnosticsNode, el, failureNode, formatDuration } from '../ui/dom.js';

interface State {
  examples: string[];
  chosen: string | null;
  threads: number;
  compat: Set<string>;
  flags: CompatFlag[];
  run: RunInfo | null;
  progress: ProgressState;
  error: unknown;
  starting: boolean;
}

export function launcherScreen(
  root: HTMLElement,
  onResults: (runId: string) => void,
): { load: () => void; runFor: (exampleId: string) => void } {
  const state: State = {
    examples: [],
    chosen: null,
    threads: 1,
    compat: new Set(),
    flags: [],
    run: null,
    progress: initialProgress(),
    error: null,
    starting: false,
  };

  let stream: Stream | null = null;

  function stopStream(): void {
    stream?.close();
    stream = null;
  }

  async function load(): Promise<void> {
    try {
      const [examples, version, runs] = await Promise.all([
        api.examples(),
        api.version(),
        api.runs(),
      ]);
      state.examples = examples.examples.filter((e) => e.readable).map((e) => e.id);
      state.flags = version.baseline_compat_flags;
      state.chosen ??= state.examples[0] ?? null;

      // A run may already be going — this page was reloaded, or another tab started one. Attaching
      // to it is the whole reason the server buffers its events.
      if (runs.active) {
        const active = await api.run(runs.active);
        attach(active);
        return;
      }
      state.error = null;
    } catch (error) {
      state.error = error;
    }
    render();
  }

  function attach(run: RunInfo): void {
    stopStream();
    state.run = run;
    state.progress = initialProgress();
    state.error = null;
    render();

    stream = openRunStream(api.eventsUrl(run.id), {
      onEvent: (event) => {
        state.progress = applyEvent(state.progress, event);
        render();
      },
      onError: (error) => {
        state.error = error;
        render();
      },
      onEnd: () => {
        // The run's own record has the manifest and the file list, which the stream does not
        // carry. One fetch at the end rather than polling throughout.
        void api.run(run.id).then((finished) => {
          state.run = finished;
          render();
        });
      },
    });
  }

  async function start(): Promise<void> {
    if (!state.chosen) return;
    state.starting = true;
    state.error = null;
    // The run before goes off the screen now rather than when the new one's first event arrives.
    // Until it did, pressing Start left the previous run's id, its `completed` state and its "See
    // the results" button on screen for as long as the POST took — a page saying something untrue
    // about the button that had just been pressed. Found by the end-to-end tests, whose helper
    // read that stale id and thought two runs had the same one.
    stopStream();
    state.run = null;
    state.progress = initialProgress();
    render();
    try {
      const run = await api.startRun({
        example: state.chosen,
        threads: state.threads,
        ...(state.compat.size > 0 ? { baseline_compat: [...state.compat] } : {}),
      });
      attach(run);
    } catch (error) {
      state.error = error;
      state.starting = false;
      if (error instanceof ApiFailure && error.code === 'run_in_progress') {
        // Refused rather than queued, by design. Attaching to the run that is going is the useful
        // thing to do about it.
        void load();
        return;
      }
      // Rendered *after* clearing `starting`, not in a `finally` that runs later: the button is
      // disabled while a start is in flight, and leaving it disabled after a failed one means the
      // only way to try again is a page reload.
      render();
      return;
    }
    state.starting = false;
  }

  async function cancel(): Promise<void> {
    if (!state.run) return;
    try {
      await api.cancelRun(state.run.id);
      // Nothing is set here on purpose: the run stops at the end of the year it is in, and the
      // `cancel_requested` event is what moves the button into its pending state.
    } catch (error) {
      state.error = error;
      render();
    }
  }

  function render(): void {
    clear(root);
    root.append(el('h2', { text: 'Run' }));
    root.append(
      el('p', { class: 'lede' },
        'One run happens at a time — the engine runs two scenarios in sequence in one process, ',
        'which is what makes the output byte-identical from run to run.'),
    );

    const chooser = el('select', { id: 'run-example' });
    for (const id of state.examples) {
      const option = el('option', { value: id, text: id });
      if (id === state.chosen) option.selected = true;
      chooser.append(option);
    }
    chooser.addEventListener('change', () => { state.chosen = chooser.value; });

    const threads = el('input', { type: 'number', id: 'threads', min: '1', max: '32' });
    threads.value = String(state.threads);
    threads.addEventListener('change', () => {
      state.threads = Math.max(1, Number(threads.value) || 1);
    });

    const running = state.run !== null && !isFinished(state.progress.state);
    const startButton = el('button', { class: 'action', type: 'button', text: 'Start' });
    startButton.disabled = state.starting || running || state.chosen === null;
    startButton.addEventListener('click', () => void start());

    const cancelButton = el('button', { class: 'action quiet', type: 'button' });
    cancelButton.textContent = state.progress.cancelRequested ? 'Stopping…' : 'Cancel';
    cancelButton.disabled = !running || state.progress.cancelRequested;
    cancelButton.addEventListener('click', () => void cancel());

    root.append(
      el('div', { class: 'row' },
        el('label', { for: 'run-example', text: 'Example' }), chooser,
        el('label', { for: 'threads', text: 'Threads' }), threads,
        startButton, cancelButton),
    );

    if (state.flags.length > 0) {
      const boxes = el('div', { class: 'row' },
        el('span', { class: 'note', text: 'Baseline compatibility:' }));
      for (const flag of state.flags) {
        const box = el('input', { type: 'checkbox', id: `flag-${flag.name}` });
        box.checked = state.compat.has(flag.name);
        box.disabled = running;
        box.addEventListener('change', () => {
          if (box.checked) state.compat.add(flag.name);
          else state.compat.delete(flag.name);
        });
        boxes.append(el('label', { for: `flag-${flag.name}`, title: flag.description },
          box, ` ${flag.name}`));
      }
      boxes.append(
        el('span', { class: 'note' },
          'with a flag on, the engine reproduces that baseline defect on purpose, and the run ',
          'manifest records it'),
      );
      root.append(boxes);
    }

    if (state.error) root.append(failureNode(state.error));

    if (state.run === null) {
      root.append(el('p', { class: 'empty', text: 'No run yet.' }));
      return;
    }

    const progress = state.progress;
    const fraction = fractionDone(progress);

    root.append(
      el('div', { class: 'row' },
        el('code', { text: state.run.id }),
        el('span', { class: `state ${progress.state}`, text: progress.state }),
        progress.truncated
          ? el('span', { class: 'note', text: 'the event buffer dropped its oldest entries' })
          : null),
    );

    const bar = el('div', { class: 'bar' });
    const filled = el('div');
    filled.style.width = `${Math.round((fraction ?? 0) * 100)}%`;
    bar.append(filled);
    root.append(bar);
    root.append(el('p', { class: 'status', text: progressLabel(progress) }));

    if (progress.cohortSize !== null) {
      root.append(
        el('p', { class: 'note' },
          `${progress.cohortSize.toLocaleString()} people, seed ${progress.seed}, `,
          `scenarios ${progress.scenarios.join(' then ')}`,
          progress.population !== null
            ? ` — ${progress.population.toLocaleString()} alive at the last year reported`
            : ''),
      );
    }

    const diagnostics = diagnosticsNode([
      ...(state.run.diagnostics ?? []),
      ...progress.diagnostics,
    ]);
    if (diagnostics) root.append(diagnostics);

    if (isFinished(progress.state)) {
      const results = el('button', { class: 'action', type: 'button', text: 'See the results' });
      results.addEventListener('click', () => onResults(state.run!.id));
      root.append(el('div', { class: 'row' }, results,
        el('span', { class: 'note', text: `took ${formatDuration(progress.elapsedMs)}` })));

      // The manifest is what makes a result file evidence rather than a table of numbers, so it is
      // shown rather than linked.
      if (state.run.manifest) {
        const details = el('details', { class: 'group' });
        details.append(el('summary', { text: 'Run manifest' }));
        details.append(
          el('div', { class: 'group-body' },
            el('pre', { class: 'json', text: JSON.stringify(state.run.manifest, null, 2) })),
        );
        root.append(details);
      }
    }
  }

  render();
  return {
    load: () => void load(),
    runFor: (exampleId: string) => {
      state.chosen = exampleId;
      void load().then(() => void start());
    },
  };
}
