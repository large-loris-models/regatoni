#include "src/litmus-gen/rule_parser.h"

#include "deps/nlohmann-json/json.hpp"

#include <map>
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

// Parse one rule JSON into a Rule. If fromFamily is set, substitute the
// "<family>" placeholder in the rule id with instName (e.g. the family's
// "<family>.defined.libm_conformance" becomes "llvm.sqrt.defined.libm_conformance"
// for the llvm.sqrt member).
Rule parseRule(const json &ruleJson, const std::string &instName,
               bool fromFamily) {
  Rule r;
  r.id = requireString(ruleJson, "id", instName + ".rules[]");
  if (fromFamily) {
    static const std::string placeholder = "<family>";
    size_t pos = r.id.find(placeholder);
    if (pos != std::string::npos)
      r.id.replace(pos, placeholder.size(), instName);
  }
  r.shape = requireString(ruleJson, "shape", r.id);
  auto flagIt = ruleJson.find("flag");
  if (flagIt != ruleJson.end() && flagIt->is_string())
    r.flag = flagIt->get<std::string>();
  return r;
}

}  // namespace

RuleDatabase parseDatabase(const std::string &text) {
  json root;
  try {
    root = json::parse(text);
  } catch (const json::parse_error &e) {
    throw std::runtime_error(std::string("JSON parse error: ") + e.what());
  }

  // Optional top-level "families" map. When an entry below carries a
  // "family" field instead of inline rules, we look the rules up here.
  std::map<std::string, const json *> families;
  auto famIt = root.find("families");
  if (famIt != root.end() && famIt->is_object()) {
    for (auto it = famIt->begin(); it != famIt->end(); ++it)
      families[it.key()] = &it.value();
  }

  // The per-instruction map can live under "instructions" (V1 JSONs) or
  // "intrinsics" (the family-grouped JSON in extraction_remaining.json).
  const json *instMap = nullptr;
  auto i1 = root.find("instructions");
  if (i1 != root.end() && i1->is_object()) {
    instMap = &*i1;
  } else {
    auto i2 = root.find("intrinsics");
    if (i2 != root.end() && i2->is_object()) instMap = &*i2;
  }
  if (!instMap)
    throw std::runtime_error(
        "top-level 'instructions' or 'intrinsics' object is missing");

  RuleDatabase db;
  for (auto it = instMap->begin(); it != instMap->end(); ++it) {
    const std::string &instName = it.key();
    const json &instJson = it.value();
    Instruction inst;
    inst.name = instName;

    // Operands: preserve JSON insertion order (when present).
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

    // Rules: prefer inline; fall back to the named family if absent.
    auto rulesIt = instJson.find("rules");
    if (rulesIt != instJson.end() && rulesIt->is_array()) {
      for (const json &ruleJson : *rulesIt)
        inst.rules.push_back(parseRule(ruleJson, instName, /*fromFamily=*/false));
    } else {
      auto fIt = instJson.find("family");
      if (fIt != instJson.end() && fIt->is_string()) {
        auto fit = families.find(fIt->get<std::string>());
        if (fit != families.end()) {
          auto rIt = fit->second->find("rules");
          if (rIt != fit->second->end() && rIt->is_array()) {
            for (const json &ruleJson : *rIt)
              inst.rules.push_back(
                  parseRule(ruleJson, instName, /*fromFamily=*/true));
          }
        }
      }
    }

    db.instructions.push_back(std::move(inst));
  }

  return db;
}

}  // namespace regatoni::litmus
