// array_instrument_pass.cpp
// 数组状态监控插桩模块 — 在 GEP 指令后插入监控代码
//
// 在 AFLGO 流程中使用:
//   opt-11 -load ./array_instrument_pass.so -array-instrument < input.bc > output.bc

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/CFG.h"
#include "llvm/ADT/StringMap.h"
#include <string>
#include <set>

using namespace llvm;

namespace {

// FNV-1a 哈希生成稳定的 ArrayID
static uint32_t generateArrayID(const std::string &funcName, const std::string &varName) {
    std::string input = funcName + "::" + varName;
    uint32_t hash = 2166136261u;
    for (char c : input) {
        hash ^= (uint8_t)c;
        hash *= 16777619u;
    }
    return hash;
}

class ArrayInstrumentPass : public ModulePass {
public:
    static char ID;
    ArrayInstrumentPass() : ModulePass(ID) {}

    bool runOnModule(Module &M) override {
        errs() << "=========================================\n";
        errs() << " 数组插桩模块 — 数组状态监控\n";
        errs() << "=========================================\n";

        // 获取或声明运行时报告函数的声明
        LLVMContext &Ctx = M.getContext();
        FunctionCallee ReportFunc = M.getOrInsertFunction(
            "__afl_report_array_state",
            Type::getVoidTy(Ctx),
            Type::getInt32Ty(Ctx),   // array_id
            Type::getInt64Ty(Ctx),   // index
            Type::getInt32Ty(Ctx),   // access_type (bit0:read, bit1:write)
            Type::getInt32Ty(Ctx)    // line_number
        );

        bool modified = false;

        // 遍历所有函数
        for (auto &F : M) {
            if (F.isDeclaration()) continue;
            if (instrumentFunction(F, ReportFunc)) {
                modified = true;
            }
        }

        errs() << "插桩完成\n";
        return modified;
    }

private:
    bool instrumentFunction(Function &F, FunctionCallee &ReportFunc) {
        std::string funcName = F.getName().str();
        LLVMContext &Ctx = F.getContext();
        IRBuilder<> Builder(Ctx);
        bool modified = false;

        // 记录所有需要插桩的 GEP 指令
        std::vector<GetElementPtrInst *> targetGEPs;

        for (auto &BB : F) {
            for (auto &Inst : BB) {
                auto *GEP = dyn_cast<GetElementPtrInst>(&Inst);
                if (!GEP) continue;

                Type *SourceTy = GEP->getSourceElementType();
                if (!SourceTy->isArrayTy()) continue;

                targetGEPs.push_back(GEP);
            }
        }

        if (targetGEPs.empty()) return false;

        // 对每个 GEP 指令插桩
        for (auto *GEP : targetGEPs) {
            // 确定访问类型
            uint32_t accessType = 0;
            for (User *U : GEP->users()) {
                if (isa<LoadInst>(U)) accessType |= 1;  // bit0 = read
                else if (auto *SI = dyn_cast<StoreInst>(U)) {
                    if (SI->getPointerOperand() == GEP) accessType |= 2; // bit1 = write
                }
            }
            if (accessType == 0) continue;

            // 获取行号
            uint32_t lineNum = 0;
            if (DILocation *Loc = GEP->getDebugLoc()) {
                lineNum = Loc->getLine();
            }

            // 生成数组 ID（支持多维数组）
            Value *BasePtr = GEP->getPointerOperand();
            std::string varName;
            int dimDepth = 0;
            Value *RootPtr = BasePtr;

            while (auto *InnerGEP = dyn_cast<GetElementPtrInst>(RootPtr)) {
                RootPtr = InnerGEP->getPointerOperand();
                dimDepth++;
            }

            if (RootPtr->hasName()) {
                varName = RootPtr->getName().str();
            } else if (BasePtr->hasName()) {
                varName = BasePtr->getName().str();
            } else {
                varName = "anon";
                // 尝试从调试信息获取变量名
                if (DILocation *Loc = GEP->getDebugLoc()) {
                    auto *Scope = Loc->getScope();
                    if (Scope) {
                        for (auto &BB : F) {
                            for (auto &Inst : BB) {
                                if (auto *DbgDecl = dyn_cast<DbgDeclareInst>(&Inst)) {
                                    if (DbgDecl->getAddress() == RootPtr ||
                                        DbgDecl->getAddress() == BasePtr) {
                                        if (auto *Var = DbgDecl->getVariable()) {
                                            varName = Var->getName().str();
                                            break;
                                        }
                                    }
                                }
                            }
                            if (varName != "anon") break;
                        }
                    }
                }
            }

            // 去 SSA 编号后缀：method22 → method
            {
                std::string base = varName;
                while (!base.empty() && std::isdigit(base.back()))
                    base.pop_back();
                if (!base.empty()) varName = base;
            }

            // 多维数组维度区分
            if (dimDepth > 0) {
                varName += "#dim" + std::to_string(dimDepth);
            }
            uint32_t arrayID = generateArrayID(funcName, varName);

            // 提取索引值
            Value *RawIdx = GEP->getOperand(GEP->getNumOperands() - 1);
            if (!RawIdx->getType()->isIntegerTy()) continue;

            // 插桩：在 GEP 之后插入 __afl_report_array_state 调用
            Builder.SetInsertPoint(GEP->getNextNode());
            Value *Idx64 = Builder.CreateZExtOrTrunc(RawIdx, Type::getInt64Ty(Ctx));

            Builder.CreateCall(ReportFunc, {
                Builder.getInt32(arrayID),
                Idx64,
                Builder.getInt32(accessType),
                Builder.getInt32(lineNum)
            });

            errs() << "  [插桩] " << funcName << "::" << varName
                   << " (ID=0x" << Twine::utohexstr(arrayID)
                   << ", 行号=" << lineNum
                   << ", 访问类型=" << accessType << ")\n";

            modified = true;
        }

        return modified;
    }
};

char ArrayInstrumentPass::ID = 0;
static RegisterPass<ArrayInstrumentPass> X("array-instrument",
    "Array State Instrumentation Pass",
    false /* does not modify CFG */,
    false /* is not a transformation */);

// Register with clang's PassManagerBuilder so the pass runs during
// normal compilation (e.g. when loaded via -Xclang -load).
// EP_OptimizerLast = run at the end of the optimization pipeline,
// before codegen. The AFLGo pass also runs at this point via
// a similar registration, ensuring both passes are applied.
static void registerArrayPass(const PassManagerBuilder &,
                              legacy::PassManagerBase &PM) {
    PM.add(new ArrayInstrumentPass());
}
static RegisterStandardPasses RegisterArrayPass(
    PassManagerBuilder::EP_OptimizerLast, registerArrayPass);

} // namespace
