#!/usr/bin/env bash
# Single entry point for the full fuzzing pipeline:
#   Centipede fuzzer  +  alive-tv oracle  +  ASAN oracle
#
# Usage:
#   nohup ./scripts/run/start.sh > build/run.log 2>&1 &
#   cat build/run_state/pids
#   ./scripts/run/stop.sh

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null

# ── State directory ──────────────────────────────────────────────────────────

RUN_STATE="$BUILD_OUT/run_state"
PIDS_FILE="$RUN_STATE/pids"
RUN_LOG="$RUN_STATE/run.log"
START_TIME="$(date +%s)"

mkdir -p "$RUN_STATE"
: > "$PIDS_FILE"
: > "$RUN_LOG"

log() { echo "[$(date -Is)] [start] $*" | tee -a "$RUN_LOG" >&2; }

# ── Detect available cores (mirrors run_oracles.sh) ─────────────────────────

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
FUZZER_CORES=${FUZZ_JOBS:-4}

if (( ${#ALL_CORES[@]} < FUZZER_CORES + 2 )); then
    log "WARNING: only ${#ALL_CORES[@]} cores available; oracles may share cores with fuzzer"
fi

# Cores after the fuzzer's allocation are for oracles.
ORACLE_CORES=("${ALL_CORES[@]:$FUZZER_CORES}")
ALIVE_CORE="${ORACLE_CORES[0]:-${ALL_CORES[0]}}"
ASAN_CORE="${ORACLE_CORES[1]:-${ALL_CORES[0]}}"

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

# ── Record a PID ────────────────────────────────────────────────────────────

record_pid() {
    local name="$1" pid="$2"
    echo "$name:$pid" >> "$PIDS_FILE"
}

# ── Summary ─────────────────────────────────────────────────────────────────

print_summary() {
    local now
    now="$(date +%s)"
    local elapsed=$(( now - START_TIME ))
    local h=$(( elapsed / 3600 ))
    local m=$(( (elapsed % 3600) / 60 ))
    local s=$(( elapsed % 60 ))

    local corpus_count=0
    if [[ -d "$CORPUS_DIR" ]]; then
        corpus_count="$(find "$CORPUS_DIR" -maxdepth 1 -type f 2>/dev/null | wc -l)"
    fi

    local crash_count=0
    local workdir
    workdir="$(ls -1dt "$BUILD_OUT"/workdir_*/crashes* 2>/dev/null | head -n1)"
    if [[ -n "$workdir" ]]; then
        crash_count="$(find "$workdir" -maxdepth 1 -type f 2>/dev/null | wc -l)"
    fi

    local miscomp_count=0
    if [[ -d "$PROJECT_ROOT/miscompilations" ]]; then
        miscomp_count="$(find "$PROJECT_ROOT/miscompilations" -maxdepth 1 -type f 2>/dev/null | wc -l)"
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

    # Read PIDs from file (may have been written by this or another process).
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

    # Wait up to 5 seconds for graceful exit, then SIGKILL.
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
    log "Stopped."
    exit 0
}
trap shutdown INT TERM

# ── Start Centipede fuzzer ──────────────────────────────────────────────────

FUZZ_WORKDIR="$BUILD_OUT/workdir_$(date +%m%d%Y)"
mkdir -p "$FUZZ_WORKDIR" "$CORPUS_DIR"

# Copy seeds into corpus dir if empty (same as run_fuzzer.sh).
if [[ -d "$SPLIT_SEEDS_DIR" ]] && [[ -z "$(ls -A "$CORPUS_DIR" 2>/dev/null)" ]]; then
    log "Copying seeds into corpus dir..."
    cp "$SPLIT_SEEDS_DIR"/* "$CORPUS_DIR/" 2>/dev/null || true
    log "Copied $(ls "$CORPUS_DIR" | wc -l) seed files"
fi

FUZZER_FLAGS=(
    --binary="$FUZZ_TARGET"
    --workdir="$FUZZ_WORKDIR"
    --j="$FUZZER_CORES"
    --timeout_per_input=10
    --rss_limit_mb=8142
    --address_space_limit_mb=0
    --corpus_dir="$CORPUS_DIR"
    --use_counter_features
    --v=1
    --max_num_crash_reports=50000
)

log "Starting Centipede fuzzer (${FUZZER_CORES} jobs)..."
ulimit -s unlimited
"$CENTIPEDE_BIN" "${FUZZER_FLAGS[@]}" >> "$RUN_LOG" 2>&1 &
FUZZER_PID=$!
record_pid "fuzzer" "$FUZZER_PID"
log "Fuzzer PID: $FUZZER_PID"

# Give the fuzzer a moment to initialize and populate the corpus dir.
sleep 5

if ! kill -0 "$FUZZER_PID" 2>/dev/null; then
    log "ERROR: fuzzer exited immediately — check $RUN_LOG"
    exit 1
fi

# ── Start oracles ───────────────────────────────────────────────────────────

ORACLE_DIR="$SCRIPT_DIR/../oracles"

log "Starting alive-tv oracle on core $ALIVE_CORE..."
taskset -c "$ALIVE_CORE" "$ORACLE_DIR/alive_tv.sh" "$CORPUS_DIR" >> "$RUN_LOG" 2>&1 &
ALIVE_PID=$!
record_pid "alive_tv" "$ALIVE_PID"
log "alive-tv oracle PID: $ALIVE_PID (core $ALIVE_CORE)"

log "Starting ASAN oracle on core $ASAN_CORE..."
taskset -c "$ASAN_CORE" "$ORACLE_DIR/asan_opt.sh" "$CORPUS_DIR" >> "$RUN_LOG" 2>&1 &
ASAN_PID=$!
record_pid "asan_opt" "$ASAN_PID"
log "ASAN oracle PID: $ASAN_PID (core $ASAN_CORE)"

# ── Status banner ───────────────────────────────────────────────────────────

log ""
log "═══════════════════════════════════════════════"
log "  Fuzzing pipeline running"
log "═══════════════════════════════════════════════"
log "  fuzzer      PID $FUZZER_PID   cores ${ALL_CORES[*]:0:$FUZZER_CORES}"
log "  alive_tv    PID $ALIVE_PID   core  $ALIVE_CORE"
log "  asan_opt    PID $ASAN_PID   core  $ASAN_CORE"
log "───────────────────────────────────────────────"
log "  corpus:     $CORPUS_DIR"
log "  workdir:    $FUZZ_WORKDIR"
log "  miscomps:   $PROJECT_ROOT/miscompilations"
log "  pids file:  $PIDS_FILE"
log "  log:        $RUN_LOG"
log "═══════════════════════════════════════════════"
log ""
log "Stop with:  ./scripts/run/stop.sh"
log ""

# ── Wait on all children ────────────────────────────────────────────────────

wait
