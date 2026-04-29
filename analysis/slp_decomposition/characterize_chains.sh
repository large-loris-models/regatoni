#!/usr/bin/env bash
#
# characterize_chains.sh — call-chain coverage characterisation for the
# (N × T) decomposition experiment.
#
# For each minimal seed N1..N16, each systematic single-T cell (N_i,T_j),
# and each synthesis S_* cell, runs pass_probe.sh --call-chains
# --pass=slp-vectorizer to capture the dynamic call-chain set. SKIPPED
# stubs (header line starts with "; SKIPPED:") are ignored. All probes
# run in parallel via xargs -P.
#
# Outputs (under docs/slp_decomposition/chains/):
#   N<i>.chains                — chains for bare seed N_i
#   N<i>_T<j>.chains           — chains for the systematic cell
#   S_<name>.chains            — chains for the synthesis cell
#   per_T_signature_T<j>.chains — chains in any (N_i,T_j) but not in the
#                                  corresponding bare N_i
#   union_N.chains              — union over all minimal seeds
#   union_NT.chains             — union over all (N_i,T_j) cells
#   union_S.chains              — union over all synthesis cells
#   summary.tsv                 — name <TAB> num_chains, one row per file
#                                  + aggregate rows (union_*, per_T_*)
#   t_uniqueness.tsv            — for each T, chains that ONLY appear when
#                                  T_j is applied to a *specific* N_i
#                                  (not in bare N, not in T applied to
#                                   other seeds): N_i, T_j, chain
#
# Configuration:
#   K=<n>      chain length passed to pass_probe.sh --k=N (default 3)
#   JOBS=<n>   parallelism for xargs -P (default: nproc/2, min 2, max 8)
#   OUT=<dir>  output dir (default docs/slp_decomposition/chains)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

K="${K:-3}"
JOBS_DEFAULT=$(( $(nproc) / 2 ))
(( JOBS_DEFAULT < 2 )) && JOBS_DEFAULT=2
(( JOBS_DEFAULT > 8 )) && JOBS_DEFAULT=8
JOBS="${JOBS:-$JOBS_DEFAULT}"

OUT="${OUT:-docs/slp_decomposition/chains}"
mkdir -p "$OUT"

DECOMP="docs/slp_decomposition"
PROBE="scripts/analysis/pass_probe.sh"

if [[ ! -x "$PROBE" ]]; then
    echo "ERROR: $PROBE not executable" >&2
    exit 2
fi

echo "[characterize_chains] K=$K JOBS=$JOBS OUT=$OUT"

# ----------------------------------------------------------------------
# Step 1 — enumerate every input file we need to probe.
# ----------------------------------------------------------------------

# Minimal seeds N1..N16 (the spec calls out 16; later N17..N22 are not
# part of the systematic experiment so they sit out of the bare-N pool
# but we still probe them as bonus.)
mapfile -t MINIMAL < <(
    for i in $(seq 1 16); do
        f="$DECOMP/minimal/N${i}_"*.ll
        compgen -G "$f" | head -n1
    done
)

# Systematic single-T cells, drop SKIPPED stubs.
mapfile -t SYSTEMATIC < <(
    for f in "$DECOMP"/systematic/N*_T*.ll; do
        bn="$(basename "$f")"
        # only single-T cells (e.g. N1_T1.ll, not N1_T1_T2.ll)
        [[ "$bn" =~ ^N[0-9]+_T[0-9]+\.ll$ ]] || continue
        # skip stubs — first non-blank line begins with "; SKIPPED:"
        head -1 "$f" | grep -q '^; SKIPPED:' && continue
        echo "$f"
    done
)

mapfile -t SYNTHESIS < <(
    for f in "$DECOMP"/synthesis/S_*.ll; do
        # skip stubs if any
        head -1 "$f" | grep -q '^; SKIPPED:' && continue
        echo "$f"
    done
)

echo "[characterize_chains] minimal=${#MINIMAL[@]} systematic=${#SYSTEMATIC[@]} synthesis=${#SYNTHESIS[@]}"

# ----------------------------------------------------------------------
# Step 2 — probe each input. We write `<basename-without-.ll>.chains` to
# $OUT, sorted/deduped. Skip files whose .chains already exist (this
# script is rerun-friendly; delete $OUT to force regeneration).
# ----------------------------------------------------------------------

probe_one() {
    local in="$1"
    local stem
    stem="$(basename "$in" .ll)"
    local out="$OUT/$stem.chains"
    [[ -s "$out" ]] && return 0
    # Race-safe: write to a tmp file, atomic mv.
    local tmp="$out.tmp.$$"
    if "$PROBE" --pass=slp-vectorizer --call-chains --k="$K" "$in" 2>/dev/null \
        | sort -u > "$tmp"; then
        mv -f "$tmp" "$out"
    else
        rm -f "$tmp"
        echo "WARN: probe failed on $in" >&2
        : > "$out"
    fi
}
export -f probe_one
export OUT K PROBE

ALL_INPUTS=("${MINIMAL[@]}" "${SYSTEMATIC[@]}" "${SYNTHESIS[@]}")
echo "[characterize_chains] Probing ${#ALL_INPUTS[@]} files (parallelism=$JOBS)..."
printf '%s\n' "${ALL_INPUTS[@]}" \
    | xargs -P "$JOBS" -I{} bash -c 'probe_one "$@"' _ {}

# ----------------------------------------------------------------------
# Step 3 — aggregate.
# ----------------------------------------------------------------------

