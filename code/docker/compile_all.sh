#!/bin/bash
# 编译所有测试目标（在容器内执行）
# 用法: bash docker/compile_all.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================"
echo " Compiling all test targets..."
echo "========================================"

TARGETS="test_simple test_multi test_string"

for t in $TARGETS; do
    echo ""
    echo "---------- $t ----------"
    bash "$PROJECT_DIR/scripts/compile_and_instrument.sh" \
        "$PROJECT_DIR/test_programs/${t}.c" "$t"
done

echo ""
echo "========================================"
echo " All targets compiled!"
echo "========================================"
ls -la test_*_instr
