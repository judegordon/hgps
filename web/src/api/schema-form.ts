// Walking the published config schema into a list of fields a form can render.
//
// Pure, and separate from the DOM, because this is where a mistake is silent: a field the walk
// misses is a field the editor never shows, and the configuration runs without it with no
// complaint from anybody (docs/decisions/0043-a-plain-typescript-frontend.md).
import type { JsonSchema, JsonSchemaType } from './types.js';

/** What kind of control a field wants. */
export type FieldKind =
  | 'string'
  | 'number'
  | 'integer'
  | 'boolean'
  | 'enum'
  | 'string-array'
  | 'number-array'
  | 'const'
  /** Something the walk does not model — an array of objects, say. Shown as raw JSON. */
  | 'json';

export interface Field {
  /** RFC 6901 JSON Pointer, which is exactly what a diagnostic's `location.pointer` carries. */
  pointer: string;
  /** The last segment, for a label. */
  name: string;
  /** Every ancestor's name, for grouping: ["running"], ["output", "individual_id_tracking"]. */
  path: string[];
  kind: FieldKind;
  required: boolean;
  title?: string;
  description?: string;
  default?: unknown;
  enumValues?: unknown[];
  minimum?: number;
  maximum?: number;
  const?: unknown;
}

/** Escapes a property name for a JSON Pointer: `~` becomes `~0` and `/` becomes `~1`. */
export function escapePointer(name: string): string {
  return name.replace(/~/g, '~0').replace(/\//g, '~1');
}

function firstType(schema: JsonSchema): JsonSchemaType | undefined {
  if (Array.isArray(schema.type)) {
    // `["string", "null"]` is "a string, optionally absent" as far as a form is concerned.
    return schema.type.find((t) => t !== 'null');
  }
  return schema.type;
}

function kindOf(schema: JsonSchema): FieldKind {
  if (schema.const !== undefined) return 'const';
  if (schema.enum !== undefined) return 'enum';

  switch (firstType(schema)) {
    case 'string':
      return 'string';
    case 'integer':
      return 'integer';
    case 'number':
      return 'number';
    case 'boolean':
      return 'boolean';
    case 'array': {
      const item = schema.items;
      const itemType = item ? firstType(item) : undefined;
      if (item?.enum !== undefined || itemType === 'string') return 'string-array';
      if (itemType === 'number' || itemType === 'integer') return 'number-array';
      // An array of objects. Editing one as a table is a screen of its own; raw JSON is honest.
      return 'json';
    }
    default:
      return 'json';
  }
}

export interface WalkOptions {
  /** How deep to descend into nested objects. Beyond it, the object is one raw-JSON field. */
  maxDepth?: number;
}

/**
 * Flattens a schema into fields, depth first, in the order the schema declares them.
 *
 * An object with `properties` becomes its properties; an object without them — a free-form map,
 * which the config schema uses for a model file list — becomes one raw-JSON field, because there
 * is nothing to generate a control from.
 */
export function walkSchema(schema: JsonSchema, options: WalkOptions = {}): Field[] {
  const maxDepth = options.maxDepth ?? 6;
  const fields: Field[] = [];

  const visit = (node: JsonSchema, path: string[], required: boolean, depth: number): void => {
    const pointer = `/${path.map(escapePointer).join('/')}`;
    const name = path[path.length - 1] ?? '';

    const isObject = firstType(node) === 'object' || node.properties !== undefined;
    if (isObject && node.properties && depth < maxDepth) {
      const requiredHere = new Set(node.required ?? []);
      for (const [key, child] of Object.entries(node.properties)) {
        visit(child, [...path, key], requiredHere.has(key), depth + 1);
      }
      return;
    }

    const field: Field = {
      pointer,
      name,
      path: path.slice(0, -1),
      kind: isObject ? 'json' : kindOf(node),
      required,
    };
    if (node.title !== undefined) field.title = node.title;
    if (node.description !== undefined) field.description = node.description;
    if (node.default !== undefined) field.default = node.default;
    if (node.enum !== undefined) field.enumValues = node.enum;
    if (node.minimum !== undefined) field.minimum = node.minimum;
    if (node.maximum !== undefined) field.maximum = node.maximum;
    if (node.const !== undefined) field.const = node.const;
    fields.push(field);
  };

  const top = new Set(schema.required ?? []);
  for (const [key, child] of Object.entries(schema.properties ?? {})) {
    visit(child, [key], top.has(key), 1);
  }
  return fields;
}

/** Reads the value a pointer names out of a document, or undefined. */
export function valueAt(document: unknown, pointer: string): unknown {
  if (pointer === '' || pointer === '/') return document;
  let node: unknown = document;
  for (const raw of pointer.replace(/^\//, '').split('/')) {
    const key = raw.replace(/~1/g, '/').replace(/~0/g, '~');
    if (node === null || typeof node !== 'object') return undefined;
    node = (node as Record<string, unknown>)[key];
  }
  return node;
}

/**
 * Writes a value at a pointer, creating the objects on the way, and returns a new document.
 *
 * `undefined` removes the key rather than setting it to undefined, because a config with a key
 * whose value is missing is not the same document as one without the key.
 */
export function setAt<T>(document: T, pointer: string, value: unknown): T {
  const segments = pointer
    .replace(/^\//, '')
    .split('/')
    .map((raw) => raw.replace(/~1/g, '/').replace(/~0/g, '~'));

  const clone = (node: unknown): Record<string, unknown> =>
    node !== null && typeof node === 'object' && !Array.isArray(node)
      ? { ...(node as Record<string, unknown>) }
      : {};

  const root = clone(document);
  let node = root;
  for (let i = 0; i < segments.length - 1; i += 1) {
    const key = segments[i] as string;
    node[key] = clone(node[key]);
    node = node[key] as Record<string, unknown>;
  }
  const last = segments[segments.length - 1] as string;
  if (value === undefined) {
    delete node[last];
  } else {
    node[last] = value;
  }
  return root as T;
}
