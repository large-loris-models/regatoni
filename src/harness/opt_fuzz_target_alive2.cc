// opt_fuzz_target_alive2.cc — Alive2 verification harness.
//
// Parses IR, clones the module, runs opt O2 on the clone, then uses Alive2's
// Verifier::compareFunctions on (original, optimized). If any function is
// unsound (num_correct == 0), abort() so Centipede records the input.
//
// Linked against ASAN LLVM + Alive2 libs; NOT sancov instrumented and NOT
// linked against the Centipede runner (Centipede calls this as an
// --extra_binaries target in the background).

#include "llvm_util/compare.h"
#include "llvm_util/llvm2alive.h"
#include "llvm_util/llvm_optimizer.h"
#include "llvm_util/utils.h"
#include "smt/smt.h"
#include "util/config.h"

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using llvm_util::Verifier;

static llvm::LLVMContext *Ctx = nullptr;
static std::stringstream *LogStream = nullptr;
static std::optional<llvm::TargetLibraryInfoWrapperPass> TLI;
static std::optional<smt::smt_initializer> SmtInit;
static std::optional<Verifier> Verif;
static std::optional<llvm_util::initializer> LlvmUtilInit;
static bool VerifierReady = false;

static void ensureVerifier(const llvm::Module &M) {
  if (VerifierReady)
    return;
  if (!LlvmUtilInit.has_value())
    LlvmUtilInit.emplace(*LogStream, M.getDataLayout());
  llvm::Triple TT(M.getTargetTriple());
  TLI.emplace(TT);
  util::config::disable_undef_input = true;
  smt::set_query_timeout("5000");
  SmtInit.emplace();
  Verif.emplace(TLI.value(), SmtInit.value(), *LogStream);
  VerifierReady = true;
}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  Ctx = new llvm::LLVMContext();
  LogStream = new std::stringstream();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size == 0)
    return 0;

  llvm::SMDiagnostic Err;
  auto Buf = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(reinterpret_cast<const char *>(Data), Size),
      "fuzz_input");

  auto M1 = llvm::parseIR(*Buf, Err, *Ctx);
  if (!M1)
    return 0;
  if (llvm::verifyModule(*M1, nullptr))
    return 0;

  std::unique_ptr<llvm::Module> M2 = llvm::CloneModule(*M1);
  if (!llvm_util::optimize_module(*M2, "O2").empty())
    return 0;

  ensureVerifier(*M1);

  for (llvm::Function &F1 : *M1) {
    if (F1.isDeclaration())
      continue;
    llvm::Function *F2 = M2->getFunction(F1.getName());
    if (!F2 || F2->isDeclaration())
      continue;

    Verif->num_correct = 0;
    Verif->num_errors = 0;
    Verif->num_unsound = 0;
    Verif->num_failed = 0;
    Verif->compareFunctions(F1, *F2);

    // Only a definitive unsoundness result is a bug. Timeouts (num_failed) and
    // translation/Alive2 errors (num_errors) are skipped — continue to next fn.
    if (Verif->num_unsound > 0) {
      llvm::errs() << "ALIVE2 MISCOMPILE in function " << F1.getName() << "\n"
                   << LogStream->str() << "\n";
      std::abort();
    }
    LogStream->str("");
  }

  return 0;
}

static std::vector<uint8_t> readAll(std::istream &in) {
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int main(int argc, char **argv) {
  LLVMFuzzerInitialize(&argc, &argv);

  if (argc < 2) {
    auto bytes = readAll(std::cin);
    LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
    return 0;
  }

  for (int i = 1; i < argc; ++i) {
    std::ifstream f(argv[i], std::ios::binary);
    if (!f) {
      llvm::errs() << "failed to open " << argv[i] << "\n";
      return 1;
    }
    auto bytes = readAll(f);
    LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
  }
  return 0;
}
