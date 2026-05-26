// corpus-analysis — structural metrics for a directory of .ll files.
//
// Reads every .ll file under --corpus-dir (or every path listed in
// --file-list), analyses each function, and writes a JSON summary
// (aggregate to stdout / --output, optional per-file JSONL alongside).
//
// CFG / instruction / type / metadata / flag / use metrics are computed
// via LLVM's IR API. Loop detection is iterative-DFS back-edge counting,
// not LoopInfo, since this tool runs across ~100k files and we want the
// cheapest correct answer.

#include "deps/nlohmann-json/json.hpp"

#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// ----------------------------------------------------------------------
// Per-function metrics.
// ----------------------------------------------------------------------
struct FnMetrics {
  std::string file;
  std::string fn;

  // CFG
  unsigned num_basic_blocks = 0;
  bool     has_loops = false;
  unsigned num_back_edges = 0;
  bool     has_phi = false;
  unsigned num_phi = 0;
  unsigned max_cfg_depth = 0;
  bool     has_diamond = false;
  bool     has_switch = false;
  unsigned num_branches = 0;

  // Instruction mix
  unsigned total_instructions = 0;
  std::map<std::string, unsigned> opcode_counts;
  unsigned num_calls = 0;
  std::set<std::string> intrinsic_names;
  unsigned num_loads = 0;
  unsigned num_stores = 0;
  unsigned num_allocas = 0;
  unsigned num_geps = 0;
  bool     has_vector_ops = false;

  // Type diversity
  std::set<std::string> types_used;
  bool has_fp = false;
  bool has_vectors = false;
  bool has_pointers = false;
  bool has_aggregates = false;

  // Metadata + attributes
  bool has_metadata = false;
  std::set<std::string> metadata_kinds;
  std::set<std::string> param_attrs;
  std::set<std::string> fn_attrs;

  // Flags
  std::map<std::string, unsigned> flag_counts;

  // Use / value-flow complexity
  unsigned max_use_count = 0;
  double   avg_use_count = 0.0;
  bool     has_multi_use = false;
  unsigned longest_def_use_chain = 0;
};

// ----------------------------------------------------------------------
// Convenience helpers.
// ----------------------------------------------------------------------
std::string typeToString(llvm::Type *T) {
  std::string s;
  llvm::raw_string_ostream os(s);
  T->print(os);
  return os.str();
}

