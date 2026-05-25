#!/usr/bin/env python3
"""
dedup.py — sqlite owner for two-tier dedup of miscompilation findings.

Tier 1 (causal partition): (guilty_pass, bisect_index) from bisect_blame.sh.
Tier 2 (instance key):     sha256 of normalized IR bytes.

Schema and rationale: docs/decisions/2026-05-01_dedup_redesign.md.

Subcommands:
  register    --reduced PATH --oracle NAME [--error-text-file PATH]
              [--original-path PATH]
  new-buckets-since  TIMESTAMP_ISO
  bucket-info BUCKET_ID
  migrate
  findings-in-bucket   BUCKET_ID
  untriaged-in-bucket  BUCKET_ID
  record-sub-clusters  BUCKET_ID --json PATH [--replace]
  assign-existing      --json PATH
  sub-clusters-since   TIMESTAMP_ISO
"""

import argparse
import hashlib
import json
import os
import sqlite3
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

# Repo root: scripts/analysis/dedup.py -> parents[2].
REPO_ROOT = Path(__file__).resolve().parents[2]
DB_PATH = Path(os.environ.get("REGATONI_DEDUP_DB", str(REPO_ROOT / "dedup.db")))
BISECT_SCRIPT = REPO_ROOT / "scripts" / "analysis" / "bisect_blame.sh"
# Default scan dir for cmd_migrate; per-run miscompilations live under
# runs/<RUN_ID>/miscompilations and the triage flow passes --misc-dir.
MISC_DIR = REPO_ROOT / "miscompilations"

SCHEMA = """
CREATE TABLE IF NOT EXISTS findings (
    finding_id        INTEGER PRIMARY KEY,
    normalized_hash   TEXT NOT NULL,
    guilty_pass       TEXT,
    bisect_index      INTEGER,
    oracle            TEXT NOT NULL,
    error_text        TEXT,
    original_path     TEXT NOT NULL,
    reduced_path      TEXT,
    normalized_path   TEXT,
    found_at          TIMESTAMP NOT NULL,
    bucket_id         INTEGER REFERENCES buckets(bucket_id),
    run_id            TEXT
);
CREATE TABLE IF NOT EXISTS buckets (
    bucket_id         INTEGER PRIMARY KEY,
    guilty_pass       TEXT,
    bisect_index      INTEGER,
    first_seen        TIMESTAMP NOT NULL,
    last_seen         TIMESTAMP NOT NULL,
    -- count of register() calls hitting this bucket; for distinct instances,
    -- use SELECT COUNT(DISTINCT normalized_hash).
    finding_count     INTEGER NOT NULL DEFAULT 0,
    representative_id INTEGER REFERENCES findings(finding_id),
    triaged_at        TIMESTAMP,
    triage_summary    TEXT,
    UNIQUE(guilty_pass, bisect_index)
);
CREATE TABLE IF NOT EXISTS sub_clusters (
    sub_cluster_id    INTEGER PRIMARY KEY,
    bucket_id         INTEGER NOT NULL REFERENCES buckets(bucket_id),
    representative_id INTEGER NOT NULL REFERENCES findings(finding_id),
    summary           TEXT,
    triaged_at        TIMESTAMP NOT NULL
);
CREATE TABLE IF NOT EXISTS finding_sub_cluster (
    finding_id        INTEGER PRIMARY KEY REFERENCES findings(finding_id),
    sub_cluster_id    INTEGER NOT NULL REFERENCES sub_clusters(sub_cluster_id)
);
"""


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def connect() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(DB_PATH), timeout=30.0, isolation_level=None)
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("PRAGMA foreign_keys=ON;")
    # Lazy schema init. CREATE TABLE IF NOT EXISTS is idempotent and sqlite
    # serializes DDL, so concurrent first-run callers cannot corrupt the
    # schema. (Cannot wrap in an explicit transaction here because
    # executescript() implicitly COMMITs before running its body.)
    conn.executescript(SCHEMA)
    # Pre-restructure databases predate the run_id column.
    cols = {r[1] for r in conn.execute("PRAGMA table_info(findings)").fetchall()}
    if "run_id" not in cols:
        conn.execute("ALTER TABLE findings ADD COLUMN run_id TEXT")
    _migrate_buckets_drop_notnull(conn)
    _migrate_null_buckets_to_sentinels(conn)
    return conn


