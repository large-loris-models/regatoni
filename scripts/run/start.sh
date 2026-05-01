#!/usr/bin/env bash
# Single entry point for the full fuzzing pipeline:
#   Centipede fuzzer  +  alive-tv oracle  +  ASAN oracle
#
# Each invocation creates a new run directory under runs/<RUN_ID>/ and points
# runs/current at it. All per-run state (workdir, corpus, run.log, oracle
# results, miscompilations, triage, stats) lives under that directory.
#
# Usage:
#   nohup ./scripts/run/start.sh [--seeds DIR] > /dev/null 2>&1 &
#   ./scripts/run/stop.sh

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null
source "$SCRIPT_DIR/run_helpers.sh"

# ── Arg parsing ─────────────────────────────────────────────────────────────

SEEDS_ARG=""
while (( $# > 0 )); do
    case "$1" in
        --seeds)    SEEDS_ARG="${2:-}"; shift 2 ;;
        --seeds=*)  SEEDS_ARG="${1#--seeds=}"; shift ;;
        -h|--help)
            sed -n '2,11p' "$0"
            exit 0
            ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

SEED_SOURCE="${SEEDS_ARG:-$SPLIT_SEEDS_DIR}"
if [[ ! -d "$SEED_SOURCE" ]]; then
    echo "ERROR: --seeds path does not exist: $SEED_SOURCE" >&2
    exit 1
fi
if [[ -z "$(find "$SEED_SOURCE" -maxdepth 1 -type f -print -quit 2>/dev/null)" ]]; then
    echo "ERROR: --seeds path is empty: $SEED_SOURCE" >&2
    exit 1
fi

# ── Initialize run directory ────────────────────────────────────────────────

regatoni_init_run_dir "$SEED_SOURCE"
RUN_DIR="$(regatoni_run_dir)"
export RUN_DIR RUN_ID

PIDS_FILE="$RUN_DIR/pids"
RUN_LOG="$RUN_DIR/run.log"
START_TIME="$(date +%s)"

: > "$PIDS_FILE"
: > "$RUN_LOG"

log() { echo "[$(date -Is)] [start] $*" | tee -a "$RUN_LOG" >&2; }

log "RUN_ID=$RUN_ID  RUN_DIR=$RUN_DIR"

# ── Detect available cores ──────────────────────────────────────────────────

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
FUZZER_CORES="${FUZZER_CORES:-${FUZZ_JOBS:-4}}"
ALIVE_SHARDS=3

