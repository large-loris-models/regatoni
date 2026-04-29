#!/usr/bin/env python3
"""Auto-discover LLVM opt passes in a Centipede coverage report and report
per-pass coverage, bucketed by pass category.

Passes are categorized into buckets (O2 Pipeline, Analysis, Sanitizers,
Codegen, LTO, Specialized, Utilities, Unknown). For each pass we report both
Touched% ((FULL+PARTIAL)/Total) and Full% (FULL/Total).

Pass attribution uses LLVM's PassRegistry.def as the source of truth: each
source file is mapped to the pass class whose symbols it implements. Helper
files (no pass class symbol) inherit the pass attribution of their directory
when that directory contains files for exactly one pass class.

The O2 pipeline set is extracted at startup from
deps/llvm-project/llvm/lib/Passes/PassBuilderPipelines.cpp. If parsing
fails, a hardcoded fallback list is used.
"""

import argparse
import re
import sys
from collections import Counter
from glob import glob
from pathlib import Path

MIN_TOTAL = 5
MIN_NAME_LEN = 3

STATUS_LINE_RE = re.compile(r'^(FULL|PARTIAL|NONE): (.*)$')
PATH_RE        = re.compile(r' (/\S+?):(\d+)(?::\d+)?$')

STATUS_IDX = {'FULL': 0, 'PARTIAL': 1, 'NONE': 2}

# Subset of O2 passes that operate intraprocedurally (single function at a
# time). These are the passes Alive2 can verify, so when --intra is set we
# narrow the O2 Pipeline view to just this set. Includes both the base name
# and the *Pass-suffixed variant so it matches either form produced by the
# scanner.
_INTRA_BASE = {
    'SLPVectorizer', 'MergeICmps', 'ExpandMemCmp', 'SpeculativeExecution',
    'InstSimplify', 'SimpleLoopUnswitch', 'LoopSimplifyCFG', 'LoopUnroll',
    'LoopLoadElimination', 'LICM', 'GVN', 'ADCE', 'Float2Int', 'SROA',
    'JumpThreading', 'EarlyCSE', 'LoopVectorize', 'LoopDistribute',
    'Reassociate', 'ConstraintElimination', 'LoopDeletion', 'LoopIdiomRecognize',
    'IndVarSimplify', 'MemCpyOpt', 'InstCombine', 'AggressiveInstCombine',
    'VectorCombine', 'SimplifyCFG', 'CorrelatedValuePropagation',
    'AlignmentFromAssumptions', 'LibCallsShrinkWrap', 'LoopUnrollPass',
    'SimplifyCFGPass', 'InstSimplifyPass',
}
INTRAPROCEDURAL_PASSES = _INTRA_BASE | {n + 'Pass' for n in _INTRA_BASE if not n.endswith('Pass')}

# ---------------------------------------------------------------------------
# O2 pipeline extraction from PassBuilderPipelines.cpp
# ---------------------------------------------------------------------------

PIPELINE_SRC = 'deps/llvm-project/llvm/lib/Passes/PassBuilderPipelines.cpp'

# Functions that build the default (non-LTO) O2 pipeline, transitively.
O2_PIPELINE_FUNCS = {
    'buildPerModuleDefaultPipeline',
    'buildModuleSimplificationPipeline',
    'buildModuleOptimizationPipeline',
    'buildInlinerPipeline',
    'buildModuleInlinerPipeline',
    'buildFunctionSimplificationPipeline',
    'buildO1FunctionSimplificationPipeline',
    'addVectorPasses',
    'addPreInlinerPasses',
    'addPGOInstrPasses',
    'addPostPGOLoopRotation',
}

# Passes used by the default pipeline that do not carry a "Pass" suffix in
# their C++ class name. The main regex only picks up "*Pass(" callsites.
O2_NO_SUFFIX_ALLOWLIST = {
    'PGOMemOPSizeOpt',
    'PGOInstrumentationGen',
    'PGOInstrumentationUse',
    'PGOInstrumentationGenCreateVar',
    'PGOIndirectCallPromotion',
    'MemProfRemoveInfo',
    'NoinlineNonPrevailing',
    'InjectTLIMappings',
}

