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

/* For now, decoder only does x86-64 */

#include "decode.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "common.h"
#include "printer.h"
#include "engine.h"

// decode context
struct _DContext {
    Rewriter* r;

    // decoder position
    uint8_t* f;
    int off;
    uint64_t iaddr; // current instruction start address

    // decoded prefixes
    bool hasRex;
    int rex; // REX prefix
    PrefixSet ps; // detected prefix set
    OpSegOverride segOv; // segment override prefix
    ValType vt; // default operand type (derived from prefixes)

    // decoded instruction parts
    int opc1, opc2;

    // temporaries for decode handler
    Operand o1, o2, o3;
    OperandEncoding oe;
    int digit;
    InstrType it;
    Instr* ii;

    // decoding result
    bool exit;   // control flow change instruction detected
    char* error; // if not-null, an decoding error was detected
};

Instr* nextInstr(Rewriter* r, uint64_t a, int len)
{
    Instr* i = r->decInstr + r->decInstrCount;
    assert(r->decInstrCount < r->decInstrCapacity);
    r->decInstrCount++;

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

Instr* addSimple(Rewriter* r, DContext* c, InstrType it)
{
    uint64_t len = (uint64_t)(c->f + c->off) - c->iaddr;
    Instr* i = nextInstr(r, c->iaddr, len);
    i->type = it;
    i->form = OF_0;

    return i;
}

Instr* addSimpleVType(Rewriter* r, DContext* c, InstrType it, ValType vt)
{
    uint64_t len = (uint64_t)(c->f + c->off) - c->iaddr;
    Instr* i = nextInstr(r, c->iaddr, len);
    i->type = it;
    i->vtype = vt;
    i->form = OF_0;

    return i;
}

Instr* addUnaryOp(Rewriter* r, DContext* c, InstrType it, Operand* o)
{
    uint64_t len = (uint64_t)(c->f + c->off) - c->iaddr;
    Instr* i = nextInstr(r, c->iaddr, len);
    i->type = it;
    i->form = OF_1;
    copyOperand( &(i->dst), o);

    return i;
}

Instr* addBinaryOp(Rewriter* r, DContext* c,
                   InstrType it, ValType vt, Operand* o1, Operand* o2)
{
    if ((vt != VT_None) && (vt != VT_Implicit)) {
        // if we specify an explicit value type, it must match destination
        // 2nd operand does not have to match (e.g. conversion/mask extraction)
        assert(vt == opValType(o1));
    }

    uint64_t len = (uint64_t)(c->f + c->off) - c->iaddr;
    Instr* i = nextInstr(r, c->iaddr, len);
    i->type = it;
    i->form = OF_2;
    i->vtype = vt;
    copyOperand( &(i->dst), o1);
    copyOperand( &(i->src), o2);

    return i;
}

Instr* addTernaryOp(Rewriter* r, DContext* c,
                    InstrType it, Operand* o1, Operand* o2, Operand* o3)
{
    uint64_t len = (uint64_t)(c->f + c->off) - c->iaddr;
    Instr* i = nextInstr(r, c->iaddr, len);
    i->type = it;
    i->form = OF_3;
    copyOperand( &(i->dst), o1);
    copyOperand( &(i->src), o2);
    copyOperand( &(i->src2), o3);

    return i;
}



// for parseModRM: register types (GP: general purpose integer, V: vector)
typedef enum _RegTypes {
    RT_None = 0,
    RT_Op1V = 1,
    RT_Op2V = 2,
    RT_G = 0,        // 1 GP operand
    RT_V = RT_Op1V,  // 1 vector operand
    RT_GG = 0,       // 2 operands, both GP
    RT_VG = RT_Op1V, // 2 ops, 1st V, 2nd GP
    RT_GV = RT_Op2V, // 2 ops, 1st GP, 2nd V
    RT_VV = RT_Op1V | RT_Op2V // 2 ops, both V
} RegTypes;

// Parse MR encoding (r/m,r: op1 is reg or memory operand, op2 is reg/digit),
// or RM encoding (reverse op1/op2 when calling this function).
// Encoding see SDM 2.1
// Input: REX prefix, SegOverride prefix, o1 or o2 may be vector registers
// Fills o1/o2/digit
// Increments offset in context according to parsed number of bytes
static
void parseModRM(DContext* cxt, ValType vt, RegTypes rt,
               Operand* o1, Operand* o2, int* digit)
{
    int modrm, mod, rm, reg; // modRM byte
    int sib, scale, idx, base; // SIB byte
    int64_t disp;
    Reg r;
    OpType ot;
    int hasDisp8 = 0, hasDisp32 = 0;

    modrm = cxt->f[cxt->off++];
    mod = (modrm & 192) >> 6;
    reg = (modrm & 56) >> 3;
    rm = modrm & 7;

    switch(vt) {
    case VT_None: ot = (cxt->rex & REX_MASK_W) ? OT_Reg64 : OT_Reg32; break;
    case VT_8:    ot = OT_Reg8; break;
    case VT_16:   ot = OT_Reg16; break;
    case VT_32:   ot = OT_Reg32; break;
    case VT_64:   ot = OT_Reg64; break;
    case VT_128:  ot = OT_Reg128; assert(rt & RT_Op2V); break;
    case VT_256:  ot = OT_Reg256; assert(rt & RT_Op2V); break;
    default: assert(0);
    }
    // r part: reg or digit, give both back to caller
    if (digit) *digit = reg;
    if (o2) {
        r = ((rt & RT_Op2V) ? Reg_X0 : Reg_AX) + reg;
        if (cxt->rex & REX_MASK_R) r += 8;
        o2->type = ot;
        o2->reg = r;
    }

    if (mod == 3) {
        // r, r
        r = ((rt & RT_Op1V) ? Reg_X0 : Reg_AX) + rm;
        if (cxt->rex & REX_MASK_B) r += 8;
        o1->type = ot;
        o1->reg = r;
        return;
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
        sib = cxt->f[cxt->off++];
        scale = 1 << ((sib & 192) >> 6);
        idx   = (sib & 56) >> 3;
        base  = sib & 7;
        if ((base == 5) && (mod == 0))
            hasDisp32 = 1;
    }

    disp = 0;
    if (hasDisp8) {
        // 8bit disp: sign extend
        disp = *((signed char*) (cxt->f + cxt->off));
        cxt->off++;
    }
    if (hasDisp32) {
        disp = *((int32_t*) (cxt->f + cxt->off));
        cxt->off += 4;
    }

    switch(vt) {
    case VT_None: ot = (cxt->rex & REX_MASK_W) ? OT_Ind64 : OT_Ind32; break;
    case VT_8:    ot = OT_Ind8; break;
    case VT_16:   ot = OT_Ind16; break;
    case VT_32:   ot = OT_Ind32; break;
    case VT_64:   ot = OT_Ind64; break;
    case VT_128:  ot = OT_Ind128; assert(rt & RT_Op1V); break;
    case VT_256:  ot = OT_Ind256; assert(rt & RT_Op1V); break;
    default: assert(0);
    }
    o1->type = ot;
    o1->seg = cxt->segOv;
    o1->scale = scale;
    o1->val = (uint64_t) disp;
    if (scale == 0) {
        r = Reg_AX + rm;
        if (cxt->rex & REX_MASK_B) r += 8;
        o1->reg = ((mod == 0) && (rm == 5)) ? Reg_IP : r;
        return;
    }

    if (cxt->rex & REX_MASK_X) idx += 8;
    r = Reg_AX + idx;
    o1->ireg = (idx == 4) ? Reg_None : r;


    if (cxt->rex & REX_MASK_B) base += 8;
    r = Reg_AX + base;
    o1->reg = ((base == 5) && (mod == 0)) ? Reg_None : r;

    // no need to use SIB if index register not used
    if (o1->ireg == Reg_None) o1->scale = 0;
}

// parse immediate value at current decode context into operand <o>
static
void parseImm(DContext* c, ValType vt, Operand* o, bool realImm64)
{
    switch(vt) {
    case VT_8:
        o->type = OT_Imm8;
        o->val = *(c->f + c->off);
        c->off++;
        break;
    case VT_16:
        o->type = OT_Imm16;
        o->val = *(uint16_t*)(c->f + c->off);
        c->off += 2;
        break;
    case VT_32:
        o->type = OT_Imm32;
        o->val = *(uint32_t*)(c->f + c->off);
        c->off += 4;
        break;
    case VT_64:
        o->type = OT_Imm64;
        if (realImm64) {
            // operand is real 64 immediate
            o->val = *(uint64_t*)(c->f + c->off);
            c->off += 8;
        }
        else {
            // operand is sign-extended from 32bit
            o->val = (int64_t)(*(int32_t*)(c->f + c->off));
            c->off += 4;
        }
        break;
    default:
        assert(0);
    }
}

static
void initDContext(DContext* cxt, Rewriter* r, uint64_t f)
{
    cxt->r = r;
    cxt->f = (uint8_t*) f;
    cxt->off = 0;

    cxt->exit = false;
    cxt->error = 0;
}

// possible prefixes:
// - REX: bits extended 64bit architecture
// - 2E : cs-segment override or branch not taken hint (Jcc)
// - ...
static
void decodePrefixes(DContext* cxt)
{
    // starts a new instruction
    cxt->iaddr = (uint64_t)(cxt->f + cxt->off);
    cxt->rex = 0;
    cxt->hasRex = false;
    cxt->segOv = OSO_None;
    cxt->ps = PS_None;
    cxt->exit = false;
    cxt->error = 0;

    while(1) {
        uint8_t b = cxt->f[cxt->off];
        if ((b >= 0x40) && (b <= 0x4F)) {
            cxt->rex = b & 15;
            cxt->hasRex = true;
        }
        else if (b == 0xF2) cxt->ps |= PS_F2;
        else if (b == 0xF3) cxt->ps |= PS_F3;
        else if (b == 0x66) cxt->ps |= PS_66;
        else if (b == 0x64) cxt->segOv = OSO_UseFS;
        else if (b == 0x65) cxt->segOv = OSO_UseGS;
        else if (b == 0x2E) cxt->ps |= PS_2E;
        else {
            // no further prefixes
            break;
        }
        cxt->off++;
    }

    // default value type (for all instrs with 16/32/64)
    cxt->vt = (cxt->rex & REX_MASK_W) ? VT_64 : VT_32;
    if (cxt->ps & PS_66) cxt->vt = VT_16;
}


// decoding via opcode tables

typedef void (*decode_handler_t)(DContext*);

static decode_handler_t opcTable[256];
static decode_handler_t opcTable0F[256];

static
void addOpc(int opc, decode_handler_t h)
{
    assert((opc>=0) && (opc<=255) && (opc != 0x0F));
    opcTable[opc] = h;
}

static
void addOpc0F(int opc, decode_handler_t h)
{
    assert((opc>=0) && (opc<=255));
    opcTable0F[opc] = h;
}

// opcode decode handlers

// handlers for multi-byte opcodes starting with 0x0F

static
void decode0F_10(DContext* c)
{
    switch(c->ps) {
    case PS_F3:   // movss xmm1,xmm2/m32 (RM)
        c->vt = VT_32;  c->it = IT_MOVSS; break;
    case PS_F2:   // movsd xmm1,xmm2/m64 (RM)
        c->vt = VT_64;  c->it = IT_MOVSD; break;
    case PS_None: // movups xmm1,xmm2/m128 (RM)
        c->vt = VT_128; c->it = IT_MOVUPS; break;
    case PS_66:   // movupd xmm1,xmm2/m128 (RM)
        c->vt = VT_128; c->it = IT_MOVUPD; break;
    default: assert(0);
    }
    parseModRM(c, c->vt, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_None, 0x0F, 0x10, -1);
}

static
void decode0F_11(DContext* c)
{
    switch(c->ps) {
    case PS_F3:   // movss xmm1/m32,xmm2 (MR)
        c->vt = VT_32;  c->it = IT_MOVSS; break;
    case PS_F2:   // movsd xmm1/m64,xmm2 (MR)
        c->vt = VT_64;  c->it = IT_MOVSD; break;
    case PS_None: // movups xmm1/m128,xmm2 (MR)
        c->vt = VT_128; c->it = IT_MOVUPS; break;
    case PS_66:   // movupd xmm1/m128,xmm2 (MR)
        c->vt = VT_128; c->it = IT_MOVUPD; break;
    default: assert(0);
    }
    parseModRM(c, c->vt, RT_VV, &c->o1, &c->o2, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_MR, SC_None, 0x0F, 0x11, -1);
}

static
void decode0F_12(DContext* c)
{
    switch(c->ps) {
    case PS_66:
        // movlpd xmm,m64 (RM) - mov DP FP from m64 to low quadword of xmm
        c->it = IT_MOVLPD; break;
    case PS_None:
        // movlps xmm,m64 (RM) - mov 2SP FP from m64 to low quadword of xmm
        c->it = IT_MOVLPS; break;
    default: assert(0);
    }
    parseModRM(c, VT_64, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_None, 0x0F, 0x12, -1);
}

static
void decode0F_13(DContext* c)
{
    switch(c->ps) {
    case PS_66:
        // movlpd m64,xmm (MR) - mov DP FP from low quadword of xmm to m64
        c->it = IT_MOVLPD; break;
    case PS_None:
        // movlps m64,xmm (MR) - mov 2SP FP from low quadword of xmm to m64
        c->it = IT_MOVLPS; break;
    default: assert(0);
    }
    parseModRM(c, VT_64, RT_VV, &c->o1, &c->o2, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_MR, SC_None, 0x0F, 0x13, -1);
}

static
void decode0F_14(DContext* c)
{
    switch(c->ps) {
    case PS_66:   // unpcklpd xmm1,xmm2/m128 (RM)
        c->it = IT_UNPCKLPD; break;
    case PS_None: // unpcklps xmm1,xmm2/m128 (RM)
        c->it = IT_UNPCKLPS; break;
    default: assert(0);
    }
    parseModRM(c, VT_128, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_None, 0x0F, 0x14, -1);
}

static
void decode0F_15(DContext* c)
{
    switch(c->ps) {
    case PS_66:   // unpckhpd xmm1,xmm2/m128 (RM)
        c->it = IT_UNPCKHPD; break;
    case PS_None: // unpckhps xmm1,xmm2/m128 (RM)
        c->it = IT_UNPCKHPS; break;
    default: assert(0);
    }
    parseModRM(c, VT_128, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_None, 0x0F, 0x15, -1);
}

static
void decode0F_16(DContext* c)
{
    switch(c->ps) {
    case PS_66:
        // movhpd xmm,m64 (RM) - mov DP FP from m64 to high quadword of xmm
        c->it = IT_MOVHPD; break;
    case PS_None:
        // movhps xmm,m64 (RM) - mov 2SP FP from m64 to high quadword of xmm
        c->it = IT_MOVHPS; break;
    default: assert(0);
    }
    parseModRM(c, VT_64, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_None, 0x0F, 0x16, -1);
}

static
void decode0F_17(DContext* c)
{
    switch(c->ps) {
    case PS_66:
        // movhpd m64,xmm (MR) - mov DP FP from high quadword of xmm to m64
        c->it = IT_MOVHPD; break;
    case PS_None:
        // movhps m64,xmm (MR) - mov 2SP FP from high quadword of xmm to m64
        c->it = IT_MOVHPS; break;
    default: assert(0);
    }
    parseModRM(c, VT_64, RT_VV, &c->o1, &c->o2, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_MR, SC_None, 0x0F, 0x17, -1);
}

static
void decode0F_1F(DContext* c)
{
    int digit;
    parseModRM(c, c->vt, RT_G, &c->o1, 0, &digit);
    switch(digit) {
    case 0:
        // 0F 1F /0: nop r/m 16/32
        assert((c->vt == VT_16) || (c->vt == VT_32));
        addUnaryOp(c->r, c, IT_NOP, &c->o1);
        break;

    default:
        addSimple(c->r, c, IT_Invalid);
        break;
    }
}

static
void decode0F_28(DContext* c)
{
    switch(c->ps) {
    case PS_None: // movaps xmm1,xmm2/m128 (RM)
        c->vt = VT_128; c->it = IT_MOVAPS; break;
    case PS_66:   // movapd xmm1,xmm2/m128 (RM)
        c->vt = VT_128; c->it = IT_MOVAPD; break;
    default: assert(0);
    }
    parseModRM(c, c->vt, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_None, 0x0F, 0x28, -1);
}

static
void decode0F_29(DContext* c)
{
    switch(c->ps) {
    case PS_None: // movaps xmm2/m128,xmm1 (MR)
        c->vt = VT_128; c->it = IT_MOVAPS; break;
    case PS_66:   // movapd xmm2/m128,xmm1 (MR)
        c->vt = VT_128; c->it = IT_MOVAPD; break;
    default: assert(0);
    }
    parseModRM(c, c->vt, RT_VV, &c->o1, &c->o2, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_None, 0x0F, 0x29, -1);
}

static
void decode0F_2E(DContext* c)
{
    assert(c->ps & PS_66);
    // ucomisd xmm1,xmm2/m64 (RM)
    parseModRM(c, VT_64, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, IT_UCOMISD, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, PS_66, OE_RM, SC_None, 0x0F, 0x2E, -1);
}

// 0x40: cmovo   r,r/m 16/32/64
// 0x41: cmovno  r,r/m 16/32/64
// 0x42: cmovc   r,r/m 16/32/64
// 0x43: cmovnc  r,r/m 16/32/64
// 0x44: cmovz   r,r/m 16/32/64
// 0x45: cmovnz  r,r/m 16/32/64
// 0x46: cmovbe  r,r/m 16/32/64
// 0x47: cmova   r,r/m 16/32/64
// 0x48: cmovs   r,r/m 16/32/64
// 0x49: cmovns  r,r/m 16/32/64
// 0x4A: cmovp   r,r/m 16/32/64
// 0x4B: cmovnp  r,r/m 16/32/64
// 0x4C: cmovl   r,r/m 16/32/64
// 0x4D: cmovge  r,r/m 16/32/64
// 0x4E: cmovle  r,r/m 16/32/64
// 0x4F: cmovg   r,r/m 16/32/64
static
void decode0F_40(DContext* c)
{
    switch (c->opc2) {
    case 0x40: c->it = IT_CMOVO; break;
    case 0x41: c->it = IT_CMOVNO; break;
    case 0x42: c->it = IT_CMOVC; break;
    case 0x43: c->it = IT_CMOVNC; break;
    case 0x44: c->it = IT_CMOVZ; break;
    case 0x45: c->it = IT_CMOVNZ; break;
    case 0x46: c->it = IT_CMOVBE; break;
    case 0x47: c->it = IT_CMOVA; break;
    case 0x48: c->it = IT_CMOVS; break;
    case 0x49: c->it = IT_CMOVNS; break;
    case 0x4A: c->it = IT_CMOVP; break;
    case 0x4B: c->it = IT_CMOVNP; break;
    case 0x4C: c->it = IT_CMOVL; break;
    case 0x4D: c->it = IT_CMOVGE; break;
    case 0x4E: c->it = IT_CMOVLE; break;
    case 0x4F: c->it = IT_CMOVG; break;
    default: assert(0);
    }
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    addBinaryOp(c->r, c, c->it, c->vt, &c->o1, &c->o2);
}

static
void decode0F_57(DContext* c)
{
    // xorps xmm1,xmm2/m64 (RM)
    parseModRM(c, VT_128, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, IT_XORPS, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, PS_None, OE_RM, SC_None, 0x0F, 0x57, -1);
}

static
void decode0F_58(DContext* c)
{
    switch(c->ps) {
    case PS_F3:   // addss xmm1,xmm2/m32 (RM)
        c->vt = VT_32;  c->it = IT_ADDSS; break;
    case PS_F2:   // addsd xmm1,xmm2/m64 (RM)
        c->vt = VT_64;  c->it = IT_ADDSD; break;
    case PS_None: // addps xmm1,xmm2/m128 (RM)
        c->vt = VT_128; c->it = IT_ADDPS; break;
    case PS_66:   // addpd xmm1,xmm2/m128 (RM)
        c->vt = VT_128; c->it = IT_ADDPD; break;
    default: assert(0);
    }
    parseModRM(c, c->vt, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_None, 0x0F, 0x58, -1);
}

static
void decode0F_59(DContext* c)
{
    switch(c->ps) {
    case PS_F3:   // mulss xmm1,xmm2/m32 (RM)
        c->vt = VT_32;  c->it = IT_MULSS; break;
    case PS_F2:   // mulsd xmm1,xmm2/m64 (RM)
        c->vt = VT_64;  c->it = IT_MULSD; break;
    case PS_None: // mulps xmm1,xmm2/m128 (RM)
        c->vt = VT_128; c->it = IT_MULPS; break;
    case PS_66:   // mulpd xmm1,xmm2/m128 (RM)
        c->vt = VT_128; c->it = IT_MULPD; break;
    default: assert(0);
    }
    parseModRM(c, c->vt, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_None, 0x0F, 0x59, -1);
}

static
void decode0F_5C(DContext* c)
{
    switch(c->ps) {
    case PS_F3:   // subss xmm1,xmm2/m32 (RM)
        c->vt = VT_32;  c->it = IT_SUBSS; break;
    case PS_F2:   // subsd xmm1,xmm2/m64 (RM)
        c->vt = VT_64;  c->it = IT_SUBSD; break;
    case PS_None: // subps xmm1,xmm2/m128 (RM)
        c->vt = VT_128; c->it = IT_SUBPS; break;
    case PS_66:   // subpd xmm1,xmm2/m128 (RM)
        c->vt = VT_128; c->it = IT_SUBPD; break;
    default: assert(0);
    }
    parseModRM(c, c->vt, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_None, 0x0F, 0x5C, -1);
}

static
void decode0F_6E(DContext* c)
{
    if (c->ps == PS_66) {
        // movd/q xmm,r/m 32/64 (RM)
        c->vt = (c->rex & REX_MASK_W) ? VT_64 : VT_32;
        c->it = (c->rex & REX_MASK_W) ? IT_MOVQ : IT_MOVD;
        parseModRM(c, c->vt, RT_GV, &c->o2, &c->o1, 0);
    } else {
        assert(0);
    }
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_dstDyn, 0x0F, 0x6E, -1);
}

