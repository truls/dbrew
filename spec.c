/**
 * Simple x86_64 emulator/re-generator
 * (c) 2015, Josef Weidendorfer, GPLv2+
 */

#include "spec.h"

#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>


// functions which can be used in code to be rewritten

__attribute__ ((noinline))
uint64_t makeDynamic(uint64_t v)
{
    return v;
}

__attribute__ ((noinline))
uint64_t makeStatic(uint64_t v)
{
    return v;
}


// forward declarations
typedef struct _DBB DBB;
typedef struct _CBB CBB;
typedef struct _EmuState EmuState;
typedef struct _CaptureConfig CaptureConfig;

void brew_print_decoded(DBB* bb);
DBB* brew_decode(Rewriter* c, uint64_t f);
void freeEmuState(Rewriter*);
void freeCaptureConfig(Rewriter*);

/*------------------------------------------------------------
 * Code Storage
 */

typedef struct _CodeStorage {
    int size;
    int fullsize; /* rounded to multiple of a page size */
    int used;
    uint8_t* buf;
} CodeStorage;

CodeStorage* initCodeStorage(int size)
{
    int fullsize;
    uint8_t* buf;
    CodeStorage* cs;

    /* round up size to multiple of a page size */
    fullsize = (size + 4095) & ~4095;

    /* We do not want to use malloc as we need execute permission.
    * This will return an address aligned to a page boundary
    */
    buf = (uint8_t*) mmap(0, fullsize,
                          PROT_READ | PROT_WRITE | PROT_EXEC,
                          MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (buf == (uint8_t*)-1) {
        perror("Can not mmap code region.");
        exit(1);
    }

    cs = (CodeStorage*) malloc(sizeof(CodeStorage));
    cs->size = size;
    cs->fullsize = fullsize;
    cs->buf = buf;
    cs->used = 0;

    //fprintf(stderr, "Allocated Code Storage (size %d)\n", fullsize);

    return cs;
}

void freeCodeStorage(CodeStorage* cs)
{
    if (cs)
        munmap(cs->buf, cs->fullsize);
    free(cs);
}

/* this checks whether enough storage is available, but does
 * not change <used>.
 */
uint8_t* reserveCodeStorage(CodeStorage* cs, int size)
{
    if (cs->fullsize - cs->used < size) {
        fprintf(stderr,
                "Error: CodeStorage (size %d) too small: used %d, need %d\n",
                cs->fullsize, cs->used, size);
        exit(1);
    }
    return cs->buf + cs->used;
}

uint8_t* useCodeStorage(CodeStorage* cs, int size)
{
    uint8_t* p = cs->buf + cs->used;
    assert(cs->fullsize - cs->used >= size);
    cs->used += size;
    return p;
}

/*------------------------------------------------------------*/
/* x86_64 Analyzers
 */

typedef enum _Reg {
    Reg_None = 0,
    // general purpose (order is important, aligned to x86 encoding)
    Reg_AX, Reg_CX, Reg_DX, Reg_BX, Reg_SP, Reg_BP, Reg_SI, Reg_DI,
    Reg_8,  Reg_9,  Reg_10, Reg_11, Reg_12, Reg_13, Reg_14, Reg_15,
    Reg_IP,
    // vector regs (MMX, XMM, YMM)
    Reg_X0, Reg_X1, Reg_X2, Reg_X3, Reg_X4, Reg_X5, Reg_X6, Reg_X7,
    Reg_X8, Reg_X9, Reg_X10, Reg_X11, Reg_X12, Reg_X13, Reg_X14, Reg_X15,
    //
    Reg_Max
} Reg;

typedef enum _InstrType {
    IT_None = 0, IT_Invalid,
    // Hints: not actual instructions
    IT_HINT_CALL, // starting inlining of another function at this point
    IT_HINT_RET,  // ending inlining at this point
    //
    IT_NOP,
    IT_CLTQ,
    IT_PUSH, IT_POP, IT_LEAVE,
    IT_MOV, IT_MOVSX, IT_LEA, IT_MOVZBL,
    IT_NEG, IT_INC, IT_DEC,
    IT_ADD, IT_ADC, IT_SUB, IT_SBB, IT_IMUL,
    IT_XOR, IT_AND, IT_OR,
    IT_SHL, IT_SHR, IT_SAR,
    IT_CALL, IT_RET, IT_JMP, IT_JMPI,
    IT_JG, IT_JE, IT_JL, IT_JNE, IT_JLE, IT_JGE, IT_JP,
    IT_CMP, IT_TEST,
    IT_BSF,
    // SSE
    IT_PXOR, IT_MOVSD, IT_MULSD, IT_ADDSD, IT_SUBSD, IT_UCOMISD,
    IT_MOVDQU, IT_PCMPEQB, IT_PMINUB, IT_PMOVMSKB,
    //
    IT_Max
} InstrType;

typedef enum _ValType {
    // implicit: width given by opcode, use with Instr.vtype
    VT_None = 0, VT_Implicit,
    VT_8, VT_16, VT_32, VT_64, VT_128, VT_256,
    //
    VT_Max
} ValType;

typedef enum _OpType {
    OT_None = 0,
    OT_Imm8, OT_Imm16, OT_Imm32, OT_Imm64,
    OT_Reg8, OT_Reg16, OT_Reg32, OT_Reg64, OT_Reg128, OT_Reg256,
    // mem (64bit addr): register indirect + displacement
    OT_Ind8, OT_Ind16, OT_Ind32, OT_Ind64, OT_Ind128, OT_Ind256,
    //
    OT_MAX
} OpType;

typedef enum _OpSegOverride {
    OSO_None = 0, OSO_UseFS, OSO_UseGS
} OpSegOverride;

typedef struct _Operand {
    uint64_t val; // imm or displacement
    OpType type;
    Reg reg;
    Reg ireg; // with SIB
    int scale; // with SIB
    OpSegOverride seg; // with OP_Ind type
} Operand;

// for passthrough instructions
typedef enum _OperandEncoding {
    OE_Invalid = 0,
    OE_None,
    OE_RM, OE_MR, OE_RMI
} OperandEncoding;

typedef enum _PrefixSet {
    PS_None = 0,
    PS_REX = 1,
    PS_66 = 2,
    PS_F2 = 4,
    PS_F3 = 8,
    PS_2E = 16
} PrefixSet;

typedef enum _OperandForm {
    OF_None = 0,
    OF_0, // no operand or implicit
    OF_1, // 1 operand: push/pop/...
    OF_2, // 2 operands: dst = dst op src
    OF_3, // 3 operands: dst = src op src2
    OF_Max
} OperandForm;

// information about capture state changes in Pass-Through instructions
typedef enum _StateChange {
    SC_None = 0,
    SC_dstDyn // operand dst is valid, should change to dynamic
} StateChange;

typedef struct _Instr {
    uint64_t addr;
    int len;
    InstrType type;

    // annotation for pass-through (not used when ptLen == 0)
    int ptLen;
    PrefixSet ptPSet;
    unsigned char ptOpc[4];
    OperandEncoding ptEnc;
    StateChange ptSChange;

    ValType vtype; // without explicit operands or all operands of same type
    OperandForm form;
    Operand dst, src; //  with binary op: dst = dst op src
    Operand src2; // with ternary op: dst = src op src2
} Instr;

// a decoded basic block
typedef struct _DBB {
    uint64_t addr;
    int size; // in bytes
    int count;
    Instr* instr; // pointer to first decoded instruction
} DBB;

// a captured basic block
typedef struct _CBB {
    // ID: address of original BB + EmuState at start
    uint64_t dec_addr;
    int esID;

    // instructions captured within this BB
    int count;
    Instr* instr;

    // two possible exits: next on branching or fall-through
    CBB *nextBranch, *nextFallThrough;
    // type of instruction ending this BB
    InstrType endType;
    // a hint for conditional branches whether branching is more likely
    Bool preferBranch;

    // for code generation/relocation
    int size;
    uint64_t addr1, addr2;
    Bool genJcc8, genJump;
} CBB;


typedef struct _Rewriter {

    // decoded instructions
    int decInstrCount, decInstrCapacity;
    Instr* decInstr;

    // decoded basic blocks
    int decBBCount, decBBCapacity;
    DBB* decBB;

    // captured instructions
    int capInstrCount, capInstrCapacity;
    Instr* capInstr;

    // captured basic blocks
    int capBBCount, capBBCapacity;
    CBB* capBB;
    CBB* currentCapBB;

    // function to capture
    uint64_t func;

    // buffer for captured code
    int capCodeCapacity;
    CodeStorage* cs;

    // structs for emulator & capture config
    CaptureConfig* cc;
    EmuState* es;
    // saved emulator states
#define SAVEDSTATE_MAX 20
    int savedStateCount;
    EmuState* savedState[SAVEDSTATE_MAX];

    // stack of unfinished BBs to capture
#define CAPTURESTACK_LEN 20
    int capStackTop;
    CBB* capStack[CAPTURESTACK_LEN];

    // capture order
#define GENORDER_MAX 20
    int genOrderCount;
    CBB* genOrder[GENORDER_MAX];

    // for optimization passes
    Bool addInliningHints;
    Bool doCopyPass; // test pass

    // debug output
    Bool showDecoding, showEmuState, showEmuSteps, showOptSteps;
} Rewriter;

// REX prefix, used in parseModRM
#define REX_MASK_B 1
#define REX_MASK_X 2
#define REX_MASK_R 4
#define REX_MASK_W 8

Rewriter* brew_new()
{
    Rewriter* r;
    int i;

    r = (Rewriter*) malloc(sizeof(Rewriter));

    // allocation of other members on demand, capacities may be reset

    r->decInstrCount = 0;
    r->decInstrCapacity = 0;
    r->decInstr = 0;

    r->decBBCount = 0;
    r->decBBCapacity = 0;
    r->decBB = 0;

    r->capInstrCount = 0;
    r->capInstrCapacity = 0;
    r->capInstr = 0;

    r->capBBCount = 0;
    r->capBBCapacity = 0;
    r->capBB = 0;
    r->currentCapBB = 0;
    r->capStackTop = -1;
    r->genOrderCount = 0;

    r->savedStateCount = 0;
    for(i=0; i< SAVEDSTATE_MAX; i++)
        r->savedState[i] = 0;

    r->capCodeCapacity = 0;
    r->cs = 0;

    r->cc = 0;
    r->es = 0;

    // optimization passes
    r->addInliningHints = True;
    r->doCopyPass = True;

    // default: debug off
    r->showDecoding = False;
    r->showEmuState = False;
    r->showEmuSteps = False;

    return r;
}

void initRewriter(Rewriter* r)
{
    if (r->decInstr == 0) {
        // default
        if (r->decInstrCapacity == 0) r->decInstrCapacity = 500;
        r->decInstr = (Instr*) malloc(sizeof(Instr) * r->decInstrCapacity);
    }
    r->decInstrCount = 0;

    if (r->decBB == 0) {
        // default
        if (r->decBBCapacity == 0) r->decBBCapacity = 50;
        r->decBB = (DBB*) malloc(sizeof(DBB) * r->decBBCapacity);
    }
    r->decBBCount = 0;

    if (r->capInstr == 0) {
        // default
        if (r->capInstrCapacity == 0) r->capInstrCapacity = 500;
        r->capInstr = (Instr*) malloc(sizeof(Instr) * r->capInstrCapacity);
    }
    r->capInstrCount = 0;

    if (r->capBB == 0) {
        // default
        if (r->capBBCapacity == 0) r->capBBCapacity = 50;
        r->capBB = (CBB*) malloc(sizeof(CBB) * r->capBBCapacity);
    }
    r->capBBCount = 0;
    r->currentCapBB = 0;

    if (r->cs == 0) {
        if (r->capCodeCapacity == 0) r->capCodeCapacity = 3000;
        if (r->capCodeCapacity >0)
            r->cs = initCodeStorage(r->capCodeCapacity);
    }
    if (r->cs)
        r->cs->used = 0;
}

void brew_free(Rewriter* r)
{
    if (!r) return;

    free(r->decInstr);
    free(r->decBB);
    free(r->capInstr);
    free(r->capBB);

    freeCaptureConfig(r);
    freeEmuState(r);

    if (r->cs)
        freeCodeStorage(r->cs);
    free(r);
}

void brew_set_decoding_capacity(Rewriter* r,
                                 int instrCapacity, int bbCapacity)
{
    r->decInstrCapacity = instrCapacity;
    free(r->decInstr);
    r->decInstr = 0;

    r->decBBCapacity = bbCapacity;
    free(r->decBB);
    r->decBB = 0;
}

void brew_set_capture_capacity(Rewriter* r,
                                int instrCapacity, int bbCapacity,
                                int codeCapacity)
{
    r->capInstrCapacity = instrCapacity;
    free(r->capInstr);
    r->capInstr = 0;

    r->capBBCapacity = bbCapacity;
    free(r->capBB);
    r->capBB = 0;

    if (r->cs)
        freeCodeStorage(r->cs);
    r->cs = 0;
    r->capCodeCapacity = codeCapacity;
}


void brew_set_function(Rewriter* rewriter, uint64_t f)
{
    rewriter->func = f;

    // reset all decoding/state
    initRewriter(rewriter);
    brew_config_reset(rewriter);

    freeEmuState(rewriter);
}

void brew_verbose(Rewriter* rewriter,
                  Bool decode, Bool emuState, Bool emuSteps)
{
    rewriter->showDecoding = decode;
    rewriter->showEmuState = emuState;
    rewriter->showEmuSteps = emuSteps;
}

void brew_optverbose(Rewriter* r, Bool v)
{
    r->showOptSteps = v;
}

uint64_t brew_generated_code(Rewriter* c)
{
    if ((c->cs == 0) || (c->cs->used == 0))
        return 0;

    if (c->genOrderCount == 0) return 0;
    return c->genOrder[0]->addr2;

    //return (uint64_t) c->cs->buf;
}

int brew_generated_size(Rewriter* c)
{
    if ((c->cs == 0) || (c->cs->used == 0))
        return 0;

    if (c->genOrderCount == 0) return 0;
    return c->cs->used - (c->genOrder[0]->addr2 - (uint64_t) c->cs->buf);

    //return c->cs->used;
}

void freeCode(Rewriter* c)
{
    if (c->cs)
        freeCodeStorage(c->cs);

    free(c->cc);
    free(c->es);

    free(c->decBB);
    free(c->decInstr);
    free(c);
}

ValType opValType(Operand* o)
{
    switch(o->type) {
    case OT_Imm8:
    case OT_Reg8:
    case OT_Ind8:
        return VT_8;
    case OT_Imm16:
    case OT_Reg16:
    case OT_Ind16:
        return VT_16;
    case OT_Imm32:
    case OT_Reg32:
    case OT_Ind32:
        return VT_32;
    case OT_Imm64:
    case OT_Reg64:
    case OT_Ind64:
        return VT_64;
    case OT_Reg128:
    case OT_Ind128:
        return VT_128;
    case OT_Reg256:
    case OT_Ind256:
        return VT_256;

    default: assert(0);
    }
    return 0; // invalid;
}

int opTypeWidth(Operand* o)
{
    switch(opValType(o)) {
    case VT_8: return 8;
    case VT_16: return 16;
    case VT_32: return 32;
    case VT_64: return 64;
    case VT_128: return 128;
    case VT_256: return 256;
    default: assert(0);
    }
    return 0;
}

Bool opIsImm(Operand* o)
{
    switch(o->type) {
    case OT_Imm8:
    case OT_Imm16:
    case OT_Imm32:
    case OT_Imm64:
        return True;
    }
    return False;
}

Bool opIsReg(Operand* o)
{
    switch(o->type) {
    case OT_Reg8:
    case OT_Reg16:
    case OT_Reg32:
    case OT_Reg64:
    case OT_Reg128:
    case OT_Reg256:
        return True;
    }
    return False;
}

Bool opIsGPReg(Operand* o)
{
    if (!opIsReg(o)) return False;
    if ((o->reg >= Reg_AX) && (o->reg <= Reg_15))
        return True;
    return False;
}

Bool opIsVReg(Operand* o)
{
    if (!opIsReg(o)) return False;
    if ((o->reg >= Reg_X0) && (o->reg <= Reg_X15))
        return True;
    return False;
}


Bool opIsInd(Operand* o)
{
    switch(o->type) {
    case OT_Ind8:
    case OT_Ind16:
    case OT_Ind32:
    case OT_Ind64:
    case OT_Ind128:
    case OT_Ind256:
        return True;
    }
    return False;
}

Bool opIsEqual(Operand* o1, Operand* o2)
{
    if (o1->type != o2->type)
        return False;
    if (opIsReg(o1))
        return (o1->reg == o2->reg);
    if (opIsImm(o1))
        return (o1->val == o2->val);
    // memory
    assert(opIsInd(o1));
    if (o1->val != o2->val) return False;
    if (o1->reg != o2->reg) return False;
    if (o1->seg != o2->seg) return False;

    if (o1->scale == 0) return True;
    if ((o1->scale != o2->scale) || (o1->ireg != o2->ireg)) return False;
    return True;
}

Operand* getRegOp(ValType t, Reg r)
{
    static Operand o;

    if ((r >= Reg_AX) && (r <= Reg_15)) {
        switch(t) {
        case VT_8:  o.type = OT_Reg8; break;
        case VT_16: o.type = OT_Reg16; break;
        case VT_32: o.type = OT_Reg32; break;
        case VT_64: o.type = OT_Reg64; break;
        default: assert(0);
        }
        o.reg = r;
        return &o;
    }

    if ((r >= Reg_X0) && (r <= Reg_X15)) {
        switch(t) {
        case VT_64:  o.type = OT_Reg64; break;
        case VT_128: o.type = OT_Reg128; break;
        case VT_256: o.type = OT_Reg256; break;
        default: assert(0);
        }
        o.reg = r;
        return &o;
    }
    assert(0);
}

Operand* getImmOp(ValType t, uint64_t v)
{
    static Operand o;

    switch(t) {
    case VT_8:
        o.type = OT_Imm8;
        o.val = v;
        break;

    case VT_16:
        o.type = OT_Imm16;
        o.val = v;
        break;

    case VT_32:
        o.type = OT_Imm32;
        o.val = v;
        break;

    case VT_64:
        o.type = OT_Imm64;
        o.val = v;
        break;

    default: assert(0);
    }

    return &o;
}


void copyOperand(Operand* dst, Operand* src)
{
    dst->type = src->type;
    switch(src->type) {
    case OT_Imm8:
        assert(src->val < (1l<<8));
        // fall-trough
    case OT_Imm16:
        assert(src->val < (1l<<16));
        // fall-trough
    case OT_Imm32:
        assert(src->val < (1l<<32));
        // fall-trough
    case OT_Imm64:
        dst->val = src->val;
        break;
    case OT_Reg8:
    case OT_Reg32:
    case OT_Reg64:
    case OT_Reg128:
    case OT_Reg256:
        dst->reg = src->reg;
        break;
    case OT_Ind8:
    case OT_Ind32:
    case OT_Ind64:
    case OT_Ind128:
    case OT_Ind256:
        assert( (src->reg == Reg_None) ||
                (src->reg == Reg_IP) ||
                ((src->reg >= Reg_AX) && (src->reg <= Reg_15)) );
        dst->reg = src->reg;
        dst->val = src->val;
        dst->seg = src->seg;
        dst->scale = src->scale;
        if (src->scale >0) {
            assert((src->scale == 1) || (src->scale == 2) ||
                   (src->scale == 4) || (src->scale == 8));
            assert((src->ireg >= Reg_AX) && (src->ireg <= Reg_15));
            dst->ireg = src->ireg;
        }
        break;
    default: assert(0);
    }
}

void opOverwriteType(Operand* o, ValType vt)
{
    if (opIsImm(o)) {
        switch(vt) {
        case VT_8:   o->type = OT_Imm8; break;
        case VT_16:  o->type = OT_Imm8; break;
        case VT_32:  o->type = OT_Imm32; break;
        case VT_64:  o->type = OT_Imm64; break;
        default: assert(0);
        }
    }
    else if (opIsReg(o)) {
        switch(vt) {
        case VT_8:   o->type = OT_Reg8; break;
        case VT_16:  o->type = OT_Reg16; break;
        case VT_32:  o->type = OT_Reg32; break;
        case VT_64:  o->type = OT_Reg64; break;
        case VT_128:
            o->type = OT_Reg128;
            assert(opIsVReg(o));
            break;
        case VT_256:
            o->type = OT_Reg256;
            assert(opIsVReg(o));
            break;
        default: assert(0);
        }
    }
    else if (opIsInd(o)) {
        switch(vt) {
        case VT_8:   o->type = OT_Ind8; break;
        case VT_16:  o->type = OT_Ind16; break;
        case VT_32:  o->type = OT_Ind32; break;
        case VT_64:  o->type = OT_Ind64; break;
        case VT_128: o->type = OT_Ind128; break;
        case VT_256: o->type = OT_Ind256; break;
        default: assert(0);
        }
    }
    else
        assert(0);
}

Bool instrIsJcc(InstrType it)
{
    switch(it) {
    case IT_JE:
    case IT_JNE:
    case IT_JP:
    case IT_JLE:
    case IT_JG:
    case IT_JL:
    case IT_JGE:
        return True;
    }
    return False;
}

void copyInstr(Instr* dst, Instr* src)
{
    dst->addr  = src->addr;
    dst->len   = src->len;
    dst->type  = src->type;
    dst->vtype = src->vtype;
    dst->form  = src->form;

    dst->dst.type = OT_None;
    dst->src.type = OT_None;
    dst->src2.type = OT_None;
    switch(src->form) {
    case OF_3:
        copyOperand(&(dst->src2), &(src->src2));
        // fall through
    case OF_2:
        copyOperand(&(dst->src), &(src->src));
        // fall through
    case OF_1:
        copyOperand(&(dst->dst), &(src->dst));
        // fall through
    case OF_0:
        break;
    default: assert(0);
    }

    dst->ptLen = src->ptLen;
    if (src->ptLen > 0) {
        dst->ptPSet = src->ptPSet;
        dst->ptEnc  = src->ptEnc;
        dst->ptSChange = src->ptSChange;
        for(int j=0; j < src->ptLen; j++)
            dst->ptOpc[j] = src->ptOpc[j];
    }
}

void initSimpleInstr(Instr* i, InstrType it)
{
    i->addr = 0; // unknown: created, not parsed
    i->len = 0;

    i->type = it;
    i->ptLen = 0; // no pass-through info
    i->vtype = VT_None;
    i->form = OF_0;
    i->dst.type = OT_None;
    i->src.type = OT_None;
    i->src2.type = OT_None;
}

void initUnaryInstr(Instr* i, InstrType it, Operand* o)
{
    initSimpleInstr(i, it);
    i->form = OF_1;
    copyOperand( &(i->dst), o);
}

void initBinaryInstr(Instr* i, InstrType it, ValType vt,
                     Operand *o1, Operand *o2)
{
    if (vt != VT_None) {
        // if we specify a value type, it must match destination
        assert(vt == opValType(o1));
        // if 2nd operand is other than immediate, types also must match
        if (!opIsImm(o2))
            assert(vt == opValType(o2));
    }

    initSimpleInstr(i, it);
    i->form = OF_2;
    i->vtype = vt;
    copyOperand( &(i->dst), o1);
    copyOperand( &(i->src), o2);
}

void initTernaryInstr(Instr* i, InstrType it,
                      Operand *o1, Operand *o2, Operand* o3)
{
    initSimpleInstr(i, it);
    i->form = OF_3;
    copyOperand( &(i->dst), o1);
    copyOperand( &(i->src), o2);
    copyOperand( &(i->src2), o3);
}


void attachPassthrough(Instr* i, PrefixSet set,
                       OperandEncoding enc, StateChange sc,
                       int b1, int b2, int b3)
{
    assert(i->ptLen == 0);
    i->ptEnc = enc;
    i->ptSChange = sc;
    i->ptPSet = set;
    assert(b1>=0);
    i->ptLen++;
    i->ptOpc[0] = (unsigned char) b1;
    if (b2 < 0) return;
    i->ptLen++;
    i->ptOpc[1] = (unsigned char) b2;
    if (b3 < 0) return;
    i->ptLen++;
    i->ptOpc[2] = (unsigned char) b3;
}

Instr* nextInstr(Rewriter* c, uint64_t a, int len)
{
    Instr* i = c->decInstr + c->decInstrCount;
    assert(c->decInstrCount < c->decInstrCapacity);
    c->decInstrCount++;

    i->addr = a;
    i->len = len;

    i->ptLen = 0;
    i->vtype = VT_None;
    i->form = OF_None;
    i->dst.type = OT_None;
    i->src.type = OT_None;
    i->src2.type = OT_None;

    return i;
}

Instr* addSimple(Rewriter* c, uint64_t a, uint64_t a2, InstrType it)
{
    Instr* i = nextInstr(c, a, a2 - a);
    i->type = it;
    i->form = OF_0;

    return i;
}

Instr* addSimpleVType(Rewriter* c, uint64_t a, uint64_t a2, InstrType it, ValType vt)
{
    Instr* i = nextInstr(c, a, a2 - a);
    i->type = it;
    i->vtype = vt;
    i->form = OF_0;

    return i;
}

Instr* addUnaryOp(Rewriter* c, uint64_t a, uint64_t a2,
                  InstrType it, Operand* o)
{
    Instr* i = nextInstr(c, a, a2 - a);
    i->type = it;
    i->form = OF_1;
    copyOperand( &(i->dst), o);

    return i;
}

Instr* addBinaryOp(Rewriter* c, uint64_t a, uint64_t a2,
                   InstrType it, ValType vt, Operand* o1, Operand* o2)
{
    if ((vt != VT_None) && (vt != VT_Implicit)) {
        // if we specify an explicit value type, it must match destination
        // 2nd operand does not have to match (e.g. conversion/mask extraction)
        assert(vt == opValType(o1));
    }

    Instr* i = nextInstr(c, a, a2 - a);
    i->type = it;
    i->form = OF_2;
    i->vtype = vt;
    copyOperand( &(i->dst), o1);
    copyOperand( &(i->src), o2);

    return i;
}

Instr* addTernaryOp(Rewriter* c, uint64_t a, uint64_t a2,
                    InstrType it, Operand* o1, Operand* o2, Operand* o3)
{
    Instr* i = nextInstr(c, a, a2 - a);
    i->type = it;
    i->form = OF_3;
    copyOperand( &(i->dst), o1);
    copyOperand( &(i->src), o2);
    copyOperand( &(i->src2), o3);

    return i;
}


// Parse RM encoding (r/m,r: op1 is reg or memory operand, op2 is reg/digit)
// Encoding see SDM 2.1
// Input: REX prefix, SegOverride prefix, o1 or o2 may be vector registers
// Fills o1/o2/digit and returns number of bytes parsed
int parseModRM(uint8_t* p,
               int rex, OpSegOverride o1Seg, Bool o1IsVec, Bool o2IsVec,
               Operand* o1, Operand* o2, int* digit)
{
    int modrm, mod, rm, reg; // modRM byte
    int sib, scale, idx, base; // SIB byte
    int64_t disp;
    Reg r;
    OpType ot;
    int o = 0;
    int hasRex = (rex>0);
    int hasDisp8 = 0, hasDisp32 = 0;

    modrm = p[o++];
    mod = (modrm & 192) >> 6;
    reg = (modrm & 56) >> 3;
    rm = modrm & 7;

    ot = (hasRex && (rex & REX_MASK_W)) ? OT_Reg64 : OT_Reg32;
    // r part: reg or digit, give both back to caller
    if (digit) *digit = reg;
    if (o2) {
        r = (o2IsVec ? Reg_X0 : Reg_AX) + reg;
        if (hasRex && (rex & REX_MASK_R)) r += 8;
        o2->type = ot;
        o2->reg = r;
    }

    if (mod == 3) {
        // r, r
        r = (o1IsVec ? Reg_X0 : Reg_AX) + rm;
        if (hasRex && (rex & REX_MASK_B)) r += 8;
        o1->type = ot;
        o1->reg = r;
        return o;
    }

    if (mod == 1) hasDisp8 = 1;
    if (mod == 2) hasDisp32 = 1;
    if ((mod == 0) && (rm == 5)) {
        // mod 0 + rm 5: RIP relative
        hasDisp32 = 1;
    }

    scale = 0;
    if (rm == 4) {
        // SIB
        sib = p[o++];
        scale = 1 << ((sib & 192) >> 6);
        idx   = (sib & 56) >> 3;
        base  = sib & 7;
        if ((base == 5) && (mod == 0))
            hasDisp32 = 1;
    }

    disp = 0;
    if (hasDisp8) {
        // 8bit disp: sign extend
        disp = *((signed char*) (p+o));
        o++;
    }
    if (hasDisp32) {
        disp = *((int32_t*) (p+o));
        o += 4;
    }

    ot = (hasRex && (rex & REX_MASK_W)) ? OT_Ind64 : OT_Ind32;
    o1->type = ot;
    o1->seg = o1Seg;
    o1->scale = scale;
    o1->val = (uint64_t) disp;
    if (scale == 0) {
        r = Reg_AX + rm;
        if (hasRex && (rex & REX_MASK_B)) r += 8;
        o1->reg = ((mod == 0) && (rm == 5)) ? Reg_IP : r;
        return o;
    }

    if (hasRex && (rex & REX_MASK_X)) idx += 8;
    r = Reg_AX + idx;
    o1->ireg = (idx == 4) ? Reg_None : r;


    if (hasRex && (rex & REX_MASK_B)) base += 8;
    r = Reg_AX + base;
    o1->reg = ((base == 5) && (mod == 0)) ? Reg_None : r;

    // no need to use SIB if index register not used
    if (o1->ireg == Reg_None) o1->scale = 0;

    return o;
}


// decode the basic block starting at f (automatically triggered by emulator)
DBB* brew_decode(Rewriter* c, uint64_t f)
{
    Bool hasRex, hasF2, hasF3, has66;
    Bool has2E; // cs-segment override or branch not taken hint (Jcc)
    OpSegOverride segOv;
    int rex;
    uint64_t a;
    int i, off, opc, opc2, digit, old_icount;
    Bool exitLoop;
    uint8_t* fp;
    Operand o1, o2, o3;
    Reg r;
    ValType vt;
    InstrType it;
    Instr* ii;
    DBB* dbb;

    if (f == 0) return 0; // nothing to decode
    if (c->decBB == 0) initRewriter(c);

    // already decoded?
    for(i = 0; i < c->decBBCount; i++)
        if (c->decBB[i].addr == f) return &(c->decBB[i]);

    if (c->showDecoding)
        printf("Decoding BB %lx ...\n", f);

    // start decoding of new BB beginning at f
    assert(c->decBBCount < c->decBBCapacity);
    dbb = &(c->decBB[c->decBBCount]);
    c->decBBCount++;
    dbb->addr = f;
    dbb->count = 0;
    dbb->size = 0;
    dbb->instr = c->decInstr + c->decInstrCount;
    old_icount = c->decInstrCount;

    fp = (uint8_t*) f;
    off = 0;
    hasRex = False;
    rex = 0;
    segOv = OSO_None;
    hasF2 = False;
    hasF3 = False;
    has66 = False;
    has2E = False;
    exitLoop = False;
    while(!exitLoop) {
        a = (uint64_t)(fp + off);

        // prefixes
        while(1) {
            if ((fp[off] >= 0x40) && (fp[off] <= 0x4F)) {
                rex = fp[off] & 15;
                hasRex = True;
                off++;
                continue;
            }
            if (fp[off] == 0xF2) {
                hasF2 = True;
                off++;
                continue;
            }
            if (fp[off] == 0xF3) {
                hasF3 = True;
                off++;
                continue;
            }
            if (fp[off] == 0x66) {
                has66 = True;
                off++;
                continue;
            }
            if (fp[off] == 0x64) {
                segOv = OSO_UseFS;
                off++;
                continue;
            }
            if (fp[off] == 0x65) {
                segOv = OSO_UseGS;
                off++;
                continue;
            }
            if (fp[off] == 0x2E) {
                has2E = True;
                off++;
                continue;
            }
            // no further prefixes
            break;
        }

        opc = fp[off++];
        switch(opc) {

        case 0x01:
            // add r/m,r 32/64 (MR, dst: r/m, src: r)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, &o2, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_ADD, vt, &o1, &o2);
            break;

        case 0x03:
            // add r,r/m 32/64 (RM, dst: r, src: r/m)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_ADD, vt, &o1, &o2);
            break;

        case 0x09:
            // or r/m,r 32/64 (MR, dst: r/m, src: r)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, &o2, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_OR, vt, &o1, &o2);
            break;

        case 0x0B:
            // or r,r/m 32/64 (RM, dst: r, src: r/m)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_OR, vt, &o1, &o2);
            break;

        case 0x0F:
            opc2 = fp[off++];
            switch(opc2) {
            case 0xAF:
                // imul r 32/64, r/m 32/64 (RM, dst: r)
                vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
                off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
                addBinaryOp(c, a, (uint64_t)(fp + off), IT_IMUL, vt, &o1, &o2);
                break;

            case 0x10:
                assert(hasF2);
                // movsd xmm2,xmm1/m64 (RM)
                off += parseModRM(fp+off, rex, segOv, 1, 1, &o2, &o1, 0);
                opOverwriteType(&o1, VT_64);
                opOverwriteType(&o2, VT_64);
                ii = addBinaryOp(c, a, (uint64_t)(fp + off),
                                 IT_MOVSD, VT_Implicit, &o1, &o2);
                attachPassthrough(ii, PS_F2, OE_RM, SC_None, 0x0F, 0x10, -1);
                break;

            case 0x11:
                assert(hasF2);
                // movsd xmm2/m64,xmm1 (MR)
                off += parseModRM(fp+off, rex, segOv, 1, 1, &o1, &o2, 0);
                opOverwriteType(&o1, VT_64);
                opOverwriteType(&o2, VT_64);
                ii = addBinaryOp(c, a, (uint64_t)(fp + off),
                                 IT_MOVSD, VT_Implicit, &o1, &o2);
                attachPassthrough(ii, PS_F2, OE_MR, SC_None, 0x0F, 0x11, -1);
                break;

            case 0x1F:
                off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, 0, &digit);
                switch(digit) {
                case 0:
                    // 0F 1F /0: nop r/m 32
                    addUnaryOp(c, a, (uint64_t)(fp + off), IT_NOP, &o1);
                    break;

                default:
                    addSimple(c, a, (uint64_t)(fp + off), IT_Invalid);
                    break;
                }
                break;

            case 0x2E:
                assert(has66);
                // ucomisd xmm1,xmm2/m64 (RM)
                off += parseModRM(fp+off, rex, segOv, 1, 1, &o2, &o1, 0);
                opOverwriteType(&o1, VT_64);
                opOverwriteType(&o2, VT_64);
                ii = addBinaryOp(c, a, (uint64_t)(fp + off),
                                 IT_UCOMISD, VT_Implicit, &o1, &o2);
                attachPassthrough(ii, PS_66, OE_RM, SC_None, 0x0F, 0x2E, -1);
                break;

            case 0x58:
                assert(hasF2);
                // addsd xmm1,xmm2/m64 (RM)
                off += parseModRM(fp+off, rex, segOv, 1, 1, &o2, &o1, 0);
                opOverwriteType(&o1, VT_64);
                opOverwriteType(&o2, VT_64);
                ii = addBinaryOp(c, a, (uint64_t)(fp + off),
                                 IT_ADDSD, VT_Implicit, &o1, &o2);
                attachPassthrough(ii, PS_F2, OE_RM, SC_None, 0x0F, 0x58, -1);
                break;

            case 0x59:
                assert(hasF2);
                // mulsd xmm1,xmm2/m64 (RM)
                off += parseModRM(fp+off, rex, segOv, 1, 1, &o2, &o1, 0);
                opOverwriteType(&o1, VT_64);
                opOverwriteType(&o2, VT_64);
                ii = addBinaryOp(c, a, (uint64_t)(fp + off),
                                 IT_MULSD, VT_Implicit, &o1, &o2);
                attachPassthrough(ii, PS_F2, OE_RM, SC_None, 0x0F, 0x59, -1);
                break;

            case 0x5C:
                assert(hasF2);
                // subsd xmm1,xmm2/m64 (RM)
                off += parseModRM(fp+off, rex, segOv, 1, 1, &o2, &o1, 0);
                opOverwriteType(&o1, VT_64);
                opOverwriteType(&o2, VT_64);
                ii = addBinaryOp(c, a, (uint64_t)(fp + off),
                                 IT_SUBSD, VT_Implicit, &o1, &o2);
                attachPassthrough(ii, PS_F2, OE_RM, SC_None, 0x0F, 0x5C, -1);
                break;

            case 0x6F:
                assert(hasF3);
                // movdqu xmm1,xmm2/m128 (RM): move unaligned dqw xmm2 -> xmm1
                off += parseModRM(fp+off, rex, segOv, 1, 1, &o2, &o1, 0);
                opOverwriteType(&o1, VT_128);
                opOverwriteType(&o2, VT_128);
                ii = addBinaryOp(c, a, (uint64_t)(fp + off),
                                 IT_MOVDQU, VT_Implicit, &o1, &o2);
                attachPassthrough(ii, PS_F3, OE_RM, SC_None, 0x0F, 0x6F, -1);
                break;

            case 0x74:
                // pcmpeqb mm,mm/m 64/128 (RM): compare packed bytes
                vt = has66 ? VT_128 : VT_64;
                off += parseModRM(fp+off, rex, segOv, 1, 1, &o2, &o1, 0);
                opOverwriteType(&o1, vt);
                opOverwriteType(&o2, vt);
                ii = addBinaryOp(c, a, (uint64_t)(fp + off),
                                 IT_PCMPEQB, VT_Implicit, &o1, &o2);
                attachPassthrough(ii, has66 ? PS_66:0, OE_RM, SC_None,
                                  0x0F, 0x74, -1);
                break;

            case 0x7E:
                assert(has66);
                // movd/q xmm,r/m 32/64 (RM)
                vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
                off += parseModRM(fp+off, rex, segOv, 1, 0, &o2, &o1, 0);
                opOverwriteType(&o1, vt);
                opOverwriteType(&o2, vt);
                ii = addBinaryOp(c, a, (uint64_t)(fp + off),
                                 IT_MOV, VT_Implicit, &o1, &o2);
                attachPassthrough(ii, PS_66, OE_RM, SC_dstDyn, 0x0F, 0x7E, -1);
                break;

            case 0x84: // JE/JZ rel32
            case 0x85: // JNE/JNZ rel32
            case 0x8A: // JP rel32
            case 0x8C: // JL/JNGE rel32
            case 0x8D: // JGE/JNL rel32
            case 0x8E: // JLE/JNG rel32
            case 0x8F: // JG/JNLE rel32
                o1.type = OT_Imm64;
                o1.val = (uint64_t) (fp + off + 4 + *(int32_t*)(fp + off));
                off += 4;
                if      (opc2 == 0x84) it = IT_JE;
                else if (opc2 == 0x85) it = IT_JNE;
                else if (opc2 == 0x8A) it = IT_JP;
                else if (opc2 == 0x8C) it = IT_JL;
                else if (opc2 == 0x8D) it = IT_JGE;
                else if (opc2 == 0x8E) it = IT_JLE;
                else if (opc2 == 0x8F) it = IT_JG;
                else assert(0);
                addUnaryOp(c, a, (uint64_t)(fp + off), it, &o1);
                exitLoop = True;
                break;

            case 0xB6:
                // movzbl r32/64,r/m8 (RM): move byte to (d)word, zero-extend
                vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
                off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
                opOverwriteType(&o1, vt);
                opOverwriteType(&o2, VT_8); // src, r/m8
                addBinaryOp(c, a, (uint64_t)(fp + off), IT_MOVZBL, vt, &o1, &o2);
                break;

            case 0xBC:
                // bsf r,r/m 32/64 (RM): bit scan forward
                vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
                off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
                addBinaryOp(c, a, (uint64_t)(fp + off), IT_BSF, vt, &o1, &o2);
                break;

            case 0xD7:
                // pmovmskb r,mm 64/128 (RM): minimum of packed bytes
                vt = has66 ? VT_128 : VT_64;
                off += parseModRM(fp+off, rex, segOv, 1, 0, &o2, &o1, 0);
                opOverwriteType(&o1, VT_32);
                opOverwriteType(&o2, vt);
                ii = addBinaryOp(c, a, (uint64_t)(fp + off),
                                 IT_PMOVMSKB, VT_32, &o1, &o2);
                attachPassthrough(ii, has66 ? PS_66:0, OE_RM, SC_dstDyn,
                                  0x0F, 0xD7, -1);
                break;

            case 0xDA:
                // pminub mm,mm/m 64/128 (RM): minimum of packed bytes
                vt = has66 ? VT_128 : VT_64;
                off += parseModRM(fp+off, rex, segOv, 1, 1, &o2, &o1, 0);
                opOverwriteType(&o1, vt);
                opOverwriteType(&o2, vt);
                ii = addBinaryOp(c, a, (uint64_t)(fp + off),
                                 IT_PMINUB, VT_Implicit, &o1, &o2);
                attachPassthrough(ii, has66 ? PS_66:0, OE_RM, SC_None,
                                  0x0F, 0xDA, -1);
                break;


            case 0xEF:
                // pxor xmm1, xmm2/m 64/128 (RM)
                vt = has66 ? VT_128 : VT_64;
                off += parseModRM(fp+off, rex, segOv, 1, 1, &o2, &o1, 0);
                opOverwriteType(&o1, vt);
                opOverwriteType(&o2, vt);
                ii = addBinaryOp(c, a, (uint64_t)(fp + off),
                                 IT_PXOR, VT_Implicit, &o1, &o2);
                attachPassthrough(ii, has66 ? PS_66 : 0, OE_RM, SC_None,
                                  0x0F, 0xEF, -1);
                break;

            default:
                addSimple(c, a, (uint64_t)(fp + off), IT_Invalid);
                break;
            }
            break;

        case 0x11:
            // adc r/m,r 32/64 (MR, dst: r/m, src: r)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, &o2, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_ADC, vt, &o1, &o2);
            break;

        case 0x13:
            // adc r,r/m 32/64 (RM, dst: r, src: r/m)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_ADC, vt, &o1, &o2);
            break;

        case 0x19:
            // sbb r/m,r 32/64 (MR, dst: r/m, src: r)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, &o2, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_SBB, vt, &o1, &o2);
            break;

        case 0x1B:
            // sbb r,r/m 32/64 (RM, dst: r, src: r/m)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_SBB, vt, &o1, &o2);
            break;

        case 0x21:
            // and r/m,r 32/64 (MR, dst: r/m, src: r)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, &o2, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_AND, vt, &o1, &o2);
            break;

        case 0x23:
            // and r,r/m 32/64 (RM, dst: r, src: r/m)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_AND, vt, &o1, &o2);
            break;

        case 0x25:
            // and eax,imm32
            o1.type = OT_Imm32;
            o1.val = *(uint32_t*)(fp + off);
            off += 4;
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_AND, VT_32,
                        getRegOp(VT_32, Reg_AX), &o1);
            break;

        case 0x29:
            // sub r/m,r 32/64 (MR)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, &o2, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_SUB, vt, &o1, &o2);
            break;

        case 0x2B:
            // sub r,r/m 32/64 (RM)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_SUB, vt, &o1, &o2);
            break;

        case 0x31:
            // xor r/m,r 32/64 (MR, dst: r/m, src: r)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, &o2, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_XOR, vt, &o1, &o2);
            break;

        case 0x33:
            // xor r,r/m 32/64 (RM, dst: r, src: r/m)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_XOR, vt, &o1, &o2);
            break;

        case 0x39:
            // cmp r/m,r 32/64 (MR)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, &o2, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_CMP, vt, &o1, &o2);
            break;

        case 0x3B:
            // cmp r,r/m 32/64 (RM)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_CMP, vt, &o1, &o2);
            break;

        case 0x3D:
            // cmp eax,imm32
            o1.type = OT_Imm32;
            o1.val = *(uint32_t*)(fp + off);
            off += 4;
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_CMP, VT_32,
                        getRegOp(VT_32, Reg_AX), &o1);
            break;

        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57:
            // push
            r = Reg_AX + (opc - 0x50);
            if (hasRex && (rex & REX_MASK_B)) r += 8;
            addUnaryOp(c, a, (uint64_t)(fp + off),
                       IT_PUSH, getRegOp(VT_64, r));
            break;

        case 0x58: case 0x59: case 0x5A: case 0x5B:
        case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            // pop
            r = Reg_AX + (opc - 0x58);
            if (hasRex && (rex & REX_MASK_B)) r += 8;
            addUnaryOp(c, a, (uint64_t)(fp + off),
                       IT_POP, getRegOp(VT_64, r));
            break;

        case 0x63:
            // movsx r64,r/m32 (RM) mov with sign extension
            assert(rex & REX_MASK_W);
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
            // src is 32 bit
            switch(o2.type) {
            case OT_Reg64: o2.type = OT_Reg32; break;
            case OT_Ind64: o2.type = OT_Ind32; break;
            default: assert(0);
            }
            addBinaryOp(c, a, (uint64_t)(fp + off),
                        IT_MOVSX, VT_None, &o1, &o2);
            break;

        case 0x68:
            // push imm32
            o1.type = OT_Imm32;
            o1.val = *(uint32_t*)(fp + off);
            off += 4;
            addUnaryOp(c, a, (uint64_t)(fp + off), IT_PUSH, &o1);
            break;

        case 0x69:
            // imul r,r/m32/64,imm32 (RMI)
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
            o3.type = OT_Imm32;
            o3.val = *(uint32_t*)(fp + off);
            off += 4;
            addTernaryOp(c, a, (uint64_t)(fp + off), IT_IMUL, &o1, &o2, &o3);
            break;

        case 0x6A:
            // push imm8
            o1.type = OT_Imm8;
            o1.val = *(uint8_t*)(fp + off);
            off++;
            addUnaryOp(c, a, (uint64_t)(fp + off), IT_PUSH, &o1);
            break;

        case 0x6B:
            // imul r,r/m32/64,imm8 (RMI)
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
            o3.type = OT_Imm8;
            o3.val = *(uint8_t*)(fp + off);
            off += 1;
            addTernaryOp(c, a, (uint64_t)(fp + off), IT_IMUL, &o1, &o2, &o3);
            break;


        case 0x74: // JE/JZ rel8
        case 0x75: // JNE/JNZ rel8
        case 0x7A: // JP rel8
        case 0x7C: // JL/JNGE rel8
        case 0x7D: // JGE/JNL rel8
        case 0x7E: // JLE/JNG rel8
        case 0x7F: // JG/JNLE rel8
            o1.type = OT_Imm64;
            o1.val = (uint64_t) (fp + off + 1 + *(int8_t*)(fp + off));
            off += 1;
            if      (opc == 0x74) it = IT_JE;
            else if (opc == 0x75) it = IT_JNE;
            else if (opc == 0x7A) it = IT_JP;
            else if (opc == 0x7C) it = IT_JL;
            else if (opc == 0x7D) it = IT_JGE;
            else if (opc == 0x7E) it = IT_JLE;
            else if (opc == 0x7F) it = IT_JG;
            else assert(0);
            addUnaryOp(c, a, (uint64_t)(fp + off), it, &o1);
            exitLoop = True;
            break;

        case 0x81:
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, 0, &digit);
            switch(digit) {
            case 0: it = IT_ADD; break; // 81/0: add r/m 32/64, imm32
            case 1: it = IT_OR;  break; // 81/1: or  r/m 32/64, imm32
            case 2: it = IT_ADC; break; // 81/2: adc r/m 32/64, imm32
            case 3: it = IT_SBB; break; // 81/3: sbb r/m 32/64, imm32
            case 4: it = IT_AND; break; // 81/4: and r/m 32/64, imm32
            case 5: it = IT_SUB; break; // 81/5: sub r/m 32/64, imm32
            case 6: it = IT_XOR; break; // 81/6: xor r/m 32/64, imm32
            case 7: it = IT_CMP; break; // 81/7: cmp r/m 32/64, imm32
            default: assert(0);
            }
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            o2.type = OT_Imm32;
            o2.val = *(uint32_t*)(fp + off);
            off += 4;
            addBinaryOp(c, a, (uint64_t)(fp + off), it, vt, &o1, &o2);
            break;

        case 0x83:
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, 0, &digit);
            // add/or/... r/m and sign-extended imm8
            switch(digit) {
            case 0: it = IT_ADD; break; // 83/0: add r/m 32/64, imm8
            case 1: it = IT_OR;  break; // 83/1: or  r/m 32/64, imm8
            case 2: it = IT_ADC; break; // 83/2: adc r/m 32/64, imm8
            case 3: it = IT_SBB; break; // 83/3: sbb r/m 32/64, imm8
            case 4: it = IT_AND; break; // 83/4: and r/m 32/64, imm8
            case 5: it = IT_SUB; break; // 83/5: sub r/m 32/64, imm8
            case 6: it = IT_XOR; break; // 83/6: xor r/m 32/64, imm8
            case 7: it = IT_CMP; break; // 83/7: cmp r/m 32/64, imm8
            default: assert(0);
            }
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            o2.type = OT_Imm8;
            o2.val = (uint8_t) (*(int8_t*)(fp + off));
            off += 1;
            addBinaryOp(c, a, (uint64_t)(fp + off), it, vt, &o1, &o2);
            break;

        case 0x85:
            // test r/m,r 32/64 (dst: r/m, src: r)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, &o2, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_TEST, vt, &o1, &o2);
            break;

        case 0x89:
            // mov r/m,r 32/64 (dst: r/m, src: r)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, &o2, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_MOV, vt, &o1, &o2);
            break;

        case 0x8B:
            // mov r,r/m 32/64 (dst: r, src: r/m)
            vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_MOV, vt, &o1, &o2);
            break;

        case 0x8D:
            // lea r32/64,m
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o2, &o1, 0);
            assert(opIsInd(&o2)); // TODO: bad code error
            addBinaryOp(c, a, (uint64_t)(fp + off),
                        IT_LEA, VT_None, &o1, &o2);
            break;

        case 0x90:
            // nop
            addSimple(c, a, (uint64_t)(fp + off), IT_NOP);
            break;

        case 0x98:
            // cltq (Intel: cdqe - sign-extend eax to rax)
            addSimpleVType(c, a, (uint64_t)(fp + off), IT_CLTQ,
                           hasRex && (rex & REX_MASK_W) ? VT_64 : VT_32);
            break;

        case 0xB8: case 0xB9: case 0xBA: case 0xBB:
        case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            // MOV r32/64,imm32/64
            o1.reg = Reg_AX + (opc - 0xB8);
            if (rex & REX_MASK_R) o1.reg += 8;
            if (rex & REX_MASK_W) {
                vt = VT_64;
                o1.type = OT_Reg64;
                o2.type = OT_Imm64;
                o2.val = *(uint64_t*)(fp + off);
                off += 8;
            }
            else {
                vt = VT_32;
                o1.type = OT_Reg32;
                o2.type = OT_Imm32;
                o2.val = *(uint32_t*)(fp + off);
                off += 4;
            }
            addBinaryOp(c, a, (uint64_t)(fp + off), IT_MOV, vt, &o1, &o2);
            break;

        case 0xC1:
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, 0, &digit);
            switch(digit) {
            case 4:
                // shl r/m 32/64,imm8 (MI) (= sal)
                o2.type = OT_Imm8;
                o2.val = *(uint8_t*)(fp + off);
                off += 1;
                addBinaryOp(c, a, (uint64_t)(fp + off),
                            IT_SHL, VT_None, &o1, &o2);
                break;

            default:
                addSimple(c, a, (uint64_t)(fp + off), IT_Invalid);
                break;
            }
            break;

        case 0xC3:
            // ret
            addSimple(c, a, (uint64_t)(fp + off), IT_RET);
            exitLoop = True;
            break;

        case 0xC7:
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, 0, &digit);
            switch(digit) {
            case 0:
                // mov r/m 32/64, imm32
                vt = (rex & REX_MASK_W) ? VT_64 : VT_32;
                o2.type = OT_Imm32;
                o2.val = *(uint32_t*)(fp + off);
                off += 4;
                addBinaryOp(c, a, (uint64_t)(fp + off), IT_MOV, vt, &o1, &o2);
                break;

            default:
                addSimple(c, a, (uint64_t)(fp + off), IT_Invalid);
                break;
            }
            break;

        case 0xC9:
            // leave ( = mov rbp,rsp + pop rbp)
            addSimple(c, a, (uint64_t)(fp + off), IT_LEAVE);
            break;

        case 0xE8:
            // call rel32
            o1.type = OT_Imm64;
            o1.val = (uint64_t) (fp + off + 4 + *(int32_t*)(fp + off));
            off += 4;
            addUnaryOp(c, a, (uint64_t)(fp + off), IT_CALL, &o1);
            exitLoop = True;
            break;

        case 0xE9:
            // jmp rel32: relative, displacement relative to next instruction
            o1.type = OT_Imm64;
            o1.val = (uint64_t) (fp + off + 4 + *(int32_t*)(fp + off));
            off += 4;
            addUnaryOp(c, a, (uint64_t)(fp + off), IT_JMP, &o1);
            exitLoop = True;
            break;

        case 0xEB:
            // jmp rel8: relative, displacement relative to next instruction
            o1.type = OT_Imm64;
            o1.val = (uint64_t) (fp + off + 1 + *(int8_t*)(fp + off));
            off += 1;
            addUnaryOp(c, a, (uint64_t)(fp + off), IT_JMP, &o1);
            exitLoop = True;
            break;

        case 0xF7:
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, 0, &digit);
            switch(digit) {
            case 3:
                // neg r/m 32/64
                addUnaryOp(c, a, (uint64_t)(fp + off), IT_NEG, &o1);
                break;

            default:
                addSimple(c, a, (uint64_t)(fp + off), IT_Invalid);
                break;
            }
            break;

        case 0xFF:
            off += parseModRM(fp+off, rex, segOv, 0, 0, &o1, 0, &digit);
            switch(digit) {
            case 0:
                // inc r/m 32/64
                addUnaryOp(c, a, (uint64_t)(fp + off), IT_INC, &o1);
                break;

            case 1:
                // dec r/m 32/64
                addUnaryOp(c, a, (uint64_t)(fp + off), IT_DEC, &o1);
                break;

            case 2:
                // call r/m64
                addUnaryOp(c, a, (uint64_t)(fp + off), IT_CALL, &o1);
                exitLoop = True;
                break;

            case 4:
                // jmp* r/m64: absolute indirect
                assert(rex == 0);
                opOverwriteType(&o1, VT_64);
                addUnaryOp(c, a, (uint64_t)(fp + off), IT_JMPI, &o1);
                exitLoop = True;
                break;

            default:
                addSimple(c, a, (uint64_t)(fp + off), IT_Invalid);
                break;
            }
            break;

        default:
            addSimple(c, a, (uint64_t)(fp + off), IT_Invalid);
            break;
        }
        hasRex = False;
        rex = 0;
        segOv = OSO_None;
        hasF2 = False;
        hasF3 = False;
        has66 = False;
        has2E = False;
    }

    assert(dbb->addr == dbb->instr->addr);
    dbb->count = c->decInstrCount - old_icount;
    dbb->size = off;

    if (c->showDecoding)
        brew_print_decoded(dbb);

    return dbb;
}




