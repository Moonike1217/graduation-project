# AFLGO + Array State Diversity 使用指南

## 一、环境搭建

### 1.1 构建 Docker 镜像

```bash
# 在项目根目录执行
docker build -t aflgo-array:latest code_new/docker/
```

### 1.2 启动容器

```bash
docker run -it --rm \
    -v "$(pwd)/code_new:/workspace" \
    aflgo-array:latest
```

进入容器后，代码在 `/workspace` 下：

```
/workspace/
├── passes/                  # LLVM Pass 插件
│   ├── array_analysis_pass.cpp    # 静态分析 Pass
│   ├── array_instrument_pass.cpp  # 插桩 Pass
│   └── CMakeLists.txt
├── runtime/                     # 运行时库
│   ├── array_state_runtime.c
│   └── array_state_runtime.h
├── diversity/                   # 多样性评估模块
│   ├── array_diversity.c        # 海明距离 + 欧氏距离 + 香农熵
│   └── array_diversity.h
├── integration/                 # AFLGO 集成
│   ├── aflgo_score_wrapper.c
│   └── aflgo_score_wrapper.h
├── test_programs/               # 测试程序
│   ├── test_simple.c            # 一维数组
│   ├── test_multi.c             # 多维数组
│   └── test_string.c            # 字符串处理
└── scripts/                     # 构建与实验脚本
    ├── setup_aflgo.sh
    ├── compile_and_instrument.sh
    └── run_experiment.sh
```

### 1.3 一键构建所有模块（在容器内）

```bash
bash /workspace/docker/build_inside.sh
```

这一步会：
1. 编译 LLVM Pass → `passes/build/array_analysis_pass.so` + `array_instrument_pass.so`
2. 编译运行时静态库 → `runtime/libarray_state.a`
3. 编译多样性评估库 → `diversity/libdiversity.a`

---

## 二、编译被测目标

### 2.1 一键编译+插桩

```bash
cd /workspace
bash docker/compile_all.sh
```

产物：
- `test_simple_instr`、`test_multi_instr`、`test_string_instr` — 三个插桩后的可执行文件
- `test_simple_analysis.log`、`test_multi_analysis.log`、`test_string_analysis.log` — 静态分析报告

| 目标 | 源代码 | 适用场景 |
|------|--------|---------|
| `test_simple` | `test_programs/test_simple.c` | 一维数组越界基础测试 |
| `test_multi` | `test_programs/test_multi.c` | 多维数组越界测试 |
| `test_string` | `test_programs/test_string.c` | 字符串处理（字符数组）测试 |

<!-- ### 2.2 手动分步查看流程（非必要）

```bash
# Step 1: C → LLVM IR
clang-11 -S -emit-llvm -g -O0 test_programs/test_simple.c -o test_simple.ll

# Step 2: 静态分析 Pass（识别数组变量）
opt-11 -load passes/build/array_analysis_pass.so \
    -array-analysis test_simple.ll -o /dev/null 2> test_simple_analysis.log

# Step 3: 插桩 Pass（注入运行时监控代码）
opt-11 -load passes/build/array_instrument_pass.so \
    -array-instrument test_simple.ll -S -o test_simple_instr.ll

# Step 4: 编译 + 链接运行时库
clang-11 -c -O0 test_simple_instr.ll -o test_simple_instr.o
clang-11 test_simple_instr.o runtime/libarray_state.a \
    -o test_simple_instr -lm
```-->

--- 

## 三、运行测试

### 3.1 手动传参验证

```bash
# 正常索引
./test_simple_instr 5

# 越界触发崩溃
./test_simple_instr 15
./test_simple_instr -1
./test_simple_instr 9999
```

每次运行后终端会输出：
- 数组访问 ID 和索引值
- SDS（状态多样性分数）
- 信息熵
- 融合得分

同时结果写入 `/tmp/afl_array_feedback.txt`。

### 3.2 查看静态分析结果

```bash
cat test_simple_analysis.log
```

包括：识别到的数组变量、GEP 指令位置、数组类型信息、影响力分数。

### 3.3 查看运行时反馈

```bash
cat /tmp/afl_array_feedback.txt
```

---

## 四、AFLGo 定向模糊测试原理（参考）

> 此节仅供理解原理。一键实验请直接跳到第五节。

AFLGo 的执行流程和参数含义：

| 参数 | 含义 |
|------|------|
| `AFL_NO_UI=1` | 关闭 UI（Docker 中必须设置） |
| `AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1` | 不因无崩溃报错 |
| `-i seeds` | 种子目录 |
| `-o output` | 输出目录 |
| `-V 60` | 运行时长（秒） |
| `-d` | 跳过确定性阶段（加快速度） |
| `@@` | 被替换为输入文件路径 |

第五节 `run_full_experiment.sh` 已自动完成：编译 AFLGo 二进制 → 生成距离 → 准备种子 → 运行 fuzzer → 收集结果。


---

## 五、对比实验（核心实验）

### 5.1 一键运行

容器内执行：

```bash
cd /workspace
bash scripts/run_full_experiment.sh [运行秒数]
```

不传参数默认每个配置跑 60 秒，传参指定时长：

```bash
bash scripts/run_full_experiment.sh 300   # 每个配置跑 5 分钟
```

### 5.2 脚本做了什么

对 `test_simple`、`test_multi`、`test_string` 三个目标，各执行 5 步：

```
Step 1: C 源码 → LLVM IR（clang-11）
Step 2: 构建 Config A 二进制（aflgo-clang 直接编译，含 AFLGo 边覆盖率）
Step 3: 构建 Config B 二进制（aflgo-clang 生成含边覆盖的 IR → opt 叠加数组插桩 → clang-11 链接）
Step 4: 运行 Config A（AFLGo 基线，边覆盖引导）
Step 5: 运行 Config B（AFLGo + Array State Diversity，边覆盖 + 数组状态双重引导）
```

