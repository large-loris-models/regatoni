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
    echo "  backend-tv FAILS (candidate isel bugs): $fails"
    if [ "$fails" -gt 0 ]; then
      find "$RD"/oracle_results/backend_tv_riscv64_*/fail -maxdepth 1 -type f \
        ! -name '*.hash' ! -name '*.log' 2>/dev/null | sed 's#.*/oracle_results/#      #'
    fi

    crashes=$(find "$RD/workdir" -path '*crashes.*' -type f ! -name '*.sig' ! -name '*.desc' 2>/dev/null | wc -l)
    echo "  codegen crashes (triage; mostly slow/timeout, not all bugs): $crashes"
    echo "  disk free: $(df -h "$PROJECT_ROOT" | awk 'NR==2{print $4}')   mem: $(free -g 2>/dev/null | awk '/Mem:/{print $3"G/"$2"G used"}')   load:$(cut -d' ' -f1-3 /proc/loadavg)"
  fi
} >> "$LOG" 2>&1
