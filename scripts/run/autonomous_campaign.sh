#!/usr/bin/env bash
# Autonomous ~9h campaign (fits a 10h window with buffer):
#   Phase 1: steering A/B on the distilled corpus — REGATONI_IR_FEATURES=1 vs 0,
#            2h each, sequential (full box).
#   Phase 2: adaptive production run (4h) on the winning steering config.
#   Phase 3: triage every run -> distinct reduced roots, flag any NEW? (5th bug).
# Writes triage_candidates/autonomous_report.txt. All runs use the distilled
# corpus (it won the corpus A/B); steering is the variable under test.
set -u
cd /data/saiva/regatoni
SEEDS=distilled_int_seeds
AB_DUR="${AB_DUR:-7200}"        # 2h per A/B arm
PROD_DUR="${PROD_DUR:-14400}"   # 4h production
PROG=/tmp/autonomous.log
REPORT=triage_candidates/autonomous_report.txt
: > "$PROG"
log(){ echo "[$(date -Is)] $*" | tee -a "$PROG"; }

launch(){ # $1=seeds $2=IR_FEATURES(0|1) -> prints RUN_ID
  REGATONI_IR_FEATURES="$2" FUZZ_TARGET_KIND=codegen ORACLE_SET=backend_only \
  BACKEND_TV_ENABLE=1 BACKEND_TV_SHARDS=4 FUZZER_CORES=6 \
    nohup bash scripts/run/start.sh --seeds "$1" >/tmp/auto_launch.log 2>&1 &
  sleep 25
  readlink runs/current
}
stoprun(){ bash scripts/run/stop.sh "$1" >/dev/null 2>&1 || true; sleep 5; }
vmcount(){ grep -rl 'Value mismatch' "runs/$1"/oracle_results/backend_tv_riscv64_*/fail/ 2>/dev/null | wc -l; }
mdcount(){ grep -rl 'more defined than target' "runs/$1"/oracle_results/backend_tv_riscv64_*/fail/ 2>/dev/null | wc -l; }
alive(){ pgrep -x centipede >/dev/null && echo UP || echo DOWN; }

# ── Phase 1: steering A/B ────────────────────────────────────────────────────
log "PHASE 1: steering A/B on $SEEDS, ${AB_DUR}s/arm"
ON="$(launch "$SEEDS" 1)";  log "arm ON  = $ON  (centipede $(alive))"
sleep "$AB_DUR"; stoprun "$ON"
OFF="$(launch "$SEEDS" 0)"; log "arm OFF = $OFF (centipede $(alive))"
sleep "$AB_DUR"; stoprun "$OFF"
on_vm="$(vmcount "$ON")"; off_vm="$(vmcount "$OFF")"
log "A/B result: steering-ON vm=$on_vm md=$(mdcount "$ON")  |  steering-OFF vm=$off_vm md=$(mdcount "$OFF")"

# ── Phase 2: adaptive production run on the winning config ───────────────────
if [ "${on_vm:-0}" -ge "${off_vm:-0}" ]; then BEST=1; else BEST=0; fi
log "PHASE 2: production run IR_FEATURES=$BEST on $SEEDS, ${PROD_DUR}s"
PROD="$(launch "$SEEDS" "$BEST")"; log "prod = $PROD (centipede $(alive))"
sleep "$PROD_DUR"; stoprun "$PROD"
log "production vm=$(vmcount "$PROD") md=$(mdcount "$PROD")"

# ── Phase 3: triage all runs -> distinct roots, flag NEW? ────────────────────
log "PHASE 3: triage (reduce + classify)"
for R in "$ON" "$OFF" "$PROD"; do
  [ -n "$R" ] || continue
  python3 scripts/analysis/triage_fails.py --run "runs/$R" --recorded value-mismatch \
    --jobs 8 --max-reduce 30 --out "triage_candidates/auto_roots_$R.txt" >>"$PROG" 2>&1 || true
done

{
  echo "# Autonomous campaign report  ($(date -Is))"
  echo
  echo "## Phase 1 — steering A/B (distilled corpus, $((AB_DUR/3600))h/arm)"
  echo "   steering ON : run $ON   value-mismatch=$on_vm   more-defined=$(mdcount "$ON")"
  echo "   steering OFF: run $OFF   value-mismatch=$off_vm   more-defined=$(mdcount "$OFF")"
  echo "   winner -> IR_FEATURES=$BEST"
  echo
  echo "## Phase 2 — production (IR_FEATURES=$BEST, $((PROD_DUR/3600))h): run $PROD"
  echo "   value-mismatch=$(vmcount "$PROD")   more-defined=$(mdcount "$PROD")"
  echo
  echo "## Distinct reduced ROOTS per run"
  for R in "$ON" "$OFF" "$PROD"; do
    [ -n "$R" ] || continue
    echo "  --- run $R ---"
    grep -E 'distinct ROOT|^===== ' "triage_candidates/auto_roots_$R.txt" 2>/dev/null | sed 's/^/    /'
  done
  echo
  echo "## *** NEW? roots (candidate 5th bug — inspect!) ***"
  if grep -hq 'NEW?' triage_candidates/auto_roots_*.txt 2>/dev/null; then
    grep -hA6 '\[NEW?\]' triage_candidates/auto_roots_*.txt 2>/dev/null
  else
    echo "   none — all roots are re-finds of bugs 1-4"
  fi
} > "$REPORT" 2>&1
log "DONE. Report: $REPORT"
cat "$REPORT" | tee -a "$PROG"
