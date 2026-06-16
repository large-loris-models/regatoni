#!/usr/bin/env bash
# Single-phase steering hunt: distilled corpus + tuple-coverage steering ON,
# bounded duration, then auto-stop + triage to distinct roots (flag NEW?).
# Used to fill the tail of the autonomous window after the A/B validated steering.
set -u
cd /data/saiva/regatoni
DUR="${DUR:-4500}"   # 75 min default
PROG=/tmp/steering_hunt.log; : > "$PROG"
log(){ echo "[$(date -Is)] $*" | tee -a "$PROG"; }

REGATONI_IR_FEATURES=1 FUZZ_TARGET_KIND=codegen ORACLE_SET=backend_only \
BACKEND_TV_ENABLE=1 BACKEND_TV_SHARDS=4 FUZZER_CORES=6 \
  nohup bash scripts/run/start.sh --seeds distilled_int_seeds >/tmp/sh_launch.log 2>&1 &
sleep 25
R="$(readlink runs/current)"; log "steering hunt run = $R, ${DUR}s"
sleep "$DUR"
bash scripts/run/stop.sh "$R" >/dev/null 2>&1 || true; sleep 5
vm=$(grep -rl 'Value mismatch' "runs/$R"/oracle_results/backend_tv_riscv64_*/fail/ 2>/dev/null | wc -l)
log "stopped. value-mismatch=$vm — triaging"
python3 scripts/analysis/triage_fails.py --run "runs/$R" --recorded value-mismatch \
  --jobs 8 --max-reduce 30 --out "triage_candidates/steering_hunt_roots.txt" >>"$PROG" 2>&1 || true
echo "## NEW? roots:" | tee -a "$PROG"
grep -hA6 '\[NEW?\]' triage_candidates/steering_hunt_roots.txt 2>/dev/null | tee -a "$PROG" || echo "  none" | tee -a "$PROG"
log "DONE -> triage_candidates/steering_hunt_roots.txt"
