# Regatoni

LLVM optimization pass fuzzer. Finds miscompilations (via Alive2) and crashes in `opt`.

## Setup

```bash
./scripts/build/bootstrap.sh           # system deps (Ubuntu/Debian)
./scripts/build/setup_deps.sh          # LLVM (sancov/asan/plain) + Alive2
./scripts/build/build_centipede.sh     # Centipede fuzzing engine
```

## Seeds

```bash
./scripts/build/collect_seeds.sh       # grab .ll files from LLVM test suite
./scripts/build/split_seeds.sh         # split multi-function files
```

## Build & Test

```bash
./scripts/run/run_tests.sh             # mutation unit tests
./scripts/build/link_fuzz_target.sh    # build fuzz harnesses
```

## Fuzz

```bash
./scripts/run/run_fuzzer.sh            # launch Centipede
```

## Verify miscompilations (background)

```bash
nohup ./scripts/run/verify_corpus.sh $PWD/corpus > verify.log 2>&1 &
```

## Triage

```bash
python3 scripts/analysis/triage_miscompilations.py
# results in miscompilations/report.md
```

Crashes: `build/workdir/crashes.*/`
Miscompilations: `miscompilations/`