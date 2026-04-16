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
}

oracle_watch_corpus "$CORPUS" asan_check
