#!/bin/bash
# http_parser_test.sh — HTTP 解析器测试辅助脚本
# 用于在 CMake 中避免复杂的转义问题

APP="$1"
if [ -z "$APP" ]; then
    echo "Usage: $0 <http_parser_app>"
    exit 1
fi

# 测试1：正常请求
python3 -c "
import sys
sys.stdout.buffer.write(b'GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n')
" > input.txt
echo "=== Test 1: Normal request ==="
"$APP" input.txt || true
echo

# 测试2：长 URI（potential OOB on uri[256]）
python3 -c "
import sys
sys.stdout.buffer.write(b'GET /' + b'A'*260 + b' HTTP/1.1\r\n\r\n')
" > input.txt
echo "=== Test 2: Long URI (260 bytes) ==="
"$APP" input.txt || true
echo

# 测试3：大 Content-Length（potential OOB on body[1024]）
python3 -c "
import sys
sys.stdout.buffer.write(b'POST /upload HTTP/1.1\r\nContent-Length: 2048\r\n\r\n' + b'B'*2048)
" > input.txt
echo "=== Test 3: Large Content-Length (2048) ==="
"$APP" input.txt || true

rm -f input.txt
