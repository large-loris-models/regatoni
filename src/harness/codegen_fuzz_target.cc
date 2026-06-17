// codegen_fuzz_target.cc — Centipede fuzz target for the LLVM RISC-V *backend*.
//
// Unlike opt_fuzz_target.cc (which runs `opt -O2`, so its sancov coverage tracks
// the middle-end optimizer), this target runs each input through the riscv64
// backend codegen pipeline (SelectionDAG isel by default; GlobalISel optionally).
// The sancov coverage therefore tracks ISEL / codegen — so Centipede is steered
// toward inputs that exercise new isel rules, which is what we want when hunting
// wrong RISC-V isel rules (orc.b / Zbb, ...). The backend-tv oracle then proves
// or refutes each interesting input.
//
// Target config is kept IDENTICAL to the backend-tv oracle
// (deps/alive2-arm-tv/tools/backend-tv.cpp + run_backend.cpp) so the fuzzer and
// the validator explore the same isel space:
//   triple    riscv64-unknown-linux-gnu
//   cpu       generic
//   features  +c,+m,+b,+f,+d,+q,+zfh        (+b = +zba,+zbb,+zbs -> orc.b)
//   FloatABI  Hard, machine outliner off, AssemblyFile via legacy PM.
//
// A codegen assertion/crash on valid IR is itself a backend bug — Centipede
// catches it as a crash. GlobalISel (env CODEGEN_GLOBAL_ISEL=1) is run with abort
// disabled so its *known* incompleteness falls back to SelectionDAG instead of
// flooding us with not-implemented crashes.
//
// The mutator (LLVMFuzzerCustomMutator) is shared verbatim with opt_fuzz_target.

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "src/mutators/corpus_index.h"
#include "src/mutators/registry.h"
#include "src/analysis/ir_feature_tuples.h"
#include "src/mutators/ir_mutations/graft_value.h"

#include <atomic>
#include <chrono>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <sstream>
#include <string>
#include <unistd.h>

static llvm::LLVMContext *Ctx = nullptr;

// riscv64 codegen target machines, built once and reused (Centipede runs inputs
// serially per worker). We keep BOTH isel pipelines so a single fuzzing run
// drives — and gathers coverage over — SelectionDAG *and* GlobalISel: a test
// case that's interesting for one backend is almost always interesting for the
// other, and SDAG (the default/production isel for RISC-V) is the higher-value
// target. Which to run is set by CODEGEN_ISEL=dag|gisel|both (default both).
static llvm::TargetMachine *g_tm_dag = nullptr;   // SelectionDAG isel
static llvm::TargetMachine *g_tm_gisel = nullptr; // GlobalISel isel
static bool g_run_dag = true;
static bool g_run_gisel = true;
static const char *kTripleStr = "riscv64-unknown-linux-gnu";
static const char *kCPU = "generic";
static const char *kFeatures = "+c,+m,+b,+f,+d,+q,+zfh";

static thread_local uint32_t g_last_mutation_id = 0;

// ── Per-mutator harness counters (shared layout with opt_fuzz_target) ─────────
namespace {
constexpr size_t kNumMutators = 14;
enum StatField : size_t {
  kAttempted = 0, kApplied = 1, kParseFail = 2,
  kVerifyFail = 3, kTooLarge = 4, kSuccess = 5, kNumStatFields = 6,
};
}  // namespace
static std::atomic<uint64_t> g_mutation_stats[kNumMutators][kNumStatFields];
static std::atomic<uint64_t> g_inputs_since_flush{0};
static uint64_t g_flush_every = 1000;
static std::string g_stats_path;

namespace {
enum FallbackSite : size_t {
  kFallbackEmptyInput = 0, kFallbackParseFail = 1, kFallbackNoneApplicable = 2,
  kFallbackApplyFailed = 3, kFallbackVerifyFail = 4, kFallbackTooLarge = 5,
  kFallbackSiteCount = 6,
};
}  // namespace
static std::atomic<uint64_t> g_fallback_counters[kFallbackSiteCount];
static const char *kFallbackSiteNames[kFallbackSiteCount] = {
    "fallback_empty_input", "fallback_parse_fail", "fallback_none_applicable",
    "fallback_apply_failed", "fallback_verify_fail", "fallback_too_large",
};
static std::string g_fallback_stats_path;

