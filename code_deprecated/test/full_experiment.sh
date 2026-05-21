#!/bin/bash
# full_experiment.sh — 完整插桩对比实验
#
# 目标: 对比有/无自定义变异器两种配置下的 AFL++ 模糊测试效果
#
# 构建方式: afl-clang-fast 同时加载两个 LLVM Pass:
#   1. AFL++ 自身的边覆盖率 Pass (由 afl-clang-fast 自动注入)
#   2. MyArrayPass (通过 -fpass-plugin= 手动加载)
#   同时 -include afl_array_state.c 将运行时库嵌入目标程序
#
# 最终二进制同时具备:
#   - AFL++ 边覆盖率插桩
#   - 数组状态追踪插桩
#   - 运行时状态采集与反馈文件写入
#
# 运行方式: 在 Docker 容器中执行
#   cd /src/my_pass/build && bash /src/my_pass/test/full_experiment.sh

set -e

BUILD_DIR="/src/my_pass/build"
TEST_DIR="/src/my_pass/test"
SRC_DIR="/src/my_pass/src"
TEMP_DIR="/tmp/full_experiment"

echo "=============================================="
echo "  完整插桩对比实验"
echo "  对比: 无变异器 vs 自定义变异器"
echo "=============================================="
echo ""

# ============================================================
# 步骤 1: 检查环境与产物
# ============================================================
echo "========== 步骤 1: 环境检查 =========="

cd "$BUILD_DIR"

if [ ! -f "MyArrayPass.so" ]; then
    echo "[-] MyArrayPass.so 不存在，请先执行 cmake .. && make -j4"
    exit 1
fi
echo "[+] MyArrayPass.so 就绪"

if [ ! -f "custom_mutator.so" ]; then
    echo "[!] custom_mutator.so 不存在，正在编译..."
    gcc -O2 -fPIC -shared "$SRC_DIR/custom_mutator.c" -o custom_mutator.so
fi
echo "[+] custom_mutator.so 就绪"

mkdir -p "$TEMP_DIR"

# ============================================================
# 步骤 2: 构建完整插桩二进制
#   afl-clang-fast 同时注入两个 Pass + 嵌入运行时库:
#   - AFL++ 边覆盖率 Pass（自动）
#   - MyArrayPass 数组状态追踪（-fpass-plugin=）
#   - afl_array_state.c 运行时库（-include）
# ============================================================
echo ""
echo "========== 步骤 2: 构建完整插桩二进制 =========="

build_full() {
    local name="$1"
    local src="$2"
    local ll="$TEMP_DIR/${name}.ll"
    local ll_instr="$TEMP_DIR/${name}_instr.ll"
    local bin="$TEMP_DIR/${name}_afl_full"

    echo "--- 构建 $name ---"

    # Step A: C → LLVM IR
    echo "  [A] clang-19 → .ll"
    clang-19 -S -emit-llvm -fno-discard-value-names -g -O0 \
        -Xclang -disable-O0-optnone "$src" -o "$ll"

    # Step B: IR → 插桩 IR（注入 __afl_report_array 调用）
    echo "  [B] opt + MyArrayPass → _instr.ll"
    opt-19 -load-pass-plugin="$BUILD_DIR/MyArrayPass.so" \
        -passes="my-array-pass" "$ll" -S -o "$ll_instr" 2>&1 | grep -E "\[Array\]|\[Instrumented\]" || true

    # Step C: 插桩 IR → AFL++ 二进制（注入边覆盖率追踪）
    echo "  [C] afl-clang-fast → 最终二进制"
    afl-clang-fast -O0 -g "$ll_instr" "$SRC_DIR/afl_array_state.c" \
        -lm -o "$bin" 2>&1 | tail -3

    if [ -f "$bin" ]; then
        echo "  [✓] $name -> $bin"
    else
        echo "  [✗] $name 构建失败!"
        exit 1
    fi
    echo ""
}

build_full "test"           "$TEST_DIR/test.c"
build_full "string_processor" "$TEST_DIR/string_processor.c"
build_full "http_parser"    "$TEST_DIR/http_parser.c"

