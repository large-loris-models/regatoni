#!/usr/bin/env python3
"""Triage miscompilations: bisect guilty pass and bucket via LLM."""

import json
import os
import subprocess
import sys
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

from dotenv import load_dotenv
from openai import OpenAI

PROJECT_ROOT = Path(__file__).resolve().parents[2]
MISC_DIR = PROJECT_ROOT / "miscompilations"
OPT = PROJECT_ROOT / "deps/llvm-build-sancov/bin/opt"
ALIVE_TV = PROJECT_ROOT / "deps/alive2/build/alive-tv"
FUZZ_ALIVE2 = PROJECT_ROOT / "build/opt_fuzz_target_alive2"

CANDIDATE_PASSES = [
    "instcombine", "gvn", "sroa", "sccp", "simplifycfg", "early-cse",
    "reassociate", "licm", "loop-rotate", "loop-unroll", "indvars",
    "dse", "adce", "jump-threading", "correlated-propagation",
    "mem2reg", "newgvn", "instsimplify", "aggressive-instcombine",
    "loop-idiom", "loop-deletion", "tailcallelim",
]

TIMEOUT = 60

FALSE_POSITIVE_MARKERS = ("function did not return!", "undef")


def is_false_positive(alive2_err: str) -> bool:
    return any(m in alive2_err for m in FALSE_POSITIVE_MARKERS)


def run(cmd, timeout=TIMEOUT, input_bytes=None):
    try:
        return subprocess.run(
            cmd, capture_output=True, timeout=timeout, input=input_bytes
        )
    except subprocess.TimeoutExpired:
        return None


def capture_alive2_error(ll_path):
    r = run([str(FUZZ_ALIVE2), str(ll_path)])
    if r is None:
        return "timeout"
    return (r.stderr or b"").decode("utf-8", errors="replace")


def get_optimized_ir(ll_path):
    r = run([str(OPT), "-O2", "-S", str(ll_path)])
    if r is None or r.returncode != 0:
        return ""
    return r.stdout.decode("utf-8", errors="replace")


def pass_unsound(ll_path, pass_name):
    """Run a single pass then use alive-tv to compare src vs optimized."""
    opt_r = run([str(OPT), f"-passes={pass_name}", "-S", str(ll_path)])
    if opt_r is None or opt_r.returncode != 0:
        return False, ""
    optimized = opt_r.stdout
    if not optimized.strip():
        return False, ""
    tv = run([str(ALIVE_TV), "--smt-to=10000", str(ll_path), "-"],
             input_bytes=optimized, timeout=120)
    if tv is None:
        return False, ""
    out = (tv.stdout or b"").decode("utf-8", errors="replace") + \
          (tv.stderr or b"").decode("utf-8", errors="replace")
    # Alive2 reports incorrect transformations with this phrase
    if "Transformation doesn't verify" in out or "ERROR:" in out and "incorrect" in out.lower():
        return True, out
    return False, out


def _bisect_worker(args):
    ll_path, pass_name = args
    ok, _ = pass_unsound(ll_path, pass_name)
    return pass_name if ok else None


def bisect_pass(ll_path):
    """Return name of first pass that shows unsoundness, or None."""
    for p in CANDIDATE_PASSES:
        ok, _ = pass_unsound(ll_path, p)
        if ok:
            return p
    return None


def process_file(ll_path):
    ll_path = Path(ll_path)
    src = ll_path.read_text(errors="replace")
    alive2_err = capture_alive2_error(ll_path)
    optimized = get_optimized_ir(ll_path)
    guilty = bisect_pass(ll_path)
    return {
        "file": str(ll_path.relative_to(PROJECT_ROOT)),
        "src": src,
        "optimized": optimized,
        "alive2_err": alive2_err,
        "guilty_pass": guilty,
    }


def classify(client, entry):
    prompt = f"""You are triaging an LLVM miscompilation reported by Alive2.

First, decide whether the report is a REAL miscompilation or a trivial/spurious
one that stems from an Alive2 limitation rather than a genuine optimizer bug.
Mark `trivial: true` when the evidence clearly points to one of:
  - Alive2 approximating something it cannot model precisely (e.g. unsupported
    intrinsics, inline asm, opaque external calls, volatile/atomic ordering
    nuances, floating-point signaling/denormal corner cases Alive2 warns about)
  - Source already has UB and the optimizer is legitimately allowed to exploit
    it (e.g. "source is always UB", "target is more defined than source" where
    source has clear UB like OOB access, signed overflow with nsw, etc.)
  - Poison/undef refinement that is sound per LangRef (optimizer narrowing an
    undef/poison value to a concrete one)
  - Trivially equivalent IR where Alive2 flags a meaningless difference
Mark `trivial: false` when the Alive2 error describes a concrete value/memory
mismatch that reflects a real semantic divergence. When unsure, prefer false.

Then produce a SHORT bucket label (3-7 words, lowercase, no punctuation) that
describes the *specific kind of unsoundness Alive2 is reporting*. Derive the
label from the actual Alive2 ERROR line and the diff between source and
optimized IR -- do NOT pick from a fixed menu, and do NOT invent a category
that is more abstract than what the evidence supports. Two reports should get
the same label iff they describe the same underlying semantic divergence.

Good labels look like: "return domain mismatch", "value mismatch on store",
"target has ub source does not", "extra poison in target", "memory contents
differ". Bad labels: generic tags like "other", "misc", "wrong optimization".

Respond with JSON: {{"trivial": true|false, "bucket": "...", "reason": "one sentence tying the label (and triviality decision) to the Alive2 ERROR"}}

Guilty pass: {entry['guilty_pass']}

Source IR:
{entry['src'][:3000]}

Optimized IR:
{entry['optimized'][:3000]}

Alive2 error:
{entry['alive2_err'][:2000]}
"""
    try:
        resp = client.chat.completions.create(
            model="claude-opus-4-6-v1",
            messages=[{"role": "user", "content": prompt}],
            temperature=0,
        )
        text = resp.choices[0].message.content.strip()
        # extract json
        start = text.find("{")
        end = text.rfind("}")
        if start >= 0 and end > start:
            return json.loads(text[start:end + 1])
    except Exception as e:
        return {"bucket": "unclassified", "reason": f"LLM error: {e}"}
    return {"bucket": "unclassified", "reason": "no parse"}


