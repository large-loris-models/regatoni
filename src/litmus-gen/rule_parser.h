#pragma once

#include "src/litmus-gen/rule_schema.h"

#include <string>

namespace regatoni::litmus {

// Parse a JSON rule database from `text` (the full file contents). Throws
// std::runtime_error with a descriptive message on parse / schema errors.
RuleDatabase parseDatabase(const std::string &text);

}  // namespace regatoni::litmus
