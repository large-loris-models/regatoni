// rewrite-seeds: transform actions.
//
// applyTransform takes a cloned module, a VMap mapping original→clone,
// the *original* function and the location (also referring to originals),
// and applies the spec's action on the clone. Returns true when the IR
// was actually changed.

#pragma once

#include "src/rewrite-seeds/match_engine.h"
#include "src/rewrite-seeds/spec.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

namespace rewrite_seeds {

bool applyTransform(llvm::Module &CM,
                    llvm::ValueToValueMapTy &VMap,
                    const llvm::Function &OF,
                    const MatchLocation &loc,
                    const TransformSpec &tx);

}  // namespace rewrite_seeds