/*------------------------------------------------------------*/
/* x86_64 printer
 */

char* regName(Reg r, OpType t)
{
    switch(t) {
    case OT_Reg32:
        switch(r) {
        case Reg_AX: return "eax";
        case Reg_BX: return "ebx";
        case Reg_CX: return "ecx";
        case Reg_DX: return "edx";
        case Reg_DI: return "edi";
        case Reg_SI: return "esi";
        case Reg_BP: return "ebp";
        case Reg_SP: return "esp";
        case Reg_8:  return "r8";
        case Reg_9:  return "r9";
        case Reg_10: return "r10";
        case Reg_11: return "r11";
        case Reg_12: return "r12";
        case Reg_13: return "r13";
        case Reg_14: return "r14";
        case Reg_15: return "r15";
        case Reg_IP: return "eip";
        }
        break;

    case OT_Reg64:
        switch(r) {
        case Reg_AX: return "rax";
        case Reg_BX: return "rbx";
        case Reg_CX: return "rcx";
        case Reg_DX: return "rdx";
        case Reg_DI: return "rdi";
        case Reg_SI: return "rsi";
        case Reg_BP: return "rbp";
        case Reg_SP: return "rsp";
        case Reg_8:  return "r8";
        case Reg_9:  return "r9";
        case Reg_10: return "r10";
        case Reg_11: return "r11";
        case Reg_12: return "r12";
        case Reg_13: return "r13";
        case Reg_14: return "r14";
        case Reg_15: return "r15";
        case Reg_IP: return "rip";

        case Reg_X0:  return "mm0";
        case Reg_X1:  return "mm1";
        case Reg_X2:  return "mm2";
        case Reg_X3:  return "mm3";
        case Reg_X4:  return "mm4";
        case Reg_X5:  return "mm5";
        case Reg_X6:  return "mm6";
        case Reg_X7:  return "mm7";
        case Reg_X8:  return "mm8";
        case Reg_X9:  return "mm9";
        case Reg_X10: return "mm10";
        case Reg_X11: return "mm11";
        case Reg_X12: return "mm12";
        case Reg_X13: return "mm13";
        case Reg_X14: return "mm14";
        case Reg_X15: return "mm15";
        }
        break;

    case OT_Reg128:
        switch(r) {
        case Reg_X0:  return "xmm0";
        case Reg_X1:  return "xmm1";
        case Reg_X2:  return "xmm2";
        case Reg_X3:  return "xmm3";
        case Reg_X4:  return "xmm4";
        case Reg_X5:  return "xmm5";
        case Reg_X6:  return "xmm6";
        case Reg_X7:  return "xmm7";
        case Reg_X8:  return "xmm8";
        case Reg_X9:  return "xmm9";
        case Reg_X10: return "xmm10";
        case Reg_X11: return "xmm11";
        case Reg_X12: return "xmm12";
        case Reg_X13: return "xmm13";
        case Reg_X14: return "xmm14";
        case Reg_X15: return "xmm15";
        }
        break;

    case OT_Reg256:
        switch(r) {
        case Reg_X0:  return "ymm0";
        case Reg_X1:  return "ymm1";
        case Reg_X2:  return "ymm2";
        case Reg_X3:  return "ymm3";
        case Reg_X4:  return "ymm4";
        case Reg_X5:  return "ymm5";
        case Reg_X6:  return "ymm6";
        case Reg_X7:  return "ymm7";
        case Reg_X8:  return "ymm8";
        case Reg_X9:  return "ymm9";
        case Reg_X10: return "ymm10";
        case Reg_X11: return "ymm11";
        case Reg_X12: return "ymm12";
        case Reg_X13: return "ymm13";
        case Reg_X14: return "ymm14";
        case Reg_X15: return "ymm15";
        }
        break;

    }
    assert(0);
}

