#!/usr/bin/env bash

# Usage: ./unique_crashes.sh [logfile]

LOG="${1:-out.log}"

if [[ ! -f "$LOG" ]]; then
    echo "Usage: $0 <logfile>" >&2
    exit 1
fi

echo "=== Unique crashes from $LOG ==="
echo ""

grep "CRASH LOG:.*opt_fuzz_target:" "$LOG" \
    | sed 's/.*opt_fuzz_target: //' \
    | sort -u \
    | while read -r msg; do
        COUNT=$(grep -cF "$msg" "$LOG")
        echo "[$COUNT hits] $msg"
    done \
    | sort -t'[' -k2 -rn

echo ""
TOTAL=$(grep -c "CRASH LOG:.*opt_fuzz_target:" "$LOG" 2>/dev/null || echo 0)
UNIQUE=$(grep "CRASH LOG:.*opt_fuzz_target:" "$LOG" | sed 's/.*opt_fuzz_target: //' | sort -u | wc -l)
echo "Total crashes: $TOTAL"
echo "Unique crashes: $UNIQUE"