static void bumpFallback(FallbackSite site) {
  g_fallback_counters[site].fetch_add(1, std::memory_order_relaxed);
}

static uint64_t now_unix_micros() {
  using namespace std::chrono;
  return duration_cast<microseconds>(system_clock::now().time_since_epoch())
      .count();
}

static void initMutationStats() {
  const char *workdir = getenv("REGATONI_WORKDIR");
  std::string base = (workdir && *workdir) ? workdir : ".";
  std::ostringstream os;
  os << base << "/regatoni-codegen-mutation-stats." << getpid() << ".csv";
  g_stats_path = os.str();
  if (FILE *f = fopen(g_stats_path.c_str(), "w")) {
    fprintf(f, "unix_micros,mutator_id,mutator_name,attempted,applied,"
               "parse_fail,verify_fail,too_large,success\n");
    fclose(f);
  } else {
    g_stats_path.clear();
  }
  std::ostringstream fos;
  fos << base << "/regatoni-codegen-fallback-stats." << getpid() << ".csv";
  g_fallback_stats_path = fos.str();
  if (FILE *f = fopen(g_fallback_stats_path.c_str(), "w")) {
    fprintf(f, "unix_micros,site_id,site_name,count\n");
    fclose(f);
  } else {
    g_fallback_stats_path.clear();
  }
  if (const char *e = getenv("REGATONI_MUTATION_FLUSH_EVERY")) {
    unsigned long v = strtoul(e, nullptr, 10);
    if (v > 0) g_flush_every = v;
  }
}

static void flushMutationStats() {
  uint64_t ts = now_unix_micros();
  if (!g_stats_path.empty()) {
    if (FILE *f = fopen(g_stats_path.c_str(), "a")) {
      const auto &all = regatoni::MutationRegistry::instance().all();
      for (size_t i = 0; i < kNumMutators; ++i) {
        std::string name = (i < all.size()) ? all[i]->name() : std::string();
        fprintf(f, "%llu,%zu,%s,%llu,%llu,%llu,%llu,%llu,%llu\n",
                (unsigned long long)ts, i + 1, name.c_str(),
                (unsigned long long)g_mutation_stats[i][kAttempted].load(std::memory_order_relaxed),
                (unsigned long long)g_mutation_stats[i][kApplied].load(std::memory_order_relaxed),
                (unsigned long long)g_mutation_stats[i][kParseFail].load(std::memory_order_relaxed),
                (unsigned long long)g_mutation_stats[i][kVerifyFail].load(std::memory_order_relaxed),
                (unsigned long long)g_mutation_stats[i][kTooLarge].load(std::memory_order_relaxed),
                (unsigned long long)g_mutation_stats[i][kSuccess].load(std::memory_order_relaxed));
      }
      fclose(f);
    }
  }
  if (!g_fallback_stats_path.empty()) {
    if (FILE *f = fopen(g_fallback_stats_path.c_str(), "a")) {
      for (size_t i = 0; i < kFallbackSiteCount; ++i)
        fprintf(f, "%llu,%zu,%s,%llu\n", (unsigned long long)ts, i,
                kFallbackSiteNames[i],
                (unsigned long long)g_fallback_counters[i].load(std::memory_order_relaxed));
      fclose(f);
    }
  }
}

static void bumpStat(uint32_t mutation_id_1_based, StatField field) {
  if (mutation_id_1_based == 0 || mutation_id_1_based > kNumMutators) return;
  g_mutation_stats[mutation_id_1_based - 1][field].fetch_add(1, std::memory_order_relaxed);
}

static void maybeFlushMutationStats() {
  uint64_t cur = g_inputs_since_flush.fetch_add(1, std::memory_order_relaxed) + 1;
  if (cur >= g_flush_every) {
    g_inputs_since_flush.store(0, std::memory_order_relaxed);
    flushMutationStats();
  }
}

