#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Format.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/ADT/StringMap.h"

#include <cstring>
#include <string>

using namespace llvm;

namespace {

// 为数组变量生成稳定的 32 位 ID
// 使用 FNV-1a 哈希函数名 + 变量名，确保同源码每次编译 ID 一致
static uint32_t generateArrayID(const std::string &funcName, const std::string &varName) {
    std::string input = funcName + "::" + varName;
    uint32_t hash = 2166136261u;
    for (char c : input) {
        hash ^= (uint8_t)c;
        hash *= 16777619u;
    }
    return hash;
}

struct MyArrayPass : public PassInfoMixin<MyArrayPass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        // 跳过声明和内部的报告函数
        if (F.isDeclaration() || F.getName().starts_with("__afl_report_array") ||
            F.getName().starts_with("__afl_array_state"))
            return PreservedAnalyses::all();

        LLVMContext &Ctx = F.getContext();
        Module *M = F.getParent();

        SmallVector<GetElementPtrInst *, 32> TargetGEPs;

        errs() << "\n>>>>> [Static Report] Analyzing Function: " << F.getName() << " <<<<<\n";

        // 静态分析：扫描所有GEP指令
        for (auto &BB : F) {
            for (auto &Inst : BB) {
                if (auto *GEP = dyn_cast<GetElementPtrInst>(&Inst)) {
                    TargetGEPs.push_back(GEP);

                    Value *BasePtr = GEP->getPointerOperand();
                    std::string ArrayName = BasePtr->hasName() ? BasePtr->getName().str() : "tmp_ptr";

                    Type *SourceTy = GEP->getSourceElementType();
                    std::string TypeStr;
                    raw_string_ostream RSO(TypeStr);
                    SourceTy->print(RSO);

                    uint64_t ArrayLen = 0;
                    if (SourceTy->isArrayTy())
                        ArrayLen = SourceTy->getArrayNumElements();

                    // 确定作用域
                    std::string Scope = "Local (Stack)";
                    if (isa<GlobalVariable>(BasePtr)) Scope = "Global";
                    else if (isa<Argument>(BasePtr)) Scope = "Parameter";

                    errs() << "[Array Found] Name: " << ArrayName
                           << " | Type: " << TypeStr
                           << " | Len: " << ArrayLen
                           << " | Scope: " << Scope << "\n";
                }
            }
        }

        if (TargetGEPs.empty()) return PreservedAnalyses::all();

        // 获取报告函数声明
        FunctionCallee ReportFn = M->getOrInsertFunction(
            "__afl_report_array",
            Type::getVoidTy(Ctx),
            Type::getInt32Ty(Ctx),  // ArrayID
            Type::getInt64Ty(Ctx),  // Index
            Type::getInt32Ty(Ctx),  // AccessType (bit0:读, bit1:写)
            Type::getInt32Ty(Ctx)   // LineNumber
        );

        IRBuilder<> Builder(Ctx);

        // 插桩每个GEP指令
        for (auto *GEP : TargetGEPs) {
            // 确定访问类型
            uint32_t AccessType = 0;
            for (User *U : GEP->users()) {
                if (isa<LoadInst>(U)) AccessType |= 1;
                else if (auto *SI = dyn_cast<StoreInst>(U))
                    if (SI->getPointerOperand() == GEP) AccessType |= 2;
            }
            if (AccessType == 0) continue;

            // 获取行号
            uint32_t LineNum = 0;
            if (DILocation *Loc = GEP->getDebugLoc())
                LineNum = Loc->getLine();

            // === 改进：使用稳定的 ArrayID（基于函数名+变量名的 FNV-1a 哈希）===
            Value *BasePtr = GEP->getPointerOperand();

            // 对于多维数组的中间GEP，回溯找到最外层数组名
            std::string VarName = BasePtr->hasName() ? BasePtr->getName().str() : "anon";
            if (VarName == "arrayidx" || VarName == "arrayidx2") {
                // 尝试从 GEP 的操作数中找出更好的名字
                if (auto *InnerGEP = dyn_cast<GetElementPtrInst>(BasePtr)) {
                    Value *InnerBase = InnerGEP->getPointerOperand();
                    if (InnerBase->hasName() && InnerBase->getName() != "arrayidx") {
                        VarName = InnerBase->getName().str();
                    }
                }
            }
            uint32_t ArrayID = generateArrayID(F.getName().str(), VarName);

            // 提取索引值（GEP 的最后一个操作数为索引）
            Value *RawIdx = GEP->getOperand(GEP->getNumOperands() - 1);
            // 如果索引不是整数类型，跳过
            if (!RawIdx->getType()->isIntegerTy()) continue;

            Value *Idx64 = Builder.CreateZExtOrTrunc(RawIdx, Type::getInt64Ty(Ctx));

            // 在GEP之后插入调用
            Instruction *NextInst = GEP->getNextNode();
            if (!NextInst)
                Builder.SetInsertPoint(GEP->getParent(), GEP->getParent()->end());
            else
                Builder.SetInsertPoint(NextInst);

            Builder.CreateCall(ReportFn, {
                Builder.getInt32(ArrayID),
                Idx64,
                Builder.getInt32(AccessType),
                Builder.getInt32(LineNum)
            });

            errs() << "  [Instrumented] " << VarName
                   << " (ID=" << format_hex(ArrayID, 10)
                   << ", idx_op=" << *RawIdx
                   << ", line=" << LineNum
                   << ", access=" << AccessType << ")\n";
        }

        return PreservedAnalyses::none();
    }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "MyArrayPass", "v0.3",
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM, auto) {
                    if (Name == "my-array-pass") {
                        FunctionPassManager FPM;
                        FPM.addPass(MyArrayPass());
                        MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
                        return true;
                    }
                    return false;
                });
        }};
}
