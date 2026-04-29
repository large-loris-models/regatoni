#!/usr/bin/env python3
"""extract_callgraph.py — lightweight C++ call-graph extractor.

Parses a single C++ source file, finds function definitions, and for every
call site within each body emits the callee name, line number, and the text
of the innermost enclosing `if`/`for`/`while`/`switch` condition.

Intentionally textual / regex-based, not a full C++ parser — it covers
direct named calls (the structural majority of SLPVectorizer.cpp), tags
lambdas as anonymous children of their enclosing function, and skips
common keywords. Virtual calls, template instantiation, and dependent
qualified names are not resolved — those are noise we accept in exchange
for not pulling in a clang AST dependency.

Output JSON shape:

    {
      "ClassName::funcName": {
        "start_line": 1234,
        "end_line": 1300,
        "calls": [
          {"callee": "otherFunc", "line": 1240, "condition": "if (x > 0)", "condition_line": 1238}
        ],
        "lambdas": ["ClassName::funcName::lambda@1245", ...]
      }
    }
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Optional


# Identifiers we never treat as callees (control-flow / type / built-ins).
KEYWORDS = {
    "if", "for", "while", "switch", "do", "else", "case", "default",
    "return", "break", "continue", "goto", "throw", "try", "catch",
    "sizeof", "alignof", "alignas", "typeid", "typeof", "decltype",
    "static_cast", "dynamic_cast", "reinterpret_cast", "const_cast",
    "new", "delete", "true", "false", "nullptr", "this", "auto",
    "namespace", "using", "typedef", "template", "typename", "class",
    "struct", "union", "enum", "public", "private", "protected",
    "virtual", "override", "final", "explicit", "friend", "operator",
    "constexpr", "consteval", "constinit", "inline", "noexcept",
    "static", "extern", "register", "thread_local", "mutable", "volatile",
    "const", "void", "bool", "char", "int", "long", "short", "signed",
    "unsigned", "float", "double", "wchar_t", "char16_t", "char32_t",
    "and", "or", "not", "xor", "bitand", "bitor", "compl", "and_eq",
    "or_eq", "xor_eq", "not_eq", "co_await", "co_return", "co_yield",
    "requires", "concept",
}


def strip_comments_and_strings(src: str) -> str:
    """Replace contents of // line comments, /* */ block comments, "..." strings,
    '...' chars, and raw strings with spaces, preserving newlines so that
    offset → line mapping stays accurate."""
    out: list[str] = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        # Line comment
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
            continue
        # Block comment
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                out.append("\n" if src[k] == "\n" else " ")
            i = j
            continue
        # Raw string R"delim(...)delim"
        if c == "R" and i + 1 < n and src[i + 1] == '"':
            j = src.find("(", i + 2)
            if j > 0:
                delim = src[i + 2 : j]
                close = ')' + delim + '"'
                k = src.find(close, j + 1)
                k = n if k < 0 else k + len(close)
                for m in range(i, k):
                    out.append("\n" if src[m] == "\n" else " ")
                i = k
                continue
        # String / char literal
        if c == '"' or c == "'":
            quote = c
            j = i + 1
            while j < n:
                if src[j] == "\\" and j + 1 < n:
                    j += 2
                    continue
                if src[j] == quote:
                    j += 1
                    break
                j += 1
            for k in range(i, j):
                out.append("\n" if src[k] == "\n" else " ")
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)


def make_line_index(src: str) -> list[int]:
    """Return cumulative-newline-offset table for fast offset → line."""
    line_offsets = [0]
    for i, c in enumerate(src):
        if c == "\n":
            line_offsets.append(i + 1)
    return line_offsets


def offset_to_line(line_offsets: list[int], offset: int) -> int:
    """1-based line number for an offset."""
    # Binary search
    lo, hi = 0, len(line_offsets) - 1
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if line_offsets[mid] <= offset:
            lo = mid
        else:
            hi = mid - 1
    return lo + 1


# Trailing post-signature qualifiers that may appear between `)` and `{`.
# Order matters — match longest first per scan.
_TRAILING_KEYWORD_RE = re.compile(
    r"\b(?:const|noexcept(?:\s*\([^)]*\))?|override|final|throw\s*\([^)]*\)|"
    r"->\s*[^{;]+?)\s*$"
)


def _strip_trailing_qualifiers(text: str) -> str:
    """Eat trailing const/noexcept/override/final/-> trailing-return-type."""
    prev = None
    while prev != text:
        prev = text
        text = _TRAILING_KEYWORD_RE.sub("", text).rstrip()
    return text


def find_function_bodies(stripped: str) -> list[dict]:
    """Walk the (comment-stripped) source and locate function definitions.

    A function body is any `{ ... }` whose opener is preceded by a signature
    `)` (possibly with const/noexcept/override/final/-> trailing return).
    Returns a flat list of {name, qualified_name, body_start, body_end,
    start_line, end_line}. Lambdas are detected separately during the body
    scan, not here.
    """
    funcs: list[dict] = []
    n = len(stripped)
    i = 0
    paren_depth = 0
    bracket_depth = 0
    # Each stack entry: {open, is_func, name, qualified_name}
    stack: list[dict] = []

    while i < n:
        c = stripped[i]
        if c == "(":
            paren_depth += 1
            i += 1
            continue
        if c == ")":
            paren_depth -= 1
            i += 1
            continue
        if c == "[":
            bracket_depth += 1
            i += 1
            continue
        if c == "]":
            bracket_depth -= 1
            i += 1
            continue
        if c == "{" and paren_depth == 0 and bracket_depth == 0:
            entry = _classify_open_brace(stripped, i)
            entry["open"] = i
            # Inherit class/namespace path from parent frames
            class_path = []
            for f in stack:
                if f.get("kind") == "class" and f.get("class_name"):
                    class_path.append(f["class_name"])
            if entry.get("is_func") and class_path and "::" not in entry["qualified_name"]:
                entry["qualified_name"] = "::".join(class_path) + "::" + entry["qualified_name"]
            stack.append(entry)
            i += 1
            continue
        if c == "}" and paren_depth == 0 and bracket_depth == 0:
            if stack:
                top = stack.pop()
                if top.get("is_func"):
                    funcs.append(
                        {
                            "name": top["name"],
                            "qualified_name": top["qualified_name"],
                            "body_start": top["open"] + 1,
                            "body_end": i,
                        }
                    )
            i += 1
            continue
        i += 1

    return funcs


_CLASS_RE = re.compile(
    r"\b(?:class|struct)\s+([A-Za-z_]\w*)\s*"
    r"(?:final\s*)?"
    r"(?::[^{;]+)?"
    r"\s*$"
)
_NAMESPACE_RE = re.compile(r"\bnamespace\s*([A-Za-z_]\w*)?\s*$")


def _skip_initializer_list(stripped: str, j: int) -> int:
    """If `stripped[j] == ')'` is the trailing `)` of a member-initializer
    in a constructor, walk back past the entire init list and return the
    index of the constructor signature's closing `)`. Otherwise return
    `j` unchanged.

    Pattern recognised: `Name(BALANCED) [, Name(BALANCED)]* :  ...)  {`
    where the leading `:` signals the start of a constructor's init list.
    """
    save = j
    cur = j
    while True:
        # Walk back over balanced ')'..'('
        if cur < 0 or stripped[cur] != ")":
            return save
        depth = 1
        k = cur - 1
        while k >= 0 and depth > 0:
            if stripped[k] == ")":
                depth += 1
            elif stripped[k] == "(":
                depth -= 1
            k -= 1
        if depth != 0:
            return save
        # Skip whitespace before '('
        m = k
        while m >= 0 and stripped[m].isspace():
            m -= 1
        # Walk back over the identifier (possibly with template args)
        if m >= 0 and stripped[m] == ">":
            td = 1
            m -= 1
            while m >= 0 and td > 0:
                if stripped[m] == ">":
                    td += 1
                elif stripped[m] == "<":
                    td -= 1
                m -= 1
        while m >= 0 and (stripped[m].isalnum() or stripped[m] in "_:"):
            m -= 1
        # Now stripped[m] is the char before this init-list element's name.
        # Skip whitespace
        while m >= 0 and stripped[m].isspace():
            m -= 1
        if m < 0:
            return save
        if stripped[m] == ":":
            # Watch out for `::` qualifier — only a single ':' starts an
            # init list. If preceded by another ':' it's a scope qualifier
            # and we should NOT treat the prior structure as init list.
            if m > 0 and stripped[m - 1] == ":":
                return save
            # Found init list start. The signature `)` is whatever comes
            # before this ':' (skipping whitespace).
            n_idx = m - 1
            while n_idx >= 0 and stripped[n_idx].isspace():
                n_idx -= 1
            return n_idx
        if stripped[m] == ",":
            # Another init-list element follows; keep walking back.
            cur = m - 1
            while cur >= 0 and stripped[cur].isspace():
                cur -= 1
            continue
        # Anything else: not an init list.
        return save


def _classify_open_brace(stripped: str, brace_pos: int) -> dict:
    """Look back from `{` to decide whether this is a function body opening,
    a class/struct body, a namespace body, or other (init list / scope)."""
    j = brace_pos - 1
    while j >= 0 and stripped[j].isspace():
        j -= 1
    if j < 0:
        return {"is_func": False, "name": None, "qualified_name": None,
                "kind": "other"}

    # Look back at a moderate window for class/namespace opener
    window_start = max(0, j - 200)
    window = stripped[window_start : j + 1]

    # Class / struct / namespace body
    cls_m = _CLASS_RE.search(window)
    if cls_m:
        return {
            "is_func": False, "name": None, "qualified_name": None,
            "kind": "class", "class_name": cls_m.group(1),
        }
    ns_m = _NAMESPACE_RE.search(window)
    if ns_m:
        return {
            "is_func": False, "name": None, "qualified_name": None,
            "kind": "namespace",
            "namespace_name": ns_m.group(1) or "(anonymous namespace)",
        }
    # extern "C" { ... } or other named scope
    if re.search(r'\bextern\s+"[^"]*"\s*$', window):
        return {"is_func": False, "name": None, "qualified_name": None,
                "kind": "extern"}
    # Walk back over whitespace + trailing qualifiers
    j = brace_pos - 1
    while j >= 0 and stripped[j].isspace():
        j -= 1

    # Strip trailing keywords like const/noexcept/override/final
    look = max(0, j - 80)
    tail = stripped[look : j + 1]
    cleaned = _strip_trailing_qualifiers(tail)
    drop = len(tail) - len(cleaned)
    j -= drop
    while j >= 0 and stripped[j].isspace():
        j -= 1

    if j < 0 or stripped[j] != ")":
        # Not a function body — could be init list, extern "C", or scope
        return {"is_func": False, "name": None, "qualified_name": None,
                "kind": "other"}

    # Detect constructor initializer list: walk back over a sequence of
    # `Name(...), Name(...), ...` separated by commas; if we land on `:`
    # then `{` opens the constructor body, not a function signature whose
    # name is the last init-list element.
    j = _skip_initializer_list(stripped, j)
    while j >= 0 and stripped[j].isspace():
        j -= 1
    if j < 0 or stripped[j] != ")":
        return {"is_func": False, "name": None, "qualified_name": None,
                "kind": "other"}

    # Find matching '(' walking back, balanced
    depth = 1
    k = j - 1
    while k >= 0 and depth > 0:
        if stripped[k] == ")":
            depth += 1
        elif stripped[k] == "(":
            depth -= 1
        k -= 1
    if depth != 0:
        return {"is_func": False, "name": None, "qualified_name": None}
    paren_open = k + 1

    # Skip whitespace before '('
    m = paren_open - 1
    while m >= 0 and stripped[m].isspace():
        m -= 1
    name_end = m + 1

    # Skip a template arg list like foo<T, U>(...)
    if m >= 0 and stripped[m] == ">":
        td = 1
        m -= 1
        while m >= 0 and td > 0:
            if stripped[m] == ">":
                td += 1
            elif stripped[m] == "<":
                td -= 1
            m -= 1
        name_end = m + 1
        while m >= 0 and stripped[m].isspace():
            m -= 1

    # Walk back over qualified-name chars (alnum + _ + : + ~ for dtor)
    while m >= 0 and (stripped[m].isalnum() or stripped[m] in "_:~"):
        m -= 1
    name_start = m + 1
    name = stripped[name_start:name_end].strip()

    if not name:
        return {"is_func": False, "name": None, "qualified_name": None,
                "kind": "other"}

    last = name.rsplit("::", 1)[-1].lstrip("~")
    if last in KEYWORDS:
        # 'if (...) {', 'while (...) {' — not a function body.
        return {"is_func": False, "name": None, "qualified_name": None,
                "kind": "control"}
    if not (last and (last[0].isalpha() or last[0] == "_")):
        return {"is_func": False, "name": None, "qualified_name": None,
                "kind": "other"}

    return {
        "is_func": True,
        "name": last,
        "qualified_name": name,
        "kind": "func",
    }


# ---- Body walker: extract calls + conditional contexts -------------------

# A small handful of "call-like" constructs we ignore (operator/cast/sizeof).
_CONTROL_KEYWORDS = ("if", "while", "for", "switch")


def scan_body(
    stripped: str,
    body_start: int,
    body_end: int,
    line_offsets: list[int],
    parent_qualified_name: str,
) -> tuple[list[dict], list[dict]]:
    """Walk a function body. Return (calls, lambdas).

    calls: list of {callee, line, condition, condition_line}.
    lambdas: list of {label, body_start, body_end} for nested lambdas — the
    caller iterates these to add anonymous child entries to the call graph.
    """
    calls: list[dict] = []
    lambdas: list[dict] = []
    i = body_start
    paren_depth = 0
    brace_depth = 0  # depth relative to body opener
    pending_cond: Optional[dict] = None
    cond_stack: list[dict] = []

    while i < body_end:
        c = stripped[i]
        if c == "(":
            paren_depth += 1
            i += 1
            continue
        if c == ")":
            paren_depth -= 1
            i += 1
            continue
        if c == "{":
            brace_depth += 1
            if pending_cond is not None:
                pending_cond["scope_depth"] = brace_depth
                cond_stack.append(pending_cond)
                pending_cond = None
            i += 1
            continue
        if c == "}":
            # Pop conditional contexts whose scope is closing
            brace_depth -= 1
            while cond_stack and cond_stack[-1]["scope_depth"] > brace_depth:
                cond_stack.pop()
            i += 1
            continue
        if c == ";":
            # If we had a pending single-statement conditional, the statement
            # ended without entering a brace block — drop it.
            pending_cond = None
            i += 1
            continue

        # Lambda detection: '[' at expression position, possibly with capture,
        # then '(' args ')' (optional) then '{'. We accept '[' followed by an
        # identifier or '&' or '=' or ',' or ']'.
        if c == "[" and _looks_like_lambda(stripped, i, body_end):
            lam_end = _scan_lambda(stripped, i, body_end)
            if lam_end is not None:
                lam_body_start, lam_body_end = lam_end
                line = offset_to_line(line_offsets, lam_body_start - 1)
                label = f"{parent_qualified_name}::lambda@{line}"
                lambdas.append(
                    {
                        "label": label,
                        "body_start": lam_body_start,
                        "body_end": lam_body_end,
                    }
                )
                # Skip to the end of the lambda body so we don't re-scan it
                # at this level — the caller will recurse into the lambda.
                i = lam_body_end + 1
                continue

        if c.isalpha() or c == "_":
            j = i
            while j < body_end and (stripped[j].isalnum() or stripped[j] == "_"):
                j += 1
            ident = stripped[i:j]

            # Control-flow: capture condition
            if ident in _CONTROL_KEYWORDS and not _is_method_qualified(stripped, i):
                cond, cond_end_idx = _capture_condition(stripped, j, body_end, ident)
                if cond is not None:
                    pending_cond = {
                        "kind": ident,
                        "condition": cond,
                        "line": offset_to_line(line_offsets, i),
                    }
                    i = cond_end_idx
                    continue
                else:
                    i = j
                    continue

            # Check if identifier is followed by '(' (call site)
            k = j
            while k < body_end and stripped[k].isspace():
                k += 1
            # Optional template args
            if k < body_end and stripped[k] == "<":
                m = _skip_template_args(stripped, k, body_end)
                if m is not None:
                    k = m
                    while k < body_end and stripped[k].isspace():
                        k += 1
            if k < body_end and stripped[k] == "(":
                if ident not in KEYWORDS:
                    cond_text = cond_stack[-1]["condition"] if cond_stack else None
                    cond_line = cond_stack[-1]["line"] if cond_stack else None
                    calls.append(
                        {
                            "callee": ident,
                            "line": offset_to_line(line_offsets, i),
                            "condition": cond_text,
                            "condition_line": cond_line,
                        }
                    )
            i = j
            continue

        i += 1

    return calls, lambdas


def _is_method_qualified(src: str, pos: int) -> bool:
    """Is `src[pos:]`-as-identifier preceded by `.`, `->`, or `::`?"""
    j = pos - 1
    while j >= 0 and src[j].isspace():
        j -= 1
    if j < 0:
        return False
    if src[j] == ":" and j > 0 and src[j - 1] == ":":
        return True
    if src[j] == ".":
        return True
    if src[j] == ">" and j > 0 and src[j - 1] == "-":
        return True
    return False


def _capture_condition(
    src: str, after_keyword: int, body_end: int, kind: str
) -> tuple[Optional[str], int]:
    """After the keyword `if`/`while`/`for`/`switch`, find `(` and capture
    its balanced contents. Returns (text-with-keyword-prefix, idx-after-`)`).
    """
    k = after_keyword
    while k < body_end and src[k].isspace():
        k += 1
    if k >= body_end or src[k] != "(":
        return None, after_keyword
    open_paren = k
    depth = 1
    k += 1
    while k < body_end and depth > 0:
        if src[k] == "(":
            depth += 1
        elif src[k] == ")":
            depth -= 1
        k += 1
    cond = src[open_paren:k]
    # Compress whitespace
    cond = re.sub(r"\s+", " ", cond).strip()
    if len(cond) > 200:
        cond = cond[:200] + "…"
    return f"{kind} {cond}", k


def _skip_template_args(src: str, start: int, end: int) -> Optional[int]:
    """If src[start] == '<', try to balance template args and return idx after.
    Reject if balancing fails or angle depth crosses zero unexpectedly."""
    if src[start] != "<":
        return None
    depth = 1
    i = start + 1
    while i < end and depth > 0:
        c = src[i]
        if c == "<":
            depth += 1
        elif c == ">":
            depth -= 1
        elif c in ";{}":
            return None
        i += 1
    return i if depth == 0 else None


def _looks_like_lambda(src: str, pos: int, end: int) -> bool:
    """Heuristic: src[pos] == '[' and the bracket is the start of a lambda
    capture, not an array subscript. We accept if the previous meaningful
    character is one of (= , ( { ; : ? ! && || << >> < > == != return) — i.e.
    expression-position rather than identifier-position."""
    if src[pos] != "[":
        return False
    j = pos - 1
    while j >= 0 and src[j].isspace():
        j -= 1
    if j < 0:
        return True
    prev = src[j]
    if prev in "=,({;:?!<>+-*/%&|^~":
        return True
    if prev == "n":
        # Maybe 'return['
        word_end = j + 1
        word_start = j
        while word_start - 1 >= 0 and (src[word_start - 1].isalnum() or src[word_start - 1] == "_"):
            word_start -= 1
        if src[word_start:word_end] == "return":
            return True
    return False


def _scan_lambda(src: str, pos: int, end: int) -> Optional[tuple[int, int]]:
    """Given src[pos] == '[', try to scan a lambda. Return (body_start, body_end)
    of its body braces, or None if it doesn't look like a complete lambda.

    body_start = index *after* the opening '{', body_end = index *of* the
    closing '}'.
    """
    # Skip capture list [ ... ]
    if src[pos] != "[":
        return None
    depth = 1
    i = pos + 1
    while i < end and depth > 0:
        if src[i] == "[":
            depth += 1
        elif src[i] == "]":
            depth -= 1
        elif src[i] in ";{}":
            return None
        i += 1
    if depth != 0:
        return None
    # Optional generic <T,U> after capture (rare in practice)
    while i < end and src[i].isspace():
        i += 1
    # Optional argument list ( ... )
    if i < end and src[i] == "(":
        depth = 1
        i += 1
        while i < end and depth > 0:
            if src[i] == "(":
                depth += 1
            elif src[i] == ")":
                depth -= 1
            elif src[i] in ";{}":
                return None
            i += 1
        if depth != 0:
            return None
    # Optional mutable / noexcept / -> ReturnType
    while i < end:
        if src[i].isspace():
            i += 1
            continue
        # Try keywords
        if src.startswith("mutable", i):
            i += len("mutable")
            continue
        if src.startswith("constexpr", i):
            i += len("constexpr")
            continue
        if src.startswith("noexcept", i):
            i += len("noexcept")
            # optional (cond)
            j = i
            while j < end and src[j].isspace():
                j += 1
            if j < end and src[j] == "(":
                d = 1
                j += 1
                while j < end and d > 0:
                    if src[j] == "(":
                        d += 1
                    elif src[j] == ")":
                        d -= 1
                    j += 1
                i = j
            continue
        if src[i] == "-" and i + 1 < end and src[i + 1] == ">":
            # Trailing return type
            j = i + 2
            while j < end and src[j] not in "{;":
                j += 1
            if j >= end or src[j] == ";":
                return None
            i = j
            continue
        break
    if i >= end or src[i] != "{":
        return None
    body_open = i
    depth = 1
    i = body_open + 1
    while i < end and depth > 0:
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
        i += 1
    if depth != 0:
        return None
    # i is now one past the closing brace
    return body_open + 1, i - 1


# ---- Top-level driver ----------------------------------------------------


def build_callgraph(path: Path, root_filter: Optional[str] = None) -> dict:
    src = path.read_text()
    stripped = strip_comments_and_strings(src)
    line_offsets = make_line_index(stripped)
    funcs = find_function_bodies(stripped)

    # Sort by start so that nested functions / lambdas come after parents
    funcs.sort(key=lambda f: f["body_start"])

    callgraph: dict[str, dict] = {}
    # Iterate funcs; for each, walk body and collect calls & lambdas.
    # Lambdas become anonymous child nodes whose body is also walked.
    queue: list[dict] = []
    for f in funcs:
        f["start_line"] = offset_to_line(line_offsets, f["body_start"] - 1)
        f["end_line"] = offset_to_line(line_offsets, f["body_end"])
        queue.append(f)

    # Deduplicate function bodies — out-of-line method bodies are unique
    # by (qualified_name, body_start). If the same qualified name shows up
    # with overlapping body extents (template overloads, etc.) we keep
    # the FIRST occurrence and merge calls into a list per name.
    seen_keys: set[tuple[str, int]] = set()

    def walk(node: dict, qual: str):
        calls, lams = scan_body(
            stripped, node["body_start"], node["body_end"], line_offsets, qual
        )
        # Filter calls: drop self-recursion noise of the simple call patterns
        # that aren't in the file's symbol set is left to the overlay step.
        if qual not in callgraph:
            callgraph[qual] = {
                "start_line": offset_to_line(line_offsets, node["body_start"] - 1),
                "end_line": offset_to_line(line_offsets, node["body_end"]),
                "calls": [],
                "lambdas": [],
            }
        callgraph[qual]["calls"].extend(calls)
        for lam in lams:
            label = lam["label"]
            callgraph[qual]["lambdas"].append(label)
            walk(
                {
                    "body_start": lam["body_start"],
                    "body_end": lam["body_end"],
                    "qualified_name": label,
                },
                label,
            )

    for f in queue:
        key = (f["qualified_name"], f["body_start"])
        if key in seen_keys:
            continue
        seen_keys.add(key)
        # Skip nested function bodies that fall inside another function.
        # (lambdas are handled inside walk(); same-level nested classes are
        # rare in this codebase.)
        if any(
            other["body_start"] < f["body_start"] < other["body_end"]
            for other in queue
            if other is not f and other["body_start"] < f["body_start"]
        ):
            continue
        walk(f, f["qualified_name"])

    if root_filter is not None:
        callgraph = _restrict_to_reachable(callgraph, root_filter)
    return callgraph


def _restrict_to_reachable(graph: dict, root_substring: str) -> dict:
    """Optional — keep only functions reachable from any node whose
    qualified name contains the given substring."""
    roots = [n for n in graph if root_substring in n]
    if not roots:
        sys.stderr.write(
            f"warning: no callgraph node matched root filter '{root_substring}'\n"
        )
        return graph
    # Build callee → callers map by name (best-effort, last component match).
    by_last: dict[str, list[str]] = {}
    for n in graph:
        last = n.rsplit("::", 1)[-1]
        by_last.setdefault(last, []).append(n)
    visited: set[str] = set()
    work = list(roots)
    while work:
        n = work.pop()
        if n in visited:
            continue
        visited.add(n)
        for call in graph.get(n, {}).get("calls", []):
            for cand in by_last.get(call["callee"], ()):
                work.append(cand)
        for lam in graph.get(n, {}).get("lambdas", []):
            work.append(lam)
    return {n: graph[n] for n in graph if n in visited}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("source", help="path to a single C++ source file")
    ap.add_argument(
        "--root",
        default=None,
        help="optional substring; restrict output to nodes reachable from a "
        "node whose qualified name contains this substring",
    )
    ap.add_argument(
        "--output",
        "-o",
        default=None,
        help="output JSON path (default: stdout)",
    )
    args = ap.parse_args()

    graph = build_callgraph(Path(args.source), args.root)
    js = json.dumps(graph, indent=2, sort_keys=True)

    if args.output:
        Path(args.output).write_text(js + "\n")
        sys.stderr.write(
            f"wrote {len(graph)} nodes to {args.output}\n"
        )
    else:
        sys.stdout.write(js + "\n")


if __name__ == "__main__":
    main()
