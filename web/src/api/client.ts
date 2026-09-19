// The HTTP client. One place that knows about fetch, so a screen never does.
import type {
  ExampleDetail,
  ExampleListing,
  ResultSummary,
  RunInfo,
  RunListing,
  SchemaDocument,
  StartRunRequest,
  ValidationResult,
  VersionInfo,
  Diagnostic,
} from './types.js';

/**
 * A request the server refused, with whatever it said about why.
 *
 * `diagnostics` matters: the server returns the same diagnostic array whether they arrived as a
 * validation result or as the reason a run would not start, so a screen renders them the same way
 * either way.
 */
export class ApiFailure extends Error {
  readonly status: number;
  readonly code: string;
  readonly diagnostics: Diagnostic[];

  constructor(status: number, code: string, message: string, diagnostics: Diagnostic[] = []) {
    super(message);
    this.name = 'ApiFailure';
    this.status = status;
    this.code = code;
    this.diagnostics = diagnostics;
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(path, {
    ...init,
    headers: {
      Accept: 'application/json',
      ...(init?.body ? { 'Content-Type': 'application/json' } : {}),
      ...init?.headers,
    },
  });

  const text = await response.text();
  let body: unknown = null;
  if (text.length > 0) {
    try {
      body = JSON.parse(text);
    } catch {
      // A body that is not JSON from an endpoint that promises JSON is itself the problem, and
      // saying so beats a parse error with no context.
      throw new ApiFailure(
        response.status,
        'bad_response',
        `${path} returned ${response.status} with a body that is not JSON`,
      );
    }
  }

  if (!response.ok) {
    const error = (body as { error?: { code?: string; message?: string; diagnostics?: Diagnostic[] } })
      ?.error;
    throw new ApiFailure(
      response.status,
      error?.code ?? 'unknown',
      error?.message ?? `${path} returned ${response.status}`,
      error?.diagnostics ?? [],
    );
  }

  return body as T;
}

export const api = {
  version: () => request<VersionInfo>('/api/version'),

  examples: () => request<{ examples: ExampleListing[] }>('/api/examples'),

  example: (id: string) => request<ExampleDetail>(`/api/examples/${encodeURIComponent(id)}`),

  schema: () => request<SchemaDocument>('/api/schema'),

  /**
   * Validate a document.
   *
   * `requireFilesExist: false` is what an editor uses while a document is half-written: it
   * validates the shape without insisting the files are there yet.
   */
  validate: (document: unknown, base?: string, requireFilesExist = true) =>
    request<ValidationResult>('/api/configs/validate', {
      method: 'POST',
      body: JSON.stringify({
        document,
        ...(base ? { base } : {}),
        require_files_exist: requireFilesExist,
      }),
    }),

  runs: () => request<RunListing>('/api/runs'),

  run: (id: string) => request<RunInfo>(`/api/runs/${encodeURIComponent(id)}`),

  startRun: (body: StartRunRequest) =>
    request<RunInfo>('/api/runs', { method: 'POST', body: JSON.stringify(body) }),

  /**
   * Ask a run to stop.
   *
   * Accepted rather than acknowledged: the engine observes cancellation at the end of the year it
   * is in, so the state change arrives on the event stream and not here (docs/api.md).
   */
  cancelRun: (id: string) =>
    request<{ id: string; state: string; note: string }>(
      `/api/runs/${encodeURIComponent(id)}/cancel`,
      { method: 'POST', body: '{}' },
    ),

  /**
   * The reduced results of one output family.
   *
   * `family` defaults to `result`, the whole-population file. An income-stratified file is asked
   * for by its category — `LowIncome`, `UpperMiddleIncome` — and the response lists every family
   * the run wrote whichever one was asked for.
   */
  summary: (id: string,
            options: { sex?: string; variables?: string[]; family?: string } = {}) => {
    const query = new URLSearchParams();
    if (options.sex) query.set('sex', options.sex);
    if (options.family) query.set('family', options.family);
    if (options.variables?.length) query.set('variable', options.variables.join(','));
    const suffix = query.toString() ? `?${query}` : '';
    return request<ResultSummary>(`/api/runs/${encodeURIComponent(id)}/summary${suffix}`);
  },

  resultUrl: (id: string, name: string) =>
    `/api/runs/${encodeURIComponent(id)}/results/${encodeURIComponent(name)}`,

  eventsUrl: (id: string) => `/api/runs/${encodeURIComponent(id)}/events`,
};
