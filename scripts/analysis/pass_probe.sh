#!/usr/bin/env bash
#
# pass_probe.sh — wrapper around build/coverage_probe.
#
# Usage:
#   ./pass_probe.sh --pass=<filter> --list-reached <input.ll>
#   ./pass_probe.sh --pass=<filter> --check=<func>      <input.ll>
#   ./pass_probe.sh --pass=<filter> --diff <a.ll> <b.ll>
#   ./pass_probe.sh --pass=<filter> --call-chains [--k=<N>] <input.ll>
#   ./pass_probe.sh --pass=<filter> --call-sequence       <input.ll>
#   ./pass_probe.sh --pass=<filter> --chain-diff [--k=<N>] <a.ll> <b.ll>
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
#   --call-chains    print every unique sliding-window call chain of
#                    length k from the dynamic function transition
#                    sequence; each chain printed as "a -> b -> c"
#   --call-sequence  print the dedup'd function transition sequence,
#                    one fn per line (in execution order)
#   --chain-diff a b print "+ chain" for chains only-in-b, "- chain"
#                    for chains only-in-a
#   --k=<N>          chain length (default 3, only meaningful for
#                    --call-chains / --chain-diff)
#   --raw            skip the post-processing simplifier and emit the
#                    full demangled symbols straight from llvm-symbolizer
#   --chain-format=<raw|simplified>   alias for --raw / default

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
    sed -n '3,32p' "$0" | sed 's/^# \?//'
    exit "${1:-2}"
}

PASS=""
MODE=""        # list | check | diff | call-chains | call-sequence | chain-diff
CHECK_FN=""
RAW=0
K=3
INPUTS=()

while (($#)); do
    case "$1" in
        --pass=*)         PASS="${1#--pass=}";       shift ;;
        --list-reached)   MODE="list";               shift ;;
        --check=*)        MODE="check"; CHECK_FN="${1#--check=}"; shift ;;
        --diff)           MODE="diff";               shift ;;
        --call-chains)    MODE="call-chains";        shift ;;
        --call-sequence)  MODE="call-sequence";      shift ;;
        --chain-diff)     MODE="chain-diff";         shift ;;
        --k=*)            K="${1#--k=}";             shift ;;
        --raw)            RAW=1;                     shift ;;
        --chain-format=raw)        RAW=1;            shift ;;
        --chain-format=simplified) RAW=0;            shift ;;
        --chain-format=*) echo "unknown --chain-format: $1" >&2; usage 2 ;;
        -h|--help)        usage 0 ;;
        --)               shift; INPUTS+=("$@");     break ;;
        -*)               echo "unknown flag: $1" >&2; usage 2 ;;
        *)                INPUTS+=("$1");            shift ;;
    esac
done

if [[ -z "$PASS" || -z "$MODE" ]]; then
    usage 2
fi
FILTER="$(normalise_pass "$PASS")"

