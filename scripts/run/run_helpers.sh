#!/usr/bin/env bash
# Helpers for per-run directory layout under runs/<RUN_ID>/.
#
# Each fuzzing run gets its own directory containing all of its state.
# Cross-run state (dedup.db, the seed corpus pool) lives at the project root.
#
# Sourced by scripts/run/start.sh and scripts/run/stop.sh.

# shellcheck disable=SC2155

# Resolve project root from this file's location so the helpers work whether
# sourced via $SCRIPT_DIR or via a direct path.
_RUN_HELPERS_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$_RUN_HELPERS_SCRIPT_DIR/../.." && pwd)}"

REGATONI_RUNS_DIR="$PROJECT_ROOT/runs"

# Pick the next RUN_ID, appending _<n> on collision (n starts at 2).
_regatoni_pick_run_id() {
    local base="$1"
    if [[ ! -e "$REGATONI_RUNS_DIR/$base" ]]; then
        printf '%s' "$base"
        return
    fi
    local n=2
    while [[ -e "$REGATONI_RUNS_DIR/${base}_${n}" ]]; do
        n=$((n + 1))
    done
    printf '%s' "${base}_${n}"
}

# regatoni_init_run_dir [seed_source]
#
# Generates RUN_ID, creates the per-run directory tree, writes manifest.json,
# and atomically points runs/current at this run. Sets and exports RUN_ID.
regatoni_init_run_dir() {
    local seed_source="${1:-}"

    local utc_date short_sha git_commit git_dirty
    utc_date="$(date -u +%Y-%m-%d)"
    short_sha="$(git -C "$PROJECT_ROOT" rev-parse --short=8 HEAD)"
    git_commit="$(git -C "$PROJECT_ROOT" rev-parse HEAD)"
    if [[ -n "$(git -C "$PROJECT_ROOT" status --porcelain 2>/dev/null)" ]]; then
        git_dirty=true
    else
        git_dirty=false
    fi

    mkdir -p "$REGATONI_RUNS_DIR"
    local base="${utc_date}_${short_sha}"
    RUN_ID="$(_regatoni_pick_run_id "$base")"
    export RUN_ID

    local run_dir="$REGATONI_RUNS_DIR/$RUN_ID"
    mkdir -p "$run_dir"/{workdir,corpus,miscompilations,triage,stats}

    # harness_binary_sha: sha256 of the fuzz target if the binary is present.
    local harness="$PROJECT_ROOT/build/opt_fuzz_target"
    local harness_sha=""
    if [[ -f "$harness" ]]; then
        harness_sha="$(sha256sum "$harness" | awk '{print $1}')"
    fi

    local start_time
    start_time="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

    RUN_ID="$RUN_ID" \
    START_TIME="$start_time" \
    GIT_COMMIT="$git_commit" \
    GIT_DIRTY="$git_dirty" \
    SEED_SOURCE="$seed_source" \
    HARNESS_SHA="$harness_sha" \
    MANIFEST_PATH="$run_dir/manifest.json" \
    python3 - <<'PYEOF'
import json, os
manifest = {
    "run_id": os.environ["RUN_ID"],
    "start_time": os.environ["START_TIME"],
    "end_time": None,
    "git_commit": os.environ["GIT_COMMIT"],
    "git_dirty": os.environ["GIT_DIRTY"] == "true",
    "centipede_flags": [],
    "seed_source": os.environ["SEED_SOURCE"],
    "harness_binary_sha": os.environ["HARNESS_SHA"],
}
with open(os.environ["MANIFEST_PATH"], "w") as f:
    json.dump(manifest, f, indent=2)
    f.write("\n")
PYEOF

    # Atomic symlink swap: write to .current.tmp, rename onto current.
    rm -f "$REGATONI_RUNS_DIR/.current.tmp"
    ln -s "$RUN_ID" "$REGATONI_RUNS_DIR/.current.tmp"
    mv -Tf "$REGATONI_RUNS_DIR/.current.tmp" "$REGATONI_RUNS_DIR/current"
}

# regatoni_finalize_run_dir [RUN_ID]
#
# Stamps end_time into the run's manifest.json and removes runs/current iff
# it points at this run.
regatoni_finalize_run_dir() {
    local rid="${1:-${RUN_ID:-}}"
    if [[ -z "$rid" ]]; then
        echo "regatoni_finalize_run_dir: no RUN_ID" >&2
        return 1
    fi
    local run_dir="$REGATONI_RUNS_DIR/$rid"
    local manifest="$run_dir/manifest.json"
    if [[ ! -f "$manifest" ]]; then
        echo "regatoni_finalize_run_dir: manifest missing: $manifest" >&2
        return 1
    fi
    local end_time
    end_time="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    MANIFEST_PATH="$manifest" END_TIME="$end_time" python3 - <<'PYEOF'
import json, os
path = os.environ["MANIFEST_PATH"]
with open(path) as f:
    m = json.load(f)
m["end_time"] = os.environ["END_TIME"]
with open(path, "w") as f:
    json.dump(m, f, indent=2)
    f.write("\n")
PYEOF
    # Drop runs/current only if it still points at this run.
    if [[ -L "$REGATONI_RUNS_DIR/current" ]]; then
        local target
        target="$(readlink "$REGATONI_RUNS_DIR/current")"
        if [[ "$target" == "$rid" ]]; then
            rm -f "$REGATONI_RUNS_DIR/current"
        fi
    fi
}

# regatoni_run_dir [RUN_ID]
#
# Echoes the absolute path to the run dir. With no arg, resolves runs/current.
regatoni_run_dir() {
    local rid="${1:-}"
    if [[ -z "$rid" ]]; then
        if [[ ! -L "$REGATONI_RUNS_DIR/current" ]]; then
            echo "regatoni_run_dir: runs/current does not exist" >&2
            return 1
        fi
        rid="$(readlink "$REGATONI_RUNS_DIR/current")"
    fi
    printf '%s\n' "$REGATONI_RUNS_DIR/$rid"
}

# regatoni_manifest_set <key> <json_value>
#
# Updates a single field in the current run's manifest.json. <json_value> is
# a JSON-encoded value (e.g. a quoted string or a JSON array).
regatoni_manifest_set() {
    local key="$1"
    local json_value="$2"
    local rid="${RUN_ID:-}"
    if [[ -z "$rid" ]]; then
        echo "regatoni_manifest_set: RUN_ID unset" >&2
        return 1
    fi
    local manifest="$REGATONI_RUNS_DIR/$rid/manifest.json"
    KEY="$key" VAL="$json_value" MANIFEST_PATH="$manifest" python3 - <<'PYEOF'
import json, os
path = os.environ["MANIFEST_PATH"]
with open(path) as f:
    m = json.load(f)
m[os.environ["KEY"]] = json.loads(os.environ["VAL"])
with open(path, "w") as f:
    json.dump(m, f, indent=2)
    f.write("\n")
PYEOF
}
