#!/usr/bin/env bash
# Alive2 oracle: delegate to opt_fuzz_target_alive2, which runs opt -O2 +
# Alive2 compareFunctions and abort()s on real miscompilations.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# Alive2 SMT queries are slower than plain opt; bump default timeout.
ORACLE_TIMEOUT="${ORACLE_TIMEOUT:-60}"

HARNESS="$BUILD_OUT/opt_fuzz_target_alive2"
MISCOMP_DIR="$PROJECT_ROOT/miscompilations"

if [[ ! -x "$HARNESS" ]]; then
    oracle_log "ERROR: harness not found or not executable: $HARNESS"
    exit 1
fi

CORPUS="${1:-$CORPUS_DIR}"
mkdir -p "$MISCOMP_DIR"

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

    oracle_record_result "alive_tv" "$ir_file" "$verdict" "$output"

    if [[ "$verdict" == "fail" ]]; then
        cp -n "$ir_file" "$MISCOMP_DIR/$(basename "$ir_file")" 2>/dev/null || true
        # Kick off reduction in the background.
        "$SCRIPT_DIR/reduce_miscompilation.sh" "$MISCOMP_DIR/$(basename "$ir_file")"
    fi
}

oracle_watch_corpus "$CORPUS" alive_tv_check
