#!/bin/bash
# compile_and_instrument.sh
# 使用 AFLGO + Array State Pass 编译和插桩目标程序
#
# 用法: ./compile_and_instrument.sh <target.c> [output_name]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <target.c> [output_name]"
    exit 1
fi

TARGET_SRC="$1"
OUTPUT_NAME="${2:-$(basename "$TARGET_SRC" .c)}"

# Pass 插件路径
ANALYSIS_PASS="$PROJECT_DIR/passes/build/libarray_analysis_pass.so"
INSTRUMENT_PASS="$PROJECT_DIR/passes/build/libarray_instrument_pass.so"
RUNTIME_LIB="$PROJECT_DIR/runtime/libarray_state.a"

# 检查依赖
if [ ! -f "$ANALYSIS_PASS" ]; then
    echo "Error: Analysis pass not found. Run setup_aflgo.sh first."
    exit 1
fi

echo "========================================"
echo " Compile & Instrument: $TARGET_SRC"
echo "========================================"

# 步骤1: 生成 LLVM IR
echo "[1/5] Generating LLVM IR..."
clang-11 -S -emit-llvm -g -O0 "$TARGET_SRC" -o "${OUTPUT_NAME}.ll"

# 步骤2: 运行静态分析 Pass
echo "[2/5] Running static analysis..."
opt-11 -load "$ANALYSIS_PASS" -array-analysis "${OUTPUT_NAME}.ll" -o /dev/null 2> "${OUTPUT_NAME}_analysis.log"

# 步骤3: 运行插桩 Pass
echo "[3/5] Running instrumentation..."
opt-11 -load "$INSTRUMENT_PASS" -array-instrument "${OUTPUT_NAME}.ll" -S -o "${OUTPUT_NAME}_instr.ll"

# 步骤4: 编译插桩后的 IR 为目标文件
echo "[4/5] Compiling instrumented IR..."
clang-11 -c -O0 "${OUTPUT_NAME}_instr.ll" -o "${OUTPUT_NAME}_instr.o"

# 步骤5: 链接运行时库生成可执行文件
echo "[5/5] Linking with runtime library..."
clang-11 "${OUTPUT_NAME}_instr.o" "$RUNTIME_LIB" -o "${OUTPUT_NAME}_instr" -lm

echo ""
echo "========================================"
echo " Build Complete!"
echo "  Executable: ${OUTPUT_NAME}_instr"
echo "========================================"
echo ""
echo "To test:"
echo "  ./${OUTPUT_NAME}_instr <index>"
echo ""
echo "To view analysis log:"
echo "  cat ${OUTPUT_NAME}_analysis.log"
echo ""
