#!/usr/bin/env bash
# Box-local campaign check-in. Appends a compact status snapshot of the active
# fuzzing run to campaign_checkin.log. Meant to run from cron, so it works whether
# or not anyone is logged in (a /schedule cloud agent can't see this box's local
# runs/). Remove with:  crontab -l | grep -v campaign_checkin | crontab -
PROJECT_ROOT=/data/saiva/regatoni
LOG="$PROJECT_ROOT/campaign_checkin.log"

RD="$(readlink -f "$PROJECT_ROOT/runs/current" 2>/dev/null)"
[ -d "$RD" ] || RD="$(ls -1dt "$PROJECT_ROOT"/runs/2026-* 2>/dev/null | head -1)"

ts="$(date -Is)"
{
  echo "================ $ts ================"
  if [ -z "$RD" ] || [ ! -d "$RD" ]; then
    echo "  no active run dir found"
  else
    echo "  run: $(basename "$RD")"
    if pgrep -x centipede >/dev/null; then echo "  fuzzer: UP"; else echo "  fuzzer: DOWN (!)"; fi
    echo "  corpus entries: $(find "$RD/corpus" -maxdepth 1 -type f 2>/dev/null | wc -l)"

    checked=0
    for c in "$RD"/oracle_results/backend_tv_riscv64_*/checked.log; do
      [ -f "$c" ] && checked=$((checked + $(wc -l < "$c")))
    done
    echo "  backend-tv checked: $checked"

    fails=$(find "$RD"/oracle_results/backend_tv_riscv64_*/fail -maxdepth 1 -type f \
              ! -name '*.hash' ! -name '*.log' 2>/dev/null | wc -l)
    # We ONLY care about miscompilations. Split by backend-tv VERDICT first:
    #   "Value mismatch"               -> real miscompile (surface every one)
    #   "Source is more defined ..."   -> refinement artifact (count only; the
    #                                     >=20-arg stack-arg lifter FP lives here,
    #                                     see triage_candidates/backend-tv-manyargs-minimal.ll)
    # NB: do NOT pre-filter Value-mismatch by freeze/poison — confirmed SDAG bug 3
    # (ushl.sat oob + freeze) has both and is REAL. Verdict, not IR shape, decides.
    md=0; vm=0
    for f in "$RD"/oracle_results/backend_tv_riscv64_*/fail/*.log; do
      [ -f "$f" ] || continue
      if grep -q 'Value mismatch' "$f"; then vm=$((vm+1)); fi
      if grep -q 'more defined than target' "$f"; then md=$((md+1)); fi
    done
    echo "  backend-tv FAILS: $fails   (Value-mismatch=$vm  more-defined-ARTIFACT=$md)"
    if [ "$vm" -gt 0 ]; then
      echo "  --- Value-mismatch miscompiles (triage these) ---"
      for f in "$RD"/oracle_results/backend_tv_riscv64_*/fail/*.log; do
        [ -f "$f" ] || continue
        grep -q 'Value mismatch' "$f" || continue
        ir="${f%.log}"
        tag="CANDIDATE"
        # known GISel mul_by_neg_one (mul nuw x, -1) — incl. computed -1 via
        # `or x,-1` / `sext iN -1`. Re-found constantly; see regatoni-gisel-mul-nuw-bug.
        if grep -q 'mul nuw' "$ir" 2>/dev/null && \
           grep -qE 'mul nuw[^,]*, -1|or i[0-9]+ %[^,]*, -1|sext i[0-9]+ -1' "$ir" 2>/dev/null; then
          tag="known-nuw?"
        fi
        echo "      [$tag] $(echo "$ir" | sed 's#.*/oracle_results/##')  seed=$(grep -m1 source_filename "$ir" 2>/dev/null | sed 's#.*/##;s/"//')"
      done
    fi

    crashes=$(find "$RD/workdir" -path '*crashes.*' -type f ! -name '*.sig' ! -name '*.desc' 2>/dev/null | wc -l)
    echo "  codegen crashes (triage; mostly slow/timeout, not all bugs): $crashes"
    echo "  disk free: $(df -h "$PROJECT_ROOT" | awk 'NR==2{print $4}')   mem: $(free -g 2>/dev/null | awk '/Mem:/{print $3"G/"$2"G used"}')   load:$(cut -d' ' -f1-3 /proc/loadavg)"
  fi
} >> "$LOG" 2>&1
