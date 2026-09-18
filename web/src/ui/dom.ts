// The few DOM helpers four screens need. Not a framework; a way of not writing
// `document.createElement` two hundred times (ADR 0043).
import type { Diagnostic } from '../api/types.js';

type Attributes = Record<string, string | number | boolean | undefined>;
type Child = Node | string | null | undefined | false;

export function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  attributes: Attributes = {},
  ...children: Child[]
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attributes)) {
    if (value === undefined || value === false) continue;
    if (key === 'class') node.className = String(value);
    else if (key === 'text') node.textContent = String(value);
    else node.setAttribute(key, value === true ? '' : String(value));
  }
  for (const child of children) {
    if (child === null || child === undefined || child === false) continue;
    node.append(typeof child === 'string' ? document.createTextNode(child) : child);
  }
  return node;
}

export function clear(node: Element): void {
  node.replaceChildren();
}

/** One diagnostic, rendered the same way wherever it came from. */
export function diagnosticNode(diagnostic: Diagnostic): HTMLElement {
  const where = diagnostic.location?.pointer;
  return el(
    'div',
    { class: `diagnostic ${diagnostic.severity}` },
    el('code', { text: diagnostic.code }),
    ' ',
    diagnostic.message,
    where ? el('code', { text: ` (${where})` }) : null,
  );
}

export function diagnosticsNode(diagnostics: Diagnostic[]): HTMLElement | null {
  if (diagnostics.length === 0) return null;
  return el('div', { class: 'diagnostics' }, ...diagnostics.map(diagnosticNode));
}

/** A short, readable time. The manifests carry ISO-8601 UTC. */
export function formatTime(iso: string | null | undefined): string {
  if (!iso) return '—';
  const when = new Date(iso);
  return Number.isNaN(when.getTime()) ? iso : when.toLocaleString();
}

export function formatDuration(ms: number | null | undefined): string {
  if (ms === null || ms === undefined) return '—';
  if (ms < 1000) return `${Math.round(ms)} ms`;
  if (ms < 60_000) return `${(ms / 1000).toFixed(1)} s`;
  const minutes = Math.floor(ms / 60_000);
  return `${minutes} min ${Math.round((ms % 60_000) / 1000)} s`;
}

/** Shows what went wrong, in the screen's own area, rather than in a toast that scrolls away. */
export function failureNode(error: unknown): HTMLElement {
  const failure = error as { message?: string; diagnostics?: Diagnostic[] };
  const node = el('div', { class: 'diagnostics' },
    el('div', { class: 'diagnostic error' }, failure?.message ?? String(error)));
  for (const diagnostic of failure?.diagnostics ?? []) {
    node.append(diagnosticNode(diagnostic));
  }
  return node;
}
