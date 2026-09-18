import { describe, expect, it } from 'vitest';
import {
  applyEvent,
  fractionDone,
  initialProgress,
  isFinished,
  progressLabel,
} from './run-progress.js';
import type { RunEvent } from './types.js';

const started: RunEvent = {
  type: 'run_started', engine_version: '0.2.0', seed: 1, trial_runs: 1,
  start_time: 2010, stop_time: 2012, cohort_size: 100,
  scenarios: ['Baseline', 'Intervention'], total_years: 6,
};

const year = (scenario: string, y: number, run = 1): RunEvent => ({
  type: 'year_completed', scenario, kind: 'baseline', run, year: y,
  elapsed_ms: 1, population: 99,
});

describe('the run progress reducer', () => {
  it('starts knowing nothing', () => {
    const progress = initialProgress();
    expect(progress.state).toBe('starting');
    expect(progress.yearsCompleted).toBe(0);
    expect(fractionDone(progress)).toBeNull();
  });

  it('takes the denominator from run_started', () => {
    const progress = applyEvent(initialProgress(), started);
    expect(progress.state).toBe('running');
    expect(progress.totalYears).toBe(6);
    expect(progress.cohortSize).toBe(100);
    expect(fractionDone(progress)).toBe(0);
  });

  it('counts each year once however many times the event arrives', () => {
    // The server replays its whole buffer on connect, and again on every reconnect. A reducer that
    // counted a replayed year would show 200% progress, which is the bug this guards.
    let progress = applyEvent(initialProgress(), started);
    for (const event of [year('Baseline', 2010), year('Baseline', 2011)]) {
      progress = applyEvent(progress, event);
    }
    expect(progress.yearsCompleted).toBe(2);

    for (const event of [year('Baseline', 2010), year('Baseline', 2011)]) {
      progress = applyEvent(progress, event);
    }
    expect(progress.yearsCompleted).toBe(2);
  });

  it('tells the two scenarios apart, and the trial runs', () => {
    let progress = applyEvent(initialProgress(), started);
    for (const event of [
      year('Baseline', 2010),
      year('Intervention', 2010),
      year('Baseline', 2010, 2),
    ]) {
      progress = applyEvent(progress, event);
    }
    expect(progress.yearsCompleted).toBe(3);
  });

  it('does not mutate the state it was given', () => {
    const before = applyEvent(initialProgress(), started);
    const snapshot = before.yearsCompleted;
    applyEvent(before, year('Baseline', 2010));
    expect(before.yearsCompleted).toBe(snapshot);
  });

  it('shows cancellation as pending until the run acts on it', () => {
    // POST /cancel returns 202 and the engine stops at the end of the year it is in. A button that
    // said "cancelled" straight away would be lying for up to a minute on a large example.
    let progress = applyEvent(applyEvent(initialProgress(), started), year('Baseline', 2010));
    progress = applyEvent(progress, { type: 'cancel_requested', note: 'x' });

    expect(progress.cancelRequested).toBe(true);
    expect(isFinished(progress.state)).toBe(false);
    expect(progressLabel(progress)).toContain('stopping');

    progress = applyEvent(progress, { type: 'state', state: 'cancelled' });
    expect(progress.cancelRequested).toBe(false);
    expect(progress.state).toBe('cancelled');
    expect(progressLabel(progress)).toContain('cancelled after 1 of 6');
  });

  it('takes the final state from the state event and not from run_completed', () => {
    // run_completed carries `cancelled`, and so does the state event. Reading one of them keeps
    // the two from disagreeing.
    let progress = applyEvent(initialProgress(), started);
    progress = applyEvent(progress, {
      type: 'run_completed', elapsed_ms: 1234, cancelled: false, outputs: ['result.csv'],
    });
    expect(progress.state).toBe('running');
    expect(progress.outputs).toEqual(['result.csv']);

    progress = applyEvent(progress, { type: 'state', state: 'completed' });
    expect(progress.state).toBe('completed');
    expect(progressLabel(progress)).toBe('finished in 1.2 s');
  });

  it('shows each diagnostic once under replay', () => {
    const diagnostic: RunEvent = {
      type: 'diagnostic', severity: 'warning', code: 'immigration_shortfall',
      message: 'a band could not be refilled', location: {},
    };
    let progress = applyEvent(initialProgress(), diagnostic);
    progress = applyEvent(progress, diagnostic);
    expect(progress.diagnostics).toHaveLength(1);
  });

  it('remembers that the buffer was truncated', () => {
    const progress = applyEvent(initialProgress(), {
      type: 'state', state: 'running', truncated: true,
    });
    expect(progress.truncated).toBe(true);
  });

  it('never reports more than complete', () => {
    let progress = applyEvent(initialProgress(), { ...started, total_years: 2 });
    for (const y of [2010, 2011, 2012, 2013]) {
      progress = applyEvent(progress, year('Baseline', y));
    }
    expect(fractionDone(progress)).toBe(1);
  });
});
