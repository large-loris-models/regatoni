#!/usr/bin/env bash
#
# pass_probe.sh — wrapper around build/coverage_probe.
#
# Usage:
#   ./pass_probe.sh --pass=<filter> --list-reached <input.ll>
#   ./pass_probe.sh --pass=<filter> --check=<func>      <input.ll>
#   ./pass_probe.sh --pass=<filter> --diff <a.ll> <b.ll>
#
#   --pass=<filter>  substring used as the coverage_probe --filter (e.g.
#                    slp-vectorizer maps to SLPVectorizer; if the filter
#                    looks like an LLVM pass name with hyphens we strip
#                    them and capitalise hints).
#   --list-reached   print every function name reached, one per line
#   --check=<func>   exit 0 if <func> appears as a substring of any reached
#                    function name, exit 1 otherwise; no stdout
#   --diff a b       print "+ name" for names only-in-b, "- name" for
#                    names only-in-a, sorted

set -euo pipefail

source "$(dirname "$0")/../build/env.sh"

PROBE_BIN="${COVERAGE_PROBE_BIN:-$BUILD_OUT/coverage_probe}"
SYMBOLIZER="${LLVM_SYMBOLIZER_PATH:-$LLVM_BUILD_PLAIN/bin/llvm-symbolizer}"

if [[ ! -x "$PROBE_BIN" ]]; then
    echo "ERROR: coverage_probe not found at $PROBE_BIN — run scripts/build/build_coverage_probe.sh" >&2
    exit 2
fi
if [[ ! -x "$SYMBOLIZER" ]]; then
    echo "ERROR: llvm-symbolizer not found at $SYMBOLIZER" >&2
    exit 2
fi
export LLVM_SYMBOLIZER_PATH="$SYMBOLIZER"

# --pass=slp-vectorizer → SLPVectorizer; --pass=instcombine → InstCombine; etc.
# We already match by substring, so the cheapest reasonable normalisation is:
# strip hyphens, then preserve the user's casing if any uppercase is present;
# otherwise leave it alone — substring "slp" matches "SLPVectorizer" inside
# "llvm::SLPVectorizerPass" because llvm-symbolizer emits the demangled name.
# The caller can always pass a pre-cased filter: --pass=SLPVectorizer.
normalise_pass() {
    local p="$1"
    case "$p" in
        slp-vectorizer|slp-vec|slp) echo "SLPVectorizer" ;;
        loop-vectorize|loop-vec)    echo "LoopVectorize" ;;
        instcombine|instcomb)       echo "InstCombine" ;;
        gvn)                        echo "GVN" ;;
        dse)                        echo "DSE" ;;
        sroa)                       echo "SROA" ;;
        *) echo "$p" ;;  # passthrough
    esac
}

usage() {
    sed -n '3,16p' "$0" | sed 's/^# \?//'
    exit "${1:-2}"
}

PASS=""
MODE=""        # list | check | diff
CHECK_FN=""
INPUTS=()

while (($#)); do
    case "$1" in
        --pass=*)        PASS="${1#--pass=}";       shift ;;
        --list-reached)  MODE="list";               shift ;;
        --check=*)       MODE="check"; CHECK_FN="${1#--check=}"; shift ;;
        --diff)          MODE="diff";               shift ;;
        -h|--help)       usage 0 ;;
        --)              shift; INPUTS+=("$@");     break ;;
        -*)              echo "unknown flag: $1" >&2; usage 2 ;;
        *)               INPUTS+=("$1");            shift ;;
    esac
done

if [[ -z "$PASS" || -z "$MODE" ]]; then
    usage 2
fi
FILTER="$(normalise_pass "$PASS")"

probe() {
    "$PROBE_BIN" "$1" "--filter=$FILTER"
}

case "$MODE" in
    list)
        if (( ${#INPUTS[@]} != 1 )); then
            echo "ERROR: --list-reached needs exactly one input file" >&2; exit 2
        fi
        probe "${INPUTS[0]}"
        ;;
    check)
        if (( ${#INPUTS[@]} != 1 )); then
            echo "ERROR: --check needs exactly one input file" >&2; exit 2
        fi
        # set -e + pipefail would propagate grep's "no match" exit through
        # the pipe and abort the script. Run the pipe under a subshell with
        # pipefail disabled so we observe grep's status cleanly.
        rc=0
        ( set +e +o pipefail
          probe "${INPUTS[0]}" | grep -qF -- "$CHECK_FN"
          exit $? ) || rc=$?
        exit "$rc"
        ;;
    diff)
        if (( ${#INPUTS[@]} != 2 )); then
            echo "ERROR: --diff needs exactly two input files" >&2; exit 2
        fi
        a_tmp="$(mktemp)"; b_tmp="$(mktemp)"
        trap 'rm -f "$a_tmp" "$b_tmp"' EXIT
        probe "${INPUTS[0]}" | sort -u > "$a_tmp"
        probe "${INPUTS[1]}" | sort -u > "$b_tmp"
        # only-in-a (deletions) → "- name"; only-in-b (additions) → "+ name"
        comm -23 "$a_tmp" "$b_tmp" | sed 's/^/- /'
        comm -13 "$a_tmp" "$b_tmp" | sed 's/^/+ /'
        ;;
esac
