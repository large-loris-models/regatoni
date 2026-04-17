#!/usr/bin/env bash
# Unified status dashboard for the fuzzing pipeline.
#
# Shows: fuzzer health, unique crashes, ASAN findings (deduped),
# Alive-TV miscompilations (deduped), and a roll-up summary.
#
# Usage: ./scripts/analysis/status.sh [run.log]
#   Default run log: $BUILD_OUT/run_state/run.log

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null

RUN_LOG="${1:-$BUILD_OUT/run_state/run.log}"
PIDS_FILE="$BUILD_OUT/run_state/pids"
ORACLE_ROOT="$BUILD_OUT/oracle_results"
ASAN_DIR="$ORACLE_ROOT/asan_opt"
# Alive-TV may run sharded (alive_tv_0/, alive_tv_1/, ...) or unsharded (alive_tv/).
mapfile -t ALIVE_DIRS < <(
    if [[ -d "$ORACLE_ROOT" ]]; then
        find "$ORACLE_ROOT" -maxdepth 1 -type d \
            \( -name 'alive_tv' -o -name 'alive_tv_*' \) | sort
    fi
)

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

if [[ -t 1 ]]; then
    C_BOLD=$'\e[1m'; C_DIM=$'\e[2m'; C_RESET=$'\e[0m'
    C_HDR=$'\e[1;36m'; C_GRN=$'\e[32m'
else
    C_BOLD=''; C_DIM=''; C_RESET=''; C_HDR=''; C_GRN=''
fi

bar() { printf '═%.0s' {1..72}; echo; }
hdr() { printf '\n%s%s%s\n' "$C_HDR" "$1" "$C_RESET"; bar; }

fmt_dur() {
    local s=$1 d h m r
    d=$((s/86400)); h=$(((s%86400)/3600)); m=$(((s%3600)/60)); r=$((s%60))
    if   (( d > 0 )); then printf '%dd %dh %dm' "$d" "$h" "$m"
    elif (( h > 0 )); then printf '%dh %dm %ds' "$h" "$m" "$r"
    else                   printf '%dm %ds' "$m" "$r"
    fi
}

# ASAN/UBSAN log → "type<TAB>top_frame<TAB>basename"
parse_asan_log() {
    local f="$1" base summary loc frame
    base=$(basename "$f" .log)
    summary=$(grep -m1 '^SUMMARY:' "$f" 2>/dev/null || true)

    if [[ -z "$summary" ]]; then
        loc=$(grep -m1 'runtime error:' "$f" 2>/dev/null \
              | sed -E 's|.*/([^/]+:[0-9]+):[0-9]+: runtime error.*|\1|' || true)
        printf '%s\t%s\t%s\n' "unknown" "${loc:-?}" "$base"
        return
    fi

    # heap-buffer-overflow / use-after-free / etc: "<san>: <type> /path:l:c in <func>"
    if [[ "$summary" =~ ^SUMMARY:\ [A-Za-z]+:\ ([A-Za-z][A-Za-z0-9_-]+)\ .*\ in\ (.+)$ ]]; then
        printf '%s\t%s\t%s\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "$base"
        return
    fi

    # Leak: SUMMARY has no "in <func>"; pull first non-allocator frame from stack.
    if [[ "$summary" == *" leaked"* ]]; then
        frame=$(grep -E '^[[:space:]]+#[0-9]+ 0x[0-9a-f]+ in ' "$f" \
            | sed -E '
                s|^[[:space:]]+#[0-9]+ 0x[0-9a-f]+ in ||
                s| /[^ ]+:[0-9]+(:[0-9]+)?$||
                s| \(/[^)]*\) \(BuildId: [^)]*\)$||
              ' \
            | grep -vE '^(operator new|operator delete|malloc|calloc|realloc|__interceptor_|llvm::User::operator new|llvm::User::allocHungoffUses|allocHungoffUses|Create|PHINode)' \
            | head -1)
        # Strip template/arg parens for cleaner dedup.
        frame=$(printf '%s' "${frame:-?}" | sed -E 's/\(.*$//')
        printf '%s\t%s\t%s\n' "memory-leak" "${frame:-?}" "$base"
        return
    fi

    # UBSAN: "<san>: <type> /path:l:c"
    if [[ "$summary" =~ ^SUMMARY:\ [A-Za-z]+:\ ([A-Za-z][A-Za-z0-9_-]+)\ (.+)$ ]]; then
        loc="${BASH_REMATCH[2]}"
        loc="${loc%"${loc##*[![:space:]]}"}"  # rtrim
        loc="${loc#*/llvm-project/}"
        printf '%s\t%s\t%s\n' "${BASH_REMATCH[1]}" "$loc" "$base"
        return
    fi

    printf '%s\t%s\t%s\n' "unknown" "?" "$base"
}

