#!/usr/bin/env bash
# Post-mortem analysis for a completed/crashed fuzzer run.
#
# Unlike status.sh (which expects a live fuzzer and reads PID files),
# this script analyzes a workdir after the fuzzer has stopped.
#
# Usage: ./scripts/analysis/postmortem.sh <workdir_path>
# Example: ./scripts/analysis/postmortem.sh build/workdir_0

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null
# env.sh enables errexit; we don't want that here — head/grep pipelines close
# pipes early and would otherwise trip -e under pipefail.
set +e
set -uo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <workdir_path>" >&2
    exit 2
fi

WORKDIR="$1"
if [[ ! -d "$WORKDIR" ]]; then
    echo "ERROR: workdir not found: $WORKDIR" >&2
    exit 1
fi
WORKDIR="$(cd "$WORKDIR" && pwd)"

# Oracle results live alongside the workdir in $BUILD_OUT/oracle_results.
ORACLE_ROOT="$BUILD_OUT/oracle_results"
ASAN_DIR="$ORACLE_ROOT/asan_opt"
mapfile -t ALIVE_DIRS < <(
    if [[ -d "$ORACLE_ROOT" ]]; then
        find "$ORACLE_ROOT" -maxdepth 1 -type d \
            \( -name 'alive_tv' -o -name 'alive_tv_*' \) | sort
    fi
)

# Locate the Centipede run log. Search candidates in order.
RUN_LOG=""
for candidate in \
    "$WORKDIR/run.log" \
    "$BUILD_OUT/run_state/run.log" \
    "$BUILD_OUT/run.log"; do
    if [[ -f "$candidate" ]]; then
        RUN_LOG="$candidate"
        break
    fi
done

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

if [[ -t 1 ]]; then
    C_BOLD=$'\e[1m'; C_DIM=$'\e[2m'; C_RESET=$'\e[0m'
    C_HDR=$'\e[1;36m'; C_GRN=$'\e[32m'; C_RED=$'\e[31m'; C_YEL=$'\e[33m'
else
    C_BOLD=''; C_DIM=''; C_RESET=''; C_HDR=''; C_GRN=''; C_RED=''; C_YEL=''
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

# ─── helpers duplicated from scripts/analysis/status.sh ───
# Keep these in sync if status.sh changes.

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

    if [[ "$summary" =~ ^SUMMARY:\ [A-Za-z]+:\ ([A-Za-z][A-Za-z0-9_-]+)\ .*\ in\ (.+)$ ]]; then
        printf '%s\t%s\t%s\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "$base"
        return
    fi

    if [[ "$summary" == *" leaked"* ]]; then
        frame=$(grep -E '^[[:space:]]+#[0-9]+ 0x[0-9a-f]+ in ' "$f" \
            | sed -E '
                s|^[[:space:]]+#[0-9]+ 0x[0-9a-f]+ in ||
                s| /[^ ]+:[0-9]+(:[0-9]+)?$||
                s| \(/[^)]*\) \(BuildId: [^)]*\)$||
              ' \
            | grep -vE '^(operator new|operator delete|malloc|calloc|realloc|__interceptor_|llvm::User::operator new|llvm::User::allocHungoffUses|allocHungoffUses|Create|PHINode)' \
            | head -1)
        frame=$(printf '%s' "${frame:-?}" | sed -E 's/\(.*$//')
        printf '%s\t%s\t%s\n' "memory-leak" "${frame:-?}" "$base"
        return
    fi

    if [[ "$summary" =~ ^SUMMARY:\ [A-Za-z]+:\ ([A-Za-z][A-Za-z0-9_-]+)\ (.+)$ ]]; then
        loc="${BASH_REMATCH[2]}"
        loc="${loc%"${loc##*[![:space:]]}"}"
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

    local base_stem
    base_stem=$(basename "$f" .ll.log)
    local reduced_candidate="$PROJECT_ROOT/miscompilations/${base_stem}.reduced.ll"
    [[ -f "$reduced_candidate" ]] && reduced="yes"

    printf '%s\t%s\t%s\t%s\n' "${func:-?}" "${err:-?}" "$reduced" "$base"
}

# ────────── Section 1: Run Summary ──────────
hdr "Section 1: Run Summary"

printf "  Workdir:       %s\n" "$WORKDIR"

if [[ -z "$RUN_LOG" ]]; then
    echo "  run log not found (looked in workdir, run_state/, build/)"
    end_mode="unknown"
else
    printf "  Run log:       %s\n" "$RUN_LOG"
fi

# Derive start/end times from file mtimes inside the workdir rather than
# the run log. The log's tail can be truncated on crash, so its last
# timestamp often lags behind the real end of fuzzing. Files that move
# throughout a run (crashes.*/*, opt_fuzz_target-*/features.*, etc.)
# give an authoritative "last activity" signal.
first_ts=""
last_ts=""
oldest_epoch=$(find "$WORKDIR" -mindepth 1 -printf '%T@\n' 2>/dev/null \
               | sort -n | head -1 | cut -d. -f1)
newest_epoch=$(find "$WORKDIR" -mindepth 1 -printf '%T@\n' 2>/dev/null \
               | sort -n | tail -1 | cut -d. -f1)
[[ -n "$oldest_epoch" ]] && first_ts=$(date -d "@$oldest_epoch" -Iseconds 2>/dev/null || echo "")
[[ -n "$newest_epoch" ]] && last_ts=$(date -d "@$newest_epoch"  -Iseconds 2>/dev/null || echo "")

if [[ -n "$first_ts" ]]; then
    printf "  Started:       %s\n" "$first_ts"
