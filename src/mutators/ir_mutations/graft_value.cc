// src/mutators/ir_mutations/graft_value.cc — gadget-graft crossover.
#include "src/mutators/ir_mutations/graft_value.h"
#include "src/mutators/corpus_index.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBuffer.h"

#include <random>
#include <set>
#include <string>
#include <vector>

namespace regatoni {

// Integer-result intrinsics safe to transplant: bit-manip / min-max / saturating
// arithmetic. All take integer args (ctlz/cttz/abs have an i1 immarg, also int);
// .with.overflow is excluded since it returns an aggregate (caught by the int
// result check). Broadening to these lets crossover recombine the rich gadgets
// the mutations synthesize — the cap the 10-min probe identified.
static bool isGraftableIntrinsic(llvm::Intrinsic::ID id) {
  switch (id) {
  case llvm::Intrinsic::ctpop: case llvm::Intrinsic::ctlz:
  case llvm::Intrinsic::cttz:  case llvm::Intrinsic::bitreverse:
  case llvm::Intrinsic::bswap: case llvm::Intrinsic::fshl:
  case llvm::Intrinsic::fshr:  case llvm::Intrinsic::abs:
  case llvm::Intrinsic::smax:  case llvm::Intrinsic::smin:
  case llvm::Intrinsic::umax:  case llvm::Intrinsic::umin:
  case llvm::Intrinsic::uadd_sat: case llvm::Intrinsic::usub_sat:
  case llvm::Intrinsic::sadd_sat: case llvm::Intrinsic::ssub_sat:
  case llvm::Intrinsic::ushl_sat: case llvm::Intrinsic::sshl_sat:
    return true;
  default:
    return false;
  }
}

// Pure scalar-integer instruction we can safely clone into a host block:
// integer result, all-integer operands (args, for intrinsics), from the
// arithmetic/cast/freeze subset + whitelisted intrinsics. No memory, no general
// calls, no ptr/fp/vector/phi/control-flow.
static bool isGraftable(const llvm::Instruction &I) {
  if (!I.getType()->isIntegerTy())
    return false;
  if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&I)) {
    if (!isGraftableIntrinsic(II->getIntrinsicID()))
      return false;
    for (const llvm::Use &A : II->args())
      if (!A.get()->getType()->isIntegerTy())
        return false;
    return true;
  }
  switch (I.getOpcode()) {
  case llvm::Instruction::Add:  case llvm::Instruction::Sub:
  case llvm::Instruction::Mul:  case llvm::Instruction::UDiv:
  case llvm::Instruction::SDiv: case llvm::Instruction::URem:
  case llvm::Instruction::SRem: case llvm::Instruction::Shl:
  case llvm::Instruction::LShr: case llvm::Instruction::AShr:
  case llvm::Instruction::And:  case llvm::Instruction::Or:
  case llvm::Instruction::Xor:  case llvm::Instruction::ICmp:
  case llvm::Instruction::Select: case llvm::Instruction::Trunc:
  case llvm::Instruction::ZExt: case llvm::Instruction::SExt:
  case llvm::Instruction::Freeze:
    break;
  default:
    return false;
  }
  for (const llvm::Value *Op : I.operands())
    if (!Op->getType()->isIntegerTy())
      return false;
  return true;
}

// A constant we can clone verbatim (no global refs / ConstantExpr that might
// dangle once the donor module is freed).
static bool isSafeConstOperand(const llvm::Value *V) {
  return llvm::isa<llvm::ConstantInt>(V) || llvm::isa<llvm::UndefValue>(V) ||
         llvm::isa<llvm::ConstantAggregateZero>(V);
}

