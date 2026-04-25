# Miscompilation Triage Playbook

## Preliminaries

The current directory should contain a collection of files in LLVM IR
(intermediate representation). You may assume that:
- The user has placed the files here because the `alive-tv` tool
  believes that the current version of LLVM optimizes these files
  incorrectly.
- Each `.ll` file defines a single LLVM function.
- The user has used `llvm-reduce` to minimize the size of every
  LLVM IR file.
- The user has normalized all IR files as much as possible, for
  example by removing named SSA values.
- The user has run `alive-tv` on every file and saved its output.
  For example, the output for `foo.ll` would be saved as `foo.ll.log`.

## Your Job

Stop now, and mention the problem to the user, if it is not the case
that the current directory contains at least one `.ll` file, if any
`.ll` file in the current directory (non-recursively) does not have a
corresponding `.ll.log` file, or if any `.ll.log` file in the current
directory does not have a corresponding `.ll` file.

You are to read all of the `.ll.log` files that were produced by
`alive-tv`, along with their matching `.ll` files. Then, the important
task you are to perform is placing these functions into a smaller
number of buckets, where each bucket corresponds to the same likely
optimizer root cause. Prefer splitting buckets over merging them when
you are not confident that two failures share the same cause. To make
this determination, you should carefully look at the entire output,
not just one single part of it.

Your results are to be presented to the user as a markdown file called
`report.md`. It should contain one section per failure bucket. Each
bucket should contain a short summary of the issue detected by
`alive-tv`, including the observed mismatch, the suspected LLVM pass or
root cause if you can infer it, and your confidence in the bucketing.
It should also include the full LLVM IR for one of the files in the
bucket. You must not print the Alive2 IR, which is part of the log
file -- this is slightly different from LLVM IR. You must print the
actual LLVM IR from the `.ll` file. Also, for each bucket, provide the
file names of all LLVM IR files that go into that bucket. Each LLVM
file should go into exactly one bucket.

## Dealing with Difficult Cases

Do not ask the user for help. Rather, if you have doubts about which
bucket some file belongs to, you may construct additional LLVM IR
files that are similar to the ones you are attempting to disambiguate,
and then see what `~/alive2-regehr/build/alive-tv -disable-undef-input`
has to say about them. If you still cannot disambiguate the situation,
then you should simply place the questionable IR file into its own
bucket, and explain the situation thoroughly in your report.

## Additional Resources

A current LLVM tree may be available at `~/llvm-project/llvm/`.  You may
consult the LLVM source code, and you may also want to run tools such
as `opt` which can be found in `~/llvm-project/for-alive/bin/opt`. If
you have questions about LLVM semantics, you may consult
`~/llvm-project/llvm/docs/LangRef.rst`.
