#!/usr/bin/env bash
# Render dashboard.html for a historical run on-demand.
# Usage: dashboard_now.sh <run_id_or_path>
set -eu
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
arg="${1:?usage: dashboard_now.sh <run_id_or_path>}"
if [[ -d "$arg" ]]; then run_dir="$arg"; else run_dir="$PROJECT_ROOT/runs/$arg"; fi
exec python3 "$SCRIPT_DIR/render_dashboard.py" --run-dir "$run_dir" --out "$run_dir/dashboard.html"
