#!/bin/bash
# run_all_tests.sh — 运行所有测试程序的全套测试流水线
#
# 依次运行 test.c, string_processor.c, http_parser.c 的
# LLVM Pass 插桩测试，记录状态报告和越界崩溃情况。
# 用法: ./run_all_tests.sh <build_dir>
#
# 示例:
#   cd build && ../test/run_all_tests.sh .

set -e
BUILD_DIR="${1:-.}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "========================================"
echo "  Full Test Pipeline"
echo "  Build dir: $BUILD_DIR"
echo "========================================"

echo ""
echo "========== 1. test.c (Basic Array Tests) =========="
(cd "$BUILD_DIR" && make run_tests 2>&1 | tail -20)
echo ""
echo "========== 2. string_processor.c (String Tests) =========="
(cd "$BUILD_DIR" && make run_string_tests 2>&1 | tail -20)
echo ""
echo "========== 3. http_parser.c (HTTP Parser Tests) =========="
(cd "$BUILD_DIR" && make run_http_tests 2>&1 | tail -30)
echo ""
echo "========================================"
echo "  All tests completed."
echo "========================================"
