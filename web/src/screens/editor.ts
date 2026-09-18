// Screen 1: the configuration editor, generated from the published JSON schema, with the server's
// located diagnostics shown inline at the field they name.
//
// Two things make this worth building rather than a textarea. The form comes from
// `GET /api/schema`, so it cannot drift from what the loader accepts. And a diagnostic carries a
// JSON pointer, so its message lands on the field it is about rather than in a list at the top —
// which is what the engine's located diagnostics were built for.
import { api, ApiFailure } from '../api/client.js';
import { groupDiagnostics, worstSeverity } from '../api/diagnostics.js';
import { setAt, valueAt, walkSchema, type Field } from '../api/schema-form.js';
import type { Diagnostic, JsonSchema, ValidationResult } from '../api/types.js';
import { clear, diagnosticNode, el, failureNode } from '../ui/dom.js';

interface State {
  schema: JsonSchema | null;
  fields: Field[];
  document: unknown;
  exampleId: string | null;
  result: ValidationResult | null;
  requireFilesExist: boolean;
  busy: boolean;
  error: unknown;
}

export function editorScreen(
  root: HTMLElement,
  onRun: (exampleId: string) => void,
): { load: (exampleId?: string) => void } {
  const state: State = {
    schema: null,
    fields: [],
    document: {},
    exampleId: null,
    result: null,
    requireFilesExist: true,
    busy: false,
    error: null,
  };

  let examples: string[] = [];

  async function load(exampleId?: string): Promise<void> {
    state.busy = true;
    render();
    try {
      if (state.schema === null) {
        state.schema = (await api.schema()).schema;
        state.fields = walkSchema(state.schema);
      }
      if (examples.length === 0) {
        examples = (await api.examples()).examples.filter((e) => e.readable).map((e) => e.id);
      }
      const chosen = exampleId ?? state.exampleId ?? examples[0];
      if (chosen) {
        const detail = await api.example(chosen);
        state.exampleId = chosen;
        state.document = detail.document;
        state.result = null;
      }
      state.error = null;
    } catch (error) {
      state.error = error;
    } finally {
      state.busy = false;
      render();
    }
  }

  async function validate(): Promise<void> {
    state.busy = true;
    render();
    try {
      state.result = await api.validate(
        state.document,
        state.exampleId ?? undefined,
        state.requireFilesExist,
      );
      state.error = null;
    } catch (error) {
      // A refusal carries the same diagnostic array a validation result does, so it is shown the
      // same way rather than as a separate kind of thing.
      state.error = error;
      if (error instanceof ApiFailure && error.diagnostics.length > 0) {
        state.result = {
          valid: false,
          error_count: error.diagnostics.filter((d) => d.severity === 'error').length,
          warning_count: error.diagnostics.filter((d) => d.severity === 'warning').length,
          diagnostics: error.diagnostics,
          summary: null,
        };
      }
    } finally {
      state.busy = false;
      render();
    }
  }

  function update(pointer: string, value: unknown): void {
    state.document = setAt(state.document, pointer, value);
    // Not re-validating on every keystroke: validation reads files, and a form that flickered
    // between valid and invalid as somebody typed a number would be unreadable.
    render();
  }

  function fieldControl(field: Field, diagnostics: Diagnostic[]): HTMLElement {
    const current = valueAt(state.document, field.pointer);
    const worst = worstSeverity(diagnostics);

    const wrapper = el('div', { class: 'field', 'data-worst': worst ?? undefined });
    const id = `f${field.pointer.replace(/\W/g, '_')}`;
    wrapper.append(
      el('label', { for: id },
        field.name,
        field.required ? el('span', { class: 'required', text: '*' }) : null),
    );

    let control: HTMLElement;
    switch (field.kind) {
      case 'boolean': {
        const input = el('input', { type: 'checkbox', id });
        input.checked = current === true;
        input.addEventListener('change', () => update(field.pointer, input.checked));
        control = input;
        break;
      }
      case 'enum': {
        const select = el('select', { id });
        for (const option of field.enumValues ?? []) {
          const node = el('option', { value: String(option), text: String(option) });
          if (current === option) node.selected = true;
          select.append(node);
        }
        select.addEventListener('change', () => update(field.pointer, select.value));
        control = select;
        break;
      }
      case 'integer':
      case 'number': {
        const input = el('input', {
          type: 'number', id,
          step: field.kind === 'integer' ? '1' : 'any',
          min: field.minimum, max: field.maximum,
        });
        input.value = current === undefined || current === null ? '' : String(current);
        input.addEventListener('change', () => {
          // An empty box removes the key rather than writing NaN, so "not set" stays a state the
          // loader can report a default for.
          update(field.pointer, input.value === '' ? undefined : Number(input.value));
        });
        control = input;
        break;
      }
      case 'string-array':
      case 'number-array': {
        const input = el('input', { type: 'text', id });
        input.value = Array.isArray(current) ? current.join(', ') : '';
        input.addEventListener('change', () => {
          const parts = input.value.split(',').map((s) => s.trim()).filter((s) => s !== '');
          update(
            field.pointer,
            field.kind === 'number-array' ? parts.map(Number) : parts,
          );
        });
        control = input;
        break;
      }
      case 'const': {
        const input = el('input', { type: 'text', id, readonly: true });
        input.value = String(current ?? field.const ?? '');
        control = input;
        break;
      }
      case 'json': {
        const area = el('textarea', { id, spellcheck: 'false' });
        area.value = current === undefined ? '' : JSON.stringify(current, null, 2);
        area.addEventListener('change', () => {
          if (area.value.trim() === '') {
            update(field.pointer, undefined);
            return;
          }
          try {
            update(field.pointer, JSON.parse(area.value));
            area.setCustomValidity('');
          } catch (error) {
            // Said here rather than swallowed: a textarea that silently discarded what was typed
            // is the worst thing on this screen.
            area.setCustomValidity(`not valid JSON: ${(error as Error).message}`);
            area.reportValidity();
          }
        });
        control = area;
        break;
      }
      default: {
        const input = el('input', { type: 'text', id });
        input.value = current === undefined || current === null ? '' : String(current);
        input.addEventListener('change', () =>
          update(field.pointer, input.value === '' ? undefined : input.value));
        control = input;
      }
    }

    wrapper.append(control);
    if (field.description) {
      wrapper.append(el('p', { class: 'hint', text: field.description }));
    }
    for (const diagnostic of diagnostics) wrapper.append(diagnosticNode(diagnostic));
    return wrapper;
  }

  function render(): void {
    clear(root);

    root.append(el('h2', { text: 'Configuration' }));
    root.append(
      el('p', { class: 'lede' },
        'Generated from the schema the engine publishes, so the form cannot drift from what the ',
        'loader accepts. Validation reports every problem at once, at the field it is about.'),
    );

    const chooser = el('select', { id: 'example' });
    for (const id of examples) {
      const option = el('option', { value: id, text: id });
      if (id === state.exampleId) option.selected = true;
      chooser.append(option);
    }
    chooser.addEventListener('change', () => void load(chooser.value));

    const requireFiles = el('input', { type: 'checkbox', id: 'require-files' });
    requireFiles.checked = state.requireFilesExist;
    requireFiles.addEventListener('change', () => {
      state.requireFilesExist = requireFiles.checked;
    });

    const validateButton = el('button', { class: 'action', type: 'button', text: 'Validate' });
    validateButton.disabled = state.busy;
    validateButton.addEventListener('click', () => void validate());

    const runButton = el('button', { class: 'action quiet', type: 'button', text: 'Run this' });
    runButton.disabled = state.busy || state.exampleId === null;
    runButton.addEventListener('click', () => {
      if (state.exampleId) onRun(state.exampleId);
    });

    root.append(
      el('div', { class: 'row' },
        el('label', { for: 'example', text: 'Example' }), chooser,
        validateButton, runButton,
        el('label', { for: 'require-files' }, requireFiles, ' the files must already exist')),
    );

    if (state.error && !(state.error instanceof ApiFailure && state.result !== null)) {
      root.append(failureNode(state.error));
    }

    if (state.result) {
      const { valid, error_count, warning_count } = state.result;
      root.append(
        el('div', { class: `diagnostic ${valid ? 'warning' : 'error'}` },
          valid
            ? `valid — ${warning_count} warning(s)`
            : `${error_count} error(s), ${warning_count} warning(s)`),
      );
    }

    const known = state.fields.map((f) => f.pointer);
    const { byPointer, unplaced } = groupDiagnostics(state.result?.diagnostics ?? [], known);

    if (unplaced.length > 0) {
      root.append(
        el('div', { class: 'diagnostics' }, ...unplaced.map(diagnosticNode)),
      );
    }

    // Grouped by the top-level section the field belongs to, which is how the schema is
    // organised and how somebody reading a config thinks about it.
    const sections = new Map<string, Field[]>();
    for (const field of state.fields) {
      const section = field.path[0] ?? field.name;
      const list = sections.get(section);
      if (list) list.push(field);
      else sections.set(section, [field]);
    }

    for (const [section, fields] of sections) {
      const all = fields.flatMap((f) => byPointer.get(f.pointer) ?? []);
      const worst = worstSeverity(all);
      const details = el('details', { class: 'group', 'data-worst': worst ?? undefined });
      if (worst !== null) details.open = true;
      details.append(
        el('summary', {}, section,
          el('span', { class: 'count', text: `${fields.length} field(s)` })),
      );
      details.append(
        el('div', { class: 'group-body' },
          ...fields.map((f) => fieldControl(f, byPointer.get(f.pointer) ?? []))),
      );
      root.append(details);
    }

    if (state.fields.length === 0 && !state.busy) {
      root.append(el('p', { class: 'empty', text: 'No schema to show.' }));
    }
  }

  render();
  return { load: (id) => void load(id) };
}
