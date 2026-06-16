#!/usr/bin/env python3
"""Input-feature profiler for LLVM IR — prototype for feature-coverage steering.

Principle: a bug is gated by the input IR's *semantic structure*, not by the
compiler path. We capture that structure agnostically as the set of
"context tuples" a function contains — one per instruction:

    (opcode|intrinsic, flags, result-type-class, {1-hop operand def-kinds})

This is the data-flow analog of AFL edge-coverage (a node + its in-edges) rather
than node-coverage (bare opcodes). It is purely mechanical — no motif is hard-coded
— yet motifs like `mul[nuw](i32, cNeg1)` or `and(i32, {mul})` emerge as distinct,
*rare* cells. Corpus document-frequency (df) of a tuple = how many corpus functions
contain it; rare tuples mark sparsely-explored regions of the input space.

Usage:
  ir_feature_profile.py --corpus <dir> [--sample N] --probe f1.ll f2.ll ...
"""
import argparse, glob, os, re, random, collections, statistics

FLAGS = {"nuw","nsw","exact","disjoint","nneg","inbounds","samesign"}
INTTY = re.compile(r"\bi(\d+)\b")
ASSIGN = re.compile(r"^(%[\w.]+)\s*=\s*(.*)$")
TOKREF = re.compile(r"%[\w.]+|@[\w.]+|-?\d+|\b(?:undef|poison|true|false|null)\b")

def type_class(rhs):
    m = INTTY.search(rhs)
    if not m: return "noint"
    w = int(m.group(1))
    return f"i{w}" if w in (1,8,16,32,64,128) else "iODD"

def const_kind(tok):
    if tok == "-1": return "cNeg1"
    if tok == "0":  return "c0"
    if tok == "1":  return "c1"
    if re.fullmatch(r"-?\d+", tok): return "cInt"
    if tok in ("true","false"): return "cBool"
    if tok in ("undef","poison"): return "cPoison"
    if tok == "null": return "cNull"
    if tok.startswith("@"): return "global"
    return None

def opcode_of(rhs):
    op = rhs.split()[0]
    if op == "call" or op.startswith("call"):
        m = re.search(r"@(llvm\.[\w.]+)", rhs)
        if m:  # collapse the type-suffix: llvm.ushl.sat.i32 -> llvm.ushl.sat
            name = re.sub(r"\.i\d+$|\.v\d+i\d+$|\.f\d+$", "", m.group(1))
            return f"call:{name}"
        return "call"
    return op

def func_tuples(body):
    defop, insts = {}, []
    for raw in body.splitlines():
        l = raw.strip()
        if not l or l.startswith((";","}")) or l.endswith(":") or l.startswith(("define","declare","attributes","source_filename","target")):
            continue
        m = ASSIGN.match(l)
        if m:
            lhs, rhs = m.group(1), m.group(2)
            defop[lhs] = opcode_of(rhs)
            insts.append((lhs, rhs))
        else:
            insts.append((None, l))
    tuples = []
    for lhs, rhs in insts:
        op = opcode_of(rhs)
        toks = rhs.split()
        flags = frozenset(t for t in toks if t in FLAGS)
        tc = type_class(rhs)
        kinds = set()
        body_after = rhs[len(toks[0]):]  # skip the opcode token
        for tok in TOKREF.findall(body_after):
            if tok.startswith("%"):
                kinds.add(defop.get(tok, "arg"))
            else:
                ck = const_kind(tok)
                if ck: kinds.add(ck)
        tuples.append((op, flags, tc, frozenset(kinds)))
    return tuples

def fmt(t):
    op, flags, tc, kinds = t
    fl = "+".join(sorted(flags)) if flags else "-"
    return f"{op}[{fl}] {tc} <{','.join(sorted(kinds))}>"

def split_functions(text):
    # crude: split on 'define', keep each define...} block
    funcs, cur, depth, inside = [], [], 0, False
    for line in text.splitlines():
        if line.startswith("define"):
            inside, cur = True, [line]; depth = line.count("{")-line.count("}"); continue
        if inside:
            cur.append(line); depth += line.count("{")-line.count("}")
            if depth <= 0:
                funcs.append("\n".join(cur)); inside=False
    return funcs or [text]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--sample", type=int, default=3000)
    ap.add_argument("--probe", nargs="+", required=True)
    a = ap.parse_args()
    random.seed(0)

    files = glob.glob(os.path.join(a.corpus, "*.ll"))
    files = random.sample(files, min(a.sample, len(files)))
    df = collections.Counter()           # tuple -> #funcs containing it
    minfreq_dist = []                    # per-func rarest-tuple df
    N = 0
    for f in files:
        for fn in split_functions(open(f, errors="replace").read()):
            ts = set(func_tuples(fn))
            if not ts: continue
            N += 1
            for t in ts: df[t] += 1
    # second pass for per-func min-df baseline (now df is complete)
    for f in files:
        for fn in split_functions(open(f, errors="replace").read()):
            ts = set(func_tuples(fn))
            if ts: minfreq_dist.append(min(df[t] for t in ts))
    base_med = statistics.median(minfreq_dist)
    print(f"corpus: {len(files)} files, {N} functions, {len(df)} distinct tuples")
    print(f"baseline: median over corpus funcs of their RAREST-tuple df = {base_med}")
    print(f"  ({sum(1 for x in minfreq_dist if x<=2)*100//len(minfreq_dist)}% of corpus funcs have a rarest-tuple df<=2)\n")

    for p in a.probe:
        ts = func_tuples("\n".join(split_functions(open(p,errors="replace").read())))
        uniq = sorted(set(ts), key=lambda t: df[t])
        mind = min(df[t] for t in uniq)
        print(f"### {os.path.basename(p)}  — rarest-tuple df = {mind}  (corpus median {base_med})")
        for t in uniq[:6]:
            pct = df[t]*100.0/N
            print(f"    df={df[t]:<5} ({pct:5.2f}% of funcs)  {fmt(t)}")
        print()

if __name__ == "__main__":
    main()
