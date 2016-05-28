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

#include <lloperand-internal.h>

#include <llbasicblock-internal.h>
#include <llcommon.h>
#include <llcommon-internal.h>

/**
 * \ingroup LLOperand
 * \defgroup LLOperand Operand
 *
 * @{
 **/

#define SHUFFLE_VECTOR

static LLVMTypeRef
ll_operand_get_type(OperandDataType dataType, int bits, LLState* state)
{
    LLVMTypeRef type = NULL;

    switch (dataType)
    {
        case OP_SI:
            type = LLVMIntTypeInContext(state->context, bits);
            break;
        case OP_VI8:
            if (bits % 8 == 0)
                type = LLVMVectorType(LLVMInt8TypeInContext(state->context), bits / 8);
            else
                warn_if_reached();
            break;
        case OP_VI64:
            if (bits % 64 == 0)
                type = LLVMVectorType(LLVMInt64TypeInContext(state->context), bits / 64);
            else
                warn_if_reached();
            break;
        case OP_SF:
            if (bits == 32)
                type = LLVMFloatTypeInContext(state->context);
            else if (bits == 64)
                type = LLVMDoubleTypeInContext(state->context);
            else
                warn_if_reached();
            break;
        case OP_VF32:
            if (bits % 32 == 0)
                type = LLVMVectorType(LLVMFloatTypeInContext(state->context), bits / 32);
            else
                warn_if_reached();
            break;
        case OP_VF64:
            if (bits % 64 == 0)
                type = LLVMVectorType(LLVMDoubleTypeInContext(state->context), bits / 64);
            else
                warn_if_reached();
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
    LLVMTypeRef target = ll_operand_get_type(dataType, bits, state);
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
LLVMValueRef
ll_operand_get_address(OperandDataType dataType, Operand* operand, LLState* state)
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

    pointerType = LLVMPointerType(ll_operand_get_type(dataType, bits, state), addrspace);

    // Optimized method to improve alias analysis which then allows vectorization
    if ((operand->scale % (bits / 8)) == 0 && (((int64_t) operand->val) % (bits / 8)) == 0)
    {
        // TODO: Also move scale into the optimized method
        if (operand->reg != Reg_None)
        {
            result = LLVMBuildSExtOrBitCast(state->builder, ll_get_register(operand->reg, state), i64, "");

            if (LLVMIsConstant(result))
                result = LLVMBuildBitCast(state->builder, ll_get_global_offset(result, state), pointerType, "");
            else
                result = LLVMBuildIntToPtr(state->builder, result, pointerType, "");

            if (operand->scale != 0)
            {
                int factor = operand->scale / (bits / 8);
                LLVMValueRef offset = LLVMBuildSExtOrBitCast(state->builder, ll_get_register(operand->ireg, state), i64, "");

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
                LLVMValueRef offset = LLVMBuildSExtOrBitCast(state->builder, ll_get_register(operand->ireg, state), i64, "");

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
            LLVMValueRef offset = LLVMBuildSExtOrBitCast(state->builder, ll_get_register(operand->reg, state), i64, "");
            result = LLVMBuildAdd(state->builder, result, offset, "");
        }

        if (operand->scale > 0)
        {
            LLVMValueRef scale = LLVMBuildSExtOrBitCast(state->builder, ll_get_register(operand->ireg, state), i64, "");
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
LLVMValueRef
ll_operand_load(OperandDataType dataType, Alignment alignment, Operand* operand, LLState* state)
{
    LLVMValueRef result = NULL;
    LLVMValueRef address;
    LLVMTypeRef type = ll_operand_get_type(dataType, opTypeWidth(operand), state);

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
                LLVMValueRef reg = ll_get_register(operand->reg, state);
                result = ll_cast_from_int(reg, dataType, opTypeWidth(operand), state);
            }
            break;
        case OT_Ind8:
        case OT_Ind16:
        case OT_Ind32:
        case OT_Ind64:
        case OT_Ind128:
        case OT_Ind256:
            address = ll_operand_get_address(dataType, operand, state);
            result = LLVMBuildLoad(state->builder, address, "");
            if (alignment == ALIGN_MAXIMUM)
                LLVMSetAlignment(result, opTypeWidth(operand) / 8);
            else
                LLVMSetAlignment(result, alignment);
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
 * \private
 *
 * \author Alexis Engelke
 *
 * \param dataType The data type used to create the pointer type, if necessary
 * \param operand The operand
 * \param value The value to store
 * \param state The module state
 **/
void
ll_operand_store(OperandDataType dataType, Alignment alignment, Operand* operand, PartialRegisterHandling zeroHandling, LLVMValueRef value, LLState* state)
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
                int regWidth = operand->reg < Reg_X0 ? 64 : LL_VECTOR_REGISTER_SIZE;

                LLVMTypeRef regType = LLVMIntTypeInContext(state->context, regWidth);

                switch (zeroHandling)
                {
                    case REG_DEFAULT:
                        // TODO: Handle ax, al, ah, where the upper part is preserved.
                        result = LLVMBuildSExtOrBitCast(state->builder, value, operandIntType, "");
                        result = LLVMBuildZExtOrBitCast(state->builder, result, regType, "");
                        break;
                    case REG_ZERO_UPPER:
                        result = LLVMBuildBitCast(state->builder, value, operandIntType, "");
                        result = LLVMBuildZExtOrBitCast(state->builder, result, regType, "");
                        break;
                    case REG_KEEP_UPPER:
                        if (LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMVectorTypeKind)
                        {
                            int elementCount = LLVMGetVectorSize(LLVMTypeOf(value));
                            int totalCount = elementCount * regWidth / operandWidth;

                            LLVMValueRef resultVector;

                            if (elementCount != totalCount)
                            {
                                LLVMValueRef current = ll_get_register(operand->reg, state);
                                LLVMTypeRef vectorType = LLVMVectorType(LLVMGetElementType(LLVMTypeOf(value)), totalCount);
                                LLVMValueRef vectorCurrent = LLVMBuildBitCast(state->builder, current, vectorType, "");
#ifdef SHUFFLE_VECTOR
                                LLVMTypeRef i32 = LLVMInt32TypeInContext(state->context);
                                LLVMValueRef maskElements[totalCount];
                                for (int i = 0; i < elementCount; i++)
                                    maskElements[i] = LLVMConstInt(i32, i, false);
                                for (int i = elementCount; i < totalCount; i++)
                                    maskElements[i] = LLVMGetUndef(i32);
                                LLVMValueRef mask = LLVMConstVector(maskElements, totalCount);
                                LLVMValueRef enlarged = LLVMBuildShuffleVector(state->builder, value, LLVMGetUndef(LLVMTypeOf(value)), mask, "");

                                for (int i = elementCount; i < totalCount; i++)
                                    maskElements[i] = LLVMConstInt(i32, totalCount + i, false);
                                mask = LLVMConstVector(maskElements, totalCount);
                                resultVector = LLVMBuildShuffleVector(state->builder, enlarged, vectorCurrent, mask, "");
#else
                                resultVector = vectorCurrent;
                                for (int i = 0; i < elementCount; i++)
                                {
                                    LLVMValueRef index = LLVMConstInt(i64, i, false);
                                    LLVMValueRef element = LLVMBuildExtractElement(state->builder, value, index, "");
                                    resultVector = LLVMBuildInsertElement(state->builder, resultVector, element, index, "");
                                }
#endif
                            }
                            else
                                resultVector = value;

                            result = LLVMBuildBitCast(state->builder, resultVector, regType, "");
                        }
                        else
                        {
                            LLVMValueRef current = ll_get_register(operand->reg, state);
                            LLVMTypeRef vectorType = LLVMVectorType(LLVMTypeOf(value), regWidth / operandWidth);
                            LLVMValueRef vectorCurrent = LLVMBuildBitCast(state->builder, current, vectorType, "");

                            LLVMValueRef constZero = LLVMConstInt(i64, 0, false);
                            LLVMValueRef exchanged = LLVMBuildInsertElement(state->builder, vectorCurrent, value, constZero, "");
                            result = LLVMBuildBitCast(state->builder, exchanged, regType, "");
                        }
                        break;
                    default:
                        warn_if_reached();
                }

                ll_set_register(operand->reg, result, state);

                char buffer[20];
                int len = snprintf(buffer, sizeof(buffer), "asm.reg.%s", regName(operand->reg, operand->type));
                LLVMSetMetadata(result, LLVMGetMDKindIDInContext(state->context, buffer, len), state->emptyMD);
            }
            break;
        case OT_Ind8:
        case OT_Ind16:
        case OT_Ind32:
        case OT_Ind64:
        case OT_Ind128:
        case OT_Ind256:
            address = ll_operand_get_address(dataType, operand, state);
            result = LLVMBuildBitCast(state->builder, value, LLVMGetElementType(LLVMTypeOf(address)), "");
            result = LLVMBuildStore(state->builder, result, address);
            if (alignment == ALIGN_MAXIMUM)
                LLVMSetAlignment(result, opTypeWidth(operand) / 8);
            else
                LLVMSetAlignment(result, alignment);
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
 * @}
 **/
