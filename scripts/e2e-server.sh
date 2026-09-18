#!/usr/bin/env bash
#
# The server the end-to-end tests drive: the real `hgps serve`, hosting the real built frontend,
# over the synthetic fixture packs.
#
#   scripts/e2e-server.sh [--port N] [--preset NAME] [--runs DIR] [--configs DIR]
#
# Playwright starts this through its `webServer` option and stops it afterwards
# (web/playwright.config.ts). It is a script rather than a line of configuration because the
# fixture packs have to be laid out first: the server's example list is "a directory holding a
# config.json", and each pack's `data.source` is the relative `../data`, so the packs and the one
# data store they share go under a root of their own.
#
# Nothing here is a mock. The point of these tests is the whole thing at once — the browser, the
# built assets, the HTTP API, the engine — so the only thing that is synthetic is the data, and
# that is synthetic everywhere else in this repository too.

set -euo pipefail

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

PORT=8099
PRESET=release
RUNS="$REPO_ROOT/out/e2e/runs"
CONFIGS="$REPO_ROOT/out/e2e/configs"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --preset) PRESET="$2"; shift 2 ;;
        --runs) RUNS="$2"; shift 2 ;;
        --configs) CONFIGS="$2"; shift 2 ;;
        -h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "e2e-server.sh: unknown argument '$1'" >&2; exit 2 ;;
    esac
done

BINARY="$REPO_ROOT/out/build/$PRESET/src/healthgps"
GENERATOR="$REPO_ROOT/out/build/$PRESET/tools/gen-fixtures"
for program in "$BINARY" "$GENERATOR"; do
    if [[ ! -x "$program" ]]; then
        echo "e2e-server.sh: $program is not there; build it first:" >&2
        echo "    cmake --preset $PRESET && cmake --build --preset $PRESET" >&2
        exit 2
    fi
done

if [[ ! -f "$REPO_ROOT/web/dist/index.html" ]]; then
    echo "e2e-server.sh: web/dist is not built; run 'npx vite build' in web/" >&2
    exit 2
fi

# A fresh pack and a fresh runs directory on every start, so the history screen's test can say what
# it expects to see rather than what happens to be left over from last time.
PACK="$REPO_ROOT/out/e2e/pack"
rm -rf "$PACK" "$CONFIGS" "$RUNS"
mkdir -p "$RUNS"
"$GENERATOR" --output "$PACK" >/dev/null

# `<root>/<pack id>/config.json`, with the one shared `<root>/data`. The ids are the directory
# names, which is what the browser will show in the example chooser.
mkdir -p "$CONFIGS"
cp -R "$PACK/model" "$CONFIGS/Synthetic"
cp -R "$PACK/model-b" "$CONFIGS/synthetic-b"
cp -R "$PACK/data" "$CONFIGS/data"

echo "e2e-server.sh: 127.0.0.1:$PORT, configs in $CONFIGS, runs in $RUNS"
exec "$BINARY" serve \
    --host 127.0.0.1 --port "$PORT" \
    --configs "$CONFIGS" \
    --runs "$RUNS" \
    --web "$REPO_ROOT/web/dist" \
    --schema "$REPO_ROOT/schemas/v2/config.json"
