// generator_registry.h — instruction name → generator function.
//
// Each generator returns a vector of NamedFiles (suffix + .ll body).
// Empty vector means the generator declined this (rule, type) combination
// (caller logs a skip). Most generators return a single NamedFile with an
// empty suffix; a few (cttz/ctlz/abs with both flag values, icmp with
// multiple predicates) return several.

#pragma once

#include "src/litmus-gen/rule_schema.h"

#include <string>
#include <vector>

namespace regatoni::litmus {

struct NamedFile {
  // Disambiguator appended to the file name when non-empty:
  //   <rule_id>.<type>.<suffix>.ll
  // and to the function name as "_<suffix>". Empty produces the default
  //   <rule_id>.<type>.ll
  std::string suffix;
  std::string body;
};

using GeneratorFn = std::vector<NamedFile> (*)(const Instruction &,
                                                const Rule &,
                                                const std::string &);

// Returns nullptr if no generator covers this instruction.
GeneratorFn findGenerator(const Instruction &inst);

}  // namespace regatoni::litmus
