# AFLGO + Array State Diversity Fuzzing Tool
# 基于 Ubuntu 20.04 + LLVM 11 + AFLGO (github.com/aflgo/aflgo)

FROM ubuntu:20.04

LABEL description="AFLGO + Array State Diversity Fuzzing Environment"

# 避免交互式配置
ENV DEBIAN_FRONTEND=noninteractive

# 安装基本依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang-11 \
    llvm-11-dev \
    llvm-11-tools \
    llvm-11 \
    lld-11 \
    wget \
    git \
    python3 \
    python3-pip \
    python3-networkx \
    python3-pydot \
    graphviz \
    cmake \
    make \
    gcc \
    g++ \
    libtool \
    autoconf \
    automake \
    bison \
    flex \
    libglib2.0-dev \
    libpixman-1-dev \
    libboost-all-dev \
    vim \
    sudo \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# 设置 LLVM 环境
ENV LLVM_CONFIG=llvm-config-11
ENV CC=clang-11
ENV CXX=clang++-11

# aflgo-clang 底层调用 clang（不带版本号），需要建 symlink
RUN ln -sf /usr/bin/clang-11 /usr/local/bin/clang && \
    ln -sf /usr/bin/clang++-11 /usr/local/bin/clang++

# 下载和编译 AFLGO
WORKDIR /opt
RUN git clone https://github.com/aflgo/aflgo.git aflgo

# 编译 AFLGO 核心（基于 AFL 2.57b）
# AFL_NO_X86=1 跳过 gcc 模式的 x86 检查（我们用 LLVM 模式插桩）
WORKDIR /opt/aflgo/afl-2.57b
RUN AFL_NO_X86=1 make clean all

# 编译 AFLGO 插桩模块
WORKDIR /opt/aflgo/instrument
RUN make clean all

# 编译距离计算工具（ARM64 上 clang 不支持 -march=native，直接删掉）
WORKDIR /opt/aflgo/distance/distance_calculator
RUN sed -i 's/-march=native//g' CMakeLists.txt && \
    cmake . && \
    cmake --build .

# 安装 Python 依赖
RUN pip3 install networkx pydot

# 设置 AFLGO 环境变量
ENV AFLGO=/opt/aflgo
ENV PATH=$AFLGO/afl-2.57b:$AFLGO/instrument:$PATH
ENV LLVM_CONFIG=llvm-config-11

# 创建工作目录
WORKDIR /workspace

# 默认命令
CMD ["/bin/bash"]
