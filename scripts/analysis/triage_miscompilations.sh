#!/usr/bin/env bash
# Automated miscompilation triage:
#   - collect *.reduced.ll witnesses from $PROJECT_ROOT/miscompilations/
#   - match each to its alive-tv log under build/oracle_results/alive_tv*/fail/
#   - normalize via scripts/analysis/normalize_ir.py
#   - diff against triage/triaged.manifest to find new findings; if there
#     are none, exit early
#   - stage findings and a tool-path-substituted TRIAGE_PLAYBOOK.md into a
#     temp working directory; in incremental mode the prior report is also
#     copied in as PREVIOUS_REPORT.md and only new findings are staged
#   - launch a Claude Code session in that directory; its stdout becomes
#     triage/report.md
#   - update triage/triaged.manifest and triage/last_run.json
#
# Usage:
#   triage_miscompilations.sh [--dry-run] [--force]
#     --force  ignore the manifest, re-triage every finding from scratch

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null

DRY_RUN=0
FORCE=0
for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY_RUN=1 ;;
        --force) FORCE=1 ;;
        -h|--help)
            sed -n '2,16p' "$0"
            exit 0
            ;;
        *)
            echo "unknown arg: $arg" >&2
            exit 2
            ;;
    esac
done

NORMALIZE="$SCRIPT_DIR/normalize_ir.py"
MISC_DIR="$PROJECT_ROOT/miscompilations"
ORACLE_ROOT="$BUILD_OUT/oracle_results"
TRIAGE_DIR="$PROJECT_ROOT/triage"
PLAYBOOK_SRC="$SCRIPT_DIR/TRIAGE_PLAYBOOK.md"
REPORT="$TRIAGE_DIR/report.md"
LAST_RUN="$TRIAGE_DIR/last_run.json"
TRIAGED_MANIFEST="$TRIAGE_DIR/triaged.manifest"
TRIAGE_LOG="$TRIAGE_DIR/triage.log"

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

if [[ ! -f "$PLAYBOOK_SRC" ]]; then
    echo "error: playbook not found: $PLAYBOOK_SRC" >&2
    exit 1
fi