char* op2string(Operand* o, ValType t)
{
    static char buf[30];
    int off = 0;
    uint64_t val;

    switch(o->type) {
    case OT_Reg32:
    case OT_Reg64:
    case OT_Reg128:
    case OT_Reg256:
        sprintf(buf, "%%%s", regName(o->reg, o->type));
        break;

    case OT_Imm8:
        val = o->val;
        assert(val < (1l<<8));
        switch(t) {
        case VT_None:
        case VT_8:
            break;
        case VT_32:
            if (val > 0x7F) val += 0xFFFFFF00;
            break;
        case VT_64:
            if (val > 0x7F) val += 0xFFFFFFFFFFFFFF00;
            break;
        default: assert(0);
        }
        sprintf(buf, "$0x%lx", val);
        break;

    case OT_Imm16:
        val = o->val;
        assert(val < (1l<<16));
        switch(t) {
        case VT_32:
            if (val > 0x7FFF) val += 0xFFFF0000;
            break;
        case VT_64:
            if (val > 0x7FFF) val += 0xFFFFFFFFFFFF0000;
            break;
        case VT_None:
            break;
        default: assert(0);
        }
        sprintf(buf, "$0x%lx", val);
        break;

    case OT_Imm32:
        val = o->val;
        assert(val < (1l<<32));
        switch(t) {
        case VT_None:
        case VT_32:
            break;
        case VT_64:
            if (val > 0x7FFFFFFF) val += 0xFFFFFFFF00000000;
            break;
        default: assert(0);
        }
        sprintf(buf, "$0x%lx", val);
        break;

    case OT_Imm64:
        sprintf(buf, "$0x%lx", o->val);
        break;

    case OT_Ind8:
    case OT_Ind16:
    case OT_Ind32:
    case OT_Ind64:
    case OT_Ind128:
    case OT_Ind256:
        off = 0;
        switch(o->seg) {
        case OSO_None: break;
        case OSO_UseFS: off += sprintf(buf+off, "fs:"); break;
        case OSO_UseGS: off += sprintf(buf+off, "gs:"); break;
        }
        if (o->val != 0) {
            if (o->val & (1l<<63))
                off += sprintf(buf+off, "-0x%lx", (~ o->val)+1);
            else
                off += sprintf(buf+off, "0x%lx", o->val);
        }
        if ((o->scale == 0) || (o->ireg == Reg_None)) {
            if (o->reg != Reg_None)
                sprintf(buf+off,"(%%%s)", regName(o->reg, OT_Reg64));
        }
        else {
            char* ri = regName(o->ireg, OT_Reg64);
            if (o->reg == Reg_None) {
                sprintf(buf+off,"(,%%%s,%d)", ri, o->scale);
            }
            else
                sprintf(buf+off,"(%%%s,%%%s,%d)",
                        regName(o->reg, OT_Reg64), ri, o->scale);
        }
        break;
    default: assert(0);
    }
    return buf;
}

char* instrName(InstrType it, int* pOpCount)
{
    char* n;
    int opCount = 0;

    switch(it) {
    case IT_HINT_CALL: n = "H-call"; break;
    case IT_HINT_RET:  n = "H-ret"; break;

    case IT_NOP:     n = "nop"; break;
    case IT_RET:     n = "ret"; break;
    case IT_LEAVE:   n = "leave"; break;
    case IT_CLTQ:    n = "clt"; break;
    case IT_PUSH:    n = "push";    opCount = 1; break;
    case IT_POP:     n = "pop";     opCount = 1; break;
    case IT_CALL:    n = "call";    opCount = 1; break;
    case IT_JMP:     n = "jmp";     opCount = 1; break;
    case IT_JMPI:    n = "jmp*";    opCount = 1; break;
    case IT_JE:      n = "je";      opCount = 1; break;
    case IT_JNE:     n = "jne";     opCount = 1; break;
    case IT_JLE:     n = "jle";     opCount = 1; break;
    case IT_JG:      n = "jg";      opCount = 1; break;
    case IT_JL:      n = "jl";      opCount = 1; break;
    case IT_JGE:     n = "jge";     opCount = 1; break;
    case IT_JP:      n = "jp";      opCount = 1; break;
    case IT_MOV:     n = "mov";     opCount = 2; break;
    case IT_MOVSX:   n = "movsx";   opCount = 2; break;
    case IT_MOVZBL:  n = "movzbl";  opCount = 2; break;
    case IT_NEG:     n = "neg";     opCount = 1; break;
    case IT_INC:     n = "inc";     opCount = 1; break;
    case IT_DEC:     n = "dec";     opCount = 1; break;
    case IT_ADD:     n = "add";     opCount = 2; break;
    case IT_ADC:     n = "adc";     opCount = 2; break;
    case IT_SUB:     n = "sub";     opCount = 2; break;
    case IT_SBB:     n = "sbb";     opCount = 2; break;
    case IT_IMUL:    n = "imul";    opCount = 2; break;
    case IT_AND:     n = "and";     opCount = 2; break;
    case IT_OR:      n = "or";      opCount = 2; break;
    case IT_XOR:     n = "xor";     opCount = 2; break;
    case IT_SHL:     n = "shl";     opCount = 2; break;
    case IT_SHR:     n = "shr";     opCount = 2; break;
    case IT_LEA:     n = "lea";     opCount = 2; break;
    case IT_CMP:     n = "cmp";     opCount = 2; break;
    case IT_TEST:    n = "test";    opCount = 2; break;
    case IT_BSF:     n = "bsf";     opCount = 2; break;
    case IT_PXOR:    n = "pxor";    opCount = 2; break;
    case IT_MOVSD:   n = "movsd";   opCount = 2; break;
    case IT_UCOMISD: n = "ucomisd"; opCount = 2; break;
    case IT_MULSD:   n = "mulsd";   opCount = 2; break;
    case IT_ADDSD:   n = "addsd";   opCount = 2; break;
    case IT_SUBSD:   n = "subsd";   opCount = 2; break;
    case IT_MOVDQU:  n = "movdqu";  opCount = 2; break;
    case IT_PCMPEQB: n = "pcmpeqb"; opCount = 2; break;
    case IT_PMINUB:  n = "pminub";  opCount = 2; break;
    case IT_PMOVMSKB:n = "pmovmskb";opCount = 2; break;
    default: n = "<Invalid>"; break;
    }

    if (pOpCount) *pOpCount = opCount;
    return n;
}

