# Array State Diversity Guided Fuzzing Tool (AFLGO-Array)

基于 AFLGO 的数组类型变量状态引导模糊测试工具。

## 项目结构

```
code_new/
├── passes/                  # LLVM Pass 插件
│   ├── array_analysis_pass.cpp    # 静态分析：数组变量识别
│   ├── array_instrument_pass.cpp  # 插桩：数组状态监控
│   └── CMakeLists.txt
├── runtime/                 # 运行时库
│   ├── array_state_runtime.c      # 状态追踪与度量
│   └── array_state_runtime.h
├── diversity/               # 多样性评估模块
│   ├── array_diversity.c          # 海明距离 + 欧氏距离 + 香农熵
│   └── array_diversity.h
├── integration/             # AFLGO 集成
│   ├── aflgo_score_wrapper.c      # 综合得分计算
│   └── aflgo_score_wrapper.h
├── test_programs/           # 测试程序
│   ├── test_simple.c              # 一维数组测试
│   ├── test_multi.c               # 多维数组测试
│   └── test_string.c              # 字符串处理测试
├── scripts/                 # 构建与实验脚本
│   ├── setup_aflgo.sh             # 环境搭建
│   ├── compile_and_instrument.sh  # 编译+插桩
│   └── run_experiment.sh          # 对比实验
├── docker/
│   └── Dockerfile           # AFLGO Docker 环境
└── README.md
```

## 环境搭建

```bash
# 方式一：使用 Docker（推荐）
cd docker
docker build -t aflgo-array .
docker run -it --rm -v $(pwd)/..:/workspace aflgo-array

# 方式二：本地安装
cd scripts
bash setup_aflgo.sh
```

## 编译与测试

```bash
# 编译测试程序并插桩
cd scripts
bash compile_and_instrument.sh ../test_programs/test_simple.c test_simple

# 运行测试
./test_simple_instr 5    # 正常访问
./test_simple_instr 15   # 越界访问
./test_simple_instr -1   # 负索引越界

# 运行实验
bash run_experiment.sh
```

## 核心模块

### 1. 静态分析模块
遍历 LLVM IR 中的 GEP 指令，识别数组变量并计算影响力分数。

### 2. 数组状态监控插桩模块
在 GEP 指令后插入监控代码，运行时记录数组访问索引、类型、行号。

### 3. 数组状态多样性评估模块
- **香农熵**: 衡量索引分布的均匀程度
- **海明距离**: 衡量输入差异度
- **欧氏距离**: 衡量状态向量的多维空间距离

### 4. 测试用例选择模块
将数组状态多样性评分与 AFLGO 距离评分融合：
`combined_score = α × distance + β × diversity + γ × hamming`
