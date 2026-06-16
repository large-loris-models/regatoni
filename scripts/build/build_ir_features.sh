#!/usr/bin/env bash
# Build tools/ir_features (LLVM-API IR feature extractor) against the plain build.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh" >/dev/null

LB="$LLVM_BUILD_PLAIN"
mkdir -p "$BUILD_OUT"

INCLUDES=( -I"$PROJECT_ROOT" -I"$LB/include" -I"$LLVM_SRC/llvm/include" )

# Minimal static-lib set for parseIRFile + IR introspection (topological order).
LIBS=(
  "$LB/lib/libLLVMIRReader.a"
  "$LB/lib/libLLVMAsmParser.a"
  "$LB/lib/libLLVMBitReader.a"
  "$LB/lib/libLLVMCore.a"
  "$LB/lib/libLLVMRemarks.a"
  "$LB/lib/libLLVMBitstreamReader.a"
  "$LB/lib/libLLVMBinaryFormat.a"
  "$LB/lib/libLLVMTargetParser.a"
  "$LB/lib/libLLVMSupport.a"
  "$LB/lib/libLLVMDemangle.a"
)

echo "[build] ir_features ..."
$CXX -std=c++17 -fno-rtti -O2 "${INCLUDES[@]}" \
  "$PROJECT_ROOT/tools/ir_features.cc" \
  -fuse-ld=lld \
  -Wl,--start-group "${LIBS[@]}" -Wl,--end-group \
  -lpthread -lm -lz -ltinfo -ldl \
  -o "$BUILD_OUT/ir_features"
echo "[build] ✓ $BUILD_OUT/ir_features"
