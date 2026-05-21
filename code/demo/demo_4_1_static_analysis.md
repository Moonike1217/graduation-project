# 4.1 静态分析模块 — 演示步骤

## 演示目标

展示静态分析 Pass 能自动识别目标程序中的数组变量，并计算影响力分数。

## 测试程序

`http_parser.c` — 微型 HTTP 请求解析器（约 364 行，非论文三个测试程序之一），包含：

| 数组 | 类型 | 说明 |
|------|------|------|
| `method[8]` | char[] | 请求方法 |
| `uri[256]` | char[] | 请求 URI |
| `headers[32]` | struct[] | 请求头数组 |
| `name[64]` / `value[128]` | char[] | 请求头字段 |
| `body[1024]` | char[] | 消息体 |
| `log_line[256]` | char[] | 日志缓冲区 |
| `check_positions[7]` | int[] | 偏移检查表 |

## 前提

Docker 容器已运行：

```bash
docker exec -it adoring_moser bash
```

## 演示步骤

### 步骤 0：编译静态分析 Pass

首次使用或修改源码后需要重新编译。Pass 源码位于 `/workspace/passes/array_analysis_pass.cpp`：

```bash
cd /workspace/passes/build && make
```

编译产物为 `libarray_analysis_pass.so`：

```bash
ls -la /workspace/passes/build/libarray_analysis_pass.so
```

> **讲解要点**：该 Pass 基于 LLVM `ModulePass` 框架，通过遍历 IR 中的 GEP（GetElementPtr）指令识别数组变量并计算影响力分数。`make` 使用 `CMakeLists.txt` 配置的 LLVM-11 工具链进行编译。

## 演示步骤

### 步骤 1：编译目标程序为 LLVM IR

```bash
cd /workspace/test_programs

clang-11 -S -emit-llvm -g -O0 -fno-discard-value-names \
    http_parser.c -o http_parser.ll
```

参数说明：
- `-S -emit-llvm`：生成 LLVM IR 文本文件（.ll）
- `-g`：保留调试信息（行号）
- `-O0`：不优化，保持 IR 可读
- `-fno-discard-value-names`：保留变量名

### 步骤 2：运行静态分析 Pass

```bash
opt-11 -load /workspace/passes/build/libarray_analysis_pass.so \
    -array-analysis http_parser.ll -o /dev/null 2>&1
```

### 步骤 3：观察输出

分析 Pass 输出包含三个部分：

#### （a）每个函数的分析过程

```
>>>>> 分析单个函数: parse_request_line <<<<<
  [数组] method | 类型: [8 x i8] | 长度: 8 | 作用域: 局部 (栈) | 影响力: 0.400
  [数组] uri    | 类型: [256 x i8] | 长度: 256 | 作用域: 局部 (栈) | 影响力: 0.400
```

> **讲解要点**：每找到一个数组 GEP 指令就打印一行，包含名称、类型、长度、作用域、影响力。

#### （b）汇总表

```
 总结: 数组变量发现情况 (已去重)
=========================================
  main::method             | 长度: 8   | 维度: 1 | 影响力: 0.300
  main::uri                | 长度: 256 | 维度: 1 | 影响力: 0.300
  parse_request_line::method  | 长度: 8   | 维度: 1 | 影响力: 0.400
  parse_request_line::uri     | 长度: 256 | 维度: 1 | 影响力: 0.400
  parse_headers::headers      | 长度: 32  | 维度: 1 | 影响力: 0.300
  parse_one_header::name   | 长度: 64  | 维度: 1 | 影响力: 0.400
  parse_one_header::value  | 长度: 128 | 维度: 1 | 影响力: 0.400
  parse_body::body         | 长度: 1024| 维度: 1 | 影响力: 0.400
  process_request::log_line| 长度: 256 | 维度: 0 | 影响力: 0.400
  ... (共 18 个)
唯一数组变量总数: 18
```

#### （c）影响力分数解读

| 分数 | 含义 | 示例 |
|------|------|------|
| 0.3 | 基础分（普通数组） | `headers[32]` 简单结构体数组 |
| 0.4 | 有写入操作（+0.1） | `method[8]` 被赋值 |
| 0.5 | 函数参数（+0.2）或条件分支（+0.3） | — |

### 步骤 4：对比不同程序

```bash
# 用论文的三个测试程序对比
clang-11 -S -emit-llvm -g -O0 test_simple.c -o /tmp/test_simple.ll
opt-11 -load /workspace/passes/build/libarray_analysis_pass.so \
    -array-analysis /tmp/test_simple.ll -o /dev/null 2>&1
```

## 预期结果

- Pass 正常加载，无报错
- 输出中包含数组名称、类型、长度、影响力分数
- `http_parser.c` 能找到 18 个唯一数组变量（去重后）
- 影响力分数在 [0.3, 0.5] 范围内
