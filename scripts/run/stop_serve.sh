#!/usr/bin/env bash
# Stop the dashboard http server started by serve_dashboard.sh.
set -u

PID_FILE="/tmp/regatoni_dashboard_http.pid"

if [[ ! -f "$PID_FILE" ]]; then
    echo "No pid file at $PID_FILE — nothing to stop."
    exit 0
fi

PID="$(cat "$PID_FILE" 2>/dev/null || true)"
if [[ -z "$PID" ]]; then
    rm -f "$PID_FILE"
    echo "Empty pid file — removed."
    exit 0
fi

if kill -0 "$PID" 2>/dev/null; then
    kill -TERM "$PID" 2>/dev/null || true
    deadline=$(( $(date +%s) + 5 ))
    while kill -0 "$PID" 2>/dev/null && (( $(date +%s) < deadline )); do
        sleep 0.5
    done
    if kill -0 "$PID" 2>/dev/null; then
        kill -KILL "$PID" 2>/dev/null || true
        echo "Force-killed dashboard server (PID $PID)."
    else
        echo "Dashboard server (PID $PID) stopped."
    fi
else
    echo "PID $PID not running."
fi

rm -f "$PID_FILE"
