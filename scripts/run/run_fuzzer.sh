#!/usr/bin/env bash

source "$(dirname "$0")/../build/env.sh"

FUZZ_TARGET="$BUILD_OUT/opt_fuzz_target"
ALIVE_FUZZ_TARGET="$BUILD_OUT/opt_fuzz_target_alive2"
FUZZ_WORKDIR="$BUILD_OUT/workdir_$(date +%m%d%Y)"

echo "=== Running Centipede ==="

for f in "$FUZZ_TARGET" "$ALIVE_FUZZ_TARGET" "$CENTIPEDE_BIN"; do
    if [[ ! -f "$f" ]]; then
        echo "ERROR: $f not found" >&2
        exit 1
    fi
done

mkdir -p "$FUZZ_WORKDIR"

# Copy seeds into corpus dir so Centipede doesn't pollute our seeds
mkdir -p "$CORPUS_DIR"
if [[ -d "$SPLIT_SEEDS_DIR" ]] && [[ -z "$(ls -A "$CORPUS_DIR" 2>/dev/null)" ]]; then
    echo "[run] Copying seeds into corpus dir..."
    cp "$SPLIT_SEEDS_DIR"/* "$CORPUS_DIR/" 2>/dev/null || true
    echo "[run] Copied $(ls "$CORPUS_DIR" | wc -l) files"
fi

JOBS=${FUZZ_JOBS:-4}

echo "[run] Target:       $FUZZ_TARGET"
echo "[run] Alive2 extra: $ALIVE_FUZZ_TARGET"
echo "[run] Workdir:      $FUZZ_WORKDIR"
echo "[run] Jobs:         $JOBS"
echo "[run] Corpus:       $CORPUS_DIR"

FLAGS=(
    --binary="$FUZZ_TARGET"
    # --extra_binaries="$ALIVE_FUZZ_TARGET"
    --workdir="$FUZZ_WORKDIR"
    --j="$JOBS"
    --timeout_per_input=30
    --rss_limit_mb=0
    --address_space_limit_mb=0
    --corpus_dir="$CORPUS_DIR"
    --v=1
    --max_num_crash_reports=10000
)

echo "[run] $CENTIPEDE_BIN ${FLAGS[*]} $*"
echo ""

ulimit -s unlimited
"$CENTIPEDE_BIN" "${FLAGS[@]}" "$@"