def _migrate_buckets_drop_notnull(conn: sqlite3.Connection) -> bool:
    """
    Older schemas declared buckets.guilty_pass / bisect_index NOT NULL, which
    collided with the (NULL, NULL) bucket the previous register code tried
    to insert for ASAN / bisect-failed findings. SQLite has no
    ALTER TABLE DROP CONSTRAINT, so rebuild the table. Idempotent.
    """
    info = conn.execute("PRAGMA table_info(buckets)").fetchall()
    # PRAGMA row: (cid, name, type, notnull, dflt_value, pk)
    notnull = {r[1]: r[3] for r in info}
    if notnull.get("guilty_pass", 0) == 0 and notnull.get("bisect_index", 0) == 0:
        return False

    # PRAGMA foreign_keys cannot toggle inside a transaction. Disable, rebuild,
    # re-enable. findings.bucket_id values are preserved 1:1.
    conn.execute("PRAGMA foreign_keys=OFF;")
    conn.execute("BEGIN IMMEDIATE;")
    try:
        conn.execute("""
            CREATE TABLE buckets_new (
                bucket_id         INTEGER PRIMARY KEY,
                guilty_pass       TEXT,
                bisect_index      INTEGER,
                first_seen        TIMESTAMP NOT NULL,
                last_seen         TIMESTAMP NOT NULL,
                finding_count     INTEGER NOT NULL DEFAULT 0,
                representative_id INTEGER REFERENCES findings(finding_id),
                triaged_at        TIMESTAMP,
                triage_summary    TEXT,
                UNIQUE(guilty_pass, bisect_index)
            )
        """)
        conn.execute("""
            INSERT INTO buckets_new(bucket_id, guilty_pass, bisect_index,
                first_seen, last_seen, finding_count, representative_id,
                triaged_at, triage_summary)
            SELECT bucket_id, guilty_pass, bisect_index, first_seen,
                   last_seen, finding_count, representative_id, triaged_at,
                   triage_summary
            FROM buckets
        """)
        conn.execute("DROP TABLE buckets;")
        conn.execute("ALTER TABLE buckets_new RENAME TO buckets;")
        conn.execute("COMMIT;")
    except Exception:
        conn.execute("ROLLBACK;")
        conn.execute("PRAGMA foreign_keys=ON;")
        raise
    conn.execute("PRAGMA foreign_keys=ON;")
    print("[dedup] migrated buckets schema (dropped NOT NULL on "
          "guilty_pass / bisect_index)", file=sys.stderr)
    return True


def _migrate_null_buckets_to_sentinels(conn: sqlite3.Connection) -> bool:
    """
    Fold any pre-existing (NULL, NULL) buckets into sentinel buckets:
    findings whose oracle is 'asan_opt' move to ('__asan__', 0); the rest
    move to ('__bisect_failed__', 0). The empty NULL bucket is then deleted.
    Idempotent — no-op if no NULL buckets exist.
    """
    null_buckets = [
        r[0] for r in conn.execute(
            "SELECT bucket_id FROM buckets "
            "WHERE guilty_pass IS NULL AND bisect_index IS NULL"
        ).fetchall()
    ]
    if not null_buckets:
        return False

    ts = now_iso()
    conn.execute("BEGIN IMMEDIATE;")
    try:
        for old_bid in null_buckets:
            findings = conn.execute(
                "SELECT finding_id, oracle FROM findings WHERE bucket_id = ?",
                (old_bid,),
            ).fetchall()
            for finding_id, oracle in findings:
                if oracle == "asan_opt":
                    gp, bi = "__asan__", 0
                else:
                    gp, bi = "__bisect_failed__", 0
                conn.execute(
                    "INSERT OR IGNORE INTO buckets(guilty_pass, bisect_index, "
                    "first_seen, last_seen, finding_count) "
                    "VALUES (?, ?, ?, ?, 0)",
                    (gp, bi, ts, ts),
                )
                new_bid = conn.execute(
                    "SELECT bucket_id FROM buckets "
                    "WHERE guilty_pass = ? AND bisect_index = ?",
                    (gp, bi),
                ).fetchone()[0]
                conn.execute(
                    "UPDATE findings SET bucket_id = ?, guilty_pass = ?, "
                    "bisect_index = ? WHERE finding_id = ?",
                    (new_bid, gp, bi, finding_id),
                )
                conn.execute(
                    "UPDATE buckets SET finding_count = finding_count + 1, "
                    "last_seen = ? WHERE bucket_id = ?",
                    (ts, new_bid),
                )
                conn.execute(
                    "UPDATE buckets SET representative_id = ? "
                    "WHERE bucket_id = ? AND representative_id IS NULL",
                    (finding_id, new_bid),
                )
            # Old NULL bucket should now be empty.
            conn.execute("DELETE FROM buckets WHERE bucket_id = ?", (old_bid,))
        conn.execute("COMMIT;")
    except Exception:
        conn.execute("ROLLBACK;")
        raise
    print(f"[dedup] migrated {len(null_buckets)} (NULL, NULL) bucket(s) "
          f"into sentinel buckets", file=sys.stderr)
    return True


