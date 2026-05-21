// array_analysis_pass.cpp
// 静态分析模块 — 识别数组变量并计算影响力分数
// 基于 LLVM Pass 框架，遍历 IR 中的 GEP 指令提取数组信息
//
// 编译: clang-11 -shared -fPIC -o array_analysis_pass.so array_analysis_pass.cpp `llvm-config-11 --cxxflags --ldflags`
//
// 在 AFLGO 流程中使用:
//   opt-11 -load ./array_analysis_pass.so -array-analysis < input.bc > /dev/null

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/LinkAllPasses.h"
#include <map>
#include <vector>
#include <string>
#include <set>
#include <cmath>
#include <cstdio>

using namespace llvm;

namespace {

// 存储单个数组变量的分析结果
struct ArrayInfo {
    std::string   name;              // 变量名
    std::string   type_str;          // 类型字符串
    uint64_t      length;            // 数组长度
    unsigned      dim_count;         // 维度数
    std::string   scope;             // 作用域: Local/Global/Parameter
    float         influence_score;   // 影响力分数 (0.0 ~ 1.0)
    std::set<unsigned> related_lines; // 关联的代码行号
};

class ArrayAnalysisPass : public ModulePass {
public:
    static char ID;
    ArrayAnalysisPass() : ModulePass(ID) {}

    bool runOnModule(Module &M) override {
        errs() << "=========================================\n";
        errs() << " 数组分析 Pass — 静态分析\n";
        errs() << "=========================================\n";

        // 遍历所有函数
        for (auto &F : M) {
            if (F.isDeclaration()) continue;
            analyzeFunction(F);
        }

        // 输出汇总结果
        printSummary();

        return false; // 不修改 IR
    }

private:
    // 函数名 -> 该函数中的数组变量列表
    std::map<std::string, std::vector<ArrayInfo>> arrayResults;

    // 去掉 LLVM SSA 编号后缀（method4 → method, headers50 → headers）
    static std::string normalizeName(const std::string &name) {
        std::string base = name;
        while (!base.empty() && std::isdigit(base.back())) {
            base.pop_back();
        }
        return base.empty() ? name : base;
    }

    // 分析单个函数
    void analyzeFunction(Function &F) {
        std::string funcName = F.getName().str();
        errs() << "\n>>>>> 分析单个函数: " << funcName << " <<<<<\n";

        // 按归一化名去重：同名取最大影响力，合并行号
        std::map<std::string, ArrayInfo> arrayMap;

        for (auto &BB : F) {
            for (auto &Inst : BB) {
                // 检测 GEP 指令
                auto *GEP = dyn_cast<GetElementPtrInst>(&Inst);
                if (!GEP) continue;

                Type *SourceTy = GEP->getSourceElementType();
                // 只处理数组类型，跳过指针运算
                if (!SourceTy->isArrayTy()) continue;

                ArrayInfo info;
                collectArrayInfo(GEP, F, info);
                info.name = normalizeName(info.name);
                std::string key = info.name + "@" + info.scope;

                auto it = arrayMap.find(key);
                if (it != arrayMap.end()) {
                    // 去重：保留最大影响力，合并行号
                    if (info.influence_score > it->second.influence_score)
                        it->second.influence_score = info.influence_score;
                    it->second.related_lines.insert(info.related_lines.begin(),
                                                    info.related_lines.end());
                } else {
                    arrayMap[key] = info;
                }

            }
        }

        // 去重后输出
        for (auto &entry : arrayMap) {
            auto &arr = entry.second;
            char scoreBuf[8];
            snprintf(scoreBuf, sizeof(scoreBuf), "%.3f", arr.influence_score);
            errs() << "  [数组] " << arr.name
                   << " | 类型: " << arr.type_str
                   << " | 长度: " << arr.length
                   << " | 作用域: " << arr.scope
                   << " | 影响力: " << scoreBuf << "\n";
        }

        if (!arrayMap.empty()) {
            std::vector<ArrayInfo> funcArrays;
            for (auto &entry : arrayMap) {
                funcArrays.push_back(entry.second);
            }
            arrayResults[funcName] = funcArrays;
        }
    }

