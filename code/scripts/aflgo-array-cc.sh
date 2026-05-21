#!/bin/bash
# aflgo-array-cc.sh -- wrapper around aflgo-clang that applies the array
# instrumentation pass via the two-step process (.c -> .ll -> opt -> .o).
#
# Usage: export CC=/workspace/scripts/aflgo-array-cc.sh
#        export CXX=/workspace/scripts/aflgo-array-cc.sh

AFLGO_CLANG="/opt/aflgo/instrument/aflgo-clang"
INST_PASS="/workspace/passes/build/libarray_instrument_pass.so"

# If we are compiling a single C/C++ source to object file, use two-step.
# Heuristic: detect when aflgo-clang would produce a .o from a .c/.cpp.
is_compile_to_obj() {
    local have_c=false
    for arg in "$@"; do
        [[ "$arg" == -c ]] && have_c=true
    done
    $have_c
}

extract_source() {
    for arg in "$@"; do
        case "$arg" in
            *.c|*.cpp|*.cxx|*.cc|*.C) echo "$arg"; return ;;
        esac
    done
}

extract_output() {
    local next_is_o=false
    for arg in "$@"; do
        if $next_is_o; then echo "$arg"; return; fi
        [[ "$arg" == -o ]] && next_is_o=true || next_is_o=false
    done
    # If -c is present without -o, derive from source
    local src
    src=$(extract_source "$@")
    if [ -n "$src" ]; then
        local base
        base=$(basename "$src")
        echo "${base%.*}.o"
    fi
}

if is_compile_to_obj "$@"; then
    src=$(extract_source "$@")
    outf=$(extract_output "$@")

    if [ -n "$src" ] && [ -n "$outf" ]; then
        # Build path for intermediate .ll
        temp_ll="${outf}.__array_inst.ll"

        # Step 1: aflgo-clang -> LLVM IR (produce .ll)
        # We replace -o and suppress .o output, writing IR instead
        newargs=()
        skip_next=false
        for arg in "$@"; do
            if $skip_next; then skip_next=false; continue; fi
            if [ "$arg" = "-o" ]; then skip_next=true; continue; fi
            newargs+=("$arg")
        done
        # Replace .o output with .ll output
        $AFLGO_CLANG "${newargs[@]}" -S -emit-llvm -o "$temp_ll" 2>/dev/null
        rc=$?

        if [ $rc -eq 0 ] && [ -f "$temp_ll" ] && grep -q "getelementptr" "$temp_ll" 2>/dev/null; then
            # Step 2: opt -> array instrumentation
            # Use .ll extension so clang recognizes it as LLVM IR later
            inst_ll="${outf}.__array_inst.inst.ll"
            opt-11 -load "$INST_PASS" -array-instrument "$temp_ll" -S -o "$inst_ll" 2>/dev/null

            if [ $? -eq 0 ] && [ -f "$inst_ll" ]; then
                # Step 3: compile instrumented IR to .o
                clang-11 -c "$inst_ll" -o "$outf" 2>/dev/null
                clang_rc=$?
                rm -f "$temp_ll" "$inst_ll"
                exit $clang_rc
            fi
        fi

        # Fallback: clean up and run aflgo-clang normally
        rm -f "$temp_ll" "${temp_ll}.inst" 2>/dev/null
        exec $AFLGO_CLANG "$@"
    fi
fi

# Non-compile case: pass through
exec $AFLGO_CLANG "$@"
