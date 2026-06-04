#!/usr/bin/env python3
"""Coverage measurement for the injected corpus.

Samples 4000 files each from {split_seeds, output/rewritten, output/injected},
runs coverage_probe --batch in parallel, and writes:

  - docs/coverage_injected.json — sets-of-functions per corpus, exclusives,
    pairwise diffs, and a gap_rules cross-reference against
    docs/rule_coverage_analysis_v2.json.

Modelled on build/run_coverage_by_category.py.
"""
import json, os, random, re, subprocess, sys, time
from concurrent.futures import ProcessPoolExecutor, as_completed

ROOT          = "/data/saiva/regatoni"
SPLIT_SEEDS   = f"{ROOT}/split_seeds"
REWRITTEN     = f"{ROOT}/output/rewritten"
INJECTED      = f"{ROOT}/output/injected"
PROBE         = f"{ROOT}/build/coverage_probe"
RULE_COV_JSON = f"{ROOT}/docs/rule_coverage_analysis_v2.json"
OUT_JSON      = f"{ROOT}/docs/coverage_injected.json"
WORK          = f"{ROOT}/build/cov_injected"
SAMPLE        = 4000
SEED          = 42
CHUNKS        = 10

os.makedirs(WORK, exist_ok=True)
rng = random.Random(SEED)

def listing(d):
    return [f for f in os.listdir(d) if f.endswith(".ll")]

def sample(paths, n):
    if len(paths) <= n: return list(paths)
    return rng.sample(paths, n)

print(f"[cov-inj] sampling {SAMPLE} per corpus (seed={SEED})", file=sys.stderr)
print(f"[cov-inj] listing {SPLIT_SEEDS}", file=sys.stderr)
ss_files = listing(SPLIT_SEEDS)
print(f"[cov-inj]   split_seeds: {len(ss_files)} .ll", file=sys.stderr)
print(f"[cov-inj] listing {REWRITTEN}", file=sys.stderr)
rw_files = listing(REWRITTEN)
print(f"[cov-inj]   rewritten: {len(rw_files)} .ll", file=sys.stderr)
print(f"[cov-inj] listing {INJECTED}", file=sys.stderr)
in_files = listing(INJECTED)
print(f"[cov-inj]   injected: {len(in_files)} .ll", file=sys.stderr)

corpora = {
    "split_seeds": [f"{SPLIT_SEEDS}/{x}" for x in sample(ss_files, SAMPLE)],
    "rewritten":   [f"{REWRITTEN}/{x}"   for x in sample(rw_files, SAMPLE)],
    "injected":    [f"{INJECTED}/{x}"    for x in sample(in_files, SAMPLE)],
}

# Also remember the sampled injected filenames so we can cross-reference
# them with the litmus rule_id later.
injected_sample_names = [os.path.basename(p) for p in corpora["injected"]]

def run_chunk(chunk_path):
    out_path = chunk_path + ".out"
    r = subprocess.run([PROBE, "--batch", chunk_path],
                       stdout=open(out_path, "w"),
                       stderr=subprocess.DEVNULL)
    return chunk_path, r.returncode

def chunked(paths, n):
    sz = (len(paths) + n - 1) // n
    return [paths[i:i+sz] for i in range(0, len(paths), sz)]

results = {}            # corpus -> set of fn names
per_file_counts = {}    # corpus -> #files with any reached fns
errors_per_corpus = {}

for cname, paths in corpora.items():
    print(f"[probe] running {cname} ({len(paths)} files)", file=sys.stderr)
    t0 = time.time()
    chunks = chunked(paths, CHUNKS)
    chunk_files = []
    for i, ch in enumerate(chunks):
        cf = f"{WORK}/{cname}.chunk{i}.list"
        with open(cf, "w") as o:
            for p in ch: o.write(p + "\n")
        chunk_files.append(cf)

    with ProcessPoolExecutor(max_workers=CHUNKS) as ex:
        futures = [ex.submit(run_chunk, cf) for cf in chunk_files]
        for fut in as_completed(futures):
            cf, rc = fut.result()
            if rc != 0:
                print(f"[probe] {cf}: exit {rc}", file=sys.stderr)

    fns = set()
    files_with_fns = set()
    errors = 0
    for cf in chunk_files:
        with open(cf + ".out") as f:
            for line in f:
                parts = line.rstrip("\n").split("\t", 1)
                if len(parts) != 2: continue
                path, fn = parts
                if fn == "ERROR":
                    errors += 1
                    continue
                fns.add(fn)
                files_with_fns.add(path)
    results[cname]            = fns
    per_file_counts[cname]    = len(files_with_fns)
    errors_per_corpus[cname]  = errors
    dt = time.time() - t0
    print(f"[probe] {cname}: {len(fns)} unique fns from "
          f"{len(files_with_fns)} files (errors={errors}) in {dt:.1f}s",
          file=sys.stderr)

# Exclusive sets vs the union of the OTHER corpora.
union_all = set().union(*results.values())
exclusive = {c: results[c] - set().union(*[v for cn, v in results.items()
                                            if cn != c])
             for c in results}

