#!/usr/bin/env bash
#
# The one command that checks everything: configure and build every preset, run the tests, then
# run the equivalence harness against the baseline. Fails on the first error.
#
#   scripts/check.sh                 # everything
#   scripts/check.sh --fast          # release preset and its tests only
#   scripts/check.sh --no-equivalence
#   scripts/check.sh --no-web
#
# Requires VCPKG_ROOT to point at a vcpkg checkout, and — unless --no-web — npm.

set -euo pipefail

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

FAST=0
RUN_EQUIVALENCE=1
RUN_WEB=1
for arg in "$@"; do
    case "$arg" in
        --fast) FAST=1 ;;
        --no-equivalence) RUN_EQUIVALENCE=0 ;;
        --no-web) RUN_WEB=0 ;;
        *) echo "check.sh: unknown argument '$arg'" >&2; exit 2 ;;
    esac
done

if [[ -z "${VCPKG_ROOT:-}" ]]; then
    echo "check.sh: VCPKG_ROOT is not set; it must point at a vcpkg checkout." >&2
    exit 2
fi

step() { printf '\n=== %s\n' "$*"; }

if [[ "$FAST" -eq 1 ]]; then
    PRESETS=(release)
else
    # Release first: it is the one the equivalence harness and the profiling numbers use, so a
    # failure there should be the first thing reported.
    PRESETS=(release debug asan-ubsan tsan)
fi

for preset in "${PRESETS[@]}"; do
    step "configure: $preset"
    cmake --preset "$preset"

    step "build: $preset"
    cmake --build --preset "$preset"

    step "test: $preset"
    ctest --preset "$preset"
done

# The graphical host, after the release build it needs and before the long comparison. Until this
# run check.sh verified nothing in web/ at all, which made "green at every commit" a claim about the
# C++ only — while three of the previous run's defects were in the frontend and its dev script
# (docs/decisions/0045-end-to-end-tests-in-a-real-browser.md).
if [[ "$RUN_WEB" -eq 1 ]]; then
    if ! command -v npm >/dev/null 2>&1; then
        echo "check.sh: npm is not on the path; the frontend cannot be checked. Use --no-web to skip it." >&2
        exit 2
    fi

    step "frontend: install, type-check, test, build"
    (
        cd "$REPO_ROOT/web"
        npm ci
        npm run typecheck
        npm test
        npx vite build
    )

    if [[ "$FAST" -eq 0 ]]; then
        # A real browser against the real server over the synthetic packs — seven seconds, because
        # a run of a synthetic pack is a fifth of one. It needs the release build, which is why it
        # is here rather than beside the other frontend steps.
        step "frontend: end to end"
        (
            cd "$REPO_ROOT/web"
            npx playwright install chromium
            npx playwright test
        )
    fi
fi

if [[ "$RUN_EQUIVALENCE" -eq 1 && "$FAST" -eq 0 ]]; then
    step "equivalence harness"
    if [[ -x tests/equivalence/run.py ]]; then
        # Twenty seeds of each example against the stored baseline reference. Add
        # --refresh-reference to re-run the baseline binary itself; see docs/equivalence.md.
        #
        # HLM_France has no failure budget. KevinHall_FINCH has one, of 3 out of its 111,836
        # comparisons, and it is the first this project has had for two runs.
        #
        # Comparing every output family rather than one file per run took that example from 22,616
        # comparisons to 111,836. Three are out of tolerance at 20 seeds and four at 60, and no cell
        # fails in both. Re-scoring the 60-seed run over subsets of itself says why: the allowance's
        # width is estimated from the same twenty draws it is judging, so a series sitting at a
        # small signed offset well inside its allowance fails in every year at once whenever a seed
        # set gives a tight sample. The worst 20-seed draw of 100 has 45 failures, 32 of them in one
        # whole-population series — so this is not about the stratified files and is older than this
        # run; what this run did was add four times as many groups for it to show up in.
        #
        # 3 is what this build produces at these seeds, with no margin, so any increase fails. The
        # other two disjoint thirds of that 60-seed run give 1 each. docs/backlog.md item 6 is the
        # work that removes the budget; docs/equivalence.md, "There is a failure budget again".
        #
        # Both examples, one per model family: HLM_France covers HLM/EBHLM and KevinHall_FINCH
        # covers StaticLinear/KevinHall and the S1 policy model.
        python3 tests/equivalence/run.py --example HLM_France --seeds 20 --use-reference
        python3 tests/equivalence/run.py --example KevinHall_FINCH --seeds 20 --use-reference \
            --max-failures 3
    else
        echo "check.sh: tests/equivalence/run.py is missing or not executable." >&2
        exit 1
    fi

    # The column inventory: every column of every output family, on both sides, and which of them
    # are identically zero. It asks a question the comparison above cannot express — "the baseline
    # has numbers here and we have nothing" is not a disagreement about a distribution — and it is
    # the check that would have caught the 49 columns the previous run found by hand
    # (docs/equivalence.md, "Every column of every family").
    #
    # One run of this build per example against the stored baseline inventory, so it needs no
    # baseline binary; all three runnable examples, `HLM_India` at the reduced cohort.
    step "column coverage"
    python3 scripts/column-coverage.py
fi

step "all checks passed"
