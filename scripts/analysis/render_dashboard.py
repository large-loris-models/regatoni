#!/usr/bin/env python3
"""Render a per-run live dashboard for a Regatoni fuzzing run.

Reads from manifest.json, fuzzing-stats CSV, batch-stats TSV, mutation-stats
CSVs, and the project-root dedup.db. Writes a single self-contained HTML file
that auto-refreshes every 60s and renders Plotly plots from CDN with inlined
JSON data.

Findings are split by oracle into two views: miscompilations (alive_tv) and
sanitizer findings (asan_opt).

--include-links makes finding/bucket IDs render as links to per-finding and
per-bucket pages. Only the live published <publish-dir>/dashboard.html is
rendered with links (the start.sh regenerator daemon passes the flag); the
in-run-dir copy, the frozen <publish-dir>/runs/<run_id>.html snapshot, and
dashboard_now.sh output are link-free by design (those page trees may not
exist alongside an archived snapshot).

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


def fmt_age(iso_str, now_ts: float) -> str:
    """Coarse 'how long ago' for a finding timestamp. '' if unparseable."""
    try:
        t = parse_iso8601(str(iso_str))
    except (ValueError, TypeError):
        return ""
    d = max(0.0, now_ts - t)
    if d < 90:
        return "just now"
    if d < 5400:
        return f"{int(d // 60)}m ago"
    if d < 129600:
        return f"{int(d // 3600)}h ago"
    return f"{int(d // 86400)}d ago"


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


def load_dedup(db_path: Path, run_id: str) -> dict:
    """Per-run dedup view, keyed by oracle. Keys:

      alive_count, asan_count  -- findings registered with this run_id, by oracle
      buckets_touched          -- distinct bucket_ids touched by this run
      miscomp_buckets          -- top-10 alive_tv buckets touching this run, as
                                  (bucket_id, guilty_pass, bisect_index,
                                   run_finding_count, sub_cluster_count)
      asan_bucket              -- (bucket_id, first_seen, last_seen,
                                   total_finding_count, sub_cluster_count) for
                                  the __asan__ sentinel bucket, or None if it
                                  doesn't exist
      recent_miscomps          -- last 10 alive_tv findings for this run, as
                                  (finding_id, bucket_id, guilty_pass, found_at,
                                   original_path)
      recent_asan              -- last 10 asan_opt findings for this run, same
                                  shape

    All zeros / empty / None if db unavailable or run_id is falsy.
    """
    empty = {
        "alive_count": 0,
        "asan_count": 0,
        "buckets_touched": 0,
        "miscomp_buckets": [],
        "asan_bucket": None,
        "recent_miscomps": [],
        "recent_asan": [],
    }
    if not db_path.exists() or not run_id:
        return empty
    try:
        conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    except sqlite3.Error:
        return empty
    try:
        d = dict(empty)
        for oracle, n in conn.execute(
            "SELECT oracle, COUNT(*) FROM findings WHERE run_id = ? GROUP BY oracle",
            (run_id,),
        ):
            if oracle == "alive_tv":
                d["alive_count"] = n
            elif oracle == "asan_opt":
                d["asan_count"] = n
        d["buckets_touched"] = conn.execute(
            "SELECT COUNT(DISTINCT bucket_id) FROM findings WHERE run_id = ?",
            (run_id,),
        ).fetchone()[0]
        d["miscomp_buckets"] = list(
            conn.execute(
                """
                SELECT b.bucket_id, b.guilty_pass, b.bisect_index,
                       COUNT(DISTINCT f.finding_id) AS run_fc,
                       (SELECT COUNT(*) FROM sub_clusters sc
                         WHERE sc.bucket_id = b.bucket_id) AS sc_count
                  FROM buckets b
                  JOIN findings f
                    ON f.bucket_id = b.bucket_id AND f.run_id = ?
                  LEFT JOIN findings rep ON rep.finding_id = b.representative_id
                 WHERE rep.oracle = 'alive_tv'
                 GROUP BY b.bucket_id
                 ORDER BY run_fc DESC, b.bucket_id ASC
                 LIMIT 10
                """,
                (run_id,),
            )
        )
        d["asan_bucket"] = conn.execute(
            "SELECT b.bucket_id, b.first_seen, b.last_seen, b.finding_count, "
            "(SELECT COUNT(*) FROM sub_clusters sc WHERE sc.bucket_id = b.bucket_id) "
            "FROM buckets b WHERE b.guilty_pass = '__asan__' AND b.bisect_index = 0"
        ).fetchone()
        for key, oracle in (
            ("recent_miscomps", "alive_tv"),
            ("recent_asan", "asan_opt"),
        ):
            d[key] = list(
                conn.execute(
                    """
                    SELECT f.finding_id, f.bucket_id, b.guilty_pass, f.found_at,
                           f.original_path
                      FROM findings f
                      LEFT JOIN buckets b ON b.bucket_id = f.bucket_id
                     WHERE f.run_id = ? AND f.oracle = ?
                     ORDER BY f.found_at DESC, f.finding_id DESC
                     LIMIT 10
                    """,
                    (run_id, oracle),
                )
            )
    except sqlite3.Error:
        return empty
    finally:
        conn.close()
    return d


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


def render_top_metrics(fuzzing_rows, batch_rows, dedup) -> str:
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

    tiles = [
        ("coverage (PCs)", f"{coverage:,}"),
        ("exec/s", f"{exec_s:,}"),
        ("corpus size", f"{corpus:,}"),
        ("miscompilations", f"{dedup['alive_count']:,}"),
        ("sanitizer findings", f"{dedup['asan_count']:,}"),
        ("buckets touched", f"{dedup['buckets_touched']:,}"),
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


def _maybe_link(text, href: str, include_links: bool) -> str:
    """Escaped `text`, wrapped in <a href=href> when include_links is set."""
    esc = html.escape(str(text))
    if include_links:
        return f"<a href='{html.escape(href)}'>{esc}</a>"
    return esc


def render_miscomp_buckets_table(buckets, include_links: bool) -> str:
    if not buckets:
        return _empty_section(
            "miscomp-buckets",
            "Top miscompilation buckets",
            "no data yet (no alive_tv buckets carry findings tagged with this "
            "run_id)",
            cls="section-miscomp",
        )
    head = (
        "<tr><th>bucket_id</th><th>guilty_pass</th>"
        "<th class='num'>bisect_index</th>"
        "<th class='num'>findings (this run)</th>"
        "<th class='num'>sub-clusters</th></tr>"
    )
    body = []
    for bid, gp, bi, fc, scc in buckets:
        body.append(
            f"<tr><td>{_maybe_link(bid, f'buckets/{bid}.html', include_links)}</td>"
            f"<td>{html.escape(gp or '—')}</td>"
            f"<td class='num'>{bi if bi is not None else '—'}</td>"
            f"<td class='num'>{fc:,}</td>"
            f"<td class='num'>{scc:,}</td></tr>"
        )
    return (
        '<section class="block section-miscomp">'
        "<h2>Top miscompilation buckets</h2>"
        f"<table class='data'><thead>{head}</thead>"
        f"<tbody>{''.join(body)}</tbody></table></section>"
    )


def render_asan_summary(asan_bucket, asan_count: int, include_links: bool) -> str:
    if asan_count == 0 and asan_bucket is None:
        msg = "no sanitizer findings registered with this run_id"
    elif asan_bucket is None:
        msg = f"{asan_count:,} sanitizer finding(s) this run"
    else:
        bid, first_seen, last_seen, total, scc = asan_bucket
        ref = _maybe_link(f"bucket {bid}", f"buckets/{bid}.html", include_links)
        sc_phrase = (
            f"{scc} sub-cluster{'s' if scc != 1 else ''} identified"
            if scc else "not yet triaged"
        )
        msg = (
            f"All sanitizer findings bucket on <code>__asan__</code> ({ref}) — "
            f"{asan_count:,} this run, {total:,} total since "
            f"{html.escape(str(first_seen))}, latest "
            f"{html.escape(str(last_seen))}, {sc_phrase}."
        )
    return (
        '<section class="block section-asan"><h2>Sanitizer findings</h2>'
        f"<p style='font-size:13px;color:#555;margin:0;'>{msg}</p></section>"
    )


def render_recent_table(rows, title: str, div_id: str, include_links: bool,
                        cls: str) -> str:
    if not rows:
        return _empty_section(
            div_id, title,
            "no data yet (no such findings registered with this run_id)",
            cls=cls,
        )
    head = (
        "<tr><th>finding_id</th><th>bucket</th>"
        "<th>found_at</th><th>original</th></tr>"
    )
    body = []
    for fid, bid, gp, found_at, opath in rows:
        fid_cell = _maybe_link(fid, f"findings/{fid}.html", include_links)
        if bid is None:
            bucket_cell = "—"
        else:
            label = f"{bid} ({gp})" if gp else f"{bid}"
            bucket_cell = _maybe_link(label, f"buckets/{bid}.html", include_links)
        base = os.path.basename(opath) if opath else "—"
        body.append(
            f"<tr><td>{fid_cell}</td><td>{bucket_cell}</td>"
            f"<td>{html.escape(str(found_at))}</td>"
            f"<td>{html.escape(base)}</td></tr>"
        )
    return (
        f'<section class="block {cls}"><h2>{html.escape(title)}</h2>'
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


def _empty_section(div_id: str, title: str, msg: str, cls: str = "") -> str:
    klass = f"block {cls}".rstrip()
    return (
        f'<section class="{klass}"><h2>{html.escape(title)}</h2>'
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
.tiles { display: grid; grid-template-columns: repeat(6, 1fr); gap: 10px;
         margin-bottom: 14px; }
.tile { background: #fff; border: 1px solid #e3e3e8; border-radius: 6px;
        padding: 12px 14px; }
.tile-val { font-size: 22px; font-weight: 600; color: #1d3557; }
.tile-lbl { font-size: 12px; color: #666; margin-top: 4px; }
.block { background: #fff; border: 1px solid #e3e3e8; border-radius: 6px;
         padding: 12px 14px; margin-bottom: 14px; }
.block.section-miscomp { border-left: 3px solid #6f9bd1; background: #f8fafd; }
.block.section-asan { border-left: 3px solid #d6a85c; background: #fffdf7; }
code { font-size: 12px; background: #f0f0f3; padding: 1px 4px;
       border-radius: 3px; }
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

# Per-finding detail pages reuse the dashboard stylesheet plus a narrower body
# and a horizontally-scrolling IR/verdict <pre> (long IR lines must not wrap).
FINDING_CSS = CSS + """
body { max-width: 1200px; margin: 16px auto; }
pre.ir { background: #fbfbfd; border: 1px solid #e3e3e8; border-radius: 6px;
         padding: 10px 12px; font-size: 12px; line-height: 1.4;
         overflow-x: auto; white-space: pre; margin: 0; }
