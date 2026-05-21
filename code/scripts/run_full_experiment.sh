#!/bin/bash
# run_full_experiment.sh
# 一键实验脚本：AFLGo 基线 vs AFLGo + Array State Diversity
#
# 用法（容器内）:
#   cd /workspace
#   bash scripts/run_full_experiment.sh [运行秒数，默认60]
#
# 输出:
#   experiments/<timestamp>/
#     ├── summary.txt              # 总结果表
#     ├── seeds/                   # 种子输入
#     ├── build_<name>/            # 每个目标的构建中间产物
#     │   ├── <name>.ll            # 原始 LLVM IR
#     │   ├── <name>_analysis.log  # 静态分析报告
#     │   ├── <name>_A             # Config A 二进制 (AFLGo 基线)
#     │   └── <name>_B             # Config B 二进制 (AFLGo + Array)
#     ├── results_A_<name>/        # Config A 模糊测试结果
#     └── results_B_<name>/        # Config B 模糊测试结果

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
EXPERIMENT_DIR="$PROJECT_DIR/experiments/$TIMESTAMP"
SEEDS_DIR="$EXPERIMENT_DIR/seeds"

RUN_TIME=${1:-60}

# ===== 路径定义 =====
RUNTIME_LIB="$PROJECT_DIR/runtime/libarray_state.a"
RUNTIME_INC="$PROJECT_DIR/runtime"
INST_PASS="$PROJECT_DIR/passes/build/libarray_instrument_pass.so"
ANAL_PASS="$PROJECT_DIR/passes/build/libarray_analysis_pass.so"
AFLGO_DIR="/opt/aflgo"
AFLGO_CC="$AFLGO_DIR/instrument/aflgo-clang"
AFLGO_FUZZ="$AFLGO_DIR/afl-2.57b/afl-fuzz"
AFLGO_RUNTIME="$AFLGO_DIR/instrument/aflgo-runtime.o"

# ===== 依赖检查 =====
echo "=========================================="
echo " AFLGo vs AFLGo+Array Diversity Experiment"
echo "=========================================="
echo "Run time per config: ${RUN_TIME}s"
echo ""

MISSING=""
for f in "$INST_PASS" "$ANAL_PASS" "$RUNTIME_LIB" "$AFLGO_CC" "$AFLGO_FUZZ" "$AFLGO_RUNTIME"; do
    [ -f "$f" ] || { echo "  ✗ Missing: $f"; MISSING=1; }
done
[ -n "$MISSING" ] && { echo ""; echo "Run build_inside.sh first."; exit 1; }
echo "All dependencies found."

# ===== 准备工作目录 =====
mkdir -p "$SEEDS_DIR" "$EXPERIMENT_DIR"
for i in 0 3 5 8 10; do
    echo "$i" > "$SEEDS_DIR/seed_$i"
done
# 给 test_multi 的双参数种子
echo "0 0" > "$SEEDS_DIR/seed_2d_1"
echo "2 3" > "$SEEDS_DIR/seed_2d_2"
echo "1 2" > "$SEEDS_DIR/seed_2d_3"

SUMMARY="$EXPERIMENT_DIR/summary.txt"
{
    echo "Experiment: $TIMESTAMP"
    echo "Run time per config: ${RUN_TIME}s"
    echo ""
    echo "Target         | Config A (AFLGo)  | Config B (AFLGo+Array)"
    echo "               | crashes | paths   | crashes | paths"
    echo "---------------|---------|---------|---------|---------"
} > "$SUMMARY"

# ===== 目标定义 =====
# 格式: "函数名|源码文件"
declare -A TARGETS
TARGETS[test_simple]="test_array_operations|test_simple.c"
TARGETS[test_multi]="test_multidimensional_array|test_multi.c"
TARGETS[test_string]="test_string_copy|test_string.c"