> 注：测试程序 call graph 扁平（所有函数直接从 main 调用），AFLGo 距离信息无实际意义。脚本跳过 LTO 距离计算，仅用 AFLGo 的边覆盖率进行引导。边覆盖引导 + 数组状态引导的双重机制已足够验证实验假设。

### 5.3 两种配置的区别

| | Config A（基线） | Config B（实验组） |
|---|---|---|
| **编译方式** | aflgo-clang 直接编译 C 源码 | aflgo-clang 生成 IR + opt 数组插桩 + clang-11 链接 |
| **AFLGo 边覆盖率** | ✅ 有 | ✅ 有（来自 `aflgo-pass.so`） |
| **AFLGo 距离引导** | 无（扁平 call graph，跳过） | 无（扁平 call graph，跳过） |
| **数组状态插桩** | ❌ 无 | ✅ 有（来自 `libarray_instrument_pass.so`） |
| **数组状态追踪** | ❌ 无（`__afl_report_array` 从未被调用） | ✅ 有（运行时追踪每个数组访问） |
| **反馈输出** | `fuzzer_stats` | `fuzzer_stats` + `/tmp/array_feedback_*.txt` |
| **环境变量** | 无 | `AFL_ARRAY_DIVERSITY=1` |

### 5.4 结果文件结构

每次实验按时间戳创建独立目录：

```
experiments/20260520_143000/
├── summary.txt                  # 总结果对比表
├── seeds/                       # 共享种子
├── feedback_test_simple.txt     # 数组状态反馈（仅 Config B）
├── feedback_test_multi.txt
├── feedback_test_string.txt
│
├── build_test_simple/           # 构建中间产物
│   ├── test_simple.ll           # 原始 LLVM IR
│   ├── test_simple_aflgo.ll     # AFLGo 插桩后的 IR
│   ├── test_simple_full.ll      # AFLGo + 数组插桩后的 IR
│   ├── test_simple_analysis.log # 静态分析报告
│   ├── test_simple_A            # Config A 可执行文件
│   └── test_simple_B            # Config B 可执行文件
│
├── results_A_test_simple/       # Config A 模糊测试输出
│   └── default/
│       ├── fuzzer_stats         # 总执行数、路径数、崩溃数等
│       ├── crashes/             # 发现的崩溃用例
│       ├── queue/               # 优质种子队列
│       └── plot_data            # 时序数据
│
└── results_B_test_simple/       # Config B 模糊测试输出
    └── default/
        ├── fuzzer_stats
        ├── crashes/
        ├── queue/
        └── plot_data
```

### 5.5 解读实验结果

```bash
# 查看汇总
cat experiments/20260520_143000/summary.txt

# 查看某个 target 的统计
cat experiments/20260520_143000/results_A_test_simple/default/fuzzer_stats

# 查看崩溃
ls experiments/20260520_143000/results_A_test_simple/default/crashes/

# 查看数组状态反馈（SDS / 熵 / 融合得分）
cat experiments/20260520_143000/feedback_test_simple.txt

# 查看静态分析日志
cat experiments/20260520_143000/build_test_simple/test_simple_analysis.log
```

关键对比指标：
- **crashes**：发现的独特崩溃数
- **paths_total**：发现的总路径数
- **execs_done**：总执行次数
- Config B 的 feedback 文件中可看到数组多样性数据（SDS、信息熵）随时间的增长趋势

---

## 六、完整工作流速查

```bash
# ===== 宿主机 =====
# 1. 构建镜像
docker build --no-cache -t aflgo-array:latest code_new/docker/

# 2. 启动容器
docker run -it --rm -v "$(pwd)/code_new:/workspace" aflgo-array:latest

# ===== 容器内 =====
# 3. 编译所有模块（Pass + 库）
bash docker/build_inside.sh

# 4. 编译全部测试目标（可选，run_full_experiment.sh 会自动编译）
bash docker/compile_all.sh

# 5. 一键实验
bash scripts/run_full_experiment.sh 60

# ===== 查看结果 =====
cat experiments/<timestamp>/summary.txt
cat experiments/<timestamp>/results_A_test_simple/default/fuzzer_stats
cat experiments/<timestamp>/feedback_test_simple.txt
```

---

## 七、常见问题

### 7.1 AFLGo 报 "No instrumentation detected"

二进制缺少 AFLGo 边覆盖率插桩。必须用 `aflgo-clang` 编译（不能用普通 `clang-11`）。

### 7.2 实验结果为 0 crash

- 检查 `fuzzer_stats` 中 `execs_done` 是否 > 0。如果为 0，说明 `afl-fuzz` 没有真正执行
- 用 `aflgo-clang -S -emit-llvm source.c` 生成的 IR 检查是否包含 `__afl` 开头函数调用
- 尝试增加运行时间：`bash scripts/run_full_experiment.sh 300`

### 7.3 反馈文件 `/tmp/array_feedback_*.txt` 不存在

只有 Config B 会生成反馈文件。如果不存在：
1. 确认 Config B 二进制已通过数组插桩 Pass（`<name>_B` 文件）
2. 程序未正常退出（被 crash/signal 终止时不会写反馈文件）
3. 运行时库 `__afl_array_state_print_report()` 未执行

### 7.4 重置实验

```bash
rm -rf experiments/*
```

### 7.5 距离信息生成失败

如果 `gen_distance_orig.sh` 执行失败（常见原因：缺少 bitcode 文件），脚本会自动降级为无距离信息的 AFLGo（等价于普通 AFL 边覆盖模式）。对于简单测试程序这不会显著影响结果。

