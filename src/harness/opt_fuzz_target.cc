// opt_fuzz_target.cc — Centipede fuzz target for LLVM opt passes
//
// - LLVMFuzzerTestOneInput: parse bytes as LLVM IR, run opt O2. Crash = bug.
// - LLVMFuzzerCustomMutator: parse bytes as IR, apply a random mutation from
//   our registry, serialize back. Falls back to libFuzzer's mutator on failure.

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include "src/mutators/registry.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>

static llvm::LLVMContext *Ctx = nullptr;
static std::mt19937 *RNG = nullptr;

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

  std::mt19937 rng(Seed);
  auto &reg = regatoni::MutationRegistry::instance();
  std::string applied = reg.applyRandom(*M, rng);
  if (applied.empty())
    return LLVMFuzzerMutate(Data, Size, MaxSize);

  if (llvm::verifyModule(*M, nullptr))
    return LLVMFuzzerMutate(Data, Size, MaxSize);

  std::string Out;
  llvm::raw_string_ostream OS(Out);
  M->print(OS, nullptr);
  OS.flush();

  if (Out.size() > MaxSize)
    return LLVMFuzzerMutate(Data, Size, MaxSize);

  std::memcpy(Data, Out.data(), Out.size());
  return Out.size();
}
