#pragma once

#include "src/litmus-gen/generator_registry.h"
#include "src/litmus-gen/rule_schema.h"

#include <string>
#include <vector>

namespace regatoni::litmus::generators {

// Public generators. Each returns a vector of NamedFile (suffix + body);
// empty vector means "skip — this generator doesn't handle this
// (instruction, rule) combination".

std::vector<NamedFile> binaryOpWithFlag(const Instruction &, const Rule &,
                                         const std::string &type);
std::vector<NamedFile> overflowGen(const Instruction &, const Rule &,
                                    const std::string &type);
std::vector<NamedFile> castOp(const Instruction &, const Rule &,
                               const std::string &type);
std::vector<NamedFile> comparison(const Instruction &, const Rule &,
                                   const std::string &type);
std::vector<NamedFile> selectOp(const Instruction &, const Rule &,
                                 const std::string &type);
std::vector<NamedFile> freezeOp(const Instruction &, const Rule &,
                                 const std::string &type);
std::vector<NamedFile> unaryIntrinsic(const Instruction &, const Rule &,
                                       const std::string &type);
std::vector<NamedFile> unaryIntrinsicWithFlag(const Instruction &,
                                               const Rule &,
                                               const std::string &type);
std::vector<NamedFile> binaryIntrinsic(const Instruction &, const Rule &,
                                        const std::string &type);
std::vector<NamedFile> absIntrinsic(const Instruction &, const Rule &,
                                     const std::string &type);
std::vector<NamedFile> ternaryIntrinsic(const Instruction &, const Rule &,
                                         const std::string &type);

// Helper exposed for unit-test-style usage.
std::string mangleRuleId(const std::string &rule_id);

}  // namespace regatoni::litmus::generators
