
#include <llvm/ADT/ArrayRef.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>

#include <llsupport.h>


extern "C"
LLVMValueRef
ll_support_get_intrinsic(LLVMModuleRef module, LLSupportIntrinsics intrinsic, LLVMTypeRef* types, unsigned typeCount)
{
    llvm::ArrayRef<llvm::Type*> Tys(llvm::unwrap(types), typeCount);
    llvm::Intrinsic::ID intrinsicId;

    switch (intrinsic)
    {
        case LL_INTRINSIC_DO_NOTHING: intrinsicId = llvm::Intrinsic::donothing; break;
        case LL_INTRINSIC_CTPOP: intrinsicId = llvm::Intrinsic::ctpop; break;
        case LL_INTRINSIC_SADD_WITH_OVERFLOW: intrinsicId = llvm::Intrinsic::sadd_with_overflow; break;
        case LL_INTRINSIC_SSUB_WITH_OVERFLOW: intrinsicId = llvm::Intrinsic::ssub_with_overflow; break;
        default: intrinsicId = llvm::Intrinsic::not_intrinsic; break;
    }

    return llvm::wrap(llvm::Intrinsic::getDeclaration(llvm::unwrap(module), intrinsicId, Tys));
}
