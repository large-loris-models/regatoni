#include "src/spec-mutate/transform_engine.h"

#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GEPNoWrapFlags.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"

namespace spec_mutate {

using namespace llvm;

// ----------------------------------------------------------------------
// Cloned-instruction lookup.
// ----------------------------------------------------------------------

static Function *clonedFn(ValueToValueMapTy &VMap, const Function &OF) {
  return cast_or_null<Function>(VMap.lookup(&OF));
}
static Instruction *clonedInst(ValueToValueMapTy &VMap,
                               const Instruction *OI) {
  if (!OI) return nullptr;
  return cast_or_null<Instruction>(VMap.lookup(OI));
}

// ----------------------------------------------------------------------
// Flag helpers (same opcode-keyed logic as match_engine.cc, applied to
// the cloned instruction). Returns true if the flag was actually set /
// cleared, false if the opcode doesn't accept it.
// ----------------------------------------------------------------------

static int fmfBit(const std::string &f) {
  if (f == "nnan")     return 0;
  if (f == "ninf")     return 1;
  if (f == "nsz")      return 2;
  if (f == "arcp")     return 3;
  if (f == "contract") return 4;
  if (f == "afn")      return 5;
  if (f == "reassoc")  return 6;
  if (f == "fast")     return 7;
  return -1;
}

static bool setFlag(Instruction *I, const std::string &flag, bool on) {
  unsigned op = I->getOpcode();
  auto isOBOOp = [&]() {
    return op == Instruction::Add || op == Instruction::Sub ||
           op == Instruction::Mul || op == Instruction::Shl;
  };
  auto isExactOp = [&]() {
    return op == Instruction::SDiv || op == Instruction::UDiv ||
           op == Instruction::LShr || op == Instruction::AShr;
  };

  if (flag == "nsw") {
    if (isOBOOp())                { I->setHasNoSignedWrap(on); return true; }
    if (op == Instruction::Trunc) { I->setHasNoSignedWrap(on); return true; }
    return false;
  }
  if (flag == "nuw") {
    if (isOBOOp())                { I->setHasNoUnsignedWrap(on); return true; }
    if (op == Instruction::Trunc) { I->setHasNoUnsignedWrap(on); return true; }
    if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
      auto cur = GEP->getNoWrapFlags();
      auto bit = GEPNoWrapFlags::noUnsignedWrap();
      GEPNoWrapFlags next = on
          ? (cur | bit)
          : GEPNoWrapFlags::fromRaw(cur.getRaw() & ~bit.getRaw());
      GEP->setNoWrapFlags(next);
      return true;
    }
    return false;
  }
  if (flag == "exact") {
    if (isExactOp()) { I->setIsExact(on); return true; }
    return false;
  }
  if (flag == "inbounds") {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
      GEP->setIsInBounds(on);
      return true;
    }
    return false;
  }
  if (flag == "nneg") {
    if (op == Instruction::ZExt) { I->setNonNeg(on); return true; }
    return false;
  }
  if (flag == "disjoint") {
    if (auto *PDI = dyn_cast<PossiblyDisjointInst>(I)) {
      PDI->setIsDisjoint(on);
      return true;
    }
    return false;
  }
  if (fmfBit(flag) >= 0 && isa<FPMathOperator>(I)) {
    FastMathFlags FMF = I->getFastMathFlags();
    if      (flag == "nnan")     FMF.setNoNaNs(on);
    else if (flag == "ninf")     FMF.setNoInfs(on);
    else if (flag == "nsz")      FMF.setNoSignedZeros(on);
    else if (flag == "arcp")     FMF.setAllowReciprocal(on);
    else if (flag == "contract") FMF.setAllowContract(on);
    else if (flag == "afn")      FMF.setApproxFunc(on);
    else if (flag == "reassoc")  FMF.setAllowReassoc(on);
    else if (flag == "fast")     FMF.setFast(on);
    I->setFastMathFlags(FMF);
    return true;
  }
  return false;
}

// ----------------------------------------------------------------------
// Attribute resolution. Shared spelling with match_engine.cc.
// ----------------------------------------------------------------------

static Attribute::AttrKind attrKind(const std::string &s) {
  if (s == "noundef")          return Attribute::NoUndef;
  if (s == "nonnull")          return Attribute::NonNull;
  if (s == "willreturn")       return Attribute::WillReturn;
  if (s == "nounwind")         return Attribute::NoUnwind;
  if (s == "mustprogress")     return Attribute::MustProgress;
  if (s == "readonly")         return Attribute::ReadOnly;
  if (s == "readnone")         return Attribute::ReadNone;
  if (s == "noalias")          return Attribute::NoAlias;
  if (s == "nofree")           return Attribute::NoFree;
  if (s == "signext")          return Attribute::SExt;
  if (s == "zeroext")          return Attribute::ZExt;
  if (s == "dereferenceable")  return Attribute::Dereferenceable;
  return Attribute::None;
}

