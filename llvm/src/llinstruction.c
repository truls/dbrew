/**
 * This file is part of DBrew, the dynamic binary rewriting library.
 *
 * (c) 2015-2016, Josef Weidendorfer <josef.weidendorfer@gmx.de>
 *
 * DBrew is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License (LGPL)
 * as published by the Free Software Foundation, either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * DBrew is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with DBrew.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 **/

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <llvm-c/Core.h>

#include <instr.h>
#include <printer.h>

#include <llinstruction-internal.h>

#include <llbasicblock-internal.h>
#include <llcommon.h>
#include <llcommon-internal.h>
#include <llflags-internal.h>
#include <llfunction.h>
#include <llfunction-internal.h>
#include <lloperand-internal.h>
#include <llsupport-internal.h>

/**
 * \defgroup LLInstruction Instruction
 * \brief Handling of X86-64 instructions
 *
 * @{
 **/

enum ConversionType {
    CT_NOT_IMPLEMENTED = 0,
    CT_NOP,
    CT_FUNCTION,
    CT_BINARY_LLVM,
};

typedef enum ConversionType ConversionType;

struct ConversionDescriptor {
    ConversionType type;
    union {
        void (*function)(Instr* instr, LLState* state);
        struct {
            OperandDataType dataType;
            PartialRegisterHandling prh;
            LLVMValueRef (*buildFunc)(LLVMBuilderRef, LLVMValueRef, LLVMValueRef, const char*);
            void (*flagFunc)(LLState*, LLVMValueRef, LLVMValueRef, LLVMValueRef);
            bool fastMath;
        } binary_llvm;
    } u;
};

typedef struct ConversionDescriptor ConversionDescriptor;

#define CD_NOP() { CT_NOP, { 0 } }
#define CD_FUNCTION(fn) { CT_FUNCTION, { .function = fn } }
#define CD_BINARY_FP_LLVM(type,prh,func,fm) { CT_BINARY_LLVM, { .binary_llvm = { type, prh, func, .fastMath = fm } } }
#define CD_BINARY_INT_LLVM(func,flagFn) { CT_BINARY_LLVM, { .binary_llvm = { OP_SI, REG_DEFAULT, func, .flagFunc = flagFn } } }