# ===================================================================
# 主循环: 对每个目标执行完整的 构建 → 测试 流程
# ===================================================================
for NAME in "${!TARGETS[@]}"; do
    IFS='|' read -r TARGET_FUNC SRC_FILE <<< "${TARGETS[$NAME]}"
    SRC="$PROJECT_DIR/test_programs/$SRC_FILE"
    BUILD_DIR="$EXPERIMENT_DIR/build_$NAME"

    echo ""
    echo "=========================================="
    echo " Target: $NAME"
    echo "   → target function: $TARGET_FUNC"
    echo "   → source: $SRC_FILE"
    echo "=========================================="

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    # ---------------------------------------------------------------
    # Step 1: C → LLVM IR (clean IR, 用于距离计算和分析)
    # ---------------------------------------------------------------
    echo "[1/5] Generating LLVM IR..."
    clang-11 -S -emit-llvm -g -O0 -fno-discard-value-names \
        -Xclang -disable-O0-optnone \
        "$SRC" -o "$NAME.ll"

    # ---------------------------------------------------------------
    # Step 2: 构建 Config A (AFLGo 基线)
    #   aflgo-clang 编译 C 源码 → 自动注入 AFLGo 边覆盖率
    #   测试程序 call graph 扁平，跳过距离计算直接用边覆盖引导
    # ---------------------------------------------------------------
    echo "[2/5] Building Config A (AFLGo baseline)..."
    $AFLGO_CC -O0 -g "$SRC" "$RUNTIME_LIB" -I"$RUNTIME_INC" \
        -o "${NAME}_A" -lm 2>"$BUILD_DIR/build_A.log"
    echo "  Config A built"

    # ---------------------------------------------------------------
    # Step 3: 构建 Config B (AFLGo + Array State Pass)
    #   先让 aflgo-clang 生成带边覆盖的 IR
    #   再用 opt 叠加数组插桩 Pass
    # ---------------------------------------------------------------
    echo "[3/5] Building Config B (AFLGo + Array State)..."

    # 3a: aflgo-clang 生成 IR（含 AFLGo 边覆盖）
    $AFLGO_CC -O0 -g -S -emit-llvm \
        "$SRC" -I"$RUNTIME_INC" \
        -o "${NAME}_aflgo.ll" 2>"$BUILD_DIR/build_B_ir.log"

    if [ ! -s "${NAME}_aflgo.ll" ]; then
        echo "  ERROR: Failed to generate AFLGo IR"
        printf "%-15s | %-7s | %-7s | %-7s | %-7s\n" \
            "$NAME" "-" "-" "-" "-" >> "$SUMMARY"
        cd "$PROJECT_DIR"
        continue
    fi

    # 3b: 静态分析
    opt-11 -load "$ANAL_PASS" -array-analysis \
        "${NAME}_aflgo.ll" -o /dev/null 2> "${NAME}_analysis.log" || true

    # 3c: 叠加数组插桩
    opt-11 -load "$INST_PASS" -array-instrument \
        "${NAME}_aflgo.ll" -S -o "${NAME}_full.ll" 2>"$BUILD_DIR/instrument.log"

    # 3d: 链接 AFLGo 运行时 + 数组运行时 → 最终二进制
    clang-11 -O0 -g "${NAME}_full.ll" \
        "$RUNTIME_LIB" \
        "$AFLGO_RUNTIME" \
        -o "${NAME}_B" -lm 2>"$BUILD_DIR/build_B_link.log"

    echo "  Config B built"

    cd "$PROJECT_DIR"

    # ---------------------------------------------------------------
    # Step 4: 运行 Config A
    # ---------------------------------------------------------------
    echo "[4/5] Running Config A (AFLGo baseline, ${RUN_TIME}s)..."

    # 创建 wrapper：AFL 传入的是文件路径，测试程序需要 argv[1] 整数
    cat > "$BUILD_DIR/run_A.sh" << 'SCRIPT_EOF'