# Pairwise diffs.
pairwise = {}
names = list(results.keys())
for i, a in enumerate(names):
    for b in names[i+1:]:
        A, B = results[a], results[b]
        pairwise[f"{a}__vs__{b}"] = {
            "only_in_a": len(A - B),
            "only_in_b": len(B - A),
            "common":    len(A & B),
            "jaccard":   len(A & B) / max(1, len(A | B)),
        }

# Gap-rules cross-reference.
print("[gap] loading rule_coverage_analysis_v2", file=sys.stderr)
rc = json.load(open(RULE_COV_JSON))
gap_rule_ids = list(rc["gap_rules"])
# rule_id -> requirement_class
class_for = {}
for r in rc["rules"]:
    if r.get("rule_id"):
        class_for[r["rule_id"]] = r.get("requirement_class", "unknown")

# Litmus filenames embed (rule_id, type, ...). The rule_id is the leading
# dot-segment prefix. To map a litmus_stem to the rule_id it tests, look
# for the longest gap_rule prefix that is followed by "." in the stem.
gap_rule_sorted = sorted(gap_rule_ids, key=lambda s: -len(s))
def rule_for_litmus_stem(stem):
    for rid in gap_rule_sorted:
        if stem == rid or stem.startswith(rid + "."):
            return rid
    return None

# Parse injected filenames in our sample to map to litmus_stem then to rule_id.
inj_re = re.compile(r"__inject_(.*?)\.ll$")
litmus_seen_in_sample = {}        # litmus_stem -> count in sample
gap_rule_hits_in_sample = {}      # gap_rule -> count in sample
for fname in injected_sample_names:
    m = inj_re.search(fname)
    if not m: continue
    stem = m.group(1)
    litmus_seen_in_sample[stem] = litmus_seen_in_sample.get(stem, 0) + 1
    rid = rule_for_litmus_stem(stem)
    if rid is not None:
        gap_rule_hits_in_sample[rid] = gap_rule_hits_in_sample.get(rid, 0) + 1

# Also: in the full injected output (not just the sample), how many
# distinct gap rules have at least one injection? Read the summary.
SUMMARY_JSON = f"{ROOT}/docs/inject_full_summary.json"
gap_rule_hits_full = {}
all_litmus_in_full = {}
if os.path.exists(SUMMARY_JSON):
    s = json.load(open(SUMMARY_JSON))
    for stem, count in s.get("written_by_litmus", {}).items():
        all_litmus_in_full[stem] = count
        rid = rule_for_litmus_stem(stem)
        if rid is not None:
            gap_rule_hits_full[rid] = gap_rule_hits_full.get(rid, 0) + count

# Breakdown by requirement_class.
by_class_full = {}
for rid, n in gap_rule_hits_full.items():
    cls = class_for.get(rid, "unknown")
    by_class_full.setdefault(cls, {"rules": [], "injections": 0})
    by_class_full[cls]["rules"].append(rid)
    by_class_full[cls]["injections"] += n

summary = {
    "sample_per_corpus":   SAMPLE,
    "seed":                SEED,
    "corpora": {
        c: {
            "input_files":     len(corpora[c]),
            "files_with_fns":  per_file_counts[c],
            "errors":          errors_per_corpus[c],
            "unique_fns":      len(results[c]),
            "exclusive_fns":   len(exclusive[c]),
        }
        for c in results
    },
    "pairwise":            pairwise,
    "union_all_fns":       len(union_all),
    "exclusive_fns_lists": {c: sorted(exclusive[c]) for c in results},
    "gap_rules": {
        "total": len(gap_rule_ids),
        "hit_in_full_injection":   sorted(gap_rule_hits_full.keys()),
        "hit_in_sample":           sorted(gap_rule_hits_in_sample.keys()),
        "by_requirement_class":    {cls: {"count": len(d["rules"]),
                                          "rules": sorted(d["rules"]),
                                          "injections": d["injections"]}
                                    for cls, d in by_class_full.items()},
    },
}
with open(OUT_JSON, "w") as o:
    json.dump(summary, o, indent=2)

print()
print("Coverage by corpus")
print("Corpus       | Sample | Fns reached | Exclusive | vs split_seeds")
print("-------------+--------+-------------+-----------+----------------")
base = len(results["split_seeds"])
for c in ("split_seeds", "rewritten", "injected"):
    n  = len(results[c])
    ex = len(exclusive[c])
    diff = "    -" if c == "split_seeds" else f"{n - base:+d}"
    print(f"{c:<12} |  {SAMPLE} | {n:>11} | {ex:>9} | {diff:>15}")
print()
print(f"union_all = {len(union_all)}  saved {OUT_JSON}")
print()
print(f"gap rules (63 total) hit in full injection: {len(gap_rule_hits_full)}")
print(f"  by requirement class:")
for cls in sorted(by_class_full.keys()):
    d = by_class_full[cls]
    print(f"    {cls:<18}  rules={len(d['rules']):>3}  injections={d['injections']:>6}")
