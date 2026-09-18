# 0034 — Every run writes a manifest beside its results

## Status

Accepted, 2026-09-18.

## Context

A result CSV from this program is a table of numbers with no statement of what produced it. The
result *JSON* carries some provenance — the model name and version, the intervention, the seed and
each run's derived seed, the config path and its SHA-256, the country and the horizon — and that is
already more than the baseline's, which records `seed().value_or(0)` (audit B-06).

What it does not carry is anything about the binary or the data. Two things follow from that, and
both have happened during this project:

- **A number cannot be tied to a commit.** Every performance figure and every equivalence result in
  [docs/performance.md](../performance.md) and [docs/equivalence.md](../equivalence.md) was produced
  by some build of this repository, and which one is recorded in prose, by hand, when somebody
  remembers.
- **A number cannot be tied to a data pack.** `data.checksum` is verified at load and then not
  written down anywhere. Two runs of the same config against two different releases of
  `healthgps-data` produce two different futures and two indistinguishable result files.

This run also introduces cancellation ([ADR 0033](0033-an-event-stream-the-simulation-cannot-see.md)),
which creates a third: a result file from a cancelled run is a valid file with fewer years in it, and
nothing in it says so.

## Decision

**Every run writes one manifest JSON beside its results**, named `<stem>_manifest.json`. It records:

| | |
|---|---|
| config | path, and the SHA-256 of its bytes |
| data | `data.source` as written, the verified archive checksum, and the directory finally read from |
| seed | the master seed **actually used**, and each trial run's derived seed |
| engine | version, full commit hash, `git describe`, whether the tree was dirty, platform, compiler, build type |
| timing | start and end, ISO-8601 UTC to the second, and elapsed milliseconds |
| run | country, horizon, trial runs, cohort size, thread count, scenarios in the order they ran, years completed, whether it was cancelled |
| results | the file names it was written beside |

**The result CSVs do not change.** Not a column, not a comment line. The CSV is what every downstream
script reads, it is what the equivalence harness compares, and it deliberately carries no timestamp
so that two runs of one config are byte-comparable (N-15). Provenance goes in a new file, and
`RunManifest.TheResultCsvIsUnchangedByTheManifestExisting` asserts it.

**The build stamp is baked in at configure time, not read from the environment at run time.**
`cmake/build_info.cmake` runs `git rev-parse`, `git describe` and `git status --porcelain` and writes
a generated header. A manifest that read `$GIT_COMMIT` would record whatever the shell believed,
which is provenance for the shell and not for the binary.

**A field that could not be determined says "unknown" rather than being empty**, and a field that has
no value says so rather than being absent. A source tarball has no git metadata, so `git_commit` is
the string `"unknown"`. A data source that is a plain directory has no single archive hash, so
`checksum` is `null` **and** a sibling `checksum_note` says why. An absent key reads as "nobody
looked"; a null with a reason distinguishes the two, and that distinction is the whole value of a
provenance record.

**`manifest_version` is the first key.** One today. A reader that finds a number it does not know
should stop rather than guess at the shape of the rest.

**It is on by default, and can be turned off.** `RunOptions::write_manifest` exists because a host
that writes its own provenance record — an HPC harness with a job database — should not be forced to
produce a second one. Nothing in this repository turns it off.

## Alternatives

- **Extend the result JSON instead.** Tempting, and it is already the provenance file. Rejected
  because that file's `results` array holds every year of every scenario — tens of megabytes on a
  real run — and provenance should be readable without parsing a simulation's worth of output. A
  manifest is two kilobytes and `jq` answers questions about it instantly.
- **Add columns to the CSV.** Multiplies a constant across every row, changes the file every
  downstream script reads, and breaks byte-comparability between runs the moment a timestamp is among
  them.
- **A sidecar `.sha256` or a checksum of the outputs.** Answers "were these files changed since they
  were written", which is a different and less useful question than "what produced them". Worth
  adding later; it is not a substitute.
- **Record the whole config document, not its hash.** Then the manifest is as large as the config and
  a reader has to diff two documents to answer "is this the same scenario". The hash answers it in
  one comparison, and the config path says where to look for the rest. (The equivalence harness
  already keys its stored references by the same kind of hash, for the same reason.)
- **Write it before the run rather than after.** Then a crashed run leaves a manifest claiming
  results that do not exist. Written after, a manifest's existence means the run finished or was
  cancelled, which is a stronger statement.
- **Embed the git commit at build time via a compile definition on every translation unit.** Would
  rebuild the whole library on every commit. The generated header goes through `configure_file`, so an
  unchanged stamp does not touch the file, and only two translation units include it.

## Consequences

Every run writes one more small file, and `RunSummary::outputs` and the `RunCompleted` event list it
alongside the rest. The CLI prints it with the others.

`git_dirty` will be `true` for most development runs, which is correct and is the point: a
measurement taken from a modified tree is not attributable to a commit, and the manifest should say
so rather than name a commit the binary was not built from.

The manifest is **not** byte-identical between two runs of the same config, because it records when
the run happened. `RunManifest.TwoRunsOfTheSameConfigAgreeOnEverythingButTheClock` pins that down:
drop the `timing` object and the two manifests must be equal. That is the same rule the CSV follows
by having no timestamp at all, applied to the one file whose job is to carry one.
