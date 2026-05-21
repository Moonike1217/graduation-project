#!/bin/bash
# aflgo-array-compiler.sh
# Wrapper around aflgo-clang that adds the array instrumentation pass.
# Usage: export CC=/workspace/scripts/aflgo-array-compiler.sh
#        export CXX=/workspace/scripts/aflgo-array-compiler.sh
#
# The wrapper compiles to LLVM IR, applies the array pass, then assembles to .o

AFLGO_CLANG="/opt/aflgo/instrument/aflgo-clang"
INST_PASS="/workspace/passes/build/libarray_instrument_pass.so"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Only instrument C/C++ source files, not linker invocations or assembler
if echo "$@" | grep -qE '\-c\b.*\.(c|cpp|cxx|cc)$'; then
    # Extract the output file
    for ((i=1; i<=$#; i++)); do
        arg="${!i}"
        if [ "$arg" = "-o" ] && [ $i -lt $# ]; then
            next_idx=$((i+1))
            OUT_FILE="${!next_idx}"
        fi
    done

    # Generate a temp LLVM IR file
    SRC_FILE=""
    for ((i=1; i<=$#; i++)); do
        arg="${!i}"
        if [[ "$arg" == *.c ]] || [[ "$arg" == *.cpp ]] || [[ "$arg" == *.cxx ]] || [[ "$arg" == *.cc ]]; then
            SRC_FILE="$arg"
        fi
    done

    if [ -n "$SRC_FILE" ] && [ -n "$OUT_FILE" ]; then
        TEMP_LL="${OUT_FILE}.array_inst.ll"
        # Step 1: aflgo-clang -> LLVM IR
        $AFLGO_CLANG "$@" -S -emit-llvm -o "$TEMP_LL" 2>/dev/null
        if [ $? -eq 0 ] && [ -f "$TEMP_LL" ]; then
            # Step 2: opt -> array instrumentation
            # Use .ll extension so clang recognizes it as LLVM IR
            INST_LL="${OUT_FILE}.array_inst.inst.ll"
            opt-11 -load "$INST_PASS" -array-instrument "$TEMP_LL" -S -o "$INST_LL" 2>/dev/null
            if [ $? -eq 0 ] && [ -f "$INST_LL" ]; then
                # Step 3: compile instrumented IR to .o
                clang-11 -c "$INST_LL" -o "$OUT_FILE" 2>/dev/null
                rm -f "$TEMP_LL" "$INST_LL"
                exit $?
            fi
            rm -f "$TEMP_LL"
        fi
    fi
fi

# Fallback: just run aflgo-clang normally
exec $AFLGO_CLANG "$@"
