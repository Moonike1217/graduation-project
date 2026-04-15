#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DebugInfoMetadata.h"

using namespace llvm;

namespace {

struct MyArrayPass : public PassInfoMixin<MyArrayPass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        // 过滤：跳过声明、空函数以及我们自己的报告函数
        if (F.isDeclaration() || F.getName().starts_with("__afl_report_array")) {
            return PreservedAnalyses::all();
        }

        LLVMContext &Ctx = F.getContext();
        Module *M = F.getParent();
        
        // --- 第一部分：静态扫描 (Static Analysis) ---
        // 扫描函数内所有的 GEP 指令并提取属性
        SmallVector<GetElementPtrInst *, 32> TargetGEPs;
        
        errs() << "\n>>>>> [Static Report] Analyzing Function: " << F.getName() << " <<<<<\n";

        for (auto &BB : F) {
            for (auto &Inst : BB) {
                if (auto *GEP = dyn_cast<GetElementPtrInst>(&Inst)) {
                    TargetGEPs.push_back(GEP);

                    // 1. 获取名称
                    Value *BasePtr = GEP->getPointerOperand();
                    std::string ArrayName = BasePtr->hasName() ? BasePtr->getName().str() : "tmp_ptr";

                    // 2. 获取类型与长度
                    Type *SourceTy = GEP->getSourceElementType();
                    std::string TypeStr;
                    raw_string_ostream RSO(TypeStr);
                    SourceTy->print(RSO);

                    uint64_t ArrayLen = 0;
                    if (SourceTy->isArrayTy()) {
                        ArrayLen = SourceTy->getArrayNumElements();
                    }

                    // 3. 获取作用域
                    std::string Scope = "Local (Stack)";
                    if (isa<GlobalVariable>(BasePtr)) Scope = "Global";
                    else if (isa<Argument>(BasePtr)) Scope = "Parameter";

                    // 输出规整的静态报告
                    errs() << "[Array Found] Name: " << ArrayName 
                           << " | Type: " << TypeStr 
                           << " | Len: " << ArrayLen 
                           << " | Scope: " << Scope << "\n";
                }
            }
        }

        if (TargetGEPs.empty()) return PreservedAnalyses::all();

        // --- 第二部分：动态插桩 (Dynamic Instrumentation) ---
        // 定义报告函数: void __afl_report_array(uint32_t id, int64_t index, int32_t type, int32_t line)
        FunctionCallee ReportFn = M->getOrInsertFunction(
            "__afl_report_array", 
            Type::getVoidTy(Ctx), 
            Type::getInt32Ty(Ctx), // ArrayID
            Type::getInt64Ty(Ctx), // Index
            Type::getInt32Ty(Ctx), // AccessType (1:Read, 2:Write)
            Type::getInt32Ty(Ctx)  // LineNumber
        );

        IRBuilder<> Builder(Ctx);

        for (auto *GEP : TargetGEPs) {
            // 1. 确定访问类型 (读/写)
            int32_t AccessType = 0; // 0:Unknown, 1:Read, 2:Write
            for (User *U : GEP->users()) {
                if (isa<LoadInst>(U)) AccessType = 1;
                else if (auto *SI = dyn_cast<StoreInst>(U)) {
                    if (SI->getPointerOperand() == GEP) AccessType = 2;
                }
            }

            // 2. 获取行号 (上下文信息)
            uint32_t LineNum = 0;
            if (DILocation *Loc = GEP->getDebugLoc()) {
                LineNum = Loc->getLine();
            }

            // 3. 准备插桩数据
            Builder.SetInsertPoint(GEP);
            // 简单的 ArrayID 生成：使用指针地址的低32位
            uint32_t ArrayID = (uint32_t)((uintptr_t)GEP->getPointerOperand() & 0xFFFFFFFF);
            
            // 获取索引值（取最后一个维度）
            Value *RawIdx = GEP->getOperand(GEP->getNumOperands() - 1);
            Value *Idx64 = Builder.CreateZExtOrTrunc(RawIdx, Type::getInt64Ty(Ctx));

            // 4. 插入调用 (在 GEP 之后)
            Builder.SetInsertPoint(GEP->getNextNode());
            Builder.CreateCall(ReportFn, {
                Builder.getInt32(ArrayID),
                Idx64,
                Builder.getInt32(AccessType),
                Builder.getInt32(LineNum)
            });
        }

        return PreservedAnalyses::none();
    }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "MyArrayPass", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM, ...) {
                    if (Name == "my-array-pass") {
                        FPM.addPass(MyArrayPass());
                        return true;
                    }
                    return false;
                });
        }
    };
}