// Returns a clean attribute name suitable for histogramming.
// Trims trailing "(value)" and surrounding quotes on string attrs.
std::string attrName(const llvm::Attribute &a) {
  std::string s = a.getAsString();
  // Drop value tail: "byval(i8)" -> "byval", "align 16" -> "align".
  auto i = s.find_first_of(" (");
  if (i != std::string::npos) s = s.substr(0, i);
  // Strip surrounding quotes from string attrs.
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

void recordType(llvm::Type *T, FnMetrics &m) {
  if (!T) return;
  m.types_used.insert(typeToString(T));
  if (T->isFloatingPointTy() ||
      (T->isVectorTy() &&
       llvm::cast<llvm::VectorType>(T)->getElementType()->isFloatingPointTy()))
    m.has_fp = true;
  if (T->isVectorTy()) m.has_vectors = true;
  if (T->isPointerTy()) m.has_pointers = true;
  if (T->isAggregateType()) m.has_aggregates = true;
}

// ----------------------------------------------------------------------
// CFG analysis. BFS depth from entry, iterative DFS for back-edges,
// successor-pair intersection for diamond detection.
// ----------------------------------------------------------------------
void analyzeCFG(const llvm::Function &F, FnMetrics &m) {
  using BBPtr = const llvm::BasicBlock *;
  if (F.empty()) return;

  m.num_basic_blocks = F.size();
  BBPtr entry = &F.getEntryBlock();

  // BFS depth from entry.
  std::unordered_map<BBPtr, int> dist;
  dist.reserve(F.size() * 2);
  dist[entry] = 0;
  std::vector<BBPtr> bfs;
  bfs.reserve(F.size());
  bfs.push_back(entry);
  for (size_t i = 0; i < bfs.size(); ++i) {
    BBPtr b = bfs[i];
    int d = dist[b];
    auto *term = b->getTerminator();
    if (!term) continue;
    for (unsigned k = 0; k < term->getNumSuccessors(); ++k) {
      BBPtr s = term->getSuccessor(k);
      if (!dist.count(s)) {
        dist[s] = d + 1;
        bfs.push_back(s);
      }
    }
  }
  for (auto &kv : dist)
    if (kv.second >= 0 &&
        static_cast<unsigned>(kv.second) > m.max_cfg_depth)
      m.max_cfg_depth = static_cast<unsigned>(kv.second);

  // Iterative DFS — count back-edges (target currently on stack).
  // 0 = unvisited, 1 = on stack (gray), 2 = finished (black).
  std::unordered_map<BBPtr, int> color;
  color.reserve(F.size() * 2);
  for (auto &BB : F) color[&BB] = 0;

  struct Frame {
    BBPtr bb;
    unsigned next_succ;
  };
  std::vector<Frame> stack;
  stack.push_back({entry, 0});
  color[entry] = 1;
  while (!stack.empty()) {
    Frame &top = stack.back();
    auto *term = top.bb->getTerminator();
    unsigned ns = term ? term->getNumSuccessors() : 0;
    if (top.next_succ < ns) {
      BBPtr s = term->getSuccessor(top.next_succ++);
      int &c = color[s];
      if (c == 0) {
        c = 1;
        stack.push_back({s, 0});
      } else if (c == 1) {
        // Back-edge: top.bb -> s where s is an ancestor on the DFS stack.
        ++m.num_back_edges;
      }
    } else {
      color[top.bb] = 2;
      stack.pop_back();
    }
  }
  m.has_loops = m.num_back_edges > 0;

  // Diamond detection: any BB with >=2 successors whose successor sets
  // intersect (i.e. paths reconverge in one step). Cheap, conservative —
  // doesn't catch deeper joins, but those imply a join nonetheless and
  // we'd flag the immediate predecessor first.
  for (auto &BB : F) {
    auto *term = BB.getTerminator();
    if (!term || term->getNumSuccessors() < 2) continue;
    unsigned ns = term->getNumSuccessors();
    for (unsigned i = 0; i < ns && !m.has_diamond; ++i) {
      auto *si = term->getSuccessor(i);
      auto *sit = si->getTerminator();
      unsigned si_ns = sit ? sit->getNumSuccessors() : 0;
      for (unsigned j = i + 1; j < ns && !m.has_diamond; ++j) {
        auto *sj = term->getSuccessor(j);
        if (si == sj) { m.has_diamond = true; break; }
        // Check si's successors against sj.
        for (unsigned a = 0; a < si_ns && !m.has_diamond; ++a) {
          auto *si_succ = sit->getSuccessor(a);
          if (si_succ == sj) { m.has_diamond = true; break; }
          auto *sjt = sj->getTerminator();
          unsigned sj_ns = sjt ? sjt->getNumSuccessors() : 0;
          for (unsigned b = 0; b < sj_ns; ++b) {
            if (si_succ == sjt->getSuccessor(b)) {
              m.has_diamond = true;
              break;
            }
          }
        }
      }
    }
    if (m.has_diamond) break;
  }
}

// ----------------------------------------------------------------------
// Instruction + flags + types + metadata + attrs.
// ----------------------------------------------------------------------
void analyzeInstructions(const llvm::Function &F, FnMetrics &m) {
  using namespace llvm;

  // Cache MD kind id -> name once per function (cheap; names are
  // context-wide stable and the table is small).
  SmallVector<StringRef, 16> md_kind_names;
  F.getContext().getMDKindNames(md_kind_names);

  // Function-level attributes.
  AttributeList AL = F.getAttributes();
  AttributeSet FnAS = AL.getFnAttrs();
  for (const auto &a : FnAS) m.fn_attrs.insert(attrName(a));

  AttributeSet RetAS = AL.getRetAttrs();
  for (const auto &a : RetAS) m.param_attrs.insert(attrName(a));
  for (unsigned i = 0, e = F.arg_size(); i < e; ++i) {
    AttributeSet PAS = AL.getParamAttrs(i);
    for (const auto &a : PAS) m.param_attrs.insert(attrName(a));
  }

  // Function signature types — record return + args even if function is empty.
  recordType(F.getReturnType(), m);
  for (auto &arg : F.args()) recordType(arg.getType(), m);

  // Per-instruction walk. Capture insts in order to compute SSA chain depth.
  std::vector<const Instruction *> ordered;
  ordered.reserve(64);

  for (const BasicBlock &BB : F) {
    for (const Instruction &I : BB) {
      ++m.total_instructions;
      ordered.push_back(&I);

      const char *op = I.getOpcodeName();
      ++m.opcode_counts[op ? op : "<unknown>"];

      // Result + operand types.
      recordType(I.getType(), m);
      if (I.getType()->isVectorTy()) m.has_vector_ops = true;
      for (unsigned k = 0, e = I.getNumOperands(); k < e; ++k) {
        if (auto *opnd = I.getOperand(k))
          recordType(opnd->getType(), m);
      }

      // Per-kind tallies.
      if (isa<PHINode>(I)) { m.has_phi = true; ++m.num_phi; }
      if (I.getOpcode() == Instruction::UncondBr ||
          I.getOpcode() == Instruction::CondBr)
        ++m.num_branches;
      if (isa<SwitchInst>(I)) m.has_switch = true;
      if (isa<LoadInst>(I)) ++m.num_loads;
      if (isa<StoreInst>(I)) ++m.num_stores;
      if (isa<AllocaInst>(I)) ++m.num_allocas;
      if (isa<GetElementPtrInst>(I)) ++m.num_geps;

      // Calls + intrinsics.
      if (const auto *CB = dyn_cast<CallBase>(&I)) {
        // Skip pure llvm.dbg.* intrinsics? Include them — they tell us
        // whether debug intrinsics survived. Don't strip.
        ++m.num_calls;
        if (const Function *Callee = CB->getCalledFunction()) {
          if (Callee->isIntrinsic()) {
            m.intrinsic_names.insert(Callee->getName().str());
          }
        }
        // Call-site attributes contribute to param_attrs.
        AttributeList CL = CB->getAttributes();
        for (unsigned i = 0, e = CB->arg_size(); i < e; ++i) {
          AttributeSet PAS = CL.getParamAttrs(i);
          for (const auto &a : PAS) m.param_attrs.insert(attrName(a));
        }
      }

      // Flags.
      if (const auto *OBO = dyn_cast<OverflowingBinaryOperator>(&I)) {
        if (OBO->hasNoSignedWrap())   ++m.flag_counts["nsw"];
        if (OBO->hasNoUnsignedWrap()) ++m.flag_counts["nuw"];
      }
      if (const auto *PEO = dyn_cast<PossiblyExactOperator>(&I)) {
        if (PEO->isExact()) ++m.flag_counts["exact"];
      }
      if (const auto *GEPO = dyn_cast<GEPOperator>(&I)) {
        if (GEPO->isInBounds()) ++m.flag_counts["inbounds"];
      }
      if (isa<FPMathOperator>(&I)) {
        FastMathFlags FMF = I.getFastMathFlags();
        if (FMF.noNaNs())         ++m.flag_counts["nnan"];
        if (FMF.noInfs())         ++m.flag_counts["ninf"];
        if (FMF.noSignedZeros())  ++m.flag_counts["nsz"];
        if (FMF.allowReciprocal()) ++m.flag_counts["arcp"];
        if (FMF.allowContract())  ++m.flag_counts["contract"];
        if (FMF.approxFunc())     ++m.flag_counts["afn"];
        if (FMF.allowReassoc())   ++m.flag_counts["reassoc"];
      }

      // Metadata kinds attached to this instruction.
      llvm::SmallVector<std::pair<unsigned, MDNode *>, 4> mds;
      I.getAllMetadata(mds);
      if (!mds.empty()) m.has_metadata = true;
      for (auto &p : mds) {
        if (p.first < md_kind_names.size())
          m.metadata_kinds.insert(md_kind_names[p.first].str());
        else
          m.metadata_kinds.insert("kind#" + std::to_string(p.first));
      }
    }
  }

  // Derived flag bools.
  for (auto &kv : m.flag_counts) {
    if (kv.first == "nsw" || kv.first == "nuw" || kv.first == "exact" ||
        kv.first == "inbounds") {
      // Tracked via flag_counts; no separate bool field needed.
    }
  }

  // Use complexity. Iterate again (or use ordered list) and count uses.
  if (!ordered.empty()) {
    unsigned long total_uses = 0;
    for (const Instruction *I : ordered) {
      unsigned uc = I->getNumUses();
      total_uses += uc;
      if (uc > m.max_use_count) m.max_use_count = uc;
      if (uc > 1) m.has_multi_use = true;
    }
    m.avg_use_count = double(total_uses) / double(ordered.size());

    // Def-use chain depth: longest path in the DAG where edges go
    // operand -> user. We compute it as longest path in BB order: each
    // instruction's depth = 1 + max(depth(op) for op in operands that
    // are instructions in the same function). Back operands from PHI
    // nodes get depth 0 (not yet computed), which underestimates but
    // is stable.
    llvm::DenseMap<const Instruction *, unsigned> depth;
    depth.reserve(ordered.size() * 2);
    for (const Instruction *I : ordered) {
      unsigned d = 1;
      for (unsigned k = 0, e = I->getNumOperands(); k < e; ++k) {
        if (const auto *op = llvm::dyn_cast<Instruction>(I->getOperand(k))) {
          auto it = depth.find(op);
          if (it != depth.end()) d = std::max(d, it->second + 1);
        }
      }
      depth[I] = d;
      if (d > m.longest_def_use_chain) m.longest_def_use_chain = d;
    }
  }
}

FnMetrics analyzeFunction(const std::string &path, const llvm::Function &F) {
  FnMetrics m;
  m.file = path;
  m.fn = F.getName().str();
  analyzeCFG(F, m);
  analyzeInstructions(F, m);
  return m;
}

// ----------------------------------------------------------------------
// JSON serialisation.
// ----------------------------------------------------------------------
json toJson(const FnMetrics &m) {
  json j;
  j["file"] = m.file;
  j["fn"]   = m.fn;

  j["cfg"] = {
    {"num_basic_blocks", m.num_basic_blocks},
    {"has_loops",        m.has_loops},
    {"num_back_edges",   m.num_back_edges},
    {"has_phi",          m.has_phi},
    {"num_phi",          m.num_phi},
    {"max_cfg_depth",    m.max_cfg_depth},
    {"has_diamond",      m.has_diamond},
    {"has_switch",       m.has_switch},
    {"num_branches",     m.num_branches},
  };

  j["inst"] = {
    {"total_instructions", m.total_instructions},
    {"num_calls",          m.num_calls},
    {"num_loads",          m.num_loads},
    {"num_stores",         m.num_stores},
    {"num_allocas",        m.num_allocas},
    {"num_geps",           m.num_geps},
    {"has_calls",          m.num_calls > 0},
    {"has_intrinsics",     !m.intrinsic_names.empty()},
    {"has_memory_ops",
        m.num_loads + m.num_stores + m.num_allocas > 0},
    {"has_vector_ops",     m.has_vector_ops},
    {"intrinsic_names",
        std::vector<std::string>(m.intrinsic_names.begin(),
                                 m.intrinsic_names.end())},
    {"opcode_counts",      m.opcode_counts},
  };

  j["types"] = {
    {"has_fp",          m.has_fp},
    {"has_vectors",     m.has_vectors},
    {"has_pointers",    m.has_pointers},
    {"has_aggregates",  m.has_aggregates},
    {"types_used",
        std::vector<std::string>(m.types_used.begin(),
                                 m.types_used.end())},
  };

  j["metadata"] = {
    {"has_metadata", m.has_metadata},
    {"kinds",
        std::vector<std::string>(m.metadata_kinds.begin(),
                                 m.metadata_kinds.end())},
  };

  j["attrs"] = {
    {"param_attrs",
        std::vector<std::string>(m.param_attrs.begin(),
                                 m.param_attrs.end())},
    {"fn_attrs",
        std::vector<std::string>(m.fn_attrs.begin(),
                                 m.fn_attrs.end())},
  };

  j["flags"] = {
    {"counts",       m.flag_counts},
    {"has_nsw",      m.flag_counts.count("nsw") > 0},
    {"has_nuw",      m.flag_counts.count("nuw") > 0},
    {"has_exact",    m.flag_counts.count("exact") > 0},
    {"has_inbounds", m.flag_counts.count("inbounds") > 0},
    {"has_fast_math",
        m.flag_counts.count("nnan") + m.flag_counts.count("ninf") +
        m.flag_counts.count("nsz")  + m.flag_counts.count("arcp") +
        m.flag_counts.count("contract") + m.flag_counts.count("afn") +
        m.flag_counts.count("reassoc") > 0},
  };

  j["uses"] = {
    {"max_use_count",         m.max_use_count},
    {"avg_use_count",         m.avg_use_count},
    {"has_multi_use",         m.has_multi_use},
    {"longest_def_use_chain", m.longest_def_use_chain},
  };

  return j;
}

// ----------------------------------------------------------------------
// Aggregation.
// ----------------------------------------------------------------------
struct Aggregate {
  std::string corpus_path;
  size_t num_files = 0;
  size_t num_failed = 0;
  size_t num_functions = 0;

  // Structural-bool counts.
  size_t fn_single_bb = 0;
  size_t fn_has_loops = 0;
  size_t fn_has_phi = 0;
  size_t fn_has_diamond = 0;
  size_t fn_has_calls = 0;
  size_t fn_has_intrinsics = 0;
  size_t fn_has_memory = 0;
  size_t fn_has_vectors = 0;
  size_t fn_has_fp = 0;
  size_t fn_has_pointers = 0;
  size_t fn_has_aggregates = 0;
  size_t fn_has_metadata = 0;
  size_t fn_has_multi_use = 0;
  size_t fn_has_switch = 0;
  size_t fn_has_nsw = 0;
  size_t fn_has_nuw = 0;
  size_t fn_has_exact = 0;
  size_t fn_has_inbounds = 0;
  size_t fn_has_fast_math = 0;

  // Numeric sums for averages.
  unsigned long sum_instructions = 0;
  unsigned long sum_basic_blocks = 0;
  unsigned long sum_branches = 0;
  unsigned long sum_num_back_edges = 0;
  unsigned long sum_max_cfg_depth = 0;
  unsigned long sum_num_phi = 0;

  // Histograms.
  std::map<std::string, unsigned long> opcode_hist;
  std::map<std::string, unsigned long> type_hist;
  std::map<std::string, unsigned long> flag_hist;
  std::map<std::string, unsigned long> metadata_hist;
  std::map<std::string, unsigned long> intrinsic_hist;
  std::map<std::string, unsigned long> fn_attr_hist;
  std::map<std::string, unsigned long> param_attr_hist;

  // Distributions (bucketed function counts).
  std::map<std::string, unsigned long> dist_insts;
  std::map<std::string, unsigned long> dist_bbs;
  std::map<std::string, unsigned long> dist_loops;
};

static const char *bucketInst(unsigned n) {
  if (n <= 2)  return "1-2";
  if (n <= 5)  return "3-5";
  if (n <= 10) return "6-10";
  if (n <= 20) return "11-20";
  if (n <= 50) return "21-50";
  return "51+";
}
static const char *bucketBB(unsigned n) {
  if (n == 1)  return "1";
  if (n == 2)  return "2";
  if (n <= 5)  return "3-5";
  if (n <= 10) return "6-10";
  return "11+";
}
static const char *bucketLoops(unsigned n) {
  if (n == 0)  return "0";
  if (n == 1)  return "1";
  if (n <= 3)  return "2-3";
  return "4+";
}

void accumulate(Aggregate &agg, const FnMetrics &m) {
  ++agg.num_functions;
  if (m.num_basic_blocks == 1) ++agg.fn_single_bb;
  if (m.has_loops)    ++agg.fn_has_loops;
  if (m.has_phi)      ++agg.fn_has_phi;
  if (m.has_diamond)  ++agg.fn_has_diamond;
  if (m.num_calls > 0)            ++agg.fn_has_calls;
  if (!m.intrinsic_names.empty()) ++agg.fn_has_intrinsics;
  if (m.num_loads + m.num_stores + m.num_allocas > 0) ++agg.fn_has_memory;
  if (m.has_vector_ops || m.has_vectors) ++agg.fn_has_vectors;
  if (m.has_fp)          ++agg.fn_has_fp;
  if (m.has_pointers)    ++agg.fn_has_pointers;
  if (m.has_aggregates)  ++agg.fn_has_aggregates;
  if (m.has_metadata)    ++agg.fn_has_metadata;
  if (m.has_multi_use)   ++agg.fn_has_multi_use;
  if (m.has_switch)      ++agg.fn_has_switch;
  if (m.flag_counts.count("nsw"))      ++agg.fn_has_nsw;
  if (m.flag_counts.count("nuw"))      ++agg.fn_has_nuw;
  if (m.flag_counts.count("exact"))    ++agg.fn_has_exact;
  if (m.flag_counts.count("inbounds")) ++agg.fn_has_inbounds;
  if (m.flag_counts.count("nnan") || m.flag_counts.count("ninf") ||
      m.flag_counts.count("nsz")  || m.flag_counts.count("arcp") ||
      m.flag_counts.count("contract") || m.flag_counts.count("afn") ||
      m.flag_counts.count("reassoc"))
    ++agg.fn_has_fast_math;

  agg.sum_instructions   += m.total_instructions;
  agg.sum_basic_blocks   += m.num_basic_blocks;
  agg.sum_branches       += m.num_branches;
  agg.sum_num_back_edges += m.num_back_edges;
  agg.sum_max_cfg_depth  += m.max_cfg_depth;
  agg.sum_num_phi        += m.num_phi;

  for (auto &kv : m.opcode_counts) agg.opcode_hist[kv.first] += kv.second;
  for (auto &t  : m.types_used)    agg.type_hist[t] += 1;
  for (auto &kv : m.flag_counts)   agg.flag_hist[kv.first] += kv.second;
  for (auto &k  : m.metadata_kinds) agg.metadata_hist[k] += 1;
  for (auto &n  : m.intrinsic_names) agg.intrinsic_hist[n] += 1;
  for (auto &a  : m.fn_attrs)        agg.fn_attr_hist[a] += 1;
  for (auto &a  : m.param_attrs)     agg.param_attr_hist[a] += 1;

  agg.dist_insts[bucketInst(m.total_instructions)] += 1;
  agg.dist_bbs[bucketBB(m.num_basic_blocks)] += 1;
  agg.dist_loops[bucketLoops(m.num_back_edges)] += 1;
}

double pct(size_t num, size_t denom) {
  if (denom == 0) return 0.0;
  return double(num) * 100.0 / double(denom);
}

json aggregateToJson(const Aggregate &agg) {
  json j;
  j["corpus_path"]   = agg.corpus_path;
  j["num_files"]     = agg.num_files;
  j["num_failed"]    = agg.num_failed;
  j["num_functions"] = agg.num_functions;

  const size_t F = agg.num_functions;

  j["structural_profile"] = {
    {"single_bb_pct",       pct(agg.fn_single_bb, F)},
    {"has_loops_pct",       pct(agg.fn_has_loops, F)},
    {"has_phi_pct",         pct(agg.fn_has_phi, F)},
    {"has_diamond_pct",     pct(agg.fn_has_diamond, F)},
    {"has_switch_pct",      pct(agg.fn_has_switch, F)},
    {"has_calls_pct",       pct(agg.fn_has_calls, F)},
    {"has_intrinsics_pct",  pct(agg.fn_has_intrinsics, F)},
    {"has_memory_pct",      pct(agg.fn_has_memory, F)},
    {"has_vectors_pct",     pct(agg.fn_has_vectors, F)},
    {"has_fp_pct",          pct(agg.fn_has_fp, F)},
    {"has_pointers_pct",    pct(agg.fn_has_pointers, F)},
    {"has_aggregates_pct",  pct(agg.fn_has_aggregates, F)},
    {"has_metadata_pct",    pct(agg.fn_has_metadata, F)},
    {"has_multi_use_pct",   pct(agg.fn_has_multi_use, F)},
    {"has_nsw_pct",         pct(agg.fn_has_nsw, F)},
    {"has_nuw_pct",         pct(agg.fn_has_nuw, F)},
    {"has_exact_pct",       pct(agg.fn_has_exact, F)},
    {"has_inbounds_pct",    pct(agg.fn_has_inbounds, F)},
    {"has_fast_math_pct",   pct(agg.fn_has_fast_math, F)},
    {"avg_instructions",
        F == 0 ? 0.0 : double(agg.sum_instructions) / double(F)},
    {"avg_basic_blocks",
        F == 0 ? 0.0 : double(agg.sum_basic_blocks) / double(F)},
    {"avg_branches",
        F == 0 ? 0.0 : double(agg.sum_branches) / double(F)},
    {"avg_back_edges",
        F == 0 ? 0.0 : double(agg.sum_num_back_edges) / double(F)},
    {"avg_max_cfg_depth",
        F == 0 ? 0.0 : double(agg.sum_max_cfg_depth) / double(F)},
    {"avg_num_phi",
        F == 0 ? 0.0 : double(agg.sum_num_phi) / double(F)},
  };

  j["instruction_histogram"] = agg.opcode_hist;
  j["type_histogram"]        = agg.type_hist;
  j["flag_histogram"]        = agg.flag_hist;
  j["metadata_histogram"]    = agg.metadata_hist;
  j["intrinsic_histogram"]   = agg.intrinsic_hist;
  j["fn_attr_histogram"]     = agg.fn_attr_hist;
  j["param_attr_histogram"]  = agg.param_attr_hist;

  j["distributions"] = {
    {"instructions_per_function", agg.dist_insts},
    {"basic_blocks_per_function", agg.dist_bbs},
    {"back_edges_per_function",   agg.dist_loops},
  };
  return j;
}

// ----------------------------------------------------------------------
// File enumeration + worker pool.
// ----------------------------------------------------------------------
void collectLLFiles(const fs::path &root, std::vector<std::string> &out) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root,
        fs::directory_options::skip_permission_denied, ec), end;
       it != end; it.increment(ec)) {
    if (ec) { ec.clear(); continue; }
    if (!it->is_regular_file(ec)) { ec.clear(); continue; }
    const fs::path &p = it->path();
    if (p.extension() == ".ll") out.push_back(p.string());
  }
}

