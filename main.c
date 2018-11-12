// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.
//
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to <http://unlicense.org/>

#include <llvm-c/BitReader.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>

#include <stdio.h>

#if defined(_MSC_VER)
#include <io.h>
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#else
#include <stdlib.h>
#endif

long long getValue(LLVMValueRef val) {
  if (LLVMIsAConstantInt(val)) {
    return LLVMConstIntGetSExtValue(val);
  } else if (LLVMIsAAllocaInst(val)) {
    printf("# alloca %d\n", LLVMGetNumOperands(val));
    LLVMValueRef x = LLVMGetOperand(val, 0);
    // LLVMValueKind kind = LLVMGetValueKind(x);
    LLVMTypeRef type = LLVMTypeOf(x);
    puts(LLVMPrintTypeToString(type));
    printf("\n#width: %u", LLVMGetIntTypeWidth(type));
    return LLVMGetIntTypeWidth(type) / 8;
  } else {
    printf("# Not a constant!\n");
    return 8;
  }
}

const char* getValueAsString(LLVMValueRef val) {
  val=0;
  return ".string \"\"";
}

const char* getName(LLVMValueRef val) {
  size_t size;
  return LLVMGetValueName2(val, &size);
}

int main(const int argc, const char *const argv[]) {
  if (2 != argc) {
    fprintf(stderr, "Invalid command line!\n");
    return 1;
  }

  const char *const inputFilename = argv[1];

  LLVMMemoryBufferRef memoryBuffer;

  // check if we are to read our input file from stdin
  if (('-' == inputFilename[0]) && ('\0' == inputFilename[1])) {
    char *message;
    if (0 != LLVMCreateMemoryBufferWithSTDIN(&memoryBuffer, &message)) {
      fprintf(stderr, "%s\n", message);
      free(message);
      return 1;
    }
  } else {
    char *message;
    if (0 != LLVMCreateMemoryBufferWithContentsOfFile(
                 inputFilename, &memoryBuffer, &message)) {
      fprintf(stderr, "%s\n", message);
      free(message);
      return 1;
    }
  }

  // now create our module using the memorybuffer
  LLVMModuleRef module;
  if (0 != LLVMParseBitcode2(memoryBuffer, &module)) {
    fprintf(stderr, "Invalid bitcode detected!\n");
    LLVMDisposeMemoryBuffer(memoryBuffer);
    return 1;
  }

  // done with the memory buffer now, so dispose of it
  LLVMDisposeMemoryBuffer(memoryBuffer);

  size_t length = 0;
  const char* sourceFile = LLVMGetSourceFileName(module, &length);
  if (*sourceFile) printf("# Source File: %s\n", sourceFile);
  printf(".global _main\n"
         ".equ _main, main\n");

  // loop through all the functions in the module
  for (LLVMValueRef function = LLVMGetFirstFunction(module); function;
       function = LLVMGetNextFunction(function)) {
    if (!LLVMIsDeclaration(function)) {
      printf("%s:\t# Function\n"
             "\tpushq\t%%rbp     \t# Save Old Base Pointer\n"
             "\tmovq\t%%rsp, %%rbp\t# Save Old Stack Pointer\n", getName(function));
    } else {
      const char* func = getName(function);
      printf("# External function declaration: %s\n"
             ".equ %s, _%s\n", func, func, func);
    }
    // loop through all the basic blocks in the function
    for (LLVMBasicBlockRef basicBlock = LLVMGetFirstBasicBlock(function);
         basicBlock; basicBlock = LLVMGetNextBasicBlock(basicBlock)) {
      const char* basicBlockName = LLVMGetBasicBlockName(basicBlock);
      if (*basicBlockName) printf("%s:\t# Basic Block\n", basicBlockName);

      // loop through all the instructions in the basic block
      for (
           LLVMValueRef instruction = LLVMGetFirstInstruction(basicBlock);
           instruction;
           instruction = LLVMGetNextInstruction(instruction)
      ) {

        const char* llvm = LLVMPrintValueToString(instruction) + 2;
        // look for math instructions
        if (LLVMIsAAllocaInst(instruction)) {
          LLVMValueRef x = LLVMGetOperand(instruction, 0);
          printf("\tsubq\t$%llu, %%rsp\t# LLVM: %s\n", getValue(x), llvm);
        } else if (LLVMIsAReturnInst(instruction)) {
          LLVMValueRef x = LLVMGetOperand(instruction, 0);
          printf("\tmovl\t$%llu, %%eax\t# LLVM: %s\n"
                 "\tmovq\t%%rbp, %%rsp\t# Restore Old Stack Pointer\n"
                 "\tpopq\t%%rbp     \t# Restore Old Base Pointer\n"
                 "\tretq            \t# Return from function\n", getValue(x), llvm);
        } else if (LLVMIsAStoreInst(instruction)) {
          LLVMValueRef x = LLVMGetOperand(instruction, 0);
          LLVMValueRef y = LLVMGetOperand(instruction, 1);
          printf("\tmovq\t$%llu, %llu        \t# LLVM: %s\n", getValue(x), getValue(y), llvm);
        } else if (LLVMIsACallInst(instruction)) {
          LLVMValueRef y = LLVMGetOperand(instruction, 1);
          printf("\tcallq\t%-8s\t# LLVM: %s\n", getName(y), llvm);
        } else {
          printf("\t# UNKNOWN INSTRUCTION\t# LLVM:\t%s\n", llvm);
        }
      }
    }
    puts("");
  }
  for (LLVMValueRef global = LLVMGetFirstGlobal(module); global;
       global = LLVMGetNextGlobal(global)) {
    printf("%s: %s\n", getName(global), getValueAsString(global));
  }

  LLVMDisposeModule(module);

  return 0;
}
