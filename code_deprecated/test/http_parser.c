// http_parser.c — 微型 HTTP 请求解析器的数组越界测试目标
//
// 模拟真实世界 HTTP 服务器中请求解析的常见缓冲区操作，
// 包含多种数组类型（固定长度字符数组、结构体数组、嵌套缓冲区）
// 和多处可触发越界的代码路径。
//
// 解析格式:
//   METHOD /uri HTTP/1.1\r\n
//   Header-Name: header-value\r\n
//   \r\n
//   [body]
//
// 越界场景:
//   1. method 字符串超过 7 字节 → method[8] 缓冲区溢出
//   2. URI 超过 255 字节 → uri[256] 缓冲区溢出
//   3. 请求头超过 32 个 → headers[32] 数组越界
//   4. 请求头名称超过 63 字节 → header.name[64] 溢出
//   5. 请求头值超过 127 字节 → header.value[128] 溢出
//   6. Content-Length 值大于 1024 → body[1024] 缓冲区溢出
//   7. body 写入时逐字节索引访问 → 多处数组索引操作

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void __afl_array_state_print_report(void);

// ============================================================
// 数据结构定义
// ============================================================

#define MAX_METHOD_LEN   8
#define MAX_URI_LEN      256
#define MAX_HEADERS      32
#define MAX_HEADER_NAME  64
#define MAX_HEADER_VALUE 128
#define MAX_BODY_LEN     1024
#define MAX_INPUT_LEN    8192

typedef struct {
    char name[MAX_HEADER_NAME];
    char value[MAX_HEADER_VALUE];
} http_header_t;

typedef struct {
    char          method[MAX_METHOD_LEN];
    char          uri[MAX_URI_LEN];
    int           num_headers;
    http_header_t headers[MAX_HEADERS];
    char          body[MAX_BODY_LEN];
    int           body_len;
} http_request_t;

// ============================================================
// 场景1: 解析请求行 "METHOD /uri HTTP/1.1\r\n"
// method 和 uri 写入固定大小缓冲区，无长度检查
// ============================================================
static int parse_request_line(const char *input, int len, int *pos,
                               http_request_t *req) {
    int p = *pos;
    int method_start = p;

    // 提取 METHOD — 无边界检查写入 method[8]
    {
        int i = 0;
        while (p < len && input[p] != ' ' && input[p] != '\r' && input[p] != '\n') {
            if (i < MAX_METHOD_LEN - 1) {
                req->method[i] = input[p];
            } else {
                // 越界路径: 当 method 字符串长度 >= 7 时触发
                req->method[i] = input[p];  // 潜在 OOB 写入
            }
            i++;
            p++;
        }
        req->method[i < MAX_METHOD_LEN ? i : MAX_METHOD_LEN - 1] = '\0';
    }

    if (p >= len || input[p] != ' ') return -1;
    p++; // 跳过空格

    // 提取 URI — 无边界检查写入 uri[256]
    {
        int i = 0;
        while (p < len && input[p] != ' ' && input[p] != '\r' && input[p] != '\n') {
            if (i < MAX_URI_LEN - 1) {
                req->uri[i] = input[p];
            } else {
                // 越界路径: URI 长度 >= 255 时触发
                req->uri[i] = input[p];  // 潜在 OOB 写入
            }
            i++;
            p++;
        }
        req->uri[i < MAX_URI_LEN ? i : MAX_URI_LEN - 1] = '\0';
    }

    if (p >= len || input[p] != ' ') return -1;
    p++; // 跳过空格

    // 跳过 HTTP 版本号 (HTTP/1.1)
    while (p < len && input[p] != '\r' && input[p] != '\n') p++;

    // 跳过 \r\n
    if (p + 1 < len && input[p] == '\r' && input[p+1] == '\n') p += 2;
    else if (p < len && input[p] == '\n') p += 1;
    else return -1;

    *pos = p;
    return 0;
}

// ============================================================
// 场景2: 解析单个请求头 "Name: value\r\n"
// name 和 value 写入固定大小的 header 结构体字段
// 同时涉及结构体数组 headers[] 的索引访问
// ============================================================
static int parse_one_header(const char *input, int len, int *pos,
                             http_header_t *hdr) {
    int p = *pos;
    memset(hdr, 0, sizeof(http_header_t));

    // 检查是否到达空行（请求头结束标志）
    if (p + 1 < len && input[p] == '\r' && input[p+1] == '\n') {
        *pos = p + 2;
        return 1; // 请求头解析完成
    }
    if (p < len && input[p] == '\n') {
        *pos = p + 1;
        return 1;
    }

    // 提取 header name
    {
        int i = 0;
        while (p < len && input[p] != ':' && input[p] != '\r' && input[p] != '\n') {
            if (i < MAX_HEADER_NAME - 1) {
                hdr->name[i] = input[p];
            } else {
                // 越界路径: header name 过长
                hdr->name[i] = input[p];
            }
            i++;
            p++;
        }
        hdr->name[i < MAX_HEADER_NAME ? i : MAX_HEADER_NAME - 1] = '\0';
    }

    if (p >= len || input[p] != ':') return -1;
    p++; // 跳过 ':'

    // 跳过冒号后的空白
    while (p < len && (input[p] == ' ' || input[p] == '\t')) p++;

    // 提取 header value
    {
        int i = 0;
        while (p < len && input[p] != '\r' && input[p] != '\n') {
            if (i < MAX_HEADER_VALUE - 1) {
                hdr->value[i] = input[p];
            } else {
                // 越界路径: header value 过长
                hdr->value[i] = input[p];
            }
            i++;
            p++;
        }
        hdr->value[i < MAX_HEADER_VALUE ? i : MAX_HEADER_VALUE - 1] = '\0';
    }

    // 跳过 \r\n
    if (p + 1 < len && input[p] == '\r' && input[p+1] == '\n') p += 2;
    else if (p < len && input[p] == '\n') p += 1;
    else return -1;

    *pos = p;
    return 0;
}

