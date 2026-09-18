import { describe, expect, it } from 'vitest';
import { groupDiagnostics, nearestKnown, worstSeverity } from './diagnostics.js';
import type { Diagnostic } from './types.js';

const at = (pointer: string, severity: 'error' | 'warning' = 'error'): Diagnostic => ({
  severity,
  code: 'config_bad_value',
  message: `about ${pointer}`,
  location: { pointer },
});

describe('placing diagnostics on fields', () => {
  const known = ['/running/seed', '/running/diseases', '/output/folder'];

  it('puts a diagnostic on the field it names', () => {
    const { byPointer, unplaced } = groupDiagnostics([at('/running/seed')], known);
    expect(byPointer.get('/running/seed')).toHaveLength(1);
    expect(unplaced).toHaveLength(0);
  });

  it('collects several on one field', () => {
    const { byPointer } = groupDiagnostics([at('/running/seed'), at('/running/seed')], known);
    expect(byPointer.get('/running/seed')).toHaveLength(2);
  });

  it('attaches a deeper pointer to the nearest field that is shown', () => {
    // A message nobody sees is worse than a message in roughly the right place.
    const { byPointer, unplaced } = groupDiagnostics([at('/running/diseases/3')], known);
    expect(byPointer.get('/running/diseases')).toHaveLength(1);
    expect(unplaced).toHaveLength(0);
  });

  it('puts a diagnostic with no pointer at the top', () => {
    const { unplaced } = groupDiagnostics(
      [{ severity: 'error', code: 'x', message: 'no field', location: {} }],
      known,
    );
    expect(unplaced).toHaveLength(1);
  });

  it('puts a pointer with no field anywhere above it at the top', () => {
    const { byPointer, unplaced } = groupDiagnostics([at('/elsewhere/entirely')], known);
    expect(byPointer.size).toBe(0);
    expect(unplaced).toHaveLength(1);
  });

  it('finds the longest known prefix', () => {
    const shown = new Set(['/a', '/a/b']);
    expect(nearestKnown('/a/b/c/d', shown)).toBe('/a/b');
    expect(nearestKnown('/a/z', shown)).toBe('/a');
    expect(nearestKnown('/z', shown)).toBeNull();
  });

  it('reports the worst severity on a field', () => {
    expect(worstSeverity([])).toBeNull();
    expect(worstSeverity([at('/x', 'warning')])).toBe('warning');
    expect(worstSeverity([at('/x', 'warning'), at('/x', 'error')])).toBe('error');
  });
});