// Set OUR rv64 features on every function, REPLACING any existing target-features.
// We deliberately do NOT append like backend-tv does: the mutator's
// modify_attributes injects target-features (e.g. rv32-only `zcf`, or `+v`) that
// conflict with rv64 and make codegen report_fatal_error. We're fuzzing isel for a
// fixed rv64 feature set, not fuzzing the feature config.
static void setTargetFeatures(llvm::Module &M) {
  for (llvm::Function &F : M) {
    F.removeFnAttr("target-features");
    F.addFnAttr("target-features", kFeatures);
  }
}

// Make codegen report_fatal_error (e.g. "Cannot select", an unsupported feature)
// non-fatal: skip the input rather than crash the worker. These are NOT the bugs
// we're after — backend-tv finds miscompiles, and a real isel ASSERTION failure
// still abort()s (the handler only intercepts report_fatal_error) and surfaces as
// a Centipede crash. Memory leaked by the skipped longjmp is bounded by Centipede's
// rss_limit (the worker is recycled).
static thread_local std::jmp_buf g_codegen_jmp;
static thread_local bool g_in_codegen = false;

static void codegenFatalHandler(void *, const char *reason, bool) {
  if (g_in_codegen) {
    g_in_codegen = false;
    std::longjmp(g_codegen_jmp, 1);
  }
  fprintf(stderr, "codegen_fuzz_target: fatal outside codegen: %s\n", reason);
  abort();
}

// Run the input through one backend's codegen for coverage (the emitted asm is
// discarded — we only want the instrumented codegen path to execute). Its own
// setjmp guard so a report_fatal_error in one backend doesn't skip the other.
static bool runOneBackend(const llvm::Module &M, llvm::TargetMachine *TM) {
  if (setjmp(g_codegen_jmp)) {     // report_fatal_error fired during codegen below
    g_in_codegen = false;
    return false;
  }
  auto MClone = llvm::CloneModule(M);
  MClone->setTargetTriple(llvm::Triple(kTripleStr));
  MClone->setDataLayout(TM->createDataLayout());
  setTargetFeatures(*MClone);

  llvm::SmallString<8192> AsmBuf;
  llvm::raw_svector_ostream OS(AsmBuf);
  llvm::legacy::PassManager PM;
  g_in_codegen = true;
  bool setup_failed = TM->addPassesToEmitFile(
      PM, OS, /*DwoOut=*/nullptr, llvm::CodeGenFileType::AssemblyFile,
      /*DisableVerify=*/false);
  if (!setup_failed)
    PM.run(*MClone);
  g_in_codegen = false;
  return !setup_failed;
}

