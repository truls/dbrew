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
#include <llfunction-internal.h>
#include <llsupport.h>

/**
 * \ingroup LLInstruction
 * \defgroup LLInstruction Instruction
 *
 * @{
 **/
#define STACK_POINTER_CAST
#define SHUFFLE_VECTOR

enum OperandDataType {
    OP_SI,
    OP_VI8,
    OP_VI64,
    OP_SF,
    OP_VF32,
    OP_VF64,
};

typedef enum OperandDataType OperandDataType;

enum PartialRegisterHandling {
    /**
     * \brief Default handling for general purpose registers
     *
     * For general purpose registers with a 32-bit operand the upper part is
     * zeroed, otherwise it is kept. For SSE registers, this is handling is not
     * allowed since there is no default (depending on VEX prefix).
     **/
    REG_DEFAULT,
    REG_ZERO_UPPER,
    REG_KEEP_UPPER
};

typedef enum PartialRegisterHandling PartialRegisterHandling;

static LLVMTypeRef
ll_get_operand_type(OperandDataType dataType, int bits, LLState* state)
{
    LLVMTypeRef type = NULL;

    switch (dataType)
    {
        case OP_SI:
            type = LLVMIntTypeInContext(state->context, bits);
            break;
        case OP_VI8:
            if (bits % 8 == 0)
            {
                type = LLVMVectorType(LLVMInt8TypeInContext(state->context), bits / 8);
            }
            else
            {
                warn_if_reached();
            }
            break;
        case OP_VI64:
            if (bits % 64 == 0)
            {
                type = LLVMVectorType(LLVMInt64TypeInContext(state->context), bits / 64);
            }
            else
            {
                warn_if_reached();
            }
            break;
        case OP_SF:
            if (bits == 32)
            {
                type = LLVMFloatTypeInContext(state->context);
            }
            else if (bits == 64)
            {
                type = LLVMDoubleTypeInContext(state->context);
            }
            else
            {
                warn_if_reached();
            }
            break;
        case OP_VF32:
            if (bits % 32 == 0)
            {
                type = LLVMVectorType(LLVMFloatTypeInContext(state->context), bits / 32);
            }
            else
            {
                warn_if_reached();
            }
            break;
        case OP_VF64:
            if (bits % 64 == 0)
            {
                type = LLVMVectorType(LLVMDoubleTypeInContext(state->context), bits / 64);
            }
            else
            {
                warn_if_reached();
            }
            break;
        default:
            warn_if_reached();
            break;
    }

    return type;
}

static LLVMValueRef
ll_cast_from_int(LLVMValueRef value, OperandDataType dataType, int bits, LLState* state)
{
    LLVMValueRef result;
    LLVMTypeRef target = ll_get_operand_type(dataType, bits, state);
    LLVMTypeKind targetKind = LLVMGetTypeKind(target);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(state->context);

    int valueLength = LLVMGetIntTypeWidth(LLVMTypeOf(value));

    if (targetKind == LLVMVectorTypeKind)
    {
#ifdef SHUFFLE_VECTOR
        int targetSize = LLVMGetVectorSize(target);
        LLVMTypeRef elementType = LLVMGetElementType(target);

        LLVMValueRef shuffleScalars[targetSize];

        for (int i = 0; i < targetSize; i++)
        {
            shuffleScalars[i] = LLVMConstInt(i32, i, false);
        }

        int totalCount = targetSize * valueLength / bits;
        LLVMTypeRef vectorType = LLVMVectorType(elementType, totalCount);
        LLVMValueRef vector = LLVMBuildBitCast(state->builder, value, vectorType, "");
        LLVMValueRef mask = LLVMConstVector(shuffleScalars, targetSize);

        result = LLVMBuildShuffleVector(state->builder, vector, LLVMGetUndef(vectorType), mask, "");
#else
        if (valueLength > bits)
        {
            value = LLVMBuildTruncOrBitCast(state->builder, value, LLVMIntTypeInContext(state->context, bits), "");
        }

        result = LLVMBuildBitCast(state->builder, value, target, "");
#endif
    }
    else
    {
        LLVMTypeRef targetIntType;

        int targetLength;

        // This is specific to x86-64: All floating-point registers we use are
        // vector registers.
        bool useVector = false;

        if (targetKind == LLVMFloatTypeKind)
        {
            targetLength = 32;
            useVector = true;
        }
        else if (targetKind == LLVMDoubleTypeKind)
        {
            targetLength = 64;
            useVector = true;
        }
        else if (targetKind == LLVMIntegerTypeKind)
        {
            targetLength = LLVMGetIntTypeWidth(target);
        }
        else
        {
            targetLength = 0;
            warn_if_reached();
        }

        if (useVector)
        {
            LLVMTypeRef vectorType = LLVMVectorType(target, valueLength / bits);
            LLVMValueRef vector = LLVMBuildBitCast(state->builder, value, vectorType, "");

            result = LLVMBuildExtractElement(state->builder, vector, LLVMConstInt(i32, 0, false), "");
        }
        else
        {
            targetIntType = LLVMIntTypeInContext(state->context, targetLength);

            if (valueLength < targetLength)
                result = LLVMBuildSExtOrBitCast(state->builder, value, targetIntType, "");
            else
                result = LLVMBuildTruncOrBitCast(state->builder, value, targetIntType, "");
        }
    }

    return result;
}

