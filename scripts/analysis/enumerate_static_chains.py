#!/usr/bin/env python3
"""enumerate_static_chains.py — upper bound on call chains from the static
call graph.

Walks docs/slp_decomposition/callgraph.json and enumerates every length-k
sliding-window call chain whose nodes lie inside SLPVectorizer.cpp's
"interesting" namespaces (SLPVectorizerPass, BoUpSLP, HorizontalReduction,
the various helper classes that show up in the dynamic trace). Each call
chain is normalised to the same surface form used by pass_probe.sh's
simplifier so a static chain and a dynamic chain are directly comparable.

Edges are derived purely from the JSON `calls` list: a node X has an edge
to every node Y whose simple name appears as a callee of X. Because the
extractor is regex-based, callees are unqualified — we resolve a name like
"vectorizeStoreChains" to *every* graph node whose simple name matches,
which over-approximates real edges. That's fine: this number is an upper
bound on dynamic feasibility, not a lower one.

Usage:
    enumerate_static_chains.py [--k=3] [--out=path] [callgraph.json]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from collections import defaultdict


# Match what pass_probe.sh's simplifier keeps. Class prefixes that appear
# in the dynamic trace (with a leading capital) survive normalisation; the
# rest get stripped or treated as lambda parents.
SLP_CLASS_PREFIXES = (
    "SLPVectorizerPass::",
    "BoUpSLP::",
    "HorizontalReduction::",
    "InstructionsState::",
    "InstructionsCompatibilityAnalysis::",
    "BinOpSameOpcodeHelper::",
    "InterchangeableInfo::",
    "TreeEntry::",
    "BlockScheduling::",
    "BaseShuffleAnalysis::",
    "ShuffleCostEstimator::",
    "ShuffleCostBuilder::",
    "ShuffleInstructionBuilder::",
)

LAMBDA_AT_RE = re.compile(r"::lambda@\d+")


def normalise(name: str) -> str:
    """Map a graph node name to the same surface form pass_probe.sh emits.

    - "ClassName::method" stays as-is when the class is in SLP_CLASS_PREFIXES.
    - "ClassName::method" with an unknown ClassName is dropped (returns "").
    - "Parent::funcName::lambda@LINE[::lambda@…]" → "funcName::lambda".
    """
    # Lambdas: collapse all "::lambda@\d+" segments, then take the simple
    # name immediately preceding the lambda marker as the parent.
    if "::lambda@" in name:
        no_lines = LAMBDA_AT_RE.sub("::lambda", name)
        # Multiple ::lambda::lambda chains → single ::lambda
        while "::lambda::lambda" in no_lines:
            no_lines = no_lines.replace("::lambda::lambda", "::lambda")
        # Find parent: token immediately before "::lambda".
        idx = no_lines.index("::lambda")
        prefix = no_lines[:idx]
        parent = prefix.rsplit("::", 1)[-1]
        if not parent:
            return ""
        return f"{parent}::lambda"

    # Plain qualified name. Keep only those rooted in our SLP class set.
    for pfx in SLP_CLASS_PREFIXES:
        if name.startswith(pfx):
            return name
    return ""


def build_graph(raw: dict) -> tuple[list[str], dict[str, set[str]]]:
    """Return (nodes, adjacency). Nodes are SLP-namespace nodes only;
    edges are those that resolve to another SLP node by simple-name
    matching against `calls`."""
    # Index nodes by simple name. A node "ClassName::method" indexes under
    # "method"; "Class::method::lambda@N" indexes under "method" too — the
    # extractor records lambdas as separate nodes but their dynamic
    # appearance is as the parent's lambda, so we index them under the
    # parent's simple name. (We also index under their full last-segment
    # for completeness.)
    nodes_all = list(raw.keys())
    by_simple: dict[str, list[str]] = defaultdict(list)
    for n in nodes_all:
        # full last segment
        seg = n.rsplit("::", 1)[-1]
        by_simple[seg].append(n)
        # for lambdas, also under the parent function's simple name
        if "::lambda@" in n:
            head = n.split("::lambda@")[0]
            parent_simple = head.rsplit("::", 1)[-1]
            by_simple[parent_simple].append(n)

    # Build forward edges from each SLP-namespace node.
    adj: dict[str, set[str]] = defaultdict(set)
    for src, body in raw.items():
        if not normalise(src):
            continue
        for c in body.get("calls", []):
            callee = c.get("callee", "")
            if not callee:
                continue
            for cand in by_simple.get(callee, ()):
                if not normalise(cand):
                    continue
                if cand == src:
                    continue  # match dynamic consecutive-fn dedup
                adj[src].add(cand)
        # `lambdas` field lists child lambdas — treat as edges, since
        # lambdas execute inside the parent.
        for lam in body.get("lambdas", []):
            if not normalise(lam):
                continue
            if lam == src:
                continue
            adj[src].add(lam)

    nodes = sorted(n for n in nodes_all if normalise(n))
    return nodes, adj


def enumerate_chains(nodes, adj, k: int) -> set[tuple[str, ...]]:
    """All distinct length-k sequences (n0, n1, …, n_{k-1}) where each
    (n_i, n_{i+1}) is an edge AND the normalised names differ at every
    step (matching the dynamic consecutive-name dedup)."""
    chains: set[tuple[str, ...]] = set()

    def dfs(path: list[str], depth: int):
        if depth == k:
            norm = tuple(normalise(n) for n in path)
            # All non-empty (guaranteed by `nodes` filter) and consecutive
            # distinct on the simplified surface form.
            if all(norm[i] != norm[i + 1] for i in range(k - 1)):
                chains.add(norm)
            return
        last = path[-1]
        for nxt in adj.get(last, ()):
            # Cheap prune: skip if normalised name equals last's.
            if normalise(nxt) == normalise(last):
                continue
            path.append(nxt)
            dfs(path, depth + 1)
            path.pop()

    for n in nodes:
        dfs([n], 1)
    return chains


def main(argv):
    p = argparse.ArgumentParser()
    p.add_argument("graph", nargs="?",
                   default="docs/slp_decomposition/callgraph.json")
    p.add_argument("--k", type=int, default=3)
    p.add_argument("--out", default="-",
                   help="output file (default stdout)")
    p.add_argument("--scope", default="all",
                   choices=("all", "slpvectorizer-pass"),
                   help="restrict to SLPVectorizerPass + its lambdas to "
                        "match the --filter=SLPVectorizer dynamic probe")
    args = p.parse_args(argv)

    raw = json.loads(Path(args.graph).read_text())
    nodes, adj = build_graph(raw)

    if args.scope == "slpvectorizer-pass":
        # Keep only nodes whose normalised form starts with
        # "SLPVectorizerPass::" or is a lambda whose parent function is
        # one of those (i.e. matches the --filter=SLPVectorizer surface).
        slp_simple_names = set()
        for n in nodes:
            if n.startswith("SLPVectorizerPass::"):
                slp_simple_names.add(n.rsplit("::", 1)[-1])
        keep = set()
        for n in nodes:
            if n.startswith("SLPVectorizerPass::"):
                keep.add(n)
            elif "::lambda@" in n:
                head = n.split("::lambda@")[0]
                parent = head.rsplit("::", 1)[-1]
                if parent in slp_simple_names:
                    keep.add(n)
        nodes = sorted(keep)
        adj = {k: {v for v in adj.get(k, set()) if v in keep}
               for k in keep}

    chains = enumerate_chains(nodes, adj, args.k)

    out_lines = sorted(" -> ".join(c) for c in chains)
    if args.out == "-":
        sys.stdout.write("\n".join(out_lines) + ("\n" if out_lines else ""))
    else:
        Path(args.out).write_text("\n".join(out_lines) + "\n")
    print(f"# nodes={len(nodes)} edges={sum(len(v) for v in adj.values())} "
          f"chains_k={args.k}={len(chains)}",
          file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1:])
