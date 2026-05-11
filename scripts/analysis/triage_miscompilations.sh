#!/usr/bin/env bash
# Automated miscompilation triage:
#   - normalize *.reduced.ll witnesses under $RUN_DIR/miscompilations/
#   - backfill the dedup database (idempotent)
#   - for each bucket with untriaged findings, invoke the LLM agent on all
#     findings in that bucket (sample if > MAX_PER_BUCKET; second-pass match
#     the remainder); persist a sub-clustering of the bucket into
#     dedup.db.sub_clusters
#   - regenerate triage/report.md from sub-clusters (not from buckets)
#
# Per-bucket gating: a bucket needs triage iff it has at least one finding
# with no row in finding_sub_cluster. Failure on one bucket leaves its
# findings untriaged and a later run retries them; other buckets advance.
#
# Usage:
#   triage_miscompilations.sh [--dry-run] [--force] [--backend NAME]
#     --force  re-triage every bucket from scratch (overwrites prior
#              sub-clusters via record-sub-clusters --replace)
#
# NOTE: the agent runs with --dangerously-skip-permissions / -bypass flags.
# TODO: replace with a sandboxed mode once the necessary tools are scoped.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null
source "$SCRIPT_DIR/../run/run_helpers.sh"

# Resolve the active run dir. Inherited from the oracle that triggered us if
# launched in-pipeline; otherwise read runs/current.
if [[ -z "${RUN_DIR:-}" ]]; then
    if ! RUN_DIR="$(regatoni_run_dir 2>/dev/null)"; then
        echo "[triage] ERROR: RUN_DIR unset and runs/current is missing" >&2
        exit 1
    fi
    export RUN_DIR
fi
if [[ -z "${RUN_ID:-}" ]]; then
    RUN_ID="$(basename "$RUN_DIR")"
    export RUN_ID
fi

DRY_RUN=0
FORCE=0
BACKEND_ARG=""
while (( $# > 0 )); do
    case "$1" in
        --dry-run) DRY_RUN=1 ;;
        --force) FORCE=1 ;;
        --backend) BACKEND_ARG="${2:-}"; shift ;;
        --backend=*) BACKEND_ARG="${1#--backend=}" ;;
        -h|--help)
            sed -n '2,18p' "$0"
            exit 0
            ;;
        *)
            echo "unknown arg: $1" >&2
            exit 2
            ;;
    esac
    shift
done

NORMALIZE="$SCRIPT_DIR/normalize_ir.py"
MISC_DIR="$RUN_DIR/miscompilations"
TRIAGE_DIR="$RUN_DIR/triage"
REPORT="$TRIAGE_DIR/report.md"
LAST_RUN="$TRIAGE_DIR/last_run.json"
TRIAGE_LOG="$TRIAGE_DIR/triage.log"
DEDUP_PY="$SCRIPT_DIR/dedup.py"
DEDUP_LOG="$RUN_DIR/dedup.log"
ORACLE_RESULTS_DIR="$RUN_DIR/oracle_results"
ORCHESTRATOR="$SCRIPT_DIR/triage_buckets.py"
MAX_PER_BUCKET="${TRIAGE_MAX_PER_BUCKET:-15}"
DEDUP_DB="$PROJECT_ROOT/dedup.db"

mkdir -p "$TRIAGE_DIR"

LOCK_FILE="$TRIAGE_DIR/.triage.lock"
exec 8>"$LOCK_FILE"
if ! flock -n 8; then
    echo "[triage] another triage is already running, skipping" >&2
    exit 0
fi

if [[ ! -d "$MISC_DIR" ]]; then
    echo "error: miscompilations dir not found: $MISC_DIR" >&2
    exit 1
fi

