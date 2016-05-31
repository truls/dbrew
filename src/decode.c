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
    // decoder position
    uint8_t* fp;
    int off;
    uint64_t iaddr; // current instruction start address

    // decoded prefixes
    bool hasRex;
    int rex; // REX prefix
    PrefixSet ps; // detected prefix set
    OpSegOverride segOv; // segment override prefix
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
    uint64_t len = (uint64_t)(c->fp + c->off) - c->iaddr;
    Instr* i = nextInstr(r, c->iaddr, len);
    i->type = it;
    i->form = OF_0;

    return i;
}

Instr* addSimpleVType(Rewriter* r, DContext* c, InstrType it, ValType vt)
{
    uint64_t len = (uint64_t)(c->fp + c->off) - c->iaddr;
    Instr* i = nextInstr(r, c->iaddr, len);
    i->type = it;
    i->vtype = vt;
    i->form = OF_0;

    return i;
}

Instr* addUnaryOp(Rewriter* r, DContext* c, InstrType it, Operand* o)
{
    uint64_t len = (uint64_t)(c->fp + c->off) - c->iaddr;
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

    uint64_t len = (uint64_t)(c->fp + c->off) - c->iaddr;
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
    uint64_t len = (uint64_t)(c->fp + c->off) - c->iaddr;
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

    modrm = cxt->fp[cxt->off++];
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
        sib = cxt->fp[cxt->off++];
        scale = 1 << ((sib & 192) >> 6);
        idx   = (sib & 56) >> 3;
        base  = sib & 7;
        if ((base == 5) && (mod == 0))
            hasDisp32 = 1;
    }

    disp = 0;
    if (hasDisp8) {
        // 8bit disp: sign extend
        disp = *((signed char*) (cxt->fp + cxt->off));
        cxt->off++;
    }
    if (hasDisp32) {
        disp = *((int32_t*) (cxt->fp + cxt->off));
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
        o->val = *(c->fp + c->off);
        c->off++;
        break;
    case VT_16:
        o->type = OT_Imm16;
        o->val = *(uint16_t*)(c->fp + c->off);
        c->off += 2;
        break;
    case VT_32:
        o->type = OT_Imm32;
        o->val = *(uint32_t*)(c->fp + c->off);
        c->off += 4;
        break;
    case VT_64:
        o->type = OT_Imm64;
        if (realImm64) {
            // operand is real 64 immediate
            o->val = *(uint64_t*)(c->fp + c->off);
            c->off += 8;
        }
        else {
            // operand is sign-extended from 32bit
            o->val = (int64_t)(*(int32_t*)(c->fp + c->off));
            c->off += 4;
        }
        break;
    default:
        assert(0);
    }
}

static
void initDContext(DContext* cxt, uint64_t f)
{
    cxt->fp = (uint8_t*) f;
    cxt->off = 0;
    cxt->rex = 0;
    cxt->hasRex = false;
    cxt->segOv = OSO_None;
    cxt->ps = PS_None;
}

// possible prefixes:
// - REX: bits extended 64bit architecture
// - 2E : cs-segment override or branch not taken hint (Jcc)
// - ...
static
void decodePrefixes(DContext* cxt)
{
    // starts a new instruction
    cxt->iaddr = (uint64_t)(cxt->fp + cxt->off);
    while(1) {
        uint8_t b = cxt->fp[cxt->off];
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
            return;
        }
        cxt->off++;
    }
}

// Decode multi-byte opcode starting with 0x0F.
// Parameters:
//  <vt> default operand type, <exit>: set to true for control flow change
static
void decode0F(Rewriter* r, DContext* cxt, ValType vt, bool* exit)
{
    int opc2, digit;
    Operand o1, o2;
    OperandEncoding oe;
    InstrType it;
    Instr* ii;

    opc2 = cxt->fp[cxt->off++];
    switch(opc2) {
    case 0x10:
        switch(cxt->ps) {
        case PS_F3:   // movss xmm1,xmm2/m32 (RM)
            vt = VT_32;  it = IT_MOVSS; break;
        case PS_F2:   // movsd xmm1,xmm2/m64 (RM)
            vt = VT_64;  it = IT_MOVSD; break;
        case PS_None: // movups xmm1,xmm2/m128 (RM)
            vt = VT_128; it = IT_MOVUPS; break;
        case PS_66:   // movupd xmm1,xmm2/m128 (RM)
            vt = VT_128; it = IT_MOVUPD; break;
        default: assert(0);
        }
        parseModRM(cxt, vt, RT_VV, &o2, &o1, 0);
        ii = addBinaryOp(r, cxt, it, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, cxt->ps, OE_RM, SC_None, 0x0F, 0x10, -1);
        break;

    case 0x11:
        switch(cxt->ps) {
        case PS_F3:   // movss xmm1/m32,xmm2 (MR)
            vt = VT_32;  it = IT_MOVSS; break;
        case PS_F2:   // movsd xmm1/m64,xmm2 (MR)
            vt = VT_64;  it = IT_MOVSD; break;
        case PS_None: // movups xmm1/m128,xmm2 (MR)
            vt = VT_128; it = IT_MOVUPS; break;
        case PS_66:   // movupd xmm1/m128,xmm2 (MR)
            vt = VT_128; it = IT_MOVUPD; break;
        default: assert(0);
        }
        parseModRM(cxt, vt, RT_VV, &o1, &o2, 0);
        ii = addBinaryOp(r, cxt, it, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, cxt->ps, OE_MR, SC_None, 0x0F, 0x11, -1);
        break;

    case 0x1F:
        parseModRM(cxt, vt, RT_G, &o1, 0, &digit);
        switch(digit) {
        case 0:
            // 0F 1F /0: nop r/m 16/32
            assert((vt == VT_16) || (vt == VT_32));
            addUnaryOp(r, cxt, IT_NOP, &o1);
            break;

        default:
            addSimple(r, cxt, IT_Invalid);
            break;
        }
        break;

    case 0x28:
        switch(cxt->ps) {
        case PS_None: // movaps xmm1,xmm2/m128 (RM)
            vt = VT_128; it = IT_MOVAPS; break;
        case PS_66:   // movapd xmm1,xmm2/m128 (RM)
            vt = VT_128; it = IT_MOVAPD; break;
        default: assert(0);
        }
        parseModRM(cxt, vt, RT_VV, &o2, &o1, 0);
        ii = addBinaryOp(r, cxt, it, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, cxt->ps, OE_RM, SC_None, 0x0F, 0x28, -1);
        break;

    case 0x29:
        switch(cxt->ps) {
        case PS_None: // movaps xmm2/m128,xmm1 (MR)
            vt = VT_128; it = IT_MOVAPS; break;
        case PS_66:   // movapd xmm2/m128,xmm1 (MR)
            vt = VT_128; it = IT_MOVAPD; break;
        default: assert(0);
        }
        parseModRM(cxt, vt, RT_VV, &o1, &o2, 0);
        ii = addBinaryOp(r, cxt, it, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, cxt->ps, OE_RM, SC_None, 0x0F, 0x29, -1);
        break;

    case 0x2E:
        assert(cxt->ps & PS_66);
        // ucomisd xmm1,xmm2/m64 (RM)
        parseModRM(cxt, VT_64, RT_VV, &o2, &o1, 0);
        ii = addBinaryOp(r, cxt, IT_UCOMISD, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, PS_66, OE_RM, SC_None, 0x0F, 0x2E, -1);
        break;

    case 0x40: // cmovo   r,r/m 16/32/64
    case 0x41: // cmovno  r,r/m 16/32/64
    case 0x42: // cmovc   r,r/m 16/32/64
    case 0x43: // cmovnc  r,r/m 16/32/64
    case 0x44: // cmovz   r,r/m 16/32/64
    case 0x45: // cmovnz  r,r/m 16/32/64
    case 0x46: // cmovbe  r,r/m 16/32/64
    case 0x47: // cmova   r,r/m 16/32/64
    case 0x48: // cmovs   r,r/m 16/32/64
    case 0x49: // cmovns  r,r/m 16/32/64
    case 0x4A: // cmovp   r,r/m 16/32/64
    case 0x4B: // cmovnp  r,r/m 16/32/64
    case 0x4C: // cmovl   r,r/m 16/32/64
    case 0x4D: // cmovge  r,r/m 16/32/64
    case 0x4E: // cmovle  r,r/m 16/32/64
    case 0x4F: // cmovg   r,r/m 16/32/64
        switch (opc2) {
        case 0x40: it = IT_CMOVO; break;
        case 0x41: it = IT_CMOVNO; break;
        case 0x42: it = IT_CMOVC; break;
        case 0x43: it = IT_CMOVNC; break;
        case 0x44: it = IT_CMOVZ; break;
        case 0x45: it = IT_CMOVNZ; break;
        case 0x46: it = IT_CMOVBE; break;
        case 0x47: it = IT_CMOVA; break;
        case 0x48: it = IT_CMOVS; break;
        case 0x49: it = IT_CMOVNS; break;
        case 0x4A: it = IT_CMOVP; break;
        case 0x4B: it = IT_CMOVNP; break;
        case 0x4C: it = IT_CMOVL; break;
        case 0x4D: it = IT_CMOVGE; break;
        case 0x4E: it = IT_CMOVLE; break;
        case 0x4F: it = IT_CMOVG; break;
        default: assert(0);
        }
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        addBinaryOp(r, cxt, it, vt, &o1, &o2);
        break;

    case 0x57:
        // xorps xmm1,xmm2/m64 (RM)
        parseModRM(cxt, VT_128, RT_VV, &o2, &o1, 0);
        ii = addBinaryOp(r, cxt, IT_XORPS, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, PS_None, OE_RM, SC_None, 0x0F, 0x57, -1);
        break;

    case 0x58:
        switch(cxt->ps) {
        case PS_F3:   // addss xmm1,xmm2/m32 (RM)
            vt = VT_32;  it = IT_ADDSS; break;
        case PS_F2:   // addsd xmm1,xmm2/m64 (RM)
            vt = VT_64;  it = IT_ADDSD; break;
        case PS_None: // addps xmm1,xmm2/m128 (RM)
            vt = VT_128; it = IT_ADDPS; break;
        case PS_66:   // addpd xmm1,xmm2/m128 (RM)
            vt = VT_128; it = IT_ADDPD; break;
        default: assert(0);
        }
        parseModRM(cxt, vt, RT_VV, &o2, &o1, 0);
        ii = addBinaryOp(r, cxt, it, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, cxt->ps, OE_RM, SC_None, 0x0F, 0x58, -1);
        break;

    case 0x59:
        switch(cxt->ps) {
        case PS_F3:   // mulss xmm1,xmm2/m32 (RM)
            vt = VT_32;  it = IT_MULSS; break;
        case PS_F2:   // mulsd xmm1,xmm2/m64 (RM)
            vt = VT_64;  it = IT_MULSD; break;
        case PS_None: // mulps xmm1,xmm2/m128 (RM)
            vt = VT_128; it = IT_MULPS; break;
        case PS_66:   // mulpd xmm1,xmm2/m128 (RM)
            vt = VT_128; it = IT_MULPD; break;
        default: assert(0);
        }
        parseModRM(cxt, vt, RT_VV, &o2, &o1, 0);
        ii = addBinaryOp(r, cxt, it, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, cxt->ps, OE_RM, SC_None, 0x0F, 0x59, -1);
        break;

    case 0x5C:
        switch(cxt->ps) {
        case PS_F3:   // subss xmm1,xmm2/m32 (RM)
            vt = VT_32;  it = IT_SUBSS; break;
        case PS_F2:   // subsd xmm1,xmm2/m64 (RM)
            vt = VT_64;  it = IT_SUBSD; break;
        case PS_None: // subps xmm1,xmm2/m128 (RM)
            vt = VT_128; it = IT_SUBPS; break;
        case PS_66:   // subpd xmm1,xmm2/m128 (RM)
            vt = VT_128; it = IT_SUBPD; break;
        default: assert(0);
        }
        parseModRM(cxt, vt, RT_VV, &o2, &o1, 0);
        ii = addBinaryOp(r, cxt, it, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, cxt->ps, OE_RM, SC_None, 0x0F, 0x5C, -1);
        break;

    case 0x6E:
        if (cxt->ps == PS_66) {
            // movd/q xmm,r/m 32/64 (RM)
            vt = (cxt->rex & REX_MASK_W) ? VT_64 : VT_32;
            it = (cxt->rex & REX_MASK_W) ? IT_MOVQ : IT_MOVD;
            parseModRM(cxt, vt, RT_GV, &o2, &o1, 0);
        } else {
            assert(0);
        }
        ii = addBinaryOp(r, cxt, it, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, cxt->ps, OE_RM, SC_dstDyn, 0x0F, 0x6E, -1);
        break;

    case 0x6F:
        switch(cxt->ps) {
        case PS_F3:
            // movdqu xmm1,xmm2/m128 (RM): move unaligned dqw xmm2 -> xmm1
            vt = VT_128; it = IT_MOVDQU; break;
        case PS_66:
            // movdqa xmm1,xmm2/m128 (RM): move aligned dqw xmm2 -> xmm1
            vt = VT_128; it = IT_MOVDQA; break;
        case PS_None:
            // movq mm1,mm2/m64 (RM): Move quadword from mm/m64 to mm.
            vt = VT_64;  it = IT_MOVQ; break;
        default: assert(0);
        }
        parseModRM(cxt, vt, RT_VV, &o2, &o1, 0);
        ii = addBinaryOp(r, cxt, it, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, cxt->ps, OE_RM, SC_None, 0x0F, 0x6F, -1);
        break;

    case 0x74:
        // pcmpeqb mm,mm/m 64/128 (RM): compare packed bytes
        switch(cxt->ps) {
        case PS_66:   vt = VT_128; break;
        case PS_None: vt = VT_64; break;
        default: assert(0);
        }
        parseModRM(cxt, vt, RT_VV, &o2, &o1, 0);
        ii = addBinaryOp(r, cxt, IT_PCMPEQB, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, cxt->ps, OE_RM, SC_None, 0x0F, 0x74, -1);
        break;

    case 0x7E:
        switch(cxt->ps) {
        case PS_66:
            // movd/q r/m 32/64,xmm (MR)
            oe = OE_MR;
            vt = (cxt->rex & REX_MASK_W) ? VT_64 : VT_32;
            it = (cxt->rex & REX_MASK_W) ? IT_MOVQ : IT_MOVD;
            parseModRM(cxt, vt, RT_GV, &o1, &o2, 0);
            break;
        case PS_F3:
            // movq xmm1, xmm2/m64 (RM) - move from xmm2/m64 to xmm1
            oe = OE_RM;
            vt = VT_64;
            it = IT_MOVQ;
            parseModRM(cxt, vt, RT_VV, &o2, &o1, 0);
            break;
        default: assert(0);
        }
        ii = addBinaryOp(r, cxt, it, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, cxt->ps, oe, SC_dstDyn, 0x0F, 0x7E, -1);
        break;

    case 0x7F:
        switch(cxt->ps) {
        case PS_F3:
            // movdqu xmm2/m128,xmm1 (MR)
            // - move unaligned double quadword from xmm1 to xmm2/m128.
            vt = VT_128; it = IT_MOVDQU; break;
        case PS_66:
            // movdqa xmm2/m128,xmm1 (MR)
            // - move aligned double quadword from xmm1 to xmm2/m128.
            vt = VT_128; it = IT_MOVDQA; break;
        default: assert(0);
        }
        parseModRM(cxt, vt, RT_VV, &o1, &o2, 0);
        ii = addBinaryOp(r, cxt, it, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, cxt->ps, OE_MR, SC_None, 0x0F, 0x7F, -1);
        break;

    case 0x80: // jo rel32
    case 0x81: // jno rel32
    case 0x82: // jc/jb/jnae rel32
    case 0x83: // jnc/jnb/jae rel32
    case 0x84: // jz/je rel32
    case 0x85: // jnz/jne rel32
    case 0x86: // jbe/jna rel32
    case 0x87: // ja/jnbe rel32
    case 0x88: // js rel32
    case 0x89: // jns rel32
    case 0x8A: // jp/jpe rel32
    case 0x8B: // jnp/jpo rel32
    case 0x8C: // jl/jnge rel32
    case 0x8D: // jge/jnl rel32
    case 0x8E: // jle/jng rel32
    case 0x8F: // jg/jnle rel32
        o1.type = OT_Imm64;
        o1.val = (uint64_t) (cxt->fp + cxt->off + 4 + *(int32_t*)(cxt->fp + cxt->off));
        cxt->off += 4;
        switch (opc2) {
        case 0x80: it = IT_JO; break;
        case 0x81: it = IT_JNO; break;
        case 0x82: it = IT_JC; break;
        case 0x83: it = IT_JNC; break;
        case 0x84: it = IT_JZ; break;
        case 0x85: it = IT_JNZ; break;
        case 0x86: it = IT_JBE; break;
        case 0x87: it = IT_JA; break;
        case 0x88: it = IT_JS; break;
        case 0x89: it = IT_JNS; break;
        case 0x8A: it = IT_JP; break;
        case 0x8B: it = IT_JNP; break;
        case 0x8C: it = IT_JL; break;
        case 0x8D: it = IT_JGE; break;
        case 0x8E: it = IT_JLE; break;
        case 0x8F: it = IT_JG; break;
        default: assert(0);
        }
        it = IT_JO + (opc2 & 0xf);
        ii = addUnaryOp(r, cxt, it, &o1);
        ii->vtype = VT_Implicit; // jump address size is implicit
        *exit = true;
        break;

    case 0xAF:
        // imul r,rm 16/32/64 (RM), signed mul (d/q)word by r/m
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        addBinaryOp(r, cxt, IT_IMUL, vt, &o1, &o2);
        break;

    case 0xB6:
        // movzbl r16/32/64,r/m8 (RM): move byte to (d)word, zero-extend
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        opOverwriteType(&o2, VT_8); // source always 8bit
        addBinaryOp(r, cxt, IT_MOVZX, vt, &o1, &o2);
        break;

    case 0xB7:
        // movzbl r32/64,r/m16 (RM): move word to (d/q)word, zero-extend
        assert((vt == VT_32) || (vt == VT_64));
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        opOverwriteType(&o2, VT_16); // source always 16bit
        addBinaryOp(r, cxt, IT_MOVZX, vt, &o1, &o2);
        break;

    case 0xBC:
        // bsf r,r/m 32/64 (RM): bit scan forward
        vt = (cxt->rex & REX_MASK_W) ? VT_64 : VT_32;
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        addBinaryOp(r, cxt, IT_BSF, vt, &o1, &o2);
        break;

    case 0xBE:
        // movsx r16/32/64,r/m8 (RM): byte to (q/d)word with sign-extension
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        opOverwriteType(&o2, VT_8); // source always 8bit
        addBinaryOp(r, cxt, IT_MOVSX, vt, &o1, &o2);
        break;

    case 0xBF:
        // movsx r32/64,r/m16 (RM). word to (q/d)word with sign-extension
        assert((vt == VT_32) || (vt == VT_64));
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        opOverwriteType(&o2, VT_16); // source always 16bit
        addBinaryOp(r, cxt, IT_MOVSX, vt, &o1, &o2);
        break;

    case 0xD4:
        // paddq mm1, mm2/m64 (RM)
        // - add quadword integer mm2/m64 to mm1
        // paddq xmm1, xmm2/m64 (RM)
        // - add packed quadword xmm2/m128 to xmm1
        vt = (cxt->ps & PS_66) ? VT_128 : VT_64;
        parseModRM(cxt, vt, RT_VV, &o2, &o1, 0);
        ii = addBinaryOp(r, cxt, IT_PADDQ, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, cxt->ps, OE_RM, SC_None, 0x0F, 0xD4, -1);
        break;

    case 0xD7:
        // pmovmskb r,mm 64/128 (RM): minimum of packed bytes
        vt = (cxt->ps & PS_66) ? VT_128 : VT_64;
        parseModRM(cxt, vt, RT_VG, &o2, &o1, 0);
        opOverwriteType(&o1, VT_32); // result always 32bit
        ii = addBinaryOp(r, cxt, IT_PMOVMSKB, VT_32, &o1, &o2);
        attachPassthrough(ii, (PrefixSet)(cxt->ps & PS_66), OE_RM, SC_dstDyn,
                          0x0F, 0xD7, -1);
        break;

    case 0xDA:
        // pminub mm,mm/m 64/128 (RM): minimum of packed bytes
        vt = (cxt->ps & PS_66) ? VT_128 : VT_64;
        parseModRM(cxt, vt, RT_VV, &o2, &o1, 0);
        ii = addBinaryOp(r, cxt, IT_PMINUB, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, (PrefixSet)(cxt->ps & PS_66), OE_RM, SC_None,
                          0x0F, 0xDA, -1);
        break;


    case 0xEF:
        // pxor xmm1, xmm2/m 64/128 (RM)
        vt = (cxt->ps & PS_66) ? VT_128 : VT_64;
        parseModRM(cxt, vt, RT_VV, &o2, &o1, 0);
        ii = addBinaryOp(r, cxt, IT_PXOR, VT_Implicit, &o1, &o2);
        attachPassthrough(ii, (PrefixSet)(cxt->ps & PS_66), OE_RM, SC_None,
                          0x0F, 0xEF, -1);
        break;

    default:
        addSimple(r, cxt, IT_Invalid);
        break;
    }
}

// Decode instruction with parsed prefixes.
// Parameters:
//  <vt> default operand type, <exit>: set to true for control flow change
static
void decode(Rewriter* r, DContext* cxt, ValType vt, bool* exit)
{
    int opc, digit;
    Operand o1, o2, o3;
    Reg reg;
    InstrType it;
    Instr* ii;

    opc = cxt->fp[cxt->off++];
    switch(opc) {

    case 0x00: // add r/m8,r8 (MR, dst: r/m, src: r)
    case 0x01: // add r/m,r 16/32/64 (MR, dst: r/m, src: r)
        if (opc == 0x00) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o1, &o2, 0);
        addBinaryOp(r, cxt, IT_ADD, vt, &o1, &o2);
        break;

    case 0x02: // add r8,r/m8 (RM, dst: r, src: r/m)
    case 0x03: // add r,r/m 16/32/64 (RM, dst: r, src: r/m)
        if (opc == 0x02) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        addBinaryOp(r, cxt, IT_ADD, vt, &o1, &o2);
        break;

    case 0x04: // add al,imm8
    case 0x05: // add ax/eax/rax,imm16/32/64
        if (opc == 0x04) vt = VT_8;
        parseImm(cxt, vt, &o1, false);
        addBinaryOp(r, cxt, IT_ADD, vt, getRegOp(vt, Reg_AX), &o1);
        break;

    case 0x08: // or r/m8,r8 (MR)
    case 0x09: // or r/m,r 16/32/64 (MR, dst: r/m, src: r)
        if (opc == 0x08) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o1, &o2, 0);
        addBinaryOp(r, cxt, IT_OR, vt, &o1, &o2);
        break;

    case 0x0A: // or r8,r/m8 (RM)
    case 0x0B: // or r,r/m 16/32/64 (RM, dst: r, src: r/m)
        if (opc == 0x0A) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        addBinaryOp(r, cxt, IT_OR, vt, &o1, &o2);
        break;

    case 0x0C: // or al,imm8
    case 0x0D: // or ax/eax/rax,imm16/32/32se (se: sign extended)
        if (opc == 0x0C) vt = VT_8;
        parseImm(cxt, vt, &o1, false);
        addBinaryOp(r, cxt, IT_OR, vt, getRegOp(vt, Reg_AX), &o1);
        break;

    case 0x0F:
        // multi-byte opcode
        decode0F(r, cxt, vt, exit);
        break;

    case 0x10: // adc r/m8,r8 (MR, dst: r/m, src: r)
    case 0x11: // adc r/m,r 16/32/64 (MR, dst: r/m, src: r)
        if (opc == 0x10) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o1, &o2, 0);
        addBinaryOp(r, cxt, IT_ADC, vt, &o1, &o2);
        break;

    case 0x12: // adc r8,r/m8 (RM, dst: r, src: r/m)
    case 0x13: // adc r,r/m 16/32/64 (RM, dst: r, src: r/m)
        if (opc == 0x12) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        addBinaryOp(r, cxt, IT_ADC, vt, &o1, &o2);
        break;

    case 0x14: // adc al,imm8
    case 0x15: // adc ax/eax/rax,imm16/32/64
        if (opc == 0x14) vt = VT_8;
        parseImm(cxt, vt, &o1, false);
        addBinaryOp(r, cxt, IT_ADC, vt, getRegOp(vt, Reg_AX), &o1);
        break;

    case 0x18: // sbb r/m8,r8 (MR)
    case 0x19: // sbb r/m,r 16/32/64 (MR, dst: r/m, src: r)
        if (opc == 0x18) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o1, &o2, 0);
        addBinaryOp(r, cxt, IT_SBB, vt, &o1, &o2);
        break;

    case 0x1A: // sbb r8,r/m8 (RM)
    case 0x1B: // sbb r,r/m 16/32/64 (RM, dst: r, src: r/m)
        if (opc == 0x1A) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        addBinaryOp(r, cxt, IT_SBB, vt, &o1, &o2);
        break;

    case 0x1C: // sbb al,imm8
    case 0x1D: // sbb ax/eax/rax,imm16/32/64
        if (opc == 0x1C) vt = VT_8;
        parseImm(cxt, vt, &o1, false);
        addBinaryOp(r, cxt, IT_SBB, vt, getRegOp(vt, Reg_AX), &o1);
        break;

    case 0x20: // and r/m8,r8 (MR, dst: r/m, src: r)
    case 0x21: // and r/m,r 16/32/64 (MR, dst: r/m, src: r)
        if (opc == 0x20) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o1, &o2, 0);
        addBinaryOp(r, cxt, IT_AND, vt, &o1, &o2);
        break;

    case 0x22: // and r8,r/m8 (RM, dst: r, src: r/m)
    case 0x23: // and r,r/m 16/32/64 (RM, dst: r, src: r/m)
        if (opc == 0x22) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        addBinaryOp(r, cxt, IT_AND, vt, &o1, &o2);
        break;

    case 0x24: // and al,imm8
    case 0x25: // and ax/eax/rax,imm16/32/32se (sign extended)
        if (opc == 0x24) vt = VT_8;
        parseImm(cxt, vt, &o1, false);
        addBinaryOp(r, cxt, IT_AND, vt, getRegOp(vt, Reg_AX), &o1);
        break;

    case 0x28: // sub r/m8,r8 (MR, dst: r/m, src: r)
    case 0x29: // sub r/m,r 16/32/64 (MR)
        if (opc == 0x28) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o1, &o2, 0);
        addBinaryOp(r, cxt, IT_SUB, vt, &o1, &o2);
        break;

    case 0x2A: // sub r8,r/m8 (RM, dst: r, src: r/m)
    case 0x2B: // sub r,r/m 16/32/64 (RM)
        if (opc == 0x2A) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        addBinaryOp(r, cxt, IT_SUB, vt, &o1, &o2);
        break;

    case 0x2C: // sub al,imm8
    case 0x2D: // sub ax/eax/rax,imm16/32/32se (sign extended)
        if (opc == 0x2C) vt = VT_8;
        parseImm(cxt, vt, &o1, false);
        addBinaryOp(r, cxt, IT_SUB, vt, getRegOp(vt, Reg_AX), &o1);
        break;

    case 0x30: // xor r/m8,r8 (MR, dst: r/m, src: r)
    case 0x31: // xor r/m,r 16/32/64 (MR, dst: r/m, src: r)
        if (opc == 0x30) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o1, &o2, 0);
        addBinaryOp(r, cxt, IT_XOR, vt, &o1, &o2);
        break;

    case 0x32: // xor r8,r/m8 (RM, dst: r, src: r/m)
    case 0x33: // xor r,r/m 16/32/64 (RM, dst: r, src: r/m)
        if (opc == 0x32) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        addBinaryOp(r, cxt, IT_XOR, vt, &o1, &o2);
        break;

    case 0x34: // xor al,imm8
    case 0x35: // xor ax/eax/rax,imm16/32/32se (sign extended)
        if (opc == 0x34) vt = VT_8;
        parseImm(cxt, vt, &o1, false);
        addBinaryOp(r, cxt, IT_XOR, vt, getRegOp(vt, Reg_AX), &o1);
        break;

    case 0x38: // cmp r/m8,r8 (RM, dst: r, src: r/m)
    case 0x39: // cmp r/m,r 16/32/64 (MR)
        if (opc == 0x38) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o1, &o2, 0);
        addBinaryOp(r, cxt, IT_CMP, vt, &o1, &o2);
        break;

    case 0x3A: // cmp r8,r/m8 (RM, dst: r, src: r/m)
    case 0x3B: // cmp r,r/m 16/32/64 (RM)
        if (opc == 0x3A) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        addBinaryOp(r, cxt, IT_CMP, vt, &o1, &o2);
        break;

    case 0x3C: // cmp al,imm8
    case 0x3D: // cmp eax,imm32
        if (opc == 0x3C) vt = VT_8;
        parseImm(cxt, vt, &o1, false);
        addBinaryOp(r, cxt, IT_CMP, vt, getRegOp(vt, Reg_AX), &o1);
        break;

    case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54: case 0x55: case 0x56: case 0x57:
        // push
        reg = Reg_AX + (opc - 0x50);
        vt = VT_64;
        if (cxt->rex & REX_MASK_B) reg += 8;
        if (cxt->ps & PS_66) vt = VT_16;
        addUnaryOp(r, cxt, IT_PUSH, getRegOp(vt, reg));
        break;

    case 0x58: case 0x59: case 0x5A: case 0x5B:
    case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        // pop
        reg = Reg_AX + (opc - 0x58);
        vt = VT_64;
        if (cxt->rex & REX_MASK_B) reg += 8;
        if (cxt->ps & PS_66) vt = VT_16;
        addUnaryOp(r, cxt, IT_POP, getRegOp(vt, reg));
        break;

    case 0x63:
        // movsx r64,r/m32 (RM) mov with sign extension
        assert(cxt->rex & REX_MASK_W);
        parseModRM(cxt, VT_None, RT_GG, &o2, &o1, 0);
        // src is 32 bit
        switch(o2.type) {
        case OT_Reg64: o2.type = OT_Reg32; break;
        case OT_Ind64: o2.type = OT_Ind32; break;
        default: assert(0);
        }
        addBinaryOp(r, cxt, IT_MOVSX, VT_None, &o1, &o2);
        break;

    case 0x68: // push imm32
        parseImm(cxt, VT_32, &o1, false);
        addUnaryOp(r, cxt, IT_PUSH, &o1);
        break;

    case 0x69: // imul r,r/m16/32/64,imm16/32 (RMI)
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        parseImm(cxt, vt, &o3, false); // with 64bit use imm32
        addTernaryOp(r, cxt, IT_IMUL, &o1, &o2, &o3);
        break;

    case 0x6A: // push imm8
        parseImm(cxt, VT_8, &o1, false);
        addUnaryOp(r, cxt, IT_PUSH, &o1);
        break;

    case 0x6B: // imul r,r/m16/32/64,imm8 (RMI)
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        parseImm(cxt, VT_8, &o3, false);
        addTernaryOp(r, cxt, IT_IMUL, &o1, &o2, &o3);
        break;

    case 0x70: // jo rel8
    case 0x71: // jno rel8
    case 0x72: // jc/jb/jnae rel8
    case 0x73: // jnc/jnb/jae rel8
    case 0x74: // jz/je rel8
    case 0x75: // jnz/jne rel8
    case 0x76: // jbe/jna rel8
    case 0x77: // ja/jnbe rel8
    case 0x78: // js rel8
    case 0x79: // jns rel8
    case 0x7A: // jp/jpe rel8
    case 0x7B: // jnp/jpo rel8
    case 0x7C: // jl/jnge rel8
    case 0x7D: // jge/jnl rel8
    case 0x7E: // jle/jng rel8
    case 0x7F: // jg/jnle rel8
        o1.type = OT_Imm64;
        o1.val = (uint64_t) (cxt->fp + cxt->off + 1 + *(int8_t*)(cxt->fp + cxt->off));
        cxt->off += 1;
        switch (opc) {
        case 0x70: it = IT_JO; break;
        case 0x71: it = IT_JNO; break;
        case 0x72: it = IT_JC; break;
        case 0x73: it = IT_JNC; break;
        case 0x74: it = IT_JZ; break;
        case 0x75: it = IT_JNZ; break;
        case 0x76: it = IT_JBE; break;
        case 0x77: it = IT_JA; break;
        case 0x78: it = IT_JS; break;
        case 0x79: it = IT_JNS; break;
        case 0x7A: it = IT_JP; break;
        case 0x7B: it = IT_JNP; break;
        case 0x7C: it = IT_JL; break;
        case 0x7D: it = IT_JGE; break;
        case 0x7E: it = IT_JLE; break;
        case 0x7F: it = IT_JG; break;
        default: assert(0);
        }
        ii = addUnaryOp(r, cxt, it, &o1);
        ii->vtype = VT_Implicit; // jump address size is implicit
        *exit = true;
        break;

    case 0x80: // add/or/... r/m and imm8
        vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o1, 0, &digit);
        switch(digit) {
        case 0: it = IT_ADD; break; // 80/0: add r/m8,imm8
        case 1: it = IT_OR;  break; // 80/1: or  r/m8,imm8
        case 2: it = IT_ADC; break; // 80/2: adc r/m8,imm8
        case 3: it = IT_SBB; break; // 80/3: sbb r/m8,imm8
        case 4: it = IT_AND; break; // 80/4: and r/m8,imm8
        case 5: it = IT_SUB; break; // 80/5: sub r/m8,imm8
        case 6: it = IT_XOR; break; // 80/6: xor r/m8,imm8
        case 7: it = IT_CMP; break; // 80/7: cmp r/m8,imm8
        default: assert(0);
        }
        parseImm(cxt, vt, &o2, false);
        addBinaryOp(r, cxt, it, vt, &o1, &o2);
        break;

    case 0x81:
        // default value type 16/32/64, imm16 (for 16), imm32 (for 32/64)
        parseModRM(cxt, vt, RT_GG, &o1, 0, &digit);
        switch(digit) {
        case 0: it = IT_ADD; break; // 81/0: add r/m 16/32/64, imm16/32
        case 1: it = IT_OR;  break; // 81/1: or  r/m 16/32/64, imm16/32
        case 2: it = IT_ADC; break; // 81/2: adc r/m 16/32/64, imm16/32
        case 3: it = IT_SBB; break; // 81/3: sbb r/m 16/32/64, imm16/32
        case 4: it = IT_AND; break; // 81/4: and r/m 16/32/64, imm16/32
        case 5: it = IT_SUB; break; // 81/5: sub r/m 16/32/64, imm16/32
        case 6: it = IT_XOR; break; // 81/6: xor r/m 16/32/64, imm16/32
        case 7: it = IT_CMP; break; // 81/7: cmp r/m 16/32/64, imm16/32
        default: assert(0);
        }
        parseImm(cxt, vt, &o2, false);
        addBinaryOp(r, cxt, it, vt, &o1, &o2);
        break;

    case 0x83:
        vt = (cxt->rex & REX_MASK_W) ? VT_64 : VT_32;
        parseModRM(cxt, vt, RT_GG, &o1, 0, &digit);
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
        parseImm(cxt, VT_8, &o2, false);
        addBinaryOp(r, cxt, it, vt, &o1, &o2);
        break;

    case 0x84:
        // test r/m,r 8 (MR) - AND r8 with r/m8; set SF, ZF, PF
        // FIXME: We do not assert on use of AH/BH/CH/DH (not supported)
    case 0x85: // test r/m,r 16/32/64 (dst: r/m, src: r)
        if (opc == 0x84) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o1, &o2, 0);
        addBinaryOp(r, cxt, IT_TEST, vt, &o1, &o2);
        break;

    case 0x88: // mov r/m8,r8 (MR)
    case 0x89: // mov r/m,r 16/32/64 (MR - dst: r/m, src: r)
        if (opc == 0x88) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o1, &o2, 0);
        addBinaryOp(r, cxt, IT_MOV, vt, &o1, &o2);
        break;

    case 0x8A: // mov r8,r/m8,r8 (RM)
    case 0x8B: // mov r,r/m 16/32/64 (RM - dst: r, src: r/m)
        if (opc == 0x8A) vt = VT_8;
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        addBinaryOp(r, cxt, IT_MOV, vt, &o1, &o2);
        break;

    case 0x8D:
        // lea r16/32/64,m (RM)
        parseModRM(cxt, vt, RT_GG, &o2, &o1, 0);
        assert(opIsInd(&o2)); // TODO: bad code error
        addBinaryOp(r, cxt, IT_LEA, vt, &o1, &o2);
        break;

    case 0x8F:
        parseModRM(cxt, vt, RT_G, &o1, 0, &digit);
        switch(digit) {
        case 0: // pop r/m 16/64
            // default operand type is 64, not 32
            if (vt == VT_32)
                opOverwriteType(&o1, VT_64);
            else
                assert(vt == VT_16);
            addUnaryOp(r, cxt, IT_POP, &o1);
            break;

        default:
            addSimple(r, cxt, IT_Invalid);
            break;
        }
        break;


    case 0x90:
        // nop
        addSimple(r, cxt, IT_NOP);
        break;

    case 0x98:
        // cltq (Intel: cdqe - sign-extend eax to rax)
        addSimpleVType(r, cxt, IT_CLTQ,
                       (cxt->rex & REX_MASK_W) ? VT_64 : VT_32);
        break;

    case 0x99:
        // cqto (Intel: cqo - sign-extend rax to rdx/rax, eax to edx/eax)
        vt = (cxt->rex & REX_MASK_W) ? VT_128 : VT_64;
        addSimpleVType(r, cxt, IT_CQTO, vt);
        break;

    case 0xA8: // test al,imm8
    case 0xA9: // test ax/eax/rax,imm16/32/32se (se: sign extended)
        if (opc == 0xA8) vt = VT_8;
        parseImm(cxt, vt, &o1, false);
        addBinaryOp(r, cxt, IT_TEST, vt, getRegOp(vt, Reg_AX), &o1);
        break;

    case 0xB0: case 0xB1: case 0xB2: case 0xB3:
    case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        // MOV r8,imm8
    case 0xB8: case 0xB9: case 0xBA: case 0xBB:
    case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        // MOV r32/64,imm32/64
        if ((opc >= 0xB0) && (opc <= 0xB7)) vt = VT_8;
        o1.reg = Reg_AX + (opc & 7);
        if (cxt->rex & REX_MASK_B) o1.reg += 8;
        o1.type = getGPRegOpType(vt);
        parseImm(cxt, vt, &o2, true);
        addBinaryOp(r, cxt, IT_MOV, vt, &o1, &o2);
        break;

    case 0xC1:
        parseModRM(cxt, VT_None, RT_GG, &o1, 0, &digit);
        switch(digit) {
        case 4:
            // shl r/m 32/64,imm8 (MI) (= sal)
            parseImm(cxt, VT_8, &o2, false);
            addBinaryOp(r, cxt, IT_SHL, VT_None, &o1, &o2);
            break;

        case 5:
            // shr r/m 32/64,imm8 (MI)
            parseImm(cxt, VT_8, &o2, false);
            addBinaryOp(r, cxt, IT_SHR, VT_None, &o1, &o2);
            break;

        case 7:
            // sar r/m 32/64,imm8 (MI)
            parseImm(cxt, VT_8, &o2, false);
            addBinaryOp(r, cxt, IT_SAR, VT_None, &o1, &o2);
            break;

        default:
            addSimple(r, cxt, IT_Invalid);
            break;
        }
        break;

    case 0xC3:
        // ret
        addSimple(r, cxt, IT_RET);
        *exit = true;
        break;

    case 0xC6:
        vt = VT_8; // all sub-opcodes use 8bit operand type
        parseModRM(cxt, vt, RT_G, &o1, 0, &digit);
        switch(digit) {
        case 0: // mov r/m8, imm8
            parseImm(cxt, vt, &o2, false);
            addBinaryOp(r, cxt, IT_MOV, vt, &o1, &o2);
            break;
        default: assert(0);
        }
        break;

    case 0xC7:
        // for 16/32/64
        parseModRM(cxt, vt, RT_G, &o1, 0, &digit);
        switch(digit) {
        case 0: // mov r/m 16/32/64, imm16/32/32se (sign extended)
            parseImm(cxt, vt, &o2, false);
            addBinaryOp(r, cxt, IT_MOV, vt, &o1, &o2);
            break;
        default: assert(0);
        }
        break;

    case 0xC9:
        // leave ( = mov rbp,rsp + pop rbp)
        addSimple(r, cxt, IT_LEAVE);
        break;

    case 0xE8:
        // call rel32
        o1.type = OT_Imm64;
        o1.val = (uint64_t) (cxt->fp + cxt->off + 4 + *(int32_t*)(cxt->fp + cxt->off));
        cxt->off += 4;
        addUnaryOp(r, cxt, IT_CALL, &o1);
        *exit = true;
        break;

    case 0xE9:
        // jmp rel32: relative, displacement relative to next instruction
        o1.type = OT_Imm64;
        o1.val = (uint64_t) (cxt->fp + cxt->off + 4 + *(int32_t*)(cxt->fp + cxt->off));
        cxt->off += 4;
        addUnaryOp(r, cxt, IT_JMP, &o1);
        *exit = true;
        break;

    case 0xEB:
        // jmp rel8: relative, displacement relative to next instruction
        o1.type = OT_Imm64;
        o1.val = (uint64_t) (cxt->fp + cxt->off + 1 + *(int8_t*)(cxt->fp + cxt->off));
        cxt->off += 1;
        addUnaryOp(r, cxt, IT_JMP, &o1);
        *exit = true;
        break;

    case 0xF6:
        // source always 8bit
        vt = VT_8;
        parseModRM(cxt, vt, RT_G, &o1, 0, &digit);
        switch(digit) {
        case 0: // test r/m8,imm8 (MI)
            parseImm(cxt, vt, &o2, false);
            addBinaryOp(r, cxt, IT_TEST, vt, &o1, &o2);
            break;
        case 2: // not r/m8
            addUnaryOp(r, cxt, IT_NOT, &o1); break;
        case 3: // neg r/m8
            addUnaryOp(r, cxt, IT_NEG, &o1); break;
        case 4: // mul r/m8 (unsigned mul ax by r/m8)
            addUnaryOp(r, cxt, IT_MUL, &o1); break;
        case 5: // imul r/m8 (signed mul ax/eax/rax by r/m8)
            addUnaryOp(r, cxt, IT_IMUL, &o1); break;
        case 6: // div r/m8 (unsigned div ax by r/m8, rem/quot in ah:al)
            addUnaryOp(r, cxt, IT_DIV, &o1); break;
        case 7: // idiv r/m8 (signed div ax by r/m8, rem/quot in ah:al)
            addUnaryOp(r, cxt, IT_IDIV1, &o1); break;
        default: assert(0);
        }
        break;

    case 0xF7:
        parseModRM(cxt, vt, RT_GG, &o1, 0, &digit);
        switch(digit) {
        case 0: // test r/m16/32/64,imm16/32/32se (MI)
            parseImm(cxt, vt, &o2, false);
            addBinaryOp(r, cxt, IT_TEST, vt, &o1, &o2);
            break;
        case 2: // not r/m 16/32/64
            addUnaryOp(r, cxt, IT_NOT, &o1); break;
        case 3: // neg r/m 16/32/64
            addUnaryOp(r, cxt, IT_NEG, &o1); break;
        case 4: // mul r/m 16/32/64 (unsigned mul ax/eax/rax by r/m)
            addUnaryOp(r, cxt, IT_MUL, &o1); break;
        case 5: // imul r/m 16/32/64 (signed mul ax/eax/rax by r/m)
            addUnaryOp(r, cxt, IT_IMUL, &o1); break;
        case 6: // div r/m 16/32/64 (unsigned div dx:ax/edx:eax/rdx:rax by r/m)
            addUnaryOp(r, cxt, IT_DIV, &o1); break;
        case 7: // idiv r/m 16/32/64 (signed div dx:ax/edx:eax/rdx:rax by r/m)
            addUnaryOp(r, cxt, IT_IDIV1, &o1); break;
        default: assert(0);
        }
        break;

    case 0xFE:
        parseModRM(cxt, VT_8, RT_G, &o1, 0, &digit);
        switch(digit) {
        case 0: // inc r/m8
            addUnaryOp(r, cxt, IT_INC, &o1); break;
        case 1: // dec r/m8
            addUnaryOp(r, cxt, IT_DEC, &o1); break;
        default: assert(0);
        }
        break;

    case 0xFF:
        parseModRM(cxt, vt, RT_G, &o1, 0, &digit);
        switch(digit) {
        case 0: // inc r/m 16/32/64
            addUnaryOp(r, cxt, IT_INC, &o1); break;
        case 1: // dec r/m 16/32/64
            addUnaryOp(r, cxt, IT_DEC, &o1); break;

        case 2:
            // call r/m64
            assert(vt == VT_64); // only 64bit target allowed in 64bit mode
            addUnaryOp(r, cxt, IT_CALL, &o1);
            *exit = true;
            break;

        case 4:
            // jmp* r/m64: absolute indirect
            assert(cxt->rex == 0);
            opOverwriteType(&o1, VT_64);
            addUnaryOp(r, cxt, IT_JMPI, &o1);
            *exit = true;
            break;

        case 6: // push r/m 16/64
            // default operand type is 64, not 32
            if (vt == VT_32)
                opOverwriteType(&o1, VT_64);
            else
                assert(vt == VT_16);
            addUnaryOp(r, cxt, IT_PUSH, &o1);
            break;

        default:
            addSimple(r, cxt, IT_Invalid);
            break;
        }
        break;

    default:
        addSimple(r, cxt, IT_Invalid);
        break;
    }
}

// decode the basic block starting at f (automatically triggered by emulator)
DBB* dbrew_decode(Rewriter* r, uint64_t f)
{
    DContext cxt;
    ValType vt;
    int i, old_icount;
    bool exitLoop;
    DBB* dbb;

    if (f == 0) return 0; // nothing to decode
    if (r->decBB == 0) initRewriter(r);

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

    initDContext(&cxt, f);
    exitLoop = false;

    while(!exitLoop) {
        decodePrefixes(&cxt);

        // default value type (for all instrs with 16/32/64)
        vt = (cxt.rex & REX_MASK_W) ? VT_64 : VT_32;
        if (cxt.ps & PS_66) vt = VT_16;

        decode(r, &cxt, vt, &exitLoop);

        cxt.rex = 0;
        cxt.hasRex = false;
        cxt.segOv = OSO_None;
        cxt.ps = PS_None;
    }

    assert(dbb->addr == dbb->instr->addr);
    dbb->count = r->decInstrCount - old_icount;
    dbb->size = cxt.off;

    if (r->showDecoding)
        dbrew_print_decoded(dbb);

    return dbb;
}

