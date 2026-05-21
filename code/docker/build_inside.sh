#!/bin/bash
# 在 Docker 容器内执行：编译 LLVM Pass + 运行时库 + 多样性评估库
# 用法: bash build_inside.sh

set -e

echo "========================================"
echo " Building all modules..."
echo "========================================"

# 编译 LLVM Pass
echo ""
echo "[1/3] Building LLVM Pass plugins..."
mkdir -p /workspace/passes/build
cd /workspace/passes/build
cmake .. -DLLVM_DIR=/usr/lib/llvm-11/cmake -Wno-dev
make -j$(nproc)
cd /workspace

# 编译运行时库
echo ""
echo "[2/3] Building runtime library..."
cd /workspace/runtime
clang-11 -c -O2 -fPIC array_state_runtime.c -o array_state_runtime.o
ar rcs libarray_state.a array_state_runtime.o
cd /workspace

# 编译多样性评估库
echo ""
echo "[3/3] Building diversity library..."
cd /workspace/diversity
clang-11 -c -O2 -fPIC array_diversity.c -o array_diversity.o -lm
ar rcs libdiversity.a array_diversity.o
cd /workspace

# 验证产物
echo ""
echo "========================================"
echo " Verifying build artifacts..."
echo "========================================"
ls -la /workspace/passes/build/*.so /workspace/runtime/libarray_state.a /workspace/diversity/libdiversity.a

echo ""
echo "========================================"
echo " Build complete!"
echo "========================================"
