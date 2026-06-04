#include "src/spec-mutate/spec.h"

#include "deps/nlohmann-json/json.hpp"

#include <fstream>
#include <sstream>

namespace spec_mutate {

using nlohmann::json;

static std::optional<std::string> optStr(const json &j, const char *k) {
  if (!j.contains(k) || j[k].is_null()) return std::nullopt;
  return j[k].get<std::string>();
}
static std::optional<int> optInt(const json &j, const char *k) {
  if (!j.contains(k) || j[k].is_null()) return std::nullopt;
  return j[k].get<int>();
}
static std::optional<bool> optBool(const json &j, const char *k) {
  if (!j.contains(k) || j[k].is_null()) return std::nullopt;
  return j[k].get<bool>();
}

static bool parseMatch(const json &j, MatchSpec &m, std::string &err) {
  if (!j.is_object()) { err = "match must be object"; return false; }
  auto t = optStr(j, "target");
  if (!t) { err = "match.target is required"; return false; }
  m.target = *t;

  if (j.contains("opcode")) {
    if (!j["opcode"].is_array()) { err = "match.opcode must be array"; return false; }
    for (const auto &op : j["opcode"]) m.opcodes.push_back(op.get<std::string>());
  }
  m.flag_present       = optStr(j, "flag_present");
  m.flag_absent        = optStr(j, "flag_absent");
  m.result_type_class  = optStr(j, "result_type_class");
  m.metadata_present   = optStr(j, "metadata_present");
  m.metadata_absent    = optStr(j, "metadata_absent");
  m.operand_index      = optInt(j, "operand_index");
  m.operand_is_param   = optBool(j, "operand_is_param");
  m.operand_type_class = optStr(j, "operand_type_class");
  m.type_class         = optStr(j, "type_class");
  m.attr_present       = optStr(j, "attr_present");
  m.attr_absent        = optStr(j, "attr_absent");
  return true;
}

static bool parseTransform(const json &j, TransformSpec &t, std::string &err) {
  if (!j.is_object()) { err = "transform must be object"; return false; }
  auto a = optStr(j, "action");
  if (!a) { err = "transform.action is required"; return false; }
  t.action = *a;
  if (auto v = optStr(j, "flag"))   t.flag   = *v;
  if (auto v = optStr(j, "attr"))   t.attr   = *v;
  if (auto v = optStr(j, "kind"))   t.kind   = *v;
  if (auto v = optStr(j, "scheme")) t.scheme = *v;
  t.value      = optInt(j, "value");
  t.on_operand = optInt(j, "on_operand");
  t.operand    = optInt(j, "operand");
  return true;
}

bool loadSpec(const std::string &path, Spec &out, std::string &err) {
  std::ifstream in(path);
  if (!in) { err = "cannot open " + path; return false; }
  json j;
  try { in >> j; }
  catch (const std::exception &e) {
    err = std::string("parse failure: ") + e.what();
    return false;
  }
  if (!j.contains("rewrites") || !j["rewrites"].is_array()) {
    err = "top-level 'rewrites' array missing";
    return false;
  }
  for (const auto &re : j["rewrites"]) {
    if (!re.is_object()) { err = "rewrite entry must be object"; return false; }
    Rewrite R;
    auto id = optStr(re, "id");
    if (!id) { err = "rewrite missing 'id'"; return false; }
    R.id = *id;
    if (auto c = optStr(re, "category"))   R.category   = *c;
    if (auto c = optStr(re, "tests_rule")) R.tests_rule = *c;
    if (!re.contains("match")) { err = R.id + ": missing 'match'"; return false; }
    if (!parseMatch(re["match"], R.match, err)) { err = R.id + ": " + err; return false; }
    if (!re.contains("transform")) { err = R.id + ": missing 'transform'"; return false; }
    if (!parseTransform(re["transform"], R.transform, err)) { err = R.id + ": " + err; return false; }
    out.rewrites.push_back(std::move(R));
  }
  return true;
}

}  // namespace spec_mutate
