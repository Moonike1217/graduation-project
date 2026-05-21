# Array State-Guided Fuzzing Tool — Passes Build

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 LLVM-11
find_package(LLVM 11 REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# 1. 静态分析模块 — 数组变量识别与影响力评估
add_library(array_analysis_pass SHARED array_analysis_pass.cpp)
target_link_libraries(array_analysis_pass ${LLVM_LIBS})
target_compile_options(array_analysis_pass PRIVATE -fno-rtti -fPIC)

# 2. 插桩模块 — 数组状态监控代码插入
add_library(array_instrument_pass SHARED array_instrument_pass.cpp)
target_link_libraries(array_instrument_pass ${LLVM_LIBS})
target_compile_options(array_instrument_pass PRIVATE -fno-rtti -fPIC)

# 安装目标
install(TARGETS array_analysis_pass array_instrument_pass
        LIBRARY DESTINATION lib)