#!/bin/bash
BIN="$1"
INPUT_FILE="$2"
"$BIN" $(cat "$INPUT_FILE")
SCRIPT_EOF
    chmod +x "$BUILD_DIR/run_A.sh"

    RESULT_A="$EXPERIMENT_DIR/results_A_$NAME"
    mkdir -p "$RESULT_A"

    AFL_NO_UI=1 \
    AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
    AFL_SKIP_CPUFREQ=1 \
        timeout ${RUN_TIME}s \
        "$AFLGO_FUZZ" \
        -i "$SEEDS_DIR" -o "$RESULT_A" \
        -d \
        -- "$BUILD_DIR/run_A.sh" "$BUILD_DIR/${NAME}_A" @@ 2>/dev/null || true

    A_CRASHES=$(find "$RESULT_A" -name "id:*" -path "*/crashes/*" 2>/dev/null | wc -l | tr -d ' ')
    A_PATHS=$(grep "paths_total" "$RESULT_A/default/fuzzer_stats" 2>/dev/null | cut -d: -f2 | tr -d ' ' || echo "0")
    A_EXECS=$(grep "execs_done" "$RESULT_A/default/fuzzer_stats" 2>/dev/null | cut -d: -f2 | tr -d ' ' || echo "0")
    echo "  → crashes=$A_CRASHES paths=$A_PATHS execs=$A_EXECS"

    # ---------------------------------------------------------------
    # Step 5: 运行 Config B
    # ---------------------------------------------------------------
    echo "[5/5] Running Config B (AFLGo+Array, ${RUN_TIME}s)..."

    cat > "$BUILD_DIR/run_B.sh" << 'SCRIPT_EOF'
#!/bin/bash
BIN="$1"
INPUT_FILE="$2"
"$BIN" $(cat "$INPUT_FILE")
SCRIPT_EOF
    chmod +x "$BUILD_DIR/run_B.sh"

    RESULT_B="$EXPERIMENT_DIR/results_B_$NAME"
    mkdir -p "$RESULT_B"

    AFL_NO_UI=1 \
    AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
    AFL_SKIP_CPUFREQ=1 \
    AFL_ARRAY_DIVERSITY=1 \
        timeout ${RUN_TIME}s \
        "$AFLGO_FUZZ" \
        -i "$SEEDS_DIR" -o "$RESULT_B" \
        -d \
        -- "$BUILD_DIR/run_B.sh" "$BUILD_DIR/${NAME}_B" @@ 2>/dev/null || true

    B_CRASHES=$(find "$RESULT_B" -name "id:*" -path "*/crashes/*" 2>/dev/null | wc -l | tr -d ' ')
    B_PATHS=$(grep "paths_total" "$RESULT_B/default/fuzzer_stats" 2>/dev/null | cut -d: -f2 | tr -d ' ' || echo "0")
    B_EXECS=$(grep "execs_done" "$RESULT_B/default/fuzzer_stats" 2>/dev/null | cut -d: -f2 | tr -d ' ' || echo "0")
    echo "  → crashes=$B_CRASHES paths=$B_PATHS execs=$B_EXECS"

    # 保存数组反馈
    [ -f "/tmp/array_feedback.txt" ] && \
        cp "/tmp/array_feedback.txt" "$EXPERIMENT_DIR/feedback_${NAME}.txt"

    # 写入汇总
    printf "%-15s | %-7s | %-7s | %-7s | %-7s\n" \
        "$NAME" "$A_CRASHES" "$A_PATHS" "$B_CRASHES" "$B_PATHS" >> "$SUMMARY"

done

# ===================================================================
# 输出最终结果
# ===================================================================
echo ""
echo "=========================================="
echo " Experiment Complete!"
echo "=========================================="
cat "$SUMMARY"
echo ""
echo "Results saved to: $EXPERIMENT_DIR"
echo ""
echo "Key files:"
echo "  summary    → $SUMMARY"
echo "  per-target → $EXPERIMENT_DIR/build_<name>/"
echo "  feedbacks  → $EXPERIMENT_DIR/feedback_*.txt"
echo ""
echo "To view fuzzer stats:"
echo "  cat $EXPERIMENT_DIR/results_A_<name>/default/fuzzer_stats"
echo "  cat $EXPERIMENT_DIR/results_B_<name>/default/fuzzer_stats"
echo ""
echo "To view array analysis:"
echo "  cat $EXPERIMENT_DIR/build_<name>/<name>_analysis.log"
