#include <stack>
#include <fstream>
#include <iostream>
#include <string.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/PassManager.h>
#include <llvm/IR/CallingConv.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/LinkAllPasses.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/Support/TargetSelect.h>
using namespace std;
using namespace llvm;
 
struct MyLoopInfo
{
   Value* beforeValue;
   PHINode* startValue;
   Value* endValue;
   Value* afterValue;
 
   BasicBlock* beforeBlock;
   BasicBlock* startBlock;
   BasicBlock* endBlock;
   BasicBlock* afterBlock;
};

bool is_token(const char *str, const char *substr) {
        return strncmp(str,substr,strlen(substr));
}

const char* tokens[8] = {"yo?","yo!","yo","Yo!","Yo?","YO!","YO?","YO"};

const int next_token(const char** s) {
        for(int i=0;i<8;++i) {
                if(is_token(*s,tokens[i])==0) {
                        *s=*s+strlen(tokens[i]);
                        return i;
                }
        }
        return -1;
}

Function* makeFunc(Module* module, const char* source, int tapeSize = 300)
{
   // Some useful types and constants
   Type* voidType = Type::getVoidTy(getGlobalContext());
   IntegerType* cellType = IntegerType::get(getGlobalContext(), 8);
   IntegerType* indexType = IntegerType::get(getGlobalContext(), 32);
   PointerType* tapeType = PointerType::get(cellType, 0);

   Value* zero = ConstantInt::get(cellType, 0);
   Value* one = ConstantInt::get(cellType, 1);
   Value* minOne = ConstantInt::get(cellType, -1);
 
   //declare i32 @getchar()
   Function* getchar = cast<Function>(module->getOrInsertFunction("getchar", cellType, NULL));
   getchar->setCallingConv(CallingConv::C);
 
   FunctionType::get(voidType,ArrayRef<Type*>(cellType),false);

   //declare i32 @putchar(i32)
   Function* putchar = cast<Function>(module->
   getOrInsertFunction("putchar", voidType, cellType, NULL));
   putchar->setCallingConv(CallingConv::C);
 
   // Contruct void main(char* tape)
   Function* main = cast<Function>(module->getOrInsertFunction("main", voidType, NULL));
   main->setCallingConv(CallingConv::C);
   BasicBlock* block = BasicBlock::Create(getGlobalContext(), "code", main);
   stack<MyLoopInfo> loops;
   IRBuilder<> codeIR(block);
   Value* head = codeIR.CreateAlloca(cellType, ConstantInt::get(indexType, tapeSize));
   Value* it = head;
   for(int i=0; i < tapeSize; i++)
   {
      codeIR.CreateStore(zero, it);
      it = codeIR.CreateGEP(it, one);
   }
   while(*source)
   {
      IRBuilder<> builder(block);
      int token = next_token(&source);
      if(token<0) {
         ++source;
         continue;
      }
      switch(token)
      {
         case 2: head = builder.CreateGEP(head, one); break;
         case 7: head = builder.CreateGEP(head, minOne); break;
         case 3:
         {
            Value* headValue = builder.CreateLoad(head);
            Value* result = builder.CreateAdd(headValue, one);
            builder.CreateStore(result, head);
            break;
         }
         case 4:
         {
            Value* headValue = builder.CreateLoad(head);
            Value* result = builder.CreateSub(headValue, one);
            builder.CreateStore(result, head);
            break;
         }
         case 5:
         {
            Value* output = builder.CreateLoad(head);
            builder.CreateCall(putchar, output);
            break;
         }
         case 0:
         {
            Value* input = builder.CreateCall(getchar);
            builder.CreateStore(input, head);
            break;
         }
         case 1:
         {
            // Construct loop info
            MyLoopInfo loop;
            loop.beforeBlock = block;
            loop.startBlock = BasicBlock::Create(getGlobalContext(), "", main);
            loop.afterBlock = BasicBlock::Create(getGlobalContext(), "", main);
            loop.beforeValue = head;
 
            // Create branching instructions
            Value* headValue = builder.CreateLoad(head);
            Value* condition = builder.CreateIsNotNull(headValue);
            builder.CreateCondBr(condition, loop.startBlock, loop.afterBlock);
 
            // Create a phi node
            IRBuilder<> sbuilder(loop.startBlock,0);
            loop.startValue = sbuilder.CreatePHI(tapeType,0);
            loop.startValue->addIncoming(loop.beforeValue, loop.beforeBlock);
 
            // Push the loop
            loops.push(loop);
            block = loop.startBlock;
            head = loop.startValue;
            break;
         }
         case 6:
         {
            // Retrieve the loop info
            MyLoopInfo loop = loops.top(); loops.pop();
            loop.endValue = head;
            loop.endBlock = block;
 
            // Create a conditional branch
            Value* headValue = builder.CreateLoad(head);
            Value* condition = builder.CreateIsNotNull(headValue);
            builder.CreateCondBr(condition, loop.startBlock, loop.afterBlock);
 
            // Augement loops phi node
            loop.startValue->addIncoming(loop.endValue, loop.endBlock);
 
            // Switch to the after block
            block = loop.afterBlock;
 
            // Create a phi node
            IRBuilder<> abuilder(block);
            PHINode* headPhi = abuilder.CreatePHI(tapeType,0);
            headPhi->addIncoming(loop.beforeValue, loop.beforeBlock);
            headPhi->addIncoming(loop.endValue, loop.endBlock);
            head = headPhi;
            break;
         }
         default:
            break;
      }
   }
 
   // Close the function
   {
      IRBuilder<> builder(block);
      builder.CreateRetVoid();
   }
   return main;
}
 
