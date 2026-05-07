#!/usr/bin/env python3
"""Render a per-run live dashboard for a Regatoni fuzzing run.

Reads from manifest.json, fuzzing-stats CSV, batch-stats TSV, mutation-stats
CSVs, and the project-root dedup.db. Writes a single self-contained HTML file
that auto-refreshes every 60s and renders Plotly plots from CDN with inlined
JSON data.

Read-only against all data sources.
"""

import argparse
import csv
import glob
import html
import json
import os
import sqlite3
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_DB = REPO_ROOT / "dedup.db"
DEFAULT_RUN_DIR = REPO_ROOT / "runs" / "current"
RUNS_DIR = REPO_ROOT / "runs"

PLOTLY_CDN = "https://cdn.plot.ly/plotly-2.27.0.min.js"


# ── Helpers ─────────────────────────────────────────────────────────────────


def fmt_iso(ts_seconds: float) -> str:
    return datetime.fromtimestamp(ts_seconds, tz=timezone.utc).strftime(
        "%Y-%m-%d %H:%M:%S UTC"
    )


def fmt_elapsed(seconds: float) -> str:
    seconds = int(seconds)
    h, rem = divmod(seconds, 3600)
    m, s = divmod(rem, 60)
    return f"{h}h {m}m {s}s"


def parse_iso8601(s: str) -> float:
    if s.endswith("Z"):
        s = s[:-1] + "+00:00"
    return datetime.fromisoformat(s).timestamp()


