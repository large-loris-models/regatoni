#!/usr/bin/env python3
"""parse_centipede_log.py — incremental tail parser for build/run_state/run.log.

Two outputs in <workdir>:
  regatoni-batch-stats.tsv   — one row per `[Sx.y] <log_type>: ft: ...` line
  regatoni-events.jsonl      — one row per `FUNC: ...` / `EDGE: ...` line

Byte-offset persistence in --offset-file lets the daemon resume across
restarts without reprocessing.

Usage:
  parse_centipede_log.py --watch \\
      --log build/run_state/run.log \\
      --workdir build/workdir_<date> \\
      --offset-file build/run_state/run.log.offset
"""

import argparse
import datetime as dt
import json
import os
import re
import signal
import sys
import time

# Fields that fuzzing-stats lines may carry. usrN slots: at most 16 today
# (kUserDomains has 16 entries in centipede/feature.h); we surface 0..13.
USR_SLOTS = list(range(14))

BATCH_COLUMNS = (
    ["iso_ts", "unix_micros", "shard", "batch_index", "log_type",
     "ft", "cov", "cnt", "cmp"]
    + [f"usr{i}" for i in USR_SLOTS]
    + ["corp_active", "corp_total", "crash", "exec_per_s"]
)

# Glog prefix:
#   IMMDD HH:MM:SS.ffffff  PID file:line] <msg>
# Year is not encoded in glog's default format. We approximate by using the
# log file's mtime year — close enough for runs that don't span new year.
GLOG_RE = re.compile(
    r"^I(\d{2})(\d{2})\s+(\d{2}):(\d{2}):(\d{2})\.(\d+)\s+\d+\s+\S+\]\s+(.*)$"
)
BATCH_RE = re.compile(r"^\[S(\d+)\.(\d+)\]\s+(\S+):\s+(.*)$")
KV_INT_RE = re.compile(r"(\b[a-zA-Z][\w/]*?):\s*([-+]?\d+(?:\.\d+)?)")
CORP_RE = re.compile(r"\bcorp:\s*(\d+)/(\d+)")
EXEC_PER_S_RE = re.compile(r"\bexec/s:\s*(\d+(?:\.\d+)?)")
EVENT_RE = re.compile(r"^(FUNC|EDGE):\s+(.*)$")


def iso_and_micros(year, month, day, hour, minute, sec, frac_str):
    micros = int((frac_str + "000000")[:6])
    t = dt.datetime(year, month, day, hour, minute, sec, micros,
                    tzinfo=dt.timezone.utc)
    unix_micros = int(t.timestamp() * 1_000_000)
    return t.isoformat(), unix_micros


def parse_batch_body(body):
    """Pull ft/cov/cnt/cmp/usrN/corp/crash/exec_per_s out of a stats line tail."""
    out = {k: "" for k in BATCH_COLUMNS}
    # Generic key:int pairs (won't catch corp/exec_per_s because of the slash).
    for m in KV_INT_RE.finditer(body):
        k = m.group(1)
        v = m.group(2)
        if k in ("ft", "cov", "cnt", "cmp", "crash"):
            out[k] = v
        elif k.startswith("usr") and k[3:].isdigit():
            i = int(k[3:])
            if i in USR_SLOTS:
                out[f"usr{i}"] = v
    cm = CORP_RE.search(body)
    if cm:
        out["corp_active"] = cm.group(1)
        out["corp_total"] = cm.group(2)
    em = EXEC_PER_S_RE.search(body)
    if em:
        out["exec_per_s"] = em.group(1)
    return out


def parse_line(line, log_year):
    g = GLOG_RE.match(line)
    if not g:
        return None
    mm, dd, HH, MM, SS, frac, body = g.groups()
    try:
        iso, um = iso_and_micros(log_year, int(mm), int(dd), int(HH),
                                 int(MM), int(SS), frac)
    except ValueError:
        return None

    bm = BATCH_RE.match(body)
    if bm:
        shard, batch_idx, log_type, tail = bm.groups()
        row = parse_batch_body(tail)
        row["iso_ts"] = iso
        row["unix_micros"] = str(um)
        row["shard"] = shard
        row["batch_index"] = batch_idx
        row["log_type"] = log_type
        return ("batch", row)

    em = EVENT_RE.match(body)
    if em:
        kind, rest = em.groups()
        # rest is "<demangled symbol> file:line:col" — split on the *last*
        # whitespace before what looks like a path with two colons.
        # Take the trailing token that has at least one ':' as source_file.
        parts = rest.rsplit(" ", 1)
        if len(parts) == 2 and ":" in parts[1]:
            symbol, source_file = parts
        else:
            symbol, source_file = rest, ""
        return ("event", {"unix_micros": um, "kind": kind,
                          "symbol": symbol, "source_file": source_file})
    return None


