#include "src/litmus-gen/rule_parser.h"

#include "deps/nlohmann-json/json.hpp"

#include <stdexcept>

namespace regatoni::litmus {

using nlohmann::json;

namespace {

// Pull a string field; throw with a helpful message on absence or wrong type.
std::string requireString(const json &j, const char *field,
                          const std::string &where) {
  auto it = j.find(field);
  if (it == j.end() || !it->is_string())
    throw std::runtime_error("missing/non-string '" + std::string(field) +
                             "' in " + where);
  return it->get<std::string>();
}

}  // namespace

RuleDatabase parseDatabase(const std::string &text) {
  json root;
  try {
    root = json::parse(text);
  } catch (const json::parse_error &e) {
    throw std::runtime_error(std::string("JSON parse error: ") + e.what());
  }

  auto insIt = root.find("instructions");
  if (insIt == root.end() || !insIt->is_object())
    throw std::runtime_error("top-level 'instructions' object is missing");

  RuleDatabase db;
  for (auto it = insIt->begin(); it != insIt->end(); ++it) {
    const std::string &instName = it.key();
    const json &instJson = it.value();
    Instruction inst;
    inst.name = instName;

    // Operands: preserve JSON insertion order.
    auto opsIt = instJson.find("operands");
    if (opsIt != instJson.end() && opsIt->is_object()) {
      for (auto opIt = opsIt->begin(); opIt != opsIt->end(); ++opIt) {
        Operand op;
        op.name = opIt.key();
        op.type_constraint =
            requireString(opIt.value(), "type_constraint",
                          instName + ".operands." + opIt.key());
        inst.operands.push_back(std::move(op));
      }
    }

    auto rulesIt = instJson.find("rules");
    if (rulesIt != instJson.end() && rulesIt->is_array()) {
      for (const json &ruleJson : *rulesIt) {
        Rule r;
        r.id = requireString(ruleJson, "id", instName + ".rules[]");
        r.shape = requireString(ruleJson, "shape", r.id);
        auto flagIt = ruleJson.find("flag");
        if (flagIt != ruleJson.end() && flagIt->is_string())
          r.flag = flagIt->get<std::string>();
        inst.rules.push_back(std::move(r));
      }
    }

    db.instructions.push_back(std::move(inst));
  }

  return db;
}

}  // namespace regatoni::litmus