// ----------------------------------------------------------------------
// Metadata construction.
//
// For a numeric range scheme on iN, build !{iN low, iN high}. Returns
// null if the scheme is unsupported for the type (e.g. "small" on i8).
// ----------------------------------------------------------------------

static MDNode *buildRangeMD(LLVMContext &Ctx, IntegerType *IT,
                            const std::string &scheme) {
  unsigned bw = IT->getBitWidth();
  APInt lo(bw, 0), hi(bw, 0);
  if (scheme == "nonzero") {
    if (bw < 2) return nullptr;             // [1,0) needs at least 2 bits
    lo = APInt(bw, 1);                       // [1, 0) wrap → nonzero
    hi = APInt(bw, 0);
  } else if (scheme == "nonneg") {
    if (bw < 2) return nullptr;
    lo = APInt(bw, 0);
    hi = APInt::getSignedMinValue(bw);       // 2^(N-1) (== signed min)
  } else if (scheme == "boolean") {
    if (bw < 2) return nullptr;
    lo = APInt(bw, 0);
    hi = APInt(bw, 2);
  } else if (scheme == "small") {
    if (bw < 9) return nullptr;              // 256 must fit in iN
    lo = APInt(bw, 0);
    hi = APInt(bw, 256);
  } else {
    return nullptr;
  }
  auto *L = ConstantInt::get(IT, lo);
  auto *H = ConstantInt::get(IT, hi);
  return MDNode::get(Ctx, {ConstantAsMetadata::get(L),
                           ConstantAsMetadata::get(H)});
}

static unsigned fpClassMask(const std::string &scheme) {
  // FPClassTest bits, matching LLVM's enum.
  //   SNaN=0x001, QNaN=0x002, NegInf=0x004, PosInf=0x200.
  if (scheme == "no_nan") return 0x003;
  if (scheme == "no_inf") return 0x204;
  return 0;
}

// ----------------------------------------------------------------------
// Constant construction.
// ----------------------------------------------------------------------

static Constant *buildIntConstant(IntegerType *IT, const std::string &scheme) {
  unsigned bw = IT->getBitWidth();
  if (scheme == "zero")              return ConstantInt::get(IT, 0);
  if (scheme == "one")               return ConstantInt::get(IT, 1);
  if (scheme == "minus_one")         return ConstantInt::getAllOnesValue(IT);
  if (scheme == "smin")              return ConstantInt::get(IT, APInt::getSignedMinValue(bw));
  if (scheme == "smax")              return ConstantInt::get(IT, APInt::getSignedMaxValue(bw));
  if (scheme == "umax")              return ConstantInt::get(IT, APInt::getMaxValue(bw));
  if (scheme == "bitwidth")          return ConstantInt::get(IT, bw);
  if (scheme == "bitwidth_minus_1")  return ConstantInt::get(IT, bw > 0 ? bw - 1 : 0);
  if (scheme == "power_of_two")      return ConstantInt::get(IT, 4);
  return nullptr;
}

// ----------------------------------------------------------------------
// Per-action implementations.
// ----------------------------------------------------------------------

static bool doAddFlag(Instruction *CI, const TransformSpec &tx) {
  return setFlag(CI, tx.flag, /*on=*/true);
}
static bool doRemoveFlag(Instruction *CI, const TransformSpec &tx) {
  return setFlag(CI, tx.flag, /*on=*/false);
}

static bool doAddParamAttr(Function *CF, unsigned idx, const TransformSpec &tx) {
  if (idx >= CF->arg_size()) return false;
  if (tx.attr == "dereferenceable") {
    uint64_t bytes = tx.value ? (uint64_t)*tx.value : 4;
    Attribute A = Attribute::getWithDereferenceableBytes(CF->getContext(), bytes);
    CF->addParamAttr(idx, A);
    return true;
  }
  Attribute::AttrKind k = attrKind(tx.attr);
  if (k == Attribute::None) return false;
  CF->addParamAttr(idx, k);
  return true;
}

static bool doRemoveParamAttr(Function *CF, unsigned idx, const TransformSpec &tx) {
  if (idx >= CF->arg_size()) return false;
  Attribute::AttrKind k = attrKind(tx.attr);
  if (k == Attribute::None) return false;
  if (!CF->hasParamAttribute(idx, k)) return false;
  CF->removeParamAttr(idx, k);
  return true;
}

static bool doAddFnAttr(Function *CF, const TransformSpec &tx) {
  Attribute::AttrKind k = attrKind(tx.attr);
  if (k == Attribute::None) {
    CF->addFnAttr(tx.attr);
  } else {
    CF->addFnAttr(k);
  }
  return true;
}

