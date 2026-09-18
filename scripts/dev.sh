#!/usr/bin/env bash
#
# The engine and the frontend's dev server, together.
#
#   scripts/dev.sh            # http://127.0.0.1:5173, proxying /api to the engine on 8080
#   scripts/dev.sh --port 9000
#
# The Vite dev server proxies /api to `hgps serve`, so the frontend is developed against the real
# engine rather than a mock — which is the only way the types in web/src/api/types.ts stay honest
# (docs/decisions/0043-a-plain-typescript-frontend.md).
#
# Both are killed together: a stray server holding port 8080 is the thing that makes the next run
# of this script fail confusingly.

set -euo pipefail

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

PORT=8080
PRESET=release
for ((i = 1; i <= $#; i++)); do
    case "${!i}" in
        --port) i=$((i + 1)); PORT="${!i}" ;;
        --preset) i=$((i + 1)); PRESET="${!i}" ;;
        -h|--help)
            sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "dev.sh: unknown argument '${!i}'" >&2; exit 2 ;;
    esac
done

BINARY="$REPO_ROOT/out/build/$PRESET/src/healthgps"
if [[ ! -x "$BINARY" ]]; then
    echo "dev.sh: $BINARY is not there; build it first:" >&2
    echo "    cmake --preset $PRESET && cmake --build --preset $PRESET" >&2
    exit 2
fi

if [[ ! -d "$REPO_ROOT/web/node_modules" ]]; then
    echo "dev.sh: installing the frontend's dependencies"
    (cd "$REPO_ROOT/web" && npm install)
fi

RUNS="${HGPS_RUNS:-$REPO_ROOT/out/runs}"
mkdir -p "$RUNS"

engine_pid=""
vite_pid=""
cleanup() {
    # Both, always: a stray server holding the port is what makes the next run fail confusingly.
    [[ -n "$vite_pid" ]] && kill "$vite_pid" 2>/dev/null || true
    [[ -n "$engine_pid" ]] && kill "$engine_pid" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "dev.sh: engine on 127.0.0.1:$PORT, runs in $RUNS"
"$BINARY" serve --host 127.0.0.1 --port "$PORT" \
    --configs "$REPO_ROOT/examples" \
    --runs "$RUNS" \
    --schema "$REPO_ROOT/schemas/v2/config.json" &
engine_pid=$!

# Wait for it to answer rather than sleeping a guess: a frontend that starts first shows "engine
# unreachable" and makes somebody wonder what they broke.
for _ in $(seq 1 50); do
    if curl -sf "http://127.0.0.1:$PORT/api/version" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

echo "dev.sh: frontend on http://127.0.0.1:5173"
# `exec`, so this subshell *becomes* vite and `$vite_pid` is vite's own pid. Without it the pid is
# the subshell running `npm run dev`, npm spawns vite as a child, and killing the subshell leaves
# vite holding port 5173 — which is exactly the stray process this script's trap exists to prevent,
# and is what the first version of it did.
(cd "$REPO_ROOT/web" && HGPS_SERVER="http://127.0.0.1:$PORT" exec npx vite) &
vite_pid=$!

# Not `wait -n`: that is bash 4.3 and up, and macOS ships bash 3.2 — which is what
# /usr/bin/env bash finds on a Mac that has not installed a newer one. The first version of this
# script used it and exited immediately with "wait: -n: invalid option", having started both
# processes and then killed them through the trap.
#
# Polling instead: whichever dies first ends the loop, and the trap stops the other.
while kill -0 "$engine_pid" 2>/dev/null && kill -0 "$vite_pid" 2>/dev/null; do
    sleep 1
done
