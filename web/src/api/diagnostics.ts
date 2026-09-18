// Putting a diagnostic on the field it names.
//
// The whole reason `location.pointer` exists (docs/server-api.md): an editor that showed every
// message in a list at the top would make the reader find the field themselves, which is what the
// engine's located diagnostics were built to avoid.
import type { Diagnostic } from './types.js';

export interface DiagnosticsByPointer {
  /** Diagnostics that name a field, keyed by its pointer. */
  byPointer: Map<string, Diagnostic[]>;
  /** Diagnostics with no pointer, which belong at the top of the form. */
  unplaced: Diagnostic[];
}

/**
 * Groups diagnostics by the field they name.
 *
 * A diagnostic whose pointer names a field the form does not show — inside an array element, say —
 * is attached to the nearest ancestor that *is* shown, rather than dropped. A message nobody sees
 * is worse than a message in roughly the right place, and `known` is what says which is which.
 */
export function groupDiagnostics(
  diagnostics: Diagnostic[],
  known: Iterable<string>,
): DiagnosticsByPointer {
  const shown = new Set(known);
  const byPointer = new Map<string, Diagnostic[]>();
  const unplaced: Diagnostic[] = [];

  for (const diagnostic of diagnostics) {
    const pointer = diagnostic.location?.pointer;
    if (!pointer) {
      unplaced.push(diagnostic);
      continue;
    }

    const target = nearestKnown(pointer, shown);
    if (target === null) {
      unplaced.push(diagnostic);
      continue;
    }
    const existing = byPointer.get(target);
    if (existing) {
      existing.push(diagnostic);
    } else {
      byPointer.set(target, [diagnostic]);
    }
  }

  return { byPointer, unplaced };
}

/** The longest prefix of `pointer` that is in `known`, or null. */
export function nearestKnown(pointer: string, known: Set<string>): string | null {
  let candidate = pointer;
  for (;;) {
    if (known.has(candidate)) return candidate;
    const cut = candidate.lastIndexOf('/');
    if (cut <= 0) return null;
    candidate = candidate.slice(0, cut);
  }
}

export function worstSeverity(diagnostics: Diagnostic[]): 'error' | 'warning' | null {
  if (diagnostics.some((d) => d.severity === 'error')) return 'error';
  if (diagnostics.length > 0) return 'warning';
  return null;
}