static bool doAddRetAttr(Function *CF, const TransformSpec &tx) {
  Type *RetTy = CF->getReturnType();
  if (RetTy->isVoidTy()) return false;
  // Only attach attributes that make sense for the return type.
  if (tx.attr == "nonnull" && !RetTy->isPointerTy()) return false;
  if (tx.attr == "noundef" && (RetTy->isVoidTy())) return false;
  if (tx.attr == "dereferenceable") {
    if (!RetTy->isPointerTy()) return false;
    uint64_t bytes = tx.value ? (uint64_t)*tx.value : 4;
    Attribute A = Attribute::getWithDereferenceableBytes(CF->getContext(), bytes);
    CF->addRetAttr(A);
    return true;
  }
  Attribute::AttrKind k = attrKind(tx.attr);
  if (k == Attribute::None) return false;
  CF->addRetAttr(k);
  return true;
}

static bool doAddMetadata(Instruction *CI, const TransformSpec &tx) {
  LLVMContext &Ctx = CI->getContext();
  if (tx.kind == "range") {
    auto *IT = dyn_cast<IntegerType>(CI->getType());
    if (!IT) return false;
    MDNode *MD = buildRangeMD(Ctx, IT, tx.scheme);
    if (!MD) return false;
    CI->setMetadata(LLVMContext::MD_range, MD);
    return true;
  }
  if (tx.kind == "nonnull") {
    if (!CI->getType()->isPointerTy()) return false;
    CI->setMetadata(LLVMContext::MD_nonnull, MDNode::get(Ctx, {}));
    return true;
  }
  if (tx.kind == "nofpclass") {
    if (!CI->getType()->isFloatingPointTy()) return false;
    unsigned mask = fpClassMask(tx.scheme);
    if (mask == 0) return false;
    auto *Bits = ConstantInt::get(Type::getInt32Ty(Ctx), mask);
    auto *MD = MDNode::get(Ctx, {ConstantAsMetadata::get(Bits)});
    CI->setMetadata(LLVMContext::MD_nofpclass, MD);
    return true;
  }
  return false;
}

static bool doInsertFreeze(Instruction *CI, const TransformSpec &tx) {
  if (!tx.on_operand) return false;
  int idx = *tx.on_operand;
  if (idx < 0 || (unsigned)idx >= CI->getNumOperands()) return false;
  // PHIs cannot have a non-PHI instruction inserted before them.
  if (isa<PHINode>(CI)) return false;
  Value *Op = CI->getOperand(idx);
  if (!Op || isa<FreezeInst>(Op)) return false;
  // Don't freeze a metadata / token / label operand.
  Type *OT = Op->getType();
  if (OT->isMetadataTy() || OT->isTokenTy() || OT->isLabelTy())
    return false;
  auto *FI = new FreezeInst(Op, "", CI->getIterator());
  CI->setOperand(idx, FI);
  return true;
}

static bool doReplaceWithConstant(Instruction *CI, const TransformSpec &tx) {
  if (!tx.operand) return false;
  int idx = *tx.operand;
  if (idx < 0 || (unsigned)idx >= CI->getNumOperands()) return false;
  Value *Op = CI->getOperand(idx);
  if (!Op) return false;
  auto *IT = dyn_cast<IntegerType>(Op->getType());
  if (!IT) return false;
  Constant *C = buildIntConstant(IT, tx.scheme);
  if (!C) return false;
  CI->setOperand(idx, C);
  return true;
}

// ----------------------------------------------------------------------
// applyTransform dispatch.
// ----------------------------------------------------------------------

bool applyTransform(Module &CM, ValueToValueMapTy &VMap,
                    const Function &OF, const MatchLocation &loc,
                    const TransformSpec &tx) {
  (void)CM;
  Function *CF = clonedFn(VMap, OF);
  if (!CF) return false;

  const std::string &A = tx.action;
  if (A == "add_flag" || A == "remove_flag" ||
      A == "add_metadata" || A == "insert_freeze" ||
      A == "replace_with_constant") {
    if (loc.kind != MatchLocation::kInstruction) return false;
    Instruction *CI = clonedInst(VMap, loc.inst);
    if (!CI) return false;
    if (A == "add_flag")               return doAddFlag(CI, tx);
    if (A == "remove_flag")            return doRemoveFlag(CI, tx);
    if (A == "add_metadata")           return doAddMetadata(CI, tx);
    if (A == "insert_freeze")          return doInsertFreeze(CI, tx);
    if (A == "replace_with_constant")  return doReplaceWithConstant(CI, tx);
  }

  if (A == "add_param_attr" || A == "remove_param_attr") {
    if (loc.kind != MatchLocation::kParameter) return false;
    if (A == "add_param_attr")    return doAddParamAttr(CF, loc.param_idx, tx);
    if (A == "remove_param_attr") return doRemoveParamAttr(CF, loc.param_idx, tx);
  }

  if (A == "add_fn_attr") {
    if (loc.kind != MatchLocation::kFunction) return false;
    return doAddFnAttr(CF, tx);
  }
  if (A == "add_ret_attr") {
    if (loc.kind != MatchLocation::kFunction) return false;
    return doAddRetAttr(CF, tx);
  }
  return false;
}

}  // namespace spec_mutate