static
void decode0F_6F(DContext* c)
{
    switch(c->ps) {
    case PS_F3:
        // movdqu xmm1,xmm2/m128 (RM): move unaligned dqw xmm2 -> xmm1
        c->vt = VT_128; c->it = IT_MOVDQU; break;
    case PS_66:
        // movdqa xmm1,xmm2/m128 (RM): move aligned dqw xmm2 -> xmm1
        c->vt = VT_128; c->it = IT_MOVDQA; break;
    case PS_None:
        // movq mm1,mm2/m64 (RM): Move quadword from mm/m64 to mm.
        c->vt = VT_64;  c->it = IT_MOVQ; break;
    default: assert(0);
    }
    parseModRM(c, c->vt, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_None, 0x0F, 0x6F, -1);
}

static
void decode0F_74(DContext* c)
{
    // pcmpeqb mm,mm/m 64/128 (RM): compare packed bytes
    switch(c->ps) {
    case PS_66:   c->vt = VT_128; break;
    case PS_None: c->vt = VT_64; break;
    default: assert(0);
    }
    parseModRM(c, c->vt, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, IT_PCMPEQB, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_None, 0x0F, 0x74, -1);
}

static
void decode0F_7E(DContext* c)
{
    switch(c->ps) {
    case PS_66:
        // movd/q r/m 32/64,xmm (MR)
        c->oe = OE_MR;
        c->vt = (c->rex & REX_MASK_W) ? VT_64 : VT_32;
        c->it = (c->rex & REX_MASK_W) ? IT_MOVQ : IT_MOVD;
        parseModRM(c, c->vt, RT_GV, &c->o1, &c->o2, 0);
        break;
    case PS_F3:
        // movq xmm1, xmm2/m64 (RM) - move from xmm2/m64 to xmm1
        c->oe = OE_RM;
        c->vt = VT_64;
        c->it = IT_MOVQ;
        parseModRM(c, c->vt, RT_VV, &c->o2, &c->o1, 0);
        break;
    default: assert(0);
    }
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, c->oe, SC_dstDyn, 0x0F, 0x7E, -1);
}

