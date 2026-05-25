#pragma once

#include "src/litmus-gen/rule_schema.h"

#include <string>
#include <vector>

namespace regatoni::litmus {

// Returns the list of type descriptors to instantiate this instruction at.
// For unary/binary integer ops the descriptor is a plain LLVM scalar
// integer type name ("i32"). For cast ops (trunc / zext / sext) it is a
// pair-descriptor "src_to_dst" (e.g. "i32_to_i16") that generators split
// on "_to_". Returns an empty vector for instructions not yet covered by
// V1 (FP, pointer, aggregate, byte type, vectors).
std::vector<std::string>
typesForInstruction(const Instruction &inst,
                    const std::vector<std::string> &requested);

}  // namespace regatoni::litmus
