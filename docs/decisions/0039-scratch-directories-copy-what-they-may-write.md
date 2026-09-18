# 0039 — A scratch directory copies what it may write and links only what it reads

## Status

Accepted, 2026-09-18.

## Context

The equivalence harness runs each implementation on a config it derives rather than on the example's
own: the seed, the output folder, the active intervention and sometimes `size_fraction` all differ
from what the example ships. That derived config has to live somewhere, and it cannot live in the
example directory — the upstream examples are read-only by rule
([docs/build-notes.md](../build-notes.md)) and this repository's converted ones are checked in.

So it lives in a scratch working directory. But a *model* file names its own CSVs by a path relative
to the config's directory — that is how the baseline resolves them, and it is right when the config
sits in the example folder, which upstream it does. A derived config in scratch therefore needs the
example's files beside it, and `link_example_files` symlinked all of them, `config.json` included,
because some are tens of megabytes and copying them per run is waste.

**A symlink is a two-way door.** Writing `<scratch>/config.json` in a directory staged that way does
not create a file: it follows the link and rewrites the example. That is not hypothetical. It
happened in the previous run: an ad-hoc measurement script named its derived config `config.json`
and silently rewrote two of the converted examples. Nothing failed — the mangled `data.source` still
ended in `.zip`, so the engine found the already-extracted pack in its content-addressed cache and
never looked at the path — so the runs kept working and the damage was found by `git status`.

The response at the time was a paragraph in the function's docstring saying *a caller must not name
its derived config after a file in the example directory*. That is a rule a reader has to know before
they need it, enforced by nothing, in a project whose own harness had already broken it once.

## Decision

**A staged scratch directory copies every file it might write to and links only what it reads.**

`stage_example_files(source_config, into)` copies the source config and symlinks everything else. The
config is copied because it is the file a caller derives *from*, and the obvious name for a derived
config is the name of the one it came from. Everything else — the FactorsMean tables, the model
JSONs, the policy-effect CSVs, the data files — is read and never written, and `HLM_India` alone
would be 42 MB a copy.

**And `write_derived_config(path, document)` refuses to write through a symlink at all.** Every
derived config in the harness goes through it. This is the part that makes the rule structural rather
than a second convention: a caller that invents a new name, or an example that ships a file this
staging decides to link, cannot reach an input through a staged directory. It raises, naming the link
and its target, rather than corrupting a file and succeeding.

That second guard is **load-bearing rather than belt-and-braces**, and `KevinHall_FINCH` is why: the
example ships *two* configs, `config.json` and `new_config.json`, and the harness derives from the
latter. So staging copies `new_config.json` and links the other 54 files — `config.json` among them.
Copying the source config alone would not have stopped the original accident on this example, because
the name the accident used belongs to the *other* config. Measured, on the real example:

```
copied : ['new_config.json']
linked : 54 files, e.g. ['Finch.DataFile.csv', 'Finch.FactorsMean.Female.Quintile1.csv', …]
config.json is still a link: True
write_derived_config(<stage>/config.json, …) -> RuntimeError: refusing to write … through a symlink
```

**And a test asserts it**, because the property is invisible when it holds.
`StagingAnExampleTest.WritingTheDerivedConfigDoesNotTouchTheSourceExample` stages a directory,
writes a derived config **named `config.json`** into it — exactly the accident that happened — and
compares the SHA-256 of the source example's `config.json` before and against after. Two more pin the
halves: that a large input is still a link rather than a copy, and that `write_derived_config` refuses
a symlinked path and leaves its target unchanged.

## Alternatives

- **Keep the docstring warning.** It was already there, it was already specific, and the accident
  happened anyway — to the same person who would go on to write the warning. A rule enforced by
  reading is not enforced.
- **Copy everything.** Simple, and correct, and it makes the India comparison copy 42 MB of CSV and
  19.7 MB of `static_model.json` per staged directory. The distinction between what is read and what
  is written is real and worth keeping; it is just worth keeping *in the code* rather than in a note.
- **Stage into a directory with no example files at all, and rewrite the model files' relative
  paths.** That means parsing and rewriting somebody else's model file — for `HLM_France`, a 19.7 MB
  JSON — to make a harness convenient. It would also mean the harness runs the baseline on a file the
  baseline's own authors never wrote, which is the one thing a comparison against the baseline must
  not do.
- **Make the staged links read-only, or use hard links.** A read-only symlink still resolves to a
  writable target; permissions on the link are not consulted. A hard link *is* the file, so writing
  through it is exactly the accident.
- **Write derived configs outside the staged directory.** Then the model files' relative CSV paths do
  not resolve, which is the problem staging exists to solve.

## Consequences

`link_example_files` is gone and its callers — `run.py` and `self_check.py` — pass the source config
rather than its directory, which is also the information the function needed to do its job.

The staged directory now contains one real file and a set of links, so `ls -l` in a working directory
says which files the harness considered writable. That is a small, useful piece of self-documentation
that the old version did not have.

Nothing about the derived config changes, so no config hash changes, so the two stored references
remain the right ones for the configs that produced them — checked by running both comparisons after
the change.