static
void decode0F_7F(DContext* c)
{
    switch(c->ps) {
    case PS_F3:
        // movdqu xmm2/m128,xmm1 (MR)
        // - move unaligned double quadword from xmm1 to xmm2/m128.
        c->vt = VT_128; c->it = IT_MOVDQU; break;
    case PS_66:
        // movdqa xmm2/m128,xmm1 (MR)
        // - move aligned double quadword from xmm1 to xmm2/m128.
        c->vt = VT_128; c->it = IT_MOVDQA; break;
    default: assert(0);
    }
    parseModRM(c, c->vt, RT_VV, &c->o1, &c->o2, 0);
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_MR, SC_None, 0x0F, 0x7F, -1);
}

// 0x80: jo rel32
// 0x81: jno rel32
// 0x82: jc/jb/jnae rel32
// 0x83: jnc/jnb/jae rel32
// 0x84: jz/je rel32
// 0x85: jnz/jne rel32
// 0x86: jbe/jna rel32
// 0x87: ja/jnbe rel32
// 0x88: js rel32
// 0x89: jns rel32
// 0x8A: jp/jpe rel32
// 0x8B: jnp/jpo rel32
// 0x8C: jl/jnge rel32
// 0x8D: jge/jnl rel32
// 0x8E: jle/jng rel32
// 0x8F: jg/jnle rel32
static
void decode0F_80(DContext* c)
{
    c->o1.type = OT_Imm64;
    c->o1.val = (uint64_t) (c->f + c->off + 4 + *(int32_t*)(c->f + c->off));
    c->off += 4;
    switch (c->opc2) {
    case 0x80: c->it = IT_JO; break;
    case 0x81: c->it = IT_JNO; break;
    case 0x82: c->it = IT_JC; break;
    case 0x83: c->it = IT_JNC; break;
    case 0x84: c->it = IT_JZ; break;
    case 0x85: c->it = IT_JNZ; break;
    case 0x86: c->it = IT_JBE; break;
    case 0x87: c->it = IT_JA; break;
    case 0x88: c->it = IT_JS; break;
    case 0x89: c->it = IT_JNS; break;
    case 0x8A: c->it = IT_JP; break;
    case 0x8B: c->it = IT_JNP; break;
    case 0x8C: c->it = IT_JL; break;
    case 0x8D: c->it = IT_JGE; break;
    case 0x8E: c->it = IT_JLE; break;
    case 0x8F: c->it = IT_JG; break;
    default: assert(0);
    }
    c->it = IT_JO + (c->opc2 & 0xf);
    c->ii = addUnaryOp(c->r, c, c->it, &c->o1);
    c->ii->vtype = VT_Implicit; // jump address size is implicit
    c->exit = true;
}

static
void decode0F_AF(DContext* c)
{
    // imul r,rm 16/32/64 (RM), signed mul (d/q)word by r/m
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    addBinaryOp(c->r, c, IT_IMUL, c->vt, &c->o1, &c->o2);
}

static
void decode0F_B6(DContext* c)
{
    // movzbl r16/32/64,r/m8 (RM): move byte to (d)word, zero-extend
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    opOverwriteType(&c->o2, VT_8); // source always 8bit
    addBinaryOp(c->r, c, IT_MOVZX, c->vt, &c->o1, &c->o2);
}

static
void decode0F_B7(DContext* c)
{
    // movzbl r32/64,r/m16 (RM): move word to (d/q)word, zero-extend
    assert((c->vt == VT_32) || (c->vt == VT_64));
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    opOverwriteType(&c->o2, VT_16); // source always 16bit
    addBinaryOp(c->r, c, IT_MOVZX, c->vt, &c->o1, &c->o2);
}

static
void decode0F_BC(DContext* c)
{
    // bsf r,r/m 32/64 (RM): bit scan forward
    c->vt = (c->rex & REX_MASK_W) ? VT_64 : VT_32;
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    addBinaryOp(c->r, c, IT_BSF, c->vt, &c->o1, &c->o2);
}

static
void decode0F_BE(DContext* c)
{
    // movsx r16/32/64,r/m8 (RM): byte to (q/d)word with sign-extension
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    opOverwriteType(&c->o2, VT_8); // source always 8bit
    addBinaryOp(c->r, c, IT_MOVSX, c->vt, &c->o1, &c->o2);
}

static
void decode0F_BF(DContext* c)
{
    // movsx r32/64,r/m16 (RM). word to (q/d)word with sign-extension
    assert((c->vt == VT_32) || (c->vt == VT_64));
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    opOverwriteType(&c->o2, VT_16); // source always 16bit
    addBinaryOp(c->r, c, IT_MOVSX, c->vt, &c->o1, &c->o2);
}

static
void decode0F_D4(DContext* c)
{
    // paddq mm1, mm2/m64 (RM)
    // - add quadword integer mm2/m64 to mm1
    // paddq xmm1, xmm2/m64 (RM)
    // - add packed quadword xmm2/m128 to xmm1
    c->vt = (c->ps & PS_66) ? VT_128 : VT_64;
    parseModRM(c, c->vt, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, IT_PADDQ, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_RM, SC_None, 0x0F, 0xD4, -1);
}

static
void decode0F_D6(DContext* c)
{
    // movq xmm2/m64,xmm1 (MR)
    assert(c->ps == PS_66);
    parseModRM(c, VT_64, RT_VV, &c->o1, &c->o2, 0);
    c->ii = addBinaryOp(c->r, c, IT_MOVQ, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, c->ps, OE_MR, SC_None, 0x0F, 0xD6, -1);
}

static
void decode0F_D7(DContext* c)
{
    // pmovmskb r,mm 64/128 (RM): minimum of packed bytes
    c->vt = (c->ps & PS_66) ? VT_128 : VT_64;
    parseModRM(c, c->vt, RT_VG, &c->o2, &c->o1, 0);
    opOverwriteType(&c->o1, VT_32); // result always 32bit
    c->ii = addBinaryOp(c->r, c, IT_PMOVMSKB, VT_32, &c->o1, &c->o2);
    attachPassthrough(c->ii, (PrefixSet)(c->ps & PS_66), OE_RM, SC_dstDyn,
                      0x0F, 0xD7, -1);
}

static
void decode0F_DA(DContext* c)
{
    // pminub mm,mm/m 64/128 (RM): minimum of packed bytes
    c->vt = (c->ps & PS_66) ? VT_128 : VT_64;
    parseModRM(c, c->vt, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, IT_PMINUB, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, (PrefixSet)(c->ps & PS_66), OE_RM, SC_None,
                      0x0F, 0xDA, -1);
}

static
void decode0F_EF(DContext* c)
{
    // pxor xmm1, xmm2/m 64/128 (RM)
    c->vt = (c->ps & PS_66) ? VT_128 : VT_64;
    parseModRM(c, c->vt, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, IT_PXOR, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, (PrefixSet)(c->ps & PS_66), OE_RM, SC_None,
                      0x0F, 0xEF, -1);
}


// handlers for single-byte opcodes

static
void decode_00(DContext* c)
{
    // 0x00: add r/m8,r8 (MR, dst: r/m, src: r)
    // 0x01: add r/m,r 16/32/64 (MR, dst: r/m, src: r)
    if (c->opc1 == 0x00) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o1, &c->o2, 0);
    addBinaryOp(c->r, c, IT_ADD, c->vt, &c->o1, &c->o2);
}

static
void decode_02(DContext* c)
{
    // 0x02: add r8,r/m8 (RM, dst: r, src: r/m)
    // 0x03: add r,r/m 16/32/64 (RM, dst: r, src: r/m)
    if (c->opc1 == 0x02) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    addBinaryOp(c->r, c, IT_ADD, c->vt, &c->o1, &c->o2);
}

static
void decode_04(DContext* c)
{
    // 0x04: add al,imm8
    // 0x05: add ax/eax/rax,imm16/32/64
    if (c->opc1 == 0x04) c->vt = VT_8;
    parseImm(c, c->vt, &c->o1, false);
    addBinaryOp(c->r, c, IT_ADD, c->vt, getRegOp(c->vt, Reg_AX), &c->o1);
}

static
void decode_08(DContext* c)
{
    // 0x08: or r/m8,r8 (MR)
    // 0x09: or r/m,r 16/32/64 (MR, dst: r/m, src: r)
    if (c->opc1 == 0x08) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o1, &c->o2, 0);
    addBinaryOp(c->r, c, IT_OR, c->vt, &c->o1, &c->o2);
}

static
void decode_0A(DContext* c)
{
    // 0x0A: or r8,r/m8 (RM)
    // 0x0B: or r,r/m 16/32/64 (RM, dst: r, src: r/m)
    if (c->opc1 == 0x0A) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    addBinaryOp(c->r, c, IT_OR, c->vt, &c->o1, &c->o2);
}

static
void decode_0C(DContext* c)
{
    // 0x0C: or al,imm8
    // 0x0D: or ax/eax/rax,imm16/32/32se (se: sign extended)
    if (c->opc1 == 0x0C) c->vt = VT_8;
    parseImm(c, c->vt, &c->o1, false);
    addBinaryOp(c->r, c, IT_OR, c->vt, getRegOp(c->vt, Reg_AX), &c->o1);
}

static
void decode_10(DContext* c)
{
    // 0x10: adc r/m8,r8 (MR, dst: r/m, src: r)
    // 0x11: adc r/m,r 16/32/64 (MR, dst: r/m, src: r)
    if (c->opc1 == 0x10) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o1, &c->o2, 0);
    addBinaryOp(c->r, c, IT_ADC, c->vt, &c->o1, &c->o2);
}

static
void decode_12(DContext* c)
{
    // 0x12: adc r8,r/m8 (RM, dst: r, src: r/m)
    // 0x13: adc r,r/m 16/32/64 (RM, dst: r, src: r/m)
    if (c->opc1 == 0x12) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    addBinaryOp(c->r, c, IT_ADC, c->vt, &c->o1, &c->o2);
}

static
void decode_14(DContext* c)
{
    // 0x14: adc al,imm8
    // 0x15: adc ax/eax/rax,imm16/32/64
    if (c->opc1 == 0x14) c->vt = VT_8;
    parseImm(c, c->vt, &c->o1, false);
    addBinaryOp(c->r, c, IT_ADC, c->vt, getRegOp(c->vt, Reg_AX), &c->o1);
}

static
void decode_18(DContext* c)
{
    // 0x18: sbb r/m8,r8 (MR)
    // 0x19: sbb r/m,r 16/32/64 (MR, dst: r/m, src: r)
    if (c->opc1 == 0x18) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o1, &c->o2, 0);
    addBinaryOp(c->r, c, IT_SBB, c->vt, &c->o1, &c->o2);
}

static
void decode_1A(DContext* c)
{
    // 0x1A: sbb r8,r/m8 (RM)
    // 0x1B: sbb r,r/m 16/32/64 (RM, dst: r, src: r/m)
    if (c->opc1 == 0x1A) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    addBinaryOp(c->r, c, IT_SBB, c->vt, &c->o1, &c->o2);
}

static
void decode_1C(DContext* c)
{
    // 0x1C: sbb al,imm8
    // 0x1D: sbb ax/eax/rax,imm16/32/64
    if (c->opc1 == 0x1C) c->vt = VT_8;
    parseImm(c, c->vt, &c->o1, false);
    addBinaryOp(c->r, c, IT_SBB, c->vt, getRegOp(c->vt, Reg_AX), &c->o1);
}

static
void decode_20(DContext* c)
{
    // 0x20: and r/m8,r8 (MR, dst: r/m, src: r)
    // 0x21: and r/m,r 16/32/64 (MR, dst: r/m, src: r)
    if (c->opc1 == 0x20) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o1, &c->o2, 0);
    addBinaryOp(c->r, c, IT_AND, c->vt, &c->o1, &c->o2);
}

static
void decode_22(DContext* c)
{
    // 0x22: and r8,r/m8 (RM, dst: r, src: r/m)
    // 0x23: and r,r/m 16/32/64 (RM, dst: r, src: r/m)
    if (c->opc1 == 0x22) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    addBinaryOp(c->r, c, IT_AND, c->vt, &c->o1, &c->o2);
}

static
void decode_24(DContext* c)
{
    // 0x24: and al,imm8
    // 0x25: and ax/eax/rax,imm16/32/32se (sign extended)
    if (c->opc1 == 0x24) c->vt = VT_8;
    parseImm(c, c->vt, &c->o1, false);
    addBinaryOp(c->r, c, IT_AND, c->vt, getRegOp(c->vt, Reg_AX), &c->o1);
}

static
void decode_28(DContext* c)
{
    // 0x28: sub r/m8,r8 (MR, dst: r/m, src: r)
    // 0x29: sub r/m,r 16/32/64 (MR)
    if (c->opc1 == 0x28) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o1, &c->o2, 0);
    addBinaryOp(c->r, c, IT_SUB, c->vt, &c->o1, &c->o2);
}

static
void decode_2A(DContext* c)
{
    // 0x2A: sub r8,r/m8 (RM, dst: r, src: r/m)
    // 0x2B: sub r,r/m 16/32/64 (RM)
    if (c->opc1 == 0x2A) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    addBinaryOp(c->r, c, IT_SUB, c->vt, &c->o1, &c->o2);
}

static
void decode_2C(DContext* c)
{
    // 0x2C: sub al,imm8
    // 0x2D: sub ax/eax/rax,imm16/32/32se (sign extended)
    if (c->opc1 == 0x2C) c->vt = VT_8;
    parseImm(c, c->vt, &c->o1, false);
    addBinaryOp(c->r, c, IT_SUB, c->vt, getRegOp(c->vt, Reg_AX), &c->o1);
}

static
void decode_30(DContext* c)
{
    // 0x30: xor r/m8,r8 (MR, dst: r/m, src: r)
    // 0x31: xor r/m,r 16/32/64 (MR, dst: r/m, src: r)
    if (c->opc1 == 0x30) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o1, &c->o2, 0);
    addBinaryOp(c->r, c, IT_XOR, c->vt, &c->o1, &c->o2);
}

static
void decode_32(DContext* c)
{
    // 0x32: xor r8,r/m8 (RM, dst: r, src: r/m)
    // 0x33: xor r,r/m 16/32/64 (RM, dst: r, src: r/m)
    if (c->opc1 == 0x32) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    addBinaryOp(c->r, c, IT_XOR, c->vt, &c->o1, &c->o2);
}

static
void decode_34(DContext* c)
{
    // 0x34: xor al,imm8
    // 0x35: xor ax/eax/rax,imm16/32/32se (sign extended)
    if (c->opc1 == 0x34) c->vt = VT_8;
    parseImm(c, c->vt, &c->o1, false);
    addBinaryOp(c->r, c, IT_XOR, c->vt, getRegOp(c->vt, Reg_AX), &c->o1);
}

static
void decode_38(DContext* c)
{
    // 0x38: cmp r/m8,r8 (RM, dst: r, src: r/m)
    // 0x39: cmp r/m,r 16/32/64 (MR)
    if (c->opc1 == 0x38) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o1, &c->o2, 0);
    addBinaryOp(c->r, c, IT_CMP, c->vt, &c->o1, &c->o2);
}

static
void decode_3A(DContext* c)
{
    // 0x3A: cmp r8,r/m8 (RM, dst: r, src: r/m)
    // 0x3B: cmp r,r/m 16/32/64 (RM)
    if (c->opc1 == 0x3A) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    addBinaryOp(c->r, c, IT_CMP, c->vt, &c->o1, &c->o2);
}

static
void decode_3C(DContext* c)
{
    // 0x3C: cmp al,imm8
    // 0x3D: cmp eax,imm32
    if (c->opc1 == 0x3C) c->vt = VT_8;
    parseImm(c, c->vt, &c->o1, false);
    addBinaryOp(c->r, c, IT_CMP, c->vt, getRegOp(c->vt, Reg_AX), &c->o1);
}

static
void decode_50(DContext* c)
{
    // 50-57: push
    Reg reg = Reg_AX + (c->opc1 - 0x50);
    c->vt = VT_64;
    if (c->rex & REX_MASK_B) reg += 8;
    if (c->ps & PS_66) c->vt = VT_16;
    addUnaryOp(c->r, c, IT_PUSH, getRegOp(c->vt, reg));
}

static
void decode_58(DContext* c)
{
    // 58-5F: pop
    Reg reg = Reg_AX + (c->opc1 - 0x58);
    c->vt = VT_64;
    if (c->rex & REX_MASK_B) reg += 8;
    if (c->ps & PS_66) c->vt = VT_16;
    addUnaryOp(c->r, c, IT_POP, getRegOp(c->vt, reg));
}

static
void decode_63(DContext* c)
{
    // movsx r64,r/m32 (RM) mov with sign extension
    assert(c->rex & REX_MASK_W);
    parseModRM(c, VT_None, RT_GG, &c->o2, &c->o1, 0);
    // src is 32 bit
    switch(c->o2.type) {
    case OT_Reg64: c->o2.type = OT_Reg32; break;
    case OT_Ind64: c->o2.type = OT_Ind32; break;
    default: assert(0);
    }
    addBinaryOp(c->r, c, IT_MOVSX, VT_None, &c->o1, &c->o2);
}

static
void decode_68(DContext* c)
{
    // push imm32
    parseImm(c, VT_32, &c->o1, false);
    addUnaryOp(c->r, c, IT_PUSH, &c->o1);
}

static
void decode_69(DContext* c)
{
    // imul r,r/m16/32/64,imm16/32 (RMI)
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    parseImm(c, c->vt, &c->o3, false); // with 64bit use imm32
    addTernaryOp(c->r, c, IT_IMUL, &c->o1, &c->o2, &c->o3);
}

static
void decode_6A(DContext* c)
{
    // push imm8
    parseImm(c, VT_8, &c->o1, false);
    addUnaryOp(c->r, c, IT_PUSH, &c->o1);
}

static
void decode_6B(DContext* c)
{
    // imul r,r/m16/32/64,imm8 (RMI)
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    parseImm(c, VT_8, &c->o3, false);
    addTernaryOp(c->r, c, IT_IMUL, &c->o1, &c->o2, &c->o3);
}

static
void decode_70(DContext* c)
{
    // 0x70: jo rel8
    // 0x71: jno rel8
    // 0x72: jc/jb/jnae rel8
    // 0x73: jnc/jnb/jae rel8
    // 0x74: jz/je rel8
    // 0x75: jnz/jne rel8
    // 0x76: jbe/jna rel8
    // 0x77: ja/jnbe rel8
    // 0x78: js rel8
    // 0x79: jns rel8
    // 0x7A: jp/jpe rel8
    // 0x7B: jnp/jpo rel8
    // 0x7C: jl/jnge rel8
    // 0x7D: jge/jnl rel8
    // 0x7E: jle/jng rel8
    // 0x7F: jg/jnle rel8
    c->o1.type = OT_Imm64;
    c->o1.val = (uint64_t) (c->f + c->off + 1 + *(int8_t*)(c->f + c->off));
    c->off += 1;
    switch (c->opc1) {
    case 0x70: c->it = IT_JO; break;
    case 0x71: c->it = IT_JNO; break;
    case 0x72: c->it = IT_JC; break;
    case 0x73: c->it = IT_JNC; break;
    case 0x74: c->it = IT_JZ; break;
    case 0x75: c->it = IT_JNZ; break;
    case 0x76: c->it = IT_JBE; break;
    case 0x77: c->it = IT_JA; break;
    case 0x78: c->it = IT_JS; break;
    case 0x79: c->it = IT_JNS; break;
    case 0x7A: c->it = IT_JP; break;
    case 0x7B: c->it = IT_JNP; break;
    case 0x7C: c->it = IT_JL; break;
    case 0x7D: c->it = IT_JGE; break;
    case 0x7E: c->it = IT_JLE; break;
    case 0x7F: c->it = IT_JG; break;
    default: assert(0);
    }
    c->ii = addUnaryOp(c->r, c, c->it, &c->o1);
    c->ii->vtype = VT_Implicit; // jump address size is implicit
    c->exit = true;


}

static
void decode_80(DContext* c)
{
    // add/or/... r/m and imm8
    c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o1, 0, &c->digit);
    switch(c->digit) {
    case 0: c->it = IT_ADD; break; // 80/0: add r/m8,imm8
    case 1: c->it = IT_OR;  break; // 80/1: or  r/m8,imm8
    case 2: c->it = IT_ADC; break; // 80/2: adc r/m8,imm8
    case 3: c->it = IT_SBB; break; // 80/3: sbb r/m8,imm8
    case 4: c->it = IT_AND; break; // 80/4: and r/m8,imm8
    case 5: c->it = IT_SUB; break; // 80/5: sub r/m8,imm8
    case 6: c->it = IT_XOR; break; // 80/6: xor r/m8,imm8
    case 7: c->it = IT_CMP; break; // 80/7: cmp r/m8,imm8
    default: assert(0);
    }
    parseImm(c, c->vt, &c->o2, false);
    addBinaryOp(c->r, c, c->it, c->vt, &c->o1, &c->o2);


}

static
void decode_81(DContext* c)
{
    // default value type 16/32/64, imm16 (for 16), imm32 (for 32/64)
    parseModRM(c, c->vt, RT_GG, &c->o1, 0, &c->digit);
    switch(c->digit) {
    case 0: c->it = IT_ADD; break; // 81/0: add r/m 16/32/64, imm16/32
    case 1: c->it = IT_OR;  break; // 81/1: or  r/m 16/32/64, imm16/32
    case 2: c->it = IT_ADC; break; // 81/2: adc r/m 16/32/64, imm16/32
    case 3: c->it = IT_SBB; break; // 81/3: sbb r/m 16/32/64, imm16/32
    case 4: c->it = IT_AND; break; // 81/4: and r/m 16/32/64, imm16/32
    case 5: c->it = IT_SUB; break; // 81/5: sub r/m 16/32/64, imm16/32
    case 6: c->it = IT_XOR; break; // 81/6: xor r/m 16/32/64, imm16/32
    case 7: c->it = IT_CMP; break; // 81/7: cmp r/m 16/32/64, imm16/32
    default: assert(0);
    }
    parseImm(c, c->vt, &c->o2, false);
    addBinaryOp(c->r, c, c->it, c->vt, &c->o1, &c->o2);


}

static
void decode_83(DContext* c)
{
    parseModRM(c, c->vt, RT_GG, &c->o1, 0, &c->digit);
    // add/or/... r/m and sign-extended imm8
    switch(c->digit) {
    case 0: c->it = IT_ADD; break; // 83/0: add r/m 16/32/64, imm8
    case 1: c->it = IT_OR;  break; // 83/1: or  r/m 16/32/64, imm8
    case 2: c->it = IT_ADC; break; // 83/2: adc r/m 16/32/64, imm8
    case 3: c->it = IT_SBB; break; // 83/3: sbb r/m 16/32/64, imm8
    case 4: c->it = IT_AND; break; // 83/4: and r/m 16/32/64, imm8
    case 5: c->it = IT_SUB; break; // 83/5: sub r/m 16/32/64, imm8
    case 6: c->it = IT_XOR; break; // 83/6: xor r/m 16/32/64, imm8
    case 7: c->it = IT_CMP; break; // 83/7: cmp r/m 16/32/64, imm8
    default: assert(0);
    }
    parseImm(c, VT_8, &c->o2, false);
    addBinaryOp(c->r, c, c->it, c->vt, &c->o1, &c->o2);


}

static
void decode_84(DContext* c)
{
    // 0x84: test r/m,r 8 (MR) - AND r8 with r/m8; set SF, ZF, PF
    // FIXME: We do not assert on use of AH/BH/CH/DH (not supported)
    // 0x85: test r/m,r 16/32/64 (dst: r/m, src: r)
    if (c->opc1 == 0x84) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o1, &c->o2, 0);
    addBinaryOp(c->r, c, IT_TEST, c->vt, &c->o1, &c->o2);
}

static
void decode_88(DContext* c)
{
    // 0x88: mov r/m8,r8 (MR)
    // 0x89: mov r/m,r 16/32/64 (MR - dst: r/m, src: r)
    if (c->opc1 == 0x88) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o1, &c->o2, 0);
    addBinaryOp(c->r, c, IT_MOV, c->vt, &c->o1, &c->o2);
}

static
void decode_8A(DContext* c)
{
    // 0x8A: mov r8,r/m8,r8 (RM)
    // 0x8B: mov r,r/m 16/32/64 (RM - dst: r, src: r/m)
    if (c->opc1 == 0x8A) c->vt = VT_8;
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    addBinaryOp(c->r, c, IT_MOV, c->vt, &c->o1, &c->o2);
}

static
void decode_8D(DContext* c)
{
    // lea r16/32/64,m (RM)
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
    assert(opIsInd(&c->o2)); // TODO: bad code error
    addBinaryOp(c->r, c, IT_LEA, c->vt, &c->o1, &c->o2);
}

static
void decode_8F(DContext* c)
{
    parseModRM(c, c->vt, RT_G, &c->o1, 0, &c->digit);
    switch(c->digit) {
    case 0: // pop r/m 16/64
        // default operand type is 64, not 32
        if (c->vt == VT_32)
            opOverwriteType(&c->o1, VT_64);
        else
            assert(c->vt == VT_16);
        addUnaryOp(c->r, c, IT_POP, &c->o1);
        break;

    default:
        addSimple(c->r, c, IT_Invalid);
        break;
    }
}

static
void decode_90(DContext* cxt)
{
    // nop
    addSimple(cxt->r, cxt, IT_NOP);
}

static
void decode_98(DContext* c)
{
    // cltq (Intel: cdqe - sign-extend eax to rax)
    addSimpleVType(c->r, c, IT_CLTQ,
                   (c->rex & REX_MASK_W) ? VT_64 : VT_32);
}

static
void decode_99(DContext* c)
{
    // cqto (Intel: cqo - sign-extend rax to rdx/rax, eax to edx/eax)
    c->vt = (c->rex & REX_MASK_W) ? VT_128 : VT_64;
    addSimpleVType(c->r, c, IT_CQTO, c->vt);
}

static
void decode_A8(DContext* c)
{
    // 0xA8: test al,imm8
    // 0xA9: test ax/eax/rax,imm16/32/32se (se: sign extended)
    if (c->opc1 == 0xA8) c->vt = VT_8;
    parseImm(c, c->vt, &c->o1, false);
    addBinaryOp(c->r, c, IT_TEST, c->vt, getRegOp(c->vt, Reg_AX), &c->o1);
}

static
void decode_B0(DContext* c)
{
    // B0-B7: mov r8,imm8
    // B8-BF: mov r32/64,imm32/64
    if ((c->opc1 >= 0xB0) && (c->opc1 <= 0xB7)) c->vt = VT_8;
    c->o1.reg = Reg_AX + (c->opc1 & 7);
    if (c->rex & REX_MASK_B) c->o1.reg += 8;
    c->o1.type = getGPRegOpType(c->vt);
    parseImm(c, c->vt, &c->o2, true);
    addBinaryOp(c->r, c, IT_MOV, c->vt, &c->o1, &c->o2);
}

static
void decode_C0(DContext* c)
{
    // 1st op 8bit
    c->vt = VT_8;
    parseModRM(c, c->vt, RT_G, &c->o1, 0, &c->digit);
    // 2nd op imm8
    parseImm(c, VT_8, &c->o2, false);
    switch(c->digit) {
    case 4: // shl r/m8,imm8 (MI) (= sal)
        addBinaryOp(c->r, c, IT_SHL, c->vt, &c->o1, &c->o2); break;
    case 5: // shr r/m8,imm8 (MI)
        addBinaryOp(c->r, c, IT_SHR, c->vt, &c->o1, &c->o2); break;
    case 7: // sar r/m8,imm8 (MI)
        addBinaryOp(c->r, c, IT_SAR, c->vt, &c->o1, &c->o2); break;
    default:
        addSimple(c->r, c, IT_Invalid);
        break;
    }


}

static
void decode_C1(DContext* c)
{
    // 1st op 16/32/64
    parseModRM(c, c->vt, RT_G, &c->o1, 0, &c->digit);
    // 2nd op imm8
    parseImm(c, VT_8, &c->o2, false);
    switch(c->digit) {
    case 4: // shl r/m 16/32/64,imm8 (MI) (= sal)
        addBinaryOp(c->r, c, IT_SHL, c->vt, &c->o1, &c->o2); break;
    case 5: // shr r/m 16/32/64,imm8 (MI)
        addBinaryOp(c->r, c, IT_SHR, c->vt, &c->o1, &c->o2); break;
    case 7: // sar r/m 16/32/64,imm8 (MI)
        addBinaryOp(c->r, c, IT_SAR, c->vt, &c->o1, &c->o2); break;
    default:
        addSimple(c->r, c, IT_Invalid);
        break;
    }
}

static
void decode_C3(DContext* c)
{
    // ret
    addSimple(c->r, c, IT_RET);
    c->exit = true;
}

static
void decode_C6(DContext* c)
{
    c->vt = VT_8; // all sub-opcodes use 8bit operand type
    parseModRM(c, c->vt, RT_G, &c->o1, 0, &c->digit);
    switch(c->digit) {
    case 0: // mov r/m8, imm8
        parseImm(c, c->vt, &c->o2, false);
        addBinaryOp(c->r, c, IT_MOV, c->vt, &c->o1, &c->o2);
        break;
    default: assert(0);
    }
}

static
void decode_C7(DContext* c)
{
    // for 16/32/64
    parseModRM(c, c->vt, RT_G, &c->o1, 0, &c->digit);
    switch(c->digit) {
    case 0: // mov r/m 16/32/64, imm16/32/32se (sign extended)
        parseImm(c, c->vt, &c->o2, false);
        addBinaryOp(c->r, c, IT_MOV, c->vt, &c->o1, &c->o2);
        break;
    default: assert(0);
    }
}

static
void decode_C9(DContext* c)
{
    // leave ( = mov rbp,rsp + pop rbp)
    addSimple(c->r, c, IT_LEAVE);
}

static
void decode_D0(DContext* c)
{
    c->vt = VT_8;
    parseModRM(c, c->vt, RT_G, &c->o1, 0, &c->digit);
    switch(c->digit) {
    case 4: // shl r/m8,1 (M1) (= sal)
        addUnaryOp(c->r, c, IT_SHL, &c->o1); break;
    case 5: // shr r/m8,1 (M1)
        addUnaryOp(c->r, c, IT_SHR, &c->o1); break;
    case 7: // sar r/m8,1 (M1)
        addUnaryOp(c->r, c, IT_SAR, &c->o1); break;
    default:
        addSimple(c->r, c, IT_Invalid); break;
    }
}

static
void decode_D1(DContext* c)
{
    // for 16/32/64
    parseModRM(c, c->vt, RT_G, &c->o1, 0, &c->digit);
    switch(c->digit) {
    case 4: // shl r/m16/32/64,1 (M1) (= sal)
        addUnaryOp(c->r, c, IT_SHL, &c->o1); break;
    case 5: // shr r/m16/32/64,1 (M1)
        addUnaryOp(c->r, c, IT_SHR, &c->o1); break;
    case 7: // sar r/m16/32/64,1 (M1)
        addUnaryOp(c->r, c, IT_SAR, &c->o1); break;
    default:
        addSimple(c->r, c, IT_Invalid); break;
    }
}

static
void decode_D2(DContext* c)
{
    c->vt = VT_8;
    parseModRM(c, c->vt, RT_G, &c->o1, 0, &c->digit);
    setRegOp(&c->o2, VT_8, Reg_CX);
    switch(c->digit) {
    case 4: // shl r/m8,cl (MC16/32/64) (= sal)
        addBinaryOp(c->r, c, IT_SHL, c->vt, &c->o1, &c->o2); break;
    case 5: // shr r/m8,cl (MC)
        addBinaryOp(c->r, c, IT_SHR, c->vt, &c->o1, &c->o2); break;
    case 7: // sar r/m8,cl (MC)
        addBinaryOp(c->r, c, IT_SAR, c->vt, &c->o1, &c->o2); break;
    default:
        addSimple(c->r, c, IT_Invalid); break;
    }
}

static
void decode_D3(DContext* c)
{
    // for 16/32/64
    parseModRM(c, c->vt, RT_G, &c->o1, 0, &c->digit);
    setRegOp(&c->o2, VT_8, Reg_CX);
    switch(c->digit) {
    case 4: // shl r/m16/32/64,cl (MC) (= sal)
        addBinaryOp(c->r, c, IT_SHL, c->vt, &c->o1, &c->o2); break;
    case 5: // shr r/m16/32/64,cl (MC)
        addBinaryOp(c->r, c, IT_SHR, c->vt, &c->o1, &c->o2); break;
    case 7: // sar r/m16/32/64,cl (MC)
        addBinaryOp(c->r, c, IT_SAR, c->vt, &c->o1, &c->o2); break;
    default:
        addSimple(c->r, c, IT_Invalid); break;
    }
}

static
void decode_E8(DContext* c)
{
    // call rel32
    c->o1.type = OT_Imm64;
    c->o1.val = (uint64_t) (c->f + c->off + 4 + *(int32_t*)(c->f + c->off));
    c->off += 4;
    addUnaryOp(c->r, c, IT_CALL, &c->o1);
    c->exit = true;
}

static
void decode_E9(DContext* c)
{
    // jmp rel32: relative, displacement relative to next instruction
    c->o1.type = OT_Imm64;
    c->o1.val = (uint64_t) (c->f + c->off + 4 + *(int32_t*)(c->f + c->off));
    c->off += 4;
    addUnaryOp(c->r, c, IT_JMP, &c->o1);
    c->exit = true;
}

static
void decode_EB(DContext* c)
{
    // jmp rel8: relative, displacement relative to next instruction
    c->o1.type = OT_Imm64;
    c->o1.val = (uint64_t) (c->f + c->off + 1 + *(int8_t*)(c->f + c->off));
    c->off += 1;
    addUnaryOp(c->r, c, IT_JMP, &c->o1);
    c->exit = true;
}

static
void decode_F6(DContext* c)
{
    // source always 8bit
    c->vt = VT_8;
    parseModRM(c, c->vt, RT_G, &c->o1, 0, &c->digit);
    switch(c->digit) {
    case 0: // test r/m8,imm8 (MI)
        parseImm(c, c->vt, &c->o2, false);
        addBinaryOp(c->r, c, IT_TEST, c->vt, &c->o1, &c->o2);
        break;
    case 2: // not r/m8
        addUnaryOp(c->r, c, IT_NOT, &c->o1); break;
    case 3: // neg r/m8
        addUnaryOp(c->r, c, IT_NEG, &c->o1); break;
    case 4: // mul r/m8 (unsigned mul ax by r/m8)
        addUnaryOp(c->r, c, IT_MUL, &c->o1); break;
    case 5: // imul r/m8 (signed mul ax/eax/rax by r/m8)
        addUnaryOp(c->r, c, IT_IMUL, &c->o1); break;
    case 6: // div r/m8 (unsigned div ax by r/m8, rem/quot in ah:al)
        addUnaryOp(c->r, c, IT_DIV, &c->o1); break;
    case 7: // idiv r/m8 (signed div ax by r/m8, rem/quot in ah:al)
        addUnaryOp(c->r, c, IT_IDIV1, &c->o1); break;
    default: assert(0);
    }
}

static
void decode_F7(DContext* c)
{
    parseModRM(c, c->vt, RT_GG, &c->o1, 0, &c->digit);
    switch(c->digit) {
    case 0: // test r/m16/32/64,imm16/32/32se (MI)
        parseImm(c, c->vt, &c->o2, false);
        addBinaryOp(c->r, c, IT_TEST, c->vt, &c->o1, &c->o2);
        break;
    case 2: // not r/m 16/32/64
        addUnaryOp(c->r, c, IT_NOT, &c->o1); break;
    case 3: // neg r/m 16/32/64
        addUnaryOp(c->r, c, IT_NEG, &c->o1); break;
    case 4: // mul r/m 16/32/64 (unsigned mul ax/eax/rax by r/m)
        addUnaryOp(c->r, c, IT_MUL, &c->o1); break;
    case 5: // imul r/m 16/32/64 (signed mul ax/eax/rax by r/m)
        addUnaryOp(c->r, c, IT_IMUL, &c->o1); break;
    case 6: // div r/m 16/32/64 (unsigned div dx:ax/edx:eax/rdx:rax by r/m)
        addUnaryOp(c->r, c, IT_DIV, &c->o1); break;
    case 7: // idiv r/m 16/32/64 (signed div dx:ax/edx:eax/rdx:rax by r/m)
        addUnaryOp(c->r, c, IT_IDIV1, &c->o1); break;
    default: assert(0);
    }
}


static
void decode_FE(DContext* c)
{
    parseModRM(c, VT_8, RT_G, &c->o1, 0, &c->digit);
    switch(c->digit) {
    case 0: // inc r/m8
        addUnaryOp(c->r, c, IT_INC, &c->o1); break;
    case 1: // dec r/m8
        addUnaryOp(c->r, c, IT_DEC, &c->o1); break;
    default: assert(0);
    }
}

static
void decode_FF(DContext* c)
{
    parseModRM(c, c->vt, RT_G, &c->o1, 0, &c->digit);
    switch(c->digit) {
    case 0: // inc r/m 16/32/64
        addUnaryOp(c->r, c, IT_INC, &c->o1); break;
    case 1: // dec r/m 16/32/64
        addUnaryOp(c->r, c, IT_DEC, &c->o1); break;

    case 2:
        // call r/m64
        assert(c->vt == VT_64); // only 64bit target allowed in 64bit mode
        addUnaryOp(c->r, c, IT_CALL, &c->o1);
        c->exit = true;
        break;

    case 4:
        // jmp* r/m64: absolute indirect
        assert(c->rex == 0);
        opOverwriteType(&c->o1, VT_64);
        addUnaryOp(c->r, c, IT_JMPI, &c->o1);
        c->exit = true;
        break;

    case 6: // push r/m 16/64
        // default operand type is 64, not 32
        if (c->vt == VT_32)
            opOverwriteType(&c->o1, VT_64);
        else
            assert(c->vt == VT_16);
        addUnaryOp(c->r, c, IT_PUSH, &c->o1);
        break;

    default:
        addSimple(c->r, c, IT_Invalid);
        break;
    }
}

