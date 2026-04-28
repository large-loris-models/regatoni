#!/usr/bin/env python3
"""Diff two Centipede coverage reports.

Reuses the pass-detection logic from pass_coverage.py so passes are bucketed
and named the same way. For each pass, prints before -> after numbers for
Touched%, FULL, PARTIAL and NONE, plus a delta in Touched%. Default sort is
by largest improvement in Touched%.
"""

import argparse
import sys
from pathlib import Path

# Reuse the parsing/classification logic from the sibling module.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from pass_coverage import (  # noqa: E402
    BUCKETS_ORDER,
    BUCKET_ALIASES,
    INTRAPROCEDURAL_PASSES,
    MIN_TOTAL,
    classify,
    extract_o2_pipeline,
    format_pct,
    scan_report,
)


def fmt_pair_pct(before: float, after: float) -> str:
    return f"{format_pct(before):>4} -> {format_pct(after):<4}"


def fmt_pair_int(before: int, after: int) -> str:
    return f"{before:>5} -> {after:<5}"


def fmt_delta_pct(delta: float) -> str:
    if abs(delta) < 0.5:
        return "  0%"
    sign = '+' if delta > 0 else '-'
    return f"{sign}{int(round(abs(delta)))}%"


def build_rows(counters_a, counters_b, o2_set):
    """Produce one row per pass that appears in either report.

    Each row: (name, bucket, before_counts, after_counts, before_touched,
               after_touched, delta_touched, total_after).
    """
    rows = []
    for name in sorted(set(counters_a) | set(counters_b)):
        a = counters_a.get(name, [0, 0, 0])
        b = counters_b.get(name, [0, 0, 0])
        ta = sum(a)
        tb = sum(b)
        # Skip passes too small in both reports — same threshold as pass_coverage.
        if ta < MIN_TOTAL and tb < MIN_TOTAL:
            continue
        touched_a = (a[0] + a[1]) * 100.0 / ta if ta else 0.0
        touched_b = (b[0] + b[1]) * 100.0 / tb if tb else 0.0
        rows.append((
            name,
            classify(name, o2_set),
            tuple(a),
            tuple(b),
            touched_a,
            touched_b,
            touched_b - touched_a,
            tb,
        ))
    return rows


def row_changed(row) -> bool:
    return row[2] != row[3]


HEADER = (
    f"{'Pass':<30}  {'Touched%':<14}  {'dTouch':>6}  "
    f"{'FULL':<14}  {'PARTIAL':<14}  {'NONE':<14}"
)
SEP = (
    f"{'-'*30:<30}  {'-'*14:<14}  {'-'*6:>6}  "
    f"{'-'*14:<14}  {'-'*14:<14}  {'-'*14:<14}"
)


def print_rows(title, rows):
    print(f"=== {title} ({len(rows)} passes) ===")
    if not rows:
        print("(nothing to show)")
        print()
        return
    print(HEADER)
    print(SEP)
    for name, _bucket, a, b, ta, tb, dt, _total in rows:
        touched = fmt_pair_pct(ta, tb)
        full = fmt_pair_int(a[0], b[0])
        partial = fmt_pair_int(a[1], b[1])
        none = fmt_pair_int(a[2], b[2])
        print(
            f"{name:<30}  {touched:<14}  {fmt_delta_pct(dt):>6}  "
            f"{full:<14}  {partial:<14}  {none:<14}"
        )
    print()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('before_report', help='Earlier coverage report')
    ap.add_argument('after_report', help='Later coverage report')
    ap.add_argument(
        '--intra', '--intraprocedural', dest='intra', action='store_true',
        help='Restrict to intraprocedural O2 passes (Alive2-verifiable set)',
    )
    ap.add_argument(
        '--all', action='store_true',
        help='Show passes whose counts are identical between reports too',
    )
    ap.add_argument(
        '--bucket',
        help='Show only one category (o2, analysis, sanitizers, codegen, '
             'lto, specialized, utilities, unknown)',
    )
    ap.add_argument(
        '--by-uncovered', dest='by_uncovered', action='store_true',
        help='Sort by largest reduction in NONE count (most newly-covered)',
    )
    ap.add_argument(
        '--regressed-first', action='store_true',
        help='Sort by largest Touched%% drop instead of largest gain',
    )
    ap.add_argument(
        '--min-delta', type=float, default=0.0,
        help='Hide rows whose |dTouched%%| is below this threshold',
    )
    ap.add_argument(
        '--summary', action='store_true',
        help='Print only the summary block, no per-pass table',
    )
    args = ap.parse_args()

    for label, p in (('before', args.before_report), ('after', args.after_report)):
        if not Path(p).is_file():
            sys.stderr.write(f"{label} report not found: {p}\n")
            return 1

    repo_root = Path.cwd()
    o2_set, o2_source = extract_o2_pipeline(repo_root)
    print(f"O2 pipeline: {len(o2_set)} passes ({o2_source})")
    print(f"Before: {args.before_report}")
    print(f"After:  {args.after_report}")
    print()

    counters_a, fa, pa, na = scan_report(args.before_report)
    counters_b, fb, pb, nb = scan_report(args.after_report)

    rows = build_rows(counters_a, counters_b, o2_set)

    if args.intra:
        rows = [r for r in rows if r[0] in INTRAPROCEDURAL_PASSES]

    if args.bucket:
        key = args.bucket.lower()
        target = BUCKET_ALIASES.get(key, args.bucket)
        if target not in BUCKETS_ORDER:
            sys.stderr.write(f"Unknown bucket '{args.bucket}'. Choose from:\n")
            for name in BUCKETS_ORDER:
                sys.stderr.write(f"  {name}\n")
            return 2
        rows = [r for r in rows if r[1] == target]

    # Compute summary BEFORE filtering out unchanged rows so the counts are
    # over the full population.
    improved = sum(1 for r in rows if r[6] > 0.5)
    regressed = sum(1 for r in rows if r[6] < -0.5)
    unchanged = sum(1 for r in rows if abs(r[6]) <= 0.5)

    visible = rows
    if not args.all:
        visible = [r for r in visible if row_changed(r)]
    if args.min_delta > 0:
        visible = [r for r in visible if abs(r[6]) >= args.min_delta]

    if args.by_uncovered:
        # Largest reduction in NONE first (a[2] - b[2] is positive = newly covered).
        sort_key = lambda r: (-(r[2][2] - r[3][2]), -r[6])
    elif args.regressed_first:
        sort_key = lambda r: (r[6], -r[7])
    else:
        sort_key = lambda r: (-r[6], -r[7])
    visible.sort(key=sort_key)

    if not args.summary:
        title = 'Pass coverage delta'
        if args.intra:
            title += ' (intraprocedural)'
        if args.bucket:
            title += f' [{args.bucket}]'
        print_rows(title, visible)

    covered_a = fa + pa
    covered_b = fb + pb
    delta_total = covered_b - covered_a
    sign = '+' if delta_total >= 0 else ''
    print("=== Summary ===")
    print(
        f"Total functions covered: {covered_a} -> {covered_b} "
        f"({sign}{delta_total})"
    )
    print(f"  FULL:    {fa} -> {fb} ({fb - fa:+d})")
    print(f"  PARTIAL: {pa} -> {pb} ({pb - pa:+d})")
    print(f"  NONE:    {na} -> {nb} ({nb - na:+d})")
    print(f"Passes improved:  {improved}")
    print(f"Passes regressed: {regressed}")
    print(f"Passes unchanged: {unchanged}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
