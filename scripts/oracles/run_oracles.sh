#!/usr/bin/env bash
# Dispatcher: launch each oracle script pinned to a free CPU core.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null

# --- Detect available cores (non-sequential on this host) -------------------
detect_cores() {
    local cpus=""
    if [[ -r /sys/fs/cgroup/cpuset.cpus.effective ]]; then
        cpus="$(cat /sys/fs/cgroup/cpuset.cpus.effective)"
    elif [[ -r /sys/fs/cgroup/cpuset/cpuset.cpus ]]; then
        cpus="$(cat /sys/fs/cgroup/cpuset/cpuset.cpus)"
    fi
    if [[ -z "$cpus" ]]; then
        cpus="0-$(($(nproc) - 1))"
    fi
    # Expand "1,12,20,32-35" into space-separated list
    local out=() part lo hi
    IFS=',' read -ra parts <<< "$cpus"
    for part in "${parts[@]}"; do
        if [[ "$part" == *-* ]]; then
            lo="${part%-*}"; hi="${part#*-}"
            for ((i=lo; i<=hi; i++)); do out+=("$i"); done
        else
            out+=("$part")
        fi
    done
    printf '%s\n' "${out[@]}"
}

mapfile -t ALL_CORES < <(detect_cores)
FUZZER_CORES=4
# Default: cores after the first 4 (reserved for fuzzer)
DEFAULT_CORES=("${ALL_CORES[@]:$FUZZER_CORES}")

# --- Default corpus discovery (mirrors verify_corpus.sh layout) -------------
default_corpus() {
    # run_fuzzer.sh creates build/workdir_<date>/ with corpus.* subdirs.
    # verify_corpus.sh uses $CORPUS_DIR. Prefer the latter if populated.
    if [[ -d "$CORPUS_DIR" ]] && compgen -G "$CORPUS_DIR/" > /dev/null; then
        echo "$CORPUS_DIR"
        return
    fi
    local latest
    latest="$(ls -1dt "$BUILD_OUT"/workdir_*/corpus.* 2>/dev/null | head -n1)"
    if [[ -n "$latest" ]]; then
        echo "$latest"
        return
    fi
    echo "$CORPUS_DIR"
}

# --- CLI --------------------------------------------------------------------
CORES_ARG=""
CORPUS_ARG=""
ORACLES_ARG=""

usage() {
    cat >&2 <<EOF
Usage: $0 [--cores c1,c2,...] [--corpus DIR] [--oracles name1,name2,...]

Defaults:
  --cores   free cores after the fuzzer's first $FUZZER_CORES: ${DEFAULT_CORES[*]}
  --corpus  $(default_corpus)
  --oracles every *.sh in $SCRIPT_DIR except common.sh / run_oracles.sh
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --cores)   CORES_ARG="$2"; shift 2 ;;
        --corpus)  CORPUS_ARG="$2"; shift 2 ;;
        --oracles) ORACLES_ARG="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage; exit 1 ;;
    esac
done

if [[ -n "$CORES_ARG" ]]; then
    IFS=',' read -ra CORES <<< "$CORES_ARG"
else
    CORES=("${DEFAULT_CORES[@]}")
fi

CORPUS="${CORPUS_ARG:-$(default_corpus)}"

if [[ -n "$ORACLES_ARG" ]]; then
    IFS=',' read -ra ORACLES <<< "$ORACLES_ARG"
else
    ORACLES=()
    for f in "$SCRIPT_DIR"/*.sh; do
        [[ -e "$f" ]] || continue
        base="$(basename "$f" .sh)"
        case "$base" in
            common|run_oracles) continue ;;
        esac
        ORACLES+=("$base")
    done
fi

if (( ${#ORACLES[@]} == 0 )); then
    echo "[run_oracles] no oracle scripts found in $SCRIPT_DIR" >&2
    exit 1
fi

if (( ${#CORES[@]} == 0 )); then
    echo "[run_oracles] no cores available" >&2
    exit 1
fi

# --- Launch -----------------------------------------------------------------
declare -a PIDS=()

cleanup() {
    echo "[run_oracles] shutting down ${#PIDS[@]} oracle(s)..." >&2
    for pid in "${PIDS[@]}"; do
        kill -TERM "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    exit 0
}
trap cleanup INT TERM

echo "[run_oracles] corpus: $CORPUS"
echo "[run_oracles] cores:  ${CORES[*]}"
echo "[run_oracles] oracles: ${ORACLES[*]}"
echo ""

idx=0
for oracle in "${ORACLES[@]}"; do
    script="$SCRIPT_DIR/$oracle.sh"
    if [[ ! -x "$script" ]]; then
        echo "[run_oracles] skip $oracle: $script not executable" >&2
        continue
    fi
    core="${CORES[$(( idx % ${#CORES[@]} ))]}"
    log="$BUILD_OUT/oracle_results/$oracle.out"
    mkdir -p "$(dirname "$log")"
    echo "[run_oracles] core $core -> $oracle  (log: $log)"
    taskset -c "$core" "$script" "$CORPUS" >>"$log" 2>&1 &
    PIDS+=("$!")
    idx=$((idx + 1))
done

echo ""
echo "[run_oracles] ${#PIDS[@]} oracle(s) running. Ctrl-C to stop."
wait
