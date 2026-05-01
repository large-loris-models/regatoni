#!/usr/bin/env python3
"""
render_triage_report.py — emit triage/report.md from sub_clusters in
miscompilations/dedup.db.

The report is grouped by bucket; each bucket lists its sub-clusters with
the agent-written summary, representative IR snippet, and Alive2 verdict
head. Buckets with no sub-clusters (no successful triage yet) appear at
the end with a NEEDS_TRIAGE marker so they aren't silently dropped.
"""

from __future__ import annotations

import argparse
import os
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DB_PATH = REPO_ROOT / "miscompilations" / "dedup.db"


def truncate(text: str, max_lines: int, max_chars: int) -> str:
    lines = text.splitlines()
    if len(lines) > max_lines:
        lines = lines[:max_lines] + [f"... [{len(lines) - max_lines} more lines]"]
    out = "\n".join(lines)
    if len(out) > max_chars:
        out = out[:max_chars] + "\n... [truncated]"
    return out


def render(db_path: Path) -> str:
    conn = sqlite3.connect(str(db_path))
    try:
        n_findings = conn.execute("SELECT COUNT(*) FROM findings").fetchone()[0]
        n_buckets = conn.execute("SELECT COUNT(*) FROM buckets").fetchone()[0]
        n_sc = conn.execute("SELECT COUNT(*) FROM sub_clusters").fetchone()[0]
        n_untriaged = conn.execute(
            "SELECT COUNT(*) FROM findings f "
            "LEFT JOIN finding_sub_cluster fsc ON fsc.finding_id = f.finding_id "
            "WHERE fsc.sub_cluster_id IS NULL"
        ).fetchone()[0]

        # Bucket header info, ordered by finding_count desc.
        buckets = conn.execute(
            "SELECT bucket_id, guilty_pass, bisect_index, finding_count "
            "FROM buckets ORDER BY finding_count DESC, bucket_id ASC"
        ).fetchall()

        sc_by_bucket: dict[int, list[tuple]] = {}
        for r in conn.execute(
            "SELECT sub_cluster_id, bucket_id, representative_id, summary, "
            "triaged_at FROM sub_clusters ORDER BY bucket_id, sub_cluster_id"
        ).fetchall():
            sc_by_bucket.setdefault(r[1], []).append(r)

        members_by_sc: dict[int, list[int]] = {}
        for sc_id, fid in conn.execute(
            "SELECT sub_cluster_id, finding_id FROM finding_sub_cluster "
            "ORDER BY sub_cluster_id, finding_id"
        ).fetchall():
            members_by_sc.setdefault(sc_id, []).append(fid)

        finding_meta: dict[int, tuple[str, str]] = {}
        for fid, npath, etext in conn.execute(
            "SELECT finding_id, normalized_path, error_text FROM findings"
        ).fetchall():
            finding_meta[fid] = (npath or "", etext or "")
    finally:
        conn.close()

    ts = datetime.now(timezone.utc).isoformat()
    out: list[str] = []
    out.append(f"# Triage Report — {ts}\n")
    out.append(
        f"{n_findings} total findings, {n_buckets} buckets, "
        f"{n_sc} distinct sub-clusters identified, "
        f"{n_untriaged} finding(s) untriaged.\n"
    )

    if n_sc == 0:
        out.append("No sub-clusters yet — run `triage_miscompilations.sh` to "
                   "produce them.\n")

    for bucket_id, guilty_pass, bisect_index, finding_count in buckets:
        scs = sc_by_bucket.get(bucket_id, [])
        gp = guilty_pass if guilty_pass is not None else "(none — bisect failed/asan)"
        bi = bisect_index if bisect_index is not None else "—"
        if not scs:
            out.append(
                f"## Bucket: {gp} @ idx={bi} ({finding_count} findings, "
                f"NEEDS_TRIAGE)\n"
            )
            out.append("_No sub-clusters yet for this bucket._\n")
            continue
        out.append(
            f"## Bucket: {gp} @ idx={bi} ({finding_count} findings, "
            f"{len(scs)} sub-clusters)\n"
        )
        for idx, (sc_id, _bid, rep_id, summary, triaged_at) in enumerate(scs, 1):
            members = members_by_sc.get(sc_id, [])
            rep_path, rep_err = finding_meta.get(rep_id, ("", ""))
            rep_basename = Path(rep_path).name if rep_path else f"finding_{rep_id}"
            out.append(f"### Sub-cluster {idx}: {summary or '(no summary)'}\n")
            out.append(f"- sub_cluster_id: `{sc_id}`")
            out.append(f"- representative: finding `{rep_id}` (`{rep_basename}`)")
            out.append(f"- members ({len(members)}): "
                       + ", ".join(f"`{m}`" for m in members))
            out.append(f"- triaged_at: {triaged_at}")
            ir_text = ""
            try:
                if rep_path and Path(rep_path).is_file():
                    ir_text = Path(rep_path).read_text(errors="replace")
            except OSError:
                ir_text = ""
            if ir_text:
                out.append("\n<details><summary>Representative IR</summary>\n")
                out.append("\n```llvm")
                out.append(truncate(ir_text, 80, 4000))
                out.append("```\n")
                out.append("</details>\n")
            if rep_err:
                out.append("<details><summary>Alive2 verdict (head)</summary>\n")
                out.append("\n```")
                out.append(truncate(rep_err, 30, 2000))
                out.append("```\n")
                out.append("</details>\n")

    return "\n".join(out) + "\n"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", required=True)
    p.add_argument("--db", default=str(DB_PATH))
    args = p.parse_args()
    text = render(Path(args.db))
    Path(args.output).write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