echo "[✓] 全部二进制构建完成"
ls -la "$TEMP_DIR"/*_afl_full

# ============================================================
# 步骤 3: 准备种子
# ============================================================
echo ""
echo "========== 步骤 3: 准备种子 =========="

mkdir -p "$TEMP_DIR/seeds_test" "$TEMP_DIR/seeds_string" "$TEMP_DIR/seeds_http"

echo "0"  > "$TEMP_DIR/seeds_test/seed1"
echo "5"  > "$TEMP_DIR/seeds_test/seed2"
echo "100" > "$TEMP_DIR/seeds_test/seed3"

echo "0"   > "$TEMP_DIR/seeds_string/seed1"
echo "5"   > "$TEMP_DIR/seeds_string/seed2"
echo "100"  > "$TEMP_DIR/seeds_string/seed3"

echo -e "GET / HTTP/1.1\r\n\r\n" \
    > "$TEMP_DIR/seeds_http/seed1"
echo -e "POST /index.html HTTP/1.1\r\nHost: test\r\nContent-Length: 5\r\n\r\nhello" \
    > "$TEMP_DIR/seeds_http/seed2"

echo "[✓] 种子就绪"

# ============================================================
# 步骤 4: 运行 AFL++ 模糊测试
#   配置 A: 无自定义变异器 (仅 AFL++ 随机变异)
#   配置 B: 有自定义变异器 (AFL++ + 数组状态引导)
# ============================================================
echo ""
echo "========== 步骤 4: 运行 AFL++ 模糊测试 (每组 30 秒) =========="

# 使用 export 确保环境变量在所有子进程中生效
export AFL_NO_UI=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_SKIP_CPUFREQ=1

# 清空旧反馈文件
rm -f /tmp/afl_array_feedback.txt

run_fuzz() {
   	local label="$1"
    local out_dir="$2"
    local seeds="$3"
    local binary="$4"
    local use_mutator="$5"   # "yes" or "no"

    echo ""
    echo "===== $label ====="
    echo "  二进制: $binary"
    echo "  种子:   $seeds"
    echo "  输出:   $out_dir"
    echo "  变异器: $use_mutator"

    rm -rf "$out_dir"
    mkdir -p "$out_dir"

    if [ "$use_mutator" = "yes" ]; then
        AFL_CUSTOM_MUTATOR_LIBRARY="$BUILD_DIR/custom_mutator.so" \
            afl-fuzz -i "$seeds" -o "$out_dir" \
            -V 30 -d -- "$binary" @@ 2>&1 | tail -5
    else
        afl-fuzz -i "$seeds" -o "$out_dir" \
            -V 30 -d -- "$binary" @@ 2>&1 | tail -5
    fi

    echo "  完成"
}

# --- 配置 A: 无自定义变异器 ---
run_fuzz "A1: test.c (无变异器)" \
    "$TEMP_DIR/out_test_no_mutator" \
    "$TEMP_DIR/seeds_test" \
    "$TEMP_DIR/test_afl_full" \
    "no"

run_fuzz "A2: string_processor (无变异器)" \
    "$TEMP_DIR/out_string_no_mutator" \
    "$TEMP_DIR/seeds_string" \
    "$TEMP_DIR/string_processor_afl_full" \
    "no"

run_fuzz "A3: http_parser (无变异器)" \
    "$TEMP_DIR/out_http_no_mutator" \
    "$TEMP_DIR/seeds_http" \
    "$TEMP_DIR/http_parser_afl_full" \
    "no"

# --- 配置 B: 有自定义变异器 ---
run_fuzz "B1: test.c (自定义变异器)" \
    "$TEMP_DIR/out_test_with_mutator" \
    "$TEMP_DIR/seeds_test" \
    "$TEMP_DIR/test_afl_full" \
    "yes"

run_fuzz "B2: string_processor (自定义变异器)" \
    "$TEMP_DIR/out_string_with_mutator" \
    "$TEMP_DIR/seeds_string" \
    "$TEMP_DIR/string_processor_afl_full" \
    "yes"

run_fuzz "B3: http_parser (自定义变异器)" \
    "$TEMP_DIR/out_http_with_mutator" \
    "$TEMP_DIR/seeds_http" \
    "$TEMP_DIR/http_parser_afl_full" \
    "yes"

# ============================================================
# 步骤 5: 收集并对比实验结果
# ============================================================
echo ""
echo "=============================================="
echo "  步骤 5: 实验结果汇总"
echo "=============================================="

extract_stats() {
    local out_dir="$1"
    local stats_file="$out_dir/default/fuzzer_stats"

    if [ -f "$stats_file" ]; then
        local execs=$(grep 'execs_done' "$stats_file" | awk -F': ' '{print $2}')
        local eps=$(grep 'execs_per_sec' "$stats_file" | awk -F': ' '{print $2}')
        local crashes=$(grep 'unique_crashes' "$stats_file" | awk -F': ' '{print $2}')
        local cycles=$(grep 'cycles_done' "$stats_file" | awk -F': ' '{print $2}')
        local edges=$(grep 'edges_found' "$stats_file" | awk -F': ' '{print $2}')
        local pend=$(grep 'pending_favs' "$stats_file" | awk -F': ' '{print $2}')
        # 计算边覆盖率 (AFL++ 默认 bitmap 65536)
        local cov_pct=0
        if [ -n "$edges" ] && [ "$edges" -gt 0 ] 2>/dev/null; then
            cov_pct=$(echo "scale=2; $edges / 65536 * 100" | bc 2>/dev/null || echo "0")
        fi
        local crash_files=$(ls "$out_dir/default/crashes/" 2>/dev/null | grep -v README | wc -l | tr -d ' ')

        echo "  execs_done=$execs  execs/sec=$eps  crashes(unique)=$crashes  crash_files=$crash_files  cycles=$cycles  edges=$edges  cov=$cov_pct%"
    else
        echo "  [无数据] fuzzer_stats 不存在"
    fi
}

echo ""
echo "========== 配置 A: 无自定义变异器 =========="
for name in test string http; do
    echo "--- $name ---"
    extract_stats "$TEMP_DIR/out_${name}_no_mutator"
done

echo ""
echo "========== 配置 B: 有自定义变异器 (数组状态引导) =========="
for name in test string http; do
    echo "--- $name ---"
    extract_stats "$TEMP_DIR/out_${name}_with_mutator"
done

echo ""
echo "=============================================="
echo "  对比表格"
echo "=============================================="
printf "%-25s | %12s | %12s | %12s | %10s\n" \
    "测试目标" "无变异器崩溃" "有变异器崩溃" "无变异器速度" "有变异器速度"
printf "%-25s-+-%12s-+-%12s-+-%12s-+-%10s\n" \
    "-------------------------" "------------" "------------" "------------" "----------"

for name in test string http; do
    no_crash=$(grep 'unique_crashes' "$TEMP_DIR/out_${name}_no_mutator/default/fuzzer_stats" 2>/dev/null | awk -F': ' '{print $2}' || echo "?")
    with_crash=$(grep 'unique_crashes' "$TEMP_DIR/out_${name}_with_mutator/default/fuzzer_stats" 2>/dev/null | awk -F': ' '{print $2}' || echo "?")
    no_eps=$(grep 'execs_per_sec' "$TEMP_DIR/out_${name}_no_mutator/default/fuzzer_stats" 2>/dev/null | awk -F': ' '{print $2}' || echo "?")
    with_eps=$(grep 'execs_per_sec' "$TEMP_DIR/out_${name}_with_mutator/default/fuzzer_stats" 2>/dev/null | awk -F': ' '{print $2}' || echo "?")

    printf "%-25s | %12s | %12s | %12s | %10s\n" \
        "$name" "$no_crash" "$with_crash" "$no_eps" "$with_eps"
done

echo ""
echo "=============================================="
echo "  实验完成"
echo "  原始数据保存在: $TEMP_DIR"
echo "=============================================="
