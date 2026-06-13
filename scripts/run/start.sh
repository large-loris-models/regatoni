#!/usr/bin/env bash
# Single entry point for the full fuzzing pipeline:
#   Centipede fuzzer  +  alive-tv oracle  +  ASAN oracle
#
# Each invocation creates a new run directory under runs/<RUN_ID>/ and points
# runs/current at it. All per-run state (workdir, corpus, run.log, oracle
# results, miscompilations, triage, stats) lives under that directory.
#
# Usage:
#   nohup ./scripts/run/start.sh [--seeds DIR[,DIR...]] [--seeds DIR ...] \
#         [--clean] [--dry-run] > /dev/null 2>&1 &
#   ./scripts/run/stop.sh
#
# --seeds    seed directory(ies); repeatable and/or comma-separated.
#            Default: $SPLIT_SEEDS_DIR.
# --clean    isolate dedup.db at $RUN_DIR/dedup.db and flush Redis.
# --dry-run  set up the run dir and copy seeds, then exit before launching
#            Centipede or any oracles. Useful for verifying flags/seeds.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null
source "$SCRIPT_DIR/run_helpers.sh"

# ── Arg parsing ─────────────────────────────────────────────────────────────

SEED_SOURCES=()
CLEAN_RUN=0
DRY_RUN=0
_append_seed_arg() {
    local raw="$1"
    local part
    IFS=',' read -r -a _parts <<< "$raw"
    for part in "${_parts[@]}"; do
        [[ -n "$part" ]] && SEED_SOURCES+=("$part")
    done
}
while (( $# > 0 )); do
    case "$1" in
        --seeds)    _append_seed_arg "${2:-}"; shift 2 ;;
        --seeds=*)  _append_seed_arg "${1#--seeds=}"; shift ;;
        --clean)    CLEAN_RUN=1; shift ;;
        --dry-run)  DRY_RUN=1; shift ;;
        -h|--help)
            sed -n '2,18p' "$0"
            exit 0
            ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