    // 收集数组变量信息
    void collectArrayInfo(GetElementPtrInst *GEP, Function &F, ArrayInfo &info) {
        Value *BasePtr = GEP->getPointerOperand();
        Type *SourceTy = GEP->getSourceElementType();

        // 变量名
        info.name = BasePtr->hasName() ? BasePtr->getName().str() : "anonymous";

        // 类型字符串
        raw_string_ostream rso(info.type_str);
        SourceTy->print(rso);

        // 数组长度
        info.length = SourceTy->getArrayNumElements();

        // 维度计数（通过追溯嵌套 GEP 链）
        info.dim_count = 0;
        Value *Ptr = BasePtr;
        while (auto *InnerGEP = dyn_cast<GetElementPtrInst>(Ptr)) {
            Ptr = InnerGEP->getPointerOperand();
            info.dim_count++;
        }

        // 作用域
        if (isa<GlobalVariable>(BasePtr)) {
            info.scope = "全局";
        } else if (isa<Argument>(BasePtr)) {
            info.scope = "参数";
        } else {
            info.scope = "局部 (栈)";
        }

        // 计算影响力分数
        info.influence_score = calculateInfluence(GEP, F);

        // 获取行号
        if (DILocation *Loc = GEP->getDebugLoc()) {
            info.related_lines.insert(Loc->getLine());
        }
    }

    // 计算变量影响力分数
    // 评估标准：
    //   +0.3 出现在条件分支（icmp）中
    //   +0.2 是循环控制变量（与 phi 节点相关）
    //   +0.2 数据依赖链较长（经过多次赋值）
    //   +0.1 是函数参数
    //   +0.1 出现在存储指令中
    float calculateInfluence(GetElementPtrInst *GEP, Function &F) {
        float score = 0.3f; // 基础分：数组变量本身

        Value *BasePtr = GEP->getPointerOperand();

        // 函数参数加分
        if (isa<Argument>(BasePtr)) {
            score += 0.2f;
        }

        // 分析索引值是否来自条件分支或循环
        Value *IdxVal = GEP->getOperand(GEP->getNumOperands() - 1);

        // 追溯索引的来源，判断是否与条件分支或循环相关
        if (auto *Load = dyn_cast<LoadInst>(IdxVal)) {
            Value *Src = Load->getPointerOperand();
            for (auto &BB : F) {
                for (auto &Inst : BB) {
                    if (auto *Br = dyn_cast<BranchInst>(&Inst)) {
                        if (Br->isConditional()) {
                            auto *ICmp = dyn_cast<ICmpInst>(Br->getCondition());
                            if (ICmp && (ICmp->getOperand(0) == Src ||
                                         ICmp->getOperand(1) == Src))
                                score += 0.3f;
                        }
                    }
                    if (isa<PHINode>(&Inst)) {
                        for (unsigned i = 0; i < Inst.getNumOperands(); i++)
                            if (Inst.getOperand(i) == Src) score += 0.2f;
                    }
                }
            }
        }

        // 如果 GEP 的用户中包含 StoreInst，加分
        for (User *U : GEP->users()) {
            if (isa<StoreInst>(U)) {
                score += 0.1f;
                break;
            }
        }

        // 归一化到 [0, 1]
        if (score > 1.0f) score = 1.0f;
        return score;
    }

    void printSummary() {
        errs() << "\n=========================================\n";
        errs() << " 总结: 数组变量发现情况 (已去重)\n";
        errs() << "=========================================\n";
        int totalArrays = 0;
        for (auto &entry : arrayResults) {
            for (auto &arr : entry.second) {
                totalArrays++;
            char scoreBuf[8];
            snprintf(scoreBuf, sizeof(scoreBuf), "%.3f", arr.influence_score);
            errs() << "  " << entry.first << "::" << arr.name
                   << " | 长度: " << arr.length
                   << " | 维度: " << arr.dim_count
                   << " | 影响力: " << scoreBuf << "\n";
            }
        }
        errs() << "唯一数组变量总数: " << totalArrays << "\n";
        errs() << "=========================================\n";
    }
};

char ArrayAnalysisPass::ID = 0;
static RegisterPass<ArrayAnalysisPass> X("array-analysis",
    "Array Variable Analysis Pass",
    false /* does not modify CFG */,
    false /* is not a transformation */);

} // namespace
