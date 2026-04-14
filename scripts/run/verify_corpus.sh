#!/usr/bin/env bash
# Scan the project corpus directory and verify each file with the Alive2
# harness. Files that make the harness abort() (exit 134) are copied into
# miscompilations/ for triage.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null

CORPUS="${1:-$CORPUS_DIR}"
HARNESS="$BUILD_OUT/opt_fuzz_target_alive2"
MISCOMP_DIR="$PROJECT_ROOT/miscompilations"
STATE_DIR="$PROJECT_ROOT/build/.verify_state"
CHECKED_LIST="$STATE_DIR/checked.txt"
LOG="$STATE_DIR/verify.log"
INTERVAL=30
PAR=2

if [[ ! -x "$HARNESS" ]]; then
    echo "ERROR: harness not found or not executable: $HARNESS" >&2
    exit 1
fi
if [[ ! -d "$CORPUS" ]]; then
    echo "ERROR: corpus dir not found: $CORPUS" >&2
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

echo "[verify] corpus=$CORPUS harness=$HARNESS miscomp=$MISCOMP_DIR par=$PAR"

while true; do
    # Corpus files are raw (no extension), named by hash. Treat every regular
    # file directly under the corpus dir as a candidate input.
    mapfile -t all_files < <(find "$CORPUS" -maxdepth 1 -type f 2>/dev/null | sort)

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
