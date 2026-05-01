#!/usr/bin/env bash
# Stop the fuzzing pipeline started by start.sh.
#
# Reads runs/current to find the active run, kills its PIDs, and stamps
# end_time into the manifest. The run directory is preserved as the artifact.
#
# Usage: ./scripts/run/stop.sh [RUN_ID]

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null
source "$SCRIPT_DIR/run_helpers.sh"

log() { echo "[$(date -Is)] [stop] $*" >&2; }

# Resolve which run to stop. Default: whatever runs/current points at.
if (( $# > 0 )); then
    RUN_ID="$1"
elif [[ -L "$PROJECT_ROOT/runs/current" ]]; then
    RUN_ID="$(readlink "$PROJECT_ROOT/runs/current")"
else
    log "No active run (runs/current missing) — nothing to stop."
    exit 0
fi
export RUN_ID

RUN_DIR="$(regatoni_run_dir "$RUN_ID")"
PIDS_FILE="$RUN_DIR/pids"

if [[ ! -f "$PIDS_FILE" ]]; then
    log "No pids file at $PIDS_FILE — nothing to stop."
    exit 0
fi

START_TIME="$(stat -c %Y "$PIDS_FILE" 2>/dev/null || date +%s)"

# ── Send SIGTERM to all listed processes ────────────────────────────────────

declare -a LIVE_PIDS=()

while IFS=: read -r name pid; do
    [[ -n "$pid" ]] || continue
    if kill -0 "$pid" 2>/dev/null; then
        log "Sending SIGTERM to $name (PID $pid)"
        kill -TERM "$pid" 2>/dev/null || true
        LIVE_PIDS+=("$name:$pid")
    else
        log "$name (PID $pid) already exited"
    fi
done < "$PIDS_FILE"

# ── Wait for processes to exit (up to 10s), then SIGKILL ────────────────────

if (( ${#LIVE_PIDS[@]} > 0 )); then
    deadline=$(( $(date +%s) + 10 ))
    for entry in "${LIVE_PIDS[@]}"; do
        name="${entry%%:*}"
        pid="${entry#*:}"
        while kill -0 "$pid" 2>/dev/null && (( $(date +%s) < deadline )); do
            sleep 0.5
        done
        if kill -0 "$pid" 2>/dev/null; then
            log "Force-killing $name (PID $pid)"
            kill -KILL "$pid" 2>/dev/null || true
        else
            log "$name (PID $pid) exited"
        fi
    done
fi

# ── Summary ─────────────────────────────────────────────────────────────────

now="$(date +%s)"
elapsed=$(( now - START_TIME ))
h=$(( elapsed / 3600 ))
m=$(( (elapsed % 3600) / 60 ))
s=$(( elapsed % 60 ))

corpus_count=0
if [[ -d "$RUN_DIR/corpus" ]]; then
    corpus_count="$(find "$RUN_DIR/corpus" -maxdepth 1 -type f 2>/dev/null | wc -l)"
fi

crash_count=0
if [[ -d "$RUN_DIR/workdir" ]]; then
    crash_count="$(find "$RUN_DIR/workdir" -maxdepth 2 -type f -path '*/crashes*/*' 2>/dev/null | wc -l)"
fi

miscomp_count=0
if [[ -d "$RUN_DIR/miscompilations" ]]; then
    miscomp_count="$(find "$RUN_DIR/miscompilations" -maxdepth 1 -type f 2>/dev/null | wc -l)"
fi

log "────────────────────────────────────────"
log "RUN_ID:             $RUN_ID"
log "Runtime:            ${h}h ${m}m ${s}s"
log "Corpus entries:     $corpus_count"
log "Crashes found:      $crash_count"
log "Miscompilations:    $miscomp_count"
log "────────────────────────────────────────"

# Stamp end_time and clear runs/current. Run dir itself is preserved.
regatoni_finalize_run_dir "$RUN_ID"
rm -f "$PIDS_FILE"
log "Stopped run $RUN_ID."
