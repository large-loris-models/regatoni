#!/usr/bin/env python3
"""Exit 0 if the given LLVM IR file is a clean scalar-integer function, else 1.

Used by scripts/oracles/backend_tv.sh as the BACKEND_TV_INT_ONLY gate. We do this
in Python on purpose: the system `grep` is `ugrep`, whose `\b`/ERE semantics gave
wildly wrong results here (it matched the `call` keyword inside value names like
`%call`, falsely flagging ~84% of clean functions). Python's `re` is reliable.

Keep the predicate in sync with is_clean_int_func() in
scripts/build/extract_single_funcs.py — same definition of "clean scalar-integer".
"""
import re
import sys

FP_PTR = re.compile(
    r"\b(?:float|double|half|bfloat|fp128|x86_fp80|ppc_fp128"
    r"|getelementptr|alloca|inttoptr|ptrtoint|addrspace|blockaddress"
    r"|load|store|ptr)\b"
    r"|[%\w\]>]\*"
)
EXOTIC = re.compile(
    r"\b(?:token|invoke|landingpad|catchswitch|catchpad|cleanuppad|cleanupret"
    r"|catchret|resume|personality|musttail|callbr|indirectbr|va_arg|asm)\b"
    r"|\.\.\."
)
VECTOR = re.compile(
    r"<\s*(?:vscale\s+x\s+)?\d+\s+x\s+"
    r"|\b(?:shufflevector|extractelement|insertelement)\b"
)
TARGET_INTRIN = re.compile(
    r"@llvm\.(?:x86|aarch64|arm|amdgcn|nvvm|ppc|hexagon|wasm|s390|mips"
    r"|riscv|ve|bpf|spv|loongarch)\."
)
# Generic intrinsics that scalar riscv64 cannot select -> backend-tv "error" and a
# codegen-target report_fatal_error ("Cannot select: vscale" / read_register
# illegal type). vscale needs +v; (read|write)_register are ABI/phys-reg specific.
UNSELECTABLE_INTRIN = re.compile(
    r"@llvm\.(?:vscale|read_register|write_register|read_volatile_register)\b"
)
# A call/invoke/callbr INSTRUCTION (opcode at start-of-instruction or after `=`),
# NOT a `%call` value name. Matched per line.
CALL_INSTR = re.compile(
    r"^\s*(?:%[\w.$-]+\s*=\s*)?(?:tail |musttail |notail )?(?:call|invoke|callbr)\s"
)


def is_clean_int_func(text):
    if FP_PTR.search(text):
        return False
    if EXOTIC.search(text):
        return False
    if VECTOR.search(text):
        return False
    if TARGET_INTRIN.search(text):
        return False
    if UNSELECTABLE_INTRIN.search(text):
        return False
    for line in text.splitlines():
        if CALL_INSTR.match(line) and "@llvm." not in line:
            return False
    return True


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: clean_int_check.py <ir-file>")
    try:
        text = open(sys.argv[1], errors="replace").read()
    except OSError:
        sys.exit(2)
    sys.exit(0 if is_clean_int_func(text) else 1)


if __name__ == "__main__":
    main()