def with_retry(fn, *args, **kwargs):
    """Retry on `database is locked` up to 5 times with exponential backoff."""
    delay = 0.1
    for attempt in range(5):
        try:
            return fn(*args, **kwargs)
        except sqlite3.OperationalError as e:
            if "locked" not in str(e).lower() or attempt == 4:
                raise
            time.sleep(delay)
            delay *= 2


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def run_bisect(reduced: Path) -> tuple[str | None, int | None, str | None]:
    """
    Returns (guilty_pass, bisect_index, fail_reason).
    On success: (pass, idx, None). On failure: (None, None, reason).
    """
    try:
        proc = subprocess.run(
            [str(BISECT_SCRIPT), "--quiet", str(reduced)],
            capture_output=True, text=True, check=False,
        )
    except FileNotFoundError as e:
        return None, None, f"bisect_script_missing: {e}"
    line = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else ""
    parts = line.split("\t")
    if len(parts) != 4:
        return None, None, f"bisect_unparseable_output: {line!r}"
    _, pass_or_fail, idx_or_reason, _ = parts
    if pass_or_fail == "FAIL":
        return None, None, f"bisect_fail: {idx_or_reason}"
    try:
        return pass_or_fail, int(idx_or_reason), None
    except ValueError:
        return None, None, f"bisect_bad_index: {idx_or_reason!r}"


