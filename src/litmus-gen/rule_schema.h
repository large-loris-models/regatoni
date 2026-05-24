// rule_schema.h — minimal in-memory mirror of the LangRef rule JSON.
//
// We only carry the fields the generators consume: instruction name,
// operand type_constraint strings, and per-rule (id, shape, flag). The
// rest of the JSON (spec text, langref line numbers, output_properties,
// cross_cutting_applicable) is informational and intentionally dropped.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace regatoni::litmus {

struct Operand {
  std::string name;             // "op1", "a", etc.
  std::string type_constraint;  // e.g. "IntT or <N x IntT>"
};

struct Rule {
  std::string id;     // e.g. "shl.poison.nuw"
  std::string shape;  // e.g. "discard_poison"
  std::optional<std::string> flag;  // e.g. "nuw"; nullopt if absent/null
};

struct Instruction {
  std::string name;                // key in JSON, e.g. "add", "llvm.sadd.with.overflow"
  std::vector<Operand> operands;   // ordered as in JSON (op1, op2, ...)
  std::vector<Rule> rules;
};

struct RuleDatabase {
  // Preserve insertion order from JSON for deterministic output.
  std::vector<Instruction> instructions;
};

}  // namespace regatoni::litmus