# Helpers
union_files() {
    # Concatenate, dedup, write to $1; remaining args are .chains files.
    local out="$1"; shift
    if (( $# == 0 )); then
        : > "$out"; return
    fi
    cat "$@" | sort -u > "$out"
}

# Map a basename N1, N2, ... to its full minimal stem (N1, N2 — chains
# files are stored by basename without _suffix), so we can find the
# probe output for "bare N_i".
declare -A MINIMAL_STEM
for f in "${MINIMAL[@]}"; do
    bn="$(basename "$f" .ll)"
    n="${bn%%_*}"
    MINIMAL_STEM[$n]="$bn"
done

# union_N
union_files "$OUT/union_N.chains" \
    $(for f in "${MINIMAL[@]}"; do echo "$OUT/$(basename "$f" .ll).chains"; done)

# union_NT
union_files "$OUT/union_NT.chains" \
    $(for f in "${SYSTEMATIC[@]}"; do echo "$OUT/$(basename "$f" .ll).chains"; done)

# union_S
union_files "$OUT/union_S.chains" \
    $(for f in "${SYNTHESIS[@]}"; do echo "$OUT/$(basename "$f" .ll).chains"; done)

# Per-T signature: chains in any (N_i,T_j) but not in bare N_i.
# We compute T_j_signature = ⋃_i ((N_i,T_j).chains \ N_i.chains)
declare -A T_IDS
for f in "${SYSTEMATIC[@]}"; do
    bn="$(basename "$f" .ll)"
    t="${bn##*_}"
    T_IDS[$t]=1
done

for t in $(printf '%s\n' "${!T_IDS[@]}" | sort -V); do
    sig="$OUT/per_T_signature_${t}.chains"
    : > "$sig.tmp"
    for f in "${SYSTEMATIC[@]}"; do
        bn="$(basename "$f" .ll)"
        # match this T
        [[ "$bn" == *"_$t" ]] || continue
        n="${bn%%_*}"
        nbare="$OUT/${MINIMAL_STEM[$n]:-$n}.chains"
        nt="$OUT/$bn.chains"
        [[ -s "$nt" ]] || continue
        if [[ -s "$nbare" ]]; then
            comm -23 <(sort -u "$nt") <(sort -u "$nbare") >> "$sig.tmp"
        else
            cat "$nt" >> "$sig.tmp"
        fi
    done
    sort -u "$sig.tmp" > "$sig"
    rm -f "$sig.tmp"
done

# t_uniqueness.tsv: chains added by T_j to N_i that don't appear in
# bare N OR in T_j applied to any other N (i.e. chains that ONLY
# materialise in this specific (N_i, T_j) combination).
{
    echo -e "N\tT\tchain"
    for f in "${SYSTEMATIC[@]}"; do
        bn="$(basename "$f" .ll)"
        n="${bn%%_*}"
        t="${bn##*_}"
        nt="$OUT/$bn.chains"
        nbare="$OUT/${MINIMAL_STEM[$n]:-$n}.chains"
        [[ -s "$nt" ]] || continue

        # Build "all chains seen in T_j applied to OTHER N's" + bare-N.
        other="$(mktemp)"
        for ff in "${SYSTEMATIC[@]}"; do
            obn="$(basename "$ff" .ll)"
            [[ "$obn" == *"_$t" ]] || continue
            [[ "$obn" == "$bn" ]] && continue
            cat "$OUT/$obn.chains" 2>/dev/null
        done | sort -u > "$other"
        # Also exclude chains in any bare N (so we capture chains
        # genuinely produced by THIS combination, not shared baseline).
        cat "$OUT/union_N.chains" >> "$other"
        sort -u -o "$other" "$other"

        comm -23 <(sort -u "$nt") "$other" \
            | awk -v n="$n" -v t="$t" '{print n"\t"t"\t"$0}'
        rm -f "$other"
    done
} > "$OUT/t_uniqueness.tsv"

# ----------------------------------------------------------------------
# Step 4 — summary.tsv
# ----------------------------------------------------------------------

count_chains() { wc -l < "$1" 2>/dev/null || echo 0; }

{
    echo -e "name\tnum_chains"
    for f in "${MINIMAL[@]}"; do
        bn="$(basename "$f" .ll)"
        echo -e "minimal:$bn\t$(count_chains "$OUT/$bn.chains")"
    done
    for f in "${SYSTEMATIC[@]}"; do
        bn="$(basename "$f" .ll)"
        echo -e "systematic:$bn\t$(count_chains "$OUT/$bn.chains")"
    done
    for f in "${SYNTHESIS[@]}"; do
        bn="$(basename "$f" .ll)"
        echo -e "synthesis:$bn\t$(count_chains "$OUT/$bn.chains")"
    done
    echo -e "union_N\t$(count_chains "$OUT/union_N.chains")"
    echo -e "union_NT\t$(count_chains "$OUT/union_NT.chains")"
    echo -e "union_S\t$(count_chains "$OUT/union_S.chains")"
    for t in $(printf '%s\n' "${!T_IDS[@]}" | sort -V); do
        echo -e "per_T_signature_$t\t$(count_chains "$OUT/per_T_signature_$t.chains")"
    done
} > "$OUT/summary.tsv"

echo "[characterize_chains] Done."
echo "  union_N    : $(count_chains "$OUT/union_N.chains") chains"
echo "  union_NT   : $(count_chains "$OUT/union_NT.chains") chains"
echo "  union_S    : $(count_chains "$OUT/union_S.chains") chains"
echo "  summary    : $OUT/summary.tsv"
echo "  uniqueness : $OUT/t_uniqueness.tsv"