static const ConversionDescriptor descriptors[IT_Max] = {
    [IT_HINT_CALL] = CD_NOP(),
    [IT_HINT_RET] = CD_NOP(),
    [IT_HINT_CALLRET] = CD_NOP(),

    [IT_NOP] = CD_NOP(),

    // Defined in llinstruction-callret.c
    [IT_CALL] = CD_FUNCTION(ll_instruction_call),
    [IT_RET] = CD_FUNCTION(ll_instruction_ret),

    // Defined in llinstruction-stack.c
    [IT_PUSH] = CD_FUNCTION(ll_instruction_stack),
    [IT_POP] = CD_FUNCTION(ll_instruction_stack),
    [IT_LEAVE] = CD_FUNCTION(ll_instruction_stack),

    // Defined in llinstruction-gp.c
    [IT_MOV] = CD_FUNCTION(ll_instruction_movgp),
    [IT_MOVZX] = CD_FUNCTION(ll_instruction_movgp),
    [IT_MOVSX] = CD_FUNCTION(ll_instruction_movgp),
    [IT_ADD] = CD_FUNCTION(ll_instruction_add),
    [IT_SUB] = CD_FUNCTION(ll_instruction_sub),
    [IT_CMP] = CD_FUNCTION(ll_instruction_cmp),
    [IT_LEA] = CD_FUNCTION(ll_instruction_lea),
    [IT_NOT] = CD_FUNCTION(ll_instruction_notneg),
    [IT_NEG] = CD_FUNCTION(ll_instruction_notneg),
    [IT_INC] = CD_FUNCTION(ll_instruction_incdec),
    [IT_DEC] = CD_FUNCTION(ll_instruction_incdec),
    [IT_AND] = CD_BINARY_INT_LLVM(LLVMBuildAnd, ll_flags_set_bit),
    [IT_OR] = CD_BINARY_INT_LLVM(LLVMBuildOr, ll_flags_set_bit),
    [IT_XOR] = CD_BINARY_INT_LLVM(LLVMBuildXor, ll_flags_set_bit),
    [IT_TEST] = CD_FUNCTION(ll_instruction_test),
    [IT_IMUL] = CD_FUNCTION(ll_instruction_imul),
    [IT_SHL] = CD_BINARY_INT_LLVM(LLVMBuildShl, ll_flags_set_shl),
    [IT_SHR] = CD_BINARY_INT_LLVM(LLVMBuildLShr, ll_flags_set_shr),
    [IT_SAR] = CD_BINARY_INT_LLVM(LLVMBuildAShr, ll_flags_set_sar),
    [IT_CLTQ] = CD_FUNCTION(ll_instruction_cdqe),

    [IT_CMOVO] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVNO] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVC] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVNC] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVZ] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVNZ] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVBE] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVA] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVS] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVNS] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVP] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVNP] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVL] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVGE] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVLE] = CD_FUNCTION(ll_instruction_cmov),
    [IT_CMOVG] = CD_FUNCTION(ll_instruction_cmov),

    [IT_SETO] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETNO] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETB] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETAE] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETE] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETNE] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETBE] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETA] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETS] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETNS] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETP] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETNP] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETL] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETGE] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETLE] = CD_FUNCTION(ll_instruction_setcc),
    [IT_SETG] = CD_FUNCTION(ll_instruction_setcc),

    // Defined in llinstruction-sse.c
    [IT_MOVD] = CD_FUNCTION(ll_instruction_movq),
    [IT_MOVQ] = CD_FUNCTION(ll_instruction_movq),
    [IT_MOVSS] = CD_FUNCTION(ll_instruction_movs),
    [IT_MOVSD] = CD_FUNCTION(ll_instruction_movs),
    [IT_MOVUPS] = CD_FUNCTION(ll_instruction_movp),
    [IT_MOVUPD] = CD_FUNCTION(ll_instruction_movp),
    [IT_MOVAPS] = CD_FUNCTION(ll_instruction_movp),
    [IT_MOVAPD] = CD_FUNCTION(ll_instruction_movp),
    [IT_MOVDQU] = CD_FUNCTION(ll_instruction_movdq),
    [IT_MOVDQA] = CD_FUNCTION(ll_instruction_movdq),
    [IT_MOVLPS] = CD_FUNCTION(ll_instruction_movlp),
    [IT_MOVLPD] = CD_FUNCTION(ll_instruction_movlp),
    [IT_MOVHPS] = CD_FUNCTION(ll_instruction_movhps),
    [IT_MOVHPD] = CD_FUNCTION(ll_instruction_movhpd),
    [IT_UNPCKLPS] = CD_FUNCTION(ll_instruction_unpckl),
    [IT_UNPCKLPD] = CD_FUNCTION(ll_instruction_unpckl),
    [IT_ADDSS] = CD_BINARY_FP_LLVM(OP_SF32, REG_KEEP_UPPER, LLVMBuildFAdd, true),
    [IT_ADDSD] = CD_BINARY_FP_LLVM(OP_SF64, REG_KEEP_UPPER, LLVMBuildFAdd, true),
    [IT_ADDPS] = CD_BINARY_FP_LLVM(OP_VF32, REG_KEEP_UPPER, LLVMBuildFAdd, true),
    [IT_ADDPD] = CD_BINARY_FP_LLVM(OP_VF64, REG_KEEP_UPPER, LLVMBuildFAdd, true),
    [IT_SUBSS] = CD_BINARY_FP_LLVM(OP_SF32, REG_KEEP_UPPER, LLVMBuildFSub, true),
    [IT_SUBSD] = CD_BINARY_FP_LLVM(OP_SF64, REG_KEEP_UPPER, LLVMBuildFSub, true),
    [IT_SUBPS] = CD_BINARY_FP_LLVM(OP_VF32, REG_KEEP_UPPER, LLVMBuildFSub, true),
    [IT_SUBPD] = CD_BINARY_FP_LLVM(OP_VF64, REG_KEEP_UPPER, LLVMBuildFSub, true),
    [IT_MULSS] = CD_BINARY_FP_LLVM(OP_SF32, REG_KEEP_UPPER, LLVMBuildFMul, true),
    [IT_MULSD] = CD_BINARY_FP_LLVM(OP_SF64, REG_KEEP_UPPER, LLVMBuildFMul, true),
    [IT_MULPS] = CD_BINARY_FP_LLVM(OP_VF32, REG_KEEP_UPPER, LLVMBuildFMul, true),
    [IT_MULPD] = CD_BINARY_FP_LLVM(OP_VF64, REG_KEEP_UPPER, LLVMBuildFMul, true),
    [IT_DIVSS] = CD_BINARY_FP_LLVM(OP_SF32, REG_KEEP_UPPER, LLVMBuildFDiv, true),
    [IT_DIVSD] = CD_BINARY_FP_LLVM(OP_SF64, REG_KEEP_UPPER, LLVMBuildFDiv, true),
    [IT_DIVPS] = CD_BINARY_FP_LLVM(OP_VF32, REG_KEEP_UPPER, LLVMBuildFDiv, true),
    [IT_DIVPD] = CD_BINARY_FP_LLVM(OP_VF64, REG_KEEP_UPPER, LLVMBuildFDiv, true),
    [IT_ORPS] = CD_BINARY_FP_LLVM(OP_VI32, REG_KEEP_UPPER, LLVMBuildOr, false),
    [IT_ORPD] = CD_BINARY_FP_LLVM(OP_VI64, REG_KEEP_UPPER, LLVMBuildOr, false),
    [IT_ANDPS] = CD_BINARY_FP_LLVM(OP_VI32, REG_KEEP_UPPER, LLVMBuildAnd, false),
    [IT_ANDPD] = CD_BINARY_FP_LLVM(OP_VI64, REG_KEEP_UPPER, LLVMBuildAnd, false),
    [IT_XORPS] = CD_BINARY_FP_LLVM(OP_VI32, REG_KEEP_UPPER, LLVMBuildXor, false),
    [IT_XORPD] = CD_BINARY_FP_LLVM(OP_VI64, REG_KEEP_UPPER, LLVMBuildXor, false),

    [IT_PXOR] = CD_BINARY_FP_LLVM(OP_VI64, REG_KEEP_UPPER, LLVMBuildXor, false),

    // Jumps are handled in the basic block generation code.
    [IT_JMP] = CD_NOP(),
    [IT_JO] = CD_NOP(),
    [IT_JNO] = CD_NOP(),
    [IT_JC] = CD_NOP(),
    [IT_JNC] = CD_NOP(),
    [IT_JZ] = CD_NOP(),
    [IT_JNZ] = CD_NOP(),
    [IT_JBE] = CD_NOP(),
    [IT_JA] = CD_NOP(),
    [IT_JS] = CD_NOP(),
    [IT_JNS] = CD_NOP(),
    [IT_JP] = CD_NOP(),
    [IT_JNP] = CD_NOP(),
    [IT_JL] = CD_NOP(),
    [IT_JGE] = CD_NOP(),
    [IT_JLE] = CD_NOP(),
    [IT_JG] = CD_NOP(),
};