int main(int argc, char* argv[])
{
   if(argc < 2)
   {
      cerr << "Usage: " << argv[0] << " yo_file" << endl;
      return -1;
   }
   ifstream sourceFile(argv[1]);
   string line, source;
   while(getline(sourceFile, line)) source += line;
 
   // Setup a module and engine for JIT-ing
   std::string error;
   InitializeNativeTarget();
   Module* module = new Module("yocode", getGlobalContext());
   ExecutionEngine *engine = EngineBuilder(module)
      .setErrorStr(&error)
      .setOptLevel(CodeGenOpt::Aggressive)
      .create();
   if(!engine)
   {
      cout << "No engine created: " << error << endl;
      return -1;
   }
 
   // Compile the YO to IR
   Function* func = makeFunc(module, source.c_str());
 
   // Run optimization passes
   FunctionPassManager pm(module);
   pm.add(new DataLayout(*(engine->getDataLayout())));
   pm.add(createVerifierPass());
 
   // Eliminate simple loops such as [>>++<<-]
   pm.add(createInstructionCombiningPass()); // Cleanup for scalarrepl.
   pm.add(createLICMPass());                 // Hoist loop invariants
   pm.add(createIndVarSimplifyPass());       // Canonicalize indvars
   pm.add(createLoopDeletionPass());         // Delete dead loops
 
   // Simplify code
   for(int repeat=0; repeat < 3; repeat++)
   {
      pm.add(createGVNPass());                  // Remove redundancies
      pm.add(createSCCPPass());                 // Constant prop with SCCP
      pm.add(createCFGSimplificationPass());    // Merge & remove BBs
      pm.add(createInstructionCombiningPass());
      pm.add(createJumpThreadingPass());        // Thread jumps
      pm.add(createAggressiveDCEPass());        // Delete dead instructions
      pm.add(createCFGSimplificationPass());    // Merge & remove BBs
      pm.add(createDeadStoreEliminationPass()); // Delete dead stores
   }
 
   // Process
   pm.run(*func);
 
   // Compile ...
   void (*yo)() = (void (*)())engine->getPointerToFunction(func);
 
   // ... and run!
   yo();
 
   return 0;
}
