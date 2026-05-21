#!/bin/bash
# run_comparison_experiment.sh
# 对 4 个目标（libxml2 / jasper / lrzip / mjs）跑 Config A vs B 对比实验
#
# 用法:
#   cd /workspace && bash scripts/run_comparison_experiment.sh [运行秒数]
#   默认每个实验 2700 秒（45 分钟）

set -e

RUN_TIME=${1:-2700}
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
EXPERIMENT_DIR="/workspace/experiments/$TIMESTAMP"
SUMMARY="$EXPERIMENT_DIR/summary.txt"
AFLGO_FUZZ="/opt/aflgo/afl-2.57b/afl-fuzz"

mkdir -p "$EXPERIMENT_DIR"

echo "=========================================="
echo " Config A vs B 对比实验"
echo " 时间: $(date)"
echo " 每轮时长: ${RUN_TIME}s"
echo " 4 targets x 2 configs = 8 轮"
echo " 预计总时长: $((8 * RUN_TIME / 60)) min"
echo "=========================================="
echo ""

# ============== 种子准备 ==============
SEEDS_DIR="$EXPERIMENT_DIR/seeds"
mkdir -p "$SEEDS_DIR"

# libxml2 种子
cat > "$SEEDS_DIR/xml_seed1.xml" << 'XMLEOF'
<?xml version="1.0"?>
<root><item id="1">hello</item></root>
XMLEOF
cat > "$SEEDS_DIR/xml_seed2.xml" << 'XMLEOF'
<?xml version="1.0"?>
<root/>
XMLEOF
cat > "$SEEDS_DIR/xml_seed3.xml" << 'XMLEOF'
<?xml version="1.0"?>
<doc><elem attr="val">text</elem></doc>
XMLEOF

# jasper 种子（PPM 格式）
mkdir -p "$SEEDS_DIR/jasper"
cat > "$SEEDS_DIR/jasper/seed1.ppm" << 'PPMEOF'
P3
2 2
255
0 0 0 255 255 255
128 128 128 64 64 64
PPMEOF
cat > "$SEEDS_DIR/jasper/seed2.ppm" << 'PPMEOF'
P3
4 4
255
255 0 0 0 255 0 0 0 255 255 255 0
255 255 255 128 128 128 64 64 64 32 32 32
16 16 16 8 8 8 4 4 4 2 2 2
1 1 1 128 0 128 0 128 0 128 128 0
PPMEOF
cp "$SEEDS_DIR/xml_seed1.xml" "$SEEDS_DIR/jasper/" 2>/dev/null || true

# lrzip 种子
mkdir -p "$SEEDS_DIR/lrzip"
dd if=/dev/urandom bs=256 count=1 2>/dev/null > "$SEEDS_DIR/lrzip/seed1.bin"
echo "hello world this is a test file for compression" > "$SEEDS_DIR/lrzip/seed2.txt"
dd if=/dev/zero bs=256 count=1 2>/dev/null > "$SEEDS_DIR/lrzip/seed3.bin"

# mjs 种子
mkdir -p "$SEEDS_DIR/mjs"
cat > "$SEEDS_DIR/mjs/seed1.js" << 'JSEOF'
var x = 1 + 1;
print(x);
JSEOF
cat > "$SEEDS_DIR/mjs/seed2.js" << 'JSEOF'
var arr = [1,2,3];
for (var i = 0; i < arr.length; i++) {
  print(arr[i]);
}
JSEOF
cat > "$SEEDS_DIR/mjs/seed3.js" << 'JSEOF'
function add(a,b) { return a+b; }
print(add(2,3));
JSEOF

# ============== 写入 summary 表头 ==============
{
    echo "=============================================="
    echo " Config A vs B 对比实验"
    echo " 时间戳: $TIMESTAMP"
    echo " 每轮时长: ${RUN_TIME}s"
    echo "=============================================="
    echo ""
    printf "%-12s %-8s %-12s %-12s %-12s %-12s %-12s\n" \
        "Target" "Config" "execs_done" "paths_total" "crashes" "uniq_crashes" "array_feedback"
    printf "%-12s %-8s %-12s %-12s %-12s %-12s %-12s\n" \
        "------" "------" "----------" "-----------" "-------" "------------" "-------------"
} > "$SUMMARY"

