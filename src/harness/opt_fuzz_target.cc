// opt_fuzz_target.cc — Centipede fuzz target for LLVM opt passes
//
// - LLVMFuzzerTestOneInput: parse bytes as LLVM IR, run opt O2. Crash = bug.
// - LLVMFuzzerCustomMutator: parse bytes as IR, apply a random mutation from
//   our registry, serialize back. Falls back to libFuzzer's mutator on failure.

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include "src/mutators/registry.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <random>
#include <string>

static llvm::LLVMContext *Ctx = nullptr;
static std::mt19937 *RNG = nullptr;

// Centipede user-defined features: domain 0, feature id = mutation index + 1.
// Zero entries are ignored by Centipede, so we reserve 0 for "no mutation".
static constexpr size_t kNumExtraFeatures = 64;
__attribute__((used, retain, section("__centipede_extra_features")))
static uint64_t regatoni_extra_features[kNumExtraFeatures];

static thread_local uint32_t g_last_mutation_id = 0;

extern "C" size_t LLVMFuzzerMutate(uint8_t *Data, size_t Size, size_t MaxSize);

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  Ctx = new llvm::LLVMContext();
  RNG = new std::mt19937(0xC0FFEE);
  (void)regatoni::MutationRegistry::instance();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size == 0)
    return 0;

  llvm::SMDiagnostic Err;
  auto Buf = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(reinterpret_cast<const char *>(Data), Size),
      "fuzz_input");

  auto M = llvm::parseIR(*Buf, Err, *Ctx);
  if (!M)
    return 0;

  llvm::StripDebugInfo(*M);

  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;

  llvm::PassBuilder PB;
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  llvm::ModulePassManager MPM =
      PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);

  MPM.run(*M, MAM);

  // Emit mutation identity as a Centipede user feature (domain 0).
  // Upper 32 bits = domain id (0); lower 32 bits = mutation index + 1.
  if (g_last_mutation_id != 0) {
    regatoni_extra_features[0] =
        (uint64_t{0} << 32) | uint64_t{g_last_mutation_id};
    g_last_mutation_id = 0;
  }
  return 0;
}

extern "C" size_t LLVMFuzzerCustomMutator(uint8_t *Data, size_t Size,
                                          size_t MaxSize, unsigned int Seed) {
  if (Size == 0)
    return LLVMFuzzerMutate(Data, Size, MaxSize);

  llvm::SMDiagnostic Err;
  auto Buf = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(reinterpret_cast<const char *>(Data), Size),
      "fuzz_input");

  using Clock = std::chrono::steady_clock;
  auto t_parse_start = Clock::now();
  auto M = llvm::parseIR(*Buf, Err, *Ctx);
  auto t_parse_end = Clock::now();
  if (!M)
    return LLVMFuzzerMutate(Data, Size, MaxSize);

  llvm::StripDebugInfo(*M);

  std::mt19937 rng(Seed);
  auto &reg = regatoni::MutationRegistry::instance();
  fprintf(stderr, "CustomMutator: invoking applyRandom (Size=%zu, Seed=%u)\n",
          Size, Seed);
  fflush(stderr);
  auto t_mut_start = Clock::now();
  std::string applied = reg.applyRandom(*M, rng);
  auto t_mut_end = Clock::now();
  fprintf(stderr, "CustomMutator: applied mutation: '%s'\n", applied.c_str());
  fflush(stderr);
  if (applied.empty())
    return LLVMFuzzerMutate(Data, Size, MaxSize);

  if (llvm::verifyModule(*M, nullptr))
    return LLVMFuzzerMutate(Data, Size, MaxSize);

  // Record which mutation ran, so LLVMFuzzerTestOneInput can emit it as a
  // user feature. Index is position in the registry; +1 so that 0 (ignored
  // by Centipede) means "no mutation recorded".
  uint32_t mutation_id = 0;
  const auto &all = reg.all();
  for (size_t i = 0; i < all.size(); ++i) {
    if (all[i]->name() == applied) {
      mutation_id = static_cast<uint32_t>(i) + 1;
      break;
    }
  }
  g_last_mutation_id = mutation_id;

  std::string Out;
  Out.reserve(Size + applied.size() + 32);
  Out.append("; regatoni-mutation: ");
  Out.append(applied);
  Out.push_back('\n');
  llvm::raw_string_ostream OS(Out);
  auto t_ser_start = Clock::now();
  M->print(OS, nullptr);
  OS.flush();
  auto t_ser_end = Clock::now();

  auto to_sec = [](Clock::duration d) {
    return std::chrono::duration<double>(d).count();
  };
  double parse_s = to_sec(t_parse_end - t_parse_start);
  double mut_s = to_sec(t_mut_end - t_mut_start);
  double ser_s = to_sec(t_ser_end - t_ser_start);
  if (parse_s > 2.0 || mut_s > 2.0 || ser_s > 2.0) {
    if (FILE *f = std::fopen("build/slow_mutations.log", "a")) {
      std::time_t now = std::time(nullptr);
      char ts[32];
      std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S",
                    std::gmtime(&now));
      std::fprintf(f,
                   "%s mutation=%s size=%zu parse=%.3fs mutate=%.3fs "
                   "serialize=%.3fs hex=",
                   ts, applied.c_str(), Size, parse_s, mut_s, ser_s);
      size_t n = Size < 200 ? Size : 200;
      for (size_t i = 0; i < n; ++i)
        std::fprintf(f, "%02x", Data[i]);
      std::fputc('\n', f);
      std::fclose(f);
    }
  }

  if (Out.size() > MaxSize)
    return LLVMFuzzerMutate(Data, Size, MaxSize);

  std::memcpy(Data, Out.data(), Out.size());
  return Out.size();
}