// ============================================================
// 场景3: 解析所有请求头
// headers[32] 数组索引在超过 32 个请求头时越界
// ============================================================
static int parse_headers(const char *input, int len, int *pos,
                          http_request_t *req) {
    req->num_headers = 0;
    int p = *pos;

    while (p < len) {
        if (req->num_headers < MAX_HEADERS) {
            int ret = parse_one_header(input, len, &p,
                                        &req->headers[req->num_headers]);
            if (ret == 1) { // 空行，请求头结束
                *pos = p;
                return 0;
            }
            if (ret < 0) return -1;
            req->num_headers++;
        } else {
            // 越界路径: 超过 32 个请求头
            // 仍尝试解析但写入越界位置
            int ret = parse_one_header(input, len, &p,
                                        &req->headers[req->num_headers]); // OOB!
            if (ret == 1) {
                *pos = p;
                return 0;
            }
            req->num_headers++;
        }
    }
    return -1;
}

// ============================================================
// 场景4: 解析消息体
// 根据 Content-Length 请求头决定读取的 body 长度
// 当 Content-Length > 1024 时，body[1024] 缓冲区溢出
// ============================================================
static int parse_body(const char *input, int len, int *pos,
                       http_request_t *req) {
    int p = *pos;
    int content_length = 0;

    // 查找 Content-Length 请求头
    for (int i = 0; i < req->num_headers; i++) {
        if (strcasecmp(req->headers[i].name, "Content-Length") == 0) {
            content_length = atoi(req->headers[i].value);
            break;
        }
    }

    if (content_length <= 0 || p >= len) {
        req->body_len = 0;
        return 0;
    }

    // 限制实际读取量
    int to_read = content_length;
    if (to_read > len - p) to_read = len - p;

    // 写入 body 缓冲区 — 无边界检查
    for (int i = 0; i < to_read; i++) {
        if (i < MAX_BODY_LEN) {
            req->body[i] = input[p + i]; // 安全写入
        } else {
            // 越界路径: Content-Length > 1024
            req->body[i] = input[p + i]; // OOB 写入!
        }
    }

    req->body_len = to_read;
    *pos = p + to_read;
    return 0;
}

// ============================================================
// 场景5: 模拟请求处理 — 对解析后的字段进行索引访问
// 在多个数组上执行读写操作，模拟实际 HTTP 服务器行为
// ============================================================
static void process_request(http_request_t *req) {
    char log_line[256];
    memset(log_line, 0, sizeof(log_line));

    // 构造日志行: "METHOD /uri" → log_line
    int pos = 0;
    for (int i = 0; req->method[i] != '\0' && pos < 255; i++) {
        log_line[pos++] = req->method[i];
    }
    log_line[pos++] = ' ';
    for (int i = 0; req->uri[i] != '\0' && pos < 255; i++) {
        log_line[pos++] = req->uri[i];
    }
    log_line[pos] = '\0';

    // 通过索引遍历请求头
    for (int h = 0; h <= req->num_headers; h++) { // 注意: <= 是刻意引入的 off-by-one
        if (h < req->num_headers) {
            // 正常访问: 读取 header name 的首字符
            char first_char = req->headers[h].name[0];
            (void)first_char;
        } else if (h == req->num_headers) {
            // off-by-one 越界读取
            char oob_char = req->headers[h].name[0]; // OOB read
            (void)oob_char;
        }
    }

    // 访问 body 中的特定偏移
    if (req->body_len > 0) {
        int check_positions[] = {0, 1, 100, 500, 1023, 1024, 2048};
        int num_positions = sizeof(check_positions) / sizeof(check_positions[0]);
        for (int i = 0; i < num_positions; i++) {
            int idx = check_positions[i];
            // 当 idx >= MAX_BODY_LEN 时越界
            char c = req->body[idx]; // 潜在 OOB 读取
            (void)c;
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

    // 读取整个输入文件
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;

    char *input = (char *)malloc(MAX_INPUT_LEN);
    if (!input) {
        fclose(f);
        return 1;
    }

    int total = (int)fread(input, 1, MAX_INPUT_LEN - 1, f);
    fclose(f);
    if (total <= 0) {
        free(input);
        return 1;
    }
    input[total] = '\0';

    // 分配并初始化请求结构体
    http_request_t req;
    memset(&req, 0, sizeof(req));

    int pos = 0;

    // 解析请求行
    if (parse_request_line(input, total, &pos, &req) < 0) {
        printf("Failed to parse request line\n");
        free(input);
        return 1;
    }

    // 解析请求头
    if (parse_headers(input, total, &pos, &req) < 0) {
        printf("Failed to parse headers\n");
        free(input);
        return 1;
    }

    // 解析消息体
    parse_body(input, total, &pos, &req);

    // 模拟请求处理
    process_request(&req);

    printf("Parsed: %s %s, %d headers, %d body bytes\n",
           req.method, req.uri, req.num_headers, req.body_len);

    free(input);

    // 打印数组状态报告
    __afl_array_state_print_report();

    return 0;
}
