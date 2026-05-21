#!/bin/bash
# multi_round_experiment.sh — 多轮对比实验 (3轮×60秒)
# 运行: docker exec aflplusplus_env bash /src/my_pass/test/multi_round_experiment.sh
set -e

BUILD_DIR="/src/my_pass/build"
TEMP_DIR="/tmp/full_experiment"
ROUNDS=3
DURATION=60

export AFL_NO_UI=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_SKIP_CPUFREQ=1

echo "=============================================="
echo "  多轮对比实验: ${ROUNDS}轮 × ${DURATION}秒"
echo "=============================================="

# 确保二进制已构建
if [ ! -f "$TEMP_DIR/test_afl_full" ]; then
    echo "[-] 完整插桩二进制不存在，请先运行 full_experiment.sh 的构建步骤"
    exit 1
fi

results_file="$TEMP_DIR/multi_round_results.txt"
echo "# Round Target Mutator Crashes ExecsPerSec Cycles CorpusCount EdgesFound" > "$results_file"

for round in $(seq 1 $ROUNDS); do
    echo ""
    echo "========== 第 ${round}/${ROUNDS} 轮 =========="

    for target in test string http; do
        case $target in
            test)   bin="$TEMP_DIR/test_afl_full"    seeds="$TEMP_DIR/seeds_test"   ;;
            string) bin="$TEMP_DIR/string_processor_afl_full" seeds="$TEMP_DIR/seeds_string" ;;
            http)   bin="$TEMP_DIR/http_parser_afl_full" seeds="$TEMP_DIR/seeds_http" ;;
        esac

        for mutator in no with; do
            label="${target}_r${round}_${mutator}"
            out_dir="$TEMP_DIR/out_${label}"
            rm -rf "$out_dir"
            mkdir -p "$out_dir"

            echo "  [$label] 开始..."

            if [ "$mutator" = "with" ]; then
                AFL_CUSTOM_MUTATOR_LIBRARY="$BUILD_DIR/custom_mutator.so" \
                    afl-fuzz -i "$seeds" -o "$out_dir" \
                    -V $DURATION -d -- "$bin" @@ > /dev/null 2>&1
            else
                afl-fuzz -i "$seeds" -o "$out_dir" \
                    -V $DURATION -d -- "$bin" @@ > /dev/null 2>&1
            fi

            # 提取数据
            stats="$out_dir/default/fuzzer_stats"
            if [ -f "$stats" ]; then
                crashes=$(grep 'saved_crashes' "$stats" | awk -F': ' '{print $2}' | tr -d ' ')
                eps=$(grep 'execs_per_sec' "$stats" | awk -F': ' '{print $2}' | tr -d ' ')
                cycles=$(grep 'cycles_done' "$stats" | awk -F': ' '{print $2}' | tr -d ' ')
                corpus=$(grep 'corpus_count' "$stats" | awk -F': ' '{print $2}' | tr -d ' ')
                edges=$(grep 'edges_found' "$stats" | awk -F': ' '{print $2}' | tr -d ' ')
                [ -z "$crashes" ] && crashes=0
                echo "$round $target $mutator $crashes $eps $cycles $corpus $edges" >> "$results_file"
                echo "    -> crashes=$crashes eps=$eps edges=$edges"
            else
                echo "$round $target $mutator FAIL FAIL FAIL FAIL FAIL" >> "$results_file"
                echo "    -> FAILED"
            fi
        done
    done
done

# ============================================================
# 汇总统计
# ============================================================
echo ""
echo "=============================================="
echo "  多轮实验结果汇总"
echo "=============================================="

calc_avg() {
    local target="$1" mutator="$2" field="$3"
    local sum=0 count=0
    while read r t m c e cy co ed; do
        if [ "$t" = "$target" ] && [ "$m" = "$mutator" ]; then
            case $field in
                crashes) val="$c" ;;
                eps)     val="$e" ;;
                cycles)  val="$cy" ;;
                corpus)  val="$co" ;;
                edges)   val="$ed" ;;
            esac
            if [ "$val" != "FAIL" ]; then
                sum=$(echo "$sum + $val" | bc 2>/dev/null || echo "$sum")
                count=$((count + 1))
            fi
        fi
    done < "$results_file"
    if [ $count -gt 0 ]; then
        echo "scale=1; $sum / $count" | bc 2>/dev/null
    else
        echo "N/A"
    fi
}

printf "%-22s | %8s | %8s | %10s | %10s\n" \
    "测试目标" "轮次" "变异器" "平均崩溃" "平均速度"
printf "%-22s-+-%8s-+-%8s-+-%10s-+-%10s\n" \
    "----------------------" "--------" "--------" "----------" "----------"

for target in test string http; do
    for mutator in no with; do
        avg_crash=$(calc_avg "$target" "$mutator" crashes)
        avg_eps=$(calc_avg "$target" "$mutator" eps)
        avg_edges=$(calc_avg "$target" "$mutator" edges)
        if [ "$mutator" = "no" ]; then label="无变异器"; else label="有变异器"; fi
        printf "%-22s | %8s | %8s | %10s | %10s\n" \
            "$target" "1-3" "$label" "$avg_crash" "$avg_eps"
    done
    echo ""
done

echo "=============================================="
echo "  详细数据: $results_file"
echo "=============================================="