# Alive-TV log → "function<TAB>error<TAB>reduced<TAB>basename"
parse_alive_log() {
    local f="$1" base func err reduced
    base=$(basename "$f" .log)
    func=$(grep -m1 '^ALIVE2 MISCOMPILE in function' "$f" 2>/dev/null \
           | sed 's/^ALIVE2 MISCOMPILE in function //' || true)
    err=$(grep -m1 '^ERROR:' "$f" 2>/dev/null | sed 's/^ERROR: //' || true)
    reduced="-"
    [[ -f "${f%.log}.reduced.ll" ]] && reduced="yes"
    printf '%s\t%s\t%s\t%s\n' "${func:-?}" "${err:-?}" "$reduced" "$base"
}

# ────────── Section 1: Fuzzer Status ──────────
hdr "Section 1: Fuzzer Status"

if [[ ! -f "$RUN_LOG" ]]; then
    echo "  run.log not found at $RUN_LOG — fuzzer not started"
else
    first_ts=$(grep -oE '^\[[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:+-]+\]' "$RUN_LOG" \
               | head -1 | tr -d '[]')

    is_running=0
    if [[ -f "$PIDS_FILE" ]]; then
        while IFS= read -r line; do
            # pids file entries are "<name>:<pid>" or just "<pid>"
            pid="${line##*:}"
            [[ "$pid" =~ ^[0-9]+$ ]] || continue
            if kill -0 "$pid" 2>/dev/null; then is_running=1; break; fi
        done < "$PIDS_FILE"
    fi

    if (( is_running == 1 )); then
        end_epoch=$(date +%s)
        printf "  Status:        %s%s%s\n" "$C_GRN" "RUNNING" "$C_RESET"
    else
        end_epoch=$(stat -c %Y "$RUN_LOG" 2>/dev/null || date +%s)
        printf "  Status:        %s%s%s\n" "$C_DIM" "STOPPED" "$C_RESET"
    fi

    if [[ -n "$first_ts" ]]; then
        first_epoch=$(date -d "$first_ts" +%s 2>/dev/null || echo 0)
        if (( first_epoch > 0 )); then
            printf "  Runtime:       %s\n" "$(fmt_dur $((end_epoch - first_epoch)))"
        fi
    fi

    latest=$(grep -oE 'ft: [0-9]+ cov: [0-9]+ cnt: [0-9]+ cmp: [0-9]+' "$RUN_LOG" | tail -1)
    if [[ -n "$latest" ]]; then
        read -r _ ft _ cov _ cnt _ cmp <<<"$latest"
        printf "  Total features:   %s\n" "$ft"
        printf "  PC coverage:   %s\n" "$cov"
        printf "  Counter features: %s\n" "$cnt"
        printf "  Cmp features:     %s\n" "$cmp"
    fi

    avg_exec=$(awk '
        /\[S[0-9]+\..*exec\/s:/ {
            if (match($0, /\[S[0-9]+\./)) {
                shard = substr($0, RSTART+2, RLENGTH-3)
                for (i = 1; i <= NF; i++) if ($i == "exec/s:") last[shard] = $(i+1)+0
            }
        }
        END {
            n = 0; s = 0
            for (k in last) { s += last[k]; n++ }
            if (n > 0) printf "%.0f exec/s avg across %d shards", s/n, n
        }' "$RUN_LOG")
    [[ -n "$avg_exec" ]] && printf "  Throughput:    %s\n" "$avg_exec"

    if [[ -d "$CORPUS_DIR" ]]; then
        cs=$(find "$CORPUS_DIR" -maxdepth 1 -type f 2>/dev/null | wc -l)
        printf "  Corpus size:   %d files\n" "$cs"
    fi
fi

# ────────── Section 2: Unique Crashes ──────────
hdr "Section 2: Unique Crashes (fuzzer)"

CRASHES_TSV="$TMP/crashes.tsv"
: > "$CRASHES_TSV"
total_crashes=0
unique_crashes=0
if [[ -f "$RUN_LOG" ]]; then
    grep "CRASH LOG:.*opt_fuzz_target:" "$RUN_LOG" 2>/dev/null \
        | sed 's/.*opt_fuzz_target: //' \
        | sort | uniq -c | sort -rn > "$CRASHES_TSV" || true
    total_crashes=$(grep -c "CRASH LOG:.*opt_fuzz_target:" "$RUN_LOG" 2>/dev/null) \
        || total_crashes=0
    unique_crashes=$(wc -l < "$CRASHES_TSV") || unique_crashes=0
fi

if (( total_crashes == 0 )); then
    echo "  no crashes recorded"
else
    printf "  %5s  %s\n" "HITS" "ASSERTION"
    printf "  %5s  %s\n" "-----" "---------"
    while read -r count rest; do
        printf "  %5d  %s\n" "$count" "${rest:0:140}"
    done < "$CRASHES_TSV"
    echo
    printf "  Total: %d crashes, %d unique\n" "$total_crashes" "$unique_crashes"
fi

# ────────── Section 3: ASAN Findings ──────────
hdr "Section 3: ASAN Oracle Findings"

unique_asan=0
if [[ ! -d "$ASAN_DIR" ]]; then
    echo "  not started"
else
    asan_pass=$(find "$ASAN_DIR/pass"    -maxdepth 1 -type f 2>/dev/null | wc -l)
    asan_fail=$(find "$ASAN_DIR/fail"    -maxdepth 1 -type f -name '*.log' 2>/dev/null | wc -l)
    asan_to=$(find "$ASAN_DIR/timeout"   -maxdepth 1 -type f 2>/dev/null | wc -l)
    asan_err=$(find "$ASAN_DIR/error"    -maxdepth 1 -type f 2>/dev/null | wc -l)
    asan_chk=0
    [[ -f "$ASAN_DIR/checked.log" ]] && asan_chk=$(wc -l < "$ASAN_DIR/checked.log")
    printf "  Progress: checked=%d  pass=%d  fail=%d  timeout=%d  error=%d\n\n" \
        "$asan_chk" "$asan_pass" "$asan_fail" "$asan_to" "$asan_err"

    if (( asan_fail == 0 )); then
        echo "  no failures"
    else
        ASAN_TSV="$TMP/asan.tsv"; : > "$ASAN_TSV"
        for f in "$ASAN_DIR/fail"/*.log; do
            [[ -e "$f" ]] || continue
            parse_asan_log "$f" >> "$ASAN_TSV"
        done

        ASAN_DEDUP="$TMP/asan_dedup.tsv"
        awk -F'\t' '
            { key = $1 "\t" $2; cnt[key]++; if (!(key in ex)) ex[key] = $3 }
            END { for (k in cnt) print cnt[k] "\t" k "\t" ex[k] }
        ' "$ASAN_TSV" | sort -rn > "$ASAN_DEDUP"
        unique_asan=$(wc -l < "$ASAN_DEDUP") || unique_asan=0

        printf "  %-22s  %-46s  %5s  %s\n" "TYPE" "TOP FRAME / LOCATION" "COUNT" "EXAMPLE"
        printf "  %-22s  %-46s  %5s  %s\n" "----" "--------------------" "-----" "-------"
        while IFS=$'\t' read -r count etype etop example; do
            printf "  %-22.22s  %-46.46s  %5d  %s\n" "$etype" "$etop" "$count" "$example"
        done < "$ASAN_DEDUP"
    fi
fi

# ────────── Section 4: Alive-TV Miscompilations ──────────
hdr "Section 4: Alive-TV Miscompilations"

unique_miscomps=0
if (( ${#ALIVE_DIRS[@]} == 0 )); then
    echo "  not started"
else
    a_pass=0; a_fail=0; a_to=0; a_err=0; a_chk=0
    for d in "${ALIVE_DIRS[@]}"; do
        a_pass=$(( a_pass + $(find "$d/pass"    -maxdepth 1 -type f 2>/dev/null | wc -l) ))
        a_fail=$(( a_fail + $(find "$d/fail"    -maxdepth 1 -type f -name '*.log' 2>/dev/null | wc -l) ))
        a_to=$((   a_to   + $(find "$d/timeout" -maxdepth 1 -type f 2>/dev/null | wc -l) ))
        a_err=$((  a_err  + $(find "$d/error"   -maxdepth 1 -type f 2>/dev/null | wc -l) ))
        [[ -f "$d/checked.log" ]] && a_chk=$(( a_chk + $(wc -l < "$d/checked.log") ))
    done
    if (( ${#ALIVE_DIRS[@]} > 1 )); then
        printf "  Shards: %d (%s)\n" "${#ALIVE_DIRS[@]}" \
            "$(printf '%s ' "${ALIVE_DIRS[@]##*/}")"
    fi
    printf "  Progress: checked=%d  pass=%d  fail=%d  timeout=%d  error=%d\n\n" \
        "$a_chk" "$a_pass" "$a_fail" "$a_to" "$a_err"

    if (( a_fail == 0 )); then
        echo "  no miscompilations"
    else
        ALIVE_TSV="$TMP/alive.tsv"; : > "$ALIVE_TSV"
        for d in "${ALIVE_DIRS[@]}"; do
            for f in "$d/fail"/*.log; do
                [[ -e "$f" ]] || continue
                parse_alive_log "$f" >> "$ALIVE_TSV"
            done
        done

        ALIVE_DEDUP="$TMP/alive_dedup.tsv"
        awk -F'\t' '
            { key = $1 "\t" $2; cnt[key]++
              if (!(key in ex)) { ex[key] = $4; red[key] = $3 } }
            END { for (k in cnt) print cnt[k] "\t" k "\t" red[k] "\t" ex[k] }
        ' "$ALIVE_TSV" | sort -rn > "$ALIVE_DEDUP"
        unique_miscomps=$(wc -l < "$ALIVE_DEDUP") || unique_miscomps=0

        printf "  %-26s  %-40s  %-7s  %s\n" "FUNCTION" "ERROR" "REDUCED" "EXAMPLE"
        printf "  %-26s  %-40s  %-7s  %s\n" "--------" "-----" "-------" "-------"
        while IFS=$'\t' read -r count func err red example; do
            printf "  %-26.26s  %-40.40s  %-7s  %s\n" "$func" "$err" "$red" "$example"
        done < "$ALIVE_DEDUP"
    fi
fi

# ────────── Section 5: Summary ──────────
hdr "Section 5: Summary"

total_uniq=$(( unique_crashes + unique_asan + unique_miscomps ))
printf "  %5d  fuzzer crashes  (unique by assertion)\n" "$unique_crashes"
printf "  %5d  ASAN findings   (unique by error type + top frame)\n" "$unique_asan"
printf "  %5d  miscompilations (unique by function + error)\n" "$unique_miscomps"
echo
printf "  %sTotal unique findings: %d%s\n" "$C_BOLD" "$total_uniq" "$C_RESET"
echo