char* instr2string(Instr* instr, int align)
{
    static char buf[100];
    char* n;
    int oc = 0, off = 0;

    n = instrName(instr->type, &oc);

    if (align)
        off += sprintf(buf, "%-7s", n);
    else
        off += sprintf(buf, "%s", n);

    // add value type if given
    Bool appendVType = (instr->vtype != VT_None);
    if ((instr->form == OF_2) &&
        (opIsGPReg(&(instr->dst)) ||
         opIsGPReg(&(instr->src)))) appendVType = False;
    if (appendVType) {
        char vt = ' ';
        switch(instr->vtype) {
        case VT_8:  vt = 'b'; break;
        case VT_16: vt = 'w'; break;
        case VT_32: vt = 'l'; break;
        case VT_64: vt = 'q'; break;
        }
        if (vt != ' ') {
            int nlen = strlen(n);
            if (buf[nlen] == ' ') buf[nlen] = vt;
            else {
                buf[nlen] = vt;
                buf[nlen+1] = 0;
                off++;
            }
        }
    }

    switch(instr->form) {
    case OF_0:
        assert(instr->dst.type == OT_None);
        assert(instr->src.type == OT_None);
        assert(instr->src2.type == OT_None);
        break;

    case OF_1:
        assert(instr->dst.type != OT_None);
        assert(instr->src.type == OT_None);
        assert(instr->src2.type == OT_None);
        off += sprintf(buf+off, " %s", op2string(&(instr->dst), instr->vtype));
        break;

    case OF_2:
        assert(instr->dst.type != OT_None);
        assert(instr->src.type != OT_None);
        assert(instr->src2.type == OT_None);
        off += sprintf(buf+off, " %s", op2string(&(instr->src), instr->vtype));
        off += sprintf(buf+off, ",%s", op2string(&(instr->dst), instr->vtype));
        break;

    case OF_3:
        assert(instr->dst.type != OT_None);
        assert(instr->src.type != OT_None);
        assert(instr->src2.type != OT_None);
        off += sprintf(buf+off, " %s", op2string(&(instr->src2), instr->vtype));
        off += sprintf(buf+off, ",%s", op2string(&(instr->src), instr->vtype));
        off += sprintf(buf+off, ",%s", op2string(&(instr->dst), instr->vtype));
        break;

    default: assert(0);
    }

    return buf;
}

char* bytes2string(Instr* instr, int start, int count)
{
    static char buf[100];
    int off = 0, i, j;
    for(i = start, j=0; (i < instr->len) && (j<count); i++, j++) {
        uint8_t b = ((uint8_t*) instr->addr)[i];
        off += sprintf(buf+off, " %02x", b);
    }
    for(;j<count;j++)
        off += sprintf(buf+off, "   ");
    return (off == 0) ? "" : buf;
}

void brew_print_decoded(DBB* bb)
{
    int i;
    for(i = 0; i < bb->count; i++) {
        Instr* instr = bb->instr + i;
        printf("  %p: %s  %s\n", (void*) instr->addr,
               bytes2string(instr, 0, 7), instr2string(instr, 1));
        if (instr->len > 7)
            printf("  %p: %s\n", (void*) instr->addr + 7,
                   bytes2string(instr, 7, 7));
        if (instr->len > 14)
            printf("  %p: %s\n", (void*) instr->addr + 14,
                   bytes2string(instr, 14, 7));
    }
}

void printDecodedBBs(Rewriter* c)
{
    int i;
    for(i=0; i< c->decBBCount; i++) {
        printf("BB %lx (%d instructions):\n", c->decBB[i].addr, c->decBB[i].count);
        brew_print_decoded(c->decBB + i);
    }
}

void brew_decode_print(Rewriter* c, uint64_t f, int count)
{
    DBB* dbb;
    int decoded = 0;

    c->decBBCount = 0;
    while(decoded < count) {
        dbb = brew_decode(c, f + decoded);
        decoded += dbb->size;
    }
    printDecodedBBs(c);
}


/*------------------------------------------------------------*/
/* x86_64 code generation
 */

// helpers for operand encodings

// return 0 - 15 for RAX - R15
int GPRegEncoding(Reg r)
{
    assert((r >= Reg_AX) && (r <= Reg_15));
    return r - Reg_AX;
}

// return 0 - 15 for XMM0 - XMM15
int VRegEncoding(Reg r)
{
    assert((r >= Reg_X0) && (r <= Reg_X15));
    return r - Reg_X0;
}

// returns static buffer with requested operand encoding
uint8_t* calcModRMDigit(Operand* o1, int digit, int* prex, int* plen)
{
    static uint8_t buf[10];
    int modrm, r1;
    int o = 0;
    ValType vt;

    assert((digit>=0) && (digit<8));
    assert(opIsReg(o1) || opIsInd(o1));

    vt = opValType(o1);
    if (vt == VT_64) *prex |= REX_MASK_W;

    modrm = (digit & 7) << 3;

    if (opIsReg(o1)) {
        // r,r: mod 3
        modrm |= 192;
        if (opIsGPReg(o1))
            r1 = GPRegEncoding(o1->reg);
        else if (opIsVReg(o1))
            r1 = VRegEncoding(o1->reg);
        else
            assert(0);
        if (r1 & 8) *prex |= REX_MASK_B;
        modrm |= (r1 & 7);
        buf[o++] = modrm;
    }
    else {
        int useDisp8 = 0, useDisp32 = 0, useSIB = 0;
        int sib = 0;
        int64_t v = (int64_t) o1->val;
        if (v != 0) {
            if ((v >= -128) && (v<128)) useDisp8 = 1;
            else if ((v >= -((int64_t)1<<31)) &&
                     (v < ((int64_t)1<<31))) useDisp32 = 1;
            else assert(0);

            if (useDisp8)
                modrm |= 64;
            if (useDisp32)
                modrm |= 128;
        }

        if ((o1->scale == 0) && (o1->reg != Reg_SP)) {
            // no SIB needed (reg not sp which requires SIB)
            if (o1->reg == Reg_None) {
                useDisp32 = 1; // encoding needs disp32
                useDisp8 = 0;
                modrm &= 63; // mod needs to be 00
                useSIB = 1;
                sib = (4 << 3) + 5; // index 4 (= none) + base 5 (= none)
            }
            else {
                if (o1->reg == Reg_IP) {
                    // should not happen, we converted RIP-rel to absolute
                    assert(0);
                    // RIP relative
                    r1 = 5;
                    modrm &= 63;
                    useDisp32 = 1;
                }
                else {
                    r1 = GPRegEncoding(o1->reg);

                    if ((r1 == 5) && (v==0)) {
                        // encoding for rbp without displacement is reused
                        // for RIP-relative addressing!
                        // we need to enforce +disp8 with disp8 = 0
                        // (see SDM, table 2-5 in 2.2.1.2)
                        useDisp8 = 1;
                        assert(modrm < 64); // check that mod = 0
                        modrm |= 64;
                    }
                }
                if (r1 & 8) *prex |= REX_MASK_B;
                modrm |= (r1 & 7);
            }
        }
        else {
            // SIB
            int ri, rb;
            useSIB = 1;
            if      (o1->scale == 2) sib |= 64;
            else if (o1->scale == 4) sib |= 128;
            else if (o1->scale == 8) sib |= 192;
            else
                assert((o1->scale == 0) || (o1->scale == 1));

            if ((o1->scale == 0) || (o1->ireg == Reg_None)) {
                // no index register: uses index 4 (usually SP, not allowed)
                sib |= (4 << 3);
            }
            else {
                ri = GPRegEncoding(o1->ireg);
                // offset 4 not allowed here, used for "no scaling"
                assert(ri != 4);
                if (ri & 8) *prex |= REX_MASK_X;
                sib |= (ri & 7) <<3;
            }

            if (o1->reg == Reg_None) {
                // encoding requires disp32 with mod = 00 / base 5 = none
                useDisp32 = 1;
                useDisp8 = 0;
                modrm &= 63;
                sib |= 5;
            }
            else {
                if (o1->reg == Reg_BP) {
                    // cannot use mod == 00
                    if ((modrm & 192) == 0) {
                        modrm |= 64;
                        useDisp8 = 1;
                    }
                }
                rb = GPRegEncoding(o1->reg);
                if (rb & 8) *prex |= REX_MASK_B;
                sib |= (rb & 7);
            }
        }

        if (useSIB)
            modrm |= 4; // signal SIB in modrm
        buf[o++] = modrm;
        if (useSIB)
            buf[o++] = sib;
        if (useDisp8)
            buf[o++] = (int8_t) v;
        if (useDisp32) {
            *(int32_t*)(buf+o) = (int32_t) v;
            o += 4;
        }
    }

    *plen = o;
    return buf;
}

uint8_t* calcModRM(Operand* o1, Operand* o2, int* prex, int* plen)
{
    int r2; // register offset encoding for operand 2

    assert(opValType(o1) == opValType(o2));

    if (opIsGPReg(o2)) {
        assert(opIsReg(o1) || opIsInd(o1));
        r2 = GPRegEncoding(o2->reg);
    }
    else if (opIsVReg(o2)) {
        assert(opIsVReg(o1) || opIsInd(o1));
        r2 = VRegEncoding(o2->reg);
    }
    else assert(0);

    if (r2 & 8) *prex |= REX_MASK_R;
    return calcModRMDigit(o1, r2 & 7, prex, plen);
}


// Generate instruction with operand encoding RM (o1: r/m, o2: r)
// into buf, up to 2 opcodes (2 if opc2 >=0).
// If result type (vt) is explicitly specified as "VT_Implicit", do not
// automatically generate REX prefix depending on operand types.
// Returns byte length of generated instruction.
int genModRM(uint8_t* buf, int opc, int opc2,
             Operand* o1, Operand* o2, ValType vt)
{
    int rex = 0, len = 0;
    int o = 0;
    uint8_t* rmBuf;

    rmBuf = calcModRM(o1, o2, &rex, &len);
    if (vt == VT_Implicit) rex &= ~REX_MASK_W;
    if (rex)
        buf[o++] = 0x40 | rex;
    buf[o++] = (uint8_t) opc;
    if (opc2 >=0)
        buf[o++] = (uint8_t) opc2;
    while(len>0) {
        buf[o++] = *rmBuf++;
        len--;
    }
    return o;
}

// Operand o1: r/m
int genDigitRM(uint8_t* buf, int opc, int digit, Operand* o1)
{
    int rex = 0, len = 0;
    int o = 0;
    uint8_t* rmBuf;

    rmBuf = calcModRMDigit(o1, digit, &rex, &len);
    if (rex)
        buf[o++] = 0x40 | rex;
    buf[o++] = (uint8_t) opc;
    while(len>0) {
        buf[o++] = *rmBuf++;
        len--;
    }
    return o;
}

// Operand o1: r/m, o2: r, o3: imm
int genModRMI(uint8_t* buf, int opc, int opc2,
              Operand* o1, Operand* o2, Operand* o3)
{
    int rex = 0, len = 0;
    int o = 0;
    uint8_t* rmBuf;

    rmBuf = calcModRM(o1, o2, &rex, &len);
    if (rex)
        buf[o++] = 0x40 | rex;
    buf[o++] = (uint8_t) opc;
    if (opc2 >=0)
        buf[o++] = (uint8_t) opc2;
    while(len>0) {
        buf[o++] = *rmBuf++;
        len--;
    }
    assert(opIsImm(o3));
    switch(opValType(o3)) {
    case VT_8:
        buf[o++] = (uint8_t) o3->val;
        break;
    case VT_32:
        *(uint32_t*)(buf+o) = (uint32_t) o3->val;
        o += 4;
        break;
    default: assert(0);
    }

    return o;
}

// Operand o1: r/m, o2: imm
int genDigitMI(uint8_t* buf, int opc, int digit, Operand* o1, Operand* o2)
{
    int rex = 0, len = 0;
    int o = 0;
    uint8_t* rmBuf;

    assert(opIsImm(o2));
    rmBuf = calcModRMDigit(o1, digit, &rex, &len);
    if (rex)
        buf[o++] = 0x40 | rex;
    buf[o++] = (uint8_t) opc;
    while(len>0) {
        buf[o++] = *rmBuf++;
        len--;
    }

    // immediate
    switch(o2->type) {
    case OT_Imm8:
        *(uint8_t*)(buf + o) = (uint8_t) o2->val;
        o += 1;
        break;

    case OT_Imm32:
        *(uint32_t*)(buf + o) = (uint32_t) o2->val;
        o += 4;
        break;

    default: assert(0);
    }

    return o;
}

// Operand o1: r (gets part of opcode), o2: imm
int genOI(uint8_t* buf, int opc, Operand* o1, Operand* o2)
{
    int rex = 0;
    int o = 0, r;

    assert(opIsReg(o1));
    assert(opIsImm(o2));

    r = GPRegEncoding(o1->reg);
    if (r & 8) rex |= REX_MASK_B;
    if (opValType(o1) == VT_64) rex |= REX_MASK_W;

    if (rex)
        buf[o++] = 0x40 | rex;
    buf[o++] = (uint8_t) (opc + (r & 7));

    // immediate
    switch(o2->type) {
    case OT_Imm8:
        *(uint8_t*)(buf + o) = (uint8_t) o2->val;
        o += 1;
        break;

    case OT_Imm32:
        *(uint32_t*)(buf + o) = (uint32_t) o2->val;
        o += 4;
        break;

    case OT_Imm64:
        *(uint64_t*)(buf + o) = o2->val;
        o += 8;
        break;

    default: assert(0);
    }

    return o;
}

// if imm64 and value fitting into imm32, return imm32 version
// otherwise, or if operand is not imm, just return the original
Operand* reduceImm64to32(Operand* o)
{
    static Operand newOp;

    if (o->type == OT_Imm64) {
        // reduction possible if signed 64bit fits into signed 32bit
        int64_t v = (int64_t) o->val;
        if ((v > -(1l << 31)) && (v < (1l << 31))) {
            newOp.type = OT_Imm32;
            newOp.val = (uint32_t) (int32_t) v;
            return &newOp;
        }
    }
    return o;
}

Operand* reduceImm32to8(Operand* o)
{
    static Operand newOp;

    if (o->type == OT_Imm32) {
        // reduction possible if signed 32bit fits into signed 8bit
        int32_t v = (int32_t) o->val;
        if ((v > -(1<<7)) && (v < (1<<7))) {
            newOp.type = OT_Imm8;
            newOp.val = (uint8_t) (int8_t) v;
            return &newOp;
        }
    }
    return o;
}


// machine code generators for instruction types
//
// 1st par is buffer to write to, with at least 15 bytes space.
// Return number of bytes written

int genRet(uint8_t* buf)
{
    buf[0] = 0xc3;
    return 1;
}

int genPush(uint8_t* buf, Operand* o)
{
    assert(o->type == OT_Reg64);
    if ((o->reg >= Reg_AX) && (o->reg <= Reg_DI)) {
        buf[0] = 0x50 + (o->reg - Reg_AX);
        return 1;
    }
    else if ((o->reg >= Reg_8) && (o->reg <= Reg_15)) {
        buf[0] = 0x41; // REX with MASK_B
        buf[1] = 0x50 + (o->reg - Reg_8);
        return 2;
    }
    assert(0);
}

int genPop(uint8_t* buf, Operand* o)
{
    assert(o->type == OT_Reg64);
    if ((o->reg >= Reg_AX) && (o->reg <= Reg_DI)) {
        buf[0] = 0x58 + (o->reg - Reg_AX);
        return 1;
    }
    else if ((o->reg >= Reg_8) && (o->reg <= Reg_15)) {
        buf[0] = 0x41; // REX with MASK_B
        buf[1] = 0x58 + (o->reg - Reg_8);
        return 2;
    }
    assert(0);
}

int genDec(uint8_t* buf, Operand* dst)
{
    switch(dst->type) {
    case OT_Ind32:
    case OT_Ind64:
    case OT_Reg32:
    case OT_Reg64:
      // use 'dec r/m 32/64' (0xFF/1)
      return genDigitRM(buf, 0xFF, 1, dst);

    default: assert(0);
    }
    return 0;
}

int genInc(uint8_t* buf, Operand* dst)
{
    switch(dst->type) {
    case OT_Ind32:
    case OT_Ind64:
    case OT_Reg32:
    case OT_Reg64:
      // use 'inc r/m 32/64' (0xFF/0)
      return genDigitRM(buf, 0xFF, 0, dst);

    default: assert(0);
    }
    return 0;
}

int genMov(uint8_t* buf, Operand* src, Operand* dst)
{
    src = reduceImm64to32(src);

    switch(dst->type) {
    case OT_Ind32:
    case OT_Ind64:
        // dst memory
        switch(src->type) {
        case OT_Reg32:
        case OT_Reg64:
            assert(opValType(src) == opValType(dst));
            // use 'mov r/m,r 32/64' (0x89 MR)
            return genModRM(buf, 0x89, -1, dst, src, VT_None);

        case OT_Imm32:
            // use 'mov r/m 32/64, imm32' (0xC7/0 MI)
            return genDigitMI(buf, 0xC7, 0, dst, src);

        default: assert(0);
        }
        break;

    case OT_Reg32:
    case OT_Reg64:
        // dst reg
        switch(src->type) {
        case OT_Ind32:
        case OT_Ind64:
        case OT_Reg32:
        case OT_Reg64:
            if (opValType(src) == opValType(dst)) {
                // use 'mov r,r/m 32/64' (0x8B RM)
                return genModRM(buf, 0x8B, -1, src, dst, VT_None);
            }
            else if ((opValType(src) == VT_32) &&
                     (opValType(dst) == VT_64)) {
                src->type = (src->type == OT_Reg32) ? OT_Reg64 : OT_Ind64;
                // use 'movsx r64 ,r/m 32' (0x63)
                return genModRM(buf, 0x63, -1, src, dst, VT_None);
            }
            break;

        case OT_Imm32:
            if (src->val == 0) {
                // setting to 0: use 'xor r/m,r 32/64' (0x31 MR)
                return genModRM(buf, 0x31, -1, dst, dst, VT_None);
            }
            // use 'mov r/m 32/64, imm32' (0xC7/0)
            return genDigitMI(buf, 0xC7, 0, dst, src);

        case OT_Imm64: {
            if (src->val == 0) {
                // setting to 0: use 'xor r/m,r 32/64' (0x31 MR)
                return genModRM(buf, 0x31, -1, dst, dst, VT_None);
            }
            // use 'mov r64,imm64' (REX.W + 0xB8)
            return genOI(buf, 0xB8, dst, src);
        }

        default: assert(0);
        }
        break;

    default: assert(0);
    }
    return 0;
}


int genAdd(uint8_t* buf, Operand* src, Operand* dst)
{
    // if src is imm, try to reduce width
    src = reduceImm64to32(src);
    src = reduceImm32to8(src);

    switch(src->type) {
    case OT_Reg32:
    case OT_Reg64:
        assert(opValType(src) == opValType(dst));
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'add r/m,r 32/64' (0x01 MR)
            return genModRM(buf, 0x01, -1, dst, src, VT_None);

        default: assert(0);
        }
        break;

    case OT_Ind32:
    case OT_Ind64:
        assert(opValType(src) == opValType(dst));
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
            // use 'add r,r/m 32/64' (0x03 RM)
            return genModRM(buf, 0x03, -1, src, dst, VT_None);

        default: assert(0);
        }
        break;

    case OT_Imm8:
        // src imm8
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'add r/m 32/64, imm8' (0x83/0 MI)
            return genDigitMI(buf, 0x83, 0, dst, src);

        default: assert(0);
        }
        break;

    case OT_Imm32:
        // src imm
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'add r/m 32/64, imm32' (0x81/0 MI)
            return genDigitMI(buf, 0x81, 0, dst, src);

        default: assert(0);
        }
        break;

    default: assert(0);
    }
    return 0;
}

