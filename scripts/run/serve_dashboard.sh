#!/usr/bin/env bash
# Idempotent helper: start a python http.server in the background that serves
# the configured dashboard publish dir. Safe to run repeatedly.
#
# Listens on $DASHBOARD_HTTP_PORT, serves $DASHBOARD_PUBLISH_DIR.
# PID is recorded to /tmp/regatoni_dashboard_http.pid; logs go to
# /tmp/regatoni_dashboard_http.log.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../build/env.sh" >/dev/null

PORT="${DASHBOARD_HTTP_PORT:-8080}"
PUB_DIR="${DASHBOARD_PUBLISH_DIR:-/data/saiva/public-reports/regatoni}"
PID_FILE="/tmp/regatoni_dashboard_http.pid"
LOG_FILE="/tmp/regatoni_dashboard_http.log"

# Already-listening check: any process listening on $PORT.
if ss -ltn 2>/dev/null | awk '{print $4}' | grep -qE ":${PORT}\$"; then
    echo "Dashboard server already running on port $PORT."
    if [[ -f "$PID_FILE" ]]; then
        echo "  pid file: $PID_FILE  ($(cat "$PID_FILE" 2>/dev/null))"
    fi
    exit 0
fi

mkdir -p "$PUB_DIR"

# Start http.server in the background. Force .html → text/html; charset=utf-8
# (default mimetype mapping in Python's http.server omits the charset).
nohup python3 -c "
import http.server, socketserver, sys, os
class H(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      '.html': 'text/html; charset=utf-8'}
class S(socketserver.TCPServer):
    allow_reuse_address = True  # must be a class attr; set before bind
PORT = int(sys.argv[1])
DIR  = sys.argv[2]
os.chdir(DIR)
with S(('', PORT), H) as httpd:
    httpd.serve_forever()
" "$PORT" "$PUB_DIR" >>"$LOG_FILE" 2>&1 </dev/null &
SERVER_PID=$!
echo "$SERVER_PID" > "$PID_FILE"

# Verify it's actually accepting connections.
ok=0
for _ in 1 2 3 4 5; do
    if curl -fsS -I "http://localhost:${PORT}/" >/dev/null 2>&1; then
        ok=1; break
    fi
    sleep 1
done

if (( ok != 1 )); then
    echo "ERROR: dashboard server did not respond on port $PORT after 5s." >&2
    echo "  log: $LOG_FILE" >&2
    exit 1
fi

HOST="$(hostname -f 2>/dev/null || hostname)"
echo "Dashboard server running. Bookmark: http://${HOST}:${PORT}/dashboard.html"
echo "  pid: $SERVER_PID  ($PID_FILE)"
echo "  serving: $PUB_DIR"
echo "  log: $LOG_FILE"
