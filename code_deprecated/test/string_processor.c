// string_processor.c — 字符串处理场景的数组越界测试目标
//
// 模拟真实世界 C 程序中常见的 4 种字符串操作模式，
// 每种模式使用固定大小缓冲区 + 外部输入的参数（无边界检查）。
//
// 场景:
//   1. 字符串拷贝 — 类似 gets/strcpy 的缓冲区溢出
//   2. 字符串拼接 — 类似 strcat 拼接越界
//   3. 标记分割 — 解析并索引访问 token 数组
//   4. 格式化写入 — 缓冲区大小不足 + 后续越界

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void __afl_array_state_print_report(void);

// ============================================================
// 场景1: 字符串拷贝（基于位置的缓冲区溢出）
// 用输入 pos 控制拷贝起始位置，无边界检查
// 当 pos > 0 时，dest 会被溢出
// ============================================================
void test_string_copy(int pos) {
    char dest[16];
    memset(dest, 0, sizeof(dest));
    const char *src = "OVERFLOW_TEST_DATA";

    // 从 pos 位置开始拷贝，无边界检查
    for (int i = 0; src[i] != '\0'; i++) {
        dest[pos + i] = src[i];  // pos > 0 时可能越界
    }
}

// ============================================================
// 场景2: 字符串拼接（基于长度的缓冲区溢出）
// 在已有字符串后追加 extra_len 个字符，无边界检查
// 当 extra_len > (缓冲区大小 - 已有字符串长度) 时溢出
// ============================================================
void test_string_concat(int extra_len) {
    char buffer[20];
    memset(buffer, 0, sizeof(buffer));
    strcpy(buffer, "BASE_STR_");

    int start = strlen(buffer);
    int limit = (extra_len < 100 && extra_len > -100) ? extra_len : 10;
    if (limit < 0) limit = -limit;

    for (int i = 0; i < limit; i++) {
        buffer[start + i] = 'X';  // limit > 10 时越界
    }
}

// ============================================================
// 场景3: 标记分割（token 数组索引越界）
// 将字符串按逗号分割后存入指针数组，用输入 idx 访问
// 当 idx < 0 或 idx >= token 数量时越界访问 tokens 数组
// ============================================================
void test_string_split(int idx) {
    char input[] = "tok0,tok1,tok2,tok3,tok4";
    char *tokens[10];
    int num_tokens = 0;

    // 分割字符串
    char *save = NULL;
    char *token = strtok_r(input, ",", &save);
    while (token != NULL && num_tokens < 10) {
        tokens[num_tokens++] = token;
        token = strtok_r(NULL, ",", &save);
    }

    // 用传入的 idx 访问 token — 越界时触发漏洞
    if (idx >= 0 && idx < num_tokens) {
        printf("Token %d: %s\n", idx, tokens[idx]);
    } else {
        // 越界访问（刻意制造漏洞）
        printf("OOB Token: %s\n", tokens[idx]);  // idx < 0 || idx >= 5 时越界
    }
}

// ============================================================
// 场景4: 格式化写入 + 后续越界
// 先用 snprintf 写入固定缓冲区，然后假设内容完整进行写操作
// 当 val 很大时，缓冲区内容被截断，但后续 strlen + 写入越界
// ============================================================
void test_format_write(int val) {
    char buf[8];
    memset(buf, 0, sizeof(buf));

    // 格式化写入固定缓冲区
    snprintf(buf, sizeof(buf), "%d", val);

    // 假设 buf 中有完整内容，进行后续操作
    if (buf[0] != '\0') {
        int len = strlen(buf);
        // 当 len == sizeof(buf) - 1 时（内容被截断），
        // buf[len] 写入的是终止符位置；但当 val 为多位数时，
        // buf 容量 (8 字节) 不足以容纳较大整数
        if (len < (int)sizeof(buf)) {
            buf[len] = '!';  // 正常情况下 OK，但若 len 计算溢出则越界
        }
    }
}

// ============================================================
// 主函数
// ============================================================
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) return 1;

    char input[64] = {0};
    if (fgets(input, sizeof(input) - 1, f) == NULL) {
        fclose(f);
        return 1;
    }
    fclose(f);

    // 去除换行符
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') input[len - 1] = '\0';

    int val = atoi(input);
    printf("--- StringProcessor with value: %d ---\n", val);

    test_string_copy(val);
    test_string_concat(val);
    test_string_split(val);
    test_format_write(val);

    // 打印数组状态报告
    __afl_array_state_print_report();

    return 0;
}
