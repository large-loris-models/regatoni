#!/usr/bin/env bash
# Alive2 oracle: delegate to opt_fuzz_target_alive2, which runs opt -O2 +
# Alive2 compareFunctions and abort()s on real miscompilations.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# Most valid IR processes in under 1 second; longer runs are timeouts or
# extremely complex queries that aren't worth blocking the oracle for.
ORACLE_TIMEOUT="${ORACLE_TIMEOUT:-4}"

HARNESS="$BUILD_OUT/opt_fuzz_target_alive2"
MISCOMP_DIR="$PROJECT_ROOT/miscompilations"
TRIAGE_DIR="$PROJECT_ROOT/triage"
TRIAGE_COUNT_FILE="$TRIAGE_DIR/.miscomp_count"
TRIAGE_LOG="$TRIAGE_DIR/triage.log"
TRIAGE_SCRIPT="$SCRIPT_DIR/../analysis/triage_miscompilations.sh"
TRIAGE_EVERY="${TRIAGE_EVERY:-20}"

if [[ ! -x "$HARNESS" ]]; then
    oracle_log "ERROR: harness not found or not executable: $HARNESS"
    exit 1
fi

CORPUS="${1:-$CORPUS_DIR}"
ORACLE_SHARD_ID="${2:-0}"
ORACLE_TOTAL_SHARDS="${3:-1}"
mkdir -p "$MISCOMP_DIR" "$TRIAGE_DIR"

oracle_init "alive_tv"

alive_tv_check() {
    local ir_file="$1"
    local output rc

    output="$(timeout "$ORACLE_TIMEOUT" "$HARNESS" "$ir_file" 2>&1)"
    rc=$?

    local verdict
    case "$rc" in
        0)   verdict="pass" ;;
        124) verdict="timeout" ;;
        134) verdict="fail" ;;
        *)   verdict="error" ;;
    esac

    oracle_record_result "$ORACLE_NAME" "$ir_file" "$verdict" "$output"

    if [[ "$verdict" == "fail" ]]; then
        cp -n "$ir_file" "$MISCOMP_DIR/$(basename "$ir_file")" 2>/dev/null || true
        # Kick off reduction in the background.
        "$SCRIPT_DIR/reduce_miscompilation.sh" "$MISCOMP_DIR/$(basename "$ir_file")"

        # Increment the miscomp counter and trigger triage every $TRIAGE_EVERY
        # findings. flock keeps this safe across sharded oracle workers.
        (
            flock -x 9
            local count=0
            [[ -f "$TRIAGE_COUNT_FILE" ]] && count=$(<"$TRIAGE_COUNT_FILE")
            [[ "$count" =~ ^[0-9]+$ ]] || count=0
            count=$((count + 1))
            if (( count >= TRIAGE_EVERY )); then
                printf '0' > "$TRIAGE_COUNT_FILE"
                oracle_log "triggering triage after $count miscompilations"
                nohup "$TRIAGE_SCRIPT" >>"$TRIAGE_LOG" 2>&1 </dev/null &
                disown 2>/dev/null || true
            else
                printf '%d' "$count" > "$TRIAGE_COUNT_FILE"
            fi
        ) 9>"$TRIAGE_COUNT_FILE.lock"
    fi
}

oracle_watch_corpus "$CORPUS" alive_tv_check