# Normalize new *.reduced.ll witnesses.
shopt -s nullglob
for reduced in "$MISC_DIR"/*.reduced.ll; do
    norm="${reduced%.ll}.normalized.ll"
    if [[ ! -f "$norm" || "$reduced" -nt "$norm" ]]; then
        if ! python3 "$NORMALIZE" "$reduced" "$norm" >/dev/null 2>&1; then
            cp "$reduced" "$norm"
            echo "[triage] normalize failed for $(basename "$reduced"); using raw" >&2
        fi
    fi
done
shopt -u nullglob

# Backfill any unregistered findings (idempotent). Scoped to this run's
# miscompilations dir and tagged with this run_id.
echo "[triage] backfilling dedup database (run_id=$RUN_ID)..." >&2
if ! python3 "$DEDUP_PY" migrate \
        --misc-dir "$MISC_DIR" \
        --oracle-results "$ORACLE_RESULTS_DIR" \
        --run-id "$RUN_ID" \
        >>"$DEDUP_LOG" 2>&1; then
    echo "[triage] WARN: dedup migrate failed, see $DEDUP_LOG" >&2
fi

# Pick backend.
BACKEND="${TRIAGE_BACKEND:-$BACKEND_ARG}"
if [[ -z "$BACKEND" ]]; then
    if command -v claude >/dev/null 2>&1; then BACKEND="claude"
    elif command -v codex >/dev/null 2>&1; then BACKEND="codex"
    else
        echo "error: no LLM CLI found; install 'claude' or 'codex'" >&2
        exit 2
    fi
fi
case "$BACKEND" in
    claude|codex) ;;
    *) echo "error: unknown backend '$BACKEND'" >&2; exit 2 ;;
esac
if ! BACKEND_BIN="$(command -v "$BACKEND")"; then
    echo "error: '$BACKEND' CLI not found on PATH" >&2
    exit 2
fi

# Compute the list of buckets that need work, for logging / dry-run.
if (( FORCE == 1 )); then
    mapfile -t BUCKETS < <(python3 -c "
import sqlite3, os
c = sqlite3.connect(os.path.join('$PROJECT_ROOT', 'dedup.db'))
for r in c.execute('SELECT bucket_id FROM buckets ORDER BY bucket_id'):
    print(r[0])
")
else
    mapfile -t BUCKETS < <(python3 -c "
import sqlite3, os
c = sqlite3.connect(os.path.join('$PROJECT_ROOT', 'dedup.db'))
sql = '''SELECT DISTINCT f.bucket_id FROM findings f
         LEFT JOIN finding_sub_cluster fsc ON fsc.finding_id = f.finding_id
         WHERE fsc.sub_cluster_id IS NULL
         ORDER BY f.bucket_id'''
for r in c.execute(sql):
    print(r[0])
")
fi

if (( ${#BUCKETS[@]} == 0 )); then
    echo "[triage] no buckets need triage" >&2
    if [[ ! -f "$REPORT" ]]; then
        python3 "$SCRIPT_DIR/render_triage_report.py" --output "$REPORT"
        echo "[triage] wrote $REPORT" >&2
    fi
    exit 0
fi

if (( DRY_RUN == 1 )); then
    echo "=== DRY RUN ===" >&2
    echo "backend: $BACKEND" >&2
    echo "force: $FORCE" >&2
    echo "max_per_bucket: $MAX_PER_BUCKET" >&2
    echo "buckets needing triage: ${#BUCKETS[@]} -> ${BUCKETS[*]}" >&2
    exit 0
fi

echo "[triage] backend=$BACKEND buckets=${#BUCKETS[@]} max_per_bucket=$MAX_PER_BUCKET" >&2

RUN_TS="$(date -Is)"

ORCH_ARGS=(--backend "$BACKEND" --backend-bin "$BACKEND_BIN"
           --log "$TRIAGE_LOG" --max-per-bucket "$MAX_PER_BUCKET")
if (( FORCE == 1 )); then
    ORCH_ARGS+=(--force)
fi

# Orchestrator returns non-zero only if at least one bucket failed; we still
# want to regenerate the report from whatever did succeed.
set +e
python3 "$ORCHESTRATOR" "${ORCH_ARGS[@]}"
ORCH_RC=$?
set -e

# Regenerate report.md from sub_clusters.
python3 "$SCRIPT_DIR/render_triage_report.py" --output "$REPORT"
echo "[triage] wrote $REPORT" >&2

# last_run.json: timestamp + counts.
python3 - "$RUN_TS" "$LAST_RUN" "$ORCH_RC" <<'PYEOF'
import json, sqlite3, sys, os
run_ts, last_run_path, rc = sys.argv[1], sys.argv[2], int(sys.argv[3])
db = os.path.join(os.environ["PROJECT_ROOT"], "dedup.db")
c = sqlite3.connect(db)
n_findings = c.execute("SELECT COUNT(*) FROM findings").fetchone()[0]
n_buckets = c.execute("SELECT COUNT(*) FROM buckets").fetchone()[0]
n_sc = c.execute("SELECT COUNT(*) FROM sub_clusters").fetchone()[0]
n_untriaged = c.execute(
    "SELECT COUNT(*) FROM findings f LEFT JOIN finding_sub_cluster fsc "
    "ON fsc.finding_id = f.finding_id WHERE fsc.sub_cluster_id IS NULL"
).fetchone()[0]
with open(last_run_path, "w") as f:
    json.dump({
        "timestamp": run_ts,
        "orchestrator_exit": rc,
        "findings_total": n_findings,
        "buckets_total": n_buckets,
        "sub_clusters_total": n_sc,
        "findings_untriaged": n_untriaged,
    }, f, indent=2)
PYEOF
echo "[triage] wrote $LAST_RUN" >&2

if (( ORCH_RC != 0 )); then
    echo "[triage] orchestrator reported failures (exit $ORCH_RC); see $TRIAGE_LOG" >&2
fi
exit "$ORCH_RC"
