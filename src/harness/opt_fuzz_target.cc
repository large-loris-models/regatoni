// opt_fuzz_target.cc — Centipede fuzz target for LLVM opt passes.
//
// LLVMFuzzerTestOneInput: parse bytes as LLVM IR, run opt O2. Crash = bug.
// LLVMFuzzerCustomMutator: parse, apply a random mutation from our registry,
// serialize. Falls back to libFuzzer's mutator on failure.

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

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <random>
#include <string>

static llvm::LLVMContext *Ctx = nullptr;
static std::mt19937 *RNG = nullptr;

static constexpr const char *kDefaultTripleStr = "x86_64-unknown-linux-gnu";

// Cached TM for the default triple; reused across inputs sharing it.
// Centipede runs LLVMFuzzerTestOneInput serially per worker, so single-thread
// reuse is safe.
static llvm::TargetMachine *g_default_tm = nullptr;

// Centipede user-defined features: domain 0, feature id = mutation index + 1.
// Zero is ignored by Centipede, so we reserve it for "no mutation".
static constexpr size_t kNumExtraFeatures = 64;
__attribute__((used, retain, section("__centipede_extra_features")))
static uint64_t regatoni_extra_features[kNumExtraFeatures];

static thread_local uint32_t g_last_mutation_id = 0;

extern "C" size_t LLVMFuzzerMutate(uint8_t *Data, size_t Size, size_t MaxSize);

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

  auto M = llvm::parseIR(*Buf, Err, *Ctx);
  if (!M)
    return LLVMFuzzerMutate(Data, Size, MaxSize);

  llvm::StripDebugInfo(*M);

  std::mt19937 rng(Seed);
  auto &reg = regatoni::MutationRegistry::instance();
  std::string applied = reg.applyRandom(*M, rng);
  if (applied.empty())
    return LLVMFuzzerMutate(Data, Size, MaxSize);

  if (llvm::verifyModule(*M, nullptr))
    return LLVMFuzzerMutate(Data, Size, MaxSize);

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
  M->print(OS, nullptr);
  OS.flush();

  if (Out.size() > MaxSize)
    return LLVMFuzzerMutate(Data, Size, MaxSize);

  std::memcpy(Data, Out.data(), Out.size());
  return Out.size();
}