fi
if [[ -n "$last_ts" ]]; then
    printf "  Last activity: %s\n" "$last_ts"
fi
if [[ -n "$first_ts" && -n "$last_ts" ]]; then
    fe=$(date -d "$first_ts" +%s 2>/dev/null || echo 0)
    le=$(date -d "$last_ts"  +%s 2>/dev/null || echo 0)
    if (( fe > 0 && le >= fe )); then
        printf "  Runtime:       %s\n" "$(fmt_dur $((le - fe)))"
    fi
fi

if [[ -n "$RUN_LOG" ]]; then
    # Classify how the run ended. Scan the tail for signals.
    tail_snippet=$(tail -n 50 "$RUN_LOG" 2>/dev/null)
    end_mode="unknown"
    end_color="$C_YEL"
    if grep -qiE 'SIGSEGV|signal 11|segmentation fault|terminate called|assertion.*fail|AddressSanitizer:|UndefinedBehaviorSanitizer:|fatal error|\bCHECK\b.*failed|abort\(\)' <<<"$tail_snippet"; then
        end_mode="crashed"
        end_color="$C_RED"
    elif grep -qiE 'signal 15|signal 9|SIGTERM|SIGKILL|killed by signal|\[stop\]|Terminated' <<<"$tail_snippet"; then
        end_mode="killed"
        end_color="$C_YEL"
    elif grep -qiE 'shutdown|shutting down|reached --num_runs|Exiting\.|Done\.|done fuzzing' <<<"$tail_snippet"; then
        end_mode="clean shutdown"
        end_color="$C_GRN"
    fi
    printf "  Ended:         %s%s%s\n" "$end_color" "$end_mode" "$C_RESET"

    latest=$(grep -oE 'ft: [0-9]+ cov: [0-9]+ cnt: [0-9]+ cmp: [0-9]+' "$RUN_LOG" | tail -1)
    if [[ -n "$latest" ]]; then
        read -r _ ft _ cov _ cnt _ cmp <<<"$latest"
        printf "  Final coverage: ft=%s cov=%s cnt=%s cmp=%s\n" "$ft" "$cov" "$cnt" "$cmp"
    fi

    if [[ "$end_mode" == "crashed" ]]; then
        echo
        printf "  %sLast 20 log lines (crash context):%s\n" "$C_DIM" "$C_RESET"
        tail -n 20 "$RUN_LOG" | sed 's/^/    /'
    fi
fi

# ────────── Section 2: Corpus Stats ──────────
hdr "Section 2: Corpus Stats"

if [[ ! -d "$CORPUS_DIR" ]]; then
    echo "  corpus dir not found: $CORPUS_DIR"
else
    corpus_count=$(find "$CORPUS_DIR" -maxdepth 1 -type f 2>/dev/null | wc -l)
    printf "  Corpus dir:    %s\n" "$CORPUS_DIR"
    printf "  Entries:       %d\n" "$corpus_count"
    if (( corpus_count > 0 )); then
        read -r sz_min sz_med sz_max sz_total < <(
            find "$CORPUS_DIR" -maxdepth 1 -type f -printf '%s\n' 2>/dev/null \
            | sort -n \
            | awk '
                { a[NR] = $1; total += $1 }
                END {
                    if (NR == 0) { print "0 0 0 0"; exit }
                    med = (NR % 2) ? a[(NR+1)/2] : int((a[NR/2] + a[NR/2+1]) / 2)
                    print a[1], med, a[NR], total
                }'
        )
        printf "  Size bytes:    min=%s  median=%s  max=%s  total=%s\n" \
            "$sz_min" "$sz_med" "$sz_max" "$sz_total"
    fi
fi

# Also surface Centipede's per-shard crash directories (distinct from fuzzer
# assertion counts below — these are raw crash inputs stored in the workdir).
shard_crashes=0
for d in "$WORKDIR"/crashes.*; do
    [[ -d "$d" ]] || continue
    shard_crashes=$(( shard_crashes + $(find "$d" -maxdepth 1 -type f 2>/dev/null | wc -l) ))
done
if (( shard_crashes > 0 )); then
    printf "  Crash inputs in workdir: %d files across crashes.* shards\n" "$shard_crashes"
fi

# ────────── Section 3: Unique Crashes ──────────
hdr "Section 3: Unique Crashes (fuzzer)"

CRASHES_TSV="$TMP/crashes.tsv"
: > "$CRASHES_TSV"
total_crashes=0
unique_crashes=0
if [[ -n "$RUN_LOG" && -f "$RUN_LOG" ]]; then
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

# ────────── Section 4: ASAN Findings ──────────
hdr "Section 4: ASAN Oracle Findings"

unique_asan=0
if [[ ! -d "$ASAN_DIR" ]]; then
    echo "  not started (no $ASAN_DIR)"
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

# ────────── Section 5: Alive-TV Miscompilations ──────────
hdr "Section 5: Alive-TV Miscompilations"

unique_miscomps=0
if (( ${#ALIVE_DIRS[@]} == 0 )); then
    echo "  not started (no alive_tv* under $ORACLE_ROOT)"
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

# ────────── Roll-up ──────────
hdr "Summary"

total_uniq=$(( unique_crashes + unique_asan + unique_miscomps ))
printf "  %5d  fuzzer crashes  (unique by assertion)\n" "$unique_crashes"
printf "  %5d  ASAN findings   (unique by error type + top frame)\n" "$unique_asan"
printf "  %5d  miscompilations (unique by function + error)\n" "$unique_miscomps"
echo
printf "  %sTotal unique findings: %d%s\n" "$C_BOLD" "$total_uniq" "$C_RESET"
echo
