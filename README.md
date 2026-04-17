# regatoni

A coverage-guided fuzzer for LLVM optimization passes.

## Overview

Regatoni generates and mutates LLVM IR, feeds it through `opt -O2` under [Centipede](https://github.com/google/fuzztest/tree/main/centipede), and watches for three classes of defects:

- **Optimizer crashes** — assertion failures, segfaults, `llvm_unreachable`
- **Miscompilations** — semantic divergence between original and optimized IR, detected by the [Alive2](https://github.com/AliveToolkit/alive2) oracle (`alive-tv`) running in the background.
- **Memory / UB bugs in the optimizer** — found by re-running corpus entries under an ASAN+UBSAN build of `opt`.

The pipeline has two halves: a Centipede fuzzer driving a sancov-instrumented harness with 14 custom IR mutations, and an oracle framework that consumes new corpus entries and classifies them as `pass` / `fail` / `timeout` / `error`.

## Quick Start

Ubuntu/Debian host with `sudo`. Everything else (clang, cmake, ninja, z3, bazel, ...) is installed by `bootstrap.sh`.

```bash
./scripts/build/bootstrap.sh              # system deps
./scripts/build/setup_deps.sh             # LLVM (sancov/asan/plain) + Alive2
./scripts/build/build_centipede.sh        # Centipede fuzzing engine
./scripts/build/collect_seeds.sh          # grab .ll files from LLVM test suite
./scripts/build/split_seeds.py            # split multi-function files
./scripts/run/run_tests.sh                # mutation unit tests
./scripts/build/link_fuzz_target.sh       # build both harnesses
./scripts/run/start.sh                    # launch fuzzer + oracles
```

## Running

```bash
nohup ./scripts/run/start.sh > build/run.log 2>&1 &
tail -f build/run_state/run.log           # live log
./scripts/analysis/status.sh              # dashboard
...
./scripts/run/stop.sh                     
```

PIDs for the fuzzer and oracles are tracked in `build/run_state/pids`. `stop.sh` reads that file and terminates each process in order.

## What Runs

`start.sh` launches three processes and pins the oracles to dedicated cores:

| Process    | Binary                          | Cores          |
|------------|---------------------------------|----------------|
| fuzzer     | `opt_fuzz_target` (sancov)      | `FUZZ_JOBS=4`  |
| alive_tv   | `opt_fuzz_target_alive2` (ASAN) | 1 (pinned)     |
| asan_opt   | ASAN-instrumented `opt`         | 1 (pinned)     |

The fuzzer uses `LLVMFuzzerCustomMutator` to parse -> mutate -> re-serialize IR via the mutation registry, then runs `opt -O2` in-process. Any crash is a bug. The alive-tv oracle invokes Alive2's `compareFunctions` on each new corpus entry; confirmed miscompilations are saved and passed through `llvm-reduce`. The ASAN oracle runs `opt -O2` under AddressSanitizer + UBSan to surface optimizer memory errors that don't assert.

## Results

```
build/workdir_<date>/crashes.*/           # Centipede crash inputs
build/workdir_<date>/coverage-report-*.txt
build/oracle_results/alive_tv/{pass,fail,timeout,error}/
build/oracle_results/asan_opt/{pass,fail,timeout,error}/
miscompilations/                          # triaged miscompilations
```

Analysis:

```bash
./scripts/analysis/status.sh                                 # dashboard
./scripts/analysis/unique_crashes.sh build/run_state/run.log # crash dedup by assertion
./scripts/analysis/pass_coverage.py                          # per-pass coverage roll-up
```

## Mutations

14 mutations ported from `alive-mutate`, all in `src/mutators/ir_mutations/`, registered in `src/mutators/registry.cc`, tested in `tests/test_mutations.cc`.

| Mutation               | Effect                                                            |
|------------------------|-------------------------------------------------------------------|
| `swap_binop`           | Replace a binary opcode with another of compatible type           |
| `swap_cmp_predicate`   | Change an `icmp`/`fcmp` predicate                                 |
| `modify_flags`         | Toggle `nsw`/`nuw`/`exact`/`fast-math` flags                      |
| `swap_operands`        | Swap operands of a binary op                                      |
| `replace_operand`      | Replace an operand with another in-scope value or a constant      |
| `shuffle_instructions` | Reorder instructions within a basic block                         |
| `move_instruction`     | Move an instruction to a different (still-valid) position         |
| `inline_call`          | Inline a direct call site                                         |
| `remove_void_call`     | Delete a `void`-returning call                                    |
| `modify_attributes`    | Add/remove function/param/return attributes                       |
| `mutate_gep`           | Perturb a GEP's indices or in-bounds flag                         |
| `resize_type`          | Change an integer/vector width                                    |
| `mutate_unary`         | Swap a unary op (`fneg`, casts) with a compatible alternative     |
| `eliminate_undef`      | Replace `undef`/`poison` operands with concrete constants         |

Every mutation runs `verifyModule()` before returning; invalid output is rejected so the harness never crashes on a mutation bug.

## Architecture

```
src/harness/          opt_fuzz_target.cc         (sancov + mutation loop)
                      opt_fuzz_target_alive2.cc  (ASAN + Alive2 compareFunctions)
src/mutators/         base.h, registry.{h,cc}
  ir_mutations/       14 mutation implementations
scripts/build/        bootstrap, setup_deps, build_centipede, link_fuzz_target, env
scripts/run/          start, stop, run_tests, run_fuzzer, verify_corpus
scripts/oracles/      alive_tv.sh, asan_opt.sh, run_oracles.sh, reduce_miscompilation.sh
scripts/analysis/     status.sh, unique_crashes.sh, pass_coverage.py, triage_miscompilations.py
tests/                test_mutations.cc
deps/                 llvm-build-{sancov,asan,plain}/, alive2/, alive2-arm-tv/
```

## LLVM Builds

`setup_deps.sh` produces three separate LLVM trees because their requirements conflict:

- **`deps/llvm-build-sancov/`** — built with `-fsanitize-coverage=inline-8bit-counters,pc-table,trace-cmp`. Linked into the main fuzz target so Centipede gets coverage feedback from inside `opt`.
- **`deps/llvm-build-asan/`** — built with `-fsanitize=address,undefined`. Provides both the ASAN-instrumented `opt` binary and the LLVM libraries linked into `opt_fuzz_target_alive2`.
- **`deps/llvm-build-plain/`** — no sanitizers, includes multiple targets. Used by Alive2 (`alive-tv`), `llvm-reduce`, `llc`, and `llvm-symbolizer`.

Alive2 is built twice: the main build against `llvm-build-plain`, plus an `arm-tv` variant (`deps/alive2-arm-tv/`) for future backend translation validation.
