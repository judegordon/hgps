# 0020 — One owner per output file; rows in a defined order

## Status

Accepted, 2026-09-17.

## Context

The baseline's only *observable* nondeterminism is in its output file. With an intervention active,
three same-seed runs produced three different files; sorting the rows made all three byte-identical,
and the row counts matched exactly (audit B-01, experiments D/E). The cause is structural:
`AnalysisModule` publishes results with `publish_async` onto `tbb::concurrent_queue`s, which the
Console's `EventMonitor` drains on `tbb::task_group` workers, and the writer appends rows in arrival
order — which follows thread scheduling.

The consequence is that a result file cannot be checksummed, diffed or content-addressed across
runs, which rules out the cheapest and most convincing regression test for a model whose entire
purpose is reproducible comparison. The baseline also embeds a wall-clock timestamp in the output
*file name* (N-15) and in the JSON *content* (N-16), so even the numbers cannot be compared by path.

## Decision

- **Each output file has exactly one owner object**, created by `app`, never shared between threads
  and never handed to a queue.
- The runner hands the writer **whole scenario result sets, in scenario order** — baseline, then
  intervention — rather than individual rows as they are produced.
- The writer emits rows sorted by the total order **`(source, run, time, gender, index_id)`**, which
  is the same key the baseline's rows carry.
- There is **no event bus, no concurrent queue and no async publication** in the output path. The
  event/message abstraction is kept only for progress and diagnostic reporting, which does not enter
  a result file.
- `output.file_name` is used exactly as configured (ADR 0010), and the wall-clock timestamp lives
  **inside** the JSON metadata, not in any path. The CSV contains no timestamp, so two runs of the
  same config are byte-comparable.

## Alternatives

- **Sort before writing, keep the concurrency.** Fixes the symptom. Every future row-producing site
  must then remember to route through the sort, and nothing enforces it.
- **Keep the queue but make it deterministic**, e.g. sequence-numbered rows reassembled in order. A
  reorder buffer, to get back to what a direct call already gives.
- **Write incrementally as years complete**, for progress visibility on long runs. Attractive; it
  conflicts with emitting a totally ordered file in one pass. Progress is reported on stderr instead,
  and `docs/backlog.md` records streaming as a possible future with a sort-on-close.

## Consequences

Result rows for a whole scenario are held in memory until the scenario finishes. At the reference
configuration that is a few thousand rows, which is nothing; `docs/performance.md` reports the peak
memory. Byte-comparable CSV output is what makes the reproducibility test — the one test neither
existing codebase has — a two-line assertion.
