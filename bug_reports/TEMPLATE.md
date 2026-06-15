Title: [<ARCH>][<SelectionDAG|GlobalISel>] Miscompilation where <plain-words description of what goes wrong — readable by anyone, no jargon-only phrasing>


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - <Compiler Explorer link>
2. GlobalISel - <Compiler Explorer link>

**Test Commit**
[<full-commit-hash>](https://github.com/llvm/llvm-project/commit/<full-commit-hash>)

**Description**
<One fluent sentence: state what the IR actually computes (so the reader needs no preconceptions), then what the buggy selector does instead. No trailing period. Link a related issue here if one exists, otherwise skip.>

**Steps to reproduce**
- Minimized test case, `input.ll`
```
<minimized IR — name the function @f so the name carries no hint about the bug>
```
```
llc -mtriple=<triple> -mattr=<attrs> input.ll   # SelectionDAG
llc -mtriple=<triple> -mattr=<attrs> -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG (<what this code computes>)
f:
	<asm>

; GlobalISel (<what this code computes>)
f:
	<asm>
```
<Witness line: a concrete poison-free input and the differing source vs. selector result, e.g. `With x = 1: source ..., GlobalISel returns ...`. No trailing period.>

**CC**: @regehr