int genSub(uint8_t* buf, Operand* src, Operand* dst)
{
    // if src is imm, try to reduce width
    src = reduceImm64to32(src);
    src = reduceImm32to8(src);

    switch(src->type) {
    case OT_Reg32:
    case OT_Reg64:
        assert(opValType(src) == opValType(dst));
        // src reg
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'sub r/m,r 32/64' (0x29 MR)
            return genModRM(buf, 0x29, -1, dst, src, VT_None);

        default: assert(0);
        }
        break;

    case OT_Ind32:
    case OT_Ind64:
        assert(opValType(src) == opValType(dst));
        // src mem
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
            // use 'sub r,r/m 32/64' (0x2B RM)
            return genModRM(buf, 0x2B, -1, src, dst, VT_None);

        default: assert(0);
        }
        break;

    case OT_Imm8:
        // src imm32
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'sub r/m 32/64, imm8' (0x83/5 MI)
            return genDigitMI(buf, 0x83, 5, dst, src);

        default: assert(0);
        }
        break;

    case OT_Imm32:
        // src imm32
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'sub r/m 32/64, imm32' (0x81/5 MI)
            return genDigitMI(buf, 0x81, 5, dst, src);

        default: assert(0);
        }
        break;

    default: assert(0);
    }
    return 0;
}

int genIMul(uint8_t* buf, Operand* src, Operand* dst)
{
    // if src is imm, try to reduce width
    src = reduceImm32to8(src);

    switch(src->type) {
    case OT_Reg32:
    case OT_Ind32:
    case OT_Reg64:
    case OT_Ind64:
        assert(opValType(src) == opValType(dst));
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
            // use 'imul r,r/m 32/64' (0x0F 0xAF RM)
            return genModRM(buf, 0x0F, 0xAF, src, dst, VT_None);

        default: assert(0);
        }
        break;

    case OT_Imm8:
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
            // use 'imul r,r/m 32/64,imm8' (0x6B/r RMI)
            return genModRMI(buf, 0x6B, -1, dst, dst, src);

        default: assert(0);
        }
        break;

    case OT_Imm32:
        assert(opValType(src) == opValType(dst));
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
            // use 'imul r,r/m 32/64,imm32' (0x69/r RMI)
            return genModRMI(buf, 0x69, -1, dst, dst, src);

        default: assert(0);
        }
        break;

    default: assert(0);
    }
    return 0;
}

int genXor(uint8_t* buf, Operand* src, Operand* dst)
{
    switch(src->type) {
    // src reg
    case OT_Reg32:
    case OT_Reg64:
        assert(opValType(src) == opValType(dst));
        switch(dst->type) {
        case OT_Reg32:
        case OT_Ind32:
        case OT_Reg64:
        case OT_Ind64:
            // use 'xor r/m,r 32/64' (0x31 MR)
            return genModRM(buf, 0x31, -1, dst, src, VT_None);

        default: assert(0);
        }
        break;

    // src mem
    case OT_Ind32:
    case OT_Ind64:
        assert(opValType(src) == opValType(dst));
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
            // use 'xor r,r/m 32/64' (0x33 RM)
            return genModRM(buf, 0x33, -1, src, dst, VT_None);

        default: assert(0);
        }
        break;

    case OT_Imm8:
        // src imm8
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'xor r/m 32/64, imm8' (0x83/6 MI)
            return genDigitMI(buf, 0x83, 6, dst, src);

        default: assert(0);
        }
        break;

    case OT_Imm32:
        // src imm32
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'xor r/m 32/64, imm32' (0x81/6 MI)
            return genDigitMI(buf, 0x81, 6, dst, src);

        default: assert(0);
        }
        break;

    default: assert(0);
    }
    return 0;
}

int genOr(uint8_t* buf, Operand* src, Operand* dst)
{
    switch(src->type) {
    // src reg
    case OT_Reg32:
    case OT_Reg64:
        assert(opValType(src) == opValType(dst));
        switch(dst->type) {
        case OT_Reg32:
        case OT_Ind32:
        case OT_Reg64:
        case OT_Ind64:
            // use 'or r/m,r 32/64' (0x09 MR)
            return genModRM(buf, 0x09, -1, dst, src, VT_None);

        default: assert(0);
        }
        break;

    // src mem
    case OT_Ind32:
    case OT_Ind64:
        assert(opValType(src) == opValType(dst));
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
            // use 'or r,r/m 32/64' (0x0B RM)
            return genModRM(buf, 0x0B, -1, src, dst, VT_None);

        default: assert(0);
        }
        break;

    case OT_Imm8:
        // src imm8
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'or r/m 32/64, imm8' (0x83/1 MI)
            return genDigitMI(buf, 0x83, 1, dst, src);

        default: assert(0);
        }
        break;

    case OT_Imm32:
        // src imm32
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'or r/m 32/64, imm32' (0x81/1 MI)
            return genDigitMI(buf, 0x81, 1, dst, src);

        default: assert(0);
        }
        break;

    default: assert(0);
    }
    return 0;
}

int genAnd(uint8_t* buf, Operand* src, Operand* dst)
{
    switch(src->type) {
    // src reg
    case OT_Reg32:
    case OT_Reg64:
        assert(opValType(src) == opValType(dst));
        switch(dst->type) {
        case OT_Reg32:
        case OT_Ind32:
        case OT_Reg64:
        case OT_Ind64:
            // use 'and r/m,r 32/64' (0x21 MR)
            return genModRM(buf, 0x21, -1, dst, src, VT_None);

        default: assert(0);
        }
        break;

    // src mem
    case OT_Ind32:
    case OT_Ind64:
        assert(opValType(src) == opValType(dst));
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
            // use 'and r,r/m 32/64' (0x23 RM)
            return genModRM(buf, 0x23, -1, src, dst, VT_None);

        default: assert(0);
        }
        break;

    case OT_Imm8:
        // src imm8
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'and r/m 32/64, imm8' (0x83/4 MI)
            return genDigitMI(buf, 0x83, 4, dst, src);

        default: assert(0);
        }
        break;

    case OT_Imm32:
        // src imm32
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'and r/m 32/64, imm32' (0x81/4 MI)
            return genDigitMI(buf, 0x81, 4, dst, src);

        default: assert(0);
        }
        break;

    default: assert(0);
    }
    return 0;
}


int genShl(uint8_t* buf, Operand* src, Operand* dst)
{
    switch(src->type) {
    // src reg
    case OT_Imm8:
        switch(dst->type) {
        case OT_Reg32:
        case OT_Ind32:
        case OT_Reg64:
        case OT_Ind64:
            // use 'shl r/m 32/64, imm8' (0xC1/4 MI)
            return genDigitMI(buf, 0xC1, 4, dst, src);

        default: assert(0);
        }
        break;

    default: assert(0);
    }
    return 0;
}


int genLea(uint8_t* buf, Operand* src, Operand* dst)
{
    assert(opIsInd(src));
    assert(opIsGPReg(dst));
    switch(dst->type) {
    case OT_Reg32:
    case OT_Reg64:
        // use 'lea r/m,r 32/64' (0x8d)
        return genModRM(buf, 0x8d, -1, src, dst, VT_None);

    default: assert(0);
    }
    return 0;
}

int genCltq(uint8_t* buf, ValType vt)
{
    switch(vt) {
    case VT_32: buf[0] = 0x98; return 1;
    case VT_64: buf[0] = 0x48; buf[1] = 0x98; return 2;
    default: assert(0);
    }
    return 0;
}


int genCmp(uint8_t* buf, Operand* src, Operand* dst)
{
    // if src is imm, try to reduce width
    src = reduceImm64to32(src);
    src = reduceImm32to8(src);

    switch(src->type) {
    // src reg
    case OT_Reg32:
    case OT_Reg64:
        assert(opValType(src) == opValType(dst));
        switch(dst->type) {
        case OT_Reg32:
        case OT_Ind32:
        case OT_Reg64:
        case OT_Ind64:
            // use 'cmp r/m,r 32/64' (0x39 MR)
            return genModRM(buf, 0x39, -1, dst, src, VT_None);

        default: assert(0);
        }
        break;

    // src mem
    case OT_Ind32:
    case OT_Ind64:
        assert(opValType(src) == opValType(dst));
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
            // use 'cmp r,r/m 32/64' (0x3B RM)
            return genModRM(buf, 0x3B, -1, src, dst, VT_None);

        default: assert(0);
        }
        break;

    case OT_Imm8:
        // src imm8
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'cmp r/m 32/64, imm8' (0x83/7 MI)
            return genDigitMI(buf, 0x83, 7, dst, src);

        default: assert(0);
        }
        break;

    case OT_Imm32:
        // src imm32
        switch(dst->type) {
        case OT_Reg32:
        case OT_Reg64:
        case OT_Ind32:
        case OT_Ind64:
            // use 'cmp r/m 32/64, imm32' (0x81/7 MI)
            return genDigitMI(buf, 0x81, 7, dst, src);

        default: assert(0);
        }
        break;

    default: assert(0);
    }
    return 0;
}


// Pass-through: parser forwarding opcodes, provides encoding
int genPassThrough(uint8_t* buf, Instr* instr)
{
    int o = 0;

    assert(instr->ptLen > 0);
    if (instr->ptPSet & PS_66) buf[o++] = 0x66;
    if (instr->ptPSet & PS_F2) buf[o++] = 0xF2;
    if (instr->ptPSet & PS_F3) buf[o++] = 0xF3;

    // FIXME: REX prefix pass-through: combine with RM encoding changes

    if (instr->ptLen < 2) instr->ptOpc[1] = -1;
    assert(instr->ptLen < 3);

    switch(instr->ptEnc) {
    case OE_MR:
        o += genModRM(buf+o, instr->ptOpc[0], instr->ptOpc[1],
                &(instr->dst), &(instr->src), instr->vtype);
        break;

    case OE_RM:
        o += genModRM(buf+o, instr->ptOpc[0], instr->ptOpc[1],
                &(instr->src), &(instr->dst), instr->vtype);
        break;

    default: assert(0);
    }

    return o;
}


//---------------------------------------------------------------
// Functions to find/allocate new (captured) basic blocks (CBBs).
// A CBB is keyed by a function address and world state ID
// (actually an emulator state esID)

// remove any previously allocated CBBs (keep allocated memory space)
void resetCapturing(Rewriter* r)
{
    // only to be called after initRewriter()
    assert(r->capInstr != 0);
    assert(r->capBB != 0);

    r->capBBCount = 0;
    r->capInstrCount = 0;
    r->currentCapBB = 0;

    r->capStackTop = -1;
    r->savedStateCount = 0;
}

// return 0 if not found
CBB *findCaptureBB(Rewriter* r, uint64_t f, int esID)
{
    int i;

    for(i = 0; i < r->capBBCount; i++)
        if ((r->capBB[i].dec_addr == f) && (r->capBB[i].esID == esID))
            return &(r->capBB[i]);

    return 0;
}

// allocate a BB structure to collect instructions for capturing
CBB* getCaptureBB(Rewriter* c, uint64_t f, int esID)
{
    CBB* bb;

    // already captured?
    bb = findCaptureBB(c, f, esID);
    if (bb) return bb;

    // start capturing of new BB beginning at f
    assert(c->capBBCount < c->capBBCapacity);
    bb = &(c->capBB[c->capBBCount]);
    c->capBBCount++;
    bb->dec_addr = f;
    bb->esID = esID;

    bb->count = 0;
    bb->instr = 0; // updated on first instruction added
    bb->nextBranch = 0;
    bb->nextFallThrough = 0;
    bb->endType = IT_None;
    bb->preferBranch = False;

    bb->size = -1; // size of 0 could be valid
    bb->addr1 = 0;
    bb->addr2 = 0;
    bb->genJcc8 = False;
    bb->genJump = False;

    return bb;
}

int pushCaptureBB(Rewriter* r, CBB* bb)
{
    assert(r->capStackTop + 1 < CAPTURESTACK_LEN);
    r->capStackTop++;
    r->capStack[r->capStackTop] = bb;

    return r->capStackTop;
}

CBB* popCaptureBB(Rewriter* r)
{
    CBB* bb = r->currentCapBB;
    assert(r->capStack[r->capStackTop] == bb);
    r->capStackTop--;
    r->currentCapBB = 0;

    return bb;
}

Instr* newCapInstr(Rewriter* r)
{
    Instr* instr;

    assert(r->capInstrCount < r->capInstrCapacity);
    instr = r->capInstr + r->capInstrCount;
    r->capInstrCount++;

    return instr;
}

// capture a new instruction
void capture(Rewriter* r, Instr* instr)
{
    Instr* newInstr;
    CBB* cbb = r->currentCapBB;
    if (cbb == 0) return;

    if (r->showEmuSteps)
        printf("Capture '%s' (into 0x%lx|%d + %d)\n",
               instr2string(instr, 0), cbb->dec_addr, cbb->esID, cbb->count);

    newInstr = newCapInstr(r);
    if (cbb->instr == 0) {
        cbb->instr = newInstr;
        assert(cbb->count == 0);
    }
    copyInstr(newInstr, instr);
    cbb->count++;
}

// generate code for a captured BB
void generate(Rewriter* c, CBB* cbb)
{
    uint8_t* buf;
    int used, i, usedTotal;

    if (cbb == 0) return;
    if (c->cs == 0) return;

    if (c->showEmuSteps)
        printf("Generating code for BB %lx|%d (%d instructions)\n",
               cbb->dec_addr, cbb->esID, cbb->count);

    usedTotal = 0;
    for(i = 0; i < cbb->count; i++) {
        Instr* instr = cbb->instr + i;

        buf = reserveCodeStorage(c->cs, 15);

        if (instr->ptLen > 0) {
            used = genPassThrough(buf, instr);
        }
        else {
            switch(instr->type) {
            case IT_ADD:
                used = genAdd(buf, &(instr->src), &(instr->dst));
                break;
            case IT_CLTQ:
                used = genCltq(buf, instr->vtype);
                break;
            case IT_CMP:
                used = genCmp(buf, &(instr->src), &(instr->dst));
                break;
            case IT_DEC:
                used = genDec(buf, &(instr->dst));
                break;
            case IT_IMUL:
                used = genIMul(buf, &(instr->src), &(instr->dst));
                break;
            case IT_INC:
                used = genInc(buf, &(instr->dst));
                break;
            case IT_XOR:
                used = genXor(buf, &(instr->src), &(instr->dst));
                break;
            case IT_OR:
                used = genOr(buf, &(instr->src), &(instr->dst));
                break;
            case IT_AND:
                used = genAnd(buf, &(instr->src), &(instr->dst));
                break;
            case IT_SHL:
                used = genShl(buf, &(instr->src), &(instr->dst));
                break;
            case IT_LEA:
                used = genLea(buf, &(instr->src), &(instr->dst));
                break;
            case IT_MOV:
            case IT_MOVSX: // converting move
                used = genMov(buf, &(instr->src), &(instr->dst));
                break;
            case IT_POP:
                used = genPop(buf, &(instr->dst));
                break;
            case IT_PUSH:
                used = genPush(buf, &(instr->dst));
                break;
            case IT_RET:
                used = genRet(buf);
                break;
            case IT_SUB:
                used = genSub(buf, &(instr->src), &(instr->dst));
                break;
            case IT_HINT_CALL:
            case IT_HINT_RET:
                used = 0;
                break;
            default: assert(0);
            }
        }
        assert(used < 15);

        instr->addr = (uint64_t) buf;
        instr->len = used;
        usedTotal += used;

        if (c->showEmuSteps) {
            printf("  I%2d : %-32s", i, instr2string(instr, 1));
            printf(" %lx %s\n", instr->addr, bytes2string(instr, 0, used));
        }

        useCodeStorage(c->cs, used);
    }

    if (c->showEmuSteps) {
        if (instrIsJcc(cbb->endType)) {
            assert(cbb->nextBranch != 0);
            assert(cbb->nextFallThrough != 0);

        printf("  I%2d : %s (%lx|%d), fall-through to (%lx|%d)\n",
               i, instrName(cbb->endType, 0),
               cbb->nextBranch->dec_addr, cbb->nextBranch->esID,
               cbb->nextFallThrough->dec_addr, cbb->nextFallThrough->esID);
        }
    }

    // add padding space after generated code for jump instruction
    buf = useCodeStorage(c->cs, 10);

    cbb->size = usedTotal;
    // start address of generated code.
    // if CBB had no instruction, this points to the padding buffer
    cbb->addr1 = (cbb->count == 0) ? ((uint64_t)buf) : cbb->instr[0].addr;
}


/*------------------------------------------------------------*/
/* x86_64 capturing emulator
 * trace execution in the emulator to capture code to generate
 *
 * We maintain states (known/static vs unknown/dynamic at capture time)
 * for registers and values on stack. To be able to do the latter, we
 * assume that the known values on stack do not get changed by
 * memory writes with dynamic address. This assumption should be fine,
 * as such behavior is dangerous and potentially a bug.
 *
 * At branches to multiple possible targets, we need to travers each path by
 * saving emulator state. After emulating one path, we roll back and
 * go the other path. As this may happen recursively, we do a kind of
 * back-tracking, with emulator states stored as stacks.
 * To allow for fast saving/restoring of emulator states, each part of
 * the emulation state (registers, bytes on stack) is given by a
 * EmuStateEntry (linked) list with the current value/state in front.
 * Saving copies the complete EmuState, inheriting the individual states.
 */

// emulator capture states
typedef enum _CaptureState {
    CS_DEAD = 0,      // uninitialized, should be invalid to access
    CS_DYNAMIC,       // data unknown at code generation time
    CS_STATIC,        // data known at code generation time
    CS_STACKRELATIVE, // address with known offset from stack top at start
    CS_STATIC2,       // same as static + indirection from memory static
    CS_Max
} CaptureState;

typedef enum _FlagType {
    FT_Carry = 0, FT_Zero, FT_Sign, FT_Overflow, FT_Parity,
    FT_Max
} FlagType;

#define MAX_CALLDEPTH 5

// emulator state. for memory, use the real memory apart from stack
typedef struct _EmuState {

    // when saving an EmuState, remember root
    EmuState* parent;

    // general registers: Reg_AX .. Reg_R15
    uint64_t reg[Reg_Max];
    CaptureState reg_state[Reg_Max];

    // x86 flags: carry (CF), zero (ZF), sign (SF), overflow (OF), parity (PF)
    // TODO: auxiliary carry
    Bool flag[FT_Max];
    CaptureState flag_state[FT_Max];

    // stack
    int stackSize;
    uint8_t* stack; // real memory backing
    uint64_t stackStart, stackAccessed, stackTop; // virtual stack boundaries
    // capture state of stack
    CaptureState *stackState;

    // own return stack
    uint64_t ret_stack[MAX_CALLDEPTH];
    int depth;

} EmuState;

// a single value with type and capture state
typedef struct _EmuValue {
    uint64_t val;
    ValType type;
    CaptureState state;
} EmuValue;

#define CC_MAXPARAM     5
#define CC_MAXCALLDEPTH 5
typedef struct _CaptureConfig
{
    CaptureState par_state[CC_MAXPARAM];
     // does function to rewrite return floating point?
    Bool hasReturnFP;
	// avoid unrolling at call depths
	Bool force_unknown[CC_MAXCALLDEPTH];
} CaptureConfig;

char captureState2Char(CaptureState s)
{
    assert((s >= 0) && (s < CS_Max));
    assert(CS_Max == 5);
    return "-DSR2"[s];
}

Bool csIsStatic(CaptureState s)
{
    if ((s == CS_STATIC) || (s == CS_STATIC2)) return True;
    return False;
}

void freeCaptureConfig(Rewriter* r)
{
    free(r->cc);
    r->cc = 0;
}

void brew_config_reset(Rewriter* c)
{
    CaptureConfig* cc;
    int i;

    if (c->cc)
        freeCaptureConfig(c);

    cc = (CaptureConfig*) malloc(sizeof(CaptureConfig));
    for(i=0; i < CC_MAXPARAM; i++)
        cc->par_state[i] = CS_DYNAMIC;
	for(i=0; i < CC_MAXCALLDEPTH; i++)
        cc->force_unknown[i] = False;
    cc->hasReturnFP = False;

    c->cc = cc;
}


CaptureConfig* getCaptureConfig(Rewriter* c)
{
    if (c->cc == 0)
        brew_config_reset(c);

    return c->cc;
}

void brew_config_staticpar(Rewriter* c, int staticParPos)
{
    CaptureConfig* cc = getCaptureConfig(c);

    assert((staticParPos >= 0) && (staticParPos < CC_MAXPARAM));
    cc->par_state[staticParPos] = CS_STATIC2;
}

/**
 * This allows to specify for a given function inlining depth that
 * values produced by binary operations always should be forced to unknown.
 * Thus, when result is known, it is converted to unknown state with
 * the value being loaded as immediate into destination.
 *
 * Brute force approach to prohibit loop unrolling.
 */
void brew_config_force_unknown(Rewriter* r, int depth)
{
    CaptureConfig* cc = getCaptureConfig(r);

    assert((depth >= 0) && (depth < CC_MAXCALLDEPTH));
    cc->force_unknown[depth] = True;
}

void brew_config_returnfp(Rewriter* c)
{
    CaptureConfig* cc = getCaptureConfig(c);
    cc->hasReturnFP = True;
}

EmuValue emuValue(uint64_t v, ValType t, CaptureState s)
{
    EmuValue ev;
    ev.val = v;
    ev.type = t;
    ev.state = s;

    return ev;
}

