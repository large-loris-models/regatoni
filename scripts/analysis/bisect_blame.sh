#!/usr/bin/env bash
# bisect_blame.sh — find the guilty pass for a miscompilation witness via
# binary search on -opt-bisect-limit, validated by alive-tv against the
# unoptimized input.
#
# Pipeline matches the alive2 harness: PassBuilder::buildPerModuleDefaultPipeline(O2)
# with default PipelineTuningOptions (default<O2>). Tools come from env.sh
# so this uses the plain (uninstrumented) opt and the project's alive-tv.
#
# Output (one TSV row to stdout):
#   <file>\t<guilty_pass>\t<bisect_index>\t<elapsed_seconds>
#   <file>\tFAIL\t<reason>\t<elapsed_seconds>
#
# Usage: bisect_blame.sh [--quiet] <ir_file>
#   --quiet  suppress the bisect-progress log on stderr (TSV row still on stdout)

set -u

QUIET=0
if [[ "${1:-}" == "--quiet" ]]; then
    QUIET=1
    shift
fi

if [[ $# -ne 1 ]]; then
    echo "usage: $0 [--quiet] <ir_file>" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# env.sh uses set -euo pipefail; isolate that from this script's set -u.
source "$SCRIPT_DIR/../build/env.sh" >/dev/null
set +e
set -u

OPT="$LLVM_BUILD_PLAIN/bin/opt"
# ALIVE_TV is exported by env.sh.
PIPELINE='default<O2>'

PER_OPT_TIMEOUT="${PER_OPT_TIMEOUT:-30}"
PER_ALIVE_TIMEOUT="${PER_ALIVE_TIMEOUT:-30}"
WHOLE_BISECT_DEADLINE="${WHOLE_BISECT_DEADLINE:-300}"

INPUT="$1"
START=$SECONDS

log() {
    (( QUIET )) || echo "$@" >&2
}

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
log "[bisect] $INPUT: MAX_N=$MAX_N"

run_opt() {
    local limit="$1" out="$2" log_path="$3"
    timeout "$PER_OPT_TIMEOUT" "$OPT" -opt-bisect-limit="$limit" \
        -passes="$PIPELINE" "$INPUT" -S -o "$out" 2>"$log_path"
}

# Returns 0 (sound) / 1 (unsound) / 2 (inconclusive).
check_sound() {
    local opt_out="$1"
    local alive_log="$WORK/alive.log"
    if ! timeout "$PER_ALIVE_TIMEOUT" "$ALIVE_TV" "$INPUT" "$opt_out" \
            >"$alive_log" 2>&1; then
        return 2
    fi
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

# Confirm hi=MAX_N is unsound and lo=0 is sound.
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

# Invariant: f(lo)=sound, f(hi)=unsound. Find smallest N in (lo, hi] with
# f(N)=unsound. Inconclusive points get pushed toward hi so the reported
# guilty index has a *definite* unsound boundary.
LO=0
HI=$MAX_N
while (( LO + 1 < HI )); do
    deadline_check
    MID=$(( (LO + HI) / 2 ))
    if ! run_opt "$MID" "$WORK/mid.ll" "$WORK/mid.log"; then
        LO=$MID
        log "[bisect] mid=$MID opt_failed lo=$LO hi=$HI"
        continue
    fi
    check_sound "$WORK/mid.ll"
    case $? in
        0) LO=$MID; log "[bisect] mid=$MID sound       lo=$LO hi=$HI" ;;
        1) HI=$MID; log "[bisect] mid=$MID unsound     lo=$LO hi=$HI" ;;
        2) LO=$MID; log "[bisect] mid=$MID inconcl     lo=$LO hi=$HI" ;;
    esac
done

GUILTY_INDEX=$HI
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
