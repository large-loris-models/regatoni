#!/usr/bin/env bash
# bisect_blame.sh — feasibility experiment for opt-bisect-limit dedup.
#
# Binary-searches over -opt-bisect-limit on the same pipeline alive2 harness
# uses (PassBuilder::buildPerModuleDefaultPipeline(O2), no custom PTO; see
# deps/alive2/llvm_util/llvm_optimizer.cpp). Finds the smallest N such that
# alive-tv reports the limit-N opt output as unsound vs the input.
#
# Outputs one TSV row to stdout:
#   <file>\t<guilty_pass>\t<bisect_index>\t<elapsed_seconds>
#   <file>\tFAIL\t<reason>\t<elapsed_seconds>

set -u

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OPT="${REPO_ROOT}/deps/llvm-build-plain/bin/opt"
ALIVE_TV="${REPO_ROOT}/deps/alive2/build/alive-tv"
PIPELINE='default<O2>'

PER_OPT_TIMEOUT=30
PER_ALIVE_TIMEOUT=30
WHOLE_BISECT_DEADLINE=300   # 5 min

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <ir_file>" >&2
    exit 2
fi

INPUT="$1"
START=$SECONDS

emit_fail() {
    local reason="$1"
    local elapsed=$(( SECONDS - START ))
    printf '%s\tFAIL\t%s\t%d\n' "$INPUT" "$reason" "$elapsed"
    exit 0
}

deadline_check() {
    if (( SECONDS - START > WHOLE_BISECT_DEADLINE )); then
        emit_fail "wall_deadline"
    fi
}

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Step 1: discover total pass count by running with a huge limit and counting
# "BISECT: running pass" lines.
if ! timeout "$PER_OPT_TIMEOUT" "$OPT" -opt-bisect-limit=1000000 \
        -passes="$PIPELINE" "$INPUT" -S -o /dev/null 2>"$WORK/full.log"; then
    emit_fail "opt_full_failed"
fi
MAX_N=$(grep -c '^BISECT: running pass' "$WORK/full.log" || true)
if [[ -z "$MAX_N" || "$MAX_N" -lt 1 ]]; then
    emit_fail "no_passes_in_pipeline"
fi

# Sanity: confirm the file actually miscompiles under the full pipeline.
run_opt() {
    local limit="$1" out="$2" log="$3"
    timeout "$PER_OPT_TIMEOUT" "$OPT" -opt-bisect-limit="$limit" \
        -passes="$PIPELINE" "$INPUT" -S -o "$out" 2>"$log"
}

# Returns 0 (sound) / 1 (unsound) / 2 (inconclusive).
check_sound() {
    local opt_out="$1"
    local alive_log
    alive_log="$WORK/alive.log"
    if ! timeout "$PER_ALIVE_TIMEOUT" "$ALIVE_TV" "$INPUT" "$opt_out" \
            >"$alive_log" 2>&1; then
        # Treat alive-tv timeout/crash as inconclusive.
        return 2
    fi
    # Parse the trailing summary block.
    local incorrect
    incorrect=$(awk '/incorrect transformations/ {print $1}' "$alive_log" \
                | tail -1)
    if [[ -z "$incorrect" ]]; then
        return 2
    fi
    if (( incorrect > 0 )); then
        return 1
    fi
    return 0
}

# Confirm hi=MAX_N is unsound and lo=0 is sound. Bail out otherwise.
if ! run_opt "$MAX_N" "$WORK/hi.ll" "$WORK/hi.log"; then
    emit_fail "opt_hi_failed"
fi
check_sound "$WORK/hi.ll"
hi_status=$?
if (( hi_status == 0 )); then
    emit_fail "full_pipeline_sound"
elif (( hi_status == 2 )); then
    emit_fail "full_pipeline_inconclusive"
fi

if ! run_opt 0 "$WORK/lo.ll" "$WORK/lo.log"; then
    emit_fail "opt_lo_failed"
fi
check_sound "$WORK/lo.ll"
lo_status=$?
if (( lo_status == 1 )); then
    emit_fail "limit0_already_unsound"
fi
# lo_status == 2 is allowed; we keep lo=0 anyway (no passes run).

# Invariant: f(lo)=sound, f(hi)=unsound. Find smallest N in (lo, hi] with
# f(N)=unsound. Inconclusive points are treated as sound (push toward hi)
# so that the result we report has a *definite* unsound boundary.
LO=0
HI=$MAX_N
ITERATIONS=0
while (( LO + 1 < HI )); do
    deadline_check
    ITERATIONS=$(( ITERATIONS + 1 ))
    MID=$(( (LO + HI) / 2 ))
    if ! run_opt "$MID" "$WORK/mid.ll" "$WORK/mid.log"; then
        # opt itself failed at this limit — treat as inconclusive, push to hi.
        LO=$MID
        continue
    fi
    check_sound "$WORK/mid.ll"
    case $? in
        0) LO=$MID ;;       # sound → guilty index is > MID
        1) HI=$MID ;;       # unsound → guilty index is ≤ MID
        2) LO=$MID ;;       # inconclusive → conservative push
    esac
done

GUILTY_INDEX=$HI
# Re-run hi to extract the pass name at that index from BISECT log.
if ! run_opt "$GUILTY_INDEX" "$WORK/g.ll" "$WORK/g.log"; then
    emit_fail "opt_at_guilty_failed"
fi
PASS_NAME=$(awk -v idx="$GUILTY_INDEX" '
    match($0, /^BISECT: running pass \(([0-9]+)\) ([^ ]+)/, a) {
        if (a[1]+0 == idx) { print a[2]; exit }
    }
' "$WORK/g.log")

if [[ -z "$PASS_NAME" ]]; then
    emit_fail "pass_name_unparseable"
fi

ELAPSED=$(( SECONDS - START ))
printf '%s\t%s\t%d\t%d\n' "$INPUT" "$PASS_NAME" "$GUILTY_INDEX" "$ELAPSED"
