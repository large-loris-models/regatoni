// opt_fuzz_target.cc — Centipede fuzz target for LLVM opt passes.
//
// LLVMFuzzerTestOneInput: parse bytes as LLVM IR, run opt O2. Crash = bug.
// LLVMFuzzerCustomMutator: parse, apply a random mutation from our registry,
// serialize. Returns 0 on any failure — Centipede's runner treats 0 as "no
// mutation produced" and skips that attempt (third_party/fuzztest/centipede/
// runner.cc:670). The byte-level libFuzzer fallback was disabled
// 2026-05-04 — see docs/decisions/2026-05-04_byte_level_disabled.md. Each
// fallback site is still counted in g_fallback_counters so we can measure
// how often we return 0.

#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"

#include "src/mutators/registry.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unistd.h>

static llvm::LLVMContext *Ctx = nullptr;
static std::mt19937 *RNG = nullptr;

static constexpr const char *kDefaultTripleStr = "x86_64-unknown-linux-gnu";

// Cached TM for the default triple; reused across inputs sharing it.
// Centipede runs LLVMFuzzerTestOneInput serially per worker, so single-thread
// reuse is safe.
static llvm::TargetMachine *g_default_tm = nullptr;

// Centipede user-defined features. Each input that came from a mutator writes
// into slot (mutation_id - 1) so that Centipede attributes the user feature to
// kUserDomains[mutation_id - 1] — one domain per mutator. The lower 32 bits
// carry a per-input hash so distinct inputs from the same mutator count as
// distinct user features in that mutator's domain.
static constexpr size_t kNumExtraFeatures = 64;
__attribute__((used, retain, section("__centipede_extra_features")))
static uint64_t regatoni_extra_features[kNumExtraFeatures];

static thread_local uint32_t g_last_mutation_id = 0;

// ── Per-mutator harness counters (Feed 1) ───────────────────────────────────
// Layout: g_mutation_stats[mutator_index_0_based][stat_field].
// Mutator IDs are 1-indexed at the boundary; we store 0-indexed internally.
namespace {
constexpr size_t kNumMutators = 14;
enum StatField : size_t {
  kAttempted = 0,
  kApplied = 1,
  kParseFail = 2,
  kVerifyFail = 3,
  kTooLarge = 4,
  kSuccess = 5,
  kNumStatFields = 6,
};
}  // namespace
static std::atomic<uint64_t> g_mutation_stats[kNumMutators][kNumStatFields];
static std::atomic<uint64_t> g_inputs_since_flush{0};
static uint64_t g_flush_every = 1000;
static std::string g_stats_path;

// ── Fallback-site counters ──────────────────────────────────────────────────
// Aggregate (not per-mutator). Increments every time LLVMFuzzerCustomMutator
// returns 0 ("no mutation produced"). Sites split naturally on the
// (selected_idx, applied) values returned by registry::applyRandom.
namespace {
enum FallbackSite : size_t {
  kFallbackEmptyInput = 0,
  kFallbackParseFail = 1,
  kFallbackNoneApplicable = 2,  // applyRandom: applicable set was empty
  kFallbackApplyFailed = 3,     // applyRandom: chosen apply() returned false
  kFallbackVerifyFail = 4,
  kFallbackTooLarge = 5,
  kFallbackSiteCount = 6,
};
}  // namespace
static std::atomic<uint64_t> g_fallback_counters[kFallbackSiteCount];
static const char *kFallbackSiteNames[kFallbackSiteCount] = {
    "fallback_empty_input",     "fallback_parse_fail",
    "fallback_none_applicable", "fallback_apply_failed",
    "fallback_verify_fail",     "fallback_too_large",
};
static std::string g_fallback_stats_path;

static void bumpFallback(FallbackSite site) {
  g_fallback_counters[site].fetch_add(1, std::memory_order_relaxed);
}

static uint32_t fnv1a32(const uint8_t *data, size_t size) {
  uint32_t h = 0x811C9DC5u;
  for (size_t i = 0; i < size; ++i) {
    h ^= data[i];
    h *= 0x01000193u;
  }
  return h;
}

