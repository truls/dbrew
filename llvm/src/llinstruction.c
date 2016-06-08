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
 * \ingroup LLInstruction
 * \defgroup LLInstruction Instruction
 *
 * @{
 **/

/**
 * Try to estimate whether a value is a pointer. When doing memory operations,
 * knowing that a value is actually a pointer permits us to do pointer
 * arithmetics, which leads to better code, but breaks vectorization and scalar
 * optimizations.
 *
 * This function is rather aggressive in marking values as pointer, as pointer
 * arithmetic for arithmetic operations is disabled by default. It can be
 * enabled via #ll_engine_enable_unsafe_pointer_optimizations.
 *
 * \todo Implement better heuristics here?
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param value The value to check
 * \param state The module state
 * \returns Whether the value should be treated as pointer
 **/
static bool
ll_value_is_pointer(LLVMValueRef value, LLState* state)
{
    // LLVMDumpValue(value);
    // printf("!!! %d\n", LLVMIsConstant(value));
    if (LLVMIsAConstantInt(value))
        return false;
    else if (LLVMIsAConstantExpr(value))
    {
        if (LLVMGetConstOpcode(value) == LLVMPtrToInt)
            return true;
        return false;
    }
    else if (LLVMIsConstant(value))
    {
        return false;
    }
    else if (LLVMIsAInstruction(value))
    {
        if (LLVMGetInstructionOpcode(value) == LLVMLoad)
            return false;
        if (LLVMGetInstructionOpcode(value) == LLVMFPToSI)
            return false;
        if (LLVMGetInstructionOpcode(value) == LLVMFPToUI)
            return false;
        if (LLVMGetInstructionOpcode(value) == LLVMPtrToInt)
            return true;

        // Other candidates?
    }

    // TODO: Implement better heuristics here.

    // The problem is: we don't know much about the value, except that its an
    // integer. The value is likely a PHI node. What do we do? In case of doubt,
    // call it a pointer and hope that LLVM will understand our intention.

    (void) state;

    return true;
}

/**
 * Handling of a push instruction.
 *
 * \todo Handle pushing of words (16 bits)
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param instr The push instruction
 * \param state The module state
 **/
static void
ll_generate_push(Instr* instr, LLState* state)
{
    LLVMTypeRef i8 = LLVMInt8TypeInContext(state->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);

    LLVMValueRef value = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
    value = LLVMBuildSExtOrBitCast(state->builder, value, i64, "");

    // Get pointer to current top of stack
    LLVMValueRef spReg = ll_get_register(Reg_SP, state);
    LLVMValueRef sp = LLVMBuildIntToPtr(state->builder, spReg, LLVMPointerType(i8, 0), "");

    // Decrement Stack Pointer via a GEP instruction
    LLVMValueRef constSub = LLVMConstInt(i64, -8, false);
    LLVMValueRef newSp = LLVMBuildGEP(state->builder, sp, &constSub, 1, "");

    // Store the new value
    LLVMValueRef castedPtr = LLVMBuildBitCast(state->builder, newSp, LLVMPointerType(i64, 0), "");
    LLVMValueRef store = LLVMBuildStore(state->builder, value, castedPtr);
    LLVMSetAlignment(store, 8);

    // Cast back to int for register store
    LLVMValueRef newSpReg = LLVMBuildPtrToInt(state->builder, newSp, i64, "");
    LLVMSetMetadata(newSpReg, LLVMGetMDKindIDInContext(state->context, "asm.reg.rsp", 11), state->emptyMD);

    ll_set_register(Reg_SP, newSpReg, state);
}

/**
 * Handling of a pop instruction.
 *
 * \todo Handle popping of words (16 bits)
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param operand The operand of the pop instruction
 * \param state The module state
 **/
static void
ll_generate_pop(Operand* operand, LLState* state)
{
    LLVMTypeRef i8 = LLVMInt8TypeInContext(state->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);

    LLVMValueRef spReg = ll_get_register(Reg_SP, state);
    LLVMValueRef sp = LLVMBuildIntToPtr(state->builder, spReg, LLVMPointerType(i8, 0), "");

    LLVMValueRef castedPtr = LLVMBuildBitCast(state->builder, sp, LLVMPointerType(i64, 0), "");
    LLVMValueRef value = LLVMBuildLoad(state->builder, castedPtr, "");
    LLVMSetAlignment(value, 8);

    ll_operand_store(OP_SI, ALIGN_MAXIMUM, operand, true, value, state);

    // Advance Stack pointer via a GEP
    LLVMValueRef constAdd = LLVMConstInt(i64, 8, false);
    LLVMValueRef newSp = LLVMBuildGEP(state->builder, sp, &constAdd, 1, "");

    // Cast back to int for register store
    LLVMValueRef newSpReg = LLVMBuildPtrToInt(state->builder, newSp, i64, "");
    LLVMSetMetadata(newSpReg, LLVMGetMDKindIDInContext(state->context, "asm.reg.rsp", 11), state->emptyMD);

    ll_set_register(Reg_SP, newSpReg, state);
}

/**
 * Handling of an instruction.
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
    LLVMValueRef cond;
    LLVMValueRef operand1;
    LLVMValueRef operand2;
    LLVMValueRef result;

    LLVMTypeRef i8 = LLVMInt8TypeInContext(state->context);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(state->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);
    LLVMTypeRef pi64 = LLVMPointerType(i64, 0);

    // Set new instruction pointer register
    uintptr_t rip = instr->addr + instr->len;
    LLVMValueRef ripValue = LLVMConstInt(LLVMInt64TypeInContext(state->context), rip, false);
    ll_set_register(Reg_IP, ripValue, state);

    // Add Metadata for debugging.
    LLVMValueRef intrinsicDoNothing = ll_support_get_intrinsic(state->module, LL_INTRINSIC_DO_NOTHING, NULL, 0);
    char* instructionName = instr2string(instr, 0, NULL);
    LLVMValueRef mdCall = LLVMBuildCall(state->builder, intrinsicDoNothing, NULL, 0, "");
    LLVMValueRef mdNode = LLVMMDStringInContext(state->context, instructionName, strlen(instructionName));
    LLVMSetMetadata(mdCall, LLVMGetMDKindIDInContext(state->context, "asm.instr", 9), mdNode);

    // TODO: Flags!
    switch (instr->type)
    {
        case IT_NOP:
            break;

        ////////////////////////////////////////////////////////////////////////
        //// Move Instructions
        ////////////////////////////////////////////////////////////////////////

        case IT_MOV:
        case IT_MOVD:
        case IT_MOVQ:
        case IT_MOVSX:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, operand1, state);
            break;
        case IT_MOVZX:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
            result = LLVMBuildZExtOrBitCast(state->builder, operand1, LLVMIntTypeInContext(state->context, opTypeWidth(&instr->dst)), "");
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_CMOVO:
        case IT_CMOVNO:
        case IT_CMOVC:
        case IT_CMOVNC:
        case IT_CMOVZ:
        case IT_CMOVNZ:
        case IT_CMOVBE:
        case IT_CMOVA:
        case IT_CMOVS:
        case IT_CMOVNS:
        case IT_CMOVP:
        case IT_CMOVNP:
        case IT_CMOVL:
        case IT_CMOVGE:
        case IT_CMOVLE:
        case IT_CMOVG:
            cond = ll_flags_condition(instr->type, IT_CMOVO, state);
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
            operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            result = LLVMBuildSelect(state->builder, cond, operand1, operand2, "");
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_SETO:
        case IT_SETNO:
        case IT_SETC:
        case IT_SETNC:
        case IT_SETZ:
        case IT_SETNZ:
        case IT_SETBE:
        case IT_SETA:
        case IT_SETS:
        case IT_SETNS:
        case IT_SETP:
        case IT_SETNP:
        case IT_SETL:
        case IT_SETGE:
        case IT_SETLE:
        case IT_SETG:
            cond = ll_flags_condition(instr->type, IT_SETO, state);
            result = LLVMBuildZExtOrBitCast(state->builder, cond, i8, "");
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;

        ////////////////////////////////////////////////////////////////////////
        //// Control Flow Instructions
        ////////////////////////////////////////////////////////////////////////

        case IT_CALL:
            {
                if (instr->dst.type != OT_Imm64)
                    warn_if_reached();

                uintptr_t address = instr->dst.val;

                // Find function with corresponding address.
                LLFunction* function = NULL;

                for (size_t i = 0; i < state->functionCount; i++)
                    if (state->functions[i]->address == address)
                        function = state->functions[i];

                if (function == NULL)
                    warn_if_reached();

                LLVMValueRef llvmFunction = function->llvmFunction;
                LLVMAddFunctionAttr(llvmFunction, LLVMInlineHintAttribute);

                // Construct arguments.
                LLVMTypeRef fnType = LLVMGetElementType(LLVMTypeOf(llvmFunction));
                size_t argCount = LLVMCountParamTypes(fnType);

                LLVMValueRef args[argCount];
                ll_operand_construct_args(fnType, args, state);

                result = LLVMBuildCall(state->builder, llvmFunction, args, argCount, "");

                if (LLVMTypeOf(result) != i64)
                    warn_if_reached();

                // TODO: Handle return values except for i64!
                ll_set_register(Reg_AX, result, state);

                // Clobber registers.
                ll_set_register(Reg_CX, LLVMGetUndef(i64), state);
                ll_set_register(Reg_DX, LLVMGetUndef(i64), state);
                ll_set_register(Reg_SI, LLVMGetUndef(i64), state);
                ll_set_register(Reg_DI, LLVMGetUndef(i64), state);
                ll_set_register(Reg_8, LLVMGetUndef(i64), state);
                ll_set_register(Reg_9, LLVMGetUndef(i64), state);
                ll_set_register(Reg_10, LLVMGetUndef(i64), state);
                ll_set_register(Reg_11, LLVMGetUndef(i64), state);
            }
            break;
        case IT_RET:
            {
                LLVMTypeRef fnType = LLVMGetElementType(LLVMTypeOf(state->currentFunction->llvmFunction));
                LLVMTypeRef retType = LLVMGetReturnType(fnType);
                LLVMTypeKind retTypeKind = LLVMGetTypeKind(retType);

                if (retTypeKind == LLVMPointerTypeKind)
                {
                    LLVMValueRef value = ll_operand_load(OP_SI, ALIGN_MAXIMUM, getRegOp(VT_64, Reg_AX), state);
                    result = LLVMBuildIntToPtr(state->builder, value, retType, "");
                }
                else if (retTypeKind == LLVMIntegerTypeKind)
                    // TODO: Non 64-bit integers!
                    result = ll_operand_load(OP_SI, ALIGN_MAXIMUM, getRegOp(VT_64, Reg_AX), state);
                else if (retTypeKind == LLVMFloatTypeKind)
                    result = ll_operand_load(OP_SF, ALIGN_MAXIMUM, getRegOp(VT_32, Reg_X0), state);
                else if (retTypeKind == LLVMDoubleTypeKind)
                    result = ll_operand_load(OP_SF, ALIGN_MAXIMUM, getRegOp(VT_64, Reg_X0), state);
                else
                {
                    result = NULL;
                    warn_if_reached();
                }

                LLVMBuildRet(state->builder, result);
            }
            break;

        ////////////////////////////////////////////////////////////////////////
        //// Stack Instructions
        ////////////////////////////////////////////////////////////////////////

        case IT_LEAVE:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, getRegOp(VT_64, Reg_BP), state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, getRegOp(VT_64, Reg_SP), REG_DEFAULT, operand1, state);
            ll_generate_pop(getRegOp(VT_64, Reg_BP), state);
            break;
        case IT_PUSH:
            ll_generate_push(instr, state);
            break;
        case IT_POP:
            ll_generate_pop(&instr->dst, state);
            break;

        ////////////////////////////////////////////////////////////////////////
        //// Integer Arithmetic Instructions
        ////////////////////////////////////////////////////////////////////////

        case IT_NOT:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            result = LLVMBuildNot(state->builder, operand1, "");
            ll_flags_invalidate(state);
            // ll_flags_set_not(result, operand1, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_NEG:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            result = LLVMBuildNeg(state->builder, operand1, "");
            ll_flags_invalidate(state);
            // ll_flags_set_neg(result, operand1, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_INC:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = LLVMConstInt(LLVMTypeOf(operand1), 1, false);
            result = LLVMBuildAdd(state->builder, operand1, operand2, "");
            ll_flags_invalidate(state);
            // ll_flags_set_inc(result, operand1, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_DEC:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = LLVMConstInt(LLVMTypeOf(operand1), 1, false);
            result = LLVMBuildSub(state->builder, operand1, operand2, "");
            ll_flags_invalidate(state);
            // ll_flags_set_dec(result, operand1, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_ADD:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");

            if (state->enableUnsafePointerOptimizations &&
                ll_value_is_pointer(operand1, state) && LLVMIsConstant(operand2))
            {
                int64_t value = LLVMConstIntGetSExtValue(operand2);

                if ((value % 8) == 0)
                {
                    LLVMValueRef ptr = LLVMBuildIntToPtr(state->builder, operand1, pi64, "");
                    LLVMValueRef offset = LLVMConstInt(i64, value / 8, true);
                    LLVMValueRef add = LLVMBuildGEP(state->builder, ptr, &offset, 1, "");
                    result = LLVMBuildPtrToInt(state->builder, add, LLVMTypeOf(operand1), "");
                }
                else
                    result = LLVMBuildAdd(state->builder, operand1, operand2, "");
            }
            else
                result = LLVMBuildAdd(state->builder, operand1, operand2, "");

            ll_flags_set_add(result, operand1, operand2, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_ADC:
            // TODO: Test this!!
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");

            operand1 = LLVMBuildAdd(state->builder, operand1, operand2, "");
            operand2 = LLVMBuildSExtOrBitCast(state->builder, ll_get_flag(RFLAG_CF, state), LLVMTypeOf(operand1), "");
            result = LLVMBuildAdd(state->builder, operand1, operand2, "");
            ll_flags_invalidate(state);
            // ll_flags_set_adc(result, operand1, operand2, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_SUB:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");

            if (state->enableUnsafePointerOptimizations &&
                ll_value_is_pointer(operand1, state) && LLVMIsConstant(operand2))
            {
                int64_t value = LLVMConstIntGetSExtValue(operand2);

                if ((value % 8) == 0)
                {
                    LLVMValueRef ptr = LLVMBuildIntToPtr(state->builder, operand1, pi64, "");
                    LLVMValueRef offset = LLVMConstInt(i64, -value / 8, true);
                    LLVMValueRef add = LLVMBuildGEP(state->builder, ptr, &offset, 1, "");
                    result = LLVMBuildPtrToInt(state->builder, add, LLVMTypeOf(operand1), "");
                }
                else
                    result = LLVMBuildSub(state->builder, operand1, operand2, "");
            }
            else
                result = LLVMBuildSub(state->builder, operand1, operand2, "");

            ll_flags_set_sub(result, operand1, operand2, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_IMUL:
            // TODO: handle variant with one operand
            if (instr->form == OF_2)
            {
                operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
                operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
                operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
                result = LLVMBuildMul(state->builder, operand1, operand2, "");
            }
            else if (instr->form == OF_3)
            {
                operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
                operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src2, state);
                operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
                result = LLVMBuildMul(state->builder, operand1, operand2, "");
            }
            else
            {
                result = LLVMGetUndef(LLVMInt64TypeInContext(state->context));
                warn_if_reached();
            }
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_AND:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildAnd(state->builder, operand1, operand2, "");
            ll_flags_set_bit(result, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_OR:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildOr(state->builder, operand1, operand2, "");
            ll_flags_set_bit(result, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_XOR:
            if (opIsEqual(&instr->dst, &instr->src))
            {
                int width = opTypeWidth(&instr->dst);
                result = LLVMConstInt(LLVMIntTypeInContext(state->context, width), 0, false);
            }
            else
            {
                operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
                operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
                operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
                result = LLVMBuildXor(state->builder, operand1, operand2, "");
            }
            ll_flags_set_bit(result, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_SHL:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildShl(state->builder, operand1, operand2, "");
            ll_flags_invalidate(state);
            // ll_flags_set_shift(result, operand1, operand2, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_SHR:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildLShr(state->builder, operand1, operand2, "");
            ll_flags_invalidate(state);
            // ll_flags_set_shift(result, operand1, operand2, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_SAR:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildAShr(state->builder, operand1, operand2, "");
            ll_flags_invalidate(state);
            // ll_flags_set_shift(result, operand1, operand2, state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_LEA:
            // assert(opIsInd(&(instr->src)));
            operand1 = ll_operand_get_address(OP_SI, &instr->src, state);
            result = LLVMBuildPtrToInt(state->builder, operand1, LLVMInt64TypeInContext(state->context), "");
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_TEST:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildAnd(state->builder, operand1, operand2, "");
            ll_flags_set_bit(result, state);
            break;
        case IT_CMP:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildSub(state->builder, operand1, operand2, "");
            ll_flags_set_sub(result, operand1, operand2, state);
            break;
        case IT_CLTQ:
            operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, getRegOp(VT_32, Reg_AX), state);
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, getRegOp(VT_64, Reg_AX), REG_DEFAULT, operand1, state);
            break;

        ////////////////////////////////////////////////////////////////////////
        //// SSE + AVX Instructions
        ////////////////////////////////////////////////////////////////////////

        case IT_MOVSS:
            operand1 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->src, state);
            if (opIsInd(&instr->src))
            {
                LLVMValueRef zeroElements[4];
                for (int i = 0; i < 4; i++)
                    zeroElements[i] = LLVMConstReal(LLVMFloatTypeInContext(state->context), 0);
                LLVMValueRef zero = LLVMConstVector(zeroElements, 4);

                result = LLVMBuildInsertElement(state->builder, zero, operand1, LLVMConstInt(i64, 0, false), "");
                opOverwriteType(&instr->dst, VT_128);
                ll_operand_store(OP_VF32, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            }
            else
                ll_operand_store(OP_SF, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        case IT_MOVSD:
            operand1 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->src, state);
            if (opIsInd(&instr->src))
            {
                LLVMValueRef zeroElements[2];
                for (int i = 0; i < 2; i++)
                    zeroElements[i] = LLVMConstReal(LLVMDoubleTypeInContext(state->context), 0);
                LLVMValueRef zero = LLVMConstVector(zeroElements, 2);

                result = LLVMBuildInsertElement(state->builder, zero, operand1, LLVMConstInt(i64, 0, false), "");
                opOverwriteType(&instr->dst, VT_128);
                ll_operand_store(OP_VF64, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            }
            else
                ll_operand_store(OP_SF, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        case IT_MOVUPS:
            operand1 = ll_operand_load(OP_VF32, ALIGN_8, &instr->src, state);
            ll_operand_store(OP_VF32, ALIGN_8, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        case IT_MOVUPD:
            operand1 = ll_operand_load(OP_VF64, ALIGN_8, &instr->src, state);
            ll_operand_store(OP_VF64, ALIGN_8, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        case IT_MOVAPS:
            operand1 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->src, state);
            ll_operand_store(OP_VF32, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        case IT_MOVAPD:
            operand1 = ll_operand_load(OP_VF64, ALIGN_MAXIMUM, &instr->src, state);
            ll_operand_store(OP_VF64, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        case IT_MOVLPS:
            operand1 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->src, state);
            ll_operand_store(OP_VF32, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        case IT_MOVLPD:
            operand1 = ll_operand_load(OP_VF64, ALIGN_MAXIMUM, &instr->src, state);
            ll_operand_store(OP_VF64, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        case IT_MOVHPS:
            {
                LLVMValueRef maskElements[4];
                maskElements[0] = LLVMConstInt(i32, 0, false);
                maskElements[1] = LLVMConstInt(i32, 1, false);
                maskElements[2] = LLVMConstInt(i32, 4, false);
                maskElements[3] = LLVMConstInt(i32, 5, false);
                LLVMValueRef mask = LLVMConstVector(maskElements, 4);

                operand1 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->dst, state);
                operand2 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->src, state);
                result = LLVMBuildShuffleVector(state->builder, operand1, operand2, mask, "");
                ll_operand_store(OP_VF32, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            }
            break;
        case IT_ADDSS:
            operand1 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->src, state);
            result = LLVMBuildFAdd(state->builder, operand1, operand2, "");
            ll_operand_store(OP_SF, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_ADDSD:
            operand1 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->src, state);
            result = LLVMBuildFAdd(state->builder, operand1, operand2, "");
            ll_operand_store(OP_SF, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_ADDPS:
            operand1 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->src, state);
            result = LLVMBuildFAdd(state->builder, operand1, operand2, "");
            ll_operand_store(OP_VF32, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_ADDPD:
            operand1 = ll_operand_load(OP_VF64, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_VF64, ALIGN_MAXIMUM, &instr->src, state);
            result = LLVMBuildFAdd(state->builder, operand1, operand2, "");
            ll_operand_store(OP_VF64, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_SUBSS:
            operand1 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->src, state);
            result = LLVMBuildFSub(state->builder, operand1, operand2, "");
            ll_operand_store(OP_SF, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_SUBSD:
            operand1 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->src, state);
            result = LLVMBuildFSub(state->builder, operand1, operand2, "");
            ll_operand_store(OP_SF, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_SUBPS:
            operand1 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->src, state);
            result = LLVMBuildFSub(state->builder, operand1, operand2, "");
            ll_operand_store(OP_VF32, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_SUBPD:
            operand1 = ll_operand_load(OP_VF64, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_VF64, ALIGN_MAXIMUM, &instr->src, state);
            result = LLVMBuildFSub(state->builder, operand1, operand2, "");
            ll_operand_store(OP_VF64, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_MULSS:
            operand1 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->src, state);
            result = LLVMBuildFMul(state->builder, operand1, operand2, "");
            ll_operand_store(OP_SF, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_MULSD:
            operand1 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_SF, ALIGN_MAXIMUM, &instr->src, state);
            result = LLVMBuildFMul(state->builder, operand1, operand2, "");
            ll_operand_store(OP_SF, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_MULPS:
            operand1 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->src, state);
            result = LLVMBuildFMul(state->builder, operand1, operand2, "");
            ll_operand_store(OP_VF32, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_MULPD:
            operand1 = ll_operand_load(OP_VF64, ALIGN_MAXIMUM, &instr->dst, state);
            operand2 = ll_operand_load(OP_VF64, ALIGN_MAXIMUM, &instr->src, state);
            result = LLVMBuildFMul(state->builder, operand1, operand2, "");
            ll_operand_store(OP_VF64, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_XORPS:
            if (opIsEqual(&instr->dst, &instr->src))
            {
                LLVMValueRef zeroElements[4];
                for (int i = 0; i < 4; i++)
                    zeroElements[i] = LLVMConstReal(LLVMFloatTypeInContext(state->context), 0);
                result = LLVMConstVector(zeroElements, 4);
            }
            else
            {
                operand1 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->dst, state);
                operand2 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->src, state);
                result = LLVMBuildXor(state->builder, operand1, operand2, "");
            }
            ll_operand_store(OP_VF32, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_XORPD:
            if (opIsEqual(&instr->dst, &instr->src))
            {
                LLVMValueRef zeroElements[2];
                for (int i = 0; i < 2; i++)
                    zeroElements[i] = LLVMConstReal(LLVMDoubleTypeInContext(state->context), 0);
                result = LLVMConstVector(zeroElements, 2);
            }
            else
            {
                operand1 = ll_operand_load(OP_VF64, ALIGN_MAXIMUM, &instr->dst, state);
                operand2 = ll_operand_load(OP_VF64, ALIGN_MAXIMUM, &instr->src, state);
                result = LLVMBuildXor(state->builder, operand1, operand2, "");
            }
            ll_operand_store(OP_VF64, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_PXOR:
            // TODO: Figure out whether these are equivalent.
            if (opIsEqual(&instr->dst, &instr->src))
            {
                result = LLVMConstInt(LLVMIntTypeInContext(state->context, opTypeWidth(&instr->dst)), 0, false);
            }
            else
            {
                operand1 = ll_operand_load(OP_VI64, ALIGN_MAXIMUM, &instr->dst, state);
                operand2 = ll_operand_load(OP_VI64, ALIGN_MAXIMUM, &instr->src, state);
                result = LLVMBuildXor(state->builder, operand1, operand2, "");
            }
            ll_operand_store(OP_VI64, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_UNPCKLPS:
            {
                LLVMValueRef maskElements[4];
                maskElements[0] = LLVMConstInt(i32, 0, false);
                maskElements[1] = LLVMConstInt(i32, 4, false);
                maskElements[2] = LLVMConstInt(i32, 1, false);
                maskElements[3] = LLVMConstInt(i32, 5, false);
                LLVMValueRef mask = LLVMConstVector(maskElements, 4);

                operand1 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->dst, state);
                operand2 = ll_operand_load(OP_VF32, ALIGN_MAXIMUM, &instr->src, state);
                result = LLVMBuildShuffleVector(state->builder, operand1, operand2, mask, "");
                ll_operand_store(OP_VF32, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            }
            break;
        case IT_UNPCKLPD:
            {
                LLVMValueRef maskElements[2];
                maskElements[0] = LLVMConstInt(i32, 0, false);
                maskElements[1] = LLVMConstInt(i32, 2, false);
                LLVMValueRef mask = LLVMConstVector(maskElements, 2);

                operand1 = ll_operand_load(OP_VF64, ALIGN_MAXIMUM, &instr->dst, state);
                operand2 = ll_operand_load(OP_VF64, ALIGN_MAXIMUM, &instr->src, state);
                result = LLVMBuildShuffleVector(state->builder, operand1, operand2, mask, "");
                ll_operand_store(OP_VF64, ALIGN_MAXIMUM, &instr->dst, REG_KEEP_UPPER, result, state);
            }
            break;


        ////////////////////////////////////////////////////////////////////////
        //// Unhandled Instructions
        ////////////////////////////////////////////////////////////////////////

        // These are no instructions
        case IT_HINT_CALL:
        case IT_HINT_RET:
            break;

        // These are handled by the basic block generation code.
        case IT_JMP:
        case IT_JO:
        case IT_JNO:
        case IT_JC:
        case IT_JNC:
        case IT_JZ:
        case IT_JNZ:
        case IT_JBE:
        case IT_JA:
        case IT_JS:
        case IT_JNS:
        case IT_JP:
        case IT_JNP:
        case IT_JL:
        case IT_JGE:
        case IT_JLE:
        case IT_JG:
            break;

        case IT_DIVSS:
        case IT_DIVSD:
        case IT_DIVPS:
        case IT_DIVPD:
        case IT_ORPS:
        case IT_ORPD:
        case IT_ANDPS:
        case IT_ANDPD:
        case IT_ANDNPS:
        case IT_ANDNPD:
        case IT_MAXSS:
        case IT_MAXSD:
        case IT_MAXPS:
        case IT_MAXPD:
        case IT_MINSS:
        case IT_MINSD:
        case IT_MINPS:
        case IT_MINPD:
        case IT_SQRTSS:
        case IT_SQRTSD:
        case IT_SQRTPS:
        case IT_SQRTPD:
        case IT_COMISS:
        case IT_COMISD:
        case IT_UCOMISS:
        case IT_ADDSUBPS:
        case IT_ADDSUBPD:
        case IT_HADDPS:
        case IT_HADDPD:
        case IT_HSUBPS:
        case IT_HSUBPD:
        case IT_RCPSS:
        case IT_RCPPS:
        case IT_RSQRTSS:
        case IT_RSQRTPS:
        case IT_PCMPEQW:
        case IT_PCMPEQD:
        case IT_CQTO:
        case IT_SBB:
        case IT_IDIV1:
        case IT_JMPI:
        case IT_BSF:
        case IT_MUL:
        case IT_DIV:
        case IT_UCOMISD:
        case IT_MOVDQU:
        case IT_MOVHPD:
        case IT_UNPCKHPS:
        case IT_UNPCKHPD:
        case IT_PMINUB:
        case IT_PADDQ:
        case IT_MOVDQA:
        case IT_PCMPEQB:
        case IT_PMOVMSKB:
        case IT_Max:
        case IT_Invalid:
        case IT_None:
        default:
            printf("%s\n", instr2string(instr, 0, NULL));
            warn_if_reached();
            break;
    }
}

/**
 * @}
 **/