details > summary { cursor: pointer; font-size: 13px; color: #1d3557;
                    margin-bottom: 6px; }
"""


def render_html(run_dir: Path, db_path: Path, include_links: bool = False) -> str:
    now_ts = time.time()
    manifest = load_manifest(run_dir)
    run_id = manifest.get("run_id") or run_dir.name

    fuzzing_rows = load_fuzzing_stats(run_dir)
    batch_rows = load_batch_stats(run_dir)
    mutators = load_mutation_stats(run_dir)
    fallbacks = load_fallback_stats(run_dir)
    dedup = load_dedup(db_path, run_id)

    parts = [
        "<!doctype html><html><head><meta charset='utf-8'>",
        "<meta http-equiv='refresh' content='60'>",
        f"<title>regatoni dashboard — {html.escape(run_id)}</title>",
        f'<script src="{PLOTLY_CDN}"></script>',
        f"<style>{CSS}</style>",
        "</head><body>",
        f"<h1>regatoni run dashboard</h1>",
        render_header(manifest, now_ts),
        render_top_metrics(fuzzing_rows, batch_rows, dedup),
        render_coverage_plot(fuzzing_rows),
        render_exec_plot(batch_rows),
        render_mutator_table(mutators),
        render_fallback_table(fallbacks),
        render_miscomp_buckets_table(dedup["miscomp_buckets"], include_links),
        render_recent_table(dedup["recent_miscomps"], "Recent miscompilations",
                            "recent-miscomp", include_links, "section-miscomp"),
        render_asan_summary(dedup["asan_bucket"], dedup["asan_count"],
                            include_links),
        render_recent_table(dedup["recent_asan"], "Recent sanitizer findings",
                            "recent-asan", include_links, "section-asan"),
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
        # Finding counts for this run, split by oracle.
        miscomp_count = None
        asan_count = None
        if db_path.exists():
            try:
                conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
                miscomp_count = conn.execute(
                    "SELECT COUNT(*) FROM findings "
                    "WHERE run_id = ? AND oracle = 'alive_tv'", (run_id,)
                ).fetchone()[0]
                asan_count = conn.execute(
                    "SELECT COUNT(*) FROM findings "
                    "WHERE run_id = ? AND oracle = 'asan_opt'", (run_id,)
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
            "asan_count": asan_count,
        })
    out.sort(key=lambda d: d["start_time"], reverse=True)
    return out


def render_index_html(runs: list[dict]) -> str:
    head = (
        "<tr><th>run_id</th><th>start</th><th>status</th>"
        "<th class='num'>peak coverage</th>"
        "<th class='num'>miscomps</th><th class='num'>asan</th></tr>"
    )
    rows = []
    for r in runs:
        peak = f"{r['peak_coverage']:,}" if r["peak_coverage"] is not None else "—"
        misc = f"{r['miscomp_count']:,}" if r["miscomp_count"] is not None else "—"
        asan = f"{r['asan_count']:,}" if r.get("asan_count") is not None else "—"
        status = r["status"]
        status_cls = "ok" if status == "finished" else "live"
        link = f"runs/{html.escape(r['run_id'])}.html"
        rows.append(
            f"<tr><td><a href='{link}'>{html.escape(r['run_id'])}</a></td>"
            f"<td>{html.escape(r['start_time'])}</td>"
            f"<td><span class='status {status_cls}'>{status}</span></td>"
            f"<td class='num'>{peak}</td>"
            f"<td class='num'>{misc}</td>"
            f"<td class='num'>{asan}</td></tr>"
        )
    extra_css = (
        ".status{padding:1px 6px;border-radius:3px;font-size:11px;}"
        ".status.live{background:#d4f0e0;color:#085e2c;}"
        ".status.ok{background:#e7e9ee;color:#444;}"
    )
    body_rows = "".join(rows) if rows else (
        "<tr><td colspan='6' class='empty'>no runs found</td></tr>"
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


# ── Per-finding detail pages ────────────────────────────────────────────────


def _read_ir(src) -> str | None:
    """Read an IR / corpus file as text, or None if it is missing or unreadable."""
    if not src:
        return None
    p = Path(src)
    if not p.is_file():
        return None
    try:
        return p.read_text(errors="replace")
    except OSError:
        return None


def _ir_snippet_html(src, max_lines: int = 10) -> str:
    """A small <pre> with the first `max_lines` of an IR file, or the
    missing-file placeholder."""
    text = _read_ir(src)
    if text is None:
        return '<div class="empty">(source IR file no longer available)</div>'
    lines = text.splitlines()
    head = "\n".join(lines[:max_lines])
    if len(lines) > max_lines:
        head += f"\n… ({len(lines) - max_lines} more line(s))"
    return f"<pre class='ir'>{html.escape(head)}</pre>"


def _load_finding_pages(db_path: Path, run_id: str) -> list[dict]:
    """One dict per finding tied to run_id, with bucket and sub-cluster context
    joined in. Empty if db unavailable or run_id falsy."""
    if not db_path.exists() or not run_id:
        return []
    try:
        conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    except sqlite3.Error:
        return []
    try:
        rows = conn.execute(
            """
            SELECT f.finding_id, f.oracle, f.found_at, f.bucket_id,
                   b.guilty_pass, b.bisect_index, b.triaged_at,
                   f.normalized_path, f.original_path, f.error_text,
                   fsc.sub_cluster_id, sc.summary
              FROM findings f
              LEFT JOIN buckets b ON b.bucket_id = f.bucket_id
              LEFT JOIN finding_sub_cluster fsc ON fsc.finding_id = f.finding_id
              LEFT JOIN sub_clusters sc ON sc.sub_cluster_id = fsc.sub_cluster_id
             WHERE f.run_id = ?
             ORDER BY f.finding_id
            """,
            (run_id,),
        ).fetchall()
    except sqlite3.Error:
        return []
    finally:
        conn.close()
    keys = (
        "finding_id", "oracle", "found_at", "bucket_id", "guilty_pass",
        "bisect_index", "bucket_triaged_at", "normalized_path", "original_path",
        "error_text", "sub_cluster_id", "sub_cluster_summary",
    )
    return [dict(zip(keys, r)) for r in rows]


def _render_finding_page(r: dict, run_id: str, now_ts: float,
                         ir_text, ir_label: str, ir_note: str) -> str:
    e = html.escape
    fid = r["finding_id"]
    oracle = r["oracle"]
    is_asan = (oracle == "asan_opt")
    kind = "sanitizer finding" if is_asan else "miscompilation"
    section_cls = "section-asan" if is_asan else "section-miscomp"
    bid = r["bucket_id"]
    gp = r["guilty_pass"]
    bi = r["bisect_index"]
    bucket_link = (
        f"<a href='../buckets/{bid}.html'>bucket {bid}</a>"
        if bid is not None else "—"
    )
    age = fmt_age(r["found_at"], now_ts)

    header = (
        f'<section class="block {section_cls}"><h2>finding {e(str(fid))}</h2>'
        "<div style='font-size:13px;line-height:1.7;'>"
        f"<b>run</b> <a href='../runs/{e(run_id)}.html'>{e(run_id)}</a> "
        f"(<a href='../dashboard.html'>&larr; back to dashboard</a>) &middot; "
        f"<b>kind</b> {e(kind)} (oracle <code>{e(oracle)}</code>) &middot; "
        f"<b>found</b> {e(str(r['found_at']))}"
        + (f" ({e(age)})" if age else "")
        + "</div></section>"
    )

    if is_asan:
        bucket_ctx = f"Bucket: <code>{e(gp or '—')}</code>"
    elif gp is not None and bi is not None:
        bucket_ctx = f"Bisected to <code>{e(gp)}</code> at pipeline index {bi}"
    else:
        bucket_ctx = "Not bucketed (bisect produced no guilty pass)"
    bucket_section = (
        '<section class="block"><h2>bucket</h2>'
        f"<p style='font-size:13px;margin:0;'>{bucket_ctx} &middot; "
        f"{bucket_link}</p></section>"
    )

    if r["sub_cluster_id"] is not None:
        summ = r["sub_cluster_summary"] or "(no summary recorded)"
        sub_ctx = f"Sub-cluster #{e(str(r['sub_cluster_id']))}: {e(summ)}"
    elif r["bucket_triaged_at"]:
        sub_ctx = (f"Bucket triaged at {e(str(r['bucket_triaged_at']))}, "
                   "this finding pending re-clustering")
    else:
        sub_ctx = "Bucket not yet triaged"
    sub_section = (
        '<section class="block"><h2>sub-cluster</h2>'
        f"<p style='font-size:13px;margin:0;'>{sub_ctx}</p></section>"
    )

    if ir_text is None:
        ir_body = '<div class="empty">(source IR file no longer available)</div>'
    else:
        ir_body = (
            f"<p style='font-size:12px;margin:0 0 6px 0;'>"
            f"<a href='{e(str(fid))}.ll'>download .ll</a></p>"
            f"<pre class='ir'>{e(ir_text)}</pre>"
        )
    ir_section = (
        f'<section class="block"><h2>{e(ir_label)}{e(ir_note)}</h2>'
        f"{ir_body}</section>"
    )

    err = r["error_text"]
    if is_asan:
        verdict_body = (
            "<details><summary>show sanitizer output</summary>"
            f"<pre class='ir'>{e(err)}</pre></details>"
            if err else '<div class="empty">(no sanitizer output captured)</div>'
        )
        verdict_section = (
            f'<section class="block {section_cls}"><h2>Sanitizer output</h2>'
            f"{verdict_body}</section>"
        )
    else:
        verdict_body = (
            f"<pre class='ir'>{e(err)}</pre>"
            if err else '<div class="empty">(no verdict captured)</div>'
        )
        verdict_section = (
            f'<section class="block {section_cls}"><h2>Alive2 verdict</h2>'
            f"{verdict_body}</section>"
        )

    return "\n".join([
        "<!doctype html><html><head><meta charset='utf-8'>",
        f"<title>finding {e(str(fid))} — {e(run_id)}</title>",
        f"<style>{FINDING_CSS}</style>",
        "</head><body>",
        f"<h1>regatoni — {e(kind)} #{e(str(fid))}</h1>",
        header,
        bucket_section,
        sub_section,
        ir_section,
        verdict_section,
        f'<div class="footer">rendered {e(fmt_iso(now_ts))}</div>',
        "</body></html>",
    ])


def _write_finding_pages(pub: Path, db_path: Path, run_id: str,
                         now_ts: float) -> int:
    """Write <pub>/findings/<id>.html (and a <id>.ll copy of the IR) for every
    finding tied to run_id. Current-run only. Returns the page count.

    Findings are immutable, so a <id>.ll that already exists is left alone; the
    HTML is always rewritten (a finding's bucket can be triaged later, which
    changes the sub-cluster section). A missing source IR file yields a
    placeholder rather than an error.
    """
    rows = _load_finding_pages(db_path, run_id)
    if not rows:
        return 0
    out_dir = pub / "findings"
    for r in rows:
        fid = r["finding_id"]
        if r["oracle"] == "asan_opt":
            src = r["normalized_path"] or r["original_path"]
            ir_label, ir_note = "Input IR (raw corpus entry)", ""
        elif r["normalized_path"]:
            src = r["normalized_path"]
            ir_label, ir_note = "Normalized IR", ""
        else:
            src = r["original_path"]
            ir_label, ir_note = "Input IR", " (raw finding, not yet reduced)"

        ir_text = _read_ir(src)
        if ir_text is not None:
            dst_ll = out_dir / f"{fid}.ll"
            if not dst_ll.exists():
                _atomic_write(dst_ll, ir_text)

        _atomic_write(
            out_dir / f"{fid}.html",
            _render_finding_page(r, run_id, now_ts, ir_text, ir_label, ir_note),
        )
    return len(rows)


# ── Per-bucket pages ────────────────────────────────────────────────────────

BUCKET_FINDINGS_CAP = 50


def _load_bucket_pages(db_path: Path, run_id: str) -> list[dict]:
    """One dict per bucket touched by run_id. A bucket is a cross-run identity:
    the findings list spans all runs (most-recent-first, capped at
    BUCKET_FINDINGS_CAP), with run_id kept as a column. Empty if db unavailable
    or run_id falsy."""
    if not db_path.exists() or not run_id:
        return []
    try:
        conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    except sqlite3.Error:
        return []
    try:
        bucket_rows = conn.execute(
            """
            SELECT b.bucket_id, b.guilty_pass, b.bisect_index, b.first_seen,
                   b.last_seen, b.triaged_at, b.representative_id, rep.oracle
              FROM buckets b
              LEFT JOIN findings rep ON rep.finding_id = b.representative_id
             WHERE b.bucket_id IN
                   (SELECT DISTINCT bucket_id FROM findings WHERE run_id = ?)
             ORDER BY b.bucket_id
            """,
            (run_id,),
        ).fetchall()
        out = []
        for (bid, gp, bi, first_seen, last_seen, triaged_at, rep_id,
             rep_oracle) in bucket_rows:
            total = conn.execute(
                "SELECT COUNT(*) FROM findings WHERE bucket_id = ?", (bid,)
            ).fetchone()[0]
            run_n = conn.execute(
                "SELECT COUNT(*) FROM findings WHERE bucket_id = ? AND run_id = ?",
                (bid, run_id),
            ).fetchone()[0]
            sub_clusters = [
                {
                    "sub_cluster_id": sid,
                    "representative_id": srep,
                    "summary": summary,
                    "triaged_at": sc_ts,
                    "member_count": members,
                    "rep_normalized_path": rep_np,
                }
                for (sid, srep, summary, sc_ts, members, rep_np) in conn.execute(
                    """
                    SELECT sc.sub_cluster_id, sc.representative_id, sc.summary,
                           sc.triaged_at,
                           (SELECT COUNT(*) FROM finding_sub_cluster fsc
                             WHERE fsc.sub_cluster_id = sc.sub_cluster_id),
                           f.normalized_path
                      FROM sub_clusters sc
                      LEFT JOIN findings f ON f.finding_id = sc.representative_id
                     WHERE sc.bucket_id = ?
                     ORDER BY 5 DESC, sc.sub_cluster_id ASC
                    """,
                    (bid,),
                ).fetchall()
            ]
            findings = conn.execute(
                """
                SELECT f.finding_id, f.run_id, f.oracle, f.found_at,
                       fsc.sub_cluster_id
                  FROM findings f
                  LEFT JOIN finding_sub_cluster fsc
                    ON fsc.finding_id = f.finding_id
                 WHERE f.bucket_id = ?
                 ORDER BY f.found_at DESC, f.finding_id DESC
                 LIMIT ?
                """,
                (bid, BUCKET_FINDINGS_CAP),
            ).fetchall()
            out.append({
                "bucket_id": bid,
                "guilty_pass": gp,
                "bisect_index": bi,
                "first_seen": first_seen,
                "last_seen": last_seen,
                "triaged_at": triaged_at,
                "representative_id": rep_id,
                "oracle": rep_oracle,
                "total_findings": total,
                "run_findings": run_n,
                "sub_clusters": sub_clusters,
                "findings": findings,
            })
    except sqlite3.Error:
        return []
    finally:
        conn.close()
    return out


def _render_bucket_page(b: dict, run_id: str, now_ts: float,
                        snapshot_run_ids: set) -> str:
    e = html.escape
    bid = b["bucket_id"]
    is_asan = (b["oracle"] == "asan_opt")
    section_cls = "section-asan" if is_asan else "section-miscomp"

    triage_bit = (
        f" &middot; <b>triaged</b> {e(str(b['triaged_at']))}"
        if b["triaged_at"] else " &middot; not yet triaged"
    )
    rep_link = (
        f" &middot; <b>representative</b> "
        f"<a href='../findings/{b['representative_id']}.html'>"
        f"#{b['representative_id']}</a>"
        if b["representative_id"] is not None else ""
    )
    header = (
        f'<section class="block {section_cls}"><h2>bucket {e(str(bid))}</h2>'
        "<div style='font-size:13px;line-height:1.7;'>"
        f"<b>guilty_pass</b> <code>{e(b['guilty_pass'] or '—')}</code> &middot; "
        f"<b>bisect_index</b> {b['bisect_index'] if b['bisect_index'] is not None else '—'} &middot; "
        f"<b>first seen</b> {e(str(b['first_seen']))} &middot; "
        f"<b>last seen</b> {e(str(b['last_seen']))} &middot; "
        f"<b>findings</b> {b['total_findings']:,} total, "
        f"{b['run_findings']:,} in run "
        f"<a href='../runs/{e(run_id)}.html'>{e(run_id)}</a>"
        + rep_link + triage_bit
        + f" &middot; <a href='../dashboard.html'>&larr; dashboard</a>"
        + "</div></section>"
    )

    # ── sub-clusters ──
    if b["sub_clusters"]:
        blocks = []
        for sc in b["sub_clusters"]:
            sid = sc["sub_cluster_id"]
            srep = sc["representative_id"]
            summ = e(sc["summary"] or "(no summary recorded)")
            snippet = _ir_snippet_html(sc["rep_normalized_path"])
            blocks.append(
                f"<div id='sc-{e(str(sid))}' style='margin:0 0 14px 0;'>"
                f"<div style='font-size:13px;'><b>#{e(str(sid))}</b> &middot; "
                f"{sc['member_count']:,} finding(s) &middot; representative "
                f"<a href='../findings/{srep}.html'>#{e(str(srep))}</a></div>"
                f"<p style='font-size:13px;margin:4px 0 6px 0;'>{summ}</p>"
                f"{snippet}</div>"
            )
        sub_section = (
            '<section class="block"><h2>sub-clusters</h2>'
            + "".join(blocks) + "</section>"
        )
    else:
        sub_section = (
            '<section class="block"><h2>sub-clusters</h2>'
            '<div class="empty">bucket not yet triaged</div></section>'
        )

    # ── findings table (cross-run, capped) ──
    n_shown = len(b["findings"])
    total = b["total_findings"]
    if total > n_shown:
        cap_line = (f"Showing {n_shown:,} of {total:,} findings "
                    "(most recent first)")
    else:
        cap_line = f"{total:,} finding(s) (most recent first)"
    head = (
        "<tr><th>finding_id</th><th>run</th><th>oracle</th>"
        "<th>found</th><th>sub-cluster</th></tr>"
    )
    body = []
    for fid, f_run, oracle, found_at, sc_id in b["findings"]:
        fid_cell = (
            f"<a href='../findings/{fid}.html'>{fid}</a>"
            if f_run == run_id else str(fid)
        )
        if not f_run:
            run_cell = "—"
        elif f_run in snapshot_run_ids:
            run_cell = f"<a href='../runs/{e(f_run)}.html'>{e(f_run)}</a>"
        else:
            run_cell = e(f_run)
        age = fmt_age(found_at, now_ts) or "—"
        sc_cell = (
            f"<a href='#sc-{e(str(sc_id))}'>#{e(str(sc_id))}</a>"
            if sc_id is not None else "—"
        )
        body.append(
            f"<tr><td>{fid_cell}</td><td>{run_cell}</td>"
            f"<td>{e(oracle or '—')}</td>"
            f"<td title='{e(str(found_at))}'>{e(age)}</td>"
            f"<td>{sc_cell}</td></tr>"
        )
    findings_section = (
        '<section class="block"><h2>findings</h2>'
        f"<p style='font-size:12px;color:#666;margin:0 0 6px 0;'>{e(cap_line)}</p>"
        f"<table class='data'><thead>{head}</thead>"
        f"<tbody>{''.join(body)}</tbody></table></section>"
    )

    return "\n".join([
        "<!doctype html><html><head><meta charset='utf-8'>",
        f"<title>bucket {e(str(bid))} — regatoni</title>",
        f"<style>{FINDING_CSS}</style>",
        "</head><body>",
        f"<h1>regatoni — bucket {e(str(bid))}</h1>",
        header,
        sub_section,
        findings_section,
        f'<div class="footer">rendered {e(fmt_iso(now_ts))}</div>',
        "</body></html>",
    ])


def _write_bucket_pages(pub: Path, db_path: Path, run_id: str,
                        now_ts: float) -> int:
    """Write <pub>/buckets/<id>.html for every bucket touched by run_id.
    Returns the page count."""
    buckets = _load_bucket_pages(db_path, run_id)
    if not buckets:
        return 0
    runs_dir = pub / "runs"
    snapshot_run_ids = (
        {p.stem for p in runs_dir.glob("*.html")} if runs_dir.is_dir() else set()
    )
    out_dir = pub / "buckets"
    for b in buckets:
        _atomic_write(
            out_dir / f"{b['bucket_id']}.html",
            _render_bucket_page(b, run_id, now_ts, snapshot_run_ids),
        )
    return len(buckets)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--run-dir", type=Path, default=DEFAULT_RUN_DIR)
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--db", type=Path, default=DEFAULT_DB)
    ap.add_argument("--publish-to", type=Path, default=None,
                    help="If set, also write dashboard.html, runs/<id>.html "
                         "(when run is finished), and an index.html into this "
                         "publish dir.")
    ap.add_argument("--include-links", action="store_true",
                    help="Render finding/bucket IDs as links to per-finding and "
                         "per-bucket pages. Only the published "
                         "<publish-dir>/dashboard.html uses links; the in-run-dir "
                         "copy and the frozen runs/<id>.html snapshot stay "
                         "link-free.")
    args = ap.parse_args()

    run_dir = args.run_dir.resolve()
    if not run_dir.exists():
        print(f"error: run dir does not exist: {run_dir}", file=sys.stderr)
        return 1

    out = args.out if args.out else run_dir / "dashboard.html"

    # The in-run-dir copy is always link-free: the findings/ and buckets/ page
    # trees live under the publish dir, not the run dir.
    html_plain = render_html(run_dir, args.db, include_links=False)
    _atomic_write(out, html_plain)

    if args.publish_to:
        pub = args.publish_to.resolve()
        manifest = load_manifest(run_dir)
        run_id = manifest.get("run_id") or run_dir.name

        # Live dashboard mirror — linked iff --include-links was passed.
        html_pub = (
            render_html(run_dir, args.db, include_links=True)
            if args.include_links else html_plain
        )
        _atomic_write(pub / "dashboard.html", html_pub)

        # Runs index.
        runs = _scan_runs(RUNS_DIR, args.db)
        _atomic_write(pub / "index.html", render_index_html(runs))

        # Per-run frozen snapshot for any finished run — always link-free, since
        # an archived snapshot may outlive (or never have) its findings/ pages.
        # We always rewrite the snapshot for the current run dir if it's
        # finished — cheap, and keeps the snapshot fresh if the manifest is
        # updated post-hoc. Written before the bucket pages so they can link
        # this run's snapshot.
        if manifest.get("end_time"):
            _atomic_write(pub / "runs" / f"{run_id}.html", html_plain)

        # Per-finding detail pages for the current run (current run only — v1).
        _write_finding_pages(pub, args.db, run_id, time.time())
        # Per-bucket pages for every bucket the current run touched (cross-run
        # findings, so a bucket's history is visible regardless of which run
        # owns its detail pages).
        _write_bucket_pages(pub, args.db, run_id, time.time())

    return 0


if __name__ == "__main__":
    sys.exit(main())