static uint64_t now_unix_micros() {
  using namespace std::chrono;
  return duration_cast<microseconds>(
             system_clock::now().time_since_epoch())
      .count();
}

static void initMutationStats() {
  const char *workdir = getenv("REGATONI_WORKDIR");
  std::string base;
  if (workdir && *workdir) {
    base = workdir;
  } else {
    fprintf(stderr,
            "REGATONI_WORKDIR not set; writing mutation stats CSV in cwd\n");
    base = ".";
  }
  std::ostringstream os;
  os << base << "/regatoni-mutation-stats." << getpid() << ".csv";
  g_stats_path = os.str();

  if (FILE *f = fopen(g_stats_path.c_str(), "w")) {
    fprintf(f,
            "unix_micros,mutator_id,mutator_name,attempted,applied,"
            "parse_fail,verify_fail,too_large,success\n");
    fclose(f);
  } else {
    fprintf(stderr, "Failed to open mutation stats file: %s\n",
            g_stats_path.c_str());
    g_stats_path.clear();
  }

  std::ostringstream fos;
  fos << base << "/regatoni-fallback-stats." << getpid() << ".csv";
  g_fallback_stats_path = fos.str();
  if (FILE *f = fopen(g_fallback_stats_path.c_str(), "w")) {
    fprintf(f, "unix_micros,site_id,site_name,count\n");
    fclose(f);
  } else {
    fprintf(stderr, "Failed to open fallback stats file: %s\n",
            g_fallback_stats_path.c_str());
    g_fallback_stats_path.clear();
  }

  if (const char *e = getenv("REGATONI_MUTATION_FLUSH_EVERY")) {
    char *end = nullptr;
    unsigned long v = strtoul(e, &end, 10);
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
                (unsigned long long)g_mutation_stats[i][kAttempted].load(
                    std::memory_order_relaxed),
                (unsigned long long)g_mutation_stats[i][kApplied].load(
                    std::memory_order_relaxed),
                (unsigned long long)g_mutation_stats[i][kParseFail].load(
                    std::memory_order_relaxed),
                (unsigned long long)g_mutation_stats[i][kVerifyFail].load(
                    std::memory_order_relaxed),
                (unsigned long long)g_mutation_stats[i][kTooLarge].load(
                    std::memory_order_relaxed),
                (unsigned long long)g_mutation_stats[i][kSuccess].load(
                    std::memory_order_relaxed));
      }
      fclose(f);
    }
  }
  if (!g_fallback_stats_path.empty()) {
    if (FILE *f = fopen(g_fallback_stats_path.c_str(), "a")) {
      for (size_t i = 0; i < kFallbackSiteCount; ++i) {
        fprintf(f, "%llu,%zu,%s,%llu\n", (unsigned long long)ts, i,
                kFallbackSiteNames[i],
                (unsigned long long)g_fallback_counters[i].load(
                    std::memory_order_relaxed));
      }
      fclose(f);
    }
  }
}

static void bumpStat(uint32_t mutation_id_1_based, StatField field) {
  if (mutation_id_1_based == 0 || mutation_id_1_based > kNumMutators) return;
  g_mutation_stats[mutation_id_1_based - 1][field].fetch_add(
      1, std::memory_order_relaxed);
}

static void maybeFlushMutationStats() {
  uint64_t cur = g_inputs_since_flush.fetch_add(1, std::memory_order_relaxed) + 1;
  if (cur >= g_flush_every) {
    g_inputs_since_flush.store(0, std::memory_order_relaxed);
    flushMutationStats();
  }
}

static std::unique_ptr<llvm::TargetMachine>
buildTargetMachine(const llvm::Triple &T) {
  std::string Err;
  const llvm::Target *TheTarget = llvm::TargetRegistry::lookupTarget(T, Err);
  if (!TheTarget)
    return nullptr;
  llvm::TargetOptions Opts;
  return std::unique_ptr<llvm::TargetMachine>(TheTarget->createTargetMachine(
      T, /*CPU=*/"", /*Features=*/"", Opts,
      /*RM=*/std::nullopt, /*CM=*/std::nullopt,
      llvm::CodeGenOptLevel::Default));
}

