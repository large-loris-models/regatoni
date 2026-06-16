// ir_features — input-feature extractor for LLVM IR, via the LLVM API (no regex).
//
// Emits the set of "context tuples" each defined function contains (see
// src/analysis/ir_feature_tuples.h — the shared single source of truth, also used
// by the codegen fuzz target's online steering features). Output:
//   === <path>::<function-name>
//   <tuple>
//   ...
// one block per defined function; tuples unique within a function.

#include "src/analysis/ir_feature_tuples.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

int main(int argc, char **argv) {
  LLVMContext ctx;
  for (int i = 1; i < argc; ++i) {
    SMDiagnostic err;
    std::unique_ptr<Module> M = parseIRFile(argv[i], err, ctx);
    if (!M) {
      errs() << "# parse-failed " << argv[i] << "\n";
      continue;
    }
    for (Function &F : *M) {
      if (F.isDeclaration()) continue;
      outs() << "=== " << argv[i] << "::" << F.getName() << "\n";
      regatoni::ftForEachTuple(F, [&](const std::string &t) {
        outs() << t << "\n";
      });
    }
  }
  return 0;
}
