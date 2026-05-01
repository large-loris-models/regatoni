#!/usr/bin/env python3
"""
triage_buckets.py — orchestrate per-bucket sub-clustering.

For each bucket that still has untriaged findings:

  - Stage all (or a deterministic sample of) findings into a temp dir as
    <finding_id>.ll + <finding_id>.log + BUCKET_CONTEXT.md + the prompt.
  - Invoke the configured LLM backend (claude / codex), expecting it to
    write sub_clusters.json into the working dir.
  - Validate the JSON and persist via dedup.py record-sub-clusters.
  - If the bucket was sampled, run a second pass on the un-clustered
    remainder using assign mode (existing sub-clusters are shown to the
    agent; new findings are assigned to one of them or to a new
    sub-cluster).

Per-bucket gating uses finding_sub_cluster membership as the source of
truth (not a global timestamp). Failure on one bucket does not block
others — it just leaves the bucket's findings untriaged so a later run
can retry.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_DIR = Path(__file__).resolve().parent
DEDUP_PY = SCRIPT_DIR / "dedup.py"
PROMPT_SRC = SCRIPT_DIR / "BUCKET_TRIAGE_PROMPT.md"


def run_dedup(*args: str, capture: bool = False) -> str:
    cmd = ["python3", str(DEDUP_PY), *args]
    if capture:
        return subprocess.check_output(cmd, text=True)
    subprocess.check_call(cmd)
    return ""


def fetch_findings_in_bucket(bucket_id: int) -> list[dict]:
    return json.loads(run_dedup("findings-in-bucket", str(bucket_id), capture=True))


def fetch_untriaged_in_bucket(bucket_id: int) -> list[int]:
    out = run_dedup("untriaged-in-bucket", str(bucket_id), capture=True).strip()
    if not out:
        return []
    return [int(x) for x in out.splitlines()]


def deterministic_sample(items: list[dict], n: int) -> list[dict]:
    """Sort by finding_id ascending, take n evenly-spaced indices."""
    items_sorted = sorted(items, key=lambda f: f["finding_id"])
    if len(items_sorted) <= n:
        return items_sorted
    step = (len(items_sorted) - 1) / (n - 1)
    picks = [items_sorted[round(i * step)] for i in range(n)]
    # round-collisions are possible; dedup while preserving order.
    seen: set[int] = set()
    out: list[dict] = []
    for f in picks:
        if f["finding_id"] in seen:
            continue
        seen.add(f["finding_id"])
        out.append(f)
    # Edge case: if collisions reduced the sample, top up with the next
    # unused finding_ids until we have n (or run out).
    if len(out) < n:
        remaining = [f for f in items_sorted if f["finding_id"] not in seen]
        out.extend(remaining[: n - len(out)])
    return out


def stage_bucket_dir(
    work_dir: Path,
    bucket_id: int,
    bucket_meta: dict,
    findings: list[dict],
    *,
    sample_of: int | None = None,
    existing_sub_clusters: list[dict] | None = None,
) -> None:
    """
    bucket_meta: {guilty_pass, bisect_index, total_finding_count}
    sample_of: total bucket size if `findings` is a strict subset; else None.
    existing_sub_clusters: when present, EXISTING_SUB_CLUSTERS.md is staged
      and the agent runs in assign mode.
    """
    work_dir.mkdir(parents=True, exist_ok=True)
    for f in findings:
        fid = f["finding_id"]
        src = Path(f["normalized_path"])
        dest_ll = work_dir / f"{fid}.ll"
        if src.is_file():
            shutil.copyfile(src, dest_ll)
        else:
            dest_ll.write_text(f"; missing IR file: {src}\n")
        (work_dir / f"{fid}.log").write_text(f.get("error_text") or "")

    ctx_lines = [
        f"# Bucket {bucket_id}",
        "",
        f"- guilty_pass: `{bucket_meta['guilty_pass']}`",
        f"- bisect_index: {bucket_meta['bisect_index']}",
        f"- total findings in bucket: {bucket_meta['total_finding_count']}",
    ]
    if sample_of is not None:
        ctx_lines.append(
            f"- sample size (this directory): {len(findings)} of {sample_of} "
            "(deterministic, sorted-by-finding-id, evenly-spaced)"
        )
        ctx_lines.append(
            "- a second pass will assign the remaining findings to your "
            "sub-clusters or create new ones."
        )
    else:
        ctx_lines.append(f"- findings in this directory: {len(findings)}")
    if existing_sub_clusters is not None:
        ctx_lines.append("- mode: **assign** — existing sub-clusters listed in "
                         "EXISTING_SUB_CLUSTERS.md must be considered first.")
    (work_dir / "BUCKET_CONTEXT.md").write_text("\n".join(ctx_lines) + "\n")

    shutil.copyfile(PROMPT_SRC, work_dir / "BUCKET_TRIAGE_PROMPT.md")

    if existing_sub_clusters is not None:
        lines = ["# Existing sub-clusters in this bucket", ""]
        for sc in existing_sub_clusters:
            lines.append(f"## sub_cluster_id = {sc['sub_cluster_id']}")
            lines.append("")
            lines.append(f"- representative_finding_id: {sc['representative_finding_id']}")
            lines.append(f"- summary: {sc['summary']}")
            lines.append("")
            lines.append("Representative IR:")
            lines.append("")
            lines.append("```llvm")
            ir_path = Path(sc.get("representative_normalized_path") or "")
            if ir_path.is_file():
                lines.append(ir_path.read_text(errors="replace").rstrip())
            else:
                lines.append("; (representative IR file unavailable)")
            lines.append("```")
            lines.append("")
            lines.append("Alive2 verdict (head):")
            lines.append("")
            lines.append("```")
            err = (sc.get("representative_error_text") or "").strip().splitlines()
            lines.extend(err[:30])
            lines.append("```")
            lines.append("")
        (work_dir / "EXISTING_SUB_CLUSTERS.md").write_text("\n".join(lines))


def invoke_agent(work_dir: Path, backend: str, backend_bin: str,
                 prompt: str, log_path: Path) -> bool:
    if backend == "claude":
        cmd = [backend_bin, "--dangerously-skip-permissions", "-p", prompt]
        cwd = str(work_dir)
    elif backend == "codex":
        cmd = [backend_bin, "exec", "--dangerously-bypass-approvals-and-sandbox",
               "-C", str(work_dir), prompt]
        cwd = None
    else:
        raise SystemExit(f"unknown backend: {backend}")
    with open(log_path, "ab") as logf:
        logf.write(f"\n=== invoking {backend} in {work_dir} ===\n".encode())
        logf.flush()
        proc = subprocess.run(cmd, cwd=cwd, stdout=logf, stderr=subprocess.STDOUT)
    return proc.returncode == 0


def load_sub_clusters_json(path: Path) -> list[dict]:
    payload = json.loads(path.read_text())
    if not isinstance(payload, list):
        raise ValueError("top-level must be a list")
    for i, sc in enumerate(payload):
        if not isinstance(sc, dict):
            raise ValueError(f"entry #{i} not an object")
        if "member_finding_ids" not in sc or not isinstance(sc["member_finding_ids"], list):
            raise ValueError(f"entry #{i}: missing/invalid member_finding_ids")
    return payload


def triage_one_bucket(
    bucket_id: int,
    *,
    backend: str,
    backend_bin: str,
    log_path: Path,
    keep_workdirs: bool,
    max_per_bucket: int,
    force: bool,
) -> tuple[bool, str]:
    """
    Returns (success, note). On failure, leaves the bucket untriaged.
    """
    findings = fetch_findings_in_bucket(bucket_id)
    if not findings:
        return True, "empty bucket"

    bucket_meta = {
        "guilty_pass": None,
        "bisect_index": None,
        "total_finding_count": len(findings),
    }
    # Pull guilty_pass / bisect_index via dedup bucket-info (one extra call).
    info_text = run_dedup("bucket-info", str(bucket_id), capture=True)
    for line in info_text.splitlines():
        if line.startswith("guilty_pass:"):
            bucket_meta["guilty_pass"] = line.split(":", 1)[1].strip()
        elif line.startswith("bisect_index:"):
            bucket_meta["bisect_index"] = line.split(":", 1)[1].strip()

    # Decide first-pass vs assign-only.
    existing = []
    sub_clusters_text = run_dedup("sub-clusters-since",
                                  "1970-01-01T00:00:00+00:00", capture=True)
    all_existing = json.loads(sub_clusters_text)
    bucket_existing = [sc for sc in all_existing if sc["bucket_id"] == bucket_id]

    if bucket_existing and not force:
        # Assign-mode only: bucket already triaged, new findings appeared.
        untriaged_ids = set(fetch_untriaged_in_bucket(bucket_id))
        if not untriaged_ids:
            return True, "already fully triaged"
        new_findings = [f for f in findings if f["finding_id"] in untriaged_ids]
        return assign_pass(
            bucket_id, bucket_meta, new_findings, bucket_existing,
            backend=backend, backend_bin=backend_bin,
            log_path=log_path, keep_workdirs=keep_workdirs,
        )

    # First-pass / forced re-triage.
    if force and bucket_existing:
        # Clearing prior sub-clusters happens via record-sub-clusters --replace.
        pass

    sample = None
    findings_for_agent = findings
    if len(findings) > max_per_bucket:
        findings_for_agent = deterministic_sample(findings, max_per_bucket)
        sample = len(findings)

    work_dir = Path(tempfile.mkdtemp(prefix=f"triage_b{bucket_id}_"))
    try:
        stage_bucket_dir(
            work_dir, bucket_id, bucket_meta, findings_for_agent,
            sample_of=sample,
        )
        prompt = (
            "Read BUCKET_TRIAGE_PROMPT.md and BUCKET_CONTEXT.md. Cluster the "
            "findings in this directory and write sub_clusters.json. Do not "
            "ask for input."
        )
        if not invoke_agent(work_dir, backend, backend_bin, prompt, log_path):
            return False, "agent invocation failed"

        sc_json = work_dir / "sub_clusters.json"
        if not sc_json.is_file():
            return False, "agent did not write sub_clusters.json"
        try:
            payload = load_sub_clusters_json(sc_json)
        except Exception as e:
            return False, f"sub_clusters.json invalid: {e}"

        # Sanity: every staged finding must appear in exactly one sub-cluster.
        staged_ids = {f["finding_id"] for f in findings_for_agent}
        seen = []
        for sc in payload:
            seen.extend(sc["member_finding_ids"])
        if set(seen) != staged_ids or len(seen) != len(staged_ids):
            return False, (f"agent did not partition staged findings: "
                           f"expected {sorted(staged_ids)}, got {sorted(seen)}")

        cmd = ["python3", str(DEDUP_PY), "record-sub-clusters", str(bucket_id),
               "--json", str(sc_json)]
        if force and bucket_existing:
            cmd.append("--replace")
        rc = subprocess.call(cmd)
        if rc != 0:
            return False, "record-sub-clusters rejected the payload"
    finally:
        if not keep_workdirs:
            shutil.rmtree(work_dir, ignore_errors=True)

    if sample is None:
        return True, f"{len(payload)} sub-cluster(s)"

    # Second pass: assign the remaining un-clustered findings.
    untriaged_ids = set(fetch_untriaged_in_bucket(bucket_id))
    if not untriaged_ids:
        return True, f"{len(payload)} sub-cluster(s) (no remainder)"
    remaining = [f for f in findings if f["finding_id"] in untriaged_ids]
    sub_clusters_after = json.loads(run_dedup(
        "sub-clusters-since", "1970-01-01T00:00:00+00:00", capture=True))
    bucket_existing_after = [sc for sc in sub_clusters_after if sc["bucket_id"] == bucket_id]
    ok, note = assign_pass(
        bucket_id, bucket_meta, remaining, bucket_existing_after,
        backend=backend, backend_bin=backend_bin,
        log_path=log_path, keep_workdirs=keep_workdirs,
    )
    return ok, f"first-pass: {len(payload)} sub-cluster(s); second-pass: {note}"


def assign_pass(
    bucket_id: int,
    bucket_meta: dict,
    findings_to_assign: list[dict],
    existing_sub_clusters: list[dict],
    *,
    backend: str,
    backend_bin: str,
    log_path: Path,
    keep_workdirs: bool,
) -> tuple[bool, str]:
    work_dir = Path(tempfile.mkdtemp(prefix=f"triage_b{bucket_id}_assign_"))
    try:
        stage_bucket_dir(
            work_dir, bucket_id, bucket_meta, findings_to_assign,
            existing_sub_clusters=existing_sub_clusters,
        )
        prompt = (
            "Read BUCKET_TRIAGE_PROMPT.md, BUCKET_CONTEXT.md, and "
            "EXISTING_SUB_CLUSTERS.md. Assign every finding in this "
            "directory to an existing sub-cluster (set "
            "existing_sub_cluster_id) or place it in a new sub-cluster. "
            "Write sub_clusters.json. Do not ask for input."
        )
        if not invoke_agent(work_dir, backend, backend_bin, prompt, log_path):
            return False, "agent invocation failed"
        sc_json = work_dir / "sub_clusters.json"
        if not sc_json.is_file():
            return False, "agent did not write sub_clusters.json"
        try:
            payload = load_sub_clusters_json(sc_json)
        except Exception as e:
            return False, f"sub_clusters.json invalid: {e}"

        new_ids = {f["finding_id"] for f in findings_to_assign}
        existing_sc_ids = {sc["sub_cluster_id"] for sc in existing_sub_clusters}
        assign_entries = []  # for assign-existing
        new_entries = []     # for record-sub-clusters
        seen: set[int] = set()
        for i, sc in enumerate(payload):
            members = sc["member_finding_ids"]
            for m in members:
                if m not in new_ids:
                    return False, (f"entry #{i}: finding {m} not in the new "
                                   f"finding set ({sorted(new_ids)})")
                if m in seen:
                    return False, f"entry #{i}: finding {m} listed twice"
                seen.add(m)
            esc = sc.get("existing_sub_cluster_id")
            if esc is not None:
                if esc not in existing_sc_ids:
                    return False, (f"entry #{i}: existing_sub_cluster_id {esc} "
                                   f"is not in this bucket")
                assign_entries.append({"sub_cluster_id": esc, "finding_ids": members})
            else:
                new_entries.append(sc)
        if seen != new_ids:
            missing = sorted(new_ids - seen)
            return False, f"agent did not assign: {missing}"

        if assign_entries:
            ae_path = work_dir / "_assign_existing.json"
            ae_path.write_text(json.dumps(assign_entries))
            rc = subprocess.call([
                "python3", str(DEDUP_PY), "assign-existing", "--json", str(ae_path),
            ])
            if rc != 0:
                return False, "assign-existing rejected the payload"
        if new_entries:
            ne_path = work_dir / "_new_sub_clusters.json"
            ne_path.write_text(json.dumps(new_entries))
            rc = subprocess.call([
                "python3", str(DEDUP_PY), "record-sub-clusters",
                str(bucket_id), "--json", str(ne_path),
            ])
            if rc != 0:
                return False, "record-sub-clusters (assign-mode new) rejected"
    finally:
        if not keep_workdirs:
            shutil.rmtree(work_dir, ignore_errors=True)

    return True, (f"assigned {sum(len(e['finding_ids']) for e in assign_entries)}, "
                  f"created {len(new_entries)} new sub-cluster(s)")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--backend", required=True, choices=["claude", "codex"])
    p.add_argument("--backend-bin", required=True)
    p.add_argument("--log", required=True, help="path for agent stdout/stderr")
    p.add_argument("--max-per-bucket", type=int,
                   default=int(os.environ.get("TRIAGE_MAX_PER_BUCKET", "15")))
    p.add_argument("--keep-workdirs", action="store_true")
    p.add_argument("--force", action="store_true",
                   help="re-triage every bucket from scratch")
    p.add_argument("--bucket", type=int, action="append",
                   help="restrict to these bucket ids (default: all needing triage)")
    args = p.parse_args()

    if args.force:
        # Every bucket with at least one finding is a candidate.
        import sqlite3
        conn = sqlite3.connect(str(REPO_ROOT / "miscompilations" / "dedup.db"))
        try:
            buckets = [r[0] for r in conn.execute(
                "SELECT bucket_id FROM buckets ORDER BY bucket_id"
            ).fetchall()]
        finally:
            conn.close()
    else:
        # Buckets with at least one untriaged finding.
        import sqlite3
        conn = sqlite3.connect(str(REPO_ROOT / "miscompilations" / "dedup.db"))
        try:
            buckets = [r[0] for r in conn.execute(
                "SELECT DISTINCT f.bucket_id FROM findings f "
                "LEFT JOIN finding_sub_cluster fsc ON fsc.finding_id = f.finding_id "
                "WHERE fsc.sub_cluster_id IS NULL "
                "ORDER BY f.bucket_id"
            ).fetchall()]
        finally:
            conn.close()

    if args.bucket:
        keep = set(args.bucket)
        buckets = [b for b in buckets if b in keep]

    log_path = Path(args.log)
    log_path.parent.mkdir(parents=True, exist_ok=True)

    if not buckets:
        print("[triage_buckets] no buckets need triage", file=sys.stderr)
        return 0

    print(f"[triage_buckets] processing {len(buckets)} bucket(s): {buckets}",
          file=sys.stderr)

    failures: list[tuple[int, str]] = []
    for bid in buckets:
        print(f"[triage_buckets] bucket {bid}: starting", file=sys.stderr)
        try:
            ok, note = triage_one_bucket(
                bid,
                backend=args.backend, backend_bin=args.backend_bin,
                log_path=log_path, keep_workdirs=args.keep_workdirs,
                max_per_bucket=args.max_per_bucket, force=args.force,
            )
        except Exception as e:
            ok, note = False, f"exception: {e!r}"
        print(f"[triage_buckets] bucket {bid}: {'ok' if ok else 'FAIL'} — {note}",
              file=sys.stderr)
        if not ok:
            failures.append((bid, note))

    if failures:
        print(f"[triage_buckets] {len(failures)} bucket(s) failed:",
              file=sys.stderr)
        for bid, note in failures:
            print(f"  bucket {bid}: {note}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