// Union the donor module's lineage tags into the host module's, so a graft
// offspring records BOTH parents' rule-of-origin tags — the recombination the
// litmus-test thesis is about. Lineage lives in module named metadata
// `!regatoni.lineage` (a list of one-string MDNodes), which survives
// parse/mutate/print because mutators never touch module-level named metadata.
static void mergeLineageInto(llvm::Module &Host, llvm::Module &Donor) {
  llvm::NamedMDNode *DN = Donor.getNamedMetadata("regatoni.lineage");
  if (!DN || DN->getNumOperands() == 0)
    return;
  llvm::LLVMContext &C = Host.getContext();
  llvm::NamedMDNode *HN = Host.getOrInsertNamedMetadata("regatoni.lineage");
  std::set<std::string> seen;
  for (const llvm::MDNode *Op : HN->operands())
    if (Op->getNumOperands() == 1)
      if (auto *S = llvm::dyn_cast<llvm::MDString>(Op->getOperand(0)))
        seen.insert(S->getString().str());
  for (const llvm::MDNode *Op : DN->operands())
    if (Op->getNumOperands() == 1)
      if (auto *S = llvm::dyn_cast<llvm::MDString>(Op->getOperand(0)))
        if (seen.insert(S->getString().str()).second)
          HN->addOperand(
              llvm::MDNode::get(C, llvm::MDString::get(C, S->getString())));
}

// ── shared graft core (Host and Donor in the SAME context) ──────────────────
bool graftGadgetInto(llvm::Function &Host, llvm::Function &Donor,
                     std::mt19937 &rng) {
  if (Host.isDeclaration() || Host.empty() || Donor.isDeclaration() ||
      Donor.empty())
    return false;
  llvm::Function *H = &Host;
  llvm::Function *DonorF = &Donor;
  llvm::BasicBlock &Entry = H->getEntryBlock();
  llvm::BasicBlock &DonorEntry = DonorF->getEntryBlock();

  // host integer values available at the front of Entry = the function args
  std::vector<llvm::Value *> hostInts;
  for (auto &A : H->args())
    if (A.getType()->isIntegerTy())
      hostInts.push_back(&A);

  // ── map donor int args -> a host int value of matching width, else const ─
  llvm::DenseMap<llvm::Value *, llvm::Value *> vmap;
  for (auto &DA : DonorF->args()) {
    if (!DA.getType()->isIntegerTy())
      continue; // non-int arg: leave unmapped; its (skipped) users won't clone
    llvm::Value *repl = nullptr;
    for (llvm::Value *hv : hostInts)
      if (hv->getType() == DA.getType()) { repl = hv; break; }
    if (!repl) {
      // Build at 64 then resize: ConstantInt::get(iN, v) asserts if v >= 2^N.
      uint64_t cv = std::uniform_int_distribution<uint64_t>(0, 7)(rng);
      unsigned bw = DA.getType()->getIntegerBitWidth();
      repl = llvm::ConstantInt::get(DA.getType(),
                                    llvm::APInt(64, cv).zextOrTrunc(bw));
    }
    vmap[&DA] = repl;
  }

  // ── clone the maximal graftable prefix-closure of the donor entry block ──
  llvm::IRBuilder<> B(&*Entry.getFirstInsertionPt());
  std::vector<llvm::Value *> clones;
  llvm::SmallPtrSet<llvm::Instruction *, 32> cloneSet;
  for (llvm::Instruction &DI : DonorEntry) {
    if (DI.isTerminator())
      break;
    if (!isGraftable(DI))
      continue;
    auto mappableOp = [&](llvm::Value *Op) {
      if (llvm::isa<llvm::Argument>(Op) || llvm::isa<llvm::Instruction>(Op))
        return vmap.count(Op) != 0;
      return isSafeConstOperand(Op);
    };
    bool mappable = true;
    if (auto *CB = llvm::dyn_cast<llvm::CallBase>(&DI)) {
      // Intrinsic call: only the *args* need mapping; the callee is re-pointed
      // at a host-module declaration below.
      for (llvm::Use &A : CB->args())
        if (!mappableOp(A.get())) { mappable = false; break; }
    } else {
      for (llvm::Value *Op : DI.operands())
        if (!mappableOp(Op)) { mappable = false; break; }
    }
    if (!mappable)
      continue;
    llvm::Instruction *NI = DI.clone();
    for (unsigned i = 0; i < NI->getNumOperands(); ++i) {
      auto it = vmap.find(NI->getOperand(i));
      if (it != vmap.end())
        NI->setOperand(i, it->second);
    }
    // A cloned intrinsic call still points at the *donor* module's declaration;
    // re-point it at a host-module declaration (created if absent) so the graft
    // doesn't reference a foreign / about-to-be-freed module.
    if (auto *CB = llvm::dyn_cast<llvm::CallBase>(NI)) {
      if (llvm::Function *Callee = CB->getCalledFunction()) {
        llvm::Module *HM = H->getParent();
        llvm::FunctionCallee HC = HM->getOrInsertFunction(
            Callee->getName(), Callee->getFunctionType());
        CB->setCalledFunction(llvm::cast<llvm::Function>(HC.getCallee()));
      }
    }
    NI->setName("");
    // Drop metadata (range/noundef etc.) — may be unsound after remapping.
    llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 4> MDs;
    NI->getAllMetadata(MDs);
    for (auto &md : MDs)
      NI->setMetadata(md.first, nullptr);
    B.Insert(NI);
    vmap[&DI] = NI;
    clones.push_back(NI);
    cloneSet.insert(NI);
  }
  if (clones.empty())
    return false;

  // ── pick a grafted value and feed it into a host instruction operand ─────
  llvm::Value *V = clones[std::uniform_int_distribution<size_t>(
      0, clones.size() - 1)(rng)];

  std::vector<std::pair<llvm::Instruction *, unsigned>> sites;
  for (llvm::BasicBlock &BB : *H)
    for (llvm::Instruction &I : BB) {
      if (cloneSet.count(&I) || llvm::isa<llvm::PHINode>(I))
        continue;
      for (unsigned i = 0; i < I.getNumOperands(); ++i) {
        llvm::Value *Op = I.getOperand(i);
        if (Op->getType()->isIntegerTy() && Op != V)
          sites.push_back({&I, i});
      }
    }
  if (sites.empty())
    return false;

  auto [Consumer, OpIdx] = sites[std::uniform_int_distribution<size_t>(
      0, sites.size() - 1)(rng)];
  llvm::Type *Want = Consumer->getOperand(OpIdx)->getType();
  llvm::Value *Adapted = V;
  if (V->getType() != Want) {
    llvm::IRBuilder<> CB(Consumer);
    unsigned vw = V->getType()->getIntegerBitWidth();
    unsigned ww = Want->getIntegerBitWidth();
    if (vw > ww)
      Adapted = CB.CreateTrunc(V, Want);
    else
      Adapted = (rng() & 1) ? CB.CreateSExt(V, Want) : CB.CreateZExt(V, Want);
  }
  Consumer->setOperand(OpIdx, Adapted);
  mergeLineageInto(*H->getParent(), *DonorF->getParent());
  return true;
}

