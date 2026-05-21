#!/bin/bash
# build_all_config_b.sh
# Build libxml2 and jasper with AFLGo + Array State Instrumentation (Config B)
# Uses -fpass-plugin= to inject the array pass at compile time
set -e

AFLGO_CLANG="/opt/aflgo/instrument/aflgo-clang"
INST_PASS="/workspace/passes/build/libarray_instrument_pass.so"
RUNTIME_LIB="/workspace/runtime/libarray_state.a"
RUNTIME_DIR="/workspace/runtime"

echo "=========================================="
echo " Building Config B: libxml2 + jasper"
echo "=========================================="

# =====================================================
# libxml2 Config A (AFLGO baseline)
# =====================================================
build_libxml2_A() {
    local SRC="/workspace/test_subjects/libxml2"
    local BUILD_A="$SRC/obj-aflgo"
    echo ""
    echo "--- libxml2 Config A (AFLGO baseline) ---"
    mkdir -p "$BUILD_A" && cd "$BUILD_A"
    if [ -f "xmllint" ]; then
        echo "  Already built, skipping."
        return
    fi
    $SRC/configure --without-python --without-ftp --without-http \
        --without-iconv --without-zlib --without-lzma \
        CC="$AFLGO_CLANG" CFLAGS="-g -O2" 2>&1 | tail -3
    make -j$(nproc) 2>&1 | tail -5
    cp xmllint xmllint_A
    echo "  → xmllint_A built"
}

# =====================================================
# libxml2 Config B (AFLGO + array)
# =====================================================
build_libxml2_B() {
    local SRC="/workspace/test_subjects/libxml2"
    local BUILD_B="$SRC/obj-aflgo-array"
    echo ""
    echo "--- libxml2 Config B (AFLGO + array) ---"
    mkdir -p "$BUILD_B" && cd "$BUILD_B"
    if [ -f "xmllint_B" ]; then
        echo "  Already built, skipping."
        return
    fi
    $SRC/configure --without-python --without-ftp --without-http \
        --without-iconv --without-zlib --without-lzma \
        CC="$AFLGO_CLANG" \
        CFLAGS="-g -O2 -fpass-plugin=$INST_PASS" \
        LDFLAGS="-L$RUNTIME_DIR" \
        LIBS="-larray_state -lm" 2>&1 | tail -3
    make -j$(nproc) 2>&1 | tail -5
    cp xmllint xmllint_B
    echo "  → xmllint_B built"
}

# =====================================================
# jasper Config A (AFLGO baseline)
# =====================================================
build_jasper_A() {
    local SRC="/workspace/test_subjects/jasper"
    local BUILD_A="$SRC/obj-aflgo"
    echo ""
    echo "--- jasper Config A (AFLGO baseline) ---"
    mkdir -p "$BUILD_A" && cd "$BUILD_A"
    if [ -f "src/appl/jasper" ]; then
        echo "  Already built, skipping."
        return
    fi
    cmake "$SRC" \
        -DCMAKE_C_COMPILER="$AFLGO_CLANG" \
        -DCMAKE_CXX_COMPILER="$AFLGO_CLANG" \
        -DJAS_ENABLE_SHARED=OFF \
        -DJAS_ENABLE_PROGRAMS=ON \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS="-g -O2" \
        -DCMAKE_CXX_FLAGS="-g -O2" 2>&1 | tail -3
    make -j$(nproc) 2>&1 | tail -5
    cp src/appl/jasper jasper_A
    echo "  → jasper_A built"
}

# =====================================================
# jasper Config B (AFLGO + array)
# =====================================================
build_jasper_B() {
    local SRC="/workspace/test_subjects/jasper"
    local BUILD_B="$SRC/obj-aflgo-array"
    echo ""
    echo "--- jasper Config B (AFLGO + array) ---"
    mkdir -p "$BUILD_B" && cd "$BUILD_B"
    if [ -f "src/appl/jasper" ]; then
        echo "  Already built, skipping."
        return
    fi
    cmake "$SRC" \
        -DCMAKE_C_COMPILER="$AFLGO_CLANG" \
        -DCMAKE_CXX_COMPILER="$AFLGO_CLANG" \
        -DJAS_ENABLE_SHARED=OFF \
        -DJAS_ENABLE_PROGRAMS=ON \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS="-g -O2 -fpass-plugin=$INST_PASS" \
        -DCMAKE_CXX_FLAGS="-g -O2 -fpass-plugin=$INST_PASS" \
        -DCMAKE_EXE_LINKER_FLAGS="-L$RUNTIME_DIR -larray_state" 2>&1 | tail -3
    make -j$(nproc) 2>&1 | tail -5
    cp src/appl/jasper jasper_B
    echo "  → jasper_B built"
}

# =====================================================
# Main
# =====================================================
case "${1:-all}" in
    libxml2)
        build_libxml2_A
        build_libxml2_B
        ;;
    jasper)
        build_jasper_A
        build_jasper_B
        ;;
    all)
        build_libxml2_A
        build_libxml2_B
        build_jasper_A
        build_jasper_B
        ;;
    *)
        echo "Usage: $0 {libxml2|jasper|all}"
        exit 1
        ;;
esac

echo ""
echo "=========================================="
echo " Build complete!"
echo "=========================================="