# Used if source parsing fails.
FALLBACK_O2_PASSES = {
    'SROA', 'EarlyCSE', 'SimplifyCFG', 'InstCombine', 'AggressiveInstCombine',
    'LibCallsShrinkWrap', 'TailCallElim', 'Reassociate', 'LoopInstSimplify',
    'LoopSimplifyCFG', 'LICM', 'LoopRotate', 'SimpleLoopUnswitch',
    'LoopIdiomRecognize', 'IndVarSimplify', 'LoopDeletion', 'LoopFullUnroll',
    'VectorCombine', 'MergedLoadStoreMotion', 'GVN', 'SCCP', 'BDCE',
    'JumpThreading', 'CorrelatedValuePropagation', 'ADCE', 'MemCpyOpt', 'DSE',
    'MoveAutoInit', 'CoroElide', 'LoopVectorize', 'LoopUnroll', 'SLPVectorizer',
    'LoopLoadElimination', 'InferAlignment', 'LoopSink', 'InstSimplify',
    'DivRemPairs', 'MergeICmps', 'ExpandMemCmp', 'GlobalDCE', 'GlobalOpt',
    'IPSCCP', 'CalledValuePropagation', 'DeadArgumentElimination',
    'InferFunctionAttrs', 'ReversePostOrderFunctionAttrs',
    'PostOrderFunctionAttrs', 'Promote', 'AlwaysInliner', 'ModuleInlinerWrapper',
    'ModuleInliner', 'OpenMPOpt', 'OpenMPOptCGSCC', 'Annotation2Metadata',
    'ForceFunctionAttrs', 'ConstantMerge', 'RelLookupTableConverter',
    'EliminateAvailableExternally', 'Float2Int', 'LowerConstantIntrinsics',
    'LoopDistribute', 'WarnMissedTransformations', 'LoopUnrollAndJam',
    'AlignmentFromAssumptions', 'CoroEarly', 'CoroCleanup', 'CoroSplit',
    'CoroAnnotationElide', 'LowerExpectIntrinsic', 'SpeculativeExecution',
    'LowerTypeTests', 'EntryExitInstrumenter', 'MemProfRemoveInfo',
    'CountVisits', 'AssumeSimplify', 'GVNHoist', 'GVNSink',
    'JumpTableToSwitch', 'ConstraintElimination', 'LoopFlatten',
    'CallSiteSplitting', 'DFAJumpThreading', 'DropUnnecessaryAssumes',
    'AllocToken', 'CGProfile', 'RecomputeGlobalsAA', 'SimplifyTypeTests',
    'PartialInliner', 'InstCombine', 'NewGVN', 'AttributorCGSCC',
    'AttributorLightCGSCC', 'ArgumentPromotion', 'AttributorLight',
    'Attributor', 'PGOForceFunctionAttrs', 'SampleProfileProbe',
    'SampleProfileLoader', 'MemProfUse', 'AssignGUID', 'PGOCtxProfLowering',
    'PGOCtxProfFlattening', 'InstrProfilingLowering', 'AddDiscriminators',
    'NoinlineNonPrevailing', 'PGOMemOPSizeOpt', 'PGOInstrumentationGen',
    'PGOInstrumentationUse', 'PGOInstrumentationGenCreateVar',
    'PGOIndirectCallPromotion', 'InjectTLIMappings',
}

# Captures ClassNamePass( or ClassNamePass<...>( — strips "Pass" suffix.
ADDPASS_CLASS_RE = re.compile(r'\b([A-Z][A-Za-z0-9_]*?)Pass(?:<[^>]*>)?\s*\(')
# Captures addPass(ClassName( for no-suffix passes (filtered by allowlist).
ADDPASS_BARE_RE  = re.compile(
    r'addPass\(\s*([A-Z][A-Za-z0-9_]*)\s*\('
)


def _find_function_body(src: str, func_name: str) -> str | None:
    """Return the body (text inside { ... }) of PassBuilder::func_name, or None."""
    key = f'PassBuilder::{func_name}'
    start = src.find(key)
    if start < 0:
        return None
    # Find first '{' after the signature.
    i = src.find('{', start)
    if i < 0:
        return None
    depth = 0
    end = -1
    for j in range(i, len(src)):
        c = src[j]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                end = j
                break
    if end < 0:
        return None
    return src[i + 1 : end]


def extract_o2_pipeline(repo_root: Path) -> tuple[set[str], str]:
    """Parse PassBuilderPipelines.cpp for the O2 pipeline pass set.

    Returns (set_of_pass_names, source_description).
    """
    path = repo_root / PIPELINE_SRC
    if not path.is_file():
        return set(FALLBACK_O2_PASSES), f'fallback ({path} not found)'
    try:
        src = path.read_text(errors='replace')
    except OSError:
        return set(FALLBACK_O2_PASSES), f'fallback (read error: {path})'

    found: set[str] = set()
    parsed_funcs = 0
    for func in O2_PIPELINE_FUNCS:
        body = _find_function_body(src, func)
        if body is None:
            continue
        parsed_funcs += 1
        for name in ADDPASS_CLASS_RE.findall(body):
            if len(name) >= MIN_NAME_LEN:
                found.add(name)
        for name in ADDPASS_BARE_RE.findall(body):
            if name in O2_NO_SUFFIX_ALLOWLIST:
                found.add(name)

    if parsed_funcs == 0 or not found:
        return set(FALLBACK_O2_PASSES), 'fallback (parse produced no passes)'

    # Always include the no-suffix allowlist: some of these appear only via
    # callbacks or conditionals we might miss.
    found.update(O2_NO_SUFFIX_ALLOWLIST)
    return found, f'parsed {parsed_funcs}/{len(O2_PIPELINE_FUNCS)} builder functions'


# ---------------------------------------------------------------------------
# PassRegistry.def parsing — source of truth for pass class names
# ---------------------------------------------------------------------------

PASS_REGISTRY_DEF = 'deps/llvm-project/llvm/lib/Passes/PassRegistry.def'

# Matches lines like:
#   MODULE_PASS("globalopt", GlobalOptPass())
#   FUNCTION_ANALYSIS("target-ir", TargetIRAnalysis())
#   FUNCTION_PASS_WITH_PARAMS("instcombine", "InstCombinePass", ...)
# Whitespace around args may include newlines (some entries span lines).
PASS_REGISTRY_RE = re.compile(
    r'^\s*(?:MODULE|FUNCTION|CGSCC|LOOP|LOOPNEST)_'
    r'(?:ALIAS_)?'
    r'(?:PASS|ANALYSIS)'
    r'(?:_WITH_PARAMS)?'
    r'\s*\(\s*"[^"]+"\s*,\s*"?'
    r'([A-Z][A-Za-z0-9_]*)',
    re.MULTILINE,
)


def parse_pass_registry(repo_root: Path) -> tuple[dict[str, str], str]:
    """Parse PassRegistry.def. Returns (class_to_base, source_description).

    class_to_base maps a C++ class name as it appears in symbols (e.g.
    'SLPVectorizerPass', 'NoinlineNonPrevailing') to a display base name with
    the Pass/Analysis suffix stripped (e.g. 'SLPVectorizer').
    """
    path = repo_root / PASS_REGISTRY_DEF
    if not path.is_file():
        return {}, f'not found: {path}'
    try:
        src = path.read_text(errors='replace')
    except OSError as e:
        return {}, f'read error: {e}'
    class_to_base: dict[str, str] = {}
    for m in PASS_REGISTRY_RE.finditer(src):
        cls = m.group(1)
        if cls.endswith('Pass') and len(cls) > 4:
            base = cls[:-4]
        elif cls.endswith('Analysis') and len(cls) > 8:
            base = cls[:-8]
        else:
            base = cls
        class_to_base[cls] = base
    return class_to_base, f'parsed {len(class_to_base)} class entries'


def build_symbol_class_re(class_to_base: dict[str, str]) -> re.Pattern | None:
    if not class_to_base:
        return None
    # Sort longest-first so alternation prefers the longest matching name.
    names = sorted(class_to_base.keys(), key=len, reverse=True)
    pattern = r'\bllvm::(' + '|'.join(re.escape(n) for n in names) + r')::'
    return re.compile(pattern)


def extract_method_owner_classes(
    symbol: str,
    class_to_base: dict[str, str],
    symbol_class_re: re.Pattern,
) -> list[str]:
    """Extract pass classes whose method this symbol IS — not classes that
    merely appear as template args or return types.

    A coverage symbol looks like `<return_type> <qualified_name>(<args>)`. We
    want the class chain of the function being defined, e.g. for
    `bool llvm::function_ref<...llvm::SLPVectorizerPass::run...>::callback_fn(...)`
    the owner is `function_ref`, not `SLPVectorizerPass`.

    Strategy: trim at the first '(' at angle-bracket depth 0 (drop args),
    keep only depth-0 characters (drop everything inside <>), then take the
    last whitespace-separated token — that's `qualified_name` of the
    function. Match registered class names within just that token.
    """
    depth = 0
    cut = len(symbol)
    for i, c in enumerate(symbol):
        if c == '<':
            depth += 1
        elif c == '>':
            if depth > 0:
                depth -= 1
        elif c == '(' and depth == 0:
            cut = i
            break
    head = symbol[:cut]

    parts: list[str] = []
    depth = 0
    for c in head:
        if c == '<':
            depth += 1
        elif c == '>':
            if depth > 0:
                depth -= 1
        elif depth == 0:
            parts.append(c)
    qualified = ''.join(parts).split()
    if not qualified:
        return []
    last = qualified[-1]
    return [class_to_base[m.group(1)] for m in symbol_class_re.finditer(last)]


# ---------------------------------------------------------------------------
# Bucket classification
# ---------------------------------------------------------------------------

ANALYSIS_EXACT = {
    'DominatorTree', 'MemorySSA', 'LoopInfo', 'BasicAA', 'ScalarEvolution',
    'AAResults', 'CallGraph', 'LazyCallGraph', 'DemandedBits',
    'BranchProbabilityInfo', 'BlockFrequencyInfo', 'PostDominatorTree',
    'AssumptionCache', 'TargetTransformInfo', 'TargetLibraryInfo',
    'LazyValueInfo', 'CycleAnalysis', 'RegionInfo', 'CFLAndersAAResult',
    'CFLSteensAAResult', 'ScopedNoAliasAA', 'TypeBasedAA', 'GlobalsAA',
    'MemoryDependence', 'MustExecute', 'PhiValues', 'PredicateInfo',
    'ProfileSummaryInfo', 'SyntheticCountsUtils', 'TargetTransformInfoImpl',
    'TargetTransformInfoWrapper', 'FunctionPropertiesAnalysis',
    'CtxProfAnalysis', 'InlineAdvisor', 'InlineSizeEstimatorAnalysis',
    'ModuleSummaryAnalysis', 'IRSimilarityIdentifier', 'OptimizationRemark',
    'OptimizationRemarkEmitter', 'LoopAccessInfo', 'StackSafety', 'Delinearization',
    'ReplayInlineAdvisor',
}

SANITIZER_PREFIXES = (
    'PGOInstrumentation', 'InstrProfiling', 'GCOVProfiler', 'MemProf',
    'HWAddress', 'NumericalStability',
)
SANITIZER_EXACT = {
    'SanitizerCoverage', 'DataFlowSanitizer', 'TypeSanitizer', 'BoundsChecking',
}

CODEGEN_PREFIXES = ('Machine', 'RegAlloc', 'MIR')

LTO_EXACT = {
    'WholeProgramDevirt', 'LowerTypeTests', 'FunctionImport', 'IROutliner',
    'ThinLTOBitcodeWriter', 'MergeFunctions', 'CrossDSOCFI', 'LowerAllowCheck',
}

SPECIALIZED_PREFIXES = ('Coro', 'ObjCARC', 'SampleProfile')
SPECIALIZED_EXACT = {
    'Debugify', 'CFGuard', 'StackSafety', 'GuardWidening',
}

UTILITIES_EXACT = {
    'Local', 'Utils', 'Scalar', 'IPO', 'Instrumentation', 'Vectorize',
    'Transforms', 'CloneFunction', 'BuildLibCalls', 'SimplifyLibCalls',
    'LoopUtils', 'BasicBlockUtils', 'ValueMapper', 'LCSSA',
    'BreakCriticalEdges', 'LoopSimplify', 'ModuleUtils', 'FunctionUtils',
    'CodeExtractor', 'PromoteMemoryToRegister', 'InlineFunction',
    'LoopRotationUtils', 'LoopVersioning', 'ScalarEvolutionExpander',
    'MemoryTaggingSupport', 'SpillUtils', 'Evaluator', 'FunctionComparator',
    'LoopConstrainer', 'SizeOpts', 'SymbolRewriter',
}
UTILITIES_PREFIXES = ('SSAUpdater', 'VPlan')

BUCKETS_ORDER = [
    'O2 Pipeline',
    'Analysis',
    'Sanitizers/Instrumentation',
    'Codegen/Backend',
    'LTO',
    'Specialized',
    'Utilities',
    'Unknown',
]

# Short names for --bucket flag.
BUCKET_ALIASES = {
    'o2': 'O2 Pipeline',
    'analysis': 'Analysis',
    'sanitizers': 'Sanitizers/Instrumentation',
    'sanitizer': 'Sanitizers/Instrumentation',
    'instrumentation': 'Sanitizers/Instrumentation',
    'codegen': 'Codegen/Backend',
    'backend': 'Codegen/Backend',
    'lto': 'LTO',
    'specialized': 'Specialized',
    'utilities': 'Utilities',
    'utils': 'Utilities',
    'unknown': 'Unknown',
}


def classify(name: str, o2_set: set[str]) -> str:
    # Filename-derived keys can carry a literal "Pass" suffix (e.g.
    # "SimplifyCFGPass" from SimplifyCFGPass.cpp). Also try the stripped
    # variant against the O2 set so it buckets with its real pass name.
    stripped = name[:-4] if name.endswith('Pass') and len(name) > 4 else None
    if name in o2_set or (stripped and stripped in o2_set):
        return 'O2 Pipeline'
    if (name in ANALYSIS_EXACT or
            name.endswith('Analysis') or
            name.endswith('Wrapper') or
            name.endswith('Info')):
        return 'Analysis'
    if 'Sanitizer' in name or name in SANITIZER_EXACT:
        return 'Sanitizers/Instrumentation'
    for p in SANITIZER_PREFIXES:
        if name.startswith(p):
            return 'Sanitizers/Instrumentation'
    for p in CODEGEN_PREFIXES:
        if name.startswith(p):
            return 'Codegen/Backend'
    if name in LTO_EXACT:
        return 'LTO'
    if name in SPECIALIZED_EXACT:
        return 'Specialized'
    for p in SPECIALIZED_PREFIXES:
        if name.startswith(p):
            return 'Specialized'
    if name in UTILITIES_EXACT or (stripped and stripped in UTILITIES_EXACT):
        return 'Utilities'
    for p in UTILITIES_PREFIXES:
        if name.startswith(p):
            return 'Utilities'
    return 'Unknown'


# ---------------------------------------------------------------------------
# Report discovery / parsing
# ---------------------------------------------------------------------------

def find_report(arg):
    if arg:
        return arg
    for suffix in ('final', 'initial'):
        candidates = glob(
            f'build/workdir_*/coverage-report-opt_fuzz_target.000000.{suffix}.txt'
        )
        if candidates:
            return max(candidates, key=lambda p: Path(p).stat().st_mtime)
    return None


def scan_report(
    report_path: str,
    symbol_class_re: re.Pattern | None,
    class_to_base: dict[str, str],
) -> tuple[dict, dict, int, int, int]:
    """Scan the coverage report.

    Returns (file_counters, file_class_hits, full, partial, none).
      - file_counters: file_path -> [F, P, N]
      - file_class_hits: file_path -> Counter({base_name: count})
    """
    file_counters: dict[str, list[int]] = {}
    file_class_hits: dict[str, Counter] = {}
    full_total = partial_total = none_total = 0

    status_match = STATUS_LINE_RE.match
    path_search  = PATH_RE.search

    with open(report_path, 'r', errors='replace') as f:
        for raw in f:
            m = status_match(raw)
            if not m:
                continue
            status = m.group(1)
            rest = m.group(2)

            if status == 'FULL':
                full_total += 1
            elif status == 'PARTIAL':
                partial_total += 1
            else:
                none_total += 1

            pm = path_search(rest)
            if not pm:
                continue
            file_path = pm.group(1)

            ctr = file_counters.get(file_path)
            if ctr is None:
                ctr = [0, 0, 0]
                file_counters[file_path] = ctr
            ctr[STATUS_IDX[status]] += 1

            if symbol_class_re is None:
                continue
            symbol = rest[:pm.start()]
            for base in extract_method_owner_classes(
                symbol, class_to_base, symbol_class_re
            ):
                hits = file_class_hits.get(file_path)
                if hits is None:
                    hits = Counter()
                    file_class_hits[file_path] = hits
                hits[base] += 1

    return file_counters, file_class_hits, full_total, partial_total, none_total


def aggregate_by_pass(
    file_counters: dict[str, list[int]],
    file_class_hits: dict[str, Counter],
) -> dict[str, list[int]]:
    """Roll up per-file counters into per-pass counters.

    Attribution priority:
      1. Direct: file has a registered pass class symbol -> dominant class.
      2. Directory: file has no class symbols, but every classified file in
         the same directory maps to a single pass class -> inherit it.
         (Matches the InstCombine/ shape; rejects mixed dirs like Scalar/.)
      3. Fallback: filename basename (legacy behavior).
    """
    file_to_pass: dict[str, str] = {}
    dir_classes: dict[str, set[str]] = {}

    for fp, hits in file_class_hits.items():
        if not hits:
            continue
        base, _ = hits.most_common(1)[0]
        file_to_pass[fp] = base
        d = str(Path(fp).parent)
        dir_classes.setdefault(d, set()).add(base)

    counters: dict[str, list[int]] = {}
    for fp, ctr in file_counters.items():
        name = file_to_pass.get(fp)
        if name is None:
            d = str(Path(fp).parent)
            classes = dir_classes.get(d)
            if classes and len(classes) == 1:
                name = next(iter(classes))
            else:
                stem = Path(fp).stem
                if len(stem) < MIN_NAME_LEN:
                    continue
                name = stem
        c = counters.get(name)
        if c is None:
            c = [0, 0, 0]
            counters[name] = c
        c[0] += ctr[0]
        c[1] += ctr[1]
        c[2] += ctr[2]
    return counters


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def format_pct(x: float) -> str:
    return f"{int(round(x))}%"


