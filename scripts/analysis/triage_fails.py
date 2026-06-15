#!/usr/bin/env python3
"""Batch-triage backend-tv fails: dedup by shape, re-probe with DEFINED inputs to
catch mischaracterized false-positives, reduce the real ones, group by reduced shape.

Pipeline (cheap -> expensive):
  1. Collect every fail IR under <run>/oracle_results/backend_tv_riscv64_*/fail/,
     read its recorded verdict (from the .log, which is the poison-ON campaign run)
     and its isel mode (from the shard dir name).
  2. Group fails by a normalized IR *shape signature* — 1600+ raw fails collapse to
     a handful of distinct shapes. We only work on one representative per shape.
  3. RE-PROBE each representative with backend-tv using DEFINED inputs
     (--disable-undef-input --disable-poison-input) in its recorded isel mode.
     This is the mischaracterization check: a fail the campaign recorded as
     "Source is more defined than target" (which we dismiss as a lifter artifact)
     that now reports "Value mismatch" under defined inputs is a REAL miscompile we
     wrongly buried.  Conversely a recorded "Value mismatch" that now verifies
     "correct" was poison-only / flaky.
  4. REDUCE every shape whose defined-input probe is a Value-mismatch
     (scripts/oracles/reduce_backend_miscompile.sh), then regroup by the *reduced*
     signature (reduction merges shapes that share a root) and label known-nuw vs new.

Usage:
  triage_fails.py [--run DIR] [--jobs N] [--wall S] [--smt-to MS]
                  [--no-reduce] [--max-reduce N] [--limit N] [--out FILE]
"""
import argparse, concurrent.futures as cf, hashlib, os, re, subprocess, sys, glob

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BTV = os.path.join(PROJECT_ROOT, "deps/alive2-arm-tv/build/backend-tv")
REDUCE = os.path.join(PROJECT_ROOT, "scripts/oracles/reduce_backend_miscompile.sh")

SKIP_PREFIX = ("source_filename", "target ", "attributes ", "define ", "}")

def shape_sig(text):
    """Order-independent signature of an IR function body — SSA names normalized,
    align/attr noise dropped, declares (intrinsic names) kept."""
    lines = []
    for ln in text.splitlines():
        s = ln.strip()
        if not s or s.startswith(";") or s.startswith(SKIP_PREFIX) or s.endswith(":"):
            continue
        s = re.sub(r"%[A-Za-z0-9_.]+", "%v", s)
        s = re.sub(r", align \d+", "", s)
        s = re.sub(r" #\d+", "", s)
        s = re.sub(r"\s+", " ", s)
        lines.append(s)
    lines.sort()
    return hashlib.md5("\n".join(lines).encode()).hexdigest()[:12]

def classify(out, rc):
    if rc == 124:
        return "timeout"
    if re.search(r"[1-9]\d* incorrect transformations", out):
        if "Value mismatch" in out:                       return "value-mismatch"
        if "Source is more defined than target" in out:   return "more-defined"
        if "Target is more poisonous than source" in out: return "more-poison"
        return "incorrect-other"
    if re.search(r"[1-9]\d* correct transformations", out) and \
       not re.search(r"[1-9]\d* (incorrect|failed-to-prove) transformations", out):
        return "correct"
    if re.search(r"[1-9]\d* failed-to-prove transformations", out):
        return "failed-to-prove"
    return "error"

def probe(ir_path, isel, wall, smt_to):
    flags = ["--backend=riscv64", f"--smt-to={smt_to}",
             "--disable-undef-input", "--disable-poison-input"]
    if isel == "gisel":
        flags.append("--global-isel")
    try:
        p = subprocess.run([BTV, *flags, ir_path], capture_output=True, text=True,
                           timeout=wall + 5)
        out, rc = p.stdout + p.stderr, p.returncode
    except subprocess.TimeoutExpired:
        out, rc = "", 124
    return classify(out, rc)