void readFileList(const std::string &path, std::vector<std::string> &out) {
  std::ifstream in(path);
  if (!in) {
    std::fprintf(stderr, "corpus-analysis: cannot open file list %s\n",
                 path.c_str());
    std::exit(1);
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    // Trim trailing whitespace.
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                              line.back() == '\t'))
      line.pop_back();
    if (!line.empty()) out.push_back(line);
  }
}

struct WorkerResult {
  Aggregate agg;
  std::vector<json> per_file;  // populated only if per_file enabled
  size_t files_done = 0;
  size_t files_failed = 0;
};

void worker(const std::vector<std::string> &paths,
            size_t begin, size_t end,
            bool per_file,
            std::atomic<size_t> *progress,
            WorkerResult *out) {
  llvm::LLVMContext Ctx;

  for (size_t i = begin; i < end; ++i) {
    const std::string &p = paths[i];
    llvm::SMDiagnostic Err;
    auto M = llvm::parseIRFile(p, Err, Ctx);
    ++out->files_done;
    if (progress) progress->fetch_add(1, std::memory_order_relaxed);
    if (!M) {
      ++out->files_failed;
      // Per-file parse failures are common in heterogeneous corpora
      // (.ll-features sidecars, etc); print only when single-threaded
      // or first few to keep stderr useful without flooding it.
      if (out->files_failed <= 5) {
        std::string s;
        llvm::raw_string_ostream os(s);
        Err.print("corpus-analysis", os);
        std::fprintf(stderr, "[parse fail] %s\n", s.c_str());
      }
      continue;
    }

    json per_file_obj;
    bool have_obj = false;
    std::vector<json> per_file_fns;

    for (const llvm::Function &F : *M) {
      if (F.isDeclaration()) continue;
      FnMetrics m = analyzeFunction(p, F);
      accumulate(out->agg, m);
      if (per_file) {
        per_file_fns.push_back(toJson(m));
        have_obj = true;
      }
    }
    if (per_file && have_obj) {
      per_file_obj["file"] = p;
      per_file_obj["functions"] = per_file_fns;
      out->per_file.push_back(std::move(per_file_obj));
    }
  }
}

