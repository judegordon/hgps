#!/usr/bin/env bash
#
# The one command that checks everything: configure and build every preset, run the tests, then
# run the equivalence harness against the baseline. Fails on the first error.
#
#   scripts/check.sh                 # everything
#   scripts/check.sh --fast          # release preset and its tests only
#   scripts/check.sh --no-equivalence
#
# Requires VCPKG_ROOT to point at a vcpkg checkout.

set -euo pipefail

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

FAST=0
RUN_EQUIVALENCE=1
for arg in "$@"; do
    case "$arg" in
        --fast) FAST=1 ;;
        --no-equivalence) RUN_EQUIVALENCE=0 ;;
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

if [[ "$RUN_EQUIVALENCE" -eq 1 && "$FAST" -eq 0 ]]; then
    step "equivalence harness"
    if [[ -x tests/equivalence/run.py ]]; then
        # Twenty seeds of the reference example against the stored baseline reference. Add
        # --refresh-reference to re-run the baseline binary itself; see docs/equivalence.md.
        # --max-failures 60: docs/equivalence.md §"The result" records the 54 that this
        # implementation has and traces them to one cause. A regression past 60 fails.
        python3 tests/equivalence/run.py --example HLM_France --seeds 20 --use-reference \
            --max-failures 60
    else
        echo "check.sh: tests/equivalence/run.py is missing or not executable." >&2
        exit 1
    fi
fi

step "all checks passed"
