// litmus-gen — generate .ll litmus tests from a LangRef rule database.
//
// Reads a JSON rule database (see docs/extraction_*.json) and emits one
// or more .ll files per (rule, type) pair into --output-dir. Each
// emitted module is parsed + verifyModule'd before being written, so a
// generator bug fails loudly with the offending IR text on stderr.
//
// Logging is per-instruction: at the end we print one line per
// instruction with its generated-file count, and a separate list of
// instructions that produced zero files with the reason (no compatible
// types / no generator / generator declined every variant).

#include "src/litmus-gen/generator_registry.h"
#include "src/litmus-gen/rule_parser.h"
#include "src/litmus-gen/rule_schema.h"
#include "src/litmus-gen/type_instantiation.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

namespace {

struct Options {
  std::string rules_path;
  std::string output_dir;
  std::vector<std::string> types{"i8", "i16", "i32", "i64"};
};

[[noreturn]] void usage(int code) {
  std::fprintf(stderr,
               "Usage: litmus-gen --rules <path.json> --output-dir <dir>\n"
               "                  [--types i8,i16,i32,i64]\n"
               "\n"
               "  Generates one or more .ll files per (rule, type) pair into\n"
               "  --output-dir. File naming:\n"
               "    <rule_id>.<type>.ll                  (default)\n"
               "    <rule_id>.<type>.<suffix>.ll         (when generator\n"
               "                                          produces variants:\n"
               "                                          icmp predicates,\n"
               "                                          cttz/abs flag\n"
               "                                          values, etc.)\n");
  std::exit(code);
}

std::vector<std::string> splitCsv(const std::string &s) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == ',') {
      if (i > start) out.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

Options parseArgs(int argc, char **argv) {
  Options opts;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto take = [&](const char *flag, std::string &dst) -> bool {
      size_t flen = std::strlen(flag);
      if (a == flag) {
        if (i + 1 >= argc) {
          std::fprintf(stderr, "litmus-gen: %s requires a value\n", flag);
          usage(2);
        }
        dst = argv[++i];
        return true;
      }
      if (a.size() > flen && a.compare(0, flen, flag) == 0 && a[flen] == '=') {
        dst = a.substr(flen + 1);
        return true;
      }
      return false;
    };
    std::string val;
    if (take("--rules", opts.rules_path)) continue;
    if (take("--output-dir", opts.output_dir)) continue;
    if (take("--types", val)) {
      opts.types = splitCsv(val);
      continue;
    }
    if (a == "-h" || a == "--help") usage(0);
    std::fprintf(stderr, "litmus-gen: unknown arg '%s'\n", a.c_str());
    usage(2);
  }
  if (opts.rules_path.empty() || opts.output_dir.empty()) usage(2);
  if (opts.types.empty()) {
    std::fprintf(stderr, "litmus-gen: --types must be non-empty\n");
    usage(2);
  }
  return opts;
}

std::string readFile(const std::string &path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::ostringstream os;
  os << f.rdbuf();
  return os.str();
}

void writeFile(const std::string &path, const std::string &body) {
  std::ofstream f(path);
  if (!f) throw std::runtime_error("cannot open for write: " + path);
  f << body;
}

bool ensureDir(const std::string &path) {
  if (path.empty()) return false;
  std::string acc;
  for (size_t i = 0; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/') {
      if (!acc.empty() && acc != ".") {
        if (mkdir(acc.c_str(), 0755) != 0 && errno != EEXIST) return false;
      }
      if (i < path.size()) acc.push_back('/');
    } else {
      acc.push_back(path[i]);
    }
  }
  return true;
}

bool verifyIR(const std::string &ir, const std::string &where) {
  llvm::LLVMContext ctx;
  llvm::SMDiagnostic err;
  auto buf = llvm::MemoryBuffer::getMemBuffer(ir, where, /*RequiresNul=*/false);
  auto m = llvm::parseIR(*buf, err, ctx);
  if (!m) {
    err.print("litmus-gen", llvm::errs());
    llvm::errs() << "--- offending IR (" << where << ") ---\n" << ir
                 << "------\n";
    return false;
  }
  std::string verr;
  llvm::raw_string_ostream verrs(verr);
  if (llvm::verifyModule(*m, &verrs)) {
    llvm::errs() << "litmus-gen: verifyModule failed for " << where << ": "
                 << verr << "\n--- offending IR ---\n" << ir << "------\n";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  Options opts = parseArgs(argc, argv);

  std::string text;
  try {
    text = readFile(opts.rules_path);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "litmus-gen: %s\n", e.what());
    return 1;
  }

  regatoni::litmus::RuleDatabase db;
  try {
    db = regatoni::litmus::parseDatabase(text);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "litmus-gen: %s\n", e.what());
    return 1;
  }

  if (!ensureDir(opts.output_dir)) {
    std::fprintf(stderr, "litmus-gen: cannot create output dir %s\n",
                 opts.output_dir.c_str());
    return 1;
  }

  // Per-instruction accounting for the end-of-run summary.
  std::map<std::string, size_t> gen_count;
  std::map<std::string, std::string> zero_reason;  // why this instruction produced nothing
  size_t total_gen = 0, total_failed = 0;

  for (const auto &inst : db.instructions) {
    gen_count[inst.name] = 0;  // ensure every instruction appears

    std::vector<std::string> types =
        regatoni::litmus::typesForInstruction(inst, opts.types);
    if (types.empty()) {
      zero_reason[inst.name] = "no compatible types (out of V1 scope)";
      continue;
    }
    auto gen = regatoni::litmus::findGenerator(inst);
    if (!gen) {
      zero_reason[inst.name] = "no generator for this instruction";
      continue;
    }
    for (const auto &rule : inst.rules) {
      for (const auto &type : types) {
        auto files = gen(inst, rule, type);
        if (files.empty()) continue;  // generator declined this (rule, type)
        for (const auto &nf : files) {
          std::string name = rule.id + "." + type;
          if (!nf.suffix.empty()) name += "." + nf.suffix;
          name += ".ll";
          if (!verifyIR(nf.body, name)) {
            ++total_failed;
            continue;
          }
          try {
            writeFile(opts.output_dir + "/" + name, nf.body);
          } catch (const std::exception &e) {
            std::fprintf(stderr, "litmus-gen: %s\n", e.what());
            ++total_failed;
            continue;
          }
          ++gen_count[inst.name];
          ++total_gen;
        }
      }
    }
    if (gen_count[inst.name] == 0)
      zero_reason[inst.name] =
          "generator declined every (rule, type) combination";
  }

  // ---- end-of-run summary ----
  std::fprintf(stderr, "\n[litmus-gen] per-instruction breakdown:\n");
  for (const auto &kv : gen_count) {
    if (kv.second > 0)
      std::fprintf(stderr, "  %-30s %5zu files\n", kv.first.c_str(), kv.second);
  }
  if (!zero_reason.empty()) {
    std::fprintf(stderr, "\n[litmus-gen] zero-output instructions:\n");
    for (const auto &kv : zero_reason) {
      std::fprintf(stderr, "  %-30s %s\n", kv.first.c_str(), kv.second.c_str());
    }
  }
  std::fprintf(stderr,
               "\n[litmus-gen] generated=%zu failed=%zu (rules JSON: %s)\n",
               total_gen, total_failed, opts.rules_path.c_str());
  return total_failed == 0 ? 0 : 1;
}