void merge(Aggregate &dst, const Aggregate &src) {
  dst.num_functions     += src.num_functions;
  dst.fn_single_bb      += src.fn_single_bb;
  dst.fn_has_loops      += src.fn_has_loops;
  dst.fn_has_phi        += src.fn_has_phi;
  dst.fn_has_diamond    += src.fn_has_diamond;
  dst.fn_has_calls      += src.fn_has_calls;
  dst.fn_has_intrinsics += src.fn_has_intrinsics;
  dst.fn_has_memory     += src.fn_has_memory;
  dst.fn_has_vectors    += src.fn_has_vectors;
  dst.fn_has_fp         += src.fn_has_fp;
  dst.fn_has_pointers   += src.fn_has_pointers;
  dst.fn_has_aggregates += src.fn_has_aggregates;
  dst.fn_has_metadata   += src.fn_has_metadata;
  dst.fn_has_multi_use  += src.fn_has_multi_use;
  dst.fn_has_switch     += src.fn_has_switch;
  dst.fn_has_nsw        += src.fn_has_nsw;
  dst.fn_has_nuw        += src.fn_has_nuw;
  dst.fn_has_exact      += src.fn_has_exact;
  dst.fn_has_inbounds   += src.fn_has_inbounds;
  dst.fn_has_fast_math  += src.fn_has_fast_math;
  dst.sum_instructions   += src.sum_instructions;
  dst.sum_basic_blocks   += src.sum_basic_blocks;
  dst.sum_branches       += src.sum_branches;
  dst.sum_num_back_edges += src.sum_num_back_edges;
  dst.sum_max_cfg_depth  += src.sum_max_cfg_depth;
  dst.sum_num_phi        += src.sum_num_phi;
  for (auto &kv : src.opcode_hist)    dst.opcode_hist[kv.first]    += kv.second;
  for (auto &kv : src.type_hist)      dst.type_hist[kv.first]      += kv.second;
  for (auto &kv : src.flag_hist)      dst.flag_hist[kv.first]      += kv.second;
  for (auto &kv : src.metadata_hist)  dst.metadata_hist[kv.first]  += kv.second;
  for (auto &kv : src.intrinsic_hist) dst.intrinsic_hist[kv.first] += kv.second;
  for (auto &kv : src.fn_attr_hist)   dst.fn_attr_hist[kv.first]   += kv.second;
  for (auto &kv : src.param_attr_hist) dst.param_attr_hist[kv.first] += kv.second;
  for (auto &kv : src.dist_insts)     dst.dist_insts[kv.first]     += kv.second;
  for (auto &kv : src.dist_bbs)       dst.dist_bbs[kv.first]       += kv.second;
  for (auto &kv : src.dist_loops)     dst.dist_loops[kv.first]     += kv.second;
}

