#!/bin/bash
# setup_aflgo.sh
# AFLGO + Array State Diversity 环境搭建脚本
#
# 用法: ./setup_aflgo.sh

set -e

echo "========================================"
echo " AFLGO + Array State Diversity Setup"
echo "========================================"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# 步骤1: 构建 Docker 镜像
echo ""
echo "[1/4] Building Docker image..."
cd "$PROJECT_DIR/docker"
docker build -t aflgo-array:latest .

# 步骤2: 编译 LLVM Pass 插件
echo ""
echo "[2/4] Building LLVM Pass plugins..."
mkdir -p "$PROJECT_DIR/passes/build"
cd "$PROJECT_DIR/passes/build"
cmake .. -DLLVM_DIR=/usr/lib/llvm-11/cmake
make -j$(nproc)

# 步骤3: 编译运行时库
echo ""
echo "[3/4] Building runtime library..."
cd "$PROJECT_DIR/runtime"
gcc -c -O2 -fPIC array_state_runtime.c -o array_state_runtime.o
ar rcs libarray_state.a array_state_runtime.o

# 步骤4: 编译多样性评估模块
echo ""
echo "[4/4] Building diversity module..."
cd "$PROJECT_DIR/diversity"
gcc -c -O2 -fPIC array_diversity.c -o array_diversity.o -lm
ar rcs libdiversity.a array_diversity.o

echo ""
echo "========================================"
echo " Build Complete!"
echo "========================================"
echo ""
echo "LLVM Pass plugins:  $PROJECT_DIR/passes/build/"
echo "Runtime library:    $PROJECT_DIR/runtime/libarray_state.a"
echo "Diversity library:  $PROJECT_DIR/diversity/libdiversity.a"
echo ""
echo "To run Docker environment:"
echo "  docker run -it --rm -v $PROJECT_DIR:/workspace aflgo-array:latest"
echo ""
