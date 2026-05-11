# Bucket Triage Prompt

You are clustering miscompilation findings within a single `opt-bisect-limit`
bucket. Every `.ll` file in this directory was blamed by binary-search on the
same `(guilty_pass, bisect_index)`. That is necessary-not-sufficient evidence
of being the same bug: passes like InstCombine, EarlyCSE, and FunctionAttrs
are themselves collections of dozens of independent rewrite rules, and one
bucket routinely fuses several distinct bugs that happen to be observed by
the same pass first.

Your job is to sub-cluster the findings here by **root cause**, judging from
the IR shape and from Alive2's verdict.

## Inputs

The working directory contains:

- `BUCKET_CONTEXT.md` — the bucket's `(guilty_pass, bisect_index)`, total
  finding count, and (when sampling is in effect) the sample size.
- `<finding_id>.ll` — the normalized LLVM IR witness for one finding.
- `<finding_id>.log` — the matching `alive-tv` output that flagged it.

Filenames are integer finding ids — not corpus hashes. Use the integer ids
as `representative_finding_id` / `member_finding_ids` in your output.

If `EXISTING_SUB_CLUSTERS.md` is present, this is **assign mode** — see the
section at the bottom.

## What counts as the same root cause

Two findings share a root cause when:

- the same buggy rewrite rule (or the same kind of refinement violation in
  Alive2's verdict) explains both, and
- the IR difference Alive2 cites is structurally the same — same kind of
  operands, same kind of folding, same family of UB introduced or value
  disagreement.

Different surface forms — different operand bit-widths, different SSA names,
different unrelated context around the buggy bit — can still be the same
bug if the underlying transformation is the same.

Concrete examples drawn from past triage of the `instcombine @ idx=14`
bucket:

- A finding whose IR contains `llvm.fabs.f16` followed by an `fcmp olt`
  against the smallest normal half (`0xH0400`) under a non-default
  `denormal-fp-math` attribute, and Alive2 reports a value mismatch on
  the boolean return — InstCombine collapsing fabs+fcmp into a denormal
  class check that is not valid in this denormal mode.
- A finding whose IR contains `getelementptr nusw i32, ptr %p, i64 %i`
  on a target with non-trivial pointer-index sizing (e.g. data layout
  `p:40:64:64:32`), and Alive2 reports a divergence in the computed
  pointer — InstCombine re-folding the GEP with wrong index-width
  assumptions.

These two bisect to the same pass+index, but they are **different bugs**
and must be in **different sub-clusters**.

## Output format

Write a single file `sub_clusters.json` to the working directory. No
preamble or trailing commentary on stdout. The file must be a JSON array:

```json
[
  {
    "representative_finding_id": <int, must be one of member_finding_ids>,
    "summary": "<1-2 sentence description of the root cause>",
    "member_finding_ids": [<int>, <int>, ...]
  },
  ...
]
```

Hard rules:

- Every finding id present in this directory must appear in **exactly one**
  sub-cluster's `member_finding_ids`.
- `representative_finding_id` must be one of `member_finding_ids` (typically
  the smallest or clearest example).
- Singletons are fine. Prefer over-splitting to over-merging — if you cannot
  confidently say two findings share a root cause, split them.
- The summary should be terse and specific: name the suspected rewrite or
  the kind of refinement violation, not a generic phrase.

## Sample mode (only when noted in `BUCKET_CONTEXT.md`)

When the bucket is large, you may receive a deterministic sample of N
findings drawn from a bucket of M total. Cluster the N findings you can
see. A second pass over the remaining M-N findings will be run separately
and will assign them to your sub-clusters or create new ones.

## Assign mode (only when `EXISTING_SUB_CLUSTERS.md` is present)

In assign mode, the bucket already has sub-clusters (from a prior triage
run or from the first pass of a sampled bucket). `EXISTING_SUB_CLUSTERS.md`
lists each existing sub-cluster's id, summary, and representative IR. The
`<finding_id>.ll`/`.log` pairs in this directory are findings that need to
be assigned to one of those sub-clusters or split off into new ones.

Output format is **the same** `sub_clusters.json` schema, but with two
extensions:

- To assign a finding to an existing sub-cluster, set
  `"existing_sub_cluster_id": <int>` and put just the new finding ids in
  `member_finding_ids` (the representative is the existing one — you can
  set `representative_finding_id` to any existing member of that
  sub-cluster, or to one of the new findings; the harness ignores it for
  existing sub-clusters).
- To create a new sub-cluster, omit `existing_sub_cluster_id` and follow
  the normal Output Format rules.

Every new finding present in the directory must be accounted for exactly
once across the output entries. Do not list pre-existing findings.
