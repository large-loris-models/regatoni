#!/usr/bin/env bash
# Shared helpers for spec-mutate test scripts. Sourced; not executed
# directly. Provides a self-contained run + assert API so each test
# script reads top-down.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$REPO_ROOT/scripts/build/env.sh" >/dev/null
BIN="$REPO_ROOT/build/spec-mutate"
OPT="$LLVM_BUILD_PLAIN/bin/opt"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
# File-backed counters: assert helpers may run inside command substitution
# subshells, which would lose any plain-variable bump.
PASS_FILE="$TMP/.pass"; FAIL_FILE="$TMP/.fail"
: > "$PASS_FILE"; : > "$FAIL_FILE"
_bump() { printf x >> "$1"; }
_count() { wc -c <"$1" | tr -d ' '; }

# write_spec NAME JSON-CONTENT — writes a spec file under $TMP.
write_spec() {
  local name="$1"; shift
  local body="$1"; shift
  printf '%s\n' "$body" > "$TMP/$name.json"
  echo "$TMP/$name.json"
}

# run_rewrite SPEC INPUT_LL OUTDIR — runs the binary, swallows JSON
# summary, returns the binary's exit code.
run_rewrite() {
  local spec="$1"; local input="$2"; local out="$3"
  local input_dir
  input_dir="$(dirname "$input")"
  mkdir -p "$out"
  # We isolate one file by symlinking it into a private dir.
  local iso="$TMP/iso_$RANDOM"
  mkdir -p "$iso"
  ln -sf "$input" "$iso/"
  "$BIN" --spec "$spec" --input-dir "$iso" --output-dir "$out" \
         --quiet >/dev/null 2>"$TMP/stderr.log"
}

# pass/fail with a label. Status text goes to stderr so callers can
# capture command stdout (e.g., a path returned by assert_match) without
# concatenating PASS lines into the value.
ok()   { _bump "$PASS_FILE"; printf "  PASS  %s\n" "$1" >&2; }
bad()  { _bump "$FAIL_FILE"; printf "  FAIL  %s\n" "$1" >&2; [[ $# -gt 1 ]] && printf "        %s\n" "$2" >&2; }

# assert_match OUTDIR PATTERN LABEL — assert some output file contains
# PATTERN (extended grep). Returns the matching file path on stdout, or
# empty string and an error log.
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

# assert_no_match — opposite.
assert_no_match() {
  local out="$1"; local pat="$2"; local label="$3"
  local hit
  hit="$(grep -lE "$pat" "$out"/*.ll 2>/dev/null | head -1 || true)"
  if [[ -n "$hit" ]]; then
    bad "$label" "unwanted match in $hit"
    return 1
  fi
  ok "$label"
}

# assert_opt_clean OUTDIR LABEL — every .ll under OUTDIR passes opt -O2.
assert_opt_clean() {
  local out="$1"; local label="$2"
  local fail=0
  for f in "$out"/*.ll; do
    if ! "$OPT" -O2 -disable-output "$f" >/dev/null 2>"$TMP/opt.err"; then
      bad "$label" "opt -O2 failed on $f: $(head -1 "$TMP/opt.err")"
      fail=1
      break
    fi
  done
  [[ $fail -eq 0 ]] && ok "$label"
  return $fail
}

# summarize — exit-code carrier.
summarize() {
  local p f
  p="$(_count "$PASS_FILE")"; f="$(_count "$FAIL_FILE")"
  printf "\n  total: %d pass, %d fail\n" "$p" "$f"
  [[ "$f" -eq 0 ]]
}
