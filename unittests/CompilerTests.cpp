#include "Toolchain/Toolchain.h"
#include "ModuleLoader.h"
#include "TestModuleFactory.h"

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"

#include "gtest/gtest.h"

using namespace llvm;
using namespace Mutang;

static TestModuleFactory TestModuleFactory;

TEST(Compiler, CompileModule) {
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  std::unique_ptr<TargetMachine> targetMachine(
                                  EngineBuilder().selectTarget(Triple(), "", "",
                                  SmallVector<std::string, 1>()));

  Compiler compiler(*targetMachine.get());

  auto someModule = TestModuleFactory.createTesterModule();
  MutangModule mutangModule(std::move(someModule), "");
  auto Binary = compiler.compileModule(mutangModule);

  ASSERT_NE(nullptr, Binary.getBinary());
}
