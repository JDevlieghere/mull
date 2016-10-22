
#include "Context.h"
#include "SimpleTest/SimpleTestFinder.h"
#include "MutationEngine.h"
#include "MutationOperators/AddMutationOperator.h"
#include "TestModuleFactory.h"

#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "gtest/gtest.h"

using namespace Mutang;
using namespace llvm;

static TestModuleFactory TestModuleFactory;

Instruction *getFirstNamedInstruction(Function &F, const StringRef &Name) {
  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    Instruction &Instr = *I;

    if (Instr.getName().equals(Name)) {
      return &*I;
    }
  }

  return nullptr;
}

TEST(MutationEngine, applyMutation) {
  auto ModuleWithTests   = TestModuleFactory.createTesterModule();
  auto ModuleWithTestees = TestModuleFactory.createTesteeModule();

  Context Ctx;
  Ctx.addModule(std::move(ModuleWithTests));
  Ctx.addModule(std::move(ModuleWithTestees));

  SimpleTestFinder Finder;
  auto Tests = Finder.findTests(Ctx);

  auto &Test = *(Tests.begin());

  ArrayRef<Testee> Testees = Finder.findTestees(Test.get(), Ctx);

  ASSERT_EQ(1U, Testees.size());

  Function *Testee = Testees.begin()->first;
  ASSERT_FALSE(Testee->empty());

  AddMutationOperator MutOp;
  std::vector<MutationOperator *> MutOps({&MutOp});

  std::vector<std::unique_ptr<MutationPoint>> MutationPoints = Finder.findMutationPoints(MutOps, *Testee);
  ASSERT_EQ(1U, MutationPoints.size());

  MutationPoint *MP = (*(MutationPoints.begin())).get();
  ASSERT_EQ(&MutOp, MP->getOperator());
  ASSERT_TRUE(isa<BinaryOperator>(MP->getOriginalValue()));

  std::string ReplacedInstructionName = MP->getOriginalValue()->getName().str();

  MutationEngine Engine;

  Engine.applyMutation(Testee->getParent(), *MP);

  // After mutation applied on instruction it should be erased
  Instruction *OldInstruction = cast<BinaryOperator>(MP->getOriginalValue());
  ASSERT_EQ(nullptr, OldInstruction->getParent());

//  Function *MutatedFunction = Testee;//ModuleWithTestees->getFunction(Testee->getName());

  // After mutation we should have new instruction with the same name as an original instruction
  Instruction *NewInstruction = getFirstNamedInstruction(*Testee, ReplacedInstructionName);
  ASSERT_TRUE(isa<BinaryOperator>(NewInstruction));
  ASSERT_EQ(Instruction::Sub, NewInstruction->getOpcode());
}

TEST(MutationEngine, applyAndRevertMutation) {
  auto ModuleWithTests   = TestModuleFactory.createTesterModule();
  auto ModuleWithTestees = TestModuleFactory.createTesteeModule();

  Context Ctx;
  Ctx.addModule(std::move(ModuleWithTests));
  Ctx.addModule(std::move(ModuleWithTestees));

  SimpleTestFinder Finder;
  auto Tests = Finder.findTests(Ctx);

  auto &Test = *(Tests.begin());

  ArrayRef<Testee> Testees = Finder.findTestees(Test.get(), Ctx);

  ASSERT_EQ(1U, Testees.size());

  Function *Testee = Testees.begin()->first;
  ASSERT_FALSE(Testee->empty());

  AddMutationOperator MutOp;
  std::vector<MutationOperator *> MutOps({&MutOp});

  std::vector<std::unique_ptr<MutationPoint>> MutationPoints = Finder.findMutationPoints(MutOps, *Testee);
  ASSERT_EQ(1U, MutationPoints.size());

  MutationPoint *MP = (*(MutationPoints.begin())).get();
  ASSERT_EQ(&MutOp, MP->getOperator());
  ASSERT_TRUE(isa<BinaryOperator>(MP->getOriginalValue()));

  Instruction *ReplacedInstruction = cast<BinaryOperator>(MP->getOriginalValue());

  std::string ReplacedInstructionName = ReplacedInstruction->getName().str();

  MutationEngine Engine;

  Engine.applyMutation(Testee->getParent(), *MP);

  // After mutation applied on instruction it should be erased
  Instruction *OldInstruction = cast<BinaryOperator>(MP->getOriginalValue());
  ASSERT_EQ(nullptr, OldInstruction->getParent());

  // After mutation we should have new instruction with the same name as an original instruction
  Instruction *NewInstruction = getFirstNamedInstruction(*Testee, ReplacedInstructionName);
  ASSERT_TRUE(isa<BinaryOperator>(NewInstruction));
  ASSERT_EQ(Instruction::Sub, NewInstruction->getOpcode());
}