// Run the input through both isel pipelines (per CODEGEN_ISEL). Coverage from
// both accumulates, so Centipede steers the corpus toward inputs interesting to
// either backend. Returns true if at least one pipeline ran.
static bool runCodegen(const llvm::Module &M) {
  bool ok = false;
  if (g_run_dag)
    ok |= runOneBackend(M, g_tm_dag);
  if (g_run_gisel)
    ok |= runOneBackend(M, g_tm_gisel);
  return ok;
}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  Ctx = new llvm::LLVMContext();

  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

  // Intercept codegen report_fatal_error (see codegenFatalHandler).
  llvm::install_fatal_error_handler(codegenFatalHandler);

  std::string Err;
  llvm::Triple TT(kTripleStr);
  const llvm::Target *T = llvm::TargetRegistry::lookupTarget(TT, Err);
  if (!T) {
    fprintf(stderr, "codegen_fuzz_target: no riscv64 target: %s\n", Err.c_str());
    abort();
  }
  llvm::TargetOptions Opt;
  Opt.FloatABIType = llvm::FloatABI::Hard;
  auto buildTM = [&](bool gisel) -> llvm::TargetMachine * {
    llvm::TargetMachine *tm = T->createTargetMachine(TT, kCPU, kFeatures, Opt,
                                                     /*RM=*/std::nullopt);
    if (!tm) {
      fprintf(stderr, "codegen_fuzz_target: failed to build TargetMachine\n");
      abort();
    }
    tm->setMachineOutliner(false);
    tm->setSupportsDefaultOutlining(false);
    if (gisel) {
      // Abort disabled so known GISel gaps fall back to SelectionDAG rather
      // than crashing the fuzzer.
      tm->setGlobalISel(true);
      tm->setGlobalISelAbort(llvm::GlobalISelAbortMode::Disable);
    }
    return tm;
  };
  g_tm_dag = buildTM(/*gisel=*/false);
  g_tm_gisel = buildTM(/*gisel=*/true);

  // Which isel pipeline(s) to drive for coverage. Default: both (SDAG + GISel),
  // so one run hunts both backends. CODEGEN_GLOBAL_ISEL=1 is kept as a back-compat
  // alias for CODEGEN_ISEL=gisel.
  if (const char *e = getenv("CODEGEN_ISEL")) {
    std::string mode(e);
    g_run_dag = (mode == "dag" || mode == "both");
    g_run_gisel = (mode == "gisel" || mode == "both");
    if (!g_run_dag && !g_run_gisel) { // unrecognized value -> default both
      g_run_dag = g_run_gisel = true;
    }
  } else if (const char *g = getenv("CODEGEN_GLOBAL_ISEL"); g && *g && *g != '0') {
    g_run_dag = false;
    g_run_gisel = true;
  }
  fprintf(stderr, "codegen_fuzz_target: isel coverage = %s%s%s\n",
          g_run_dag ? "dag" : "", (g_run_dag && g_run_gisel) ? "+" : "",
          g_run_gisel ? "gisel" : "");

  (void)regatoni::MutationRegistry::instance();
  initMutationStats();

  const char *idx_dir_env = getenv("REGATONI_CORPUS_INDEX_DIR");
  std::string idx_dir =
      (idx_dir_env && *idx_dir_env) ? idx_dir_env : "int_func_seeds";
  regatoni::CorpusIndex::instance().build(idx_dir);
  return 0;
}

// ── Input-feature steering (opt-in via REGATONI_IR_FEATURES=1) ──────────────
// Emit one Centipede user feature per distinct IR context-tuple, so the fuzzer
// is rewarded for reaching previously-unseen tuples — the df=0 frontier where
// every confirmed bug lives. Same tuple logic as tools/ir_features.cc (shared
// header) so steering features match the offline analysis exactly.
static constexpr size_t kIRFeatCap = 4096;
__attribute__((used, retain, section("__centipede_extra_features")))
static uint64_t g_ir_extra_features[kIRFeatCap];
static const bool g_emit_ir_features = [] {
  const char *e = std::getenv("REGATONI_IR_FEATURES");
  return e && e[0] == '1';
}();

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  uint32_t mid = g_last_mutation_id;
  g_last_mutation_id = 0;
  maybeFlushMutationStats();

  if (Size == 0)
    return 0;

  llvm::SMDiagnostic Err;
  auto Buf = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(reinterpret_cast<const char *>(Data), Size), "fuzz_input");
  auto M = llvm::parseIR(*Buf, Err, *Ctx);
  if (!M) {
    if (mid != 0) bumpStat(mid, kParseFail);
    return 0;
  }
  if (mid != 0 && llvm::verifyModule(*M, nullptr)) {
    bumpStat(mid, kVerifyFail);
    return 0;
  }

  if (g_emit_ir_features) {
    std::memset(g_ir_extra_features, 0, sizeof(g_ir_extra_features));
    regatoni::ftEmitFeatureHashes(*M, g_ir_extra_features, kIRFeatCap, /*domain=*/0);
  }

  llvm::StripDebugInfo(*M);
  runCodegen(*M);

  if (mid != 0)
    bumpStat(mid, kSuccess);
  return 0;
}

