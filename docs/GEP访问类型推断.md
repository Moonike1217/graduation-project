# 答辩备答：LLVM Pass 如何推断数组访问类型

## 问题

你的工具如何区分数组访问是读操作还是写操作？

## 一句话回答

通过在编译时遍历 GEP 指令的 user 链（use-def chain），检查该 GEP 的结果被 `LoadInst` 还是 `StoreInst` 使用，从而推断访问类型。

## 详细解释

### 1. 什么是 user 链？

LLVM IR 中，每条指令都会产生一个值（Value），其他指令如果要使用这个值，就会成为它的 User（使用者）。每个 Value 都维护了一个 user 列表，可以通过 `GEP->users()` 遍历。

### 2. 判定逻辑（代码位置：array_instrument_pass.cpp）

```cpp
uint32_t AccessType = 0;
for (User *U : GEP->users()) {        // 遍历所有使用该GEP结果的指令
    if (isa<LoadInst>(U))              // LoadInst → 读操作
        AccessType |= 1;               // 置 bit0
    else if (auto *SI = dyn_cast<StoreInst>(U))
        if (SI->getPointerOperand() == GEP)  // StoreInst且GEP是地址操作数 → 写操作
            AccessType |= 2;           // 置 bit1
}
```

### 3. 三种典型场景

| 场景 | C代码 | GEP的User | AccessType | 含义 |
|------|-------|-----------|------------|------|
| 纯读 | `x = buf[i]` | Load | 1 (bit0) | 读取数组元素 |
| 纯写 | `buf[i] = x` | Store | 2 (bit1) | 写入数组元素 |
| 读写 | `buf[i] += 1` | Load + Store | 3 (bit0+bit1) | 先读后写 |

### 4. 为什么需要额外检查 Store 的地址操作数？

Store 指令有两个操作数：**要存的值** 和 **要存的地址**。只有 GEP 作为"地址操作数"时才算写操作：

```llvm
%ptr = GEP ...
store float 999.0, float* %ptr  ① GEP 是地址 → 写操作（需检查）
store float* %ptr, ptr %other   ② GEP 是数据 → 不是写操作（排除）
```

因此用 `SI->getPointerOperand() == GEP` 确保只有情况①被识别为写访问。

## 与论文的对应

该方法对应论文第四章 §4.2.1 中"静态分析：数组属性提取"部分——`determine_access_type` 功能的具体实现。该分析在**编译时**完成，不产生运行时开销。

---

## 常见追问

### Q1：访问类型只需要两个 bit，为什么用 32 位整数？

#### 一句话回答

没有实质性的理由——用 `uint32_t` 只是因为 LLVM 里 `i32` 是默认整数类型，方便且零额外开销。

#### 三个考量

**① LLVM 的惯例**

声明函数签名时用了 `Type::getInt32Ty(Ctx)`——在 LLVM Pass 里到处都在用 i32，和其他参数（array_id、line）保持一致更自然。

**② ABI 层面没有区别**

| 类型 | 传参占用 | 是否需要额外指令 |
|------|----------|-----------------|
| `uint8_t` | 1 个寄存器 | 需要零扩展成 32 位 |
| `uint32_t` | 1 个寄存器 | 直接传，无需转换 |

实际上 `uint8_t` 传参也是用一个寄存器，反而编译器要多做一次零扩展对齐调用约定，**用 i32 甚至更高效**。

**③ 预留余量**

现在只用了 bit0（读）和 bit1（写），未来如果需要增加 volatile 访问、原子操作等标志位，直接加 bit 就行，不用改函数签名和插桩代码。

#### 本质

> 在 LLVM IR 层面，`i8` 和 `i32` 的传参开销完全相同。在两个方案没有差异的前提下，选择了和周围代码风格一致的 `i32`。