# Build the index of alive-tv fail logs once: log_basename → full path.
# Multiple alive_tv shards (alive_tv_0, alive_tv_1, ...) are searched.
declare -A LOG_INDEX
shopt -s nullglob
for shard in "$ORACLE_ROOT"/alive_tv "$ORACLE_ROOT"/alive_tv_*; do
    [[ -d "$shard/fail" ]] || continue
    for f in "$shard/fail"/*.log; do
        base="$(basename "$f")"
        LOG_INDEX["$base"]="$f"
    done
done
shopt -u nullglob

# Collect (reduced_ll, log) pairs.
PAIRS=()
SKIPPED=0
shopt -s nullglob
for reduced in "$MISC_DIR"/*.reduced.ll; do
    base="$(basename "$reduced" .reduced.ll)"  # strip .reduced.ll
    log_key="${base}.log"
    log_path="${LOG_INDEX[$log_key]:-}"
    if [[ -z "$log_path" ]]; then
        SKIPPED=$((SKIPPED + 1))
        continue
    fi
    PAIRS+=("$reduced|$log_path")
done
shopt -u nullglob

if (( ${#PAIRS[@]} == 0 )); then
    echo "no findings to triage (skipped $SKIPPED without matching log)" >&2
    exit 0
fi

echo "[triage] found ${#PAIRS[@]} findings ($SKIPPED skipped without log)" >&2

# Normalize each *.reduced.ll → *.reduced.normalized.ll (skip if up to date).
NORMALIZED_FILES=()
for pair in "${PAIRS[@]}"; do
    reduced="${pair%%|*}"
    norm="${reduced%.ll}.normalized.ll"
    if [[ ! -f "$norm" || "$reduced" -nt "$norm" ]]; then
        if ! python3 "$NORMALIZE" "$reduced" "$norm" >/dev/null 2>&1; then
            # Fall back to the raw reduced file if normalization fails.
            cp "$reduced" "$norm"
            echo "[triage] normalize failed for $(basename "$reduced"); using raw" >&2
        fi
    fi
    NORMALIZED_FILES+=("$norm")
done

# Build basename → (ll_src, log_path) maps so we can decide what to stage.
declare -A LL_SRC_BY_NAME
declare -A LOG_BY_NAME
for ((i = 0; i < ${#PAIRS[@]}; i++)); do
    pair="${PAIRS[$i]}"
    reduced="${pair%%|*}"
    log_path="${pair##*|}"
    norm="${NORMALIZED_FILES[$i]}"
    if [[ -f "$norm" ]]; then
        ll_src="$norm"
    else
        ll_src="$reduced"
    fi
    name="$(basename "$ll_src")"
    LL_SRC_BY_NAME["$name"]="$ll_src"
    LOG_BY_NAME["$name"]="$log_path"
done

# Sorted list of every basename in the current set.
mapfile -t CURRENT_SORTED < <(printf '%s\n' "${!LL_SRC_BY_NAME[@]}" | sort)

# Diff against the manifest of previously-triaged filenames to decide whether
# this is a fresh, incremental, or no-op run. --force always treats the run
# as fresh and triages everything.
NEW_FILES=()
if (( FORCE == 0 )) && [[ -f "$TRIAGED_MANIFEST" ]]; then
    mapfile -t NEW_FILES < <(comm -23 \
        <(printf '%s\n' "${CURRENT_SORTED[@]}") \
        <(sort -u "$TRIAGED_MANIFEST"))
    if (( ${#NEW_FILES[@]} == 0 )); then
        echo "No new findings to triage" >&2
        exit 0
    fi
fi

INCREMENTAL=0
if (( FORCE == 0 )) && [[ -f "$REPORT" ]] && (( ${#NEW_FILES[@]} > 0 )); then
    INCREMENTAL=1
fi

# Stage everything for claude in a temp working directory.
WORK_DIR="$(mktemp -d -t triage.XXXXXX)"

cleanup() {
    if (( DRY_RUN == 0 )); then
        rm -rf "$WORK_DIR"
    fi
}
trap cleanup EXIT

# Decide which findings to copy in. In incremental mode we only stage the
# new ones and let claude reconcile against the prior report; in fresh mode
# we stage every finding in the current set.
if (( INCREMENTAL == 1 )); then
    STAGE_LIST=("${NEW_FILES[@]}")
    cp "$REPORT" "$WORK_DIR/PREVIOUS_REPORT.md"
else
    STAGE_LIST=("${CURRENT_SORTED[@]}")
fi

for ll_name in "${STAGE_LIST[@]}"; do
    cp "${LL_SRC_BY_NAME[$ll_name]}" "$WORK_DIR/$ll_name"
    cp "${LOG_BY_NAME[$ll_name]}" "$WORK_DIR/${ll_name}.log"
done

# Copy the playbook, expanding the whitelisted ${VAR} placeholders to
# project-local tool paths. The whitelist keeps envsubst from touching
# unrelated $-tokens in the playbook prose.
envsubst '${ALIVE_TV} ${OPT} ${LLVM_BUILD_PLAIN} ${LLVM_SRC}' \
    < "$PLAYBOOK_SRC" \
    > "$WORK_DIR/TRIAGE_PLAYBOOK.md"

if (( INCREMENTAL == 1 )); then
    MODE_LABEL="incremental"
else
    MODE_LABEL="fresh"
fi

if (( DRY_RUN == 1 )); then
    echo "=== DRY RUN ===" >&2
    echo "mode: $MODE_LABEL" >&2
    echo "findings (current): ${#PAIRS[@]}" >&2
    echo "staged: ${#STAGE_LIST[@]}" >&2
    echo "work dir (preserved): $WORK_DIR" >&2
    echo "$WORK_DIR"
    exit 0
fi

# Locate the claude CLI. Honor $CLAUDE_BIN if set, else search $PATH and a
# couple of common install locations.
CLAUDE_BIN="${CLAUDE_BIN:-}"
if [[ -z "$CLAUDE_BIN" ]]; then
    if command -v claude >/dev/null 2>&1; then
        CLAUDE_BIN="$(command -v claude)"
    elif [[ -x "$HOME/.local/bin/claude" ]]; then
        CLAUDE_BIN="$HOME/.local/bin/claude"
    elif [[ -x "$HOME/.claude/local/claude" ]]; then
        CLAUDE_BIN="$HOME/.claude/local/claude"
    else
        echo "error: 'claude' CLI not found on PATH" >&2
        echo "  set CLAUDE_BIN to the claude executable, or install Claude Code" >&2
        exit 2
    fi
fi

if (( INCREMENTAL == 1 )); then
    PROMPT="Read TRIAGE_PLAYBOOK.md. A previous report exists at PREVIOUS_REPORT.md. New findings are the .ll and .ll.log files in this directory. Follow the incremental update instructions in the playbook."
else
    PROMPT="Read TRIAGE_PLAYBOOK.md and follow the fresh triage instructions. The .ll and .ll.log files are in this directory."
fi

echo "[triage] launching claude in $WORK_DIR ($MODE_LABEL, ${#STAGE_LIST[@]} of ${#PAIRS[@]} findings staged)..." >&2
TMP_REPORT="$(mktemp)"
if ! (cd "$WORK_DIR" && "$CLAUDE_BIN" --dangerously-skip-permissions -p "$PROMPT" 2>>"$TRIAGE_LOG") > "$TMP_REPORT"; then
    rc=$?
    rm -f "$TMP_REPORT"
    echo "[triage] claude session failed (exit $rc)" >&2
    exit "$rc"
fi
mv "$TMP_REPORT" "$REPORT"
echo "[triage] wrote $REPORT" >&2

# Manifest of every filename now reflected in the report. The next run diffs
# against this to find new findings.
printf '%s\n' "${CURRENT_SORTED[@]}" > "$TRIAGED_MANIFEST"
echo "[triage] wrote $TRIAGED_MANIFEST (${#CURRENT_SORTED[@]} entries)" >&2

# last_run.json: timestamp + count + file list.
{
    printf '{\n'
    printf '  "timestamp": "%s",\n' "$(date -Is)"
    printf '  "mode": "%s",\n' "$MODE_LABEL"
    printf '  "count": %d,\n' "${#PAIRS[@]}"
    printf '  "staged": %d,\n' "${#STAGE_LIST[@]}"
    printf '  "files": [\n'
    n=${#NORMALIZED_FILES[@]}
    for ((i = 0; i < n; i++)); do
        comma=','
        (( i == n - 1 )) && comma=''
        printf '    "%s"%s\n' "$(basename "${NORMALIZED_FILES[$i]}")" "$comma"
    done
    printf '  ]\n'
    printf '}\n'
} > "$LAST_RUN"
echo "[triage] wrote $LAST_RUN" >&2
