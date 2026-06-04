// spec-mutate: transform actions.
//
// applyTransform takes a cloned module, a VMap mapping original→clone,
// the *original* function and the location (also referring to originals),
// and applies the spec's action on the clone. Returns true when the IR
// was actually changed.

#pragma once

#include "src/spec-mutate/match_engine.h"
#include "src/spec-mutate/spec.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

namespace spec_mutate {

bool applyTransform(llvm::Module &CM,
                    llvm::ValueToValueMapTy &VMap,
                    const llvm::Function &OF,
                    const MatchLocation &loc,
                    const TransformSpec &tx);

}  // namespace spec_mutate
