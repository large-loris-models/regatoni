#include "src/rewrite-seeds/match_engine.h"

#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"

#include <algorithm>

namespace rewrite_seeds {

using namespace llvm;

// ----------------------------------------------------------------------
// Flag inspection.
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

bool instHasFlag(const Instruction &I, const std::string &flag) {
  if (flag == "nsw") {
    if (auto *OBO = dyn_cast<OverflowingBinaryOperator>(&I))
      return OBO->hasNoSignedWrap();
    if (auto *TI = dyn_cast<TruncInst>(&I))
      return TI->hasNoSignedWrap();
    return false;
  }
  if (flag == "nuw") {
    if (auto *OBO = dyn_cast<OverflowingBinaryOperator>(&I))
      return OBO->hasNoUnsignedWrap();
    if (auto *TI = dyn_cast<TruncInst>(&I))
      return TI->hasNoUnsignedWrap();
    if (auto *GEP = dyn_cast<GetElementPtrInst>(&I))
      return GEP->hasNoUnsignedWrap();
    return false;
  }
  if (flag == "exact") {
    if (auto *PEO = dyn_cast<PossiblyExactOperator>(&I))
      return PEO->isExact();
    return false;
  }
  if (flag == "inbounds") {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(&I))
      return GEP->isInBounds();
    return false;
  }
  if (flag == "nneg") {
    return I.getOpcode() == Instruction::ZExt && I.hasNonNeg();
  }
  if (flag == "disjoint") {
    if (auto *PDI = dyn_cast<PossiblyDisjointInst>(&I))
      return PDI->isDisjoint();
    return false;
  }
  if (fmfBit(flag) >= 0) {
    if (!isa<FPMathOperator>(&I)) return false;
    FastMathFlags FMF = I.getFastMathFlags();
    if (flag == "nnan")     return FMF.noNaNs();
    if (flag == "ninf")     return FMF.noInfs();
    if (flag == "nsz")      return FMF.noSignedZeros();
    if (flag == "arcp")     return FMF.allowReciprocal();
    if (flag == "contract") return FMF.allowContract();
    if (flag == "afn")      return FMF.approxFunc();
    if (flag == "reassoc")  return FMF.allowReassoc();
    if (flag == "fast")     return FMF.isFast();
  }
  return false;
}

// ----------------------------------------------------------------------
// Type-class predicate.
// ----------------------------------------------------------------------

static bool typeMatchesClass(Type *T, const std::string &cls) {
  if (cls == "integer") return T->isIntegerTy();
  if (cls == "float")   return T->isFloatingPointTy();
  if (cls == "pointer") return T->isPointerTy();
  if (cls == "vector")  return T->isVectorTy();
  return false;
}

// ----------------------------------------------------------------------
// Attribute resolution (string → Attribute::AttrKind).
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

static bool fnHasAttr(const Function &F, const std::string &name) {
  Attribute::AttrKind k = attrKind(name);
  if (k != Attribute::None) return F.hasFnAttribute(k);
  return F.hasFnAttribute(name);
}
static bool paramHasAttr(const Function &F, unsigned idx,
                         const std::string &name) {
  Attribute::AttrKind k = attrKind(name);
  if (k != Attribute::None) return F.hasParamAttribute(idx, k);
  return F.getAttributes().hasParamAttr(idx, name);
}

// ----------------------------------------------------------------------
// Per-target match.
// ----------------------------------------------------------------------

static bool instructionMatches(const Instruction &I, const MatchSpec &m) {
  if (!m.opcodes.empty()) {
    const char *op = I.getOpcodeName();
    if (!op || std::find(m.opcodes.begin(), m.opcodes.end(),
                         std::string(op)) == m.opcodes.end())
      return false;
  }
  if (m.flag_present && !instHasFlag(I, *m.flag_present)) return false;
  if (m.flag_absent  &&  instHasFlag(I, *m.flag_absent))  return false;
  if (m.result_type_class &&
      !typeMatchesClass(I.getType(), *m.result_type_class)) return false;
  if (m.metadata_present  && !I.hasMetadata(*m.metadata_present)) return false;
  if (m.metadata_absent   &&  I.hasMetadata(*m.metadata_absent))  return false;

  if (m.operand_index || m.operand_is_param || m.operand_type_class) {
    if (!m.operand_index) return false;            // need an index to anchor
    int idx = *m.operand_index;
    if (idx < 0 || (unsigned)idx >= I.getNumOperands()) return false;
    const Value *Op = I.getOperand(idx);
    if (m.operand_is_param && *m.operand_is_param != isa<Argument>(Op))
      return false;
    if (m.operand_type_class &&
        !typeMatchesClass(Op->getType(), *m.operand_type_class))
      return false;
  }
  return true;
}

static bool parameterMatches(const Function &F, unsigned idx,
                             const MatchSpec &m) {
  const Argument *A = F.getArg(idx);
  if (m.type_class && !typeMatchesClass(A->getType(), *m.type_class))
    return false;
  if (m.attr_present && !paramHasAttr(F, idx, *m.attr_present)) return false;
  if (m.attr_absent  &&  paramHasAttr(F, idx, *m.attr_absent))  return false;
  return true;
}

static bool functionMatches(const Function &F, const MatchSpec &m) {
  if (m.attr_present && !fnHasAttr(F, *m.attr_present)) return false;
  if (m.attr_absent  &&  fnHasAttr(F, *m.attr_absent))  return false;
  return true;
}

// ----------------------------------------------------------------------
// findMatches — entry point.
// ----------------------------------------------------------------------

std::vector<MatchLocation> findMatches(const Function &F, const Rewrite &R) {
  std::vector<MatchLocation> out;
  const MatchSpec &m = R.match;

  if (m.target == "instruction") {
    for (const BasicBlock &BB : F) {
      for (const Instruction &I : BB) {
        if (!instructionMatches(I, m)) continue;
        MatchLocation L;
        L.kind = MatchLocation::kInstruction;
        L.inst = &I;
        L.operand_idx = m.operand_index ? *m.operand_index : -1;
        out.push_back(L);
      }
    }
  } else if (m.target == "parameter") {
    for (unsigned i = 0; i < F.arg_size(); ++i) {
      if (!parameterMatches(F, i, m)) continue;
      MatchLocation L;
      L.kind = MatchLocation::kParameter;
      L.param_idx = i;
      out.push_back(L);
    }
  } else if (m.target == "function") {
    if (functionMatches(F, m)) {
      MatchLocation L;
      L.kind = MatchLocation::kFunction;
      out.push_back(L);
    }
  }
  return out;
}

}  // namespace rewrite_seeds