extern "C" size_t LLVMFuzzerCustomMutator(uint8_t *Data, size_t Size,
                                          size_t MaxSize, unsigned int Seed) {
  if (Size == 0) {
    bumpFallback(kFallbackEmptyInput);
    return 0;
  }
  llvm::SMDiagnostic Err;
  auto Buf = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(reinterpret_cast<const char *>(Data), Size), "fuzz_input");
  auto M = llvm::parseIR(*Buf, Err, *Ctx);
  if (!M) {
    bumpFallback(kFallbackParseFail);
    return 0;
  }
  llvm::StripDebugInfo(*M);

  std::mt19937 rng(Seed);
  auto &reg = regatoni::MutationRegistry::instance();
  int selected_idx = -1;
  std::string applied = reg.applyRandom(*M, rng, &selected_idx);

  uint32_t mutation_id =
      (selected_idx >= 0) ? static_cast<uint32_t>(selected_idx) + 1 : 0;
  if (mutation_id != 0) bumpStat(mutation_id, kAttempted);

  if (applied.empty()) {
    bumpFallback(selected_idx < 0 ? kFallbackNoneApplicable
                                  : kFallbackApplyFailed);
    return 0;
  }
  bumpStat(mutation_id, kApplied);

  if (llvm::verifyModule(*M, nullptr)) {
    bumpStat(mutation_id, kVerifyFail);
    bumpFallback(kFallbackVerifyFail);
    return 0;
  }

  std::string Out;
  Out.reserve(Size + applied.size() + 32);
  Out.append("; regatoni-mutation: ");
  Out.append(applied);
  Out.push_back('\n');
  llvm::raw_string_ostream OS(Out);
  M->print(OS, nullptr);
  OS.flush();

  if (Out.size() > MaxSize) {
    bumpStat(mutation_id, kTooLarge);
    bumpFallback(kFallbackTooLarge);
    return 0;
  }

  g_last_mutation_id = mutation_id;
  std::memcpy(Data, Out.data(), Out.size());
  return Out.size();
}

// Native two-parent crossover. Centipede calls this `crossover_level`% of the
// time with TWO evolved corpus members. We graft a value sub-DAG from parent 2
// (donor) into parent 1 (host) — true genetic crossover over the evolving
// population, vs the GraftValue mutation which only grafts from the static seed
// library. Both parents parse into one context, so no bitcode round-trip.
extern "C" size_t LLVMFuzzerCustomCrossOver(const uint8_t *Data1, size_t Size1,
                                            const uint8_t *Data2, size_t Size2,
                                            uint8_t *Out, size_t MaxOutSize,
                                            unsigned int Seed) {
  if (Size1 == 0 || Size2 == 0)
    return 0;
  llvm::SMDiagnostic E1, E2;
  auto HostBuf = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(reinterpret_cast<const char *>(Data1), Size1), "host");
  auto Host = llvm::parseIR(*HostBuf, E1, *Ctx);
  if (!Host)
    return 0;
  auto DonorBuf = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(reinterpret_cast<const char *>(Data2), Size2), "donor");
  auto Donor = llvm::parseIR(*DonorBuf, E2, *Ctx);
  if (!Donor)
    return 0;
  llvm::StripDebugInfo(*Host);
  llvm::StripDebugInfo(*Donor);

  auto firstDefined = [](llvm::Module &M) -> llvm::Function * {
    for (auto &F : M)
      if (!F.isDeclaration() && !F.empty())
        return &F;
    return nullptr;
  };
  llvm::Function *HF = firstDefined(*Host);
  llvm::Function *DF = firstDefined(*Donor);
  if (!HF || !DF)
    return 0;

  std::mt19937 rng(Seed);
  if (!regatoni::graftGadgetInto(*HF, *DF, rng))
    return 0;
  if (llvm::verifyModule(*Host, nullptr))
    return 0;

  std::string Str("; regatoni-crossover: graft\n");
  llvm::raw_string_ostream OS(Str);
  Host->print(OS, nullptr);
  OS.flush();
  if (Str.size() > MaxOutSize)
    return 0;
  std::memcpy(Out, Str.data(), Str.size());
  return Str.size();
}
