// What a stream of run events means for what is on screen.
//
// Pure: events in, state out, no DOM. That is what makes it the one part of this frontend with
// unit tests worth writing (docs/decisions/0043-a-plain-typescript-frontend.md).
//
// Two properties it has to have, both of which come from the server's contract:
//
//   * **Idempotent under replay.** A client that connects late is sent the whole buffer before the
//     live events, and a reconnect starts that again. Applying the same event twice must not count
//     a year twice.
//   * **Honest about cancellation.** `POST /cancel` returns 202 and the engine stops at the end of
//     the year it is in, so there is a state between "asked" and "stopped" that the button has to
//     show. Pretending the run has stopped is the thing not to do.
import type { Diagnostic, RunEvent, RunState } from './types.js';

export interface ProgressState {
  state: RunState;
  /** True once cancellation has been asked for and before the run has acted on it. */
  cancelRequested: boolean;
  yearsCompleted: number;
  totalYears: number;
  /** The most recent year finished, for a line of text. */
  currentYear: number | null;
  currentScenario: string | null;
  scenarios: string[];
  cohortSize: number | null;
  seed: number | null;
  population: number | null;
  elapsedMs: number | null;
  outputs: string[];
  diagnostics: Diagnostic[];
  /** True if the server said its event buffer dropped entries, so the replay has a hole. */
  truncated: boolean;
  /** Every (scenario, run, year) already counted, so a replay cannot count one twice. */
  seenYears: Set<string>;
}

export function initialProgress(): ProgressState {
  return {
    state: 'starting',
    cancelRequested: false,
    yearsCompleted: 0,
    totalYears: 0,
    currentYear: null,
    currentScenario: null,
    scenarios: [],
    cohortSize: null,
    seed: null,
    population: null,
    elapsedMs: null,
    outputs: [],
    diagnostics: [],
    truncated: false,
    seenYears: new Set(),
  };
}

/** True once nothing more will arrive. */
export function isFinished(state: RunState): boolean {
  return state === 'completed' || state === 'cancelled' || state === 'failed';
}

/** The fraction done, in [0, 1], or null while the denominator is unknown. */
export function fractionDone(progress: ProgressState): number | null {
  if (progress.totalYears <= 0) return null;
  return Math.min(1, progress.yearsCompleted / progress.totalYears);
}

/**
 * Applies one event. Returns a new state; never mutates the one given, so a caller can compare.
 */
export function applyEvent(previous: ProgressState, event: RunEvent): ProgressState {
  const next: ProgressState = { ...previous, seenYears: previous.seenYears };

  switch (event.type) {
    case 'run_started':
      next.state = 'running';
      next.totalYears = event.total_years;
      next.scenarios = event.scenarios;
      next.cohortSize = event.cohort_size;
      next.seed = event.seed;
      return next;

    case 'scenario_started':
      next.currentScenario = event.scenario;
      return next;

    case 'year_completed': {
      // Keyed by what makes a year unique within a run, so the replay the server sends on connect
      // — and on every reconnect — cannot inflate the count.
      const key = `${event.scenario}#${event.run}#${event.year}`;
      if (!next.seenYears.has(key)) {
        next.seenYears = new Set(next.seenYears).add(key);
        next.yearsCompleted = next.seenYears.size;
      }
      next.currentYear = event.year;
      next.currentScenario = event.scenario;
      next.population = event.population;
      return next;
    }

    case 'scenario_completed':
      return next;

    case 'run_completed':
      next.elapsedMs = event.elapsed_ms;
      next.outputs = event.outputs;
      // Deliberately not setting `state` here: the server sends a `state` event of its own, and
      // taking the state from one place keeps the two from disagreeing on a cancelled run.
      return next;

    case 'diagnostic': {
      const diagnostic: Diagnostic = {
        severity: event.severity,
        code: event.code,
        message: event.message,
        location: event.location,
      };
      // A replay would otherwise show every warning twice.
      const already = next.diagnostics.some(
        (d) =>
          d.code === diagnostic.code &&
          d.message === diagnostic.message &&
          d.severity === diagnostic.severity,
      );
      next.diagnostics = already ? next.diagnostics : [...next.diagnostics, diagnostic];
      return next;
    }

    case 'cancel_requested':
      next.cancelRequested = true;
      return next;

    case 'state':
      next.state = event.state;
      if (event.truncated) next.truncated = true;
      // Once the run has finished, "cancellation pending" is no longer true whichever way it went.
      if (isFinished(event.state)) next.cancelRequested = false;
      return next;

    default:
      return next;
  }
}

/** What the progress line should say. One place, so the wording is consistent. */
export function progressLabel(progress: ProgressState): string {
  if (progress.state === 'starting') return 'starting…';
  if (progress.cancelRequested && !isFinished(progress.state)) {
    return `stopping at the end of ${progress.currentYear ?? 'this year'}…`;
  }
  switch (progress.state) {
    case 'running': {
      const where = progress.currentScenario
        ? `${progress.currentScenario} ${progress.currentYear ?? ''}`.trim()
        : 'running';
      return progress.totalYears > 0
        ? `${where} — ${progress.yearsCompleted} of ${progress.totalYears} years`
        : where;
    }
    case 'completed':
      return progress.elapsedMs !== null
        ? `finished in ${(progress.elapsedMs / 1000).toFixed(1)} s`
        : 'finished';
    case 'cancelled':
      return `cancelled after ${progress.yearsCompleted} of ${progress.totalYears} years`;
    case 'failed':
      return 'failed';
    default:
      return String(progress.state);
  }
}
