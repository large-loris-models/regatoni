#!/usr/bin/env bash
# Watch a Centipede workdir for new corpus entries and verify each one with
# the Alive2 harness. Files that make the harness abort() (exit 134) are
# copied into miscompilations/ for triage.

set -u

WORKDIR="${1:-build/workdir}"
HARNESS="./build/opt_fuzz_target_alive2"
MISCOMP_DIR="miscompilations"
STATE_DIR="$WORKDIR/.verify_state"
CHECKED_LIST="$STATE_DIR/checked.txt"
LOG="$STATE_DIR/verify.log"
INTERVAL=30
PAR=2

if [[ ! -x "$HARNESS" ]]; then
    echo "ERROR: harness not found or not executable: $HARNESS" >&2
    exit 1
fi
if [[ ! -d "$WORKDIR" ]]; then
    echo "ERROR: workdir not found: $WORKDIR" >&2
    exit 1
fi

mkdir -p "$MISCOMP_DIR" "$STATE_DIR"
touch "$CHECKED_LIST"

verify_one() {
    local f="$1"
    local harness="$2"
    local miscomp_dir="$3"
    local log="$4"

    timeout 60s "$harness" "$f" >/dev/null 2>&1
    local rc=$?
    if [[ $rc -eq 134 ]]; then
        local base
        base="$(basename "$f")"
        cp "$f" "$miscomp_dir/$base"
        echo "[$(date -Is)] MISCOMP $f" | tee -a "$log"
    fi
}
export -f verify_one

echo "[verify] workdir=$WORKDIR harness=$HARNESS miscomp=$MISCOMP_DIR par=$PAR"

while true; do
    # Corpus files live in workdir as corpus.* shards plus seed subdirs.
    # Treat every regular file under workdir (except our own state) as a
    # candidate input; the harness will reject anything that isn't valid IR.
    mapfile -t all_files < <(find "$WORKDIR" -type f \
        -not -path "$STATE_DIR/*" \
        -not -name 'fuzz.log*' \
        -not -name '*.log' 2>/dev/null | sort)

    # Diff against the checked list.
    new_files=()
    for f in "${all_files[@]}"; do
        if ! grep -Fxq "$f" "$CHECKED_LIST"; then
            new_files+=("$f")
        fi
    done

    if (( ${#new_files[@]} > 0 )); then
        echo "[verify] $(date -Is): checking ${#new_files[@]} new file(s)"
        printf '%s\0' "${new_files[@]}" \
            | xargs -0 -n1 -P"$PAR" -I{} \
                bash -c 'verify_one "$@"' _ {} "$HARNESS" "$MISCOMP_DIR" "$LOG"
        printf '%s\n' "${new_files[@]}" >> "$CHECKED_LIST"
    fi

    sleep "$INTERVAL"
done
