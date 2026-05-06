cmake_minimum_required(VERSION 3.10)
project(MyArrayPass)

# 1. 寻找 LLVM
find_package(LLVM REQUIRED CONFIG)

# 打印信息
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# 2. 包含 LLVM 的头文件和定义
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# 3. 核心：获取 LLVM 的库名
llvm_map_components_to_libnames(llvm_libs support core irreader passes)

# 4. 定义 LLVM Pass 插件
add_library(MyArrayPass MODULE src/MyArrayPass.cpp)

# 5. 关键配置
set_target_properties(MyArrayPass PROPERTIES
    COMPILE_FLAGS "-fno-rtti"
    PREFIX ""
    SUFFIX ".so"
)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# 6. 编译运行时库 — 独立测试模式（无 AFL++ 依赖）
add_library(afl_array_state STATIC src/afl_array_state.c)
target_compile_definitions(afl_array_state PRIVATE AFL_RUNTIME=0)

# 7. AFL++ 集成编译目标（使用系统安装的 AFL++ 编译器）
find_program(AFL_CLANG_FAST afl-clang-fast PATHS /usr/local/bin NO_DEFAULT_PATH)
if(NOT AFL_CLANG_FAST)
    set(AFL_CLANG_FAST afl-clang-fast)
endif()

add_custom_target(compile_afl_test
    COMMAND ${AFL_CLANG_FAST} -O0 -g
        ${CMAKE_SOURCE_DIR}/test/test.c
        -include ${CMAKE_SOURCE_DIR}/src/afl_array_state.c
        -o ${CMAKE_BINARY_DIR}/test_afl_instr
    COMMENT "Compiling test program with AFL++ + array state instrumentation"
)

# AFL++ baseline（无数组插桩，用于对比实验）
add_custom_target(compile_afl_baseline
    COMMAND ${AFL_CLANG_FAST} -O0 -g
        ${CMAKE_SOURCE_DIR}/test/test.c
        -o ${CMAKE_BINARY_DIR}/test_afl_baseline
    COMMENT "Compiling baseline test (without array state pass)"
)

# 8. 总体验证目标：编译 → 插桩 → 链接 → 运行
add_custom_target(run_tests
    # 步骤1：生成原始 LLVM IR
    COMMAND clang-19 -S -emit-llvm -fno-discard-value-names -g -O0 -Xclang -disable-O0-optnone ${CMAKE_SOURCE_DIR}/test/test.c -o ${CMAKE_BINARY_DIR}/test.ll
    # 步骤2：用 MyArrayPass 进行插桩
    COMMAND opt-19 -load-pass-plugin=${CMAKE_BINARY_DIR}/MyArrayPass.so -passes="my-array-pass" ${CMAKE_BINARY_DIR}/test.ll -S -o ${CMAKE_BINARY_DIR}/test_instrumented.ll
    # 步骤3：编译插桩后的 IR + 链接运行时库 → 生成可执行文件
    COMMAND clang-19 ${CMAKE_BINARY_DIR}/test_instrumented.ll -L${CMAKE_BINARY_DIR} -lafl_array_state -o ${CMAKE_BINARY_DIR}/test_app
    # 步骤4：运行测试 — 使用 shell 包装确保即使越界 crash 也继续执行
    COMMAND echo "1" > ${CMAKE_BINARY_DIR}/input.txt && (${CMAKE_BINARY_DIR}/test_app ${CMAKE_BINARY_DIR}/input.txt || true)
    COMMAND echo "9" > ${CMAKE_BINARY_DIR}/input.txt && (${CMAKE_BINARY_DIR}/test_app ${CMAKE_BINARY_DIR}/input.txt || true)
    COMMAND echo "11" > ${CMAKE_BINARY_DIR}/input.txt && (${CMAKE_BINARY_DIR}/test_app ${CMAKE_BINARY_DIR}/input.txt || true)
    COMMAND echo "-1" > ${CMAKE_BINARY_DIR}/input.txt && (${CMAKE_BINARY_DIR}/test_app ${CMAKE_BINARY_DIR}/input.txt || true)
    COMMAND echo "33" > ${CMAKE_BINARY_DIR}/input.txt && (${CMAKE_BINARY_DIR}/test_app ${CMAKE_BINARY_DIR}/input.txt || true)
    COMMAND echo "9999" > ${CMAKE_BINARY_DIR}/input.txt && (${CMAKE_BINARY_DIR}/test_app ${CMAKE_BINARY_DIR}/input.txt || true)
    COMMAND rm -f ${CMAKE_BINARY_DIR}/input.txt
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running full instrumentation and test pipeline (crashes expected = array OOB detection working)"
)

add_dependencies(run_tests MyArrayPass afl_array_state)

# 9. 编译自定义变异器（custom mutator.so）
add_custom_target(build_mutator
    COMMAND ${CMAKE_C_COMPILER} -O2 -fPIC -shared
        ${CMAKE_SOURCE_DIR}/src/custom_mutator.c
        -o ${CMAKE_BINARY_DIR}/custom_mutator.so
    COMMENT "Building custom mutator shared library for AFL++"
)

# 10. AFL++ 模糊测试实验（快速演示）
# 使用方法：make fuzz_experiment
# 该目标会启动一个短时 AFL++ 测试会话，生成实验数据
add_custom_target(fuzz_experiment
    COMMAND mkdir -p ${CMAKE_BINARY_DIR}/experiment_seeds
    COMMAND echo "0" > ${CMAKE_BINARY_DIR}/experiment_seeds/seed1
    COMMAND echo "5" > ${CMAKE_BINARY_DIR}/experiment_seeds/seed2
    COMMAND echo "100" > ${CMAKE_BINARY_DIR}/experiment_seeds/seed3
    # 确保 AFL++ 在 tmux/screen 中运行时不会有终端问题
    COMMAND AFL_NO_UI=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
        afl-fuzz -i ${CMAKE_BINARY_DIR}/experiment_seeds
        -o ${CMAKE_BINARY_DIR}/experiment_output
        -V 30
        -- ${CMAKE_BINARY_DIR}/test_afl_instr @@
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running AFL++ fuzz experiment (30 seconds)"
)

# 11. 生成实验数据摘要
add_custom_target(experiment_report
    COMMAND echo "===== Fuzz Experiment Results ====="
    COMMAND echo "--- Instrumented (with array state pass) ---"
    COMMAND ls -la ${CMAKE_BINARY_DIR}/experiment_output/default/crashes/ 2>/dev/null || echo "No crashes found yet"
    COMMAND echo "--- Output stats ---"
    COMMAND cat ${CMAKE_BINARY_DIR}/experiment_output/default/fuzzer_stats 2>/dev/null | head -30
    COMMAND echo "==================================="
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