def _do_register(conn: sqlite3.Connection, *, reduced: Path, oracle: str,
                 error_text: str | None, original_path: str | None,
                 run_id: str | None = None) -> dict:
    """
    Single-transaction register. Returns dict with bucket_id, new_bucket,
    new_instance, finding_id.

    ASAN findings (oracle == 'asan_opt') skip bisect and bucket on the
    sentinel ('__asan__', 0). alive_tv findings whose bisect returns FAIL
    bucket on ('__bisect_failed__', 0). Sentinels keep the (guilty_pass,
    bisect_index) UNIQUE index well-defined and queryable; see the
    discussion in docs/decisions/2026-05-01_dedup_redesign.md.
    """
    normalized_hash = sha256_file(reduced)

    if oracle == "asan_opt":
        guilty_pass, bisect_index = "__asan__", 0
    else:
        guilty_pass, bisect_index, fail_reason = run_bisect(reduced)
        if fail_reason is not None:
            # Bisect failed: park the finding in the bisect-failed sentinel
            # bucket so it isn't lost. The reason surfaces on stderr.
            print(f"[dedup] bisect failed for {reduced}: {fail_reason}",
                  file=sys.stderr)
            guilty_pass, bisect_index = "__bisect_failed__", 0

    conn.execute("BEGIN IMMEDIATE;")
    try:
        cur = conn.execute(
            "INSERT OR IGNORE INTO buckets(guilty_pass, bisect_index, "
            "first_seen, last_seen, finding_count) VALUES (?, ?, ?, ?, 0)",
            (guilty_pass, bisect_index, now_iso(), now_iso()),
        )
        new_bucket = (cur.rowcount == 1)
        row = conn.execute(
            "SELECT bucket_id FROM buckets "
            "WHERE guilty_pass = ? AND bisect_index = ?",
            (guilty_pass, bisect_index),
        ).fetchone()
        bucket_id = row[0]

        # Tier 2: same hash already in this bucket?
        prior = conn.execute(
            "SELECT 1 FROM findings WHERE bucket_id = ? AND normalized_hash = ? "
            "LIMIT 1",
            (bucket_id, normalized_hash),
        ).fetchone()
        new_instance = (prior is None)

        # Insert finding.
        cur = conn.execute(
            "INSERT INTO findings(normalized_hash, guilty_pass, bisect_index, "
            "oracle, error_text, original_path, reduced_path, normalized_path, "
            "found_at, bucket_id, run_id) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (
                normalized_hash, guilty_pass, bisect_index, oracle, error_text,
                original_path or str(reduced),
                str(reduced),
                str(reduced),
                now_iso(),
                bucket_id,
                run_id,
            ),
        )
        finding_id = cur.lastrowid

        # Update bucket counters; set representative on first insert.
        if new_bucket:
            conn.execute(
                "UPDATE buckets SET representative_id = ?, finding_count = 1, "
                "last_seen = ? WHERE bucket_id = ?",
                (finding_id, now_iso(), bucket_id),
            )
        else:
            conn.execute(
                "UPDATE buckets SET finding_count = finding_count + 1, "
                "last_seen = ? WHERE bucket_id = ?",
                (now_iso(), bucket_id),
            )

        conn.execute("COMMIT;")
    except Exception:
        conn.execute("ROLLBACK;")
        raise

    return {
        "bucket_id": bucket_id,
        "new_bucket": new_bucket,
        "new_instance": new_instance,
        "finding_id": finding_id,
    }


def cmd_register(args) -> int:
    reduced = Path(args.reduced)
    if not reduced.is_file():
        print(f"error: --reduced not a file: {reduced}", file=sys.stderr)
        return 1
    error_text = None
    if args.error_text_file:
        try:
            error_text = Path(args.error_text_file).read_text(errors="replace")
        except OSError as e:
            print(f"warning: could not read error-text-file: {e}", file=sys.stderr)

    conn = connect()
    try:
        result = with_retry(
            _do_register, conn,
            reduced=reduced, oracle=args.oracle,
            error_text=error_text, original_path=args.original_path,
            run_id=args.run_id,
        )
    finally:
        conn.close()

    print(
        f"bucket={result['bucket_id']} "
        f"new_bucket={int(result['new_bucket'])} "
        f"new_instance={int(result['new_instance'])} "
        f"finding={result['finding_id']}"
    )
    return 0


def cmd_new_buckets_since(args) -> int:
    conn = connect()
    try:
        rows = conn.execute(
            "SELECT bucket_id FROM buckets WHERE first_seen >= ? "
            "ORDER BY bucket_id",
            (args.timestamp,),
        ).fetchall()
    finally:
        conn.close()
    for r in rows:
        print(r[0])
    return 0


def cmd_bucket_info(args) -> int:
    conn = connect()
    try:
        b = conn.execute(
            "SELECT bucket_id, guilty_pass, bisect_index, first_seen, "
            "last_seen, finding_count, representative_id "
            "FROM buckets WHERE bucket_id = ?",
            (args.bucket_id,),
        ).fetchone()
        if b is None:
            print(f"error: no such bucket: {args.bucket_id}", file=sys.stderr)
            return 1
        bucket_id, guilty_pass, bisect_index, first_seen, last_seen, count, rep_id = b
        rep_path, rep_err = None, None
        if rep_id is not None:
            r = conn.execute(
                "SELECT normalized_path, error_text FROM findings "
                "WHERE finding_id = ?", (rep_id,),
            ).fetchone()
            if r is not None:
                rep_path, rep_err = r
    finally:
        conn.close()

    print(f"bucket_id: {bucket_id}")
    print(f"guilty_pass: {guilty_pass}")
    print(f"bisect_index: {bisect_index}")
    print(f"finding_count: {count}")
    print(f"first_seen: {first_seen}")
    print(f"last_seen: {last_seen}")
    print(f"representative_finding: {rep_id}")
    print(f"representative_normalized_path: {rep_path}")
    print("representative_error_text:")
    if rep_err:
        print(rep_err)
    return 0


