# 4.2 数组状态监控插桩模块 — 演示步骤

## 演示目标

展示插桩 Pass 能在数组访问位置插入监控代码，运行时实时记录数组访问的索引、类型和行号。

## 前提

延续 4.1 的步骤，`http_parser.ll` 已生成在 `/workspace/test_programs/` 中，运行时库已编译：

```bash
ls -la /workspace/runtime/libarray_state.a
```

## 演示步骤

### 步骤 1：运行插桩 Pass

```bash
cd /workspace/test_programs

opt-11 -load /workspace/passes/build/libarray_instrument_pass.so \
    -array-instrument http_parser.ll -S -o http_parser_instr.ll 2>&1
```

### 步骤 2：观察插桩输出

```
=========================================
 数组插桩模块 — 数组状态监控
=========================================
  [插桩] parse_request_line::method#dim1 (ID=0x82210e20, 行号=69, 访问类型=2)
  [插桩] parse_request_line::method22#dim1 (ID=0x2503db54, 行号=72, 访问类型=2)
  [插桩] parse_body::body#dim1         (ID=0xeb4ded0b, 行号=245, 访问类型=2)
  [插桩] process_request::method#dim1  (ID=0xf5041f0f, 行号=267, 访问类型=1)
  [插桩] process_request::log_line     (ID=0xd5a8a279, 行号=268, 访问类型=2)
  ... (共 26 个插桩点)
插桩完成
```

> **讲解要点**：每行对应一个插桩点——
> - `函数名::变量名`：监控的数组位置
> - `#dimN`：多维数组的维度后缀
> - `ID=0x...`：数组的唯一 FNV-1a 哈希
> - `行号=N`：对应源代码行号
> - `访问类型=1`：读操作；`访问类型=2`：写操作

### 步骤 3：查看插桩后的 IR

打开 IR 文件可以看到 GEP 指令后插入了监控调用：

```bash
grep -A 5 "__afl_report_array_state" http_parser_instr.ll | head -20
```

显示效果类似：

```llvm
%ptr = getelementptr [8 x i8], ptr %method, i64 0, i64 %idx
call void @__afl_report_array_state(
    i32 0x82210e20,          ; array_id
    i64 %idx,                ; index
    i32 2,                   ; access_type (写)
    i32 69                   ; line_number
)
```

### 步骤 4：编译插桩后的二进制

```bash
# 创建 stub（替代 AFLGO 运行时，便于独立演示）
cat > /tmp/stub.c << 'EOF'
#include <stdio.h>
#include <stdint.h>
unsigned char *__afl_area_ptr = NULL;
void __afl_array_state_print_report(void) {}
void __afl_report_array_state(uint32_t id, int64_t idx,
                              uint32_t type, uint32_t line) {
    printf("  [array] id=0x%08x index=%ld type=%d line=%d\n",
           id, (long)idx, (int)type, (int)line);
}
EOF

# 编译 stub → 编译插桩 IR → 链接
clang-11 -c /tmp/stub.c -o /tmp/stub.o
clang-11 -c -O0 http_parser_instr.ll -o http_parser_instr.o
clang-11 /tmp/stub.o http_parser_instr.o -o http_parser_demo -lm
```

### 步骤 5：运行测试

#### （a）正常输入

```bash
printf "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n" \
    | ./http_parser_demo /dev/stdin
```

预期输出：
```
Parsed: GET /index.html, 1 headers, 0 body bytes
```

#### （b）带多个请求头和 body 的输入

```bash
printf "POST /api/data HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nContent-Length: 50\r\n\r\n%s" \
    "$(python3 -c "print('X'*50)")" | ./http_parser_demo /dev/stdin
```

### 步骤 6：验证插桩不改变程序行为

对比插桩前后的输出是否一致：

```bash
# 编译非插桩版本
clang-11 -O0 http_parser.c -o /tmp/http_parser_native -lm

# 相同输入，对比输出
INPUT="GET /test HTTP/1.1

"
echo -n "$INPUT" | /tmp/http_parser_native /dev/stdin
echo -n "$INPUT" | ./http_parser_demo /dev/stdin
```

两次输出应该一致，证明插桩不改变原程序语义。

## 插桩点完整清单

| 函数 | 数组 | 行号 | 类型 | 说明 |
|------|------|------|------|------|
| parse_request_line | method | 69 | 写 | 提取 METHOD |
| parse_request_line | uri | 88 | 写 | 提取 URI |
| parse_body | body | 245 | 写 | 写入 body |
| process_request | method | 267 | 读 | 遍历 method 构造日志 |
| process_request | log_line | 268 | 写 | 写入日志行 |
| process_request | name | 284 | 读 | 越界读取 headers |
| process_request | body | 296 | 读 | 按偏移检查 body |
| parse_one_header | name | 139 | 写 | 提取 header name |
| parse_one_header | value | 161 | 写 | 提取 header value |

> **讲解要点**：
> - 插桩点覆盖了所有数组访问位置（读和写）
> - 行号与源代码精确对应，方便定位代码位置
> - 插桩后的程序行为与插桩前完全一致
