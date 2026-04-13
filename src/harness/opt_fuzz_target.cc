// opt_fuzz_target.cc — Minimal Centipede fuzz target for LLVM opt passes
//
// Takes raw bytes from the fuzzer, tries to parse as LLVM IR,
// runs opt passes if valid. Crashes in opt = bugs found for free.

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <cstddef>
#include <cstdint>
#include <string>

static llvm::LLVMContext *Ctx = nullptr;

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  Ctx = new llvm::LLVMContext();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size == 0)
    return 0;

  // Parse bytes as LLVM IR
  llvm::SMDiagnostic Err;
  auto Buf = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(reinterpret_cast<const char *>(Data), Size),
      "fuzz_input");

  auto M = llvm::parseIR(*Buf, Err, *Ctx);
  if (!M)
    return 0; // Not valid IR — discard

  // Run the default O2 pipeline
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

  // This is where crashes happen — opt bugs
  MPM.run(*M, MAM);

  return 0;
}