EmuValue staticEmuValue(uint64_t v, ValType t)
{
    EmuValue ev;
    ev.val = v;
    ev.type = t;
    ev.state = CS_STATIC;

    return ev;
}

void resetEmuState(EmuState* es)
{
    int i;
    static Reg calleeSave[] = {
        Reg_BP, Reg_BX, Reg_12, Reg_13, Reg_14, Reg_15, Reg_None };

    es->parent = 0;

    for(i=0; i<Reg_Max; i++) {
        es->reg[i] = 0;
        es->reg_state[i] = CS_DEAD;
    }

    for(i=0; i<FT_Max; i++) {
        es->flag[i] = False;
        es->flag_state[i] = CS_DEAD;
    }

    for(i=0; i< es->stackSize; i++)
        es->stack[i] = 0;
    for(i=0; i< es->stackSize; i++)
        es->stackState[i] = CS_DEAD;

    // use real addresses for now
    es->stackStart = (uint64_t) es->stack;
    es->stackTop = es->stackStart + es->stackSize;
    es->stackAccessed = es->stackTop;

    // calling convention:
    //  rbp, rbx, r12-r15 have to be preserved by callee
    for(i=0; calleeSave[i] != Reg_None; i++)
        es->reg_state[calleeSave[i]] = CS_DYNAMIC;
    // RIP always known
    es->reg_state[Reg_IP] = CS_STATIC;

    es->depth = 0;
}

EmuState* allocEmuState(int size)
{
    EmuState* es;

    es = (EmuState*) malloc(sizeof(EmuState));
    es->stackSize = size;
    es->stack = (uint8_t*) malloc(size);
    es->stackState = (CaptureState*) malloc(sizeof(CaptureState) * size);

    return es;
}

void freeEmuState(Rewriter* r)
{
    if (!r->es) return;

    free(r->es->stack);
    free(r->es->stackState);
    free(r->es);
    r->es = 0;
}

// are the capture states of a memory resource from different EmuStates equal?
// this is required for compatibility of generated code points, and
// compatibility is needed to be able to jump between such code points
Bool csIsEqual(EmuState* es1, CaptureState s1, uint64_t v1,
               EmuState* es2, CaptureState s2, uint64_t v2)
{
    // normalize meta states: CS_STATIC2 is equivalent to CS_STATIC
    if (s1 == CS_STATIC2) s1 = CS_STATIC;
    if (s2 == CS_STATIC2) s2 = CS_STATIC;
    // handle DEAD equal to DYNAMIC (no need to distinguish)
    if (s1 == CS_DEAD) s1 = CS_DYNAMIC;
    if (s2 == CS_DEAD) s2 = CS_DYNAMIC;

    if (s1 != s2) return False;

    switch(s1) {
    case CS_STATIC:
        // for static capture states, values have to be equal
        return (v1 == v2);

    case CS_STACKRELATIVE:
        // FIXME: in reality: same offset from a versioned anchor
        // for now: assume same anchor version (within same rewriting action)
        if (es1->parent != es2->parent) return False;
        return (v1 == v2);

    default:
        break;
    }
    return True;
}

// states are equal if metainformation is equal and static data is the same
Bool esIsEqual(EmuState* es1, EmuState* es2)
{
    int i;

    // same state for registers?
    for(i = Reg_AX; i <= Reg_15; i++) {
        if (!csIsEqual(es1, es1->reg_state[i], es1->reg[i],
                       es2, es2->reg_state[i], es2->reg[i]))
            return False;
    }

    // same state for flag registers?
    for(i = 0; i < FT_Max; i++) {
        if (!csIsEqual(es1, es1->flag_state[i], es1->flag[i],
                       es2, es2->flag_state[i], es2->flag[i]))
            return False;
    }

    // for equality, must be at same call depth
    if (es1->depth != es2->depth) return False;

    // Stack
    // all known data has to be the same
    if (es1->stackSize < es2->stackSize) {
        int diff = es2->stackSize - es1->stackSize;
        // stack of es2 is larger: bottom should not be static
        for(i = 0; i < diff; i++) {
            if (csIsStatic(es2->stackState[i]))
                return False;
        }
        // check for equal state at byte granularity
        for(i = 0; i < es1->stackSize; i++) {
            if (!csIsEqual(es1, es1->stackState[i], es1->stack[i],
                           es2, es2->stackState[i+diff], es2->stack[i+diff]))
                return False;
        }
    }
    else {
        // es1 larger
        int diff = es1->stackSize - es2->stackSize;
        // bottom of es1 should not be static
        for(i = 0; i < diff; i++) {
            if (csIsStatic(es1->stackState[i]))
                return False;
        }
        // check for equal state at byte granularity
        for(i = 0; i < es2->stackSize; i++) {
            if (!csIsEqual(es1, es1->stackState[i+diff], es1->stack[i+diff],
                           es2, es2->stackState[i], es2->stack[i]))
                return False;
        }
    }

    return True;
}

void copyEmuState(EmuState* dst, EmuState* src)
{
    int i;

    dst->parent = src->parent;

    for(i=0; i<Reg_Max; i++) {
        dst->reg[i] = src->reg[i];
        dst->reg_state[i] = src->reg_state[i];
    }

    for(i = 0; i < FT_Max; i++) {
        dst->flag[i] = src->flag[i];
        dst->flag_state[i] = src->flag_state[i];
    }

    dst->stackTop = src->stackTop;
    dst->stackAccessed = src->stackAccessed;
    if (src->stackSize < dst->stackSize) {
        // stack to restore is smaller than at destination:
        // fill start of destination with DEAD entries
        int diff = dst->stackSize - src->stackSize;

        dst->stackStart = src->stackStart - diff;
        for(i = 0; i < diff; i++) {
            dst->stackState[i] = CS_DEAD;
            dst->stack[i] = 0;
        }
        for(i = 0; i < src->stackSize; i++) {
            dst->stack[i+diff] = src->stack[i];
            dst->stackState[i+diff] = src->stackState[i];
        }
    }
    else {
        // stack to restore is larger than at destination:
        // make sure that start of source was never accessed
        int diff = src->stackSize - dst->stackSize;
        assert(src->stackAccessed - src->stackStart >= diff);

        dst->stackStart = src->stackStart + diff;
        for(i = 0; i < dst->stackSize; i++) {
            dst->stack[i] = src->stack[i+diff];
            dst->stackState[i] = src->stackState[i+diff];
        }
    }
    assert(dst->stackTop == dst->stackStart + dst->stackSize);

    dst->depth = src->depth;
    for(i = 0; i < src->depth; i++)
        dst->ret_stack[i] = src->ret_stack[i];
}

EmuState* cloneEmuState(EmuState* src)
{
    EmuState* dst;

    // allocate only stack space that was accessed in source
    dst = allocEmuState(src->stackTop - src->stackAccessed);
    copyEmuState(dst, src);

    // remember that we cloned dst from src
    dst->parent = src;

    return dst;
}

// checks current state against already saved states, and returns an ID
// (which is the index in the saved state list of the rewriter)
int saveEmuState(Rewriter* r)
{
    int i;

    printf("Saving current emulator state: ");
    //printStaticEmuState(r->es, -1);
    for(i = 0; i < r->savedStateCount; i++) {
        //printf("Check ES %d\n", i);
        //printStaticEmuState(r->savedState[i], i);
        if (esIsEqual(r->es, r->savedState[i])) {
            printf("already existing, esID %d\n", i);
            return i;
        }
    }
    printf("new with esID %d\n", i);
    assert(i < SAVEDSTATE_MAX);
    r->savedState[i] = cloneEmuState(r->es);
    r->savedStateCount++;

    return i;
}

void restoreEmuState(Rewriter* r, int esID)
{
    assert((esID >= 0) && (esID < r->savedStateCount));
    assert(r->savedState[esID] != 0);
    copyEmuState(r->es, r->savedState[esID]);
}

char* flagName(int f)
{
    switch(f) {
    case FT_Zero:     return "ZF";
    case FT_Carry:    return "CF";
    case FT_Sign:     return "SF";
    case FT_Overflow: return "OF";
    case FT_Parity:   return "PF";
    }
    assert(0);
}

void printEmuState(EmuState* es)
{
    int i, spOff, spMin, spMax, o, oo;

    printf("Emulation State:\n");

    printf("  Call stack (current depth %d): ", es->depth);
    for(i=0; i<es->depth; i++)
        printf(" %p", (void*) es->ret_stack[i]);
    printf("%s\n", (es->depth == 0) ? " (empty)":"");

    printf("  Registers:\n");
    for(i=Reg_AX; i<Reg_8; i++) {
        printf("    %%%-3s = 0x%016lx %c", regName(i, OT_Reg64),
               es->reg[i], captureState2Char( es->reg_state[i] ));
        printf("    %%%-3s = 0x%016lx %c\n", regName(i+8, OT_Reg64),
               es->reg[i+8], captureState2Char( es->reg_state[i+8] ));
    }
    printf("    %%%-3s = 0x%016lx %c\n", regName(Reg_IP, OT_Reg64),
           es->reg[Reg_IP], captureState2Char( es->reg_state[Reg_IP] ));

    printf("  Flags: ");
    for(i = 0; i < FT_Max; i++) {
        if (i>0) printf("  ");
        printf("%s %d %c", flagName(i), es->flag[i],
               captureState2Char(es->flag_state[i]));
    }
    printf("\n");

    spOff = es->reg[Reg_SP] - es->stackStart;
    spMax = spOff /8*8 + 40;
    spMin = spOff /8*8 - 32;
    if (es->stackStart + spMin < es->stackAccessed)
        spMin = (es->stackAccessed - es->stackStart)/8*8;
    if (es->stackStart + spMax > es->stackTop)
        spMax = es->stackSize -1;
    if (spMin >= spMax)
        printf("  Stack: (empty)\n");
    else {
        printf("  Stack:\n");
        for(o = spMin; o < spMax; o += 8) {
            printf("   %016lx ", (uint64_t) (es->stackStart + o));
            for(oo = o; oo < o+8 && oo <= spMax; oo++) {
                printf(" %s%02x %c", (oo == spOff) ? "*" : " ", es->stack[oo],
                       captureState2Char(es->stackState[oo]));
            }
            printf("\n");
        }
        printf("   %016lx  %s\n",
               (uint64_t) (es->stackStart + o), (o == spOff) ? "*" : " ");
    }
}

// print only state information important to distinguish for capturing
void printStaticEmuState(EmuState* es, int esID)
{
    int i, c, cc;

    printf("Emulation Static State (esID %d, call depth %d):\n",
           esID, es->depth);

    printf("  Registers: ");
    c = 0;
    for(i=Reg_AX; i<=Reg_15; i++) {
        if (es->reg_state[i] == CS_DEAD) continue;
        if (es->reg_state[i] == CS_DYNAMIC) continue;

        if (c>0) printf(", ");
        switch(es->reg_state[i]) {
        case CS_STATIC:
        case CS_STATIC2:
            printf("%%%s (0x%lx)", regName(i, OT_Reg64), es->reg[i]);
            break;
        case CS_STACKRELATIVE:
            printf("%%%s (R %ld)",
                   regName(i, OT_Reg64), es->reg[i] - es->stackTop);
            break;
        default: assert(0);
        }
        c++;
    }
    if (c>0)
        printf("\n");
    else
        printf("(none)\n");

    printf("  Flags: ");
    c = 0;
    for(i = 0; i < FT_Max; i++) {
        if (!csIsStatic(es->flag_state[i])) continue;
        if (c>0) printf(", ");
        printf("%s (%d)", flagName(i), es->flag[i]);
        c++;
    }
    if (c>0)
        printf("\n");
    else
        printf("(none)\n");

    printf("  Stack: ");
    cc = 0;
    c = 0;
    for(i = 0; i < es->stackSize; i++) {
        if (!csIsStatic(es->stackState[i])) {
            c = 0;
            continue;
        }
        if (c == 0)
            printf("\n   %016lx ", (uint64_t) (es->stackStart + i));
        else
            printf(" ");
        printf("%02x", es->stack[i]);
        cc++;
        c++;
    }
    if (cc>0)
        printf("\n");
    else
        printf("(none)\n");
}

char combineState(CaptureState s1, CaptureState s2,
                  Bool isSameValue)
{
    // dead/invalid: combining with something invalid makes result invalid
    if ((s1 == CS_DEAD) || (s2 == CS_DEAD)) return CS_DEAD;

    // if both are static, static-ness is preserved
    if (csIsStatic(s1) && csIsStatic(s2)) {
        if ((s1 == CS_STATIC2) || (s2 == CS_STATIC2)) return CS_STATIC2;
        return CS_STATIC;
    }

    // stack-relative handling:
    // depends on combining of sub-state of one value or combining two values
    if (isSameValue) {
        // if both are stack-relative, it is preserved
        if ((s1 == CS_STACKRELATIVE) && (s2 == CS_STACKRELATIVE))
            return CS_STACKRELATIVE;
    }
    else {
        // STACKRELATIVE is preserved if other is STATIC (FIXME: only ADD!)
        if ((s1 == CS_STACKRELATIVE) && csIsStatic(s2))
            return CS_STACKRELATIVE;
        if (csIsStatic(s1) && (s2 == CS_STACKRELATIVE))
            return CS_STACKRELATIVE;
    }

    return CS_DYNAMIC;
}

char combineState4Flags(CaptureState s1, CaptureState s2)
{
    CaptureState s;

    s = combineState(s1, s2, 0);
    // STACKRELATIVE/STATIC2 makes no sense for flags
    if (s == CS_STACKRELATIVE) s = CS_DYNAMIC;
    if (s == CS_STATIC2) s = CS_STATIC;

    return s;
}

//---------------------------------------------------------
// emulator functions


// flag setting helpers

/* Setting some flags can get complicated.
 * From libx86emu/prim_ops.c (github.com/wfeldt/libx86emu)
 */
static uint32_t parity_tab[8] =
{
    0x96696996, 0x69969669, 0x69969669, 0x96696996,
    0x69969669, 0x96696996, 0x96696996, 0x69969669,
};

#define PARITY(x)   (((parity_tab[(x) / 32] >> ((x) % 32)) & 1) == 0)
#define XOR2(x)     (((x) ^ ((x)>>1)) & 0x1)

// set flags for operation "v1 - v2"
CaptureState setFlagsSub(EmuState* es, EmuValue* v1, EmuValue* v2)
{
    CaptureState st;
    uint64_t r, bc, d, s;

    st = combineState4Flags(v1->state, v2->state);
    es->flag_state[FT_Carry]    = st;
    es->flag_state[FT_Zero]     = st;
    es->flag_state[FT_Sign]     = st;
    es->flag_state[FT_Overflow] = st;
    es->flag_state[FT_Parity]   = st;

    assert(v1->type == v2->type);

    d = v1->val;
    s = v2->val;
    r = d - s;
    bc = (r & (~d | s)) | (~d & s);

    es->flag[FT_Zero]   = (d == s);
    es->flag[FT_Parity] = PARITY(r & 0xff);
    switch(v1->type) {
    case VT_8:
        es->flag[FT_Sign]     = (r >> 7) & 1;
        es->flag[FT_Carry]    = (bc >>7) & 1;
        es->flag[FT_Overflow] = XOR2(bc >> 6);
        break;
    case VT_32:
        es->flag[FT_Sign]     = (r >> 31) & 1;
        es->flag[FT_Carry]    = (bc >>31) & 1;
        es->flag[FT_Overflow] = XOR2(bc >> 30);
        break;
    case VT_64:
        es->flag[FT_Sign]     = (r >> 63) & 1;
        es->flag[FT_Carry]    = (bc >>63) & 1;
        es->flag[FT_Overflow] = XOR2(bc >> 62);
        break;
    default: assert(0);
    }

    return st;
}

// set flags for operation "v1 + v2"
void setFlagsAdd(EmuState* es, EmuValue* v1, EmuValue* v2)
{
    CaptureState st;
    uint64_t r, cc, d, s;

    st = combineState4Flags(v1->state, v2->state);
    es->flag_state[FT_Carry]    = st;
    es->flag_state[FT_Zero]     = st;
    es->flag_state[FT_Sign]     = st;
    es->flag_state[FT_Overflow] = st;
    es->flag_state[FT_Parity]   = st;

    assert(v1->type == v2->type);

    d = v1->val;
    s = v2->val;
    r = d + s;
    cc = (r & (~d | s)) | (~d & s);

    es->flag[FT_Parity] = PARITY(r & 0xff);
    switch(v1->type) {
    case VT_8:
        es->flag[FT_Carry]    = (cc >> 7) & 1;
        es->flag[FT_Zero]     = ((r & ((1<<8)-1)) == 0);
        es->flag[FT_Sign]     = (r >> 7) & 1;
        es->flag[FT_Overflow] = XOR2(cc >> 6);
        break;
    case VT_32:
        es->flag[FT_Carry]    = (cc >> 31) & 1;
        es->flag[FT_Zero]     = ((r & ((1l<<32)-1)) == 0);
        es->flag[FT_Sign]     = (r >> 31) & 1;
        es->flag[FT_Overflow] = XOR2(cc >> 30);
        break;
    case VT_64:
        es->flag[FT_Carry]    = (cc >> 63) & 1;
        es->flag[FT_Zero]     = (r  == 0);
        es->flag[FT_Sign]     = (r >> 63) & 1;
        es->flag[FT_Overflow] = XOR2(cc >> 62);
        break;
    default: assert(0);
    }
}

// for bitwise operations: And, Xor, Or
CaptureState setFlagsBit(EmuState* es, InstrType it,
                         EmuValue* v1, EmuValue* v2, Bool sameOperands)
{
    CaptureState s;
    uint64_t res;

    assert(v1->type == v2->type);

    s = combineState4Flags(v1->state, v2->state);
    // xor op,op results in known zero
    if ((it == IT_XOR) && sameOperands) s = CS_STATIC;

    // carry/overflow always cleared
    es->flag[FT_Carry] = 0;
    es->flag[FT_Overflow] = 0;
    es->flag_state[FT_Carry] = CS_STATIC;
    es->flag_state[FT_Overflow] = CS_STATIC;

    es->flag_state[FT_Zero] = s;
    es->flag_state[FT_Sign] = s;
    es->flag_state[FT_Parity] = s;

    switch(it) {
    case IT_AND: res = v1->val & v2->val; break;
    case IT_XOR: res = v1->val ^ v2->val; break;
    case IT_OR:  res = v1->val | v2->val; break;
    default: assert(0);
    }

    es->flag[FT_Zero]  = (res == 0);
    es->flag[FT_Parity] = PARITY(res & 0xff);
    switch(v1->type) {
    case VT_8:
        es->flag[FT_Sign] = ((res & (1l<<7)) != 0);
        break;
    case VT_32:
        es->flag[FT_Sign] = ((res & (1l<<31)) != 0);
        break;
    case VT_64:
        es->flag[FT_Sign] = ((res & (1l<<63)) != 0);
        break;
    default: assert(0);
    }

    return s;
}


// helpers for capture processing

// if addr on stack, return true and stack offset in <off>,
//  otherwise return false
// the returned offset is static only if address is stack-relative
Bool getStackOffset(EmuState* es, EmuValue* addr, EmuValue* off)
{
    if ((addr->val >= es->stackStart) && (addr->val < es->stackTop)) {
        off->type = VT_32;
        off->state = (addr->state == CS_STACKRELATIVE) ? CS_STATIC : CS_DYNAMIC;
        off->val = addr->val - es->stackStart;
        return True;
    }
    return False;
}

CaptureState getStackState(EmuState* es, EmuValue* off)
{
    if (off->state == CS_STATIC) {
        if (off->val >= es->stackSize) return CS_DEAD;
        if (off->val < es->stackAccessed - es->stackStart) return CS_DEAD;
        return es->stackState[off->val];
    }
    return CS_DYNAMIC;
}

void getStackValue(EmuState* es, EmuValue* v, EmuValue* off)
{
    int i, count;
    CaptureState state;

    assert((off->val >= 0) && (off->val < es->stackSize));

    switch(v->type) {
    case VT_32:
        v->val = *(uint32_t*) (es->stack + off->val);
        count = 4;
        break;

    case VT_64:
        v->val = *(uint64_t*) (es->stack + off->val);
        count = 8;
        break;

    default: assert(0);
    }

    if (off->state == CS_STATIC) {
        state = getStackState(es, off);
        for(i=1; i<count; i++)
            state = combineState(state, es->stackState[off->val + i], 1);
    }
    else
        state = CS_DYNAMIC;

    v->state = state;
}


void setStackValue(EmuState* es, EmuValue* v, EmuValue* off)
{
    uint32_t* a32;
    uint64_t* a64;
    int i, count;

    switch(v->type) {
    case VT_32:
        a32 = (uint32_t*) (es->stack + off->val);
        *a32 = (uint32_t) v->val;
        count = 4;
        break;

    case VT_64:
        a64 = (uint64_t*) (es->stack + off->val);
        *a64 = (uint64_t) v->val;
        count = 8;
        break;

    default: assert(0);
    }

    if (off->state == CS_STATIC) {
        for(i=0; i<count; i++)
            es->stackState[off->val + i] = v->state;
    }

    if (es->stackStart + off->val < es->stackAccessed)
        es->stackAccessed = es->stackStart + off->val;
}

void getRegValue(EmuValue* v, EmuState* es, Reg r, ValType t)
{
    v->type = t;
    v->val = es->reg[r];
    v->state = es->reg_state[r];
}

