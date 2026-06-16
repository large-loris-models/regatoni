#!/usr/bin/env python3
"""Corpus distillation + rarity profiling by input-feature (context-tuple) coverage.

Feature tuples come from the LLVM-API extractor `build/ir_features` (NOT regex), so
there is no parsing ambiguity. Two modes:

  profile  — corpus tuple document-frequency (df) + per-probe rarest-tuple df
             (validates "bugs sit in rare/df=0 cells").
  distill  — greedy (lazy) set-cover: the smallest seed set whose union covers the
             corpus's context-tuple universe. Set-cover provably RETAINS every rare
             (df=1) cell while dropping seeds that only repeat common tuples — i.e. it
             raises feature density instead of diluting it (cf. naive concatenation).

Usage:
  corpus_distill.py profile --corpus DIR [--sample N] --probe a.ll b.ll ...
  corpus_distill.py distill --corpus DIR [--sample N] [--target 1.0] [--out FILE]
"""
import argparse, glob, os, subprocess, sys, random, heapq, statistics, collections

BIN = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "build", "ir_features")

def extract(files, chunk=300):
    """Run ir_features over files; return {filepath: set(tuples)} (functions unioned)."""
    out = collections.defaultdict(set)
    for i in range(0, len(files), chunk):
        batch = files[i:i+chunk]
        p = subprocess.run([BIN, *batch], capture_output=True, text=True)
        cur = None
        for line in p.stdout.splitlines():
            if line.startswith("=== "):
                cur = line[4:].rsplit("::", 1)[0]   # path (drop ::funcname)
            elif cur is not None and line:
                out[cur].add(line)
    return out

def corpus_files(d, sample):
    fs = sorted(glob.glob(os.path.join(d, "*.ll")))
    random.seed(0)
    return random.sample(fs, min(sample, len(fs))) if sample and sample < len(fs) else fs

def cmd_profile(a):
    files = corpus_files(a.corpus, a.sample)
    feats = extract(files)
    df = collections.Counter()
    for s in feats.values():
        for t in s: df[t] += 1
    N = len(feats)
    minfreqs = [min(df[t] for t in s) for s in feats.values() if s]
    base = statistics.median(minfreqs)
    print(f"corpus: {N} funcs/files, {len(df)} distinct tuples")
    print(f"baseline rarest-tuple df: median={base}  "
          f"({sum(1 for x in minfreqs if x<=2)*100//len(minfreqs)}% of funcs have df<=2)\n")
    pf = extract(a.probe)
    for p in a.probe:
        ts = pf.get(p, set())
        if not ts:
            print(f"### {os.path.basename(p)} — (no tuples / parse fail)\n"); continue
        ranked = sorted(ts, key=lambda t: df[t])
        print(f"### {os.path.basename(p)} — rarest-tuple df={min(df[t] for t in ts)} (corpus median {base})")
        for t in ranked[:6]:
            print(f"    df={df[t]:<5} ({df[t]*100.0/N:5.2f}%)  {t}")
        print()

def cmd_distill(a):
    files = corpus_files(a.corpus, a.sample)
    feats = extract(files)
    files = [f for f in feats if feats[f]]
    universe = set().union(*feats.values()) if feats else set()
    U = len(universe)
    print(f"corpus: {len(files)} seeds, {U} distinct tuples")

    # lazy greedy set cover
    covered = set()
    # heap of (-gain, filepath); gains are upper bounds, re-validated lazily
    heap = [(-len(feats[f]), f) for f in files]
    heapq.heapify(heap)
    picked, curve = [], []
    target = int(a.target * U)
    while heap and len(covered) < target:
        negg, f = heapq.heappop(heap)
        gain = len(feats[f] - covered)
        if gain == 0:
            continue
        if -negg != gain:                       # stale: re-push with true gain
            heapq.heappush(heap, (-gain, f)); continue
        picked.append(f); covered |= feats[f]
        curve.append((len(picked), len(covered)))

    print(f"\nset-cover curve (seeds -> % of {U} tuples):")
    marks = {}
    for n, c in curve:
        frac = c / U
        for thr in (0.50, 0.80, 0.90, 0.95, 0.99, 1.00):
            if thr not in marks and frac >= thr:
                marks[thr] = n
    for thr in (0.50, 0.80, 0.90, 0.95, 0.99, 1.00):
        if thr in marks:
            print(f"   {int(thr*100):3d}% of tuples covered by {marks[thr]:>5} seeds "
                  f"({marks[thr]*100//len(files)}% of corpus)")
    print(f"\ndistilled set: {len(picked)} seeds (was {len(files)}) "
          f"covering {len(covered)*100//U}% of the tuple universe")
    # rare-cell retention check
    df = collections.Counter()
    for s in feats.values():
        for t in s: df[t] += 1
    rare = {t for t in universe if df[t] == 1}
    kept = set().union(*[feats[f] for f in picked]) if picked else set()
    print(f"rare (df=1) cells: {len(rare)} total, {len(rare & kept)} retained in distilled set "
          f"({len(rare & kept)*100//max(1,len(rare))}%)")
    if a.out:
        with open(a.out, "w") as fh:
            fh.write("\n".join(picked) + "\n")
        print(f"wrote {len(picked)} seed paths -> {a.out}")

def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="mode", required=True)
    p1 = sub.add_parser("profile"); p1.add_argument("--corpus", required=True)
    p1.add_argument("--sample", type=int, default=0); p1.add_argument("--probe", nargs="+", required=True)
    p2 = sub.add_parser("distill"); p2.add_argument("--corpus", required=True)
    p2.add_argument("--sample", type=int, default=0); p2.add_argument("--target", type=float, default=1.0)
    p2.add_argument("--out", default=None)
    a = ap.parse_args()
    (cmd_profile if a.mode == "profile" else cmd_distill)(a)

if __name__ == "__main__":
    main()