# ============== 通用实验函数 ==============
# 参数: target_name config binary_path seed_dir aflgo_args...
# aflgo_args 中的 @@ 会被 AFLGo 替换为输入文件路径
run_one_experiment() {
    local name=$1
    local config=$2
    local binary=$3
    local seed_dir=$4
    shift 4
    local aflgo_args=("$@")  # 剩余参数传给 AFLGo 的目标程序

    local result_dir="$EXPERIMENT_DIR/results_${config}_${name}"

    mkdir -p "$result_dir"

    # 构建环境变量
    local env_vars="AFL_NO_UI=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 AFL_SKIP_CPUFREQ=1"
    if [ "$config" = "B" ]; then
        env_vars="AFL_ARRAY_DIVERSITY=1 $env_vars"
    fi

    echo ""
    echo "------------------------------------------------------"
    echo " [$(date +%H:%M:%S)] 开始: ${name} Config ${config}"
    echo " 种子目录: $seed_dir"
    echo " 命令: $binary ${aflgo_args[*]}"
    echo "------------------------------------------------------"

    # 清空之前的反馈文件
    rm -f /tmp/array_feedback.txt

    # 运行 fuzzer
    env $env_vars timeout ${RUN_TIME}s \
        $AFLGO_FUZZ -i "$seed_dir" -o "$result_dir" -d \
        -- "$binary" "${aflgo_args[@]}" 2>&1 || true

    # 收集结果（AFLGo stats 在结果目录根目录，不在 default/ 子目录）
    local stats_file="$result_dir/fuzzer_stats"
    local execs=0 paths=0 crashes=0 unique=0 feedback="N/A"

    if [ -f "$stats_file" ]; then
        execs=$(grep "execs_done" "$stats_file" | cut -d: -f2 | tr -d ' ' || echo "0")
        paths=$(grep "paths_total" "$stats_file" | cut -d: -f2 | tr -d ' ' || echo "0")
        crashes=$(grep "unique_crashes" "$stats_file" | cut -d: -f2 | tr -d ' ' || echo "0")
        unique=$(find "$result_dir/crashes" -name "id:*" 2>/dev/null | wc -l | tr -d ' ')
        [ -z "$unique" ] && unique=0
    fi

    # 保存数组反馈
    if [ "$config" = "B" ] && [ -f "/tmp/array_feedback.txt" ]; then
        cp "/tmp/array_feedback.txt" "$EXPERIMENT_DIR/feedback_${name}.txt" 2>/dev/null || true
        feedback=$(head -1 /tmp/array_feedback.txt 2>/dev/null | tr -d '\n')
    fi

    # 写入 summary
    printf "%-12s %-8s %-12s %-12s %-12s %-12s %-12s\n" \
        "$name" "$config" "$execs" "$paths" "$crashes" "$unique" "$feedback" >> "$SUMMARY"

    echo "  -> execs=$execs paths=$paths crashes=$crashes unique=$unique"
}

# ============== 主循环 ==============

# 1. libxml2 - xmllint --valid @@
echo ""
echo "========== [1/4] libxml2 (xmllint) =========="
run_one_experiment "xmllint" "A" \
    "/workspace/test_subjects/libxml2/obj-aflgo/xmllint_A" \
    "$SEEDS_DIR" "--valid" "@@"
run_one_experiment "xmllint" "B" \
    "/workspace/test_subjects/libxml2/obj-aflgo-array/xmllint_B" \
    "$SEEDS_DIR" "--valid" "@@"

# 2. jasper -f @@ -F /tmp/jasper_out
echo ""
echo "========== [2/4] jasper =========="
run_one_experiment "jasper" "A" \
    "/workspace/test_subjects/jasper/obj-aflgo/jasper_A" \
    "$SEEDS_DIR/jasper" "-f" "@@" "-T" "jpc" "-F" "/tmp/jasper_out"
run_one_experiment "jasper" "B" \
    "/workspace/test_subjects/jasper/obj-aflgo-array/jasper_B" \
    "$SEEDS_DIR/jasper" "-f" "@@" "-T" "jpc" "-F" "/tmp/jasper_out"

# 3. lrzip -o /tmp/lrzip_out.lzo @@
echo ""
echo "========== [3/4] lrzip =========="
run_one_experiment "lrzip" "A" \
    "/workspace/test_subjects/lrzip/lrzip_A" \
    "$SEEDS_DIR/lrzip" "-o" "/tmp/lrzip_out.lzo" "@@"
run_one_experiment "lrzip" "B" \
    "/workspace/test_subjects/lrzip/lrzip_B" \
    "$SEEDS_DIR/lrzip" "-o" "/tmp/lrzip_out.lzo" "@@"

# 4. mjs @@
echo ""
echo "========== [4/4] mjs =========="
run_one_experiment "mjs" "A" \
    "/workspace/mjs_src/obj-aflgo/mjs_A" \
    "$SEEDS_DIR/mjs" "@@"
run_one_experiment "mjs" "B" \
    "/workspace/mjs_src/obj-aflgo-array/mjs_B" \
    "$SEEDS_DIR/mjs" "@@"

# ============== 输出汇总 ==============
echo ""
echo "=============================================="
echo " 实验完成！"
echo "=============================================="
echo ""
cat "$SUMMARY"
echo ""
echo "结果目录: $EXPERIMENT_DIR"
echo "详细日志: $EXPERIMENT_DIR/results_<config>_<target>/default/fuzzer_stats"
echo "数组反馈: $EXPERIMENT_DIR/feedback_*.txt (仅 Config B)"
echo ""
