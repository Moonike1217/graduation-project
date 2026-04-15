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
# 虽然插件通常不直接链接所有库，但映射这些组件有助于 CMake 处理依赖
llvm_map_components_to_libnames(llvm_libs support core irreader passes)

# 4. 定义插件
# 使用 MODULE 模式，因为这是一个动态加载的插件
add_library(MyArrayPass MODULE MyArrayPass.cpp)

# 5. 关键配置
set_target_properties(MyArrayPass PROPERTIES
    # 禁用 RTTI (运行时类型识别) 以匹配 LLVM 默认编译选项
    COMPILE_FLAGS "-fno-rtti"
    # 确保在 macOS 或其他系统上，插件可以看到外部符号
    PREFIX ""
    SUFFIX ".so"
)

# Ensure the shared library is placed in the correct directory
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# 6. 添加测试目标
add_custom_target(run_tests
    # COMMAND clang-19 -S -emit-llvm -fno-discard-value-names -g -O1 ${CMAKE_SOURCE_DIR}/test.c -o ${CMAKE_BINARY_DIR}/test.ll
    COMMAND clang-19 -S -emit-llvm -fno-discard-value-names -g -O0 -Xclang -disable-O0-optnone ${CMAKE_SOURCE_DIR}/test.c -o ${CMAKE_BINARY_DIR}/test.ll
    COMMAND opt-19 -load-pass-plugin=${CMAKE_BINARY_DIR}/MyArrayPass.so -passes="my-array-pass" ${CMAKE_BINARY_DIR}/test.ll -S -o ${CMAKE_BINARY_DIR}/test_instrumented.ll
    COMMAND clang-19 ${CMAKE_BINARY_DIR}/test_instrumented.ll -o ${CMAKE_BINARY_DIR}/test_app
    COMMAND ${CMAKE_BINARY_DIR}/test_app
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running tests with MyArrayPass"
)

# Add dependency to ensure MyArrayPass is built before running tests
add_dependencies(run_tests MyArrayPass)