static
void initDecodeTables(void)
{
    static bool done = false;
    if (done) return;

    for(int i = 0; i<256; i++) {
        opcTable[i] = 0;
        opcTable0F[i] = 0;
    }

    addOpc0F(0x10, decode0F_10);
    addOpc0F(0x11, decode0F_11);
    addOpc0F(0x12, decode0F_12);
    addOpc0F(0x13, decode0F_13);
    addOpc0F(0x14, decode0F_14);
    addOpc0F(0x15, decode0F_15);
    addOpc0F(0x16, decode0F_16);
    addOpc0F(0x17, decode0F_17);
    addOpc0F(0x1F, decode0F_1F);
    addOpc0F(0x28, decode0F_28);
    addOpc0F(0x29, decode0F_29);
    addOpc0F(0x2E, decode0F_2E);

    addOpc0F(0x40, decode0F_40);
    addOpc0F(0x41, decode0F_40);
    addOpc0F(0x42, decode0F_40);
    addOpc0F(0x43, decode0F_40);
    addOpc0F(0x44, decode0F_40);
    addOpc0F(0x45, decode0F_40);
    addOpc0F(0x46, decode0F_40);
    addOpc0F(0x47, decode0F_40);
    addOpc0F(0x48, decode0F_40);
    addOpc0F(0x49, decode0F_40);
    addOpc0F(0x4A, decode0F_40);
    addOpc0F(0x4B, decode0F_40);
    addOpc0F(0x4C, decode0F_40);
    addOpc0F(0x4D, decode0F_40);
    addOpc0F(0x4E, decode0F_40);
    addOpc0F(0x4F, decode0F_40);

    addOpc0F(0x57, decode0F_57);
    addOpc0F(0x58, decode0F_58);
    addOpc0F(0x59, decode0F_59);
    addOpc0F(0x5C, decode0F_5C);
    addOpc0F(0x6E, decode0F_6E);
    addOpc0F(0x6F, decode0F_6F);
    addOpc0F(0x74, decode0F_74);
    addOpc0F(0x7E, decode0F_7E);
    addOpc0F(0x7F, decode0F_7F);

    addOpc0F(0x80, decode0F_80);
    addOpc0F(0x81, decode0F_80);
    addOpc0F(0x82, decode0F_80);
    addOpc0F(0x83, decode0F_80);
    addOpc0F(0x84, decode0F_80);
    addOpc0F(0x85, decode0F_80);
    addOpc0F(0x86, decode0F_80);
    addOpc0F(0x87, decode0F_80);
    addOpc0F(0x88, decode0F_80);
    addOpc0F(0x8A, decode0F_80);
    addOpc0F(0x8B, decode0F_80);
    addOpc0F(0x8C, decode0F_80);
    addOpc0F(0x8D, decode0F_80);
    addOpc0F(0x8E, decode0F_80);
    addOpc0F(0x8F, decode0F_80);

    addOpc0F(0xAF, decode0F_AF);
    addOpc0F(0xB6, decode0F_B6);
    addOpc0F(0xB7, decode0F_B7);
    addOpc0F(0xBC, decode0F_BC);
    addOpc0F(0xBE, decode0F_BE);
    addOpc0F(0xBF, decode0F_BF);
    addOpc0F(0xD4, decode0F_D4);
    addOpc0F(0xD6, decode0F_D6);
    addOpc0F(0xD7, decode0F_D7);
    addOpc0F(0xDA, decode0F_DA);
    addOpc0F(0xEF, decode0F_EF);

    addOpc(0x00, decode_00);
    addOpc(0x01, decode_00);
    addOpc(0x02, decode_02);
    addOpc(0x03, decode_02);
    addOpc(0x04, decode_04);
    addOpc(0x05, decode_04);
    addOpc(0x08, decode_08);
    addOpc(0x09, decode_08);
    addOpc(0x0A, decode_0A);
    addOpc(0x0B, decode_0A);
    addOpc(0x0C, decode_0C);
    addOpc(0x0D, decode_0C);
    addOpc(0x10, decode_10);
    addOpc(0x11, decode_10);
    addOpc(0x12, decode_12);
    addOpc(0x13, decode_12);
    addOpc(0x14, decode_14);
    addOpc(0x15, decode_14);
    addOpc(0x18, decode_18);
    addOpc(0x19, decode_18);
    addOpc(0x1A, decode_1A);
    addOpc(0x1B, decode_1A);
    addOpc(0x1C, decode_1C);
    addOpc(0x1D, decode_1C);
    addOpc(0x20, decode_20);
    addOpc(0x21, decode_20);
    addOpc(0x22, decode_22);
    addOpc(0x23, decode_22);
    addOpc(0x24, decode_24);
    addOpc(0x25, decode_24);
    addOpc(0x28, decode_28);
    addOpc(0x29, decode_28);
    addOpc(0x2A, decode_2A);
    addOpc(0x2B, decode_2A);
    addOpc(0x2C, decode_2C);
    addOpc(0x2D, decode_2C);
    addOpc(0x30, decode_30);
    addOpc(0x31, decode_30);
    addOpc(0x32, decode_32);
    addOpc(0x33, decode_32);
    addOpc(0x34, decode_34);
    addOpc(0x35, decode_34);
    addOpc(0x38, decode_38);
    addOpc(0x39, decode_38);
    addOpc(0x3A, decode_3A);
    addOpc(0x3B, decode_3A);
    addOpc(0x3C, decode_3C);
    addOpc(0x3D, decode_3C);

    addOpc(0x50, decode_50);
    addOpc(0x51, decode_50);
    addOpc(0x52, decode_50);
    addOpc(0x53, decode_50);
    addOpc(0x54, decode_50);
    addOpc(0x55, decode_50);
    addOpc(0x56, decode_50);
    addOpc(0x57, decode_50);
    addOpc(0x58, decode_58);
    addOpc(0x59, decode_58);
    addOpc(0x5A, decode_58);
    addOpc(0x5B, decode_58);
    addOpc(0x5C, decode_58);
    addOpc(0x5D, decode_58);
    addOpc(0x5E, decode_58);
    addOpc(0x5F, decode_58);

    addOpc(0x63, decode_63);
    addOpc(0x68, decode_68);
    addOpc(0x69, decode_69);
    addOpc(0x6A, decode_6A);
    addOpc(0x6B, decode_6B);

    addOpc(0x70, decode_70);
    addOpc(0x71, decode_70);
    addOpc(0x72, decode_70);
    addOpc(0x73, decode_70);
    addOpc(0x74, decode_70);
    addOpc(0x75, decode_70);
    addOpc(0x76, decode_70);
    addOpc(0x77, decode_70);
    addOpc(0x78, decode_70);
    addOpc(0x79, decode_70);
    addOpc(0x7A, decode_70);
    addOpc(0x7B, decode_70);
    addOpc(0x7C, decode_70);
    addOpc(0x7D, decode_70);
    addOpc(0x7E, decode_70);
    addOpc(0x7F, decode_70);

    addOpc(0x80, decode_80);
    addOpc(0x81, decode_81);
    addOpc(0x83, decode_83);
    addOpc(0x84, decode_84);
    addOpc(0x85, decode_84);
    addOpc(0x88, decode_88);
    addOpc(0x89, decode_88);
    addOpc(0x8A, decode_8A);
    addOpc(0x8B, decode_8A);
    addOpc(0x8D, decode_8D);
    addOpc(0x8F, decode_8F);

    addOpc(0x90, decode_90);
    addOpc(0x98, decode_98);
    addOpc(0x99, decode_99);
    addOpc(0xA8, decode_A8);
    addOpc(0xA9, decode_A8);

    addOpc(0xB0, decode_B0);
    addOpc(0xB1, decode_B0);
    addOpc(0xB2, decode_B0);
    addOpc(0xB3, decode_B0);
    addOpc(0xB4, decode_B0);
    addOpc(0xB5, decode_B0);
    addOpc(0xB6, decode_B0);
    addOpc(0xB7, decode_B0);
    addOpc(0xB8, decode_B0);
    addOpc(0xB9, decode_B0);
    addOpc(0xBA, decode_B0);
    addOpc(0xBB, decode_B0);
    addOpc(0xBC, decode_B0);
    addOpc(0xBD, decode_B0);
    addOpc(0xBE, decode_B0);
    addOpc(0xBF, decode_B0);

    addOpc(0xC0, decode_C0);
    addOpc(0xC1, decode_C1);
    addOpc(0xC3, decode_C3);
    addOpc(0xC6, decode_C6);
    addOpc(0xC7, decode_C7);
    addOpc(0xC9, decode_C9);
    addOpc(0xD0, decode_D0);
    addOpc(0xD1, decode_D1);
    addOpc(0xD2, decode_D2);
    addOpc(0xD3, decode_D3);
    addOpc(0xE8, decode_E8);
    addOpc(0xE9, decode_E9);
    addOpc(0xEB, decode_EB);
    addOpc(0xF6, decode_F6);
    addOpc(0xF7, decode_F7);
    addOpc(0xFE, decode_FE);
    addOpc(0xFF, decode_FF);
}

// decode the basic block starting at f (automatically triggered by emulator)
DBB* dbrew_decode(Rewriter* r, uint64_t f)
{
    DContext cxt;
    decode_handler_t handler;
    int i, old_icount, opc;
    DBB* dbb;

    if (f == 0) return 0; // nothing to decode
    if (r->decBB == 0) initRewriter(r);
    initDecodeTables();

    // already decoded?
    for(i = 0; i < r->decBBCount; i++)
        if (r->decBB[i].addr == f) return &(r->decBB[i]);

    // start decoding of new BB beginning at f
    assert(r->decBBCount < r->decBBCapacity);
    dbb = &(r->decBB[r->decBBCount]);
    r->decBBCount++;
    dbb->addr = f;
    dbb->fc = config_find_function(r, f);
    dbb->count = 0;
    dbb->size = 0;
    dbb->instr = r->decInstr + r->decInstrCount;
    old_icount = r->decInstrCount;

    if (r->showDecoding)
        printf("Decoding BB %s ...\n", prettyAddress(f, dbb->fc));

    initDContext(&cxt, r, f);

    while(!cxt.exit) {
        decodePrefixes(&cxt);

        // parse opcode, use opcode tables or switch-case handler
        opc = cxt.f[cxt.off++];
        cxt.opc1 = opc;
        if (opc == 0x0F) {
            // opcode starting with 0x0F
            opc = cxt.f[cxt.off++];
            cxt.opc2 = opc;
            handler =  opcTable0F[opc];
            if (handler)
                (*handler)(&cxt);
            else
                addSimple(r, &cxt, IT_Invalid);
            continue;
        }

        handler =  opcTable[opc];
        if (handler)
            (*handler)(&cxt);
        else
            addSimple(r, &cxt, IT_Invalid);
    }

    assert(dbb->addr == dbb->instr->addr);
    dbb->count = r->decInstrCount - old_icount;
    dbb->size = cxt.off;

    if (r->showDecoding)
        dbrew_print_decoded(dbb);

    return dbb;
}