def dedupe_key(entry):
    # Dedupe by (guilty_pass, normalized alive2 error first lines)
    err = entry["alive2_err"]
    lines = [l.strip() for l in err.splitlines() if l.strip()]
    sig = "\n".join(lines[:6])
    return (entry["guilty_pass"] or "unknown", sig)


def main():
    load_dotenv()
    files = sorted(MISC_DIR.glob("*"))
    files = [f for f in files if f.is_file() and f.name != "report.md"]
    print(f"[triage] {len(files)} files", file=sys.stderr)

    workers = max(1, min(8, os.cpu_count() or 4))
    entries = []
    with ProcessPoolExecutor(max_workers=workers) as ex:
        futs = {ex.submit(process_file, f): f for f in files}
        for i, fut in enumerate(as_completed(futs), 1):
            try:
                entries.append(fut.result())
            except Exception as e:
                print(f"[triage] error on {futs[fut]}: {e}", file=sys.stderr)
            if i % 10 == 0:
                print(f"[triage] processed {i}/{len(files)}", file=sys.stderr)

    # Drop Alive2 false positives (recursion sentinels, undef-related noise)
    total_entries = len(entries)
    entries = [e for e in entries if not is_false_positive(e["alive2_err"])]
    dropped = total_entries - len(entries)
    print(f"[triage] dropped {dropped} false positives "
          f"(undef / 'function did not return!')", file=sys.stderr)

    # Dedupe
    seen = {}
    for e in entries:
        k = dedupe_key(e)
        seen.setdefault(k, []).append(e)

    unique = [v[0] for v in seen.values()]
    print(f"[triage] {len(unique)} unique after dedup", file=sys.stderr)

    client = OpenAI(
        base_url="https://tritonai-api.ucsd.edu/v1",
        api_key=os.environ["TRITON_API_KEY"],
    )

    buckets = defaultdict(list)
    trivial = []
    for e in unique:
        cls = classify(client, e)
        e["bucket"] = cls.get("bucket", "unclassified")
        e["reason"] = cls.get("reason", "")
        e["trivial"] = bool(cls.get("trivial", False))
        if e["trivial"]:
            trivial.append(e)
        else:
            buckets[e["bucket"]].append(e)
    print(f"[triage] LLM flagged {len(trivial)} trivial / alive2-limitation entries",
          file=sys.stderr)

    # Count dups per bucket
    report = ["# Miscompilation Triage Report\n",
              f"Total files: {len(files)}  |  "
              f"False positives filtered: {dropped}  |  "
              f"Real miscompilations: {len(entries)}  |  "
              f"Unique signatures: {len(unique)}  |  "
              f"LLM-flagged trivial: {len(trivial)}\n"]
    report.append("\n## Bucket summary\n")
    report.append("| Bucket | Unique | Total files |")
    report.append("|---|---|---|")
    for b, items in sorted(buckets.items(), key=lambda x: -len(x[1])):
        total = sum(len(seen[dedupe_key(it)]) for it in items)
        report.append(f"| {b} | {len(items)} | {total} |")

    report.append("\n## Representative examples\n")
    for b, items in sorted(buckets.items(), key=lambda x: -len(x[1])):
        rep = items[0]
        report.append(f"### {b}\n")
        report.append(f"- **Guilty pass:** `{rep['guilty_pass']}`")
        report.append(f"- **Reason:** {rep['reason']}")
        report.append(f"- **Example file:** `{rep['file']}`")
        report.append(f"- **Unique signatures in bucket:** {len(items)}")
        report.append("\n<details><summary>Alive2 error</summary>\n\n```\n"
                      + rep["alive2_err"][:2000] + "\n```\n</details>\n")
        report.append("<details><summary>Source IR</summary>\n\n```llvm\n"
                      + rep["src"][:2000] + "\n```\n</details>\n")

    if trivial:
        report.append("\n## Trivial / Alive2-limitation (LLM-filtered)\n")
        report.append("| File | Bucket | Guilty pass | Reason |")
        report.append("|---|---|---|---|")
        for e in trivial:
            report.append(
                f"| `{e['file']}` | {e['bucket']} | `{e['guilty_pass']}` | {e['reason']} |"
            )

    report.append("\n## All unique entries (non-trivial)\n")
    report.append("| File | Bucket | Guilty pass |")
    report.append("|---|---|---|")
    for e in unique:
        if e.get("trivial"):
            continue
        report.append(f"| `{e['file']}` | {e['bucket']} | `{e['guilty_pass']}` |")

    out = MISC_DIR / "report.md"
    out.write_text("\n".join(report))
    print(f"[triage] wrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
