#!/usr/bin/env bash
#
# The wall time, CPU time and peak memory of one or more examples, as a table and as JSON.
#
#   scripts/measure.sh                                  # HLM_France and KevinHall_FINCH, 3 runs
#   scripts/measure.sh --runs 5 --example HLM_France
#   scripts/measure.sh --binary /path/to/healthgps --json out/timings.json
#
# Written to be run by a person and by CI. Every figure in docs/performance.md was taken with the
# same three numbers on the same three examples; this script exists so that the *Linux* ones can be
# taken at all, since every measurement in that document has been macOS and Apple clang.
#
# **Read CI's numbers as indicative only.** A GitHub-hosted runner is a shared virtual machine with
# neighbours, and the same job has been seen to vary by tens of percent between runs of the same
# commit. What the numbers are good for is an order of magnitude and a ratio between two examples on
# one machine; what they are not good for is comparing a run today against a run last month, or
# against the macOS figures. A regression test on these numbers would fail on the weather.
#
# The run uses a derived config — the example's own, with `output.folder` cleared and
# `output.file_name` fixed — so that `--output` decides where results go and two runs can be
# compared. The derived config is written beside the example so that its relative input paths still
# resolve, and removed afterwards.

set -euo pipefail

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

RUNS=3
BINARY="$REPO_ROOT/out/build/release/src/healthgps"
JSON=""
EXAMPLES=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --runs) RUNS="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --example) EXAMPLES+=("$2"); shift 2 ;;
        --json) JSON="$2"; shift 2 ;;
        -h|--help) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "measure.sh: unknown argument '$1'" >&2; exit 2 ;;
    esac
done

if [[ ${#EXAMPLES[@]} -eq 0 ]]; then
    EXAMPLES=(HLM_France KevinHall_FINCH)
fi

if [[ ! -x "$BINARY" ]]; then
    echo "measure.sh: $BINARY is not there; build it first." >&2
    exit 2
fi

# `/usr/bin/time` rather than the shell's builtin, for the peak resident set size. The two spell
# their flags differently and print different things, which is the only platform difference here.
TIME_FLAG="-l"
if [[ "$(uname -s)" != "Darwin" ]]; then
    TIME_FLAG="-v"
fi

WORK="$REPO_ROOT/out/measure"
rm -rf "$WORK"
mkdir -p "$WORK"

DERIVED=()
cleanup() {
    for path in "${DERIVED[@]:-}"; do
        [[ -n "$path" ]] && rm -f "$path"
    done
}
trap cleanup EXIT INT TERM

for example in "${EXAMPLES[@]}"; do
    config="$REPO_ROOT/examples/$example/config.json"
    if [[ ! -f "$config" ]]; then
        echo "measure.sh: no such example '$example'" >&2
        exit 2
    fi

    derived="$REPO_ROOT/examples/$example/measure-config.json"
    DERIVED+=("$derived")
    python3 - "$config" "$derived" <<'PY'
import json, sys
document = json.loads(open(sys.argv[1]).read())
document["output"]["file_name"] = "result.csv"
document["output"]["folder"] = ""
# Individual tracking writes a second, large file and is not what the model's cost is about.
document["output"].pop("individual_id_tracking", None)
open(sys.argv[2], "w").write(json.dumps(document, indent=2))
PY

    for run in $(seq 1 "$RUNS"); do
        out="$WORK/$example-$run"
        mkdir -p "$out"
        /usr/bin/time "$TIME_FLAG" "$BINARY" --config "$derived" --output "$out" \
            > "$out/stdout.txt" 2> "$out/time.txt" || {
            echo "measure.sh: $example run $run failed" >&2
            tail -5 "$out/time.txt" >&2
            exit 1
        }
    done
done

python3 - "$WORK" "$RUNS" "$JSON" "${EXAMPLES[@]}" <<'PY'
import json, platform, re, subprocess, sys, pathlib

work, runs, json_path = pathlib.Path(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
examples = sys.argv[4:]


def parse(text: str) -> tuple[float, float, float]:
    """(wall seconds, cpu seconds, peak MiB) from either /usr/bin/time dialect."""
    macos = re.search(r"([\d.]+)\s+real\s+([\d.]+)\s+user\s+([\d.]+)\s+sys", text)
    if macos:
        peak = int(re.search(r"(\d+)\s+maximum resident set size", text).group(1))
        return (float(macos.group(1)), float(macos.group(2)) + float(macos.group(3)),
                peak / 1048576)

    def field(name: str) -> str:
        return re.search(rf"{name}:\s*(\S+)", text).group(1)

    clock = field(r"Elapsed \(wall clock\) time \(h:mm:ss or m:ss\)")
    parts = [float(p) for p in clock.split(":")]
    wall = parts[0] * 60 + parts[1] if len(parts) == 2 else parts[0] * 3600 + parts[1] * 60 + parts[2]
    cpu = float(field("User time \\(seconds\\)")) + float(field("System time \\(seconds\\)"))
    # GNU time reports kilobytes, and has done since the ru_maxrss unit confusion was settled.
    return wall, cpu, int(field(r"Maximum resident set size \(kbytes\)")) / 1024


report = {
    "platform": f"{platform.system()} {platform.machine()}",
    "runs": runs,
    "examples": {},
}

print(f"{'example':20s} {'wall best':>10s} {'median':>9s} {'cpu best':>9s} "
      f"{'median':>9s} {'peak MiB':>9s}")
for example in examples:
    walls, cpus, peaks = [], [], []
    for run in range(1, runs + 1):
        wall, cpu, peak = parse((work / f"{example}-{run}" / "time.txt").read_text())
        walls.append(wall)
        cpus.append(cpu)
        peaks.append(peak)
    walls.sort(); cpus.sort(); peaks.sort()
    middle = len(walls) // 2
    report["examples"][example] = {
        "wall_seconds": walls, "cpu_seconds": cpus, "peak_mib": peaks,
    }
    print(f"{example:20s} {walls[0]:10.2f} {walls[middle]:9.2f} {cpus[0]:9.2f} "
          f"{cpus[middle]:9.2f} {peaks[middle]:9.1f}")

if json_path:
    pathlib.Path(json_path).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(json_path).write_text(json.dumps(report, indent=2) + "\n")
    print(f"\nwrote {json_path}")
PY
