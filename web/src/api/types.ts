// The shapes docs/server-api.md publishes, written down.
//
// This file is the contract between two languages. A field renamed on the server becomes a compile
// error here, which is the cheapest test this project can have of that contract
// (docs/decisions/0043-a-plain-typescript-frontend.md).

/** How bad a diagnostic is. Two levels, as the engine has. */
export type Severity = 'warning' | 'error';

/**
 * Where a diagnostic was found. Every field is optional, because not every problem has one.
 *
 * `pointer` is the one that matters to an editor: a JSON Pointer (RFC 6901) into the document that
 * was submitted, so the message goes on the field it names rather than at the top of a form.
 */
export interface Location {
  file?: string;
  pointer?: string;
  line?: number;
  column?: number;
}

export interface Diagnostic {
  severity: Severity;
  code: string;
  message: string;
  location: Location;
}

/** The one error shape, so a client has one thing to handle. */
export interface ApiError {
  error: {
    code: string;
    message: string;
    diagnostics?: Diagnostic[];
  };
}

export interface CompatFlag {
  name: string;
  description: string;
}

export interface VersionInfo {
  engine_version: string;
  git_commit: string;
  git_describe: string;
  git_dirty: boolean;
  platform: string;
  compiler: string;
  build_type: string;
  api_version: number;
  baseline_compat_flags: CompatFlag[];
}

export interface ExampleListing {
  id: string;
  path: string;
  root: string;
  readable: boolean;
}

/** What the engine understood about a configuration — not what a reader would guess from it. */
export interface ConfigSummary {
  sha256: string;
  seed: number;
  start_time: number;
  stop_time: number;
  trial_runs: number;
  diseases: string[];
  active_intervention: string | null;
  data_source: string;
  data_checksum: string | null;
  output_folder: string;
  output_file_name: string;
  baseline_compat: string[];
}

export interface ExampleDetail {
  id: string;
  path: string;
  sha256: string;
  document: unknown;
  summary: ConfigSummary;
  diagnostics: Diagnostic[];
}

export interface ValidationResult {
  valid: boolean;
  error_count: number;
  warning_count: number;
  diagnostics: Diagnostic[];
  summary: ConfigSummary | null;
}

export interface RunDescription {
  country: string;
  disease_count: number;
  risk_factor_count: number;
  cohort_size: number;
  start_time: number;
  stop_time: number;
  trial_runs: number;
  seed: number;
  run_seeds: number[];
  scenarios: string[];
}

export type RunState = 'starting' | 'running' | 'completed' | 'cancelled' | 'failed';

export interface RunInfo {
  id: string;
  example: string;
  state: RunState;
  started_utc: string;
  finished_utc: string | null;
  years_completed: number;
  total_years: number;
  elapsed_ms: number;
  cancelled: boolean;
  output_folder: string;
  diagnostics: Diagnostic[];
  description: RunDescription | null;
  error: string | null;
  /** Present on `GET /api/runs/{id}`, absent from a listing. */
  manifest?: Record<string, unknown> | null;
  results?: string[];
}

export interface RunListing {
  active: string | null;
  runs: RunInfo[];
}

export interface StartRunRequest {
  example: string;
  threads?: number;
  baseline_compat?: string[];
  write_manifest?: boolean;
}

/** One line of a chart: parallel to the summary's `years`, with `null` for a year with no value. */
export interface SummarySeries {
  scenario: string;
  variable: string;
  sex: string;
  values: Array<number | null>;
}

/** One output family a run wrote: the whole-population CSV, or one income category's. */
export interface SummaryFamily {
  family: string;
  file: string;
}

export interface ResultSummary {
  id: string;
  reduction: string;
  sex: string;
  /** Which family this summary reduced: `result`, or an income category such as `LowIncome`. */
  family: string;
  /** The file it reduced, by the name the run wrote it under. */
  file: string;
  /** Every family the run wrote, so a client can offer the selector without a second request. */
  families: SummaryFamily[];
  scenarios: string[];
  years: number[];
  variables: string[];
  series: SummarySeries[];
}

export interface SchemaDocument {
  schema: JsonSchema;
  version: number;
  source: string;
}

/**
 * As much of JSON Schema as the config schema uses. Deliberately not a complete model of the
 * specification: a form generator that claimed to handle all of it and did not would be worse than
 * one whose type says what it covers.
 */
export interface JsonSchema {
  type?: JsonSchemaType | JsonSchemaType[];
  title?: string;
  description?: string;
  default?: unknown;
  const?: unknown;
  enum?: unknown[];
  properties?: Record<string, JsonSchema>;
  required?: string[];
  additionalProperties?: boolean | JsonSchema;
  items?: JsonSchema;
  minimum?: number;
  maximum?: number;
  minItems?: number;
  uniqueItems?: boolean;
  pattern?: string;
  oneOf?: JsonSchema[];
  anyOf?: JsonSchema[];
  $comment?: string;
}

export type JsonSchemaType =
  | 'object'
  | 'array'
  | 'string'
  | 'number'
  | 'integer'
  | 'boolean'
  | 'null';

/** One server-sent event, as the engine's event stream maps onto the wire. */
export type RunEvent =
  | { type: 'run_started'; engine_version: string; seed: number; trial_runs: number;
      start_time: number; stop_time: number; cohort_size: number; scenarios: string[];
      total_years: number }
  | { type: 'scenario_started'; scenario: string; kind: string; run: number }
  | { type: 'year_completed'; scenario: string; kind: string; run: number; year: number;
      elapsed_ms: number; population: number }
  | { type: 'scenario_completed'; scenario: string; kind: string; run: number;
      elapsed_ms: number; years_completed: number }
  | { type: 'run_completed'; elapsed_ms: number; cancelled: boolean; outputs: string[] }
  | { type: 'diagnostic'; severity: Severity; code: string; message: string; location: Location }
  | { type: 'cancel_requested'; note: string }
  | { type: 'state'; state: RunState; truncated?: boolean };
