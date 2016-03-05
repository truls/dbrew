
#ifndef BREW_PRINTER
#define BREW_PRINTER

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <brew-common.h>

const char* regName(Reg r, OpType t);
char* op2string(Operand* o, ValType t);
const char* instrName(InstrType it, int* pOpCount);
char* instr2string(Instr* instr, int align);
char* bytes2string(Instr* instr, int start, int count);
void brew_print_decoded(DBB* bb);
void printDecodedBBs(Rewriter* c);

#endif