// static LLVMValueRef
// ll_cast_to_int(LLVMValueRef value, int bits, LLState* state)
// {
//     return LLVMBuildBitCast(state->builder, value, LLVMIntTypeInContext(state->context, bits), "");
// }
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
 * \private
 *
 * \param value The value to check
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
 * Get a pointer to the a known global constant
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param constGlobal The constant global address
 * \param state The module state
 * \returns An i8 pointer which represents the address
 **/
static LLVMValueRef
ll_get_global_offset(LLVMValueRef constGlobal, LLState* state)
{
    uintptr_t ptr = LLVMConstIntGetZExtValue(constGlobal);

    if (ptr == 0)
        return LLVMConstPointerNull(LLVMInt8TypeInContext(state->context));

    if (state->globalOffsetBase == 0)
    {
        state->globalOffsetBase = ptr;
        state->globalBase = LLVMAddGlobal(state->module, LLVMInt8TypeInContext(state->context), "__ll_global_base__");
        LLVMAddGlobalMapping(state->engine, state->globalBase, (void*) ptr);
    }

    uintptr_t offset = ptr - state->globalOffsetBase;
    LLVMValueRef llvmOffset = LLVMConstInt(LLVMInt32TypeInContext(state->context), offset, false);

    return LLVMBuildGEP(state->builder, state->globalBase, &llvmOffset, 1, "");
}

/**
 * Get the pointer corresponding to an operand.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param dataType The data type used to create the appropriate pointer type
 * \param operand The operand, must be Ind32 or Ind64
 * \param state The module state
 * \returns The pointer which corresponds to the operand
 **/
static LLVMValueRef
ll_get_operand_address(OperandDataType dataType, Operand* operand, LLState* state)
{
    LLVMValueRef result;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);

    LLVMTypeRef pointerType;
    int addrspace;
    int bits = opTypeWidth(operand);

    switch (operand->seg)
    {
        case OSO_None:
            addrspace = 0;
            break;
        case OSO_UseGS:
            addrspace = 256;
            break;
        case OSO_UseFS:
            addrspace = 257;
            break;
        default:
            addrspace = 0;
            warn_if_reached();
            break;
    }

    pointerType = LLVMPointerType(ll_get_operand_type(dataType, bits, state), addrspace);

    // Optimized method to improve alias analysis which then allows vectorization
    if ((operand->scale % (bits / 8)) == 0 && (((int64_t) operand->val) % (bits / 8)) == 0)
    {
        // TODO: Also move scale into the optimized method
        if (operand->reg != Reg_None)
        {
            result = LLVMBuildSExtOrBitCast(state->builder, state->currentBB->registers[operand->reg - Reg_AX], i64, "");

            if (LLVMIsConstant(result))
                result = LLVMBuildBitCast(state->builder, ll_get_global_offset(result, state), pointerType, "");
            else
                result = LLVMBuildIntToPtr(state->builder, result, pointerType, "");

            if (operand->scale != 0)
            {
                int factor = operand->scale / (bits / 8);
                LLVMValueRef offset = LLVMBuildSExtOrBitCast(state->builder, state->currentBB->registers[operand->ireg - Reg_AX], i64, "");

                if (factor != 1)
                {
                    offset = LLVMBuildMul(state->builder, offset, LLVMConstInt(i64, factor, false), "");
                }

                result = LLVMBuildGEP(state->builder, result, &offset, 1, "");
            }

            if (operand->val != 0)
            {
                LLVMValueRef offset = LLVMConstInt(i64, ((int64_t) operand->val) / (bits / 8), false);

                result = LLVMBuildGEP(state->builder, result, &offset, 1, "");
            }
        }
        else
        {
            result = ll_get_global_offset(LLVMConstInt(i64, operand->val, false), state);
            result = LLVMBuildBitCast(state->builder, result, pointerType, "");

            if (operand->scale != 0)
            {
                int factor = operand->scale / (bits / 8);
                LLVMValueRef offset = LLVMBuildSExtOrBitCast(state->builder, state->currentBB->registers[operand->ireg - Reg_AX], i64, "");

                if (factor != 1)
                {
                    offset = LLVMBuildMul(state->builder, offset, LLVMConstInt(i64, factor, false), "");
                }

                result = LLVMBuildGEP(state->builder, result, &offset, 1, "");
            }
        }

        // LLVMDumpValue(result);
    }
    else
    {
        result = LLVMConstInt(i64, operand->val, false);

        if (operand->reg != Reg_None)
        {
            LLVMValueRef offset = LLVMBuildSExtOrBitCast(state->builder, state->currentBB->registers[operand->reg - Reg_AX], i64, "");
            result = LLVMBuildAdd(state->builder, result, offset, "");
        }

        if (operand->scale > 0)
        {
            LLVMValueRef scale = LLVMBuildSExtOrBitCast(state->builder, state->currentBB->registers[operand->ireg - Reg_AX], i64, "");
            LLVMValueRef factor = LLVMConstInt(LLVMInt64TypeInContext(state->context), operand->scale, false);
            LLVMValueRef offset = LLVMBuildMul(state->builder, scale, factor, "");
            result = LLVMBuildAdd(state->builder, result, offset, "");
        }

        if (LLVMIsConstant(result))
            result = LLVMBuildBitCast(state->builder, ll_get_global_offset(result, state), pointerType, "");
        else
            result = LLVMBuildIntToPtr(state->builder, result, pointerType, "");
    }

    return result;
}