// ── GraftValue mutation: donor sampled from the static seed library ─────────
bool GraftValue::canApply(const llvm::Module &M) const {
  if (CorpusIndex::instance().num_functions() == 0)
    return false;
  for (const auto &F : M)
    if (!F.isDeclaration() && !F.empty())
      return true;
  return false;
}

bool GraftValue::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::Function *> hosts;
  for (auto &F : M)
    if (!F.isDeclaration() && !F.empty())
      hosts.push_back(&F);
  if (hosts.empty())
    return false;
  llvm::Function *H = hosts[std::uniform_int_distribution<size_t>(
      0, hosts.size() - 1)(rng)];

  // Sample a donor from the seed library and bring it into M's context.
  // (Cross-context: the library lives in CorpusIndex's own context, so we must
  // round-trip through bitcode. The native crossover hook avoids this by parsing
  // both parents into one context.)
  const CorpusIndex &idx = CorpusIndex::instance();
  llvm::Function *LibF = idx.sample_any(rng);
  if (!LibF)
    return false;
  llvm::SmallString<8192> BC;
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
  llvm::Function *DonorF = TmpM->getFunction(LibF->getName());
  if (!DonorF || DonorF->isDeclaration())
    return false;
  llvm::StripDebugInfo(*TmpM);

  return graftGadgetInto(*H, *DonorF, rng);
}

} // namespace regatoni