if (( ${#SEED_SOURCES[@]} == 0 )); then
    SEED_SOURCES=("$SPLIT_SEEDS_DIR")
fi

for src in "${SEED_SOURCES[@]}"; do
    if [[ ! -d "$src" ]]; then
        echo "ERROR: --seeds path does not exist: $src" >&2
        exit 1
    fi
    if [[ -z "$(find "$src" -maxdepth 1 -type f -print -quit 2>/dev/null)" ]]; then
        echo "ERROR: --seeds path is empty: $src" >&2
        exit 1
    fi
done

# Comma-joined for manifest.json.
SEED_SOURCE_DESC="$(IFS=,; echo "${SEED_SOURCES[*]}")"

# ── Initialize run directory ────────────────────────────────────────────────

regatoni_init_run_dir "$SEED_SOURCE_DESC"
RUN_DIR="$(regatoni_run_dir)"
export RUN_DIR RUN_ID

PIDS_FILE="$RUN_DIR/pids"
RUN_LOG="$RUN_DIR/run.log"
START_TIME="$(date +%s)"

: > "$PIDS_FILE"
: > "$RUN_LOG"

log() { echo "[$(date -Is)] [start] $*" | tee -a "$RUN_LOG" >&2; }

log "RUN_ID=$RUN_ID  RUN_DIR=$RUN_DIR"

# ── --clean: isolate dedup.db, flush Redis ──────────────────────────────────
# Downstream Python/bash scripts honor REGATONI_DEDUP_DB and fall back to the
# project-root dedup.db when it is unset.

if (( CLEAN_RUN )); then
    DEDUP_DB_PATH="$RUN_DIR/dedup.db"
    export REGATONI_DEDUP_DB="$DEDUP_DB_PATH"

    redis_flushed="no"
    if command -v redis-cli >/dev/null 2>&1; then
        if redis-cli -h "${REDIS_HOST:-127.0.0.1}" -p "${REDIS_PORT:-6379}" \
                FLUSHDB >/dev/null 2>&1; then
            redis_flushed="yes"
        else
            log "WARNING: redis-cli FLUSHDB failed (Redis unreachable?); continuing"
        fi
    else
        log "redis-cli not found; skipping FLUSHDB"
    fi

    if [[ "$redis_flushed" == "yes" ]]; then
        log "Clean run: isolated dedup.db at $DEDUP_DB_PATH, Redis flushed"
    else
        log "Clean run: isolated dedup.db at $DEDUP_DB_PATH (Redis not flushed)"
    fi
else
    DEDUP_DB_PATH="${REGATONI_DEDUP_DB:-$PROJECT_ROOT/dedup.db}"
fi

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

# ── Campaign shape (env-configurable) ───────────────────────────────────────
# FUZZ_TARGET_KIND  opt      -> Centipede coverage on `opt -O2` (middle-end).
#                   codegen  -> coverage on riscv64 backend codegen (isel) — use
#                              this for the RISC-V isel hunt (codegen_fuzz_target).
# ORACLE_SET        default      -> alive-tv (xALIVE_SHARDS) + ASAN, validating `opt`.
#                   backend_only -> skip those (free their cores); backend-tv only.
# BACKEND_TV_ENABLE 1 -> backend-tv (riscv64) oracle, BACKEND_TV_SHARDS hash-shards
#                        each of SelectionDAG + GlobalISel (backend-tv is the slow
#                        bottleneck, so scale shards up on big boxes).
# Focused isel campaign on a 16-core box, e.g.:
#   FUZZ_TARGET_KIND=codegen ORACLE_SET=backend_only BACKEND_TV_ENABLE=1 \
#   BACKEND_TV_SHARDS=5 FUZZER_CORES=6 ./scripts/run/start.sh --seeds int_func_seeds
FUZZ_TARGET_KIND="${FUZZ_TARGET_KIND:-opt}"
ORACLE_SET="${ORACLE_SET:-default}"
BACKEND_TV_ENABLE="${BACKEND_TV_ENABLE:-0}"
BACKEND_TV_ARCH="${BACKEND_TV_ARCH:-riscv64}"
BACKEND_TV_SHARDS="${BACKEND_TV_SHARDS:-1}"      # shards per isel mode

RUN_MIDEND_ORACLES=1
[[ "$ORACLE_SET" == backend_only ]] && RUN_MIDEND_ORACLES=0

# Core accounting: fuzzer takes the first FUZZER_CORES; oracles are handed the
# rest sequentially via next_oracle_core (wrapping to core 0 if oversubscribed).
alive_n=$(( RUN_MIDEND_ORACLES ? ALIVE_SHARDS : 0 ))
asan_n=$(( RUN_MIDEND_ORACLES ? 1 : 0 ))
btv_n=$(( BACKEND_TV_ENABLE ? 2 * BACKEND_TV_SHARDS : 0 ))
NEEDED=$(( FUZZER_CORES + alive_n + asan_n + btv_n ))
if (( ${#ALL_CORES[@]} < NEEDED )); then
    log "WARNING: only ${#ALL_CORES[@]} cores available (need $NEEDED); oracles may share cores"
fi

ORACLE_CORES=("${ALL_CORES[@]:$FUZZER_CORES}")
_oc_idx=0
next_oracle_core() {
    local c="${ORACLE_CORES[$_oc_idx]:-${ALL_CORES[0]}}"
    _oc_idx=$((_oc_idx + 1))
    echo "$c"
}

# ── Check required binaries ─────────────────────────────────────────────────

case "$FUZZ_TARGET_KIND" in
    opt)     FUZZ_TARGET="$BUILD_OUT/opt_fuzz_target" ;;
    codegen) FUZZ_TARGET="$BUILD_OUT/codegen_fuzz_target" ;;
    *) log "ERROR: FUZZ_TARGET_KIND must be opt|codegen (got '$FUZZ_TARGET_KIND')"; exit 1 ;;
esac
ALIVE_HARNESS="$BUILD_OUT/opt_fuzz_target_alive2"
ASAN_OPT="$LLVM_BUILD_ASAN/bin/opt"

# Only require the binaries the chosen campaign actually uses.
declare -a REQUIRED=( "fuzz target:$FUZZ_TARGET" "centipede:$CENTIPEDE_BIN" )
(( RUN_MIDEND_ORACLES )) && REQUIRED+=( "alive-tv harness:$ALIVE_HARNESS" "ASAN opt:$ASAN_OPT" )
(( BACKEND_TV_ENABLE )) && REQUIRED+=( "backend-tv:${BACKEND_TV_BIN:-$ARM_TV_BUILD/backend-tv}" )

err=0
for pair in "${REQUIRED[@]}"; do
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
log "Fuzz target: $FUZZ_TARGET_KIND ($FUZZ_TARGET)   oracle set: $ORACLE_SET"

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

# Harness reads this to build the cross-corpus inline_call splicing index.
# We point it at $CORPUS_DIR (populated below from every --seeds dir) so the
# index sees the full union regardless of how many seed sources were given.
export REGATONI_CORPUS_INDEX_DIR="$CORPUS_DIR"

# Copy seeds from each --seeds source into the per-run corpus dir. cp will
# overwrite on filename collisions across sources (last source wins).
log "Copying seeds into $CORPUS_DIR..."
for src in "${SEED_SOURCES[@]}"; do
    n_before=$(find "$CORPUS_DIR" -maxdepth 1 -type f 2>/dev/null | wc -l)
    find "$src" -maxdepth 1 -type f -print0 | xargs -0 cp -t "$CORPUS_DIR/"
    n_after=$(find "$CORPUS_DIR" -maxdepth 1 -type f 2>/dev/null | wc -l)
    log "  $((n_after - n_before)) seeds from $src"
done
TOTAL_SEEDS=$(find "$CORPUS_DIR" -maxdepth 1 -type f | wc -l)
log "Total seeds in corpus: $TOTAL_SEEDS"

# ── Run summary ─────────────────────────────────────────────────────────────

CLEAN_RUN_DESC="no"
(( CLEAN_RUN )) && CLEAN_RUN_DESC="yes"

log "═══════════════════════════════════════"
log "Regatoni run: $RUN_ID"
log "Seeds: $TOTAL_SEEDS files from $SEED_SOURCE_DESC"
log "Corpus: $CORPUS_DIR/"
log "Dedup DB: $DEDUP_DB_PATH"
log "Miscompilations: $RUN_DIR/miscompilations/"
log "Clean run: $CLEAN_RUN_DESC"
log "═══════════════════════════════════════"

if (( DRY_RUN )); then
    log "Dry run: exiting before launching Centipede or oracles."
    regatoni_finalize_run_dir "$RUN_ID"
    exit 0
fi

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
ASAN_PID=""
if (( RUN_MIDEND_ORACLES )); then
    for shard in $(seq 0 $((ALIVE_SHARDS - 1))); do
        core="$(next_oracle_core)"
        log "Starting alive-tv oracle shard $shard on core $core..."
        taskset -c "$core" "$ORACLE_DIR/alive_tv.sh" "$CORPUS_DIR" "$shard" "$ALIVE_SHARDS" >> "$RUN_LOG" 2>&1 &
        pid=$!
        record_pid "alive_tv_$shard" "$pid"
        ALIVE_PIDS+=("$pid")
        ALIVE_USED_CORES+=("$core")
        log "alive-tv oracle shard $shard PID: $pid (core $core)"
    done

    asan_core="$(next_oracle_core)"
    log "Starting ASAN oracle on core $asan_core..."
    taskset -c "$asan_core" "$ORACLE_DIR/asan_opt.sh" "$CORPUS_DIR" >> "$RUN_LOG" 2>&1 &
    ASAN_PID=$!
    record_pid "asan_opt" "$ASAN_PID"
    log "ASAN oracle PID: $ASAN_PID (core $asan_core)"
else
    log "ORACLE_SET=backend_only — skipping alive-tv + ASAN (cores freed for backend-tv)"
fi

# ── Optional backend-tv oracle: BACKEND_TV_SHARDS shards × {SelectionDAG, GlobalISel}
declare -a BTV_PIDS=()
declare -a BTV_USED_CORES=()
if (( BACKEND_TV_ENABLE )); then
    # INT_ONLY gate on: the fuzzer's rare mutated tail reintroduces ptr/call/etc.
    # that would yield false "incorrect"/error verdicts. BACKEND_TV_INT_ONLY=0
    # validates everything (non-integer campaign).
    BTV_INT_ONLY="${BACKEND_TV_INT_ONLY:-1}"
    log "Starting backend-tv ($BACKEND_TV_ARCH, ${BACKEND_TV_SHARDS} shards × dagisel+gisel, int_only=$BTV_INT_ONLY)..."
    for mode in dagisel gisel; do
        gisel_env=()
        [[ "$mode" == gisel ]] && gisel_env=("BACKEND_TV_GLOBAL_ISEL=1")
        for shard in $(seq 0 $((BACKEND_TV_SHARDS - 1))); do
            core="$(next_oracle_core)"
            taskset -c "$core" env BACKEND_TV_ARCH="$BACKEND_TV_ARCH" \
                BACKEND_TV_INT_ONLY="$BTV_INT_ONLY" "${gisel_env[@]}" \
                "$ORACLE_DIR/backend_tv.sh" "$CORPUS_DIR" "$shard" "$BACKEND_TV_SHARDS" >> "$RUN_LOG" 2>&1 &
            pid=$!
            record_pid "backend_tv_${mode}_$shard" "$pid"
            BTV_PIDS+=("$pid"); BTV_USED_CORES+=("$core")
            log "  backend_tv $mode shard $shard/$BACKEND_TV_SHARDS PID: $pid (core $core)"
        done
    done
fi

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
            --include-links \
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
log "  fuzzer($FUZZ_TARGET_KIND)  PID $FUZZER_PID   cores ${ALL_CORES[*]:0:$FUZZER_CORES}"
for i in "${!ALIVE_PIDS[@]}"; do
    log "  alive_tv_$i  PID ${ALIVE_PIDS[$i]}   core  ${ALIVE_USED_CORES[$i]}"
done
[[ -n "$ASAN_PID" ]] && log "  asan_opt    PID $ASAN_PID"
if (( BACKEND_TV_ENABLE )); then
    log "  backend_tv  $BACKEND_TV_ARCH ${BACKEND_TV_SHARDS}×(dagisel+gisel)  cores ${BTV_USED_CORES[*]}"
fi
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