def cmd_migrate(args) -> int:
    """
    Bulk-register every *.reduced.normalized.ll under --misc-dir (default
    miscompilations/) that isn't already in findings.normalized_path. Pairs
    each with its alive_tv fail log if one is on disk under --oracle-results.
    """
    misc_dir = Path(args.misc_dir) if args.misc_dir else MISC_DIR
    oracle_results_root = (
        Path(args.oracle_results) if args.oracle_results
        else REPO_ROOT / "build" / "oracle_results"
    )

    conn = connect()
    try:
        already = set(
            r[0] for r in conn.execute(
                "SELECT normalized_path FROM findings"
            ).fetchall()
        )
    finally:
        conn.close()

    log_index: dict[str, Path] = {}
    if oracle_results_root.is_dir():
        for shard in oracle_results_root.iterdir():
            if not shard.name.startswith("alive_tv"):
                continue
            fail_dir = shard / "fail"
            if not fail_dir.is_dir():
                continue
            for log in fail_dir.glob("*.log"):
                log_index[log.name] = log

    norm_files = sorted(misc_dir.glob("*.reduced.normalized.ll"))
    n_total = len(norm_files)
    n_skipped = 0
    n_registered = 0

    for norm in norm_files:
        if str(norm) in already:
            n_skipped += 1
            continue
        # base name before .reduced.normalized.ll
        base = norm.name[: -len(".reduced.normalized.ll")]
        log_path = log_index.get(f"{base}.log")
        # original = the unreduced corpus entry, named like the hash
        original = misc_dir / base
        original_str = str(original) if original.exists() else str(norm)

        error_text = None
        if log_path is not None:
            try:
                error_text = log_path.read_text(errors="replace")
            except OSError:
                pass

        conn = connect()
        try:
            result = with_retry(
                _do_register, conn,
                reduced=norm, oracle="alive_tv",
                error_text=error_text, original_path=original_str,
                run_id=args.run_id,
            )
        finally:
            conn.close()
        n_registered += 1
        print(
            f"[migrate] {norm.name} -> bucket={result['bucket_id']} "
            f"new_bucket={int(result['new_bucket'])} "
            f"new_instance={int(result['new_instance'])}"
        )

    print(
        f"[migrate] total={n_total} registered={n_registered} "
        f"already_present={n_skipped}"
    )
    return 0


