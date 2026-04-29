#!/usr/bin/env bash
#
# Run pass_probe (--pass=slp-vectorizer --list-reached) on every
# split_seeds/SLPVectorizer__*.ll file and write one TSV row per seed:
#   <filename>\t<sorted, comma-separated function list>
#
# Output: docs/slp_decomposition/all_seeds_coverage.tsv
# Errors / empty results: still emit the row with an empty signature
# field so the file is a 1:1 map of seeds → signatures.

set -euo pipefail

cd "$(dirname "$0")/../.."

PROBE=./scripts/analysis/pass_probe.sh
OUT_TSV=docs/slp_decomposition/all_seeds_coverage.tsv

if [[ ! -x "$PROBE" ]]; then
    echo "ERROR: $PROBE not executable" >&2
    exit 2
fi

mapfile -t SEEDS < <(ls split_seeds/SLPVectorizer__*.ll 2>/dev/null | sort)
if (( ${#SEEDS[@]} == 0 )); then
    echo "ERROR: no SLP seeds found under split_seeds/" >&2
    exit 2
fi

echo "[batch] $(date +%H:%M:%S) probing ${#SEEDS[@]} seeds, parallelism=$(nproc)" >&2

# Single-seed worker: emit one TSV line, stable per-seed.
probe_one() {
    local seed="$1"
    local sig
    if sig=$(./scripts/analysis/pass_probe.sh \
                 --pass=slp-vectorizer --list-reached "$seed" 2>/dev/null \
             | sort -u | paste -sd, -); then
        printf '%s\t%s\n' "$seed" "$sig"
    else
        printf '%s\t\n' "$seed"
    fi
}
export -f probe_one

# xargs is more portable than GNU parallel; 16-way is plenty for a
# coverage_probe-bound workload (each child runs ~2s of LLVM).
PAR=$(( $(nproc) > 16 ? 16 : $(nproc) ))
TMP_TSV=$(mktemp)
trap 'rm -f "$TMP_TSV"' EXIT

printf '%s\n' "${SEEDS[@]}" \
    | xargs -P "$PAR" -I{} bash -c 'probe_one "$@"' _ {} \
    > "$TMP_TSV"

# Re-sort by seed name so the output is deterministic.
sort -u "$TMP_TSV" > "$OUT_TSV"

# ----------------- summary stats -----------------
total=$(wc -l < "$OUT_TSV")
nonempty=$(awk -F'\t' '$2 != ""' "$OUT_TSV" | wc -l)
unique_sigs=$(awk -F'\t' '{print $2}' "$OUT_TSV" | sort -u | wc -l)
union=$(awk -F'\t' '{ n=split($2, a, ","); for (i=1;i<=n;i++) print a[i] }' "$OUT_TSV" \
        | sort -u | grep -v '^$' | wc -l)

echo "[batch] $(date +%H:%M:%S) done"
echo
echo "  seeds processed:           $total"
echo "  seeds with coverage:       $nonempty"
echo "  unique coverage sigs:      $unique_sigs"
echo "  union of fns across corpus: $union"
echo
echo "  TSV: $OUT_TSV"
