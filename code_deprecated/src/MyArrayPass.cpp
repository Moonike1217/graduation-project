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
                    Type *SourceTy = GEP->getSourceElementType();
                    // 跳过非数组类型的 GEP（如指针运算 i8* + offset），只处理数组访问
                    if (!SourceTy->isArrayTy()) continue;

                    TargetGEPs.push_back(GEP);

                    Value *BasePtr = GEP->getPointerOperand();
                    std::string ArrayName = BasePtr->hasName() ? BasePtr->getName().str() : "tmp_ptr";

                    std::string TypeStr;
                    raw_string_ostream RSO(TypeStr);
                    SourceTy->print(RSO);

                    uint64_t ArrayLen = SourceTy->getArrayNumElements();

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

            // === 多维数组维度感知的 ArrayID 生成 ===
            // 对于 matrix[r][c]，LLVM IR 生成嵌套 GEP：
            //   GEP1: getelementptr [3 x [4 x i8]], ptr %matrix, 0, %r  (row)
            //   GEP2: getelementptr [4 x i8], ptr %arrayidx, 0, %c      (element)
            // 回溯 GEP 链找到根变量名，并按深度追加 "#dimN" 后缀
            Value *BasePtr = GEP->getPointerOperand();

            std::string VarName;
            int dimDepth = 0;
            Value *RootPtr = BasePtr;
            while (auto *InnerGEP = dyn_cast<GetElementPtrInst>(RootPtr)) {
                RootPtr = InnerGEP->getPointerOperand();
                dimDepth++;
            }

            if (RootPtr->hasName()) {
                VarName = RootPtr->getName().str();
            } else if (BasePtr->hasName()) {
                VarName = BasePtr->getName().str();
            } else {
                // 尝试从 debug info 获取变量名（处理指针运算产生的临时值）
                VarName = "anon";
                if (DILocation *Loc = GEP->getDebugLoc()) {
                    auto *Scope = Loc->getScope();
                    if (Scope) {
                        // 从作用域中查找局部变量的 dbg.declare 信息
                        for (auto &BB : F) {
                            for (auto &Inst : BB) {
                                if (auto *DbgDecl = dyn_cast<DbgDeclareInst>(&Inst)) {
                                    if (DbgDecl->getAddress() == RootPtr ||
                                        DbgDecl->getAddress() == BasePtr) {
                                        if (auto *Var = DbgDecl->getVariable()) {
                                            VarName = Var->getName().str();
                                            break;
                                        }
                                    }
                                }
                            }
                            if (VarName != "anon") break;
                        }
                    }
                }
            }

            // 内层维度追加 "#dimN" 后缀，生成不同的 ArrayID
            if (dimDepth > 0) {
                VarName += "#dim" + std::to_string(dimDepth);
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
