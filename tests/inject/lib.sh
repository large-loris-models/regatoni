#!/usr/bin/env bash
# Shared helpers for spec-inject test scripts. Sourced; not executed
# directly. Pattern follows tests/rewrite/lib.sh.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$REPO_ROOT/scripts/build/env.sh" >/dev/null
BIN="$REPO_ROOT/build/spec-inject"
OPT="$LLVM_BUILD_PLAIN/bin/opt"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
PASS_FILE="$TMP/.pass"; FAIL_FILE="$TMP/.fail"
: > "$PASS_FILE"; : > "$FAIL_FILE"
_bump() { printf x >> "$1"; }
_count() { wc -c <"$1" | tr -d ' '; }

# stage_litmus DIR FILE ... — copy litmus fixtures into a single litmus dir.
stage_dir() {
  local dst="$1"; shift
  mkdir -p "$dst"
  local f
  for f in "$@"; do
    cp "$f" "$dst/"
  done
}

# run_inject LITMUS_DIR HOST_DIR OUT_DIR [extra args...] — runs the binary.
run_inject() {
  local litmus="$1"; local hosts="$2"; local out="$3"; shift 3
  mkdir -p "$out"
  "$BIN" --litmus-dir "$litmus" --host-dir "$hosts" --output-dir "$out" \
         --quiet "$@" >/dev/null 2>"$TMP/stderr.log"
}

ok()   { _bump "$PASS_FILE"; printf "  PASS  %s\n" "$1" >&2; }
bad()  { _bump "$FAIL_FILE"; printf "  FAIL  %s\n" "$1" >&2; [[ $# -gt 1 ]] && printf "        %s\n" "$2" >&2; }

# assert_file_exists DIR PATTERN LABEL — assert at least one file in DIR matches PATTERN.
assert_file_exists() {
  local dir="$1"; local pat="$2"; local label="$3"
  local hit
  hit="$(find "$dir" -maxdepth 1 -name "$pat" 2>/dev/null | head -1)"
  if [[ -z "$hit" ]]; then
    bad "$label" "no file matched $pat in $dir"
    return 1
  fi
  ok "$label"
  echo "$hit"
}

# assert_match OUTDIR PATTERN LABEL — assert some output file contains PATTERN (extended grep).
assert_match() {
  local out="$1"; local pat="$2"; local label="$3"
  local hit
  hit="$(grep -lE "$pat" "$out"/*.ll 2>/dev/null | head -1 || true)"
  if [[ -z "$hit" ]]; then
    bad "$label" "no output file matched /$pat/ in $out"
    return 1
  fi
  ok "$label"
  echo "$hit"
}

# assert_no_files OUTDIR LABEL — assert OUTDIR contains zero .ll files.
assert_no_files() {
  local out="$1"; local label="$2"
  local n
  n="$(find "$out" -maxdepth 1 -name '*.ll' 2>/dev/null | wc -l)"
  if [[ "$n" -ne 0 ]]; then
    bad "$label" "expected 0 files, got $n in $out"
    return 1
  fi
  ok "$label"
}

# assert_opt_clean OUTDIR LABEL — every .ll under OUTDIR passes opt -O2.
assert_opt_clean() {
  local out="$1"; local label="$2"
  local fail=0
  shopt -s nullglob
  local files=("$out"/*.ll)
  shopt -u nullglob
  if [[ ${#files[@]} -eq 0 ]]; then
    bad "$label" "no .ll files to test under $out"
    return 1
  fi
  for f in "${files[@]}"; do
    if ! "$OPT" -passes='default<O2>' -disable-output "$f" >/dev/null 2>"$TMP/opt.err"; then
      bad "$label" "opt -O2 failed on $f: $(head -1 "$TMP/opt.err")"
      fail=1
      break
    fi
  done
  [[ $fail -eq 0 ]] && ok "$label"
  return $fail
}

summarize() {
  local p f
  p="$(_count "$PASS_FILE")"; f="$(_count "$FAIL_FILE")"
  printf "\n  total: %d pass, %d fail\n" "$p" "$f"
  [[ "$f" -eq 0 ]]
}
