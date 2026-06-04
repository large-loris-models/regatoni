// spec-mutate: in-memory representation of a JSON rewrite spec.
//
// A Spec is a list of Rewrites. Each Rewrite pairs a conjunctive match
// against IR with one transform action. The engine doesn't know about
// specific rules — it executes the spec.

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace spec_mutate {

struct MatchSpec {
  std::string target;  // "instruction" | "parameter" | "function"

  // Instruction predicates.
  std::vector<std::string> opcodes;        // empty == any
  std::optional<std::string> flag_present;
  std::optional<std::string> flag_absent;
  std::optional<std::string> result_type_class;
  std::optional<std::string> metadata_present;
  std::optional<std::string> metadata_absent;
  std::optional<int>         operand_index;
  std::optional<bool>        operand_is_param;
  std::optional<std::string> operand_type_class;

  // Parameter predicates.
  std::optional<std::string> type_class;

  // Parameter / function attribute predicates (same names).
  std::optional<std::string> attr_present;
  std::optional<std::string> attr_absent;
};

struct TransformSpec {
  std::string action;

  // Per-action fields. Each action reads a subset.
  std::string         flag;
  std::string         attr;
  std::optional<int>  value;       // attribute byte count, etc.
  std::string         kind;        // metadata kind
  std::string         scheme;      // metadata / constant scheme
  std::optional<int>  on_operand;  // insert_freeze
  std::optional<int>  operand;     // replace_with_constant
};

struct Rewrite {
  std::string   id;
  std::string   category;
  std::string   tests_rule;
  MatchSpec     match;
  TransformSpec transform;
};

struct Spec {
  std::vector<Rewrite> rewrites;
};

// Load the JSON spec at `path`. On failure returns false and sets `err`.
bool loadSpec(const std::string &path, Spec &out, std::string &err);

}  // namespace spec_mutate
