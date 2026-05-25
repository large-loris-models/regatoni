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
std::vector<NamedFile> fnegOp(const Instruction &, const Rule &,
                               const std::string &type);
std::vector<NamedFile> gepOp(const Instruction &, const Rule &,
                              const std::string &type);
std::vector<NamedFile> loadStoreOp(const Instruction &, const Rule &,
                                    const std::string &type);
std::vector<NamedFile> vectorOp(const Instruction &, const Rule &,
                                 const std::string &type);
std::vector<NamedFile> fpMinMax(const Instruction &, const Rule &,
                                 const std::string &type);
std::vector<NamedFile> comparisonIntrinsic(const Instruction &, const Rule &,
                                            const std::string &type);
std::vector<NamedFile> memoryIntrinsic(const Instruction &, const Rule &,
                                        const std::string &type);
std::vector<NamedFile> satConversionIntrinsic(const Instruction &,
                                               const Rule &,
                                               const std::string &type);
std::vector<NamedFile> satShiftIntrinsic(const Instruction &, const Rule &,
                                          const std::string &type);
std::vector<NamedFile> fixedPointIntrinsic(const Instruction &, const Rule &,
                                            const std::string &type);
std::vector<NamedFile> unaryFPIntrinsic(const Instruction &, const Rule &,
                                         const std::string &type);
std::vector<NamedFile> binaryFPIntrinsic(const Instruction &, const Rule &,
                                          const std::string &type);
std::vector<NamedFile> ternaryFPIntrinsic(const Instruction &, const Rule &,
                                           const std::string &type);
std::vector<NamedFile> fpToIntIntrinsic(const Instruction &, const Rule &,
                                         const std::string &type);
std::vector<NamedFile> powiIntrinsic(const Instruction &, const Rule &,
                                      const std::string &type);
std::vector<NamedFile> integerVectorReduction(const Instruction &,
                                               const Rule &,
                                               const std::string &type);
std::vector<NamedFile> fpVectorReduction(const Instruction &, const Rule &,
                                          const std::string &type);
std::vector<NamedFile> fpMinMaxVectorReduction(const Instruction &,
                                                const Rule &,
                                                const std::string &type);
std::vector<NamedFile> fixedPointSatIntrinsic(const Instruction &,
                                               const Rule &,
                                               const std::string &type);
std::vector<NamedFile> fpClassQuery(const Instruction &, const Rule &,
                                     const std::string &type);
std::vector<NamedFile> clmulIntrinsic(const Instruction &, const Rule &,
                                       const std::string &type);
std::vector<NamedFile> ptrmaskIntrinsic(const Instruction &, const Rule &,
                                         const std::string &type);

// Helper exposed for unit-test-style usage.
std::string mangleRuleId(const std::string &rule_id);

}  // namespace regatoni::litmus::generators