void setRegValue(EmuValue* v, EmuState* es, Reg r, ValType t)
{
    assert(v->type == t);
    es->reg[r] = v->val;
    es->reg_state[r] = v->state;
}

void getMemValue(EmuValue* v, EmuValue* addr, EmuState* es, ValType t,
                 Bool shouldBeStack)
{
    EmuValue off;
    int isOnStack;

    isOnStack = getStackOffset(es, addr, &off);
    if (isOnStack) {
        v->type = t;
        getStackValue(es, v, &off);
        return;
    }

    assert(!shouldBeStack);
    v->state = CS_DYNAMIC;
    // explicit request to make memory access result static
    if (addr->state == CS_STATIC2) v->state = CS_STATIC2;

    v->type = t;
    switch(t) {
    case VT_32: v->val = *(uint32_t*) addr->val; break;
    case VT_64: v->val = *(uint64_t*) addr->val; break;
    default: assert(0);
    }
}

void setMemValue(EmuValue* v, EmuValue* addr, EmuState* es, ValType t,
                 int shouldBeStack)
{
    EmuValue off;
    uint32_t* a32;
    uint64_t* a64;
    Bool isOnStack;

    assert(v->type == t);
    isOnStack = getStackOffset(es, addr, &off);
    if (isOnStack) {
        setStackValue(es, v, &off);
        return;
    }

    assert(!shouldBeStack);

    switch(t) {
    case VT_32:
        a32 = (uint32_t*) addr->val;
        *a32 = (uint32_t) v->val;
        break;

    case VT_64:
        a64 = (uint64_t*) addr->val;
        *a64 = (uint64_t) v->val;
        break;

    default: assert(0);
    }
}

// helper for getOpAddr()
void addRegToValue(EmuValue* v, EmuState* es, Reg r, int scale)
{
    if (r == Reg_None) return;

    v->state = combineState(v->state, es->reg_state[r], 0);
    v->val += scale * es->reg[r];
}

// get resulting address (and state) for memory operands
void getOpAddr(EmuValue* v, EmuState* es, Operand* o)
{
    assert(opIsInd(o));
    assert(o->seg == OSO_None); // TODO

    v->type = VT_64;
    v->val = o->val;
    v->state = CS_STATIC;

    if (o->reg != Reg_None)
        addRegToValue(v, es, o->reg, 1);

    if (o->scale > 0)
        addRegToValue(v, es, o->ireg, o->scale);
}

// returned value v should be casted to expected type (8/16/32 bit)
void getOpValue(EmuValue* v, EmuState* es, Operand* o)
{
    EmuValue addr;

    switch(o->type) {
    case OT_Imm8:
    case OT_Imm16:
    case OT_Imm32:
    case OT_Imm64:
        *v = staticEmuValue(o->val, opValType(o));
        return;

    case OT_Reg32:
        v->type = VT_32;
        v->val = (uint32_t) es->reg[o->reg];
        v->state = es->reg_state[o->reg];
        return;

    case OT_Reg64:
        v->type = VT_64;
        v->val = (uint64_t) es->reg[o->reg];
        v->state = es->reg_state[o->reg];
        return;

    case OT_Ind32:
    case OT_Ind64:
        getOpAddr(&addr, es, o);
        getMemValue(v, &addr, es, opValType(o), 0);
        return;

    default: assert(0);
    }
}

// only the bits of v are used which are required for operand type
void setOpValue(EmuValue* v, EmuState* es, Operand* o)
{
    EmuValue addr;

    assert(v->type == opValType(o));
    switch(o->type) {
    case OT_Reg32:
        es->reg[o->reg] = (uint32_t) v->val;
        es->reg_state[o->reg] = v->state;
        return;

    case OT_Reg64:
        es->reg[o->reg] = v->val;
        es->reg_state[o->reg] = v->state;
        return;

    case OT_Ind32:
    case OT_Ind64:
        getOpAddr(&addr, es, o);
        setMemValue(v, &addr, es, opValType(o), 0);
        return;

    default: assert(0);
    }
}

// Do we maintain capture state for a value pointed to by an operand?
// Returns false for memory locations not on stack or when stack offset
//  is not static/known.
Bool keepsCaptureState(EmuState* es, Operand* o)
{
    EmuValue addr;
    EmuValue off;
    Bool isOnStack;

    // never should be called with immediate ops (do not point to location)
    assert(!opIsImm(o));
    if (opIsGPReg(o)) return 1;

    getOpAddr(&addr, es, o);
    isOnStack = getStackOffset(es, &addr, &off);
    if (!isOnStack) return 0;
    return csIsStatic(off.state);
}

// apply known state to memory operand (this modifies the operand in-place)
void applyStaticToInd(Operand* o, EmuState* es)
{
    if (!opIsInd(o)) return;

    if ((o->reg != Reg_None) && csIsStatic(es->reg_state[o->reg])) {
        o->val += es->reg[o->reg];
        o->reg = Reg_None;
    }
    if ((o->scale > 0) && csIsStatic(es->reg_state[o->ireg])) {
        o->val += o->scale * es->reg[o->ireg];
        o->scale = 0;
    }
}


// capture processing for instruction types

// both MOV and MOVSX (sign extend 32->64)
void captureMov(Rewriter* c, Instr* orig, EmuState* es, EmuValue* res)
{
    Instr i;
    Operand *o;

    // data movement from orig->src to orig->dst, value is res

    if (res->state == CS_DEAD) return;

    o = &(orig->src);
    if (csIsStatic(res->state)) {
        // no need to update data if capture state is maintained
        if (keepsCaptureState(es, &(orig->dst))) return;

        // source is static, use immediate
        o = getImmOp(res->type, res->val);
    }
    initBinaryInstr(&i, orig->type, orig->vtype, &(orig->dst), o);
    applyStaticToInd(&(i.dst), es);
    applyStaticToInd(&(i.src), es);
    capture(c, &i);
}

// dst = dst op src
void captureBinaryOp(Rewriter* c, Instr* orig, EmuState* es, EmuValue* res)
{
    EmuValue opval;
    Instr i;
    Operand *o;

    if (res->state == CS_DEAD) return;

    if (csIsStatic(res->state)) {
        // force results to become unknown?
        if (c->cc->force_unknown[es->depth]) {
            res->state = CS_DYNAMIC;
        }
        else {
            // no need to update data if capture state is maintained
            if (keepsCaptureState(es, &(orig->dst))) return;
        }
        // if result is known and goes to memory, generate imm store
        initBinaryInstr(&i, IT_MOV, res->type,
                        &(orig->dst), getImmOp(res->type, res->val));
        applyStaticToInd(&(i.dst), es);
        capture(c, &i);
        return;
    }

    // if dst (= 2.op) known/constant and a reg/stack, we need to update it
    // example: %eax += %ebx with %eax known to be 5  =>  %eax=5, %eax+=%ebx
    getOpValue(&opval, es, &(orig->dst));
    if (keepsCaptureState(es, &(orig->dst)) && csIsStatic(opval.state)) {

        // - instead of adding src to 0, we can move the src to dst
        // - instead of multiply src with 1, move
        // TODO: mulitply with 0: here too late, state of result gets static
        if (((orig->type == IT_ADD) && (opval.val == 0)) ||
            ((orig->type == IT_IMUL) && (opval.val == 1))) {
            initBinaryInstr(&i, IT_MOV, opval.type,
                            &(orig->dst), &(orig->src));
            applyStaticToInd(&(i.dst), es);
            applyStaticToInd(&(i.src), es);
            capture(c, &i);
            return;
        }

        initBinaryInstr(&i, IT_MOV, opval.type,
                        &(orig->dst), getImmOp(opval.type, opval.val));
        capture(c, &i);
    }

    o = &(orig->src);
    getOpValue(&opval, es, &(orig->src));
    if (csIsStatic(opval.state)) {
        // if 1st source (=src) is known/constant and a reg, make it immediate

        if (((orig->type == IT_ADD) && (opval.val == 0)) ||
            ((orig->type == IT_SHL) && (opval.val == 0)) ||
            ((orig->type == IT_SHR) && (opval.val == 0)) ||
            ((orig->type == IT_IMUL) && (opval.val == 1))) {
            // adding 0 / multiplying with 1 changes nothing...
            return;
        }
        o = getImmOp(opval.type, opval.val);
    }
    initBinaryInstr(&i, orig->type, res->type, &(orig->dst), o);
    applyStaticToInd(&(i.dst), es);
    applyStaticToInd(&(i.src), es);
    capture(c, &i);
}

// dst = unary-op dst
void captureUnaryOp(Rewriter* c, Instr* orig, EmuState* es, EmuValue* res)
{
    Instr i;

    if (csIsStatic(res->state)) return;

    initUnaryInstr(&i, orig->type, &(orig->dst));
    applyStaticToInd(&(i.dst), es);
    capture(c, &i);
}

void captureLea(Rewriter* c, Instr* orig, EmuState* es, EmuValue* res)
{
    Instr i;

    assert(opIsReg(&(orig->dst)));
    if (csIsStatic(res->state)) {
        if (c->cc->force_unknown[es->depth]) {
            // force results to become unknown => load value into dest

            res->state = CS_DYNAMIC;
            initBinaryInstr(&i, IT_MOV, res->type,
                            &(orig->dst), getImmOp(res->type, res->val));
            capture(c, &i);
        }
        return;
    }
    initBinaryInstr(&i, IT_LEA, orig->vtype, &(orig->dst), &(orig->src));
    applyStaticToInd(&(i.src), es);
    capture(c, &i);
}

void captureCmp(Rewriter* c, Instr* orig, EmuState* es, CaptureState s)
{
    EmuValue opval;
    Instr i;
    Operand *o;

    if (csIsStatic(s)) return;

    getOpValue(&opval, es, &(orig->dst));
    if (csIsStatic(opval.state)) {
        // cannot replace dst with imm: no such encoding => update dst
        initBinaryInstr(&i, IT_MOV, opval.type,
                        &(orig->dst), getImmOp(opval.type, opval.val));
        capture(c, &i);
    }

    o = &(orig->src);
    getOpValue(&opval, es, &(orig->src));
    if (csIsStatic(opval.state))
        o = getImmOp(opval.type, opval.val);

    initBinaryInstr(&i, IT_CMP, orig->vtype, &(orig->dst), o);
    applyStaticToInd(&(i.dst), es);
    applyStaticToInd(&(i.src), es);
    capture(c, &i);
}

void captureTest(Rewriter* c, Instr* orig, EmuState* es, CaptureState s)
{
    Instr i;

    if (csIsStatic(s)) return;

    initBinaryInstr(&i, IT_TEST, orig->vtype, &(orig->dst), &(orig->src));
    applyStaticToInd(&(i.dst), es);
    applyStaticToInd(&(i.src), es);
    capture(c, &i);
}

void captureRet(Rewriter* c, Instr* orig, EmuState* es)
{
    EmuValue v;
    Instr i;

    // when returning an integer: if AX state is static, load constant
    if (!c->cc->hasReturnFP) {
        getRegValue(&v, es, Reg_AX, VT_64);
        if (csIsStatic(v.state)) {
            initBinaryInstr(&i, IT_MOV, VT_64,
                            getRegOp(VT_64, Reg_AX), getImmOp(v.type, v.val));
            capture(c, &i);
        }
    }
    capture(c, orig);
}

// helper for capturePassThrough: do capture state modifications
// if provided as meta information (e.g. setting values in locations unknown)
void processPassThrough(Rewriter* c, Instr* i, EmuState* es)
{
    assert(i->ptLen >0);
    if (i->ptSChange == SC_None) return;

    switch(i->dst.type) {
    case OT_Reg32:
    case OT_Reg64:
        if (opIsGPReg(&(i->dst)))
            es->reg_state[i->dst.reg] = CS_DYNAMIC;
        break;

        // memory locations not handled yet

    default: assert(0);
    }
}

void capturePassThrough(Rewriter* c, Instr* orig, EmuState* es)
{
    Instr i;

    // pass-through: may have influence to emu state
    processPassThrough(c, orig, es);

    assert(orig->ptLen >0);
    initSimpleInstr(&i, orig->type);
    i.vtype  = orig->vtype;

    i.ptLen  = orig->ptLen;
    i.ptEnc  = orig->ptEnc;
    i.ptPSet = orig->ptPSet;
    for(int j=0; j<orig->ptLen; j++)
        i.ptOpc[j] = orig->ptOpc[j];

    switch(orig->ptEnc) {
    case OE_MR:
        assert(opIsReg(&(orig->dst)) || opIsInd(&(orig->dst)));
        assert(opIsReg(&(orig->src)));

        i.form = OF_2;
        copyOperand( &(i.dst), &(orig->dst));
        copyOperand( &(i.src), &(orig->src));
        applyStaticToInd(&(i.dst), es);
        break;

    case OE_RM:
        assert(opIsReg(&(orig->src)) || opIsInd(&(orig->src)));
        assert(opIsReg(&(orig->dst)));

        i.form = OF_2;
        copyOperand( &(i.dst), &(orig->dst));
        copyOperand( &(i.src), &(orig->src));
        applyStaticToInd(&(i.src), es);
        break;

    default: assert(0);
    }
    capture(c, &i);
}

// this ends a captured BB, queuing new paths to be traced
void captureJcc(Rewriter* r, InstrType it,
                uint64_t branchTarget, uint64_t fallthroughTarget,
                Bool didBranch)
{
    CBB *cbb, *cbbBR, *cbbFT;
    int esID;

    cbb = popCaptureBB(r);
    cbb->endType = it;
    // use observed behavior from trace as hint for code generation
    cbb->preferBranch = didBranch;

    esID = saveEmuState(r);
    cbbFT = getCaptureBB(r, fallthroughTarget, esID);
    cbbBR = getCaptureBB(r, branchTarget, esID);
    cbb->nextFallThrough = cbbFT;
    cbb->nextBranch = cbbBR;

    // entry pushed last will be processed first
    if (didBranch) {
        pushCaptureBB(r, cbbFT);
        pushCaptureBB(r, cbbBR);
    }
    else {
        pushCaptureBB(r, cbbBR);
        pushCaptureBB(r, cbbFT);
    }
    // current CBB should be closed.
    // (we have to open a new CBB before allowing new instructions to capture)
    assert(r->currentCapBB == 0);
}

//----------------------------------------------------------
// optimization pass

// test: simply copy instructions
Instr* optPassCopy(Rewriter* r, CBB* cbb)
{
    Instr *first, *instr;
    int i;

    if (cbb->count == 0) return 0;

    first = newCapInstr(r);
    copyInstr(first, cbb->instr);
    for(i = 1; i < cbb->count; i++) {
        instr = newCapInstr(r);
        copyInstr(instr, cbb->instr + i);
    }
    return first;
}

void optPass(Rewriter* r, CBB* cbb)
{
    Instr* newInstrs;

    if (r->showOptSteps) {
        printf("Run Optimization for CBB (%lx|%d)\n",
               cbb->dec_addr, cbb->esID);
    }

    if (r->doCopyPass) {
        newInstrs = optPassCopy(r, cbb);
        if (newInstrs)
            cbb->instr = newInstrs;
    }
}

//----------------------------------------------------------
// Emulator for instruction types