// ----------------------------------------------------------------------
// CLI.
// ----------------------------------------------------------------------
struct Opts {
  std::string corpus_dir;
  std::string file_list;
  std::string output;
  bool per_file = false;
  unsigned threads = 0;
  bool quiet = false;
};

void usage() {
  std::fprintf(stderr,
    "Usage: corpus-analysis [--corpus-dir DIR | --file-list FILE]\n"
    "                       [--output PATH.json] [--per-file]\n"
    "                       [--threads N] [--quiet]\n");
}

bool parse(int argc, char **argv, Opts &o) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char *flag) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s: missing argument\n", flag);
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--corpus-dir")      o.corpus_dir = next("--corpus-dir");
    else if (a == "--file-list")  o.file_list  = next("--file-list");
    else if (a == "--output")     o.output     = next("--output");
    else if (a == "--per-file")   o.per_file   = true;
    else if (a == "--threads")    o.threads    = std::stoi(next("--threads"));
    else if (a == "--quiet")      o.quiet      = true;
    else if (a == "-h" || a == "--help") { usage(); std::exit(0); }
    else {
      std::fprintf(stderr, "unknown flag: %s\n", a.c_str());
      usage();
      return false;
    }
  }
  if (o.corpus_dir.empty() && o.file_list.empty()) {
    std::fprintf(stderr, "--corpus-dir or --file-list required\n");
    usage();
    return false;
  }
  if (!o.corpus_dir.empty() && !o.file_list.empty()) {
    std::fprintf(stderr, "--corpus-dir and --file-list are mutually exclusive\n");
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  Opts opts;
  if (!parse(argc, argv, opts)) return 2;

  // Gather paths.
  std::vector<std::string> paths;
  if (!opts.corpus_dir.empty()) {
    fs::path root(opts.corpus_dir);
    if (!fs::exists(root)) {
      std::fprintf(stderr, "corpus dir not found: %s\n", opts.corpus_dir.c_str());
      return 1;
    }
    collectLLFiles(root, paths);
  } else {
    readFileList(opts.file_list, paths);
  }
  if (!opts.quiet)
    std::fprintf(stderr, "[corpus-analysis] %zu .ll files\n", paths.size());

  // Split into worker partitions.
  unsigned T = opts.threads;
  if (T == 0) T = std::max(1u, std::thread::hardware_concurrency());
  if (T > paths.size()) T = std::max<unsigned>(1, paths.size());
  std::vector<WorkerResult> results(T);
  std::vector<std::thread> threads;
  threads.reserve(T);

  std::atomic<size_t> progress{0};
  std::atomic<bool> done{false};
  std::thread progress_thread;
  if (!opts.quiet && paths.size() > 1000) {
    progress_thread = std::thread([&]() {
      while (!done.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        size_t p = progress.load(std::memory_order_relaxed);
        std::fprintf(stderr, "[corpus-analysis] %zu / %zu files\n",
                     p, paths.size());
      }
    });
  }

  size_t chunk = (paths.size() + T - 1) / T;
  for (unsigned t = 0; t < T; ++t) {
    size_t b = std::min(paths.size(), size_t(t) * chunk);
    size_t e = std::min(paths.size(), b + chunk);
    threads.emplace_back(worker, std::cref(paths), b, e,
                         opts.per_file, &progress, &results[t]);
  }
  for (auto &th : threads) th.join();
  done.store(true);
  if (progress_thread.joinable()) progress_thread.join();

  // Merge.
  Aggregate total;
  total.corpus_path = opts.corpus_dir.empty() ? opts.file_list : opts.corpus_dir;
  total.num_files = paths.size();
  for (auto &r : results) {
    merge(total, r.agg);
    total.num_failed += r.files_failed;
  }

  // Emit aggregate JSON.
  json out = aggregateToJson(total);
  std::string s = out.dump(2);
  std::printf("%s\n", s.c_str());

  if (!opts.output.empty()) {
    std::ofstream of(opts.output);
    if (!of) {
      std::fprintf(stderr, "cannot write %s\n", opts.output.c_str());
      return 1;
    }
    of << s << "\n";
    if (!opts.quiet)
      std::fprintf(stderr, "[corpus-analysis] wrote %s\n",
                   opts.output.c_str());
  }

  if (opts.per_file) {
    std::string pf;
    if (!opts.output.empty()) pf = opts.output + ".per-file.jsonl";
    else                      pf = "corpus-analysis.per-file.jsonl";
    std::ofstream of(pf);
    if (!of) {
      std::fprintf(stderr, "cannot write %s\n", pf.c_str());
      return 1;
    }
    for (auto &r : results)
      for (auto &j : r.per_file)
        of << j.dump() << "\n";
    if (!opts.quiet)
      std::fprintf(stderr, "[corpus-analysis] wrote %s\n", pf.c_str());
  }

  if (!opts.quiet) {
    std::fprintf(stderr,
      "[corpus-analysis] %zu files (%zu failed), %zu functions\n",
      total.num_files, total.num_failed, total.num_functions);
  }
  return 0;
}