/**
 * Create the value corresponding to an operand.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param dataType The data type used to create the appropriate type
 * \param operand The operand
 * \param state The module state
 * \returns The value which corresponds to the operand
 **/
static LLVMValueRef
ll_operand_load(OperandDataType dataType, Operand* operand, LLState* state)
{
    LLVMValueRef result = NULL;
    LLVMValueRef address;
    LLVMTypeRef type = ll_get_operand_type(dataType, opTypeWidth(operand), state);

    switch (operand->type)
    {
        case OT_Imm8:
        case OT_Imm16:
        case OT_Imm32:
        case OT_Imm64:
            result = LLVMConstInt(type, operand->val, false);
            break;
        case OT_Reg8:
        case OT_Reg16:
        case OT_Reg32:
        case OT_Reg64:
        case OT_Reg128:
        case OT_Reg256:
            {
                LLVMValueRef reg = state->currentBB->registers[operand->reg - Reg_AX];
                // LLVMTypeRef target = ll_get_operand_type(dataType, opTypeWidth(operand), state);
                result = ll_cast_from_int(reg, dataType, opTypeWidth(operand), state);
            }
            break;
        case OT_Ind8:
        case OT_Ind16:
        case OT_Ind32:
        case OT_Ind64:
        case OT_Ind128:
        case OT_Ind256:
            address = ll_get_operand_address(dataType, operand, state);
            result = LLVMBuildLoad(state->builder, address, "");
            // TODO: Require alignment "hints" (?) to go higher.
            if (opTypeWidth(operand) <= 64)
            {
                LLVMSetAlignment(result, opTypeWidth(operand) / 8);
            }
            else
            {
                LLVMSetAlignment(result, 8);
            }
            break;
        case OT_MAX:
        case OT_None:
        default:
            warn_if_reached();
            break;
    }

    return result;
}

/**
 * Store the value in an operand.
 *
 * \todo How do we handle cases where the upper part of the register is
 * unmodified (e.g. ymm, ah)?
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param dataType The data type used to create the pointer type, if necessary
 * \param operand The operand
 * \param value The value to store
 * \param state The module state
 **/
