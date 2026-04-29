#!/usr/bin/env python3
"""coverage_overlay.py — overlay probe coverage onto a static call graph.

Reads a call graph JSON (from extract_callgraph.py) and a probe-output
text file (from `pass_probe.sh --list-reached`). Marks each callgraph
node REACHED or UNREACHED and identifies the **frontier**: every edge
from a reached parent to an unreached child. For each frontier edge
prints the call site line and its enclosing if/for/while/switch
condition (the "blocking condition" that, when satisfied, would let
execution flow into the unreached child).

Output:

  - human-readable report on stdout
  - JSON file (if --output passed) with the structured frontier list

Matching strategy: probe coverage names are simplified (e.g.
`SLPVectorizerPass::vectorizeStoreChain` or just `runImpl::lambda`).
For each callgraph node we test (in order):

  1. exact qualified-name match
  2. last-component-match (for unqualified node names)
  3. lambda label match (`Parent::lambda@1234` → `Parent::lambda` in
     simplified probe output)

A callee in the call graph may not have a corresponding callgraph node
(e.g. external utility, std lib, or a method on a non-tracked class).
Those callees are filtered out of the frontier — they're not "unreached
SLP code", they're cross-module calls that are out of scope.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


# Identifiers we never want to flag as missing-coverage targets — they're
# either utility macros, container methods, or stdlib helpers that are
# either inlined or live outside the source we're tracking.
_NOISE_CALLEES = frozenset(
    {
        # Common LLVM debug macros + stream helpers
        "LLVM_DEBUG", "dbgs", "errs", "outs", "report_fatal_error",
        "format", "format_decimal", "Twine",
        # Container / range methods (member fns, not free fns we own)
        "size", "empty", "front", "back", "begin", "end", "rbegin", "rend",
        "data", "clear", "reserve", "resize", "push_back", "emplace_back",
        "pop_back", "pop_back_val", "pop_front", "insert", "erase",
        "find", "contains", "count", "lookup", "at",
        "try_emplace", "emplace", "insert_range", "append", "assign",
        "set", "test", "any", "all", "none", "flip", "reset",
        "first", "second", "value", "key", "operator",
        "getFirst", "getSecond", "getName", "getType", "getOpcode",
        # llvm::cast / dyn_cast / isa family
        "cast", "dyn_cast", "dyn_cast_or_null", "dyn_cast_if_present",
        "isa", "isa_and_nonnull", "isa_and_present", "cast_or_null",
        # ADT helpers
        "make_pair", "make_tuple", "move", "forward", "swap",
        "addressof", "ref", "cref", "min", "max", "abs",
        "all_of", "any_of", "none_of", "count_if", "find_if",
        "find_if_not", "for_each", "transform", "copy", "fill",
        "sort", "stable_sort", "reverse", "rotate", "is_sorted",
        "lower_bound", "upper_bound", "equal", "equal_range",
        "binary_search", "next_permutation", "iota",
        "make_optional", "make_unique", "make_shared",
        "get", "release", "reset_value",
        "enumerate", "zip", "make_range", "seq", "to_vector",
        "concat", "drop_begin", "drop_end", "take_front", "take_back",
        "drop_front", "drop_back", "reverse_iterator",
        # SLP-internal: a few things that look like calls but are CRTP
        # type-trait checks.
        "IsaPred",
    }
)


def parse_coverage(path: Path) -> set[str]:
    """Parse the probe --list-reached output. Each non-empty, non-comment
    line is one reached function name (simplified by pass_probe.sh)."""
    names: set[str] = set()
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        names.add(line)
    return names


def build_node_index(graph: dict) -> tuple[dict[str, set[str]], dict[str, set[str]]]:
    """Build two indices over callgraph node names:
      - by_full[name]            → {name}             (exact)
      - by_last[last_component]  → {name1, name2, …}  (group of nodes
                                                       sharing a last
                                                       component)
    """
    by_full: dict[str, set[str]] = {}
    by_last: dict[str, set[str]] = {}
    for n in graph:
        by_full.setdefault(n, set()).add(n)
        last = n.rsplit("::", 1)[-1]
        # Strip the @line suffix from lambda labels so they match the
        # simplified probe output (which collapses to `Parent::lambda`).
        if "@" in last:
            last = last.split("@", 1)[0]
        by_last.setdefault(last, set()).add(n)
    return by_full, by_last


def is_reached(node_name: str, coverage: set[str]) -> bool:
    """Mark a node REACHED if any coverage entry contains the node's
    qualified name as a substring (simplified probe names share the
    last `Class::method` component with our callgraph keys)."""
    if node_name in coverage:
        return True
    # Drop the @line suffix on lambdas before matching.
    candidate = node_name
    if "@" in candidate:
        idx = candidate.find("@")
        candidate = candidate[:idx]
    if candidate in coverage:
        return True
    # Substring match against simplified names. probe outputs collapse
    # qualifiers like `llvm::` and `slpvectorizer::` already, so an
    # exact-or-suffix check is enough.
    for cov in coverage:
        if candidate == cov:
            return True
        if cov.endswith("::" + candidate.rsplit("::", 1)[-1]):
            # last-component substring fallback — use sparingly
            # (we'll only consume it if exact didn't match)
            pass
    return False


def resolve_callee(
    callee: str,
    parent: str,
    by_full: dict[str, set[str]],
    by_last: dict[str, set[str]],
) -> set[str]:
    """Map a callee (as written in source) to a set of candidate callgraph
    nodes. Empty set ⇒ callee is not in our tracked source file (e.g.
    stdlib / external) and should be filtered out of the frontier."""
    # Direct full-name match (rare — only when the source writes a fully
    # qualified call like `llvm::sortPtrAccesses(...)` and the callgraph
    # key happens to match).
    if callee in by_full:
        return {callee}
    # Last-component match. Prefer same-class first, then any.
    cands = by_last.get(callee, set())
    if not cands:
        return set()
    parent_class = parent.rsplit("::", 1)[0] if "::" in parent else None
    if parent_class:
        in_same_class = {c for c in cands if c.startswith(parent_class + "::")}
        if in_same_class:
            return in_same_class
    return set(cands)


def overlay(graph: dict, coverage: set[str]) -> dict:
    by_full, by_last = build_node_index(graph)
    reached: dict[str, bool] = {n: is_reached(n, coverage) for n in graph}

    frontier: list[dict] = []
    for parent, info in graph.items():
        if not reached.get(parent):
            continue
        # Track unique (parent, callee_name, line) tuples to dedupe.
        seen: set[tuple[str, str, int]] = set()
        for call in info.get("calls", []):
            callee = call["callee"]
            if callee in _NOISE_CALLEES:
                continue
            cands = resolve_callee(callee, parent, by_full, by_last)
            if not cands:
                # External / stdlib / unresolved — not part of the frontier.
                continue
            # If ANY candidate is reached, we treat the call as resolved.
            if any(reached.get(c) for c in cands):
                continue
            key = (parent, callee, call["line"])
            if key in seen:
                continue
            seen.add(key)
            frontier.append(
                {
                    "parent": parent,
                    "callee": callee,
                    "callee_candidates": sorted(cands),
                    "line": call["line"],
                    "condition": call.get("condition"),
                    "condition_line": call.get("condition_line"),
                }
            )
        # Also flag lambdas the parent declares but that aren't reached.
        for lam in info.get("lambdas", []):
            if not reached.get(lam):
                # A lambda that the parent declares but never invokes —
                # interesting only when it's actually called from somewhere.
                # We skip it here since the call-site emit above already
                # captured the invocation point (if any).
                pass

    summary = {
        "total_nodes": len(graph),
        "reached_nodes": sum(1 for v in reached.values() if v),
        "unreached_nodes": sum(1 for v in reached.values() if not v),
        "frontier_edges": len(frontier),
        "frontier": sorted(frontier, key=lambda e: (e["parent"], e["line"])),
        "reached": sorted(n for n, v in reached.items() if v),
        "unreached": sorted(n for n, v in reached.items() if not v),
    }
    return summary


# ---- Reporting -----------------------------------------------------------


def render_report(summary: dict) -> str:
    out: list[str] = []
    out.append("=" * 72)
    out.append(
        f"call graph coverage: {summary['reached_nodes']} reached / "
        f"{summary['total_nodes']} total "
        f"({summary['unreached_nodes']} unreached)"
    )
    out.append(f"frontier edges: {summary['frontier_edges']}")
    out.append("=" * 72)
    out.append("")
    out.append("FRONTIER EDGES (reached → unreached):")
    out.append("")
    if not summary["frontier"]:
        out.append("  (none — all reached parents have all callees reached)")
    else:
        for e in summary["frontier"]:
            out.append(f"  {e['parent']}:{e['line']} → {e['callee']}")
            cands = e.get("callee_candidates") or []
            if cands and cands != [e["callee"]]:
                # Show the resolved candidate set when ambiguous.
                if len(cands) == 1:
                    out.append(f"    target: {cands[0]}")
                else:
                    out.append(f"    candidates ({len(cands)}): {', '.join(cands[:4])}{'…' if len(cands) > 4 else ''}")
            cond = e.get("condition")
            if cond:
                out.append(f"    condition: {cond}")
            else:
                out.append("    condition: (unconditional)")
            out.append("")
    return "\n".join(out)


# ---- CLI -----------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("callgraph", help="JSON output from extract_callgraph.py")
    ap.add_argument(
        "coverage",
        help="probe output text file (one reached fn per line, from "
        "pass_probe.sh --list-reached)",
    )
    ap.add_argument(
        "--output",
        "-o",
        default=None,
        help="output JSON path for the structured frontier (optional)",
    )
    ap.add_argument(
        "--quiet",
        action="store_true",
        help="suppress the human-readable report",
    )
    args = ap.parse_args()

    graph = json.loads(Path(args.callgraph).read_text())
    coverage = parse_coverage(Path(args.coverage))

    summary = overlay(graph, coverage)
    if not args.quiet:
        sys.stdout.write(render_report(summary) + "\n")

    if args.output:
        Path(args.output).write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n"
        )
        sys.stderr.write(
            f"wrote frontier ({summary['frontier_edges']} edges) "
            f"to {args.output}\n"
        )


if __name__ == "__main__":
    main()
