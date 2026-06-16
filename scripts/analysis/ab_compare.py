#!/usr/bin/env python3
"""Compare two fuzzing-run arms (A/B corpus experiment).

Metrics per arm (aligned with the thesis: a denser, feature-distilled corpus
should reach bug-rich df=0 cells faster):
  - fuzzer throughput     : corpus entries generated, backend-tv inputs checked
  - bug yield             : Value-mismatch fails, time-to-first, distinct shapes
  - artifact volume       : "more defined" fails (for context)

Distinct shapes use the LLVM-API feature extractor (build/ir_features): two
Value-mismatch fails with the same context-tuple set count as one shape — a proxy
for distinct bug roots without running the (slow) reducer.
"""
import argparse, glob, os, subprocess, collections, sys

ROOT = "/data/saiva/regatoni"
BIN = f"{ROOT}/build/ir_features"

def run_start(rd):
    p = f"{rd}/pids"
    return os.path.getmtime(p) if os.path.exists(p) else None

def fail_logs(rd, vt):
    out = []
    for f in glob.glob(f"{rd}/oracle_results/backend_tv_riscv64_*/fail/*.log"):
        t = open(f, errors="replace").read()
        if vt == "vm" and "Value mismatch" in t: out.append(f)
        elif vt == "md" and "more defined than target" in t: out.append(f)
    return out

def checked(rd):
    n = 0
    for c in glob.glob(f"{rd}/oracle_results/backend_tv_riscv64_*/checked.log"):
        n += sum(1 for _ in open(c, errors="replace"))
    return n

def corpus_generated(rd):
    # fuzzer-generated entries are hash-named (no .ll extension)
    return sum(1 for f in glob.glob(f"{rd}/corpus/*")
               if os.path.isfile(f) and not f.endswith(".ll"))

def shape_sig(ir_path):
    p = subprocess.run([BIN, ir_path], capture_output=True, text=True)
    tuples = [l for l in p.stdout.splitlines() if l and not l.startswith("===")]
    return frozenset(tuples)

def arm(rd, dur):
    st = run_start(rd)
    vmlogs = fail_logs(rd, "vm")
    vm_irs = [f[:-4] for f in vmlogs]            # strip .log
    shapes = set(shape_sig(ir) for ir in vm_irs) if vm_irs else set()
    tfirst = None
    if vmlogs and st:
        tfirst = min(os.path.getmtime(f) for f in vmlogs) - st
    return {
        "run": os.path.basename(rd),
        "corpus_generated": corpus_generated(rd),
        "checked": checked(rd),
        "value_mismatch": len(vmlogs),
        "vm_distinct_shapes": len(shapes),
        "more_defined": len(fail_logs(rd, "md")),
        "t_first_vm_min": round(tfirst/60, 1) if tfirst is not None else None,
    }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("arm_a"); ap.add_argument("arm_b")
    ap.add_argument("--dur", type=int, default=7200)
    a = ap.parse_args()
    A = arm(f"{ROOT}/runs/{a.arm_a}", a.dur)
    B = arm(f"{ROOT}/runs/{a.arm_b}", a.dur)
    print(f"# A/B corpus experiment  ({a.dur//60} min per arm)\n")
    print(f"{'metric':<22} {'A: distilled(3186)':>20} {'B: full(19082)':>18}")
    print("-"*62)
    for k, label in [("corpus_generated","corpus generated"),
                     ("checked","backend-tv checked"),
                     ("value_mismatch","Value-mismatch fails"),
                     ("vm_distinct_shapes","  distinct shapes"),
                     ("t_first_vm_min","time-to-first VM (min)"),
                     ("more_defined","more-defined (artifact)")]:
        print(f"{label:<22} {str(A[k]):>20} {str(B[k]):>18}")
    print(f"\nA run: {A['run']}   B run: {B['run']}")
    print("\nRead: higher corpus/checked = throughput; more VM + distinct shapes,")
    print("and *smaller* time-to-first = the distilled corpus reaching bugs faster.")

if __name__ == "__main__":
    main()
