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
 * \defgroup LLOperand Operand
 * \brief Handling of instruction operands
 *
 * @{
 **/

#define SHUFFLE_VECTOR
#define VECTOR_ZEXT_SHUFFLE

/**
 * Infer the size of the operand.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param dataType The data type
 * \param operand The operand
 * \returns The bit length of the operand
 **/
static int
ll_operand_get_type_length(OperandDataType dataType, Operand* operand)
{
    int bits = -1;

    switch (dataType)
    {
        case OP_SI:
        case OP_VI8:
        case OP_VI64:
        case OP_VI32:
        case OP_VF32:
        case OP_VF64:
            bits = opTypeWidth(operand);
            break;
        case OP_SF32:
            bits = 32;
            break;
        case OP_SF64:
            bits = 64;
            break;
        default:
            warn_if_reached();
            break;

    }

    return bits;
}

/**
 * Infer the LLVM type from the requested data type and the number of bits.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param dataType The data type
 * \param bits The number of bits
 * \param state The module state
 * \returns An LLVM type matching the data type and the number of bits
 **/
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
        case OP_VI32:
            if (bits % 32 == 0)
                type = LLVMVectorType(LLVMInt32TypeInContext(state->context), bits / 32);
            else
                warn_if_reached();
            break;
        case OP_SF32:
            type = LLVMFloatTypeInContext(state->context);
            break;
        case OP_SF64:
            type = LLVMDoubleTypeInContext(state->context);
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

/**
 * Cast a value from an integer type to the appropriate LLVM type. See
 * #ll_operand_get_type for the type inference.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param value The value to cast
 * \param dataType The data type
 * \param operand The register operand
 * \param state The module state
 * \returns The casted value to an appropriate type
 **/
static LLVMValueRef
ll_cast_from_int(LLVMValueRef value, OperandDataType dataType, Operand* operand, LLState* state)
{
    int bits = ll_operand_get_type_length(dataType, operand);
    LLVMValueRef result;
    LLVMTypeRef target = ll_operand_get_type(dataType, bits, state);
    LLVMTypeKind targetKind = LLVMGetTypeKind(target);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(state->context);

    int valueLength = LLVMGetIntTypeWidth(LLVMTypeOf(value));

    if (targetKind == LLVMVectorTypeKind)
    {
        int targetSize = LLVMGetVectorSize(target);
        int totalCount = targetSize * valueLength / bits;

        LLVMTypeRef elementType = LLVMGetElementType(target);
        LLVMTypeRef vectorType = LLVMVectorType(elementType, totalCount);
        LLVMValueRef vector = LLVMBuildBitCast(state->builder, value, vectorType, "");

        if (totalCount > targetSize)
        {
            LLVMValueRef maskElements[targetSize];
            for (int i = 0; i < targetSize; i++)
                maskElements[i] = LLVMConstInt(i32, i, false);

            LLVMValueRef mask = LLVMConstVector(maskElements, targetSize);
            result = LLVMBuildShuffleVector(state->builder, vector, LLVMGetUndef(vectorType), mask, "");
        }
        else
            result = vector;
    }
    else
    {
        // This is specific to x86-64: All floating-point registers we use are
        // vector registers.
        if (targetKind != LLVMIntegerTypeKind)
        {
            LLVMTypeRef vectorType = LLVMVectorType(target, valueLength / bits);
            LLVMValueRef vector = LLVMBuildBitCast(state->builder, value, vectorType, "");

            result = LLVMBuildExtractElement(state->builder, vector, LLVMConstInt(i32, 0, false), "");
        }
        else
        {
            if (operand->reg.rt == RT_GP8Leg && operand->reg.ri >= RI_AH && operand->reg.ri < RI_R8L)
                value = LLVMBuildLShr(state->builder, value, LLVMConstInt(LLVMTypeOf(value), 8, false), "");

            result = LLVMBuildTruncOrBitCast(state->builder, value, target, "");
        }
    }

    return result;
}
/**
 * Cast a value to an integer type to store it in the register file.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param value The value to cast
 * \param dataType The data type
 * \param operand The register operand
 * \param zeroHandling Handling of unused upper parts of the register
 * \param state The module state
 * \returns The casted value to an integer of the register type
 **/
static LLVMValueRef
ll_cast_to_int(LLVMValueRef value, OperandDataType dataType, Operand* operand, PartialRegisterHandling zeroHandling, LLState* state)
{
    LLVMTypeRef i32 = LLVMInt32TypeInContext(state->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);
    LLVMTypeRef iVec = LLVMIntTypeInContext(state->context, LL_VECTOR_REGISTER_SIZE);

    int operandWidth = ll_operand_get_type_length(dataType, operand);
    LLVMTypeRef operandIntType = LLVMIntTypeInContext(state->context, operandWidth);

    LLVMValueRef current = ll_get_register(operand->reg, state);
    LLVMValueRef result = NULL;

    if (zeroHandling == REG_DEFAULT)
    {
        if (!opIsGPReg(operand))
            warn_if_reached();

        value = LLVMBuildSExtOrBitCast(state->builder, value, operandIntType, "");
        value = LLVMBuildZExtOrBitCast(state->builder, value, i64, "");
        if (operand->reg.rt == RT_GP32 || operand->reg.rt == RT_GP64)
            result = value;
        else
        {
            uint64_t mask = 0;
            if (operand->reg.rt == RT_GP8Leg && operand->reg.ri >= RI_AH && operand->reg.ri < RI_R8L)
            {
                mask = 0xff00;
                value = LLVMBuildShl(state->builder, value, LLVMConstInt(i64, 8, false), "");
            }
            else if (operand->reg.rt == RT_GP8 || operand->reg.rt == RT_GP8Leg)
                mask = 0xff;
            else if (operand->reg.rt == RT_GP16)
                mask = 0xffff;
            else
                warn_if_reached();

            LLVMValueRef masked = LLVMBuildAnd(state->builder, current, LLVMConstInt(i64, ~mask, false), "");
            result = LLVMBuildOr(state->builder, masked, value, "");
        }
    }
#ifndef VECTOR_ZEXT_SHUFFLE
    else if (zeroHandling == REG_ZERO_UPPER)
    {
        result = LLVMBuildBitCast(state->builder, value, operandIntType, "");
        result = LLVMBuildZExtOrBitCast(state->builder, result, iVec, "");
    }
#endif
    else if (zeroHandling == REG_ZERO_UPPER || zeroHandling == REG_KEEP_UPPER)
    {
        if (!opIsVReg(operand))
            warn_if_reached();

        if (zeroHandling == REG_ZERO_UPPER)
            current = LLVMConstNull(iVec);

        LLVMTypeRef valueType = LLVMTypeOf(value);
        bool valueIsVector = LLVMGetTypeKind(valueType) == LLVMVectorTypeKind;

        int elementCount = valueIsVector ? LLVMGetVectorSize(valueType) : 1;
        int totalCount = elementCount * LL_VECTOR_REGISTER_SIZE / operandWidth;

        if (valueIsVector)
        {
            if (elementCount != totalCount)
            {
                LLVMTypeRef vectorType = LLVMVectorType(LLVMGetElementType(valueType), totalCount);
                LLVMValueRef vectorCurrent = LLVMBuildBitCast(state->builder, current, vectorType, "");

#ifdef SHUFFLE_VECTOR
                LLVMValueRef maskElements[totalCount];
                for (int i = 0; i < totalCount; i++)
                    maskElements[i] = LLVMConstInt(i32, i, false);
                LLVMValueRef mask = LLVMConstVector(maskElements, totalCount);
                LLVMValueRef enlarged = LLVMBuildShuffleVector(state->builder, value, LLVMGetUndef(valueType), mask, "");

                for (int i = elementCount; i < totalCount; i++)
                    maskElements[i] = LLVMConstInt(i32, totalCount + i, false);
                mask = LLVMConstVector(maskElements, totalCount);
                result = LLVMBuildShuffleVector(state->builder, enlarged, vectorCurrent, mask, "");
#else
                result = vectorCurrent;
                for (int i = 0; i < elementCount; i++)
                {
                    LLVMValueRef index = LLVMConstInt(i64, i, false);
                    LLVMValueRef element = LLVMBuildExtractElement(state->builder, value, index, "");
                    result = LLVMBuildInsertElement(state->builder, result, element, index, "");
                }
#endif
            }
            else
                result = value;
        }
        else
        {
            LLVMTypeRef vectorType = LLVMVectorType(valueType, totalCount);
            LLVMValueRef vectorCurrent = LLVMBuildBitCast(state->builder, current, vectorType, "");

            LLVMValueRef constZero = LLVMConstInt(i64, 0, false);
            result = LLVMBuildInsertElement(state->builder, vectorCurrent, value, constZero, "");
        }

        result = LLVMBuildBitCast(state->builder, result, iVec, "");
    }
    else
        warn_if_reached();

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
 * \param pointerType The type of the pointer
 * \param state The module state
 * \returns A pointer of the given type which represents the address
 **/
static LLVMValueRef
ll_get_global_offset(LLVMValueRef constGlobal, LLVMTypeRef pointerType, LLState* state)
{
    uintptr_t ptr = LLVMConstIntGetZExtValue(constGlobal);

    if (ptr == 0)
        return LLVMConstPointerNull(pointerType);

    if (state->globalOffsetBase == 0)
    {
        state->globalOffsetBase = ptr;
        state->globalBase = LLVMAddGlobal(state->module, LLVMInt8TypeInContext(state->context), "__ll_global_base__");
        LLVMAddGlobalMapping(state->engine, state->globalBase, (void*) ptr);
    }

    uintptr_t offset = ptr - state->globalOffsetBase;
    LLVMValueRef llvmOffset = LLVMConstInt(LLVMInt32TypeInContext(state->context), offset, false);
    LLVMValueRef pointer = LLVMBuildGEP(state->builder, state->globalBase, &llvmOffset, 1, "");

    return LLVMBuildPointerCast(state->builder, pointer, pointerType, "");
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
    int bits = ll_operand_get_type_length(dataType, operand);

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
        if (operand->reg.rt != RT_None)
        {
            result = LLVMBuildSExtOrBitCast(state->builder, ll_get_register(operand->reg, state), i64, "");

            if (LLVMIsConstant(result))
                result = ll_get_global_offset(result, pointerType, state);
            else
                result = LLVMBuildIntToPtr(state->builder, result, pointerType, "");

            if (operand->val != 0)
            {
                LLVMValueRef offset = LLVMConstInt(i64, ((int64_t) operand->val) / (bits / 8), false);

                result = LLVMBuildGEP(state->builder, result, &offset, 1, "");
            }
        }
        else
        {
            result = ll_get_global_offset(LLVMConstInt(i64, operand->val, false), pointerType, state);
        }

        if (operand->scale != 0)
        {
            int factor = operand->scale / (bits / 8);
            LLVMValueRef offset = LLVMBuildSExtOrBitCast(state->builder, ll_get_register(operand->ireg, state), i64, "");

            if (LLVMIsNull(result))
            {
                // Fallback to inttoptr if this is definitly not-a-pointer.
                // Therefore, we don't need to use ll_get_global_offset.
                offset = LLVMBuildMul(state->builder, offset, LLVMConstInt(i64, operand->scale, false), "");
                result = LLVMBuildIntToPtr(state->builder, offset, pointerType, "");
            }
            else
            {
                if (factor != 1)
                    offset = LLVMBuildMul(state->builder, offset, LLVMConstInt(i64, factor, false), "");

                result = LLVMBuildGEP(state->builder, result, &offset, 1, "");
            }
        }
    }
    else
    {
        // Inefficient pointer computations for programs which do strange things
        result = LLVMConstInt(i64, operand->val, false);

        if (operand->reg.rt != RT_None)
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
            result = ll_get_global_offset(result, pointerType, state);
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
 * \param alignment Additional alignment information
 * \param operand The operand
 * \param state The module state
 * \returns The value which corresponds to the operand
 **/
LLVMValueRef
ll_operand_load(OperandDataType dataType, Alignment alignment, Operand* operand, LLState* state)
{
    int operandWidth = ll_operand_get_type_length(dataType, operand);
    LLVMValueRef result = NULL;
    LLVMValueRef address;
    LLVMTypeRef type = ll_operand_get_type(dataType, operandWidth, state);

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
        case OT_Reg512:
            {
                LLVMValueRef reg = ll_get_register(operand->reg, state);
                result = ll_cast_from_int(reg, dataType, operand, state);
            }
            break;
        case OT_Ind8:
        case OT_Ind16:
        case OT_Ind32:
        case OT_Ind64:
        case OT_Ind128:
        case OT_Ind256:
        case OT_Ind512:
            address = ll_operand_get_address(dataType, operand, state);
            result = LLVMBuildLoad(state->builder, address, "");
            if (alignment == ALIGN_MAXIMUM)
                LLVMSetAlignment(result, operandWidth / 8);
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
 * \param alignment Additional alignment information
 * \param operand The operand
 * \param zeroHandling Handling of unused upper parts of the register
 * \param value The value to store
 * \param state The module state
 **/
void
ll_operand_store(OperandDataType dataType, Alignment alignment, Operand* operand, PartialRegisterHandling zeroHandling, LLVMValueRef value, LLState* state)
{
    LLVMValueRef address;
    LLVMValueRef result;

    int operandWidth = ll_operand_get_type_length(dataType, operand);

    switch (operand->type)
    {
        case OT_Reg8:
        case OT_Reg16:
        case OT_Reg32:
        case OT_Reg64:
        case OT_Reg128:
        case OT_Reg256:
        case OT_Reg512:
            {
                result = ll_cast_to_int(value, dataType, operand, zeroHandling, state);
                ll_set_register(operand->reg, result, state);

                if (!LLVMIsConstant(result))
                {
                    char buffer[20];
                    int len = snprintf(buffer, sizeof(buffer), "asm.reg.%s", regName(operand->reg));
                    LLVMSetMetadata(result, LLVMGetMDKindIDInContext(state->context, buffer, len), state->emptyMD);
                }
            }
            break;
        case OT_Ind8:
        case OT_Ind16:
        case OT_Ind32:
        case OT_Ind64:
        case OT_Ind128:
        case OT_Ind256:
        case OT_Ind512:
            address = ll_operand_get_address(dataType, operand, state);
            result = LLVMBuildBitCast(state->builder, value, LLVMGetElementType(LLVMTypeOf(address)), "");
            result = LLVMBuildStore(state->builder, result, address);
            if (alignment == ALIGN_MAXIMUM)
                LLVMSetAlignment(result, operandWidth / 8);
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

void
ll_operand_construct_args(LLVMTypeRef fnType, LLVMValueRef* args, LLState* state)
{
    Reg gpRegisters[6] = {
        getReg(RT_GP64, RI_DI),
        getReg(RT_GP64, RI_SI),
        getReg(RT_GP64, RI_D),
        getReg(RT_GP64, RI_C),
        getReg(RT_GP64, RI_8),
        getReg(RT_GP64, RI_9),
    };
    int gpRegisterIndex = 0;

    size_t argCount = LLVMCountParamTypes(fnType);
    LLVMTypeRef argTypes[argCount];
    LLVMGetParamTypes(fnType, argTypes);

    for (uintptr_t i = 0; i < argCount; i++)
    {
        LLVMTypeKind argTypeKind = LLVMGetTypeKind(argTypes[i]);

        switch (argTypeKind)
        {
            case LLVMIntegerTypeKind:
            case LLVMPointerTypeKind:
                {
                    if (gpRegisterIndex >= 6)
                        warn_if_reached();

                    LLVMValueRef reg = ll_get_register(gpRegisters[gpRegisterIndex], state);
                    gpRegisterIndex++;

                    if (argTypeKind == LLVMIntegerTypeKind)
                        args[i] = LLVMBuildTruncOrBitCast(state->builder, reg, argTypes[i], "");
                    else
                        args[i] = LLVMBuildIntToPtr(state->builder, reg, argTypes[i], "");
                }
                break;
            case LLVMVoidTypeKind:
            case LLVMHalfTypeKind:
            case LLVMFloatTypeKind:
            case LLVMDoubleTypeKind:
            case LLVMX86_FP80TypeKind:
            case LLVMFP128TypeKind:
            case LLVMPPC_FP128TypeKind:
            case LLVMLabelTypeKind:
            case LLVMFunctionTypeKind:
            case LLVMStructTypeKind:
            case LLVMArrayTypeKind:
            case LLVMVectorTypeKind:
            case LLVMMetadataTypeKind:
            case LLVMX86_MMXTypeKind:
            case LLVMTokenTypeKind:
            default:
                warn_if_reached();
        }
    }
}

/**
 * @}
 **/