# ------------------------------------------------------------------
# Simplifier: collapses the demangled C++ symbols emitted by
# llvm-symbolizer --functions=linkage into agent-friendly names.
#
# Transformations (in order, applied per line):
#   1. Strip the parameter list: walk to the first top-level '(' that
#      isn't inside template angle brackets.
#   2. Collapse remaining template arguments <…>.
#   3. Drop the leading return-type token (last whitespace-separated
#      word is the qualified function name).
#   4. Strip leading "llvm::" and any further lowercase namespace
#      prefixes (e.g. "slpvectorizer::" → drop, but "BoUpSLP::" stays).
#   5. Lambdas — names containing "::$_N" or "::'lambda'" — collapse to
#      "<parent_function>::lambda" by walking back over the parent's
#      param list to find the enclosing function name.
#   6. std internals — names containing "std::__…" — collapse to
#      "<parent_function>::sort_internals" if a parent function can be
#      recovered from the template args, else just "std::<short_name>".
#   7. Deduplicate (keeps the first occurrence).
#
# When CHAIN_DELIM is set in the environment, each input line is split
# on the delimiter, every piece is simplified, and the pieces are
# rejoined with the same delimiter — so "a -> b -> c" stays a chain
# but each leg is normalised. Dedup is by the simplified chain.
# ------------------------------------------------------------------
simplify_stream() {
# Run the inline Python via process substitution so the heredoc supplies
# the script path while stdin is still the upstream pipe. (`python3 - <<EOF`
# would route the heredoc into stdin and the symbolizer output would be
# silently dropped.)
python3 <(cat <<'PY'
import os
import re
import sys

LLVM_NS_RE = re.compile(r'^llvm::')
LOWER_NS_RE = re.compile(r'^([a-z_][a-zA-Z0-9_]*)::')


def find_first_top_paren(s):
    """Index of the first '(' that isn't inside <...>; -1 if none."""
    depth_a = 0
    for i, c in enumerate(s):
        if c == '<':
            depth_a += 1
        elif c == '>' and depth_a > 0:
            depth_a -= 1
        elif c == '(' and depth_a == 0:
            return i
    return -1


def collapse_templates(s):
    """Strip <...> repeatedly, innermost first."""
    while True:
        new = re.sub(r'<[^<>]*>', '', s)
        if new == s:
            return s
        s = new


def strip_namespaces(s):
    s = LLVM_NS_RE.sub('', s)
    while True:
        m = LOWER_NS_RE.match(s)
        if not m:
            return s
        s = s[m.end():]


def find_parent_func(s):
    """Find the simple name of the function lexically containing the
    first lambda marker. Returns None if extraction fails."""
    marker_idx = -1
    for marker in ('::$_', "::'lambda'"):
        i = s.find(marker)
        if i >= 0 and (marker_idx < 0 or i < marker_idx):
            marker_idx = i
    if marker_idx <= 0:
        return None

    # Walk backwards balancing parens. The char immediately before the
    # marker should be ')'; otherwise we don't have a recognisable
    # parent-call structure.
    j = marker_idx - 1
    if j < 0 or s[j] != ')':
        return None
    depth = 1
    j -= 1
    while j >= 0 and depth > 0:
        if s[j] == ')':
            depth += 1
        elif s[j] == '(':
            depth -= 1
        j -= 1
    if depth != 0:
        return None
    paren_open = j + 1  # position of the matched '('

    # Skip back over a possible template-arg list on the function name.
    name_end = paren_open
    if name_end > 0 and s[name_end - 1] == '>':
        tdepth = 1
        k = name_end - 2
        while k >= 0 and tdepth > 0:
            if s[k] == '>':
                tdepth += 1
            elif s[k] == '<':
                tdepth -= 1
            k -= 1
        name_end = k + 1

    # Walk back over the qualified-name characters.
    k = name_end - 1
    while k >= 0 and (s[k].isalnum() or s[k] in '_:'):
        k -= 1
    qual = s[k + 1:name_end]
    if not qual:
        return None
    return qual.rsplit('::', 1)[-1]


def simplify(line):
    s = line.strip()
    if not s:
        return None

    # std internals first — many std::__ entries also contain "::$_"
    # in their template arguments (the embedded lambda type), and we'd
    # rather bucket them as "<parent>::sort_internals" than fold them
    # in with the user-written "<parent>::lambda".
    if 'std::__' in s:
        parent = find_parent_func(s)
        if parent:
            return f"{parent}::sort_internals"
        m = re.search(r'\b(std::[A-Za-z_][\w]*)', s)
        if m:
            return m.group(1)

    # Lambdas — including function_ref<…>::callback_fn<lambda> wrappers
    # which always contain a "::$_" marker.
    if "::$_" in s or "::'lambda'" in s:
        parent = find_parent_func(s)
        if parent:
            return f"{parent}::lambda"

    # Plain function: strip params, templates, return type, namespaces.
    paren = find_first_top_paren(s)
    if paren >= 0:
        s = s[:paren]
    s = collapse_templates(s)
    parts = s.split()
    if not parts:
        return None
    s = parts[-1]
    s = strip_namespaces(s)
    return s or None


CHAIN_DELIM = os.environ.get('CHAIN_DELIM', '')
NO_DEDUP = os.environ.get('NO_DEDUP', '') == '1'


def simplify_chain(line):
    parts = line.split(CHAIN_DELIM)
    pieces = []
    for p in parts:
        s = simplify(p)
        if s is None:
            return None
        pieces.append(s)
    return CHAIN_DELIM.join(pieces)


seen = set()
for raw in sys.stdin:
    raw = raw.rstrip('\n')
    if CHAIN_DELIM and CHAIN_DELIM in raw:
        out = simplify_chain(raw)
    else:
        out = simplify(raw)
    if out is None:
        continue
    if not NO_DEDUP:
        if out in seen:
            continue
        seen.add(out)
    print(out)
PY
)
}

# probe_raw <args...> — passes positional args through to coverage_probe.
# The first positional after the flags is the input file.
probe_raw() {
    "$PROBE_BIN" "$@" "--filter=$FILTER"
}

probe() {
    if (( RAW )); then
        probe_raw "$@"
    else
        probe_raw "$@" | simplify_stream
    fi
}

# Chain-aware probe: simplifier needs to split on " -> " before
# normalising each leg.
probe_chain() {
    if (( RAW )); then
        probe_raw "$@"
    else
        CHAIN_DELIM=" -> " probe_raw "$@" | CHAIN_DELIM=" -> " simplify_stream
    fi
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
        # pipefail disabled so we observe grep's status cleanly. --check
        # always greps the simplified output unless --raw was passed.
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
        # interleaved so consumers see both directions.
        comm -23 "$a_tmp" "$b_tmp" | sed 's/^/- /'
        comm -13 "$a_tmp" "$b_tmp" | sed 's/^/+ /'
        ;;
    call-chains)
        if (( ${#INPUTS[@]} != 1 )); then
            echo "ERROR: --call-chains needs exactly one input file" >&2; exit 2
        fi
        probe_chain --call-chains "--k=$K" "${INPUTS[0]}"
        ;;
    call-sequence)
        if (( ${#INPUTS[@]} != 1 )); then
            echo "ERROR: --call-sequence needs exactly one input file" >&2; exit 2
        fi
        # Sequence is order-sensitive — never globally dedup. coverage_probe
        # already collapses consecutive repeats; simplification can produce
        # new adjacent dupes (e.g. two distinct demangled symbols mapping to
        # the same simplified name), which we collapse here without touching
        # non-adjacent repeats.
        if (( RAW )); then
            probe_raw --call-sequence "${INPUTS[0]}"
        else
            probe_raw --call-sequence "${INPUTS[0]}" \
                | NO_DEDUP=1 simplify_stream \
                | awk 'NR==1 || $0 != prev { print } { prev = $0 }'
        fi
        ;;
    chain-diff)
        if (( ${#INPUTS[@]} != 2 )); then
            echo "ERROR: --chain-diff needs exactly two input files" >&2; exit 2
        fi
        a_tmp="$(mktemp)"; b_tmp="$(mktemp)"
        trap 'rm -f "$a_tmp" "$b_tmp"' EXIT
        probe_chain --call-chains "--k=$K" "${INPUTS[0]}" | sort -u > "$a_tmp"
        probe_chain --call-chains "--k=$K" "${INPUTS[1]}" | sort -u > "$b_tmp"
        comm -23 "$a_tmp" "$b_tmp" | sed 's/^/- /'
        comm -13 "$a_tmp" "$b_tmp" | sed 's/^/+ /'
        ;;
esac