# Layout: FUZZER_CORES fuzzer + ALIVE_SHARDS alive-tv + 1 ASAN.
NEEDED=$(( FUZZER_CORES + ALIVE_SHARDS + 1 ))
if (( ${#ALL_CORES[@]} < NEEDED )); then
    log "WARNING: only ${#ALL_CORES[@]} cores available (need $NEEDED); oracles may share cores"
fi

ORACLE_CORES=("${ALL_CORES[@]:$FUZZER_CORES}")
ASAN_CORE="${ORACLE_CORES[$ALIVE_SHARDS]:-${ALL_CORES[0]}}"

# ── Check required binaries ─────────────────────────────────────────────────

FUZZ_TARGET="$BUILD_OUT/opt_fuzz_target"
ALIVE_HARNESS="$BUILD_OUT/opt_fuzz_target_alive2"
ASAN_OPT="$LLVM_BUILD_ASAN/bin/opt"

err=0
for pair in "fuzz target:$FUZZ_TARGET" "alive-tv harness:$ALIVE_HARNESS" "ASAN opt:$ASAN_OPT" "centipede:$CENTIPEDE_BIN"; do
    label="${pair%%:*}"
    path="${pair#*:}"
    if [[ ! -x "$path" ]]; then
        log "ERROR: $label not found or not executable: $path"
        err=1
    fi
done
if (( err )); then
    log "Aborting — missing binaries. Run the build scripts first."
    exit 1
fi

record_pid() {
    local name="$1" pid="$2"
    echo "$name:$pid" >> "$PIDS_FILE"
}

# ── Summary ─────────────────────────────────────────────────────────────────

print_summary() {
    local now elapsed h m s
    now="$(date +%s)"
    elapsed=$(( now - START_TIME ))
    h=$(( elapsed / 3600 ))
    m=$(( (elapsed % 3600) / 60 ))
    s=$(( elapsed % 60 ))

    local corpus_count=0
    if [[ -d "$RUN_DIR/corpus" ]]; then
        corpus_count="$(find "$RUN_DIR/corpus" -maxdepth 1 -type f 2>/dev/null | wc -l)"
    fi

    local crash_count=0
    if [[ -d "$RUN_DIR/workdir" ]]; then
        crash_count="$(find "$RUN_DIR/workdir" -maxdepth 2 -type f -path '*/crashes*/*' 2>/dev/null | wc -l)"
    fi

    local miscomp_count=0
    if [[ -d "$RUN_DIR/miscompilations" ]]; then
        miscomp_count="$(find "$RUN_DIR/miscompilations" -maxdepth 1 -type f 2>/dev/null | wc -l)"
    fi

    log "────────────────────────────────────────"
    log "Runtime:            ${h}h ${m}m ${s}s"
    log "Corpus entries:     $corpus_count"
    log "Crashes found:      $crash_count"
    log "Miscompilations:    $miscomp_count"
    log "────────────────────────────────────────"
}

# ── Shutdown handler ────────────────────────────────────────────────────────

shutdown() {
    log "Shutting down..."

    local pids=()
    if [[ -f "$PIDS_FILE" ]]; then
        while IFS=: read -r name pid; do
            [[ -n "$pid" ]] || continue
            if kill -0 "$pid" 2>/dev/null; then
                log "Sending SIGTERM to $name (PID $pid)"
                kill -TERM "$pid" 2>/dev/null || true
                pids+=("$pid")
            fi
        done < "$PIDS_FILE"
    fi

    if (( ${#pids[@]} > 0 )); then
        local deadline=$(( $(date +%s) + 5 ))
        for pid in "${pids[@]}"; do
            while kill -0 "$pid" 2>/dev/null && (( $(date +%s) < deadline )); do
                sleep 0.5
            done
            if kill -0 "$pid" 2>/dev/null; then
                log "Force-killing PID $pid"
                kill -KILL "$pid" 2>/dev/null || true
            fi
        done
    fi

    print_summary
    regatoni_finalize_run_dir "$RUN_ID"
    log "Stopped."
    exit 0
}
trap shutdown INT TERM

# ── Set up workdir, corpus, env for child processes ─────────────────────────

FUZZ_WORKDIR="$RUN_DIR/workdir"
CORPUS_DIR="$RUN_DIR/corpus"
export CORPUS_DIR

# Harness reads this to pick a directory for regatoni-mutation-stats.<pid>.csv.
export REGATONI_WORKDIR="$FUZZ_WORKDIR"

# Copy seeds from --seeds path into the per-run corpus dir.
log "Copying seeds from $SEED_SOURCE into $CORPUS_DIR..."
find "$SEED_SOURCE" -maxdepth 1 -type f -print0 | xargs -0 cp -t "$CORPUS_DIR/"
log "Copied $(find "$CORPUS_DIR" -maxdepth 1 -type f | wc -l) seed files"

# ── Centipede flags ─────────────────────────────────────────────────────────

FUZZER_FLAGS=(
    --binary="$FUZZ_TARGET"
    --workdir="$FUZZ_WORKDIR"
    --j="$FUZZER_CORES"
    --timeout_per_input="$TIMEOUT_PER_INPUT"
    --rss_limit_mb="$RSS_LIMIT_MB"
    --address_space_limit_mb=0
    --corpus_dir="$CORPUS_DIR"
    -crossover_level="$CROSSOVER_LEVEL"
    --use_counter_features
    --v=1
    --max_num_crash_reports=50000
    # Emit "FUNC: <symbol>" / "EDGE: <symbol>" lines into run.log on first
    # sighting of each new PC. Required by parse_centipede_log.py for the
    # events.jsonl feed.
    --log_features_shards=1
    # Emit intermediate coverage-report-*.latest.txt / corpus-stats-*.latest.json
    # / rusage-report-*.latest.txt every N batches. With observed exec/s ~200
    # and batch sizes of 1000-3000 inputs, batches arrive every ~5-15s, so
    # N=100 corresponds to roughly 8-25 minutes between snapshots — close to
    # the 10-minute target. Tune if exec/s deviates substantially.
    --telemetry_frequency=100
)

if [[ -n "${CORPUS_WEIGHT_METHOD:-}" && "$CORPUS_WEIGHT_METHOD" != "uniform" ]]; then
    FUZZER_FLAGS+=("--corpus_weight_method=$CORPUS_WEIGHT_METHOD")
fi
if [[ -n "${USER_FEATURE_DOMAIN_MASK:-}" ]]; then
    FUZZER_FLAGS+=("--user_feature_domain_mask=$USER_FEATURE_DOMAIN_MASK")
fi
if [[ "${USE_PCPAIR_FEATURES:-false}" == "true" ]]; then
    FUZZER_FLAGS+=("--use_pcpair_features")
fi
if [[ -n "${CALLSTACK_LEVEL:-}" ]] && (( CALLSTACK_LEVEL > 0 )); then
    FUZZER_FLAGS+=("--callstack_level=$CALLSTACK_LEVEL")
fi

# Stamp the constructed flag list into the manifest before launching.
FLAGS_JSON="$(printf '%s\n' "${FUZZER_FLAGS[@]}" | python3 -c '
import json, sys
print(json.dumps([l.rstrip("\n") for l in sys.stdin]))
')"
regatoni_manifest_set centipede_flags "$FLAGS_JSON"

# ── Start Centipede fuzzer ──────────────────────────────────────────────────

log "Starting Centipede fuzzer (${FUZZER_CORES} jobs)..."
ulimit -s unlimited
"$CENTIPEDE_BIN" "${FUZZER_FLAGS[@]}" >> "$RUN_LOG" 2>&1 &
FUZZER_PID=$!
record_pid "fuzzer" "$FUZZER_PID"
log "Fuzzer PID: $FUZZER_PID"

sleep 5

if ! kill -0 "$FUZZER_PID" 2>/dev/null; then
    log "ERROR: fuzzer exited immediately — check $RUN_LOG"
    exit 1
fi

# ── Start oracles ───────────────────────────────────────────────────────────

ORACLE_DIR="$SCRIPT_DIR/../oracles"

declare -a ALIVE_PIDS=()
declare -a ALIVE_USED_CORES=()
for shard in $(seq 0 $((ALIVE_SHARDS - 1))); do
    core="${ORACLE_CORES[$shard]:-${ALL_CORES[0]}}"
    log "Starting alive-tv oracle shard $shard on core $core..."
    taskset -c "$core" "$ORACLE_DIR/alive_tv.sh" "$CORPUS_DIR" "$shard" "$ALIVE_SHARDS" >> "$RUN_LOG" 2>&1 &
    pid=$!
    record_pid "alive_tv_$shard" "$pid"
    ALIVE_PIDS+=("$pid")
    ALIVE_USED_CORES+=("$core")
    log "alive-tv oracle shard $shard PID: $pid (core $core)"
done

log "Starting ASAN oracle on core $ASAN_CORE..."
taskset -c "$ASAN_CORE" "$ORACLE_DIR/asan_opt.sh" "$CORPUS_DIR" >> "$RUN_LOG" 2>&1 &
ASAN_PID=$!
record_pid "asan_opt" "$ASAN_PID"
log "ASAN oracle PID: $ASAN_PID (core $ASAN_CORE)"

# ── Start log parser daemon ─────────────────────────────────────────────────

PARSER="$SCRIPT_DIR/../analysis/parse_centipede_log.py"
if [[ -x "$PARSER" || -f "$PARSER" ]]; then
    log "Starting Centipede log parser daemon..."
    python3 "$PARSER" --watch \
        --log "$RUN_LOG" \
        --workdir "$RUN_DIR/stats" \
        --offset-file "$RUN_DIR/run.log.offset" \
        >> "$RUN_LOG" 2>&1 &
    PARSER_PID=$!
    record_pid "log_parser" "$PARSER_PID"
    log "Log parser PID: $PARSER_PID"
else
    log "WARNING: log parser not found at $PARSER — skipping"
fi

# ── Start dashboard regenerator daemon ──────────────────────────────────────

DASHBOARD="$SCRIPT_DIR/../analysis/render_dashboard.py"
DASHBOARD_PUBLISH_DIR="${DASHBOARD_PUBLISH_DIR:-}"
DASHBOARD_HTTP_PORT="${DASHBOARD_HTTP_PORT:-8080}"
if [[ -f "$DASHBOARD" ]]; then
    log "Starting dashboard regenerator daemon..."
    PUBLISH_ARGS=()
    if [[ -n "$DASHBOARD_PUBLISH_DIR" ]]; then
        PUBLISH_ARGS=(--publish-to "$DASHBOARD_PUBLISH_DIR")
    fi
    (while true; do
        python3 "$DASHBOARD" --run-dir "$RUN_DIR" --out "$RUN_DIR/dashboard.html" \
            "${PUBLISH_ARGS[@]}" \
            2>>"$RUN_DIR/dashboard.log" || true
        sleep 60
    done) &
    DASHBOARD_PID=$!
    record_pid "dashboard" "$DASHBOARD_PID"
    log "Dashboard daemon PID: $DASHBOARD_PID  (out: $RUN_DIR/dashboard.html)"
    if [[ -n "$DASHBOARD_PUBLISH_DIR" ]]; then
        DASH_HOST="$(hostname -f 2>/dev/null || hostname)"
        log "═══════════════════════════════════════════════"
        log "  Dashboard: http://${DASH_HOST}:${DASHBOARD_HTTP_PORT}/dashboard.html"
        log "═══════════════════════════════════════════════"
    fi
else
    log "WARNING: dashboard renderer not found at $DASHBOARD — skipping"
fi

# ── Status banner ───────────────────────────────────────────────────────────

log ""
log "═══════════════════════════════════════════════"
log "  Fuzzing pipeline running"
log "═══════════════════════════════════════════════"
log "  fuzzer      PID $FUZZER_PID   cores ${ALL_CORES[*]:0:$FUZZER_CORES}"
for i in "${!ALIVE_PIDS[@]}"; do
    log "  alive_tv_$i  PID ${ALIVE_PIDS[$i]}   core  ${ALIVE_USED_CORES[$i]}"
done
log "  asan_opt    PID $ASAN_PID   core  $ASAN_CORE"
log "───────────────────────────────────────────────"
log "  run dir:    $RUN_DIR"
log "  corpus:     $CORPUS_DIR"
log "  workdir:    $FUZZ_WORKDIR"
log "  miscomps:   $RUN_DIR/miscompilations"
log "  pids file:  $PIDS_FILE"
log "  log:        $RUN_LOG"
log "═══════════════════════════════════════════════"
log ""
log "Stop with:  ./scripts/run/stop.sh"
log ""

wait