def rolling_median(values, window):
    if not values:
        return []
    out = []
    for i in range(len(values)):
        lo = max(0, i - window + 1)
        chunk = sorted(values[lo : i + 1])
        n = len(chunk)
        if n % 2:
            out.append(chunk[n // 2])
        else:
            out.append(0.5 * (chunk[n // 2 - 1] + chunk[n // 2]))
    return out


# ── Data loaders ────────────────────────────────────────────────────────────


def load_manifest(run_dir: Path) -> dict:
    p = run_dir / "manifest.json"
    if not p.exists():
        return {}
    try:
        with open(p) as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError):
        return {}


def load_fuzzing_stats(run_dir: Path) -> list:
    """Returns a list of dicts (one per data row, skipping the all-zero warmup
    row only if Centipede emitted one and there is real data after it)."""
    paths = sorted(glob.glob(str(run_dir / "workdir" / "fuzzing-stats-*.csv")))
    rows = []
    for p in paths:
        try:
            with open(p) as f:
                reader = csv.DictReader(f)
                for r in reader:
                    rows.append(r)
        except OSError:
            continue
    # Sort by UnixMicros_Max if present.
    def _t(r):
        try:
            return int(r.get("UnixMicros_Max", "0") or 0)
        except ValueError:
            return 0

    rows.sort(key=_t)
    return rows


def load_batch_stats(run_dir: Path) -> list:
    p = run_dir / "stats" / "regatoni-batch-stats.tsv"
    if not p.exists():
        # Also accept the un-prefixed name from the spec.
        alt = run_dir / "stats" / "batch-stats.tsv"
        if alt.exists():
            p = alt
        else:
            return []
    rows = []
    try:
        with open(p) as f:
            reader = csv.DictReader(f, delimiter="\t")
            for r in reader:
                rows.append(r)
    except OSError:
        return []
    return rows


def load_mutation_stats(run_dir: Path) -> list:
    """Aggregate per-mutator counters across PIDs.

    Each per-PID file is append-only with periodic snapshots of cumulative
    counters per mutator. Take the *last* row per (PID, mutator_id) and sum
    across PIDs to get the run-wide totals.
    """
    # The harness writes per-PID files into the workdir (REGATONI_WORKDIR), but
    # also accept the spec'd location under stats/.
    candidates = []
    candidates += glob.glob(str(run_dir / "workdir" / "regatoni-mutation-stats.*.csv"))
    candidates += glob.glob(str(run_dir / "stats" / "mutation-stats.*.csv"))
    if not candidates:
        return []

    by_mutator = {}  # mutator_id -> dict
    for path in candidates:
        try:
            with open(path) as f:
                reader = csv.DictReader(f)
                latest_per_mutator = {}
                for row in reader:
                    try:
                        mid = int(row["mutator_id"])
                    except (KeyError, ValueError):
                        continue
                    latest_per_mutator[mid] = row
            for mid, row in latest_per_mutator.items():
                acc = by_mutator.setdefault(
                    mid,
                    {
                        "id": mid,
                        "name": row.get("mutator_name", "?"),
                        "attempted": 0,
                        "applied": 0,
                        "parse_fail": 0,
                        "verify_fail": 0,
                        "too_large": 0,
                        "success": 0,
                    },
                )
                # Keep first-seen name; harness should be consistent.
                for k in ("attempted", "applied", "parse_fail", "verify_fail", "too_large", "success"):
                    try:
                        acc[k] += int(row.get(k, 0) or 0)
                    except ValueError:
                        pass
        except OSError:
            continue

    out = list(by_mutator.values())
    out.sort(key=lambda r: r["attempted"], reverse=True)
    return out


def load_fallback_stats(run_dir: Path) -> list:
    """Aggregate fallback-site counters across PIDs.

    Per-PID file is append-only with periodic snapshots of cumulative counts.
    Take the last row per (PID, site_id), sum across PIDs.
    """
    candidates = glob.glob(
        str(run_dir / "workdir" / "regatoni-fallback-stats.*.csv")
    ) + glob.glob(str(run_dir / "stats" / "fallback-stats.*.csv"))
    if not candidates:
        return []

    by_site = {}
    for path in candidates:
        try:
            with open(path) as f:
                reader = csv.DictReader(f)
                latest_per_site = {}
                for row in reader:
                    try:
                        sid = int(row["site_id"])
                    except (KeyError, ValueError):
                        continue
                    latest_per_site[sid] = row
            for sid, row in latest_per_site.items():
                acc = by_site.setdefault(
                    sid,
                    {"id": sid, "name": row.get("site_name", "?"), "count": 0},
                )
                try:
                    acc["count"] += int(row.get("count", 0) or 0)
                except ValueError:
                    pass
        except OSError:
            continue

    out = list(by_site.values())
    out.sort(key=lambda r: r["id"])
    return out


def load_dedup(db_path: Path, run_id: str):
    """Returns (top_buckets, recent_findings). Empty lists if db unavailable
    or run_id is None."""
    if not db_path.exists() or not run_id:
        return [], []
    try:
        conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    except sqlite3.Error:
        return [], []
    try:
        buckets = list(
            conn.execute(
                """
                SELECT b.bucket_id, b.guilty_pass, b.bisect_index,
                       b.finding_count, b.first_seen
                  FROM buckets b
                 WHERE EXISTS (SELECT 1 FROM findings f
                                WHERE f.bucket_id = b.bucket_id
                                  AND f.run_id = ?)
                 ORDER BY b.finding_count DESC
                 LIMIT 10
                """,
                (run_id,),
            )
        )
        findings = list(
            conn.execute(
                """
                SELECT f.finding_id, f.bucket_id, f.oracle, f.found_at,
                       f.original_path, b.guilty_pass
                  FROM findings f
                  LEFT JOIN buckets b ON b.bucket_id = f.bucket_id
                 WHERE f.run_id = ?
                 ORDER BY f.found_at DESC
                 LIMIT 10
                """,
                (run_id,),
            )
        )
    except sqlite3.Error:
        return [], []
    finally:
        conn.close()
    return buckets, findings


# ── Section renderers ───────────────────────────────────────────────────────


def render_header(manifest: dict, now_ts: float) -> str:
    run_id = manifest.get("run_id", "(unknown)")
    start = manifest.get("start_time")
    end = manifest.get("end_time")
    git = manifest.get("git_commit", "")[:12] or "(unknown)"
    if manifest.get("git_dirty"):
        git += "+dirty"
    seed_src = manifest.get("seed_source", "(unknown)")

    if start:
        try:
            start_ts = parse_iso8601(start)
        except ValueError:
            start_ts = now_ts
    else:
        start_ts = now_ts

    if end:
        try:
            cur_ts = parse_iso8601(end)
            cur_label = "ended"
        except ValueError:
            cur_ts = now_ts
            cur_label = "now"
    else:
        cur_ts = now_ts
        cur_label = "now"

    elapsed = max(0.0, cur_ts - start_ts)

    return (
        '<section class="header">'
        f"<div><b>run</b> {html.escape(str(run_id))} &middot; "
        f"<b>commit</b> {html.escape(git)} &middot; "
        f"<b>seeds</b> {html.escape(str(seed_src))}</div>"
        f"<div><b>started</b> {html.escape(fmt_iso(start_ts))} &middot; "
        f"<b>{html.escape(cur_label)}</b> {html.escape(fmt_iso(cur_ts))}</div>"
        f"<div><b>elapsed</b> {html.escape(fmt_elapsed(elapsed))}</div>"
        "</section>"
    )


def _last_int(rows, field, default=0):
    for r in reversed(rows):
        v = r.get(field)
        if v is None or v == "":
            continue
        try:
            return int(v)
        except ValueError:
            try:
                return int(float(v))
            except ValueError:
                continue
    return default


def render_top_metrics(fuzzing_rows, batch_rows, buckets, findings, miscomp_count) -> str:
    coverage = _last_int(fuzzing_rows, "NumCoveredPcs_Max")
    corpus = _last_int(fuzzing_rows, "ActiveCorpusSize_Max")
    exec_s = 0
    for r in reversed(batch_rows):
        v = r.get("exec_per_s")
        if v not in (None, ""):
            try:
                exec_s = int(v)
                break
            except ValueError:
                try:
                    exec_s = int(float(v))
                    break
                except ValueError:
                    continue
    distinct_buckets = len(buckets)

    tiles = [
        ("coverage (PCs)", f"{coverage:,}"),
        ("exec/s", f"{exec_s:,}"),
        ("corpus size", f"{corpus:,}"),
        ("miscomps (this run)", f"{miscomp_count:,}"),
        ("buckets touched", f"{distinct_buckets:,}"),
    ]
    cells = "".join(
        f'<div class="tile"><div class="tile-val">{html.escape(v)}</div>'
        f'<div class="tile-lbl">{html.escape(l)}</div></div>'
        for (l, v) in tiles
    )
    return f'<section class="tiles">{cells}</section>'


def _coverage_traces(rows):
    if not rows:
        return None
    xs = []
    cov = []
    cnt = []
    cmp_ = []
    for r in rows:
        try:
            t_us = int(r.get("UnixMicros_Max", "0") or 0)
        except ValueError:
            continue
        if t_us <= 0:
            continue
        xs.append(t_us / 1e6 * 1000)  # plotly: ms since epoch
        try:
            cov.append(int(r.get("NumCoveredPcs_Max", 0) or 0))
        except ValueError:
            cov.append(0)
        try:
            cnt.append(int(r.get("Num8BitCounterFts_Max", 0) or 0))
        except ValueError:
            cnt.append(0)
        try:
            cmp_.append(int(r.get("NumCmpFts_Max", 0) or 0))
        except ValueError:
            cmp_.append(0)
    if not xs:
        return None
    return {"xs": xs, "cov": cov, "cnt": cnt, "cmp": cmp_}


def render_coverage_plot(rows) -> str:
    data = _coverage_traces(rows)
    if data is None:
        return _empty_section(
            "coverage", "Coverage over time", "no data yet (no fuzzing-stats rows)"
        )

    traces = [
        {
            "x": data["xs"],
            "y": data["cov"],
            "mode": "lines",
            "name": "covered PCs",
            "line": {"width": 2},
        },
        {
            "x": data["xs"],
            "y": data["cnt"],
            "mode": "lines",
            "name": "8-bit counter features",
            "visible": "legendonly",
        },
        {
            "x": data["xs"],
            "y": data["cmp"],
            "mode": "lines",
            "name": "cmp features",
            "visible": "legendonly",
        },
    ]
    layout = {
        "margin": {"l": 60, "r": 20, "t": 30, "b": 40},
        "xaxis": {"type": "date", "title": "time"},
        "yaxis": {"title": "count"},
        "height": 320,
    }
    return _plot_section("coverage", "Coverage over time", traces, layout)


def render_exec_plot(rows) -> str:
    if not rows:
        return _empty_section(
            "exec", "exec/s over time", "no data yet (no batch-stats rows)"
        )
    xs = []
    raw = []
    for r in rows:
        try:
            t_us = int(r.get("unix_micros", "0") or 0)
            v = r.get("exec_per_s", "")
            if v in (None, ""):
                continue
            ev = int(float(v))
        except ValueError:
            continue
        # Skip startup log-types (begin-fuzz, init-done, rerun-old, load-shard)
        # which carry exec_per_s == 0 and would drag the line to zero.
        if ev <= 0:
            continue
        xs.append(t_us / 1e6 * 1000)
        raw.append(ev)
    if not xs:
        return _empty_section(
            "exec", "exec/s over time", "no data yet (batch-stats has no exec_per_s)"
        )
    smoothed = rolling_median(raw, window=20)
    traces = [
        {
            "x": xs,
            "y": raw,
            "mode": "lines",
            "name": "raw",
            "line": {"width": 1, "color": "rgba(120,140,180,0.45)"},
        },
        {
            "x": xs,
            "y": smoothed,
            "mode": "lines",
            "name": "rolling median (w=20)",
            "line": {"width": 2, "color": "rgb(30,80,170)"},
        },
    ]
    layout = {
        "margin": {"l": 60, "r": 20, "t": 30, "b": 40},
        "xaxis": {"type": "date", "title": "time"},
        "yaxis": {"title": "exec/s"},
        "height": 280,
    }
    return _plot_section("exec", "exec/s over time", traces, layout)


def render_mutator_table(rows) -> str:
    if not rows:
        return _empty_section(
            "mutators",
            "Per-mutator activity",
            "no data yet (no regatoni-mutation-stats.*.csv files found)",
        )
    head = (
        "<tr><th>id</th><th>name</th>"
        "<th class='num'>attempted</th><th class='num'>applied</th>"
        "<th class='num'>success</th><th class='num'>success rate</th>"
        "<th class='num'>parse_fail</th><th class='num'>verify_fail</th></tr>"
    )
    body = []
    for r in rows:
        zero = r["attempted"] == 0
        rate = (r["success"] / r["attempted"]) if r["attempted"] else 0.0
        rate_s = f"{rate * 100:.2f}%" if r["attempted"] else "—"
        cls = " class='zero'" if zero else ""
        body.append(
            f"<tr{cls}>"
            f"<td>{r['id']}</td>"
            f"<td>{html.escape(r['name'])}</td>"
            f"<td class='num'>{r['attempted']:,}</td>"
            f"<td class='num'>{r['applied']:,}</td>"
            f"<td class='num'>{r['success']:,}</td>"
            f"<td class='num'>{rate_s}</td>"
            f"<td class='num'>{r['parse_fail']:,}</td>"
            f"<td class='num'>{r['verify_fail']:,}</td>"
            "</tr>"
        )
    return (
        '<section class="block"><h2>Per-mutator activity</h2>'
        f"<table class='data'><thead>{head}</thead>"
        f"<tbody>{''.join(body)}</tbody></table></section>"
    )


def render_fallback_table(rows) -> str:
    if not rows:
        return _empty_section(
            "fallbacks",
            "Fallback sites",
            "no data yet (no regatoni-fallback-stats.*.csv files found)",
        )
    total = sum(r["count"] for r in rows)
    head = (
        "<tr><th>id</th><th>site</th>"
        "<th class='num'>count</th><th class='num'>share</th></tr>"
    )
    body = []
    for r in rows:
        share = (r["count"] / total) if total else 0.0
        share_s = f"{share * 100:.2f}%" if total else "—"
        body.append(
            f"<tr><td>{r['id']}</td>"
            f"<td>{html.escape(r['name'])}</td>"
            f"<td class='num'>{r['count']:,}</td>"
            f"<td class='num'>{share_s}</td></tr>"
        )
    body.append(
        f"<tr><td></td><td><b>total</b></td>"
        f"<td class='num'><b>{total:,}</b></td><td class='num'></td></tr>"
    )
    return (
        '<section class="block"><h2>Fallback sites</h2>'
        "<p style='font-size:12px;color:#666;margin:0 0 6px 0;'>"
        "Number of times LLVMFuzzerCustomMutator fell through to the "
        "byte-level fallback path, by site.</p>"
        f"<table class='data'><thead>{head}</thead>"
        f"<tbody>{''.join(body)}</tbody></table></section>"
    )


def render_buckets_table(buckets) -> str:
    if not buckets:
        return _empty_section(
            "buckets",
            "Top buckets",
            "no data yet (no buckets carry findings tagged with this run_id)",
        )
    head = (
        "<tr><th>bucket_id</th><th>guilty_pass</th>"
        "<th class='num'>bisect_index</th>"
        "<th class='num'>finding_count</th><th>first_seen</th></tr>"
    )
    body = []
    for b in buckets:
        bid, gp, bi, fc, fs = b
        body.append(
            f"<tr><td>{bid}</td>"
            f"<td>{html.escape(gp or '—')}</td>"
            f"<td class='num'>{bi if bi is not None else '—'}</td>"
            f"<td class='num'>{fc:,}</td>"
            f"<td>{html.escape(str(fs))}</td></tr>"
        )
    return (
        '<section class="block"><h2>Top buckets</h2>'
        f"<table class='data'><thead>{head}</thead>"
        f"<tbody>{''.join(body)}</tbody></table></section>"
    )


def render_findings_table(findings) -> str:
    if not findings:
        return _empty_section(
            "findings",
            "Recent miscomps",
            "no data yet (no findings registered with this run_id)",
        )
    head = (
        "<tr><th>finding_id</th><th>bucket</th>"
        "<th>found_at</th><th>oracle</th><th>original</th></tr>"
    )
    body = []
    for f in findings:
        fid, bid, oracle, found_at, opath, gp = f
        bucket_lbl = f"{bid} ({html.escape(gp)})" if gp else f"{bid}"
        base = os.path.basename(opath) if opath else "—"
        body.append(
            f"<tr><td>{fid}</td>"
            f"<td>{bucket_lbl}</td>"
            f"<td>{html.escape(str(found_at))}</td>"
            f"<td>{html.escape(oracle or '—')}</td>"
            f"<td>{html.escape(base)}</td></tr>"
        )
    return (
        '<section class="block"><h2>Recent miscomps</h2>'
        f"<table class='data'><thead>{head}</thead>"
        f"<tbody>{''.join(body)}</tbody></table></section>"
    )


# ── HTML scaffolding ────────────────────────────────────────────────────────


def _plot_section(div_id: str, title: str, traces, layout) -> str:
    payload = json.dumps({"data": traces, "layout": layout})
    return (
        f'<section class="block"><h2>{html.escape(title)}</h2>'
        f'<div id="plot-{div_id}" class="plot"></div>'
        f'<script type="application/json" id="data-{div_id}">{payload}</script>'
        f'<script>(function(){{var p=document.getElementById("data-{div_id}");'
        f'var d=JSON.parse(p.textContent);'
        f'Plotly.newPlot("plot-{div_id}",d.data,d.layout,'
        f'{{displayModeBar:false,responsive:true}});}})();</script>'
        "</section>"
    )


def _empty_section(div_id: str, title: str, msg: str) -> str:
    return (
        f'<section class="block"><h2>{html.escape(title)}</h2>'
        f'<div class="empty">{html.escape(msg)}</div></section>'
    )


CSS = """
:root { color-scheme: light; }
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica,
       Arial, sans-serif; margin: 16px; color: #1d1d1f; background: #fafafa; }
h1 { font-size: 20px; margin: 0 0 8px 0; }
h2 { font-size: 15px; margin: 0 0 8px 0; color: #333; font-weight: 600; }
.header { background: #fff; border: 1px solid #e3e3e8; border-radius: 6px;
          padding: 10px 14px; margin-bottom: 12px; line-height: 1.5;
          font-size: 13px; }
.header b { color: #555; font-weight: 600; }
.tiles { display: grid; grid-template-columns: repeat(5, 1fr); gap: 10px;
         margin-bottom: 14px; }
.tile { background: #fff; border: 1px solid #e3e3e8; border-radius: 6px;
        padding: 12px 14px; }
.tile-val { font-size: 22px; font-weight: 600; color: #1d3557; }
.tile-lbl { font-size: 12px; color: #666; margin-top: 4px; }
.block { background: #fff; border: 1px solid #e3e3e8; border-radius: 6px;
         padding: 12px 14px; margin-bottom: 14px; }
.plot { width: 100%; }
.empty { color: #888; font-style: italic; padding: 8px 0; font-size: 13px; }
table.data { width: 100%; border-collapse: collapse; font-size: 13px; }
table.data th, table.data td { padding: 5px 8px; border-bottom: 1px solid #ececf0;
                                text-align: left; }
table.data th { font-weight: 600; color: #555; background: #f7f7fa; }
table.data td.num, table.data th.num { text-align: right;
                                       font-variant-numeric: tabular-nums; }
table.data tr.zero td { background: #fdecec; color: #a02020; }
.footer { font-size: 11px; color: #888; text-align: right; margin-top: 8px; }
"""


def render_html(run_dir: Path, db_path: Path) -> str:
    now_ts = time.time()
    manifest = load_manifest(run_dir)
    run_id = manifest.get("run_id") or run_dir.name

    fuzzing_rows = load_fuzzing_stats(run_dir)
    batch_rows = load_batch_stats(run_dir)
    mutators = load_mutation_stats(run_dir)
    fallbacks = load_fallback_stats(run_dir)
    buckets, findings = load_dedup(db_path, run_id)

    # Miscomps registered with this run_id: count via sqlite directly.
    miscomp_count = 0
    if db_path.exists() and run_id:
        try:
            conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
            miscomp_count = conn.execute(
                "SELECT COUNT(*) FROM findings WHERE run_id = ?", (run_id,)
            ).fetchone()[0]
            conn.close()
        except sqlite3.Error:
            pass

    parts = [
        "<!doctype html><html><head><meta charset='utf-8'>",
        "<meta http-equiv='refresh' content='60'>",
        f"<title>regatoni dashboard — {html.escape(run_id)}</title>",
        f'<script src="{PLOTLY_CDN}"></script>',
        f"<style>{CSS}</style>",
        "</head><body>",
        f"<h1>regatoni run dashboard</h1>",
        render_header(manifest, now_ts),
        render_top_metrics(fuzzing_rows, batch_rows, buckets, findings, miscomp_count),
        render_coverage_plot(fuzzing_rows),
        render_exec_plot(batch_rows),
        render_mutator_table(mutators),
        render_fallback_table(fallbacks),
        render_buckets_table(buckets),
        render_findings_table(findings),
        f'<div class="footer">rendered {html.escape(fmt_iso(now_ts))} '
        f"&middot; auto-refresh every 60s</div>",
        "</body></html>",
    ]
    return "\n".join(parts)


def _atomic_write(out: Path, content: str) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(out.suffix + ".tmp")
    tmp.write_text(content, encoding="utf-8")
    os.replace(tmp, out)


def _scan_runs(runs_dir: Path, db_path: Path) -> list[dict]:
    """Walk runs/, return one dict per run, newest start_time first."""
    if not runs_dir.is_dir():
        return []
    out = []
    for entry in runs_dir.iterdir():
        if not entry.is_dir() or entry.is_symlink():
            continue
        if entry.name.startswith("."):
            continue
        manifest = load_manifest(entry)
        if not manifest:
            continue
        run_id = manifest.get("run_id") or entry.name
        start = manifest.get("start_time", "")
        end = manifest.get("end_time")
        # peak coverage: max NumCoveredPcs_Max across rows.
        peak_cov = None
        try:
            for r in load_fuzzing_stats(entry):
                v = r.get("NumCoveredPcs_Max")
                if v in (None, ""):
                    continue
                try:
                    iv = int(v)
                except ValueError:
                    continue
                if peak_cov is None or iv > peak_cov:
                    peak_cov = iv
        except Exception:
            pass
        # miscomp count for this run.
        miscomp_count = None
        if db_path.exists():
            try:
                conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
                miscomp_count = conn.execute(
                    "SELECT COUNT(*) FROM findings WHERE run_id = ?", (run_id,)
                ).fetchone()[0]
                conn.close()
            except sqlite3.Error:
                pass
        out.append({
            "run_id": run_id,
            "start_time": start,
            "status": "finished" if end else "running",
            "end_time": end,
            "peak_coverage": peak_cov,
            "miscomp_count": miscomp_count,
        })
    out.sort(key=lambda d: d["start_time"], reverse=True)
    return out


def render_index_html(runs: list[dict]) -> str:
    head = (
        "<tr><th>run_id</th><th>start</th><th>status</th>"
        "<th class='num'>peak coverage</th>"
        "<th class='num'>miscomps</th></tr>"
    )
    rows = []
    for r in runs:
        peak = f"{r['peak_coverage']:,}" if r["peak_coverage"] is not None else "—"
        misc = f"{r['miscomp_count']:,}" if r["miscomp_count"] is not None else "—"
        status = r["status"]
        status_cls = "ok" if status == "finished" else "live"
        link = f"runs/{html.escape(r['run_id'])}.html"
        rows.append(
            f"<tr><td><a href='{link}'>{html.escape(r['run_id'])}</a></td>"
            f"<td>{html.escape(r['start_time'])}</td>"
            f"<td><span class='status {status_cls}'>{status}</span></td>"
            f"<td class='num'>{peak}</td>"
            f"<td class='num'>{misc}</td></tr>"
        )
    extra_css = (
        ".status{padding:1px 6px;border-radius:3px;font-size:11px;}"
        ".status.live{background:#d4f0e0;color:#085e2c;}"
        ".status.ok{background:#e7e9ee;color:#444;}"
    )
    body_rows = "".join(rows) if rows else (
        "<tr><td colspan='5' class='empty'>no runs found</td></tr>"
    )
    body = (
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta http-equiv='refresh' content='120'>"
        "<title>regatoni runs</title>"
        f"<style>{CSS}\n{extra_css}</style>"
        "</head><body><h1>regatoni runs</h1>"
        "<section class='block'>"
        "<p style='font-size:12px;color:#666;margin:0 0 8px 0;'>"
        "<a href='dashboard.html'>→ live dashboard (currently active run)</a></p>"
        f"<table class='data'><thead>{head}</thead>"
        f"<tbody>{body_rows}</tbody>"
        "</table></section>"
        f"<div class='footer'>rendered {html.escape(fmt_iso(time.time()))}</div>"
        "</body></html>"
    )
    return body


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--run-dir", type=Path, default=DEFAULT_RUN_DIR)
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--db", type=Path, default=DEFAULT_DB)
    ap.add_argument("--publish-to", type=Path, default=None,
                    help="If set, also write dashboard.html, runs/<id>.html "
                         "(when run is finished), and an index.html into this "
                         "publish dir.")
    args = ap.parse_args()

    run_dir = args.run_dir.resolve()
    if not run_dir.exists():
        print(f"error: run dir does not exist: {run_dir}", file=sys.stderr)
        return 1

    out = args.out if args.out else run_dir / "dashboard.html"

    html_doc = render_html(run_dir, args.db)
    _atomic_write(out, html_doc)

    if args.publish_to:
        pub = args.publish_to.resolve()
        # Live dashboard mirror.
        _atomic_write(pub / "dashboard.html", html_doc)

        # Runs index.
        runs = _scan_runs(RUNS_DIR, args.db)
        _atomic_write(pub / "index.html", render_index_html(runs))

        # Per-run frozen snapshot for any finished run. We always rewrite the
        # snapshot for the current run dir if it's finished — cheap, and keeps
        # the snapshot fresh if the manifest is updated post-hoc.
        manifest = load_manifest(run_dir)
        run_id = manifest.get("run_id") or run_dir.name
        if manifest.get("end_time"):
            _atomic_write(pub / "runs" / f"{run_id}.html", html_doc)

    return 0


if __name__ == "__main__":
    sys.exit(main())