/**
 * Handling of an instruction.
 *
 * \todo Support other return types than i64, float and double
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param instr The push instruction
 * \param state The module state
 **/
void
ll_generate_instruction(Instr* instr, LLState* state)
{
    // Set new instruction pointer register
    uintptr_t rip = instr->addr + instr->len;
    LLVMValueRef ripValue = LLVMConstInt(LLVMInt64TypeInContext(state->context), rip, false);
    ll_set_register(getReg(RT_IP, 0), FACET_I64, ripValue, true, state);

    // Add Metadata for debugging.
    LLVMValueRef intrinsicDoNothing = ll_support_get_intrinsic(state->module, LL_INTRINSIC_DO_NOTHING, NULL, 0);
    char* instructionName = instr2string(instr, 0, NULL, NULL);
    LLVMValueRef mdCall = LLVMBuildCall(state->builder, intrinsicDoNothing, NULL, 0, "");
    LLVMValueRef mdNode = LLVMMDStringInContext(state->context, instructionName, strlen(instructionName));
    LLVMSetMetadata(mdCall, LLVMGetMDKindIDInContext(state->context, "asm.instr", 9), mdNode);

    const ConversionDescriptor* cd = &descriptors[instr->type];

    switch (cd->type)
    {
        LLVMValueRef operand1;
        LLVMValueRef operand2;
        LLVMValueRef result;

        case CT_FUNCTION:
            cd->u.function(instr, state);
            break;
        case CT_BINARY_LLVM:
            operand1 = ll_operand_load(cd->u.binary_llvm.dataType, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(cd->u.binary_llvm.dataType, ALIGN_MAXIMUM, &instr->src, state);
            if (LLVMTypeOf(operand2) != LLVMTypeOf(operand1))
                operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = cd->u.binary_llvm.buildFunc(state->builder, operand1, operand2, "");
            if (cd->u.binary_llvm.fastMath && state->enableFastMath)
                ll_support_enable_fast_math(result);
            if (cd->u.binary_llvm.flagFunc)
                cd->u.binary_llvm.flagFunc(state, result, operand1, operand2);
            ll_operand_store(cd->u.binary_llvm.dataType, ALIGN_MAXIMUM, &instr->dst, cd->u.binary_llvm.prh, result, state);
            break;
        case CT_NOT_IMPLEMENTED:
            printf("Could not handle instruction: %s\n", instr2string(instr, 0, NULL, NULL));
            warn_if_reached();
            break;
        case CT_NOP:
            break;
        default:
            warn_if_reached();
    }
}

/**
 * @}
 **/
