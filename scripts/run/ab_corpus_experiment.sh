#!/usr/bin/env bash
# Unattended A/B: does the feature-distilled corpus find bugs faster than the
# full corpus, equal time, full box each (sequential — no core contention)?
#
#   Arm A: distilled_int_seeds (3186, set-cover of the tuple universe)
#   Arm B: combined_int_seeds  (19082, the full corpus)
#
# Identical config otherwise (codegen target, both selectors, backend-tv oracle).
# Writes triage_candidates/ab_corpus_report.txt at the end.
set -u
cd /data/saiva/regatoni
DUR="${DUR:-7200}"                       # seconds per arm (default 2h)
REPORT=/data/saiva/regatoni/triage_candidates/ab_corpus_report.txt
PROG=/tmp/ab_experiment.log
: > "$PROG"
log(){ echo "[$(date -Is)] $*" | tee -a "$PROG"; }

launch(){ # $1=seed dir -> prints RUN_ID on stdout
  FUZZ_TARGET_KIND=codegen ORACLE_SET=backend_only BACKEND_TV_ENABLE=1 \
  BACKEND_TV_SHARDS=4 FUZZER_CORES=6 \
    nohup bash scripts/run/start.sh --seeds "$1" >/tmp/ab_arm_launch.log 2>&1 &
  sleep 25
  readlink runs/current
}

log "=== A/B corpus experiment: DUR=${DUR}s per arm ==="
log "Arm A (distilled) launching..."
A="$(launch distilled_int_seeds)"; log "Arm A run = $A"
sleep "$DUR"
log "Stopping Arm A ($A)..."; bash scripts/run/stop.sh "$A" >/dev/null 2>&1 || true
sleep 5

log "Arm B (full) launching..."
B="$(launch combined_int_seeds)"; log "Arm B run = $B"
sleep "$DUR"
log "Stopping Arm B ($B)..."; bash scripts/run/stop.sh "$B" >/dev/null 2>&1 || true
sleep 5

log "Comparing -> $REPORT"
python3 scripts/analysis/ab_compare.py "$A" "$B" --dur "$DUR" > "$REPORT" 2>>"$PROG" || true
log "=== DONE. Report: $REPORT ==="
cat "$REPORT" | tee -a "$PROG"