// return 0 to fall through to next instruction, or return address to jump to
uint64_t emulateInstr(Rewriter* c, EmuState* es, Instr* instr)
{
    EmuValue vres, v1, v2, addr;
    CaptureState s;
    ValType vt;

    if (instr->ptLen > 0) {
        // memory addressing in captured instructions depends on emu state
        capturePassThrough(c, instr, es);
        return 0;
    }

    switch(instr->type) {

    case IT_ADD:
        getOpValue(&v1, es, &(instr->dst));
        getOpValue(&v2, es, &(instr->src));

        vt = opValType(&(instr->dst));
        // sign-extend src/v2 if needed
        if (instr->src.type == OT_Imm8) {
            // sign-extend to 64bit (may be cutoff later)
            v2.val = (int64_t) (int8_t) v2.val;
            v2.type = vt;
        }

        setFlagsAdd(es, &v1, &v2);
        assert(v1.type == v2.type);

        switch(vt) {
        case VT_32:
            v1.val = ((uint32_t) v1.val + (uint32_t) v2.val);
            break;

        case VT_64:
            v1.val = v1.val + v2.val;
            break;

        default:assert(0);
        }

        v1.state = combineState(v1.state, v2.state, 0);
        // for capture we need state of original dst, do it before setting dst
        captureBinaryOp(c, instr, es, &v1);
        setOpValue(&v1, es, &(instr->dst));
        break;

    case IT_CALL:
        // TODO: keep call. For now, we always inline
        getOpValue(&v1, es, &(instr->dst));
        assert(es->depth < MAX_CALLDEPTH);
        assert(csIsStatic(v1.state)); // call target must be known

        // push address of instruction after CALL onto stack
        es->reg[Reg_SP] -= 8;
        addr = emuValue(es->reg[Reg_SP], VT_64, es->reg_state[Reg_SP]);
        v2.state = CS_DYNAMIC;
        v2.type = VT_64;
        v2.val = instr->addr + instr->len;
        setMemValue(&v2, &addr, es, VT_64, 1);

        es->ret_stack[es->depth++] = v2.val;

        if (c->addInliningHints) {
            Instr i;
            initSimpleInstr(&i, IT_HINT_CALL);
            capture(c, &i);
        }

        // special handling for known functions
        if ((v1.val == (uint64_t) makeDynamic) &&
            csIsStatic(es->reg_state[Reg_DI])) {
            // update register value to static value
            Instr i;
            initBinaryInstr(&i, IT_MOV, VT_64,
                            getRegOp(VT_64, Reg_DI),
                            getImmOp(VT_64, es->reg[Reg_DI]));
            capture(c, &i);
            es->reg_state[Reg_DI] = CS_DYNAMIC;
        }
        if (v1.val == (uint64_t) makeStatic)
            es->reg_state[Reg_DI] = CS_STATIC2;

        // address to jump to
        return v1.val;

    case IT_CLTQ:
        switch(instr->vtype) {
        case VT_32:
            es->reg[Reg_AX] = (int32_t) (int16_t) es->reg[Reg_AX];
            break;
        case VT_64:
            es->reg[Reg_AX] = (int64_t) (int32_t) es->reg[Reg_AX];
            break;
        default: assert(0);
        }
        if (!csIsStatic(es->reg_state[Reg_AX]))
            capture(c, instr);
        break;

    case IT_CMP:
        getOpValue(&v1, es, &(instr->dst));
        getOpValue(&v2, es, &(instr->src));

        vt = opValType(&(instr->dst));
        // sign-extend src/v2 if needed
        if (instr->src.type == OT_Imm8) {
            // sign-extend to 64bit (may be cutoff later)
            v2.val = (int64_t) (int8_t) v2.val;
            v2.type = vt;
        }
        s = setFlagsSub(es, &v1, &v2);
        captureCmp(c, instr, es, s);
        break;

    case IT_DEC:
        getOpValue(&v1, es, &(instr->dst));
        switch(instr->dst.type) {
        case OT_Reg32:
        case OT_Ind32:
            v1.val = (uint32_t)(((int32_t) v1.val) - 1);
            break;

        case OT_Reg64:
        case OT_Ind64:
            v1.val = (uint64_t)(((int64_t) v1.val) - 1);
            break;

        default:assert(0);
        }
        captureUnaryOp(c, instr, es, &v1);
        setOpValue(&v1, es, &(instr->dst));
        break;

    case IT_IMUL:
        getOpValue(&v1, es, &(instr->dst));
        getOpValue(&v2, es, &(instr->src));

        assert(opIsGPReg(&(instr->dst)));
        assert(v1.type == v2.type);
        switch(instr->src.type) {
        case OT_Reg32:
        case OT_Ind32:
            vres.type = VT_32;
            vres.val = (uint64_t) ((int32_t) v1.val * (int32_t) v2.val);
            break;

        case OT_Reg64:
        case OT_Ind64:
            vres.type = VT_64;
            vres.val = (uint64_t) ((int64_t) v1.val * (int64_t) v2.val);
            break;

        default:assert(0);
        }

        // optimization: muliply with static 0 results in static 0
        if ((csIsStatic(v1.state) && (v1.val == 0)) ||
            (csIsStatic(v2.state) && (v2.val == 0)))
            vres.state = CS_STATIC;
        else
            vres.state = combineState(v1.state, v2.state, 0);

        // for capture we need state of dst, do before setting dst
        captureBinaryOp(c, instr, es, &vres);
        setOpValue(&vres, es, &(instr->dst));
        break;

    case IT_INC:
        getOpValue(&v1, es, &(instr->dst));
        switch(instr->dst.type) {
        case OT_Reg32:
        case OT_Ind32:
            v1.val = (uint32_t)(((int32_t) v1.val) + 1);
            break;

        case OT_Reg64:
        case OT_Ind64:
            v1.val = (uint64_t)(((int64_t) v1.val) + 1);
            break;

        default:assert(0);
        }
        captureUnaryOp(c, instr, es, &v1);
        setOpValue(&v1, es, &(instr->dst));
        break;

    case IT_JE:
        if (es->flag_state[FT_Zero] != CS_STATIC) {
            captureJcc(c, IT_JE, instr->dst.val, instr->addr + instr->len,
                        es->flag[FT_Zero]);
        }
        if (es->flag[FT_Zero] == True) return instr->dst.val;
        return instr->addr + instr->len;

    case IT_JNE:
        if (es->flag_state[FT_Zero] != CS_STATIC) {
            captureJcc(c, IT_JNE, instr->dst.val, instr->addr + instr->len,
                        !es->flag[FT_Zero]);
        }
        if (es->flag[FT_Zero] == False) return instr->dst.val;
        return instr->addr + instr->len;

    case IT_JLE:
        if ((es->flag_state[FT_Zero] != CS_STATIC) ||
            (es->flag_state[FT_Sign] != CS_STATIC)) {
            captureJcc(c, IT_JLE, instr->dst.val, instr->addr + instr->len,
                        es->flag[FT_Zero] || es->flag[FT_Sign]);
        }
        if ((es->flag[FT_Zero] == True) ||
            (es->flag[FT_Sign] == True)) return instr->dst.val;
        return instr->addr + instr->len;

    case IT_JG:
        if ((es->flag_state[FT_Zero] != CS_STATIC) ||
            (es->flag_state[FT_Sign] != CS_STATIC)) {
            captureJcc(c, IT_JG, instr->dst.val, instr->addr + instr->len,
                       !es->flag[FT_Zero] && !es->flag[FT_Sign]);
        }
        if ((es->flag[FT_Zero] == False) &&
            (es->flag[FT_Sign] == False)) return instr->dst.val;
        return instr->addr + instr->len;

    case IT_JL:
        if ((es->flag_state[FT_Sign] != CS_STATIC) ||
            (es->flag_state[FT_Overflow] != CS_STATIC)) {
            captureJcc(c, IT_JL, instr->dst.val, instr->addr + instr->len,
                       es->flag[FT_Sign] != es->flag[FT_Overflow]);
        }
        if (es->flag[FT_Sign] != es->flag[FT_Overflow]) return instr->dst.val;
        return instr->addr + instr->len;

    case IT_JGE:
        if ((es->flag_state[FT_Sign] != CS_STATIC) ||
            (es->flag_state[FT_Overflow] != CS_STATIC)) {
            captureJcc(c, IT_JGE, instr->dst.val, instr->addr + instr->len,
                       es->flag[FT_Sign] == es->flag[FT_Overflow]);
        }
        if (es->flag[FT_Sign] == es->flag[FT_Overflow]) return instr->dst.val;
        return instr->addr + instr->len;

    case IT_JP:
        if (es->flag_state[FT_Parity] != CS_STATIC) {
            captureJcc(c, IT_JP, instr->dst.val, instr->addr + instr->len,
                       es->flag[FT_Parity]);
        }
        if (es->flag[FT_Parity] == True) return instr->dst.val;
        return instr->addr + instr->len;

    case IT_JMP:
        assert(instr->dst.type == OT_Imm64);

        // address to jump to
        return instr->dst.val;

    case IT_JMPI:
        getOpValue(&v1, es, &(instr->dst));

        switch(instr->dst.type) {
        case OT_Reg64: break;
        case OT_Ind64:
            getOpAddr(&v2, es, &(instr->dst));
            if (csIsStatic(v2.state)) {
                // Assume indirect jump with target at constant address
                // in memory to be constant: follow resolved PLT entries
                v1.state = CS_STATIC;
            }
            break;
        default: assert(0);
        }

        assert(csIsStatic(v1.state));
        return v1.val; // address to jump to

    case IT_LEA:
        switch(instr->dst.type) {
        case OT_Reg32:
        case OT_Reg64:
            assert(opIsInd(&(instr->src)));
            getOpAddr(&v1, es, &(instr->src));
            if (opValType(&(instr->dst)) == VT_32) {
                v1.val = (uint32_t) v1.val;
                v1.type = VT_32;
            }
            captureLea(c, instr, es, &v1);
            // may overwrite a state needed for correct capturing
            setOpValue(&v1, es, &(instr->dst));
            break;

        default:assert(0);
        }
        break;

    case IT_LEAVE: {
        // leave = mov rbp,rsp + pop rbp

        Instr i;
        // mov rbp,rsp
        initSimpleInstr(&i, IT_MOV);
        copyOperand( &(i.src), getRegOp(VT_64, Reg_BP) );
        copyOperand( &(i.dst), getRegOp(VT_64, Reg_SP) );
        getOpValue(&v1, es, &(i.src));
        setOpValue(&v1, es, &(i.dst));
        captureMov(c, &i, es, &v1);
        // pop rbp
        initUnaryInstr(&i, IT_POP, getRegOp(VT_64, Reg_BP));
        addr = emuValue(es->reg[Reg_SP], VT_64, es->reg_state[Reg_SP]);
        getMemValue(&v1, &addr, es, VT_64, 1);
        setOpValue(&v1, es, &(i.dst));
        es->reg[Reg_SP] += 8;
        if (!csIsStatic(v1.state))
            capture(c, &i);
        break;
    }

    case IT_MOV:
    case IT_MOVSX: // converting move
        switch(instr->src.type) {
        case OT_Reg32:
        case OT_Ind32:
        case OT_Imm32: {
            ValType dst_t = opValType(&(instr->dst));
            assert(dst_t == VT_32 || dst_t == VT_64);
            getOpValue(&v1, es, &(instr->src));
            if (dst_t == VT_64) {
                // also a regular mov may sign-extend: imm32->64
                // assert(instr->type == IT_MOVSX);
                // sign extend lower 32 bit to 64 bit
                v1.val = (int64_t) (int32_t) v1.val;
                v1.type = VT_64;
            }
            captureMov(c, instr, es, &v1);
            setOpValue(&v1, es, &(instr->dst));
            break;
        }

        case OT_Reg64:
        case OT_Ind64:
        case OT_Imm64:
            assert(opValType(&(instr->dst)) == VT_64);
            getOpValue(&v1, es, &(instr->src));
            captureMov(c, instr, es, &v1);
            setOpValue(&v1, es, &(instr->dst));
            break;

        default:assert(0);
        }
        break;

    case IT_NOP:
        // nothing to do
        break;

    case IT_NEG:
        getOpValue(&v1, es, &(instr->dst));
        switch(instr->dst.type) {
        case OT_Reg32:
        case OT_Ind32:
            v1.val = (uint32_t)(- ((int32_t) v1.val));
            break;


        case OT_Reg64:
        case OT_Ind64:
            v1.val = (uint64_t)(- ((int64_t) v1.val));
            break;

        default:assert(0);
        }
        captureUnaryOp(c, instr, es, &v1);
        setOpValue(&v1, es, &(instr->dst));
        break;


    case IT_POP:
        switch(instr->dst.type) {
        case OT_Reg32:
            addr = emuValue(es->reg[Reg_SP], VT_64, es->reg_state[Reg_SP]);
            getMemValue(&v1, &addr, es, VT_32, 1);
            setOpValue(&v1, es, &(instr->dst));
            es->reg[Reg_SP] += 4;
            if (!csIsStatic(v1.state))
                capture(c, instr);
            break;

        case OT_Reg64:
            addr = emuValue(es->reg[Reg_SP], VT_64, es->reg_state[Reg_SP]);
            getMemValue(&v1, &addr, es, VT_64, 1);
            setOpValue(&v1, es, &(instr->dst));
            es->reg[Reg_SP] += 8;
            if (!csIsStatic(v1.state))
                capture(c, instr);
            break;

        default: assert(0);
        }
        break;

    case IT_PUSH:
        switch(instr->dst.type) {
        case OT_Reg32:
            es->reg[Reg_SP] -= 4;
            addr = emuValue(es->reg[Reg_SP], VT_64, es->reg_state[Reg_SP]);
            getOpValue(&v1, es, &(instr->dst));
            setMemValue(&v1, &addr, es, VT_32, 1);
            if (!csIsStatic(v1.state))
                capture(c, instr);
            break;

        case OT_Reg64:
            es->reg[Reg_SP] -= 8;
            addr = emuValue(es->reg[Reg_SP], VT_64, es->reg_state[Reg_SP]);
            getOpValue(&v1, es, &(instr->dst));
            setMemValue(&v1, &addr, es, VT_64, 1);
            if (!csIsStatic(v1.state))
                capture(c, instr);
            break;

        default: assert(0);
        }
        break;

    case IT_RET:
        if (c->addInliningHints) {
            Instr i;
            initSimpleInstr(&i, IT_HINT_RET);
            capture(c, &i);
        }

        es->depth--;
        if (es->depth >= 0) {
            // pop return address from stack
            addr = emuValue(es->reg[Reg_SP], VT_64, es->reg_state[Reg_SP]);
            getMemValue(&v1, &addr, es, VT_64, 1);
            es->reg[Reg_SP] += 8;

            assert(v1.val == es->ret_stack[es->depth]);
            return es->ret_stack[es->depth];
        }
        break;

    case IT_SHL:
        // FIXME: do flags (shifting into CF, set OF)
        getOpValue(&v1, es, &(instr->dst));
        getOpValue(&v2, es, &(instr->src));

        switch(opValType(&(instr->dst))) {
        case VT_32:
            v1.val = (uint32_t) (v1.val << (v2.val & 31));
            break;

        case VT_64:
            v1.val = v1.val << (v2.val & 63);
            break;

        default: assert(0);
        }
        v1.state = combineState(v1.state, v2.state, 0);
        captureBinaryOp(c, instr, es, &v1);
        setOpValue(&v1, es, &(instr->dst));
        break;

    case IT_SUB:
        getOpValue(&v1, es, &(instr->dst));
        getOpValue(&v2, es, &(instr->src));

        vt = opValType(&(instr->dst));
        // sign-extend src/v2 if needed
        if (instr->src.type == OT_Imm8) {
            // sign-extend to 64bit (may be cutoff later)
            v2.val = (int64_t) (int8_t) v2.val;
            v2.type = vt;
        }
        else if (instr->src.type == OT_Imm32 && vt == VT_64) {
            // sign-extend to 64bit (may be cutoff later)
            v2.val = (int64_t) (int32_t) v2.val;
            v2.type = vt;
        }

        setFlagsSub(es, &v1, &v2);
        assert(v1.type == v2.type);

        switch(vt) {
        case VT_32:
            v1.val = ((uint32_t) v1.val - (uint32_t) v2.val);
            break;

        case VT_64:
            v1.val = v1.val - v2.val;
            break;

        default: assert(0);
        }

        v1.state = combineState(v1.state, v2.state, 0);
        // for capturing we need state of original dst, do before setting dst
        captureBinaryOp(c, instr, es, &v1);
        setOpValue(&v1, es, &(instr->dst));
        break;

    case IT_TEST:
        getOpValue(&v1, es, &(instr->dst));
        getOpValue(&v2, es, &(instr->src));

        assert(v1.type == v2.type);
        s = setFlagsBit(es, IT_AND, &v1, &v2, False);
        captureTest(c, instr, es, s);
        break;

    case IT_XOR:
    case IT_OR:
    case IT_AND:
        getOpValue(&v1, es, &(instr->dst));
        getOpValue(&v2, es, &(instr->src));

        assert(v1.type == v2.type);
        v1.state = setFlagsBit(es, instr->type, &v1, &v2,
                               opIsEqual(&(instr->dst), &(instr->src)));
        switch(instr->type) {
        case IT_AND: v1.val = v1.val & v2.val; break;
        case IT_XOR: v1.val = v1.val ^ v2.val; break;
        case IT_OR:  v1.val = v1.val | v2.val; break;
        default: assert(0);
        }
        // for capturing we need state of original dst
        captureBinaryOp(c, instr, es, &v1);
        setOpValue(&v1, es, &(instr->dst));
        break;


    default: assert(0);
    }
    return 0;
}

//----------------------------------------------------------
// Rewrite engine

// FIXME: this always assumes 5 parameters
uint64_t vEmulateAndCapture(Rewriter* c, va_list args)
{
    // calling convention x86-64: parameters are stored in registers
    static Reg parReg[5] = { Reg_DI, Reg_SI, Reg_DX, Reg_CX, Reg_8 };

    int i, esID;
    uint64_t par[5];
    EmuState* es;
    DBB *dbb;
    CBB *cbb;
    Instr* instr;
    uint64_t bb_addr, nextbb_addr;

    par[0] = va_arg(args, uint64_t);
    par[1] = va_arg(args, uint64_t);
    par[2] = va_arg(args, uint64_t);
    par[3] = va_arg(args, uint64_t);
    par[4] = va_arg(args, uint64_t);

#if 0
    // with rewrite(Rewriter *r, ...)
    //
    // setup int parameters for virtual CPU according to x86_64 calling conv.
    // see https://en.wikipedia.org/wiki/X86_calling_conventions
    asm("mov %%rsi, %0;" : "=r" (par[0]) : );
    asm("mov %%rdx, %0;" : "=r" (par[1]) : );
    asm("mov %%rcx, %0;" : "=r" (par[2]) : );
    asm("mov %%r8, %0;"  : "=r" (par[3]) : );
    asm("mov %%r9, %0;"  : "=r" (par[4]) : );
#endif

    if (!c->es)
        c->es = allocEmuState(1024);
    resetEmuState(c->es);
    es = c->es;

    resetCapturing(c);
    if (c->cs)
        c->cs->used = 0;

    for(i=0;i<5;i++) {
        es->reg[parReg[i]] = par[i];
        es->reg_state[parReg[i]] = c->cc ? c->cc->par_state[i] : CS_DYNAMIC;
    }

    es->reg[Reg_SP] = (uint64_t) (es->stackStart + es->stackSize);
    es->reg_state[Reg_SP] = CS_STACKRELATIVE;

    // Pass 1: traverse all paths and generate CBBs

    // push new CBB for c->func (as request to decode and emulate/capture
    // starting at that address)
    esID = saveEmuState(c);
    cbb = getCaptureBB(c, c->func, esID);
    // new CBB has to be first in this rewriter (we start with it in Pass 2)
    assert(cbb = c->capBB);
    pushCaptureBB(c, cbb);
    assert(c->capStackTop == 0);

    // and start with this CBB
    bb_addr = cbb->dec_addr;
    c->currentCapBB = cbb;
    if (c->addInliningHints) {
        // hint: here starts a function, we can assume ABI calling conventions
        Instr i;
        initSimpleInstr(&i, IT_HINT_CALL);
        capture(c, &i);
    }

    if (c->showEmuSteps) {
        printf("Processing BB (%lx|%d)\n", cbb->dec_addr, cbb->esID);
        printStaticEmuState(es, cbb->esID);
    }
    if (c->showEmuState) {
        es->reg[Reg_IP] = bb_addr;
        printEmuState(es);
    }

    while(1) {
        if (c->currentCapBB == 0) {
            // open next yet-to-be-processed CBB
            while(c->capStackTop >= 0) {
                cbb = c->capStack[c->capStackTop];
                if (cbb->endType == IT_None) break;
                // cbb already handled; go to previous item on capture stack
                c->capStackTop--;
            }
            // all paths captured?
            if (c->capStackTop < 0) break;

            assert(cbb != 0);
            assert(cbb->count == 0); // should have no instructions yet
            restoreEmuState(c, cbb->esID);
            bb_addr = cbb->dec_addr;
            c->currentCapBB = cbb;

            if (c->showEmuSteps) {
                printf("Processing BB (%lx|%d), %d BBs in queue\n",
                       cbb->dec_addr, cbb->esID, c->capStackTop);
                printStaticEmuState(es, cbb->esID);
            }
            if (c->showEmuState) {
                es->reg[Reg_IP] = bb_addr;
                printEmuState(es);
            }
        }

        // decode and process instructions starting at bb_addr.
        // note: multiple original BBs may be combined into one CBB
        dbb = brew_decode(c, bb_addr);
        for(i = 0; i < dbb->count; i++) {
            instr = dbb->instr + i;

            if (c->showEmuSteps)
                printf("Emulate '%p: %s'\n",
                       (void*) instr->addr, instr2string(instr, 0));

            // for RIP-relative accesses
            es->reg[Reg_IP] = instr->addr + instr->len;

            nextbb_addr = emulateInstr(c, es, instr);

            if (c->showEmuState) {
                if (nextbb_addr != 0) es->reg[Reg_IP] = nextbb_addr;
                printEmuState(es);
            }

            // side-exit taken?
            if (nextbb_addr != 0) break;
        }
        if (i == dbb->count) {
            // fall through at end of BB
            nextbb_addr = instr->addr + instr->len;
        }
        if (es->depth < 0) {
            // finish this path
            assert(instr->type == IT_RET);
            captureRet(c, instr, es);

            // go to next path to trace
            cbb = popCaptureBB(c);
            cbb->endType = IT_RET;
        }
        bb_addr = nextbb_addr;
    }

    // Pass 2: apply optimization passes to CBBs

    for(i = 0; i < c->capBBCount; i++) {
        cbb = c->capBB + i;
        optPass(c, cbb);
    }

    // Pass 3: generating code for BBs without linking them

    assert(c->capStackTop == -1);
    // start with first CBB created
    pushCaptureBB(c, c->capBB);
    while(c->capStackTop >= 0) {
        cbb = c->capStack[c->capStackTop];
        c->capStackTop--;
        if (cbb->size >= 0) continue;

        assert(c->genOrderCount < GENORDER_MAX);
        c->genOrder[c->genOrderCount++] = cbb;
        generate(c, cbb);

        if (instrIsJcc(cbb->endType)) {
            // FIXME: order according to branch preference
            pushCaptureBB(c, cbb->nextBranch);
            pushCaptureBB(c, cbb->nextFallThrough);
        }
    }

    // Pass 4: determine trailing bytes needed for each BB

    c->genOrder[c->genOrderCount] = 0;
    for(i=0; i < c->genOrderCount; i++) {
        uint8_t* buf;
        int diff;

        cbb = c->genOrder[i];
        buf = useCodeStorage(c->cs, cbb->size);
        cbb->addr2 = (uint64_t) buf;
        if (cbb->size > 0) {
            assert(cbb->count>0);
            memcpy(buf, (char*)cbb->addr1, cbb->size);
        }
        if (!instrIsJcc(cbb->endType)) continue;

        diff = cbb->nextBranch->addr1 - (cbb->addr1 + cbb->size);
        if ((diff > -120) && (diff < 120))
            cbb->genJcc8 = True;
        useCodeStorage(c->cs, cbb->genJcc8 ? 2 : 6);
        if (cbb->nextFallThrough != c->genOrder[i+1]) {
            cbb->genJump = True;
            useCodeStorage(c->cs, 5);
        }
    }

    // Pass 5: fill trailing bytes with jump instructions

    for(i=0; i < c->genOrderCount; i++) {
        uint8_t* buf;
        uint64_t buf_addr;
        int diff;

        cbb = c->genOrder[i];
        if (!instrIsJcc(cbb->endType)) continue;

        buf = (uint8_t*) (cbb->addr2 + cbb->size);
        buf_addr = (uint64_t) buf;
        if (cbb->genJcc8) {
            diff = cbb->nextBranch->addr2 - (buf_addr + 2);
            assert((diff > -128) && (diff < 127));

            switch(cbb->endType) {
            case IT_JE:  buf[0] = 0x74; break;
            case IT_JNE: buf[0] = 0x75; break;
            case IT_JP:  buf[0] = 0x7A; break;
            case IT_JL:  buf[0] = 0x7C; break;
            case IT_JGE: buf[0] = 0x7D; break;
            case IT_JLE: buf[0] = 0x7E; break;
            case IT_JG:  buf[0] = 0x7F; break;
            default: assert(0);
            }
            buf[1] = (int8_t) diff;
            buf += 2;
        }
        else {
            diff = cbb->nextBranch->addr2 - (buf_addr + 6);
            buf[0] = 0x0F;
            switch(cbb->endType) {
            case IT_JE:  buf[1] = 0x84; break;
            case IT_JNE: buf[1] = 0x85; break;
            case IT_JP:  buf[1] = 0x8A; break;
            case IT_JL:  buf[1] = 0x8C; break;
            case IT_JGE: buf[1] = 0x8D; break;
            case IT_JLE: buf[1] = 0x8E; break;
            case IT_JG:  buf[1] = 0x8F; break;
            default: assert(0);
            }
            *(int32_t*)(buf+2) = diff;
            buf += 6;
        }
        if (cbb->genJump) {
            buf_addr = (uint64_t) buf;
            diff = cbb->nextFallThrough->addr2 - (buf_addr + 5);
            buf[0] = 0xE9;
            *(int32_t*)(buf+1) = diff;
            buf += 5;
        }
    }

    // return value according to calling convention
    return es->reg[Reg_AX];
}

uint64_t brew_emulate_capture(Rewriter* r, ...)
{
    uint64_t res;
    va_list argptr;

    va_start(argptr, r);
    res = vEmulateAndCapture(r, argptr);
    va_end(argptr);

    return res;
}

//-----------------------------------------------------------------
// convenience functions, using defaults

Rewriter* defaultRewriter = 0;

Rewriter* getDefaultRewriter()
{
    if (!defaultRewriter)
        defaultRewriter = brew_new();

    return defaultRewriter;
}

void brew_def_verbose(Bool decode, Bool emuState, Bool emuSteps)
{
    brew_verbose(getDefaultRewriter(), decode, emuState, emuSteps);
}

uint64_t brew_rewrite(uint64_t func, ...)
{
    Rewriter* r;
    va_list argptr;

    r = getDefaultRewriter();
    brew_set_function(r, func);

    va_start(argptr, func);
    // throw away result of emulation
    vEmulateAndCapture(r, argptr);
    va_end(argptr);

    return brew_generated_code(r);
}
