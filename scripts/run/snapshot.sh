#!/usr/bin/env bash
# Trigger an on-demand Centipede telemetry snapshot via SIGUSR1, then analyze
# the resulting coverage report and append summary metrics to a trend CSV.
#
# Usage:  ./scripts/run/snapshot.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null

PIDS_FILE="$BUILD_OUT/run_state/pids"
TRENDS_CSV="$BUILD_OUT/coverage_trends.csv"

log() { echo "[$(date -Is)] [snapshot] $*" >&2; }

# ── Locate fuzzer PID ───────────────────────────────────────────────────────

if [[ ! -f "$PIDS_FILE" ]]; then
    log "ERROR: $PIDS_FILE not found — is the fuzzer running? (start with scripts/run/start.sh)"
    exit 1
fi

FUZZER_PID="$(awk -F: '$1=="fuzzer"{print $2}' "$PIDS_FILE" | tail -n1)"
if [[ -z "$FUZZER_PID" ]]; then
    log "ERROR: no 'fuzzer' entry in $PIDS_FILE"
    exit 1
fi
if ! kill -0 "$FUZZER_PID" 2>/dev/null; then
    log "ERROR: fuzzer PID $FUZZER_PID is not running"
    exit 1
fi

# ── Locate current workdir ──────────────────────────────────────────────────

WORKDIR="$(ls -1dt "$BUILD_OUT"/workdir_* 2>/dev/null | head -n1)"
if [[ -z "$WORKDIR" || ! -d "$WORKDIR" ]]; then
    log "ERROR: no workdir found under $BUILD_OUT/workdir_*"
    exit 1
fi

# ── Trigger the snapshot ────────────────────────────────────────────────────

# Record the current newest snapshot report (if any) so we can detect the new
# one after the signal regardless of the exact timestamp Centipede picks.
PREV_REPORT="$(ls -1t "$WORKDIR"/coverage-report-*.snapshot_*.txt 2>/dev/null | head -n1 || true)"

log "Sending SIGUSR1 to fuzzer PID $FUZZER_PID"
kill -USR1 "$FUZZER_PID"

# ── Wait for the snapshot file ──────────────────────────────────────────────
#
# Centipede only dumps telemetry between batches, so the new report can take
# a few seconds to appear depending on batch_size / exec speed. We poll for up
# to ~60s.

NEW_REPORT=""
for _ in $(seq 1 60); do
    sleep 1
    CANDIDATE="$(ls -1t "$WORKDIR"/coverage-report-*.snapshot_*.txt 2>/dev/null | head -n1 || true)"
    if [[ -n "$CANDIDATE" && "$CANDIDATE" != "$PREV_REPORT" ]]; then
        NEW_REPORT="$CANDIDATE"
        break
    fi
done

if [[ -z "$NEW_REPORT" ]]; then
    log "ERROR: no new snapshot report appeared within 60s (workdir=$WORKDIR)"
    exit 1
fi

log "New snapshot: $NEW_REPORT"

# ── Analyze with pass_coverage.py ───────────────────────────────────────────

ANALYSIS_OUT="$(mktemp)"
trap 'rm -f "$ANALYSIS_OUT"' EXIT

if ! "$SCRIPT_DIR/../analysis/pass_coverage.py" --summary "$NEW_REPORT" >"$ANALYSIS_OUT" 2>&1; then
    log "ERROR: pass_coverage.py failed"
    cat "$ANALYSIS_OUT" >&2
    exit 1
fi

cat "$ANALYSIS_OUT"

# ── Append to trend CSV ─────────────────────────────────────────────────────
#
# Columns:
#   timestamp,report,total_covered,total_functions,total_pct,
#     o2_touched_pct,o2_full_pct,o2_zero_cov,
#     analysis_touched_pct,sanitizers_touched_pct,
#     codegen_touched_pct,utilities_touched_pct,unknown_touched_pct

# Parse "Total functions: N/M (P%)" from the analysis.
TOTAL_LINE="$(grep -E '^Total functions:' "$ANALYSIS_OUT" || true)"
COVERED="$(sed -nE 's#^Total functions: ([0-9]+)/[0-9]+.*#\1#p' <<<"$TOTAL_LINE")"
FUNCS="$(sed -nE 's#^Total functions: [0-9]+/([0-9]+).*#\1#p' <<<"$TOTAL_LINE")"
PCT="$(sed -nE 's#^Total functions: [0-9]+/[0-9]+ \(([0-9.]+)%\).*#\1#p' <<<"$TOTAL_LINE")"

# Extract a bucket's touched% / full% from the summary table.
#   $1 = bucket label as shown in the summary (e.g. "O2 Pipeline")
#   prints: touched_pct full_pct zero_coverage
bucket_row() {
    awk -v name="$1" '
        $0 ~ "^" name {
            # Line format: "<name padded>  Passes  Touched%  Full%  Zero"
            # Touched% and Full% end with "%"; strip them. Zero-cov is an int.
            n = NF
            zero = $n
            full = $(n-1); gsub("%", "", full)
            touched = $(n-2); gsub("%", "", touched)
            printf "%s %s %s\n", touched, full, zero
            exit
        }
    ' "$ANALYSIS_OUT"
}

read -r O2_T O2_F O2_Z          <<<"$(bucket_row 'O2 Pipeline')"
read -r AN_T AN_F AN_Z          <<<"$(bucket_row 'Analysis')"
read -r SAN_T SAN_F SAN_Z       <<<"$(bucket_row 'Sanitizers/Instrumentation')"
read -r CG_T CG_F CG_Z          <<<"$(bucket_row 'Codegen/Backend')"
read -r UT_T UT_F UT_Z          <<<"$(bucket_row 'Utilities')"
read -r UN_T UN_F UN_Z          <<<"$(bucket_row 'Unknown')"

mkdir -p "$(dirname "$TRENDS_CSV")"
if [[ ! -f "$TRENDS_CSV" ]]; then
    echo "timestamp,report,total_covered,total_functions,total_pct,o2_touched_pct,o2_full_pct,o2_zero,analysis_touched_pct,sanitizers_touched_pct,codegen_touched_pct,utilities_touched_pct,unknown_touched_pct" > "$TRENDS_CSV"
fi

NOW="$(date -Is)"
printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "$NOW" "$(basename "$NEW_REPORT")" \
    "${COVERED:-}" "${FUNCS:-}" "${PCT:-}" \
    "${O2_T:-}" "${O2_F:-}" "${O2_Z:-}" \
    "${AN_T:-}" "${SAN_T:-}" "${CG_T:-}" "${UT_T:-}" "${UN_T:-}" \
    >> "$TRENDS_CSV"

log "Appended trend row to $TRENDS_CSV"