def print_bucket_table(bucket: str, rows: list[tuple]) -> None:
    print(f"=== {bucket} ({len(rows)} passes) ===")
    if not rows:
        print("(no passes in this bucket)")
        print()
        return
    header = (
        f"{'Pass':<32} {'Touched%':>8}  {'Full%':>5}  "
        f"{'Full':>4}  {'Partial':>7}  {'None':>5}  {'Total':>5}"
    )
    sep = (
        f"{'-----':<32} {'--------':>8}  {'-----':>5}  "
        f"{'----':>4}  {'-------':>7}  {'-----':>5}  {'-----':>5}"
    )
    print(header)
    print(sep)
    for name, f, p, n, total, touched, full_pct in rows:
        print(
            f"{name:<32} {format_pct(touched):>8}  {format_pct(full_pct):>5}  "
            f"{f:>4}  {p:>7}  {n:>5}  {total:>5}"
        )
    print()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('report', nargs='?', help='Path to coverage report')
    ap.add_argument('--bucket', help='Show only one bucket (o2, analysis, sanitizers, codegen, lto, specialized, utilities, unknown)')
    ap.add_argument('--summary', action='store_true', help='Show only the category summary')
    ap.add_argument('--all', action='store_true', help='Show all bucket detail tables')
    ap.add_argument(
        '--intraprocedural', '--intra', dest='intraprocedural',
        action='store_true',
        help='Restrict the O2 Pipeline view to intraprocedural passes that '
             'Alive2 can verify',
    )
    ap.add_argument(
        '--by-uncovered', dest='by_uncovered', action='store_true',
        help='Sort pass tables by absolute NONE count (descending) instead '
             'of by Touched%%',
    )
    args = ap.parse_args()

    repo_root = Path.cwd()
    o2_set, o2_source = extract_o2_pipeline(repo_root)
    print(f"O2 pipeline: {len(o2_set)} passes ({o2_source})")

    class_to_base, registry_source = parse_pass_registry(repo_root)
    print(f"Pass registry: {registry_source}")
    symbol_class_re = build_symbol_class_re(class_to_base)

    report = find_report(args.report)
    if not report or not Path(report).is_file():
        sys.stderr.write(f"Usage: {sys.argv[0]} [coverage-report]\n")
        sys.stderr.write("No coverage report found under build/workdir_*/\n")
        return 1

    file_counters, file_class_hits, full_total, partial_total, none_total = (
        scan_report(report, symbol_class_re, class_to_base)
    )
    counters = aggregate_by_pass(file_counters, file_class_hits)

    # Build per-pass rows: (name, F, P, N, Total, Touched%, Full%)
    rows_by_bucket: dict[str, list[tuple]] = {b: [] for b in BUCKETS_ORDER}
    for name, (f, p, n) in counters.items():
        total = f + p + n
        if total < MIN_TOTAL:
            continue
        touched = (f + p) * 100.0 / total
        full_pct = f * 100.0 / total
        bucket = classify(name, o2_set)
        rows_by_bucket[bucket].append(
            (name, f, p, n, total, touched, full_pct)
        )

    o2_label = 'O2 Pipeline'
    if args.intraprocedural:
        o2_label = 'Intraprocedural O2 Passes'
        rows_by_bucket['O2 Pipeline'] = [
            r for r in rows_by_bucket['O2 Pipeline']
            if r[0] in INTRAPROCEDURAL_PASSES
        ]
        rows_by_bucket[o2_label] = rows_by_bucket.pop('O2 Pipeline')

    if args.by_uncovered:
        sort_key = lambda r: (-r[3], -r[4])  # none desc, then total desc
    else:
        sort_key = lambda r: (r[5], -r[4])   # touched asc, then total desc
    for bucket, rows in rows_by_bucket.items():
        rows.sort(key=sort_key)

    covered = full_total + partial_total
    instr_total = full_total + partial_total + none_total

    print()
    print("=== Pass Coverage Report ===")
    print(f"Report: {report}")
    if instr_total > 0:
        pct = covered * 100.0 / instr_total
        print(f"Total functions: {covered}/{instr_total} ({pct:.1f}%)")
    else:
        print("Total functions: 0/0")
    print()

    print("=== Summary by Category ===")
    header = (
        f"{'Category':<30} {'Passes':>6}  {'Avg Touched%':>12}  "
        f"{'Avg Full%':>9}  {'Zero-coverage':>14}"
    )
    print(header)
    print(
        f"{'-'*30:<30} {'------':>6}  {'------------':>12}  "
        f"{'---------':>9}  {'-------------':>14}"
    )
    bucket_order = [
        o2_label if b == 'O2 Pipeline' else b for b in BUCKETS_ORDER
    ]
    for bucket in bucket_order:
        rows = rows_by_bucket[bucket]
        npasses = len(rows)
        if npasses == 0:
            print(f"{bucket:<30} {0:>6}  {'-':>12}  {'-':>9}  {'-':>14}")
            continue
        avg_touched = sum(r[5] for r in rows) / npasses
        avg_full = sum(r[6] for r in rows) / npasses
        zeroes = sum(1 for r in rows if r[5] == 0)
        print(
            f"{bucket:<30} {npasses:>6}  {format_pct(avg_touched):>12}  "
            f"{format_pct(avg_full):>9}  {zeroes:>14}"
        )
    print()

    if args.summary:
        return 0

    # Determine which bucket(s) to print in detail.
    if args.bucket:
        key = args.bucket.lower()
        target = BUCKET_ALIASES.get(key, args.bucket)
        if target == 'O2 Pipeline':
            target = o2_label
        if target not in rows_by_bucket:
            sys.stderr.write(f"Unknown bucket '{args.bucket}'. Choose from:\n")
            for name in BUCKETS_ORDER:
                sys.stderr.write(f"  {name}\n")
            return 2
        print_bucket_table(target, rows_by_bucket[target])
        return 0

    if args.all:
        for bucket in bucket_order:
            print_bucket_table(bucket, rows_by_bucket[bucket])
        return 0

    # Default: just O2 Pipeline detail (most interesting).
    print_bucket_table(o2_label, rows_by_bucket[o2_label])
    print("(use --all for all buckets, --bucket <name> for a specific one, --summary to hide details)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