static void
ll_operand_store(OperandDataType dataType, Operand* operand, PartialRegisterHandling zeroHandling, LLVMValueRef value, LLState* state)
{
    LLVMValueRef address;
    LLVMValueRef result;

    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);
    unsigned int operandWidth = opTypeWidth(operand);
    LLVMTypeRef operandIntType = LLVMIntTypeInContext(state->context, opTypeWidth(operand));

    switch (operand->type)
    {
        case OT_Reg8:
        case OT_Reg16:
        case OT_Reg32:
        case OT_Reg64:
        case OT_Reg128:
        case OT_Reg256:
            {
                // bool keepUpper =
                int regWidth = operand->reg < Reg_X0 ? 64 : 256;

                // TODO: This is likely _really_ wrong and buggy.
                // TODO: Keep upper part
                LLVMTypeRef regType = LLVMIntTypeInContext(state->context, regWidth);

                switch (zeroHandling)
                {
                    case REG_DEFAULT:
                        // TODO: Handle ax, al, ah.
                        result = LLVMBuildSExtOrBitCast(state->builder, value, operandIntType, "");
                        result = LLVMBuildZExtOrBitCast(state->builder, result, regType, "");
                        break;
                    case REG_ZERO_UPPER:
                        result = LLVMBuildBitCast(state->builder, value, operandIntType, "");
                        result = LLVMBuildZExtOrBitCast(state->builder, result, regType, "");
                        break;
                    case REG_KEEP_UPPER:
                        if (true)
                        {
                            if (LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMVectorTypeKind)
                            {
                                int elementCount = LLVMGetVectorSize(LLVMTypeOf(value));
                                int totalCount = elementCount * regWidth / operandWidth;
                                LLVMValueRef current = state->currentBB->registers[operand->reg - Reg_AX];
                                LLVMTypeRef vectorType = LLVMVectorType(LLVMGetElementType(LLVMTypeOf(value)), totalCount);
                                LLVMValueRef vectorCurrent = LLVMBuildBitCast(state->builder, current, vectorType, "");

#ifdef SHUFFLE_VECTOR
                                LLVMTypeRef i32 = LLVMInt32TypeInContext(state->context);
                                LLVMValueRef maskElements[totalCount];
                                for (int i = 0; i < elementCount; i++)
                                {
                                    maskElements[i] = LLVMConstInt(i32, i, false);
                                }
                                for (int i = elementCount; i < totalCount; i++)
                                {
                                    maskElements[i] = LLVMGetUndef(i32);
                                }
                                LLVMValueRef mask = LLVMConstVector(maskElements, totalCount);
                                LLVMValueRef enlarged = LLVMBuildShuffleVector(state->builder, value, LLVMGetUndef(LLVMTypeOf(value)), mask, "");
                                for (int i = elementCount; i < totalCount; i++)
                                {
                                    maskElements[i] = LLVMConstInt(i32, totalCount + i, false);
                                }
                                mask = LLVMConstVector(maskElements, totalCount);
                                vectorCurrent = LLVMBuildShuffleVector(state->builder, enlarged, vectorCurrent, mask, "");
#else
                                // This turned out to be sufficient.
                                for (int i = 0; i < elementCount; i++)
                                {
                                    LLVMValueRef index = LLVMConstInt(i64, i, false);
                                    LLVMValueRef element = LLVMBuildExtractElement(state->builder, value, index, "");
                                    vectorCurrent = LLVMBuildInsertElement(state->builder, vectorCurrent, element, index, "");
                                }
#endif

                                result = LLVMBuildBitCast(state->builder, vectorCurrent, regType, "");
                            }
                            else
                            {
                                int totalCount = regWidth / operandWidth;
                                LLVMValueRef current = state->currentBB->registers[operand->reg - Reg_AX];
                                LLVMTypeRef vectorType = LLVMVectorType(LLVMTypeOf(value), totalCount);
                                LLVMValueRef vectorCurrent = LLVMBuildBitCast(state->builder, current, vectorType, "");

                                LLVMValueRef constZero = LLVMConstInt(i64, 0, false);
                                vectorCurrent = LLVMBuildInsertElement(state->builder, vectorCurrent, value, constZero, "");

                                result = LLVMBuildBitCast(state->builder, vectorCurrent, regType, "");
                            }
                        }
                        else
                        {
                            result = LLVMBuildBitCast(state->builder, value, operandIntType, "");
                            LLVMValueRef current = state->currentBB->registers[operand->reg - Reg_AX];
                            LLVMTypeRef vectorType = LLVMVectorType(operandIntType, regWidth / operandWidth);
                            LLVMValueRef vectorCurrent = LLVMBuildBitCast(state->builder, current, vectorType, "");
                            LLVMValueRef constZero = LLVMConstInt(i64, 0, false);
                            LLVMValueRef swapped = LLVMBuildInsertElement(state->builder, vectorCurrent, result, constZero, "");
                            result = LLVMBuildBitCast(state->builder, swapped, regType, "");
                        }
                        break;
                    default:
                        warn_if_reached();
                }

                state->currentBB->registers[operand->reg - Reg_AX] = result;

                char buffer[20];
                int len = snprintf(buffer, sizeof(buffer), "asm.reg.%s", regName(operand->reg, operand->type));
                LLVMSetMetadata(result, LLVMGetMDKindIDInContext(state->context, buffer, len), state->emptyMD);
            }
            // state->currentBB->registers[operand->reg - Reg_AX] = ll_cast_to_int(value, opTypeWidth(operand), state);
            break;
        case OT_Ind8:
        case OT_Ind16:
        case OT_Ind32:
        case OT_Ind64:
        case OT_Ind128:
        case OT_Ind256:
            address = ll_get_operand_address(dataType, operand, state);
            // printf("%d %d\n", LLVMGetIntTypeWidth(LLVMTypeOf(value)), operandWidth);
            // result = LLVMBuildSExtOrBitCast(state->builder, value, operandIntType, "");
            // if (LLVMGetIntTypeWidth(LLVMTypeOf(value)) < operandWidth)
            // {
            //     result = LLVMBuildTruncOrBitCast(state->builder, result, operandIntType, "");
            // }
            // else
            // {
            //     result = LLVMBuildSExtOrBitCast(state->builder, result, operandIntType, "");
            // }
            result = LLVMBuildBitCast(state->builder, value, LLVMGetElementType(LLVMTypeOf(address)), "");
            result = LLVMBuildStore(state->builder, result, address);
            if (opTypeWidth(operand) <= 64)
            {
                LLVMSetAlignment(result, opTypeWidth(operand) / 8);
            }
            else
            {
                LLVMSetAlignment(result, 8);
            }
            break;
        case OT_Imm8:
        case OT_Imm16:
        case OT_Imm32:
        case OT_Imm64:
        case OT_MAX:
        case OT_None:
        default:
            warn_if_reached();
            break;
    }
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
    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);

    LLVMValueRef value = ll_operand_load(OP_SI, &instr->dst, state);
    value = LLVMBuildSExtOrBitCast(state->builder, value, i64, "");

    // Get pointer to current top of stack
    LLVMValueRef spReg = state->currentBB->registers[Reg_SP - Reg_AX];

