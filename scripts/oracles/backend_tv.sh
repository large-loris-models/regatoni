#!/usr/bin/env bash
# Backend translation-validation oracle.
#
# Delegates to the arm-tv build's `backend-tv`, which lowers each corpus
# function to machine code, lifts it back to IR, and checks refinement. This
# validates the LLVM *backend* (codegen) — a different stage than alive_tv.sh,
# which validates `opt -O2` (the target-independent IR optimizer).
#
# Pick the target backend with BACKEND_TV_ARCH=aarch64 (default) or riscv64;
# run two instances for both. Unlike the alive_tv harness (which abort()s on a
# miscompilation, signalling via exit code), backend-tv reports its verdict in
# a stdout Summary and exits 0 regardless, so we classify by parsing it.
#
# NOTE: backend-tv is much slower than alive_tv — its full machine-state SMT
# query times out even on trivial functions at the default budget — so most
# inputs land in `timeout`. It is best run opt-in / on a reduced corpus rather
# than as an always-on, high-throughput oracle.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

BACKEND_TV_ARCH="${BACKEND_TV_ARCH:-aarch64}"     # aarch64 | riscv64
SMT_TO_MS="${BACKEND_TV_SMT_TO_MS:-10000}"        # per-SMT-query budget (ms)
# Per-input wall clock. NOTE: we deliberately do NOT use the global ORACLE_TIMEOUT
# here — regatoni.conf pins that to 4s, which suits the sub-second alive-tv oracle
# but is far too tight for SMT-heavy backend TV. Use a dedicated knob (default 30s,
# comfortably above SMT_TO so the SMT budget is what actually bounds a query).
BACKEND_TV_TIMEOUT="${BACKEND_TV_TIMEOUT:-30}"

# Always disable UNDEF inputs: undef can read differently at every use (not even
# physically realizable in a register), it's being removed from LLVM, and it is
# the sole cause of the asm-refinement query going quantified — Z3 then times out
# even on a trivial `add`. Disabling undef alone makes it tractable (instant);
# measured: `--disable-undef-input` => 0s, `--disable-poison-input` alone => timeout.
#
# POISON inputs are KEPT: they're free (undef-off already makes it tractable) and
# poison is real, load-bearing LLVM semantics (nsw/nuw/inbounds/...) worth
# validating in the backend. Disabling poison would be a soundness-narrowing
# assumption with no perf benefit. Set BACKEND_TV_DISABLE_POISON=1 to drop it too
# (marginally faster, narrower coverage) if you ever need to.
INPUT_FLAGS=(--disable-undef-input)
[[ -n "${BACKEND_TV_DISABLE_POISON:-}" ]] && INPUT_FLAGS+=(--disable-poison-input)

# isel mode: backend-tv defaults to SelectionDAG isel. Set BACKEND_TV_GLOBAL_ISEL=1
# to validate the GlobalISel path instead (a different selector that lowers a lot
# of code differently — fertile ground for isel-rule bugs). Results go to a
# separate oracle dir (..._gisel) so the two modes never collide.
ISEL_TAG="dagisel"
if [[ -n "${BACKEND_TV_GLOBAL_ISEL:-}" ]]; then
    INPUT_FLAGS+=(--global-isel)
    ISEL_TAG="gisel"
fi

HARNESS="${BACKEND_TV_BIN:-$ARM_TV_BUILD/backend-tv}"

if [[ ! -x "$HARNESS" ]]; then
    oracle_log "ERROR: backend-tv not found or not executable: $HARNESS"
    exit 1
fi
case "$BACKEND_TV_ARCH" in
    aarch64|riscv64) ;;
    *) oracle_log "ERROR: BACKEND_TV_ARCH must be aarch64 or riscv64 (got '$BACKEND_TV_ARCH')"; exit 1 ;;
esac

CORPUS="${1:-$CORPUS_DIR}"
ORACLE_SHARD_ID="${2:-0}"
ORACLE_TOTAL_SHARDS="${3:-1}"

oracle_init "backend_tv_${BACKEND_TV_ARCH}_${ISEL_TAG}"
oracle_log "backend=$BACKEND_TV_ARCH isel=$ISEL_TAG bin=$HARNESS smt_to=${SMT_TO_MS}ms wall=${BACKEND_TV_TIMEOUT}s int_only=${BACKEND_TV_INT_ONLY}"

# Extract the integer count for a given "<n> <label> transformations|errors"
# line from backend-tv's Summary block. Empty if absent.
_bt_count() {
    grep -oE "[0-9]+ $1" <<<"$2" | grep -oE '^[0-9]+' | tail -1
}

# Integer-purity gate. The seed corpus is pre-filtered to clean scalar-integer
# functions; the fuzzer's mutator is ~99.9% integer-preserving on integer seeds
# (measured), but the rare tail reintroduces ptr/call/etc. that would emit false
# "incorrect" verdicts or errors. With BACKEND_TV_INT_ONLY=1 we skip any input
# that isn't clean scalar-integer BEFORE the expensive backend-tv call.
#
# The check is delegated to clean_int_check.py (Python re). Do NOT inline it with
# `grep`: the system grep is `ugrep`, whose \b/ERE matched `call` inside value
# names like `%call` and falsely flagged ~84% of clean functions.
BACKEND_TV_INT_ONLY="${BACKEND_TV_INT_ONLY:-0}"
_CLEAN_INT_CHECK="$SCRIPT_DIR/clean_int_check.py"

_btv_is_clean_int() {
    python3 "$_CLEAN_INT_CHECK" "$1"
}

backend_tv_check() {
    local ir_file="$1"
    local output rc verdict
    local incorrect correct failed errors

    if [[ "$BACKEND_TV_INT_ONLY" == 1 ]] && ! _btv_is_clean_int "$ir_file"; then
        return 0    # not clean scalar-integer: skip (caller still marks it checked)
    fi

    output="$(timeout "$BACKEND_TV_TIMEOUT" "$HARNESS" \
        --backend="$BACKEND_TV_ARCH" --smt-to="$SMT_TO_MS" \
        "${INPUT_FLAGS[@]}" "$ir_file" 2>&1)"
    rc=$?

    incorrect="$(_bt_count 'incorrect transformations'      "$output")"
    correct="$(_bt_count 'correct transformations'          "$output")"
    failed="$(_bt_count 'failed-to-prove transformations'   "$output")"
    errors="$(_bt_count 'Alive2 errors'                     "$output")"

    if (( rc == 124 )); then
        verdict="timeout"                                  # killed by wall-clock
    elif [[ "${incorrect:-0}" -gt 0 ]]; then
        verdict="fail"                                     # confirmed backend miscompilation
    elif [[ "${errors:-0}" -gt 0 ]]; then
        verdict="error"
    elif [[ "${correct:-0}" -gt 0 && "${incorrect:-0}" -eq 0 && "${failed:-0}" -eq 0 ]]; then
        verdict="pass"
    elif [[ "${failed:-0}" -gt 0 ]]; then
        verdict="timeout"                                  # couldn't prove (usually SMT timeout)
    else
        verdict="error"                                    # crash / lift failure / no Summary
    fi

    oracle_record_result "$ORACLE_NAME" "$ir_file" "$verdict" "$output"
}

oracle_watch_corpus "$CORPUS" backend_tv_check
