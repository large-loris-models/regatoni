#!/usr/bin/env bash
# ASAN oracle: run ASAN-instrumented opt -O2 on each corpus entry.
# Sanitizer findings => fail (new bugs). Other crashes => error.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

ASAN_OPT="$LLVM_BUILD_ASAN/bin/opt"

if [[ ! -x "$ASAN_OPT" ]]; then
    oracle_log "ERROR: ASAN opt not found: $ASAN_OPT"
    exit 1
fi

CORPUS="${1:-$CORPUS_DIR}"

oracle_init "asan_opt"

DEDUP_LOG="$PROJECT_ROOT/miscompilations/dedup.log"
DEDUP_PY="$SCRIPT_DIR/../analysis/dedup.py"

asan_check() {
    local ir_file="$1"
    local output rc

    output="$(timeout "$ORACLE_TIMEOUT" "$ASAN_OPT" -O2 -S "$ir_file" -o /dev/null 2>&1)"
    rc=$?

    local verdict
    if (( rc == 0 )); then
        verdict="pass"
    elif (( rc == 124 )); then
        verdict="timeout"
    elif grep -qE 'AddressSanitizer|UndefinedBehavior' <<< "$output"; then
        verdict="fail"
    else
        verdict="error"
    fi

    oracle_record_result "asan_opt" "$ir_file" "$verdict" "$output"

    if [[ "$verdict" == "fail" ]]; then
        # ASAN findings have no reduction/normalization in v1; register the
        # raw IR. dedup.py skips bisect for oracle=asan_opt and parks the
        # finding in the (NULL, NULL) bucket per the decision doc.
        local err_tmp
        err_tmp="$(mktemp)"
        printf '%s' "$output" > "$err_tmp"
        python3 "$DEDUP_PY" register \
            --reduced "$ir_file" \
            --oracle asan_opt \
            --error-text-file "$err_tmp" \
            --original-path "$ir_file" \
            >> "$DEDUP_LOG" 2>&1 || true
        rm -f "$err_tmp"
    fi
}

oracle_watch_corpus "$CORPUS" asan_check