// Returns nullptr for triple-less modules or unregistered triples; PassBuilder
// then falls back to generic TTI. Per-triple TMs other than the default are
// leaked into a process-wide pool (the set of distinct triples is tiny).
static llvm::TargetMachine *resolveTargetMachine(llvm::Module &M) {
  const llvm::Triple &MT = M.getTargetTriple();
  if (MT.empty())
    return nullptr;
  if (g_default_tm && MT.str() == g_default_tm->getTargetTriple().str())
    return g_default_tm;
  std::string Err;
  const llvm::Target *TheTarget = llvm::TargetRegistry::lookupTarget(MT, Err);
  if (!TheTarget)
    return nullptr;
  llvm::TargetOptions Opts;
  std::unique_ptr<llvm::TargetMachine> TM(TheTarget->createTargetMachine(
      MT, /*CPU=*/"", /*Features=*/"", Opts,
      /*RM=*/std::nullopt, /*CM=*/std::nullopt,
      llvm::CodeGenOptLevel::Default));
  if (TM)
    return TM.release();
  return nullptr;
}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  Ctx = new llvm::LLVMContext();
  RNG = new std::mt19937(0xC0FFEE);

  // Without these, PassBuilder gets a null TM and every pass sees generic
  // TTI (NoTTIImpl, 32-bit vector register width).
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

  llvm::Triple DefaultT(kDefaultTripleStr);
  auto TM = buildTargetMachine(DefaultT);
  if (TM) {
    g_default_tm = TM.release();
  } else {
    fprintf(stderr,
            "LLVMFuzzerInitialize: failed to build default TargetMachine for "
            "%s — falling back to generic TTI\n",
            kDefaultTripleStr);
  }

  (void)regatoni::MutationRegistry::instance();
  initMutationStats();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  uint32_t mid = g_last_mutation_id;
  g_last_mutation_id = 0;
  // Flush cadence is "every N inputs" — count *all* inputs, mutated or not.
  maybeFlushMutationStats();

  if (Size == 0)
    return 0;

  llvm::SMDiagnostic Err;
  auto Buf = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(reinterpret_cast<const char *>(Data), Size),
      "fuzz_input");

  auto M = llvm::parseIR(*Buf, Err, *Ctx);
  if (!M) {
    if (mid != 0) bumpStat(mid, kParseFail);
    return 0;
  }

  if (mid != 0 && llvm::verifyModule(*M, nullptr)) {
    bumpStat(mid, kVerifyFail);
    return 0;
  }

  llvm::StripDebugInfo(*M);

  llvm::TargetMachine *TM = resolveTargetMachine(*M);

  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;

  // Match `opt -O2`: its default<O2> alias parser flips Loop/SLPVectorization
  // via setupOptionsForPipelineAlias. We bypass that by calling
  // buildPerModuleDefaultPipeline directly, so set them explicitly.
  llvm::PipelineTuningOptions PTO;
  PTO.LoopVectorization = true;
  PTO.SLPVectorization = true;
  llvm::PassBuilder PB(TM, PTO);
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  llvm::ModulePassManager MPM =
      PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);

  MPM.run(*M, MAM);

  // Per-mutator user-feature: high 32 bits = domain id (mid - 1), low 32 bits
  // = per-input hash so distinct inputs from the same mutator are distinct
  // user features inside that mutator's domain.
  if (mid != 0) {
    uint32_t slot = mid - 1;
    if (slot < kNumExtraFeatures) {
      regatoni_extra_features[slot] =
          (uint64_t{slot} << 32) | uint64_t{fnv1a32(Data, Size)};
    }
    bumpStat(mid, kSuccess);
  }
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
      llvm::StringRef(reinterpret_cast<const char *>(Data), Size),
      "fuzz_input");

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

  // Attribute the attempt to the selected mutator (if any was applicable),
  // even when apply() itself returned false.
  uint32_t mutation_id =
      (selected_idx >= 0) ? static_cast<uint32_t>(selected_idx) + 1 : 0;
  if (mutation_id != 0) bumpStat(mutation_id, kAttempted);

  if (applied.empty()) {
    // selected_idx == -1 ⇒ no mutator's canApply() returned true.
    // selected_idx >=  0 ⇒ chosen mutator's apply() returned false.
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
