#!/bin/bash
# build_lrzip_config_b.sh
# Build lrzip with AFLGo + Array State Instrumentation (Config B)
#
# Usage: docker exec <container> bash /workspace/scripts/build_lrzip_config_b.sh
#
# This script:
# 1. Uses existing obj-aflgo build artifacts for lzma and other deps
# 2. Rebuilds core source files with array instrumentation pass
# 3. Links everything with the array runtime library
set -e

LRZIP_SRC="/tmp/lrzip"
BUILD_A="$LRZIP_SRC/obj-aflgo"
BUILD_B="$LRZIP_SRC/obj-aflgo-array"
INST_PASS="/workspace/passes/build/libarray_instrument_pass.so"
RUNTIME_LIB="/workspace/runtime/libarray_state.a"
AFLGO_CLANG="/opt/aflgo/instrument/aflgo-clang"
ARRAY_RUNTIME_INC="/workspace/runtime"

# Compiler flags (from Makefile)
DEFS="-DHAVE_CONFIG_H"
INCLUDES="-I. -I lzma/C -I obj-aflgo"
CFLAGS="-g -O2"

# Source files to instrument (core lrzip sources)
declare -a CORE_SOURCES=(
    "stream.c"
    "rzip.c"
    "runzip.c"
    "lrzip.c"
    "util.c"
    "md5.c"
    "aes.c"
    "sha4.c"
    "main.c"
    "liblrzip.c"
)

mkdir -p "$BUILD_B"
cd "$LRZIP_SRC"

echo "=========================================="
echo " Building lrzip Config B (AFLGo + Array)"
echo "=========================================="

# Stage 1: Compile core sources with array instrumentation
echo ""
echo "[1/4] Compiling core sources with array pass..."
for src in "${CORE_SOURCES[@]}"; do
    echo "  Processing $src..."
    base="${src%.c}"

    # Step 1a: aflgo-clang → LLVM IR (with edge coverage)
    $AFLGO_CLANG $DEFS $INCLUDES $CFLAGS \
        -S -emit-llvm "$src" \
        -o "$BUILD_B/${base}.ll" 2>/dev/null

    # Step 1b: opt → add array instrumentation
    opt-11 -load "$INST_PASS" -array-instrument \
        "$BUILD_B/${base}.ll" -S \
        -o "$BUILD_B/${base}_instr.ll" 2>/dev/null

    # Step 1c: clang → compile instrumented IR to .o
    clang-11 -c "$BUILD_B/${base}_instr.ll" \
        -o "$BUILD_B/${base}.o" 2>/dev/null

    echo "    → ${base}.o"
done

# Stage 2: Compile libzpaq (C++ file)
echo ""
echo "[2/4] Compiling libzpaq..."
if [ -f "libzpaq/libzpaq.cpp" ]; then
    $AFLGO_CLANG $DEFS $INCLUDES $CFLAGS \
        -S -emit-llvm "libzpaq/libzpaq.cpp" \
        -o "$BUILD_B/libzpaq.ll" 2>/dev/null

    opt-11 -load "$INST_PASS" -array-instrument \
        "$BUILD_B/libzpaq.ll" -S \
        -o "$BUILD_B/libzpaq_instr.ll" 2>/dev/null

    clang-11 -c "$BUILD_B/libzpaq_instr.ll" \
        -o "$BUILD_B/libzpaq.o" 2>/dev/null
    echo "    → libzpaq.o"
fi

# Stage 3: Copy lzma objects from Config A build (no array instrumentation needed)
echo ""
echo "[3/4] Copying lzma objects from Config A build..."
# Extract the lzma library objects
LZMA_DIR="$BUILD_B/lzma/C"
mkdir -p "$LZMA_DIR"

if [ -f "$BUILD_A/lzma/C/liblzma.la" ]; then
    # Use the pre-built lzma static library
    cp "$BUILD_A/lzma/C/.libs/liblzma.a" "$BUILD_B/liblzma.a" 2>/dev/null || \
    cp "$BUILD_A/lzma/C/liblzma.a" "$BUILD_B/liblzma.a" 2>/dev/null
    echo "    → liblzma.a copied"
fi

# Also copy other convenience library objects (non-instrumented)
# We only need them to resolve symbols
for obj in sha4.o md5.o aes.o util.o; do
    if [ ! -f "$BUILD_B/$obj" ]; then
        # Use the version from build A if our instrumented build failed
        [ -f "$BUILD_A/$obj" ] && cp "$BUILD_A/$obj" "$BUILD_B/$obj" && echo "    → $obj (from Config A)"
    fi
done

# Stage 4: Link everything
echo ""
echo "[4/4] Linking final binary..."
cd "$BUILD_B"

# Collect all instrumented .o files
INSTR_OBJS=""
for src in stream rzip runzip lrzip util md5 aes sha4 main liblrzip libzpaq; do
    if [ -f "${src}.o" ]; then
        INSTR_OBJS="$INSTR_OBJS ${src}.o"
    fi
done

# Link with AFLGo runtime + array runtime + lzma + system libs
clang++-11 -g -O2 $INSTR_OBJS \
    "$RUNTIME_LIB" \
    "/opt/aflgo/instrument/aflgo-runtime.o" \
    "$BUILD_B/liblzma.a" \
    -o "lrzip_array" \
    -lm -llzo2 -lbz2 -lz -lpthread 2>&1

echo ""
echo "=========================================="
echo " Config B binary built!"
echo "=========================================="
file "$BUILD_B/lrzip_array"
echo ""
echo "Checking for AFL instrumentation:"
strings "$BUILD_B/lrzip_array" | grep "__AFL_SHM_ID" | head -1
echo ""
echo "Checking for array instrumentation:"
strings "$BUILD_B/lrzip_array" | grep "__afl_report_array_state" | head -1
echo ""
ls -la "$BUILD_B/lrzip_array"
