// src/mutators/tests/inline_call_test.cc — standalone smoke test for the
// cross-corpus InlineCall splicer.
//
// Build (from project root):
//   source scripts/build/env.sh
//   $CXX -std=c++17 -fno-rtti -I. -I"$LLVM_SRC/llvm/include" -I"$LLVM_BUILD/include" \
//       src/mutators/tests/inline_call_test.cc src/mutators/corpus_index.cc \
//       src/mutators/ir_mutations/inline_call.cc \
//       -Wl,--start-group $LLVM_BUILD/lib/libLLVM*.a -Wl,--end-group \
//       -ldl -lpthread -lm -lz -ltinfo -o /tmp/inline_call_test
//   /tmp/inline_call_test [corpus_dir]   # defaults to split_seeds/
//
// Asserts: builds a CorpusIndex from the corpus; finds a corpus file with
// exactly one spliceable callsite whose signature is indexed; runs
// InlineCall::apply on it; the result passes verifyModule *after* the inline;
// and the spliceable callsite is gone (inlined away).

#include "src/mutators/corpus_index.h"
#include "src/mutators/ir_mutations/inline_call.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <random>

using namespace llvm;

// Count call sites in M to a non-intrinsic, non-self function declaration whose
// signature has a corpus match. Mirrors InlineCall's spliceable predicate.
static size_t spliceableCount(const Module &M) {
  const regatoni::CorpusIndex &idx = regatoni::CorpusIndex::instance();
  size_t n = 0;
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (auto *CB = dyn_cast<CallBase>(&I)) {
          if (CB->isInlineAsm()) continue;
          const Function *callee = CB->getCalledFunction();
          if (!callee || !callee->isDeclaration() || callee->isIntrinsic())
            continue;
          if (callee == CB->getFunction()) continue;
          if (idx.has_matching(callee->getFunctionType())) ++n;
        }
  return n;
}

int main(int argc, char **argv) {
  std::filesystem::path corpus_dir = (argc > 1) ? argv[1] : "split_seeds";
  regatoni::CorpusIndex::instance().build(corpus_dir);

  regatoni::InlineCall mut;
  size_t examined = 0;
  for (auto &entry : std::filesystem::directory_iterator(corpus_dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".ll")
      continue;
    ++examined;
    LLVMContext Ctx;
    SMDiagnostic Err;
    std::unique_ptr<Module> M = parseIRFile(entry.path().string(), Err, Ctx);
    if (!M) continue;
    if (spliceableCount(*M) != 1) continue;  // want a crisp post-condition
    assert(mut.canApply(*M) && "canApply must agree with spliceableCount");

    std::mt19937 rng(0);
    if (!mut.apply(*M, rng)) continue;  // try the next candidate

    assert(!verifyModule(*M, &errs()) && "spliced module must verify");
    assert(spliceableCount(*M) == 0 && "the spliced callsite must be inlined");

    printf("[PASS] InlineCall splice: %s — apply succeeded, module verifies, "
           "callsite inlined (examined %zu files)\n",
           entry.path().filename().string().c_str(), examined);
    return 0;
  }

  fprintf(stderr, "[FAIL] no usable splice candidate found in %s (%zu files)\n",
          corpus_dir.string().c_str(), examined);
  return 1;
}