#ifdef STACK_POINTER_CAST
    LLVMTypeRef i8 = LLVMInt8TypeInContext(state->context);
    LLVMValueRef sp = LLVMBuildIntToPtr(state->builder, spReg, LLVMPointerType(i8, 0), "");
#else
    LLVMValueRef spOffset = LLVMBuildSub(state->builder, spReg, state->currentFunction->spInt, "");
    LLVMValueRef sp = LLVMBuildGEP(state->builder, state->currentFunction->sp, &spOffset, 1, "");
#endif

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

    state->currentBB->registers[Reg_SP - Reg_AX] = newSpReg;
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
    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);

    LLVMValueRef spReg = state->currentBB->registers[Reg_SP - Reg_AX];

#ifdef STACK_POINTER_CAST
    LLVMTypeRef i8 = LLVMInt8TypeInContext(state->context);
    LLVMValueRef sp = LLVMBuildIntToPtr(state->builder, spReg, LLVMPointerType(i8, 0), "");
#else
    LLVMValueRef spOffset = LLVMBuildSub(state->builder, spReg, state->currentFunction->spInt, "");
    LLVMValueRef sp = LLVMBuildGEP(state->builder, state->currentFunction->sp, &spOffset, 1, "");
#endif

    LLVMValueRef castedPtr = LLVMBuildBitCast(state->builder, sp, LLVMPointerType(i64, 0), "");
    LLVMValueRef value = LLVMBuildLoad(state->builder, castedPtr, "");
    LLVMSetAlignment(value, 8);

    ll_operand_store(OP_SI, operand, true, value, state);

    // Advance Stack pointer via a GEP
    LLVMValueRef constAdd = LLVMConstInt(i64, 8, false);
    LLVMValueRef newSp = LLVMBuildGEP(state->builder, sp, &constAdd, 1, "");

    // Cast back to int for register store
    LLVMValueRef newSpReg = LLVMBuildPtrToInt(state->builder, newSp, i64, "");
    LLVMSetMetadata(newSpReg, LLVMGetMDKindIDInContext(state->context, "asm.reg.rsp", 11), state->emptyMD);

    state->currentBB->registers[Reg_SP - Reg_AX] = newSpReg;
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
    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);
    LLVMTypeRef pi64 = LLVMPointerType(i64, 0);

    // Set new instruction pointer register
    uintptr_t rip = instr->addr + instr->len;
    LLVMValueRef ripValue = LLVMConstInt(LLVMInt64TypeInContext(state->context), rip, false);
    state->currentBB->registers[Reg_IP - Reg_AX] = ripValue;

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
            operand1 = ll_operand_load(OP_SI, &instr->src, state);
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, operand1, state);
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
            operand1 = ll_operand_load(OP_SI, &instr->src, state);
            operand2 = ll_operand_load(OP_SI, &instr->dst, state);
            result = LLVMBuildSelect(state->builder, cond, operand1, operand2, "");
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
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
            // TODO: Test this!!
            // DBrew currently cannot decode SETcc.
            cond = ll_flags_condition(instr->type, IT_SETO, state);
            result = LLVMBuildZExtOrBitCast(state->builder, cond, i8, "");
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;

        ////////////////////////////////////////////////////////////////////////
        //// Control Flow Instructions
        ////////////////////////////////////////////////////////////////////////

        case IT_RET:
            operand1 = LLVMBuildSExtOrBitCast(state->builder, state->currentBB->registers[0], LLVMInt64TypeInContext(state->context), "");
            LLVMBuildRet(state->builder, operand1);
            break;
        case IT_JMP:
            // The destination comes from the decoder.
            LLVMBuildBr(state->builder, state->currentBB->nextBranch->llvmBB);
            break;
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
            cond = ll_flags_condition(instr->type, IT_JO, state);
            LLVMBuildCondBr(state->builder, cond, state->currentBB->nextBranch->llvmBB, state->currentBB->nextFallThrough->llvmBB);
            break;

        ////////////////////////////////////////////////////////////////////////
        //// Stack Instructions
        ////////////////////////////////////////////////////////////////////////

        case IT_LEAVE:
            operand1 = ll_operand_load(OP_SI, getRegOp(VT_64, Reg_BP), state);
            ll_operand_store(OP_SI, getRegOp(VT_64, Reg_SP), REG_DEFAULT, operand1, state);
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
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            result = LLVMBuildNot(state->builder, operand1, "");
            ll_flags_invalidate(state);
            // ll_flags_set_not(result, operand1, state);
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_NEG:
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            result = LLVMBuildNeg(state->builder, operand1, "");
            ll_flags_invalidate(state);
            // ll_flags_set_neg(result, operand1, state);
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_INC:
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            operand2 = LLVMConstInt(LLVMTypeOf(operand1), 1, false);
            result = LLVMBuildAdd(state->builder, operand1, operand2, "");
            ll_flags_invalidate(state);
            // ll_flags_set_inc(result, operand1, state);
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_DEC:
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            operand2 = LLVMConstInt(LLVMTypeOf(operand1), 1, false);
            result = LLVMBuildSub(state->builder, operand1, operand2, "");
            ll_flags_invalidate(state);
            // ll_flags_set_dec(result, operand1, state);
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_ADD:
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");

            if (state->unsafePointerOptimizations &&
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
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_ADC:
            // TODO: Test this!!
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");

            operand1 = LLVMBuildAdd(state->builder, operand1, operand2, "");
            operand2 = LLVMBuildSExtOrBitCast(state->builder, state->currentBB->registers[RFLAG_CF], LLVMTypeOf(operand1), "");
            result = LLVMBuildAdd(state->builder, operand1, operand2, "");
            ll_flags_invalidate(state);
            // ll_flags_set_adc(result, operand1, operand2, state);
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_SUB:
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");

            if (state->unsafePointerOptimizations &&
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
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_IMUL:
            // TODO: currently, only the variant with two operands is handled!
            // warn_if_reached();
            if (instr->form == OF_2)
            {
                operand1 = ll_operand_load(OP_SI, &instr->dst, state);
                operand2 = ll_operand_load(OP_SI, &instr->src, state);
                operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
                result = LLVMBuildMul(state->builder, operand1, operand2, "");
            }
            else
            {
                result = LLVMGetUndef(LLVMInt64TypeInContext(state->context));
                warn_if_reached();
            }
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_AND:
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildAnd(state->builder, operand1, operand2, "");
            ll_flags_set_bit(result, state);
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_OR:
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildOr(state->builder, operand1, operand2, "");
            ll_flags_set_bit(result, state);
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_XOR:
            if (opIsEqual(&instr->dst, &instr->src))
            {
                int width = opTypeWidth(&instr->dst);
                result = LLVMConstInt(LLVMIntTypeInContext(state->context, width), 0, false);
            }
            else
            {
                operand1 = ll_operand_load(OP_SI, &instr->dst, state);
                operand2 = ll_operand_load(OP_SI, &instr->src, state);
                operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
                result = LLVMBuildXor(state->builder, operand1, operand2, "");
            }
            ll_flags_set_bit(result, state);
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_SHL:
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildShl(state->builder, operand1, operand2, "");
            ll_flags_invalidate(state);
            // ll_flags_set_shift(result, operand1, operand2, state);
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_SHR:
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildLShr(state->builder, operand1, operand2, "");
            ll_flags_invalidate(state);
            // ll_flags_set_shift(result, operand1, operand2, state);
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_SAR:
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildAShr(state->builder, operand1, operand2, "");
            ll_flags_invalidate(state);
            // ll_flags_set_shift(result, operand1, operand2, state);
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_LEA:
            // assert(opIsInd(&(instr->src)));
            operand1 = ll_get_operand_address(OP_SI, &instr->src, state);
            result = LLVMBuildPtrToInt(state->builder, operand1, LLVMInt64TypeInContext(state->context), "");
            ll_operand_store(OP_SI, &instr->dst, REG_DEFAULT, result, state);
            break;
        case IT_TEST:
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildAnd(state->builder, operand1, operand2, "");
            ll_flags_set_bit(result, state);
            break;
        case IT_CMP:
            operand1 = ll_operand_load(OP_SI, &instr->dst, state);
            operand2 = ll_operand_load(OP_SI, &instr->src, state);
            operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
            result = LLVMBuildSub(state->builder, operand1, operand2, "");
            ll_flags_set_sub(result, operand1, operand2, state);
            break;
        case IT_CLTQ:
            operand1 = ll_operand_load(OP_SI, getRegOp(VT_32, Reg_AX), state);
            ll_operand_store(OP_SI, getRegOp(VT_64, Reg_AX), REG_DEFAULT, operand1, state);
            break;

        ////////////////////////////////////////////////////////////////////////
        //// SSE + AVX Instructions
        ////////////////////////////////////////////////////////////////////////

        case IT_MOVSS:
            operand1 = ll_operand_load(OP_SF, &instr->src, state);
            ll_operand_store(OP_SF, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        case IT_MOVSD:
            operand1 = ll_operand_load(OP_SF, &instr->src, state);
            ll_operand_store(OP_SF, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        case IT_MOVUPS:
            operand1 = ll_operand_load(OP_VF32, &instr->src, state);
            ll_operand_store(OP_VF32, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        case IT_MOVUPD:
            operand1 = ll_operand_load(OP_VF64, &instr->src, state);
            ll_operand_store(OP_VF64, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        case IT_MOVAPS:
            // TODO: Set alignment
            operand1 = ll_operand_load(OP_VF32, &instr->src, state);
            ll_operand_store(OP_VF32, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        case IT_MOVAPD:
            // TODO: Set alignment
            operand1 = ll_operand_load(OP_VF64, &instr->src, state);
            ll_operand_store(OP_VF64, &instr->dst, REG_KEEP_UPPER, operand1, state);
            break;
        // case IT_MOVLPS:
        //     operand1 = ll_operand_load(OP_VF32, &instr->src, state);
        //     ll_operand_store(OP_VF32, &instr->dst, REG_KEEP_UPPER, operand1, state);
        //     break;
        // case IT_MOVLPD:
        //     operand1 = ll_operand_load(OP_VF64, &instr->src, state);
        //     ll_operand_store(OP_VF64, &instr->dst, REG_KEEP_UPPER, operand1, state);
        //     break;
        // case IT_MOVHPS:
        //     {
        //         LLVMValueRef maskElements[4];
        //         maskElements[0] = LLVMConstInt(i32, 0, false);
        //         maskElements[1] = LLVMConstInt(i32, 1, false);
        //         maskElements[2] = LLVMConstInt(i32, 4, false);
        //         maskElements[3] = LLVMConstInt(i32, 5, false);
        //         LLVMValueRef mask = LLVMConstVector(maskElements, 4);

        //         operand1 = ll_operand_load(OP_VF32, &instr->dst, state);
        //         operand2 = ll_operand_load(OP_VF32, &instr->src, state);
        //         result = LLVMBuildShuffleVector(state->builder, operand1, operand2, mask, "");
        //         ll_operand_store(OP_VF32, &instr->dst, REG_KEEP_UPPER, result, state);
        //     }
        //     break;
        case IT_ADDSS:
            operand1 = ll_operand_load(OP_SF, &instr->dst, state);
            operand2 = ll_operand_load(OP_SF, &instr->src, state);
            result = LLVMBuildFAdd(state->builder, operand1, operand2, "");
            ll_operand_store(OP_SF, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_ADDSD:
            operand1 = ll_operand_load(OP_SF, &instr->dst, state);
            operand2 = ll_operand_load(OP_SF, &instr->src, state);
            result = LLVMBuildFAdd(state->builder, operand1, operand2, "");
            ll_operand_store(OP_SF, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_ADDPS:
            operand1 = ll_operand_load(OP_VF32, &instr->dst, state);
            operand2 = ll_operand_load(OP_VF32, &instr->src, state);
            result = LLVMBuildFAdd(state->builder, operand1, operand2, "");
            ll_operand_store(OP_VF32, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_ADDPD:
            operand1 = ll_operand_load(OP_VF64, &instr->dst, state);
            operand2 = ll_operand_load(OP_VF64, &instr->src, state);
            result = LLVMBuildFAdd(state->builder, operand1, operand2, "");
            ll_operand_store(OP_VF64, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_SUBSS:
            operand1 = ll_operand_load(OP_SF, &instr->dst, state);
            operand2 = ll_operand_load(OP_SF, &instr->src, state);
            result = LLVMBuildFSub(state->builder, operand1, operand2, "");
            ll_operand_store(OP_SF, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_SUBSD:
            operand1 = ll_operand_load(OP_SF, &instr->dst, state);
            operand2 = ll_operand_load(OP_SF, &instr->src, state);
            result = LLVMBuildFSub(state->builder, operand1, operand2, "");
            ll_operand_store(OP_SF, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_SUBPS:
            operand1 = ll_operand_load(OP_VF32, &instr->dst, state);
            operand2 = ll_operand_load(OP_VF32, &instr->src, state);
            result = LLVMBuildFSub(state->builder, operand1, operand2, "");
            ll_operand_store(OP_VF32, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_SUBPD:
            operand1 = ll_operand_load(OP_VF64, &instr->dst, state);
            operand2 = ll_operand_load(OP_VF64, &instr->src, state);
            result = LLVMBuildFSub(state->builder, operand1, operand2, "");
            ll_operand_store(OP_VF64, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_MULSS:
            operand1 = ll_operand_load(OP_SF, &instr->dst, state);
            operand2 = ll_operand_load(OP_SF, &instr->src, state);
            result = LLVMBuildFMul(state->builder, operand1, operand2, "");
            ll_operand_store(OP_SF, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_MULSD:
            operand1 = ll_operand_load(OP_SF, &instr->dst, state);
            operand2 = ll_operand_load(OP_SF, &instr->src, state);
            result = LLVMBuildFMul(state->builder, operand1, operand2, "");
            ll_operand_store(OP_SF, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_MULPS:
            operand1 = ll_operand_load(OP_VF32, &instr->dst, state);
            operand2 = ll_operand_load(OP_VF32, &instr->src, state);
            result = LLVMBuildFMul(state->builder, operand1, operand2, "");
            ll_operand_store(OP_VF32, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_MULPD:
            operand1 = ll_operand_load(OP_VF64, &instr->dst, state);
            operand2 = ll_operand_load(OP_VF64, &instr->src, state);
            result = LLVMBuildFMul(state->builder, operand1, operand2, "");
            ll_operand_store(OP_VF64, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        case IT_PXOR:
        case IT_XORPS:
            // TODO: Figure out whether these are equivalent.
            if (opIsEqual(&instr->dst, &instr->src))
            {
                // int count = opTypeWidth(&instr->dst) / 64;

                // LLVMTypeRef vectorType = LLVMVectorType(i64, count);
                // result = LLVMConstNull(vectorType);
                result = LLVMConstInt(LLVMIntTypeInContext(state->context, opTypeWidth(&instr->dst)), 0, false);
            }
            else
            {
                operand1 = ll_operand_load(OP_VI64, &instr->dst, state);
                operand2 = ll_operand_load(OP_VI64, &instr->src, state);
                result = LLVMBuildXor(state->builder, operand1, operand2, "");
            }
            ll_operand_store(OP_VI64, &instr->dst, REG_KEEP_UPPER, result, state);
            break;
        // case IT_UNPCKLPS:
        //     {
        //         LLVMValueRef maskElements[4];
        //         maskElements[0] = LLVMConstInt(i32, 0, false);
        //         maskElements[1] = LLVMConstInt(i32, 4, false);
        //         maskElements[2] = LLVMConstInt(i32, 1, false);
        //         maskElements[3] = LLVMConstInt(i32, 5, false);
        //         LLVMValueRef mask = LLVMConstVector(maskElements, 4);

        //         operand1 = ll_operand_load(OP_VF32, &instr->dst, state);
        //         operand2 = ll_operand_load(OP_VF32, &instr->src, state);
        //         result = LLVMBuildShuffleVector(state->builder, operand1, operand2, mask, "");
        //         ll_operand_store(OP_VF32, &instr->dst, REG_KEEP_UPPER, result, state);
        //     }
        //     break;
        // case IT_UNPCKLPD:
        //     {
        //         LLVMValueRef maskElements[2];
        //         maskElements[0] = LLVMConstInt(i32, 0, false);
        //         maskElements[1] = LLVMConstInt(i32, 2, false);
        //         LLVMValueRef mask = LLVMConstVector(maskElements, 2);

        //         operand1 = ll_operand_load(OP_VF64, &instr->dst, state);
        //         operand2 = ll_operand_load(OP_VF64, &instr->src, state);
        //         result = LLVMBuildShuffleVector(state->builder, operand1, operand2, mask, "");
        //         ll_operand_store(OP_VF64, &instr->dst, REG_KEEP_UPPER, result, state);
        //     }
        //     break;


        ////////////////////////////////////////////////////////////////////////
        //// Unhandled Instructions
        ////////////////////////////////////////////////////////////////////////

        case IT_HINT_CALL:
        case IT_HINT_RET:
            break;
        case IT_CQTO:
        case IT_MOVZBL:
        case IT_SBB:
        case IT_IDIV1:
        case IT_CALL:
        case IT_JMPI:
        case IT_BSF:
        case IT_MUL:
        case IT_DIV:
        case IT_UCOMISD:
        case IT_MOVDQU:
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
