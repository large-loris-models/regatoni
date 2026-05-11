// src/mutators/ir_mutations/inline_call.cc
#include "src/mutators/ir_mutations/inline_call.h"
#include "src/mutators/corpus_index.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <random>
#include <vector>

namespace regatoni {

// A direct call whose callee is a non-intrinsic, non-self function declaration.
// These are the splice candidates: the callee has no body in M, and we can
// graft one in from the corpus library if a signature-matching function exists.
static const llvm::Function *spliceableCallee(const llvm::CallBase &CB) {
  if (CB.isInlineAsm())
    return nullptr;
  const llvm::Function *callee = CB.getCalledFunction();
  if (!callee)
    return nullptr;
  if (!callee->isDeclaration() || callee->isIntrinsic())
    return nullptr;
  if (callee == CB.getFunction())
    return nullptr;
  return callee;
}

bool InlineCall::canApply(const llvm::Module &M) const {
  const CorpusIndex &idx = CorpusIndex::instance();
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (auto *CB = llvm::dyn_cast<llvm::CallBase>(&I))
          if (const llvm::Function *callee = spliceableCallee(*CB))
            if (idx.has_matching(callee->getFunctionType()))
              return true;
  return false;
}

bool InlineCall::apply(llvm::Module &M, std::mt19937 &rng) {
  const CorpusIndex &idx = CorpusIndex::instance();

  std::vector<llvm::CallBase *> sites;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (auto *CB = llvm::dyn_cast<llvm::CallBase>(&I))
          if (const llvm::Function *callee = spliceableCallee(*CB))
            if (idx.has_matching(callee->getFunctionType()))
              sites.push_back(CB);
  if (sites.empty())
    return false;

  llvm::CallBase *CB = sites[std::uniform_int_distribution<size_t>(
      0, sites.size() - 1)(rng)];
  llvm::Function *Callee = CB->getCalledFunction();

  llvm::Function *LibF = idx.sample_matching(Callee->getFunctionType(), rng);
  if (!LibF)
    return false;

  // The library function lives in CorpusIndex's private LLVMContext.
  // CloneFunctionInto can't cross contexts, so round-trip the library module
  // through bitcode to obtain a copy in M's context, then graft its body in.
  llvm::SmallString<4096> BC;
  {
    llvm::raw_svector_ostream OS(BC);
    llvm::WriteBitcodeToFile(*LibF->getParent(), OS);
  }
  auto BCBuf = llvm::MemoryBuffer::getMemBuffer(
      llvm::StringRef(BC.data(), BC.size()), "corpus_lib",
      /*RequiresNullTerminator=*/false);
  auto TmpMOrErr =
      llvm::parseBitcodeFile(BCBuf->getMemBufferRef(), M.getContext());
  if (!TmpMOrErr) {
    llvm::consumeError(TmpMOrErr.takeError());
    return false;
  }
  std::unique_ptr<llvm::Module> TmpM = std::move(*TmpMOrErr);
  llvm::Function *TmpF = TmpM->getFunction(LibF->getName());
  if (!TmpF || TmpF->isDeclaration())
    return false;
  // Personality / prefix / prologue can reference module-scope globals that the
  // self-contained-body filter doesn't see; cloning those would leave Callee
  // pointing into TmpM after it's destroyed. Skip such functions.
  if (TmpF->hasPersonalityFn() || TmpF->hasPrefixData() ||
      TmpF->hasPrologueData())
    return false;
  // The harness strips debug info from incoming modules; do the same here so
  // CloneFunctionInto(DifferentModule) doesn't splice the corpus function's
  // DICompileUnits into M's !llvm.dbg.cu (which would risk verifier failures).
  llvm::StripDebugInfo(*TmpM);

  // Map the library function's arguments onto Callee's, and the function itself
  // onto Callee so recursive self-calls in the body get rewired.
  llvm::ValueToValueMapTy VMap;
  VMap[TmpF] = Callee;
  auto DestArg = Callee->arg_begin();
  for (llvm::Argument &SrcArg : TmpF->args()) {
    DestArg->setName(SrcArg.getName());
    VMap[&SrcArg] = &*DestArg++;
  }
  llvm::SmallVector<llvm::ReturnInst *, 8> Returns;
  llvm::CloneFunctionInto(Callee, TmpF, VMap,
                          llvm::CloneFunctionChangeType::DifferentModule,
                          Returns);
  // Internal linkage so the inliner is allowed to fold the body away.
  Callee->setLinkage(llvm::GlobalValue::InternalLinkage);

  llvm::InlineFunctionInfo IFI;
  auto Res = llvm::InlineFunction(*CB, IFI);
  return Res.isSuccess();
}

} // namespace regatoni