class Outputs:
    def __init__(self, workdir):
        self.tsv_path = os.path.join(workdir, "regatoni-batch-stats.tsv")
        self.jsonl_path = os.path.join(workdir, "regatoni-events.jsonl")
        self.tsv_f = None
        self.jsonl_f = None

    def open(self):
        # Append; write TSV header only if file is new/empty.
        new_tsv = (not os.path.exists(self.tsv_path)
                   or os.path.getsize(self.tsv_path) == 0)
        self.tsv_f = open(self.tsv_path, "a", buffering=1)
        if new_tsv:
            self.tsv_f.write("\t".join(BATCH_COLUMNS) + "\n")
        self.jsonl_f = open(self.jsonl_path, "a", buffering=1)

    def write_batch(self, row):
        self.tsv_f.write("\t".join(row[k] for k in BATCH_COLUMNS) + "\n")

    def write_event(self, ev):
        self.jsonl_f.write(json.dumps(ev) + "\n")

    def close(self):
        for f in (self.tsv_f, self.jsonl_f):
            if f:
                try:
                    f.close()
                except Exception:
                    pass


def read_offset(path):
    try:
        with open(path) as f:
            return int(f.read().strip() or "0")
    except FileNotFoundError:
        return 0
    except ValueError:
        return 0


def write_offset(path, n):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write(str(n))
    os.replace(tmp, path)


def process_chunk(chunk, log_year, outs):
    n_batch = n_event = 0
    for line in chunk.decode("utf-8", errors="replace").splitlines():
        r = parse_line(line, log_year)
        if r is None:
            continue
        kind, payload = r
        if kind == "batch":
            outs.write_batch(payload)
            n_batch += 1
        elif kind == "event":
            outs.write_event(payload)
            n_event += 1
    return n_batch, n_event


_stop = False


def _on_signal(signum, frame):
    global _stop
    _stop = True


def watch(args, outs):
    signal.signal(signal.SIGTERM, _on_signal)
    signal.signal(signal.SIGINT, _on_signal)
    last = read_offset(args.offset_file) if args.offset_file else 0
    log_year = dt.datetime.utcnow().year
    while not _stop:
        try:
            cur = os.path.getsize(args.log)
        except FileNotFoundError:
            cur = 0
        if cur < last:
            # Truncated/rotated.
            last = 0
        if cur > last:
            with open(args.log, "rb") as f:
                f.seek(last)
                chunk = f.read(cur - last)
            nl = chunk.rfind(b"\n")
            if nl < 0:
                time.sleep(args.poll_seconds)
                continue
            usable = chunk[: nl + 1]
            process_chunk(usable, log_year, outs)
            last += len(usable)
            if args.offset_file:
                write_offset(args.offset_file, last)
        time.sleep(args.poll_seconds)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--log", required=True, help="Path to run.log")
    p.add_argument("--workdir", required=True,
                   help="Output dir (for the .tsv and .jsonl)")
    p.add_argument("--offset-file", default=None,
                   help="Persist byte offset across restarts")
    p.add_argument("--watch", action="store_true",
                   help="Daemon mode; poll for new content")
    p.add_argument("--poll-seconds", type=float, default=2.0)
    args = p.parse_args()

    os.makedirs(args.workdir, exist_ok=True)
    outs = Outputs(args.workdir)
    outs.open()
    try:
        if args.watch:
            watch(args, outs)
        else:
            # One-shot: read from offset to current size, then exit.
            last = read_offset(args.offset_file) if args.offset_file else 0
            try:
                cur = os.path.getsize(args.log)
            except FileNotFoundError:
                cur = 0
            if cur < last:
                last = 0
            if cur > last:
                with open(args.log, "rb") as f:
                    f.seek(last)
                    chunk = f.read(cur - last)
                nl = chunk.rfind(b"\n")
                if nl >= 0:
                    usable = chunk[: nl + 1]
                    log_year = dt.datetime.utcnow().year
                    process_chunk(usable, log_year, outs)
                    last += len(usable)
                    if args.offset_file:
                        write_offset(args.offset_file, last)
    finally:
        outs.close()


if __name__ == "__main__":
    main()
