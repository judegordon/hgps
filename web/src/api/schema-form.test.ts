import { describe, expect, it } from 'vitest';
import { escapePointer, setAt, valueAt, walkSchema } from './schema-form.js';
import type { JsonSchema } from './types.js';

const schema: JsonSchema = {
  type: 'object',
  properties: {
    version: { const: 2, description: 'the config format version' },
    running: {
      type: 'object',
      required: ['seed', 'start_time'],
      properties: {
        seed: { type: 'integer', minimum: 0, description: 'the master seed' },
        start_time: { type: 'integer' },
        diseases: { type: 'array', items: { type: 'string' } },
        interventions: {
          type: 'object',
          properties: { active_type_id: { type: ['string', 'null'] } },
        },
      },
    },
    baseline_compat: { type: 'array', items: { type: 'string' }, default: [] },
    modelling: {
      type: 'object',
      // A free-form map: there is nothing to generate a control from.
      properties: { risk_factor_models: { type: 'object' } },
    },
  },
  required: ['version', 'running'],
};

describe('walking the schema into fields', () => {
  const fields = walkSchema(schema);
  const at = (pointer: string) => fields.find((f) => f.pointer === pointer);

  it('reaches a nested field and gives it a JSON pointer', () => {
    // The pointer is the whole point: it is exactly what a diagnostic's location carries.
    expect(at('/running/seed')).toBeDefined();
    expect(at('/running/seed')?.kind).toBe('integer');
    expect(at('/running/interventions/active_type_id')?.kind).toBe('string');
  });

  it('does not make a field of the objects on the way', () => {
    expect(at('/running')).toBeUndefined();
  });

  it('carries required, defaults and bounds through', () => {
    expect(at('/running/seed')?.required).toBe(true);
    expect(at('/running/seed')?.minimum).toBe(0);
    expect(at('/running/diseases')?.required).toBe(false);
    expect(at('/baseline_compat')?.default).toEqual([]);
  });

  it('reads a nullable type as its non-null half', () => {
    // `["string", "null"]` is "a string, optionally absent" as far as a form is concerned.
    expect(at('/running/interventions/active_type_id')?.kind).toBe('string');
  });

  it('knows an array of strings from an array of something else', () => {
    expect(at('/running/diseases')?.kind).toBe('string-array');
  });

  it('falls back to raw JSON for an object with no declared properties', () => {
    // A free-form map, which this schema uses for the model file list. Generating a control is
    // impossible; pretending otherwise would lose the field.
    expect(at('/modelling/risk_factor_models')?.kind).toBe('json');
  });

  it('marks a const field as one', () => {
    expect(at('/version')?.kind).toBe('const');
    expect(at('/version')?.const).toBe(2);
  });

  it('keeps the schema order', () => {
    expect(fields.map((f) => f.pointer).slice(0, 3)).toEqual([
      '/version',
      '/running/seed',
      '/running/start_time',
    ]);
  });

  it('stops descending at the depth limit rather than for ever', () => {
    const shallow = walkSchema(schema, { maxDepth: 1 });
    expect(shallow.find((f) => f.pointer === '/running')?.kind).toBe('json');
    expect(shallow.find((f) => f.pointer === '/running/seed')).toBeUndefined();
  });
});

describe('reading and writing by pointer', () => {
  const document = { running: { seed: 7, diseases: ['asthma'] }, version: 2 };

  it('reads a value', () => {
    expect(valueAt(document, '/running/seed')).toBe(7);
    expect(valueAt(document, '/running/diseases')).toEqual(['asthma']);
    expect(valueAt(document, '/nope')).toBeUndefined();
    expect(valueAt(document, '/nope/deeper')).toBeUndefined();
  });

  it('writes a value without touching the original', () => {
    const changed = setAt(document, '/running/seed', 9);
    expect(valueAt(changed, '/running/seed')).toBe(9);
    expect(document.running.seed).toBe(7);
    // The parts it did not touch are still there.
    expect(valueAt(changed, '/running/diseases')).toEqual(['asthma']);
    expect(valueAt(changed, '/version')).toBe(2);
  });

  it('creates the objects on the way', () => {
    const changed = setAt({}, '/a/b/c', 1);
    expect(valueAt(changed, '/a/b/c')).toBe(1);
  });

  it('removes a key rather than setting it to undefined', () => {
    // A config with a key whose value is missing is not the same document as one without the key,
    // and the difference is what the loader reports as a default being applied.
    const changed = setAt(document, '/running/seed', undefined);
    expect(Object.hasOwn(valueAt(changed, '/running') as object, 'seed')).toBe(false);
  });

  it('escapes the two characters a pointer segment cannot hold', () => {
    expect(escapePointer('a/b')).toBe('a~1b');
    expect(escapePointer('a~b')).toBe('a~0b');
    const changed = setAt({}, `/${escapePointer('a/b')}`, 1);
    expect((changed as Record<string, unknown>)['a/b']).toBe(1);
    expect(valueAt(changed, '/a~1b')).toBe(1);
  });
});