def label_reduced(text):
    """Quick known-vs-new label from a reduced witness. The known GISel
    mul_by_neg_one bug is `mul nuw` by -1 — mul is COMMUTATIVE, so -1 may be on
    either side (`mul nuw i8 -1, %x` or `mul nuw i32 %x, -1`), and the -1 may be
    computed (`or %x,-1` / `sext iN -1`). Match all of these."""
    for ln in text.splitlines():
        s = ln.strip()
        if s.startswith(";"):
            continue
        if "mul nuw" in s and re.search(r"(^|[ ,])-1([ ,]|$)", s):
            return "known-nuw"
    body = "\n".join(l for l in text.splitlines() if not l.strip().startswith(";"))
    if ("mul nuw" in body) and re.search(r"(or i\d+ %\w+, -1|sext i\d+ -1)", body):
        return "known-nuw"
    if re.search(r"\b[us]shl\.sat", body):
        return "satshift-freeze"
    return "NEW?"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", default=None)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--wall", type=int, default=12)
    ap.add_argument("--smt-to", type=int, default=6000)
    ap.add_argument("--no-reduce", action="store_true")
    ap.add_argument("--max-reduce", type=int, default=40)
    ap.add_argument("--limit", type=int, default=0, help="cap shapes probed (debug)")
    ap.add_argument("--recorded", default=None,
                    help="comma-list of recorded verdicts to keep (e.g. value-mismatch)")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    run = args.run
    if not run:
        cur = os.path.join(PROJECT_ROOT, "runs/current")
        run = os.path.realpath(cur) if os.path.isdir(cur) else \
              sorted(glob.glob(os.path.join(PROJECT_ROOT, "runs/2026-*")))[-1]
    out = open(args.out, "w") if args.out else sys.stdout
    def emit(*a): print(*a, file=out); out.flush()

    # 1. collect fails
    fails = []
    for shard in sorted(glob.glob(os.path.join(run, "oracle_results/backend_tv_riscv64_*/fail"))):
        isel = "gisel" if "_gisel_" in shard else "dagisel"
        for f in glob.glob(os.path.join(shard, "*")):
            if f.endswith((".hash", ".log")) or not os.path.isfile(f):
                continue
            try:
                text = open(f, errors="replace").read()
            except OSError:
                continue
            log = f + ".log"
            rec = "?"
            if os.path.isfile(log):
                rec = classify(open(log, errors="replace").read(), 0)
            if args.recorded and rec not in args.recorded.split(","):
                continue
            fails.append((f, isel, rec, shape_sig(text), text))
    emit(f"# triage_fails — run {os.path.basename(run)}")
    emit(f"# {len(fails)} raw fails")
    if not fails:
        return

    # 2. group by shape (key = sig+isel: same shape under different selectors differs)
    shapes = {}
    for f, isel, rec, sig, text in fails:
        shapes.setdefault((sig, isel), []).append((f, rec, text))
    emit(f"# {len(shapes)} distinct (shape x isel) groups")
    reps = list(shapes.items())
    if args.limit:
        reps = reps[: args.limit]

    # 3. re-probe each representative with DEFINED inputs (parallel)
    def work(item):
        (sig, isel), members = item
        rep_f, _, _ = members[0]
        return (sig, isel, members, probe(rep_f, isel, args.wall, args.smt_to))
    emit(f"\n## Phase A — reclassify {len(reps)} shapes with DEFINED inputs (jobs={args.jobs})")
    results = []
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for r in ex.map(work, reps):
            results.append(r)

    # reclassification table + mischaracterization flags
    emit(f"\n{'recorded(poison-on)':>22} -> {'defined-input':<16}  shapes  fails")
    cross = {}
    for sig, isel, members, dverdict in results:
        rec = members[0][1]
        cross.setdefault((rec, dverdict), [0, 0])
        cross[(rec, dverdict)][0] += 1
        cross[(rec, dverdict)][1] += len(members)
    for (rec, dv), (ns, nf) in sorted(cross.items(), key=lambda kv: -kv[1][1]):
        flag = ""
        if rec == "more-defined" and dv == "value-mismatch":
            flag = "  <<< MISCHARACTERIZED: dismissed artifact is a REAL miscompile"
        elif rec == "value-mismatch" and dv == "correct":
            flag = "  (poison-only / flaky — verifies correct on defined inputs)"
        emit(f"{rec:>22} -> {dv:<16}  {ns:>5}  {nf:>5}{flag}")

    real = [(sig, isel, members) for sig, isel, members, dv in results if dv == "value-mismatch"]
    mischar = [(sig, isel, members) for sig, isel, members, dv in results
               if dv == "value-mismatch" and members[0][1] == "more-defined"]
    emit(f"\n# real miscompiles (defined-input Value-mismatch): {len(real)} shapes"
         f" ({sum(len(m) for _,_,m in real)} fails); of these {len(mischar)} were MISCHARACTERIZED")

    if args.no_reduce or not real:
        return

    # 4. reduce real ones (in PARALLEL — sequential reduction walls out), group by
    #    reduced signature. Each reduce_backend_miscompile.sh runs its own llvm-reduce.
    emit(f"\n## Phase B — reduce {min(len(real), args.max_reduce)} real shapes "
         f"(parallel, jobs={args.jobs}) & group by reduced form")
    def reduce_one(item):
        sig, isel, members = item
        rep_f = members[0][0]
        red_path = f"/tmp/triage_red_{sig}_{isel}.ll"
        try:
            subprocess.run([REDUCE, "--isel", isel, rep_f, red_path],
                           capture_output=True, text=True, timeout=1200)
        except subprocess.TimeoutExpired:
            pass
        if not os.path.isfile(red_path):
            return (sig, isel, members, None, None)
        return (sig, isel, members, open(red_path, errors="replace").read(), red_path)

    reduced_groups = {}
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for sig, isel, members, rtext, red_path in ex.map(reduce_one, real[: args.max_reduce]):
            if rtext is None:
                emit(f"  [reduce failed] {sig} {isel}")
                continue
            rsig = shape_sig(rtext)
            g = reduced_groups.setdefault(rsig, {"isel": isel, "label": label_reduced(rtext),
                                                 "text": rtext, "raw_shapes": 0, "raw_fails": 0,
                                                 "examples": []})
            g["raw_shapes"] += 1
            g["raw_fails"] += len(members)
            g["examples"].append((members[0][0], red_path))

    emit(f"\n# {len(reduced_groups)} distinct ROOT shapes after reduction:")
    for rsig, g in sorted(reduced_groups.items(), key=lambda kv: -kv[1]["raw_fails"]):
        emit(f"\n===== [{g['label']}] reduced-sig {rsig}  isel={g['isel']}  "
             f"({g['raw_shapes']} raw shapes, {g['raw_fails']} fails) =====")
        emit(f"  example fail: {g['examples'][0][0]}")
        emit(f"  reduced:      {g['examples'][0][1]}")
        for ln in g["text"].splitlines():
            if ln.strip() and not ln.strip().startswith(";"):
                emit("    " + ln)

if __name__ == "__main__":
    main()