def cmd_findings_in_bucket(args) -> int:
    conn = connect()
    try:
        rows = conn.execute(
            "SELECT finding_id, normalized_path, error_text, original_path, "
            "found_at FROM findings WHERE bucket_id = ? ORDER BY found_at ASC, "
            "finding_id ASC",
            (args.bucket_id,),
        ).fetchall()
    finally:
        conn.close()
    out = [
        {
            "finding_id": r[0],
            "normalized_path": r[1],
            "error_text": r[2],
            "original_path": r[3],
            "found_at": r[4],
        }
        for r in rows
    ]
    json.dump(out, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


def cmd_untriaged_in_bucket(args) -> int:
    conn = connect()
    try:
        rows = conn.execute(
            "SELECT f.finding_id FROM findings f "
            "LEFT JOIN finding_sub_cluster fsc ON fsc.finding_id = f.finding_id "
            "WHERE f.bucket_id = ? AND fsc.sub_cluster_id IS NULL "
            "ORDER BY f.finding_id ASC",
            (args.bucket_id,),
        ).fetchall()
    finally:
        conn.close()
    for r in rows:
        print(r[0])
    return 0


def cmd_record_sub_clusters(args) -> int:
    """
    Persist a JSON description of sub-clusters for one bucket. The JSON is a
    list of {representative_finding_id, summary, member_finding_ids}. All
    members must belong to BUCKET_ID. Atomic: either every sub-cluster is
    inserted or none is.

    --replace deletes any pre-existing sub-clusters for this bucket first.
    Without it, --json members must be currently-untriaged findings of the
    bucket (the assign-mode path).
    """
    bucket_id = args.bucket_id
    payload = json.loads(Path(args.json).read_text())
    if not isinstance(payload, list):
        print("error: top-level JSON must be a list", file=sys.stderr)
        return 1

    conn = connect()
    try:
        # Validate bucket exists.
        if conn.execute(
            "SELECT 1 FROM buckets WHERE bucket_id = ?", (bucket_id,)
        ).fetchone() is None:
            print(f"error: no such bucket: {bucket_id}", file=sys.stderr)
            return 1

        all_in_bucket = {
            r[0] for r in conn.execute(
                "SELECT finding_id FROM findings WHERE bucket_id = ?",
                (bucket_id,),
            ).fetchall()
        }
        if args.replace:
            allowed = all_in_bucket
        else:
            allowed = {
                r[0] for r in conn.execute(
                    "SELECT f.finding_id FROM findings f "
                    "LEFT JOIN finding_sub_cluster fsc "
                    "  ON fsc.finding_id = f.finding_id "
                    "WHERE f.bucket_id = ? AND fsc.sub_cluster_id IS NULL",
                    (bucket_id,),
                ).fetchall()
            }

        # Validate every payload member.
        seen_members: set[int] = set()
        for i, sc in enumerate(payload):
            rep = sc.get("representative_finding_id")
            members = sc.get("member_finding_ids", [])
            summary = sc.get("summary", "")
            if not isinstance(members, list) or not members:
                print(f"error: sub-cluster #{i}: empty/missing member_finding_ids",
                      file=sys.stderr)
                return 1
            if rep not in members:
                print(f"error: sub-cluster #{i}: representative {rep} not in members",
                      file=sys.stderr)
                return 1
            for m in members:
                if m not in all_in_bucket:
                    print(f"error: sub-cluster #{i}: finding {m} not in bucket "
                          f"{bucket_id}", file=sys.stderr)
                    return 1
                if m not in allowed:
                    print(f"error: sub-cluster #{i}: finding {m} is not eligible "
                          f"(already in a sub-cluster; pass --replace to override)",
                          file=sys.stderr)
                    return 1
                if m in seen_members:
                    print(f"error: sub-cluster #{i}: finding {m} appears in "
                          f"multiple sub-clusters", file=sys.stderr)
                    return 1
                seen_members.add(m)
            if not isinstance(summary, str):
                print(f"error: sub-cluster #{i}: summary must be a string",
                      file=sys.stderr)
                return 1

        ts = now_iso()
        conn.execute("BEGIN IMMEDIATE;")
        try:
            if args.replace:
                # Drop any prior sub-cluster assignments for this bucket so
                # the FK chain stays consistent.
                conn.execute(
                    "DELETE FROM finding_sub_cluster WHERE sub_cluster_id IN "
                    "(SELECT sub_cluster_id FROM sub_clusters WHERE bucket_id = ?)",
                    (bucket_id,),
                )
                conn.execute(
                    "DELETE FROM sub_clusters WHERE bucket_id = ?", (bucket_id,)
                )
            for sc in payload:
                rep = sc["representative_finding_id"]
                summary = sc.get("summary", "") or ""
                members = sc["member_finding_ids"]
                cur = conn.execute(
                    "INSERT INTO sub_clusters(bucket_id, representative_id, "
                    "summary, triaged_at) VALUES (?, ?, ?, ?)",
                    (bucket_id, rep, summary, ts),
                )
                sub_cluster_id = cur.lastrowid
                conn.executemany(
                    "INSERT INTO finding_sub_cluster(finding_id, sub_cluster_id) "
                    "VALUES (?, ?)",
                    [(m, sub_cluster_id) for m in members],
                )
            # If every finding in the bucket is now in a sub-cluster, stamp
            # buckets.triaged_at as a convenience (informational only — not
            # used as a gate).
            still_untriaged = conn.execute(
                "SELECT 1 FROM findings f "
                "LEFT JOIN finding_sub_cluster fsc ON fsc.finding_id = f.finding_id "
                "WHERE f.bucket_id = ? AND fsc.sub_cluster_id IS NULL LIMIT 1",
                (bucket_id,),
            ).fetchone()
            if still_untriaged is None:
                conn.execute(
                    "UPDATE buckets SET triaged_at = ? WHERE bucket_id = ?",
                    (ts, bucket_id),
                )
            conn.execute("COMMIT;")
        except Exception:
            conn.execute("ROLLBACK;")
            raise
    finally:
        conn.close()

    print(f"recorded {len(payload)} sub-cluster(s) for bucket {bucket_id}")
    return 0


def cmd_assign_existing(args) -> int:
    """
    Add findings to an existing sub-cluster. JSON payload:
      [{"sub_cluster_id": <int>, "finding_ids": [<int>, ...]}, ...]

    Each finding must (a) belong to the same bucket as the target
    sub-cluster, and (b) not already be assigned to any sub-cluster.
    Atomic transaction.
    """
    payload = json.loads(Path(args.json).read_text())
    if not isinstance(payload, list):
        print("error: top-level JSON must be a list", file=sys.stderr)
        return 1

    conn = connect()
    try:
        # Validate each entry up front.
        sc_bucket: dict[int, int] = {}
        for r in conn.execute(
            "SELECT sub_cluster_id, bucket_id FROM sub_clusters"
        ).fetchall():
            sc_bucket[r[0]] = r[1]
        finding_bucket: dict[int, int] = {}
        for r in conn.execute(
            "SELECT finding_id, bucket_id FROM findings"
        ).fetchall():
            finding_bucket[r[0]] = r[1]
        already_assigned = {
            r[0] for r in conn.execute(
                "SELECT finding_id FROM finding_sub_cluster"
            ).fetchall()
        }
        seen: set[int] = set()
        for i, entry in enumerate(payload):
            sc_id = entry.get("sub_cluster_id")
            fids = entry.get("finding_ids", [])
            if sc_id not in sc_bucket:
                print(f"error: entry #{i}: no such sub_cluster_id {sc_id}",
                      file=sys.stderr)
                return 1
            tgt_bucket = sc_bucket[sc_id]
            if not isinstance(fids, list) or not fids:
                print(f"error: entry #{i}: finding_ids must be a non-empty list",
                      file=sys.stderr)
                return 1
            for fid in fids:
                if fid not in finding_bucket:
                    print(f"error: entry #{i}: no such finding {fid}",
                          file=sys.stderr)
                    return 1
                if finding_bucket[fid] != tgt_bucket:
                    print(f"error: entry #{i}: finding {fid} is in bucket "
                          f"{finding_bucket[fid]}, sub_cluster {sc_id} is in "
                          f"bucket {tgt_bucket}", file=sys.stderr)
                    return 1
                if fid in already_assigned:
                    print(f"error: entry #{i}: finding {fid} already assigned",
                          file=sys.stderr)
                    return 1
                if fid in seen:
                    print(f"error: entry #{i}: finding {fid} appears in "
                          f"multiple entries", file=sys.stderr)
                    return 1
                seen.add(fid)

        conn.execute("BEGIN IMMEDIATE;")
        try:
            for entry in payload:
                sc_id = entry["sub_cluster_id"]
                conn.executemany(
                    "INSERT INTO finding_sub_cluster(finding_id, sub_cluster_id) "
                    "VALUES (?, ?)",
                    [(fid, sc_id) for fid in entry["finding_ids"]],
                )
            # Stamp triaged_at on any bucket that is now fully covered.
            ts = now_iso()
            conn.execute(
                "UPDATE buckets SET triaged_at = ? "
                "WHERE triaged_at IS NULL AND bucket_id NOT IN ("
                "  SELECT DISTINCT f.bucket_id FROM findings f "
                "  LEFT JOIN finding_sub_cluster fsc "
                "    ON fsc.finding_id = f.finding_id "
                "  WHERE fsc.sub_cluster_id IS NULL)",
                (ts,),
            )
            conn.execute("COMMIT;")
        except Exception:
            conn.execute("ROLLBACK;")
            raise
    finally:
        conn.close()

    total = sum(len(e["finding_ids"]) for e in payload)
    print(f"assigned {total} finding(s) to {len(payload)} sub-cluster(s)")
    return 0


def cmd_sub_clusters_since(args) -> int:
    conn = connect()
    try:
        rows = conn.execute(
            "SELECT sc.sub_cluster_id, sc.bucket_id, b.guilty_pass, "
            "b.bisect_index, sc.representative_id, sc.summary, sc.triaged_at, "
            "f.normalized_path, f.error_text "
            "FROM sub_clusters sc "
            "JOIN buckets b ON b.bucket_id = sc.bucket_id "
            "JOIN findings f ON f.finding_id = sc.representative_id "
            "WHERE sc.triaged_at >= ? "
            "ORDER BY sc.bucket_id, sc.sub_cluster_id",
            (args.timestamp,),
        ).fetchall()
        members_by_sc: dict[int, list[int]] = {}
        for sc_id, fid in conn.execute(
            "SELECT sub_cluster_id, finding_id FROM finding_sub_cluster"
        ).fetchall():
            members_by_sc.setdefault(sc_id, []).append(fid)
    finally:
        conn.close()

    out = []
    for r in rows:
        sc_id = r[0]
        out.append({
            "sub_cluster_id": sc_id,
            "bucket_id": r[1],
            "guilty_pass": r[2],
            "bisect_index": r[3],
            "representative_finding_id": r[4],
            "summary": r[5],
            "triaged_at": r[6],
            "representative_normalized_path": r[7],
            "representative_error_text": r[8],
            "member_finding_ids": sorted(members_by_sc.get(sc_id, [])),
        })
    json.dump(out, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)

    pr = sub.add_parser("register")
    pr.add_argument("--reduced", required=True)
    pr.add_argument("--oracle", required=True)
    pr.add_argument("--error-text-file")
    pr.add_argument("--original-path")
    pr.add_argument("--run-id", default=None,
                    help="Run ID to attribute this finding to.")
    pr.set_defaults(func=cmd_register)

    pn = sub.add_parser("new-buckets-since")
    pn.add_argument("timestamp")
    pn.set_defaults(func=cmd_new_buckets_since)

    pb = sub.add_parser("bucket-info")
    pb.add_argument("bucket_id", type=int)
    pb.set_defaults(func=cmd_bucket_info)

    pm = sub.add_parser("migrate")
    pm.add_argument("--misc-dir", default=None,
                    help="Directory containing *.reduced.normalized.ll. "
                         "Defaults to <repo>/miscompilations.")
    pm.add_argument("--oracle-results", default=None,
                    help="Directory containing per-shard alive_tv*/fail/*.log "
                         "files. Defaults to build/oracle_results.")
    pm.add_argument("--run-id", default=None,
                    help="Attribute migrated findings to this run_id.")
    pm.set_defaults(func=cmd_migrate)

    pf = sub.add_parser("findings-in-bucket")
    pf.add_argument("bucket_id", type=int)
    pf.set_defaults(func=cmd_findings_in_bucket)

    pu = sub.add_parser("untriaged-in-bucket")
    pu.add_argument("bucket_id", type=int)
    pu.set_defaults(func=cmd_untriaged_in_bucket)

    prs = sub.add_parser("record-sub-clusters")
    prs.add_argument("bucket_id", type=int)
    prs.add_argument("--json", required=True, help="path to JSON payload")
    prs.add_argument("--replace", action="store_true",
                     help="delete prior sub-clusters for this bucket first")
    prs.set_defaults(func=cmd_record_sub_clusters)

    pss = sub.add_parser("sub-clusters-since")
    pss.add_argument("timestamp")
    pss.set_defaults(func=cmd_sub_clusters_since)

    pae = sub.add_parser("assign-existing")
    pae.add_argument("--json", required=True,
                     help="path to [{sub_cluster_id, finding_ids: [...]}, ...]")
    pae.set_defaults(func=cmd_assign_existing)

    args = p.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
