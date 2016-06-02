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

/* For now, decoder only does x86-64
 *
 * FIXME:
 * for 8bit regs, we do not support/assert on use of AH/BH/CH/DH
*/

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
                   InstrType it, ValType vt,
                   Operand* o1, Operand* o2)
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
                    InstrType it, ValType vt,
                    Operand* o1, Operand* o2, Operand* o3)
{
    if ((vt != VT_None) && (vt != VT_Implicit)) {
        // if we specify an explicit value type, it must match destination
        // 2nd operand does not have to match (e.g. conversion/mask extraction)
        assert(vt == opValType(o1));
    }

    uint64_t len = (uint64_t)(c->f + c->off) - c->iaddr;
    Instr* i = nextInstr(r, c->iaddr, len);
    i->type = it;
    i->form = OF_3;
    i->vtype = vt;
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
    cxt->ps = PS_No;

    cxt->opc1 = -1;
    cxt->opc2 = -1;
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
}

/**
 * Decoding handlers called via opcode tables
 *
 * For each entry in an opcode table, up to 3 handlers can be called.
 */
typedef void (*DecHandler)(DContext*);

typedef enum _OpcType {
    OT_Invalid,
    OT_Single,  // opcode for 1 instructions
    OT_Four,    // opcode for 4 instr (no prefix, 66, F3, F2)
    OT_Group    // opcode for 8 instructions
} OpcType;

typedef struct _OpcInfo OpcInfo;
struct _OpcInfo {
    OpcType t;
    int eStart;  // offset into opcEntry table
};

typedef struct _OpcEntry OpcEntry;
struct _OpcEntry {
    DecHandler h1, h2, h3;
    ValType vt;    // default or specific operand type?
    InstrType it;  // preset for it in DContext
};

static OpcInfo opcTable[256];
static OpcInfo opcTable0F[256];

#define OPCENTRY_SIZE 1000
static OpcEntry opcEntry[OPCENTRY_SIZE];

// set type for opcode, allocate space in opcEntry table
// if type already set, only check
// returns start offset into opcEntry table
static
int setOpcInfo(int opc, OpcType t)
{
    static int used = 0; // use count of opcEntry table
    OpcInfo* oi;

    if ((opc>=0) && (opc<=0xFF)) {
        assert(opc != 0x0F);
        oi = &(opcTable[opc]);
    }
    else if ((opc>=0x0F00) && (opc<=0x0FFF)) {
        oi = &(opcTable0F[opc - 0x0F00]);
    }
    else assert(0);

    if (oi->t == OT_Invalid) {
        // opcode not seen yet
        oi->t = t;
        oi->eStart = used;
        switch(t) {
        case OT_Single: used += 1; break;
        case OT_Four:   used += 4; break;
        case OT_Group:  used += 8; break;
        default: assert(0);
        }
        assert(used <= OPCENTRY_SIZE);
        for(int i = oi->eStart; i < used; i++)
            opcEntry[i].h1 = 0;
    }
    else
        assert(oi->t == t);

    return oi->eStart;
}

static
OpcEntry* getOpcEntry(int opc, OpcType t, int off)
{
    int count;
    int start = setOpcInfo(opc, t);
    switch(t) {
    case OT_Single: count = 1; break;
    case OT_Four:   count = 4; break;
    case OT_Group:  count = 8; break;
    default: assert(0);
    }
    assert((off >= 0) && (off<count));
    return &(opcEntry[start+off]);
}

static
void initOpcEntry(OpcEntry* e, InstrType it, ValType vt,
                  DecHandler h1, DecHandler h2, DecHandler h3)
{
    e->h1 = h1;
    e->h2 = h2;
    e->h3 = h3;
    e->vt = vt;
    e->it = it;
}


static
OpcEntry* setOpc(int opc, InstrType it, ValType vt,
                 DecHandler h1, DecHandler h2, DecHandler h3)
{
    OpcEntry* e = getOpcEntry(opc, OT_Single, 0);
    initOpcEntry(e, it, vt, h1, h2, h3);
    return e;
}

// set handler for opcodes with instruction depending on 66/F2/f3 prefix
static
OpcEntry* setOpcP(int opc, PrefixSet ps,
                  InstrType it, ValType vt,
                  DecHandler h1, DecHandler h2, DecHandler h3)
{
    int off;
    switch(ps) {
    case PS_No: off = 0; break;
    case PS_66:   off = 1; break;
    case PS_F3:   off = 2; break;
    case PS_F2:   off = 3; break;
    default: assert(0);
    }

    OpcEntry* e = getOpcEntry(opc, OT_Four, off);
    initOpcEntry(e, it, vt, h1, h2, h3);
    return e;
}

// set handler for opcodes using a sub-opcode group
// use the digit sub-opcode for <off>
static
OpcEntry* setOpcG(int opc, int off,
                  InstrType it, ValType vt,
                  DecHandler h1, DecHandler h2, DecHandler h3)
{
    OpcEntry* e = getOpcEntry(opc, OT_Group, off);
    initOpcEntry(e, it, vt, h1, h2, h3);
    return e;
}


static
OpcEntry* setOpcH(int opc, DecHandler h)
{
    return setOpc(opc, IT_None, VT_Default, h, 0, 0);
}

static
void processOpc(OpcInfo* oi, DContext* c)
{
    OpcEntry* e;
    int off = 0;

    switch(oi->t) {
    case OT_Single:
        break;
    case OT_Four:
        switch(c->ps) {
        case PS_No: off = 0; break;
        case PS_66:   off = 1; break;
        case PS_F3:   off = 2; break;
        case PS_F2:   off = 3; break;
        default: assert(0);
        }
        break;
    case OT_Group:
        off = (c->f[c->off] & 56) >> 3; // digit
        break;
    default: assert(0);
    }
    e = &(opcEntry[oi->eStart+off]);

    c->it = e->it;
    if (e->vt == VT_Default) {
        // derive type from prefixes
        c->vt = (c->rex & REX_MASK_W) ? VT_64 : VT_32;
        if (c->ps & PS_66) c->vt = VT_16;
    }
    else
        c->vt = e->vt;

    if (e->h1 == 0) {
        // invalid opcode
        addSimple(c->r, c, IT_Invalid);
        return;
    }
    (e->h1)(c);
    if (e->h2 == 0) return;
    (e->h2)(c);
    if (e->h3 == 0) return;
    (e->h3)(c);
}

// operand processing handlers

// RM encoding for 2 GP registers
static void parseRM(DContext* c)
{
    parseModRM(c, c->vt, RT_GG, &c->o2, &c->o1, 0);
}

// MR encoding for 2 GP registers
static void parseMR(DContext* c)
{
    parseModRM(c, c->vt, RT_GG, &c->o1, &c->o2, 0);
}

// RM encoding for 2 vector registers, remember encoding for pass-through
static void parseRMVV(DContext* c)
{
    parseModRM(c, c->vt, RT_VV, &c->o2, &c->o1, 0);
    c->oe = OE_RM;
}

// MR encoding for 2 vector registers, remember encoding for pass-through
static void parseMRVV(DContext* c)
{
    parseModRM(c, c->vt, RT_VV, &c->o1, &c->o2, 0);
    c->oe = OE_MR;
}


// parse immediate into op 1 (for 64bit with imm32 signed extension)
static void parseI1(DContext* c)
{
    parseImm(c, c->vt, &c->o1, false);
}

// parse immediate into op 2 (for 64bit with imm32 signed extension)
static void parseI2(DContext* c)
{
    parseImm(c, c->vt, &c->o2, false);
}

// parse immediate into op 2 (for 64bit with imm32 signed extension)
static void parseI3(DContext* c)
{
    parseImm(c, c->vt, &c->o3, false);
}

// parse immediate 8bit into op 3, signed extend to operand type
static void parseI3_8se(DContext* c)
{
    parseImm(c, VT_8, &c->o3, false);
    // sign-extend op3 to required type: 8->64 works for all
    c->o3.val = (int64_t)(int8_t)c->o3.val;
    c->o3.type = getImmOpType(c->vt);
}

// set op1 as al/ax/eax/rax register
static void setO1RegA(DContext* c)
{
    setRegOp(&c->o1, c->vt, Reg_AX);
}


// instruction append handlers

// append simple instruction without operands
static void addSInstr(DContext* c)
{
    c->ii = addSimple(c->r, c, c->it);
}

// append unary instruction
static void addUInstr(DContext* c)
{
    c->ii = addUnaryOp(c->r, c, c->it, &c->o1);
}

// append binary instruction
static void addBInstr(DContext* c)
{
    c->ii = addBinaryOp(c->r, c, c->it, c->vt, &c->o1, &c->o2);
}

// append binary instruction with implicit type (depends on instr name)
static void addBInsImp(DContext* c)
{
    c->ii = addBinaryOp(c->r, c, c->it, VT_Implicit, &c->o1, &c->o2);
}


// append ternary instruction
static void addTInstr(DContext* c)
{
    c->ii = addTernaryOp(c->r, c, c->it, c->vt, &c->o1, &c->o2, &c->o3);
}

// request exit from decoder
static void reqExit(DContext* c)
{
    c->exit = true;
}

// attach pass-through information
static void attach(DContext* c)
{
    attachPassthrough(c->ii, c->ps, c->oe, SC_None,
                      c->opc1, c->opc2, -1);
}

// opcode-specific decode handlers


//
// handlers for multi-byte opcodes starting with 0x0F
//


static
void decode0F_12(DContext* c)
{
    switch(c->ps) {
    case PS_66:
        // movlpd xmm,m64 (RM) - mov DP FP from m64 to low quadword of xmm
        c->it = IT_MOVLPD; break;
    case PS_No:
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
    case PS_No:
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
    case PS_No: // unpcklps xmm1,xmm2/m128 (RM)
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
    case PS_No: // unpckhps xmm1,xmm2/m128 (RM)
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
    case PS_No:
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
    case PS_No:
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
    case PS_No: // movaps xmm1,xmm2/m128 (RM)
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
    case PS_No: // movaps xmm2/m128,xmm1 (MR)
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
    attachPassthrough(c->ii, PS_No, OE_RM, SC_None, 0x0F, 0x57, -1);
}

static
void decode0F_58(DContext* c)
{
    switch(c->ps) {
    case PS_F3:   // addss xmm1,xmm2/m32 (RM)
        c->vt = VT_32;  c->it = IT_ADDSS; break;
    case PS_F2:   // addsd xmm1,xmm2/m64 (RM)
        c->vt = VT_64;  c->it = IT_ADDSD; break;
    case PS_No: // addps xmm1,xmm2/m128 (RM)
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
    case PS_No: // mulps xmm1,xmm2/m128 (RM)
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
    case PS_No: // subps xmm1,xmm2/m128 (RM)
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
    case PS_No:
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
    case PS_No: c->vt = VT_64; break;
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
    // paddq mm1,mm2/m64 (RM)
    // - add quadword integer mm2/m64 to mm1
    // paddq xmm1,xmm2/m64 (RM)
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
    // pxor xmm1,xmm2/m 64/128 (RM)
    c->vt = (c->ps & PS_66) ? VT_128 : VT_64;
    parseModRM(c, c->vt, RT_VV, &c->o2, &c->o1, 0);
    c->ii = addBinaryOp(c->r, c, IT_PXOR, VT_Implicit, &c->o1, &c->o2);
    attachPassthrough(c->ii, (PrefixSet)(c->ps & PS_66), OE_RM, SC_None,
                      0x0F, 0xEF, -1);
}


//
// handlers for single-byte opcodes
//

static
void decode_50(DContext* c)
{
    // 0x50-57: push r16/r64
    Reg reg = Reg_AX + (c->opc1 - 0x50);
    c->vt = VT_64;
    if (c->rex & REX_MASK_B) reg += 8;
    if (c->ps & PS_66) c->vt = VT_16;
    addUnaryOp(c->r, c, IT_PUSH, getRegOp(c->vt, reg));
}

static
void decode_58(DContext* c)
{
    // 0x58-5F: pop r16/r64
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
    // add/or/... r/m8,imm8
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
    // default value type 16/32/64, imm16/32/32se (se: sign extended)
    parseModRM(c, c->vt, RT_GG, &c->o1, 0, &c->digit);
    switch(c->digit) {
    case 0: c->it = IT_ADD; break; // 81/0: add r/m 16/32/64, imm16/32/32se
    case 1: c->it = IT_OR;  break; // 81/1: or  r/m 16/32/64, imm16/32/32se
    case 2: c->it = IT_ADC; break; // 81/2: adc r/m 16/32/64, imm16/32/32se
    case 3: c->it = IT_SBB; break; // 81/3: sbb r/m 16/32/64, imm16/32/32se
    case 4: c->it = IT_AND; break; // 81/4: and r/m 16/32/64, imm16/32/32se
    case 5: c->it = IT_SUB; break; // 81/5: sub r/m 16/32/64, imm16/32/32se
    case 6: c->it = IT_XOR; break; // 81/6: xor r/m 16/32/64, imm16/32/32se
    case 7: c->it = IT_CMP; break; // 81/7: cmp r/m 16/32/64, imm16/32/32se
    default: assert(0);
    }
    parseImm(c, c->vt, &c->o2, false);
    addBinaryOp(c->r, c, c->it, c->vt, &c->o1, &c->o2);
}

static
void decode_83(DContext* c)
{
    parseModRM(c, c->vt, RT_GG, &c->o1, 0, &c->digit);
    // add/or/... r/m16/32/64,imm8se (sign-extended)
    switch(c->digit) {
    case 0: c->it = IT_ADD; break; // 83/0: add r/m 16/32/64, imm8se
    case 1: c->it = IT_OR;  break; // 83/1: or  r/m 16/32/64, imm8se
    case 2: c->it = IT_ADC; break; // 83/2: adc r/m 16/32/64, imm8se
    case 3: c->it = IT_SBB; break; // 83/3: sbb r/m 16/32/64, imm8se
    case 4: c->it = IT_AND; break; // 83/4: and r/m 16/32/64, imm8se
    case 5: c->it = IT_SUB; break; // 83/5: sub r/m 16/32/64, imm8se
    case 6: c->it = IT_XOR; break; // 83/6: xor r/m 16/32/64, imm8se
    case 7: c->it = IT_CMP; break; // 83/7: cmp r/m 16/32/64, imm8se
    default: assert(0);
    }
    parseImm(c, VT_8, &c->o2, false);
    // sign-extend op2 to required type: 8->64 works for all
    c->o2.val = (int64_t)(int8_t)c->o2.val;
    c->o2.type = getImmOpType(c->vt);
    addBinaryOp(c->r, c, c->it, c->vt, &c->o1, &c->o2);
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
void decode_98(DContext* c)
{
    // cltq (Intel: cdqe - sign-extend eax to rax)
    c->vt = (c->rex & REX_MASK_W) ? VT_64 : VT_32;
    addSimpleVType(c->r, c, IT_CLTQ, c->vt);
}

static
void decode_99(DContext* c)
{
    // cqto (Intel: cqo - sign-extend rax to rdx/rax, eax to edx/eax)
    c->vt = (c->rex & REX_MASK_W) ? VT_128 : VT_64;
    addSimpleVType(c->r, c, IT_CQTO, c->vt);
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
    // initialize only once
    if (done) return;
    done = true;

    for(int i = 0; i<256; i++) {
        opcTable[i].t   = OT_Invalid;
        opcTable0F[i].t = OT_Invalid;
    }

    // 0x00: add r/m8,r8 (MR, dst: r/m, src: r)
    // 0x01: add r/m,r 16/32/64 (MR, dst: r/m, src: r)
    // 0x02: add r8,r/m8 (RM, dst: r, src: r/m)
    // 0x03: add r,r/m 16/32/64 (RM, dst: r, src: r/m)
    // 0x04: add al,imm8 (I)
    // 0x05: add ax/eax/rax,imm16/32/32se (I) - (se: sign extended)
    setOpc(0x00, IT_ADD, VT_8,       parseMR, addBInstr, 0);
    setOpc(0x01, IT_ADD, VT_Default, parseMR, addBInstr, 0);
    setOpc(0x02, IT_ADD, VT_8,       parseRM, addBInstr, 0);
    setOpc(0x03, IT_ADD, VT_Default, parseRM, addBInstr, 0);
    setOpc(0x04, IT_ADD, VT_8,       setO1RegA, parseI2, addBInstr);
    setOpc(0x05, IT_ADD, VT_Default, setO1RegA, parseI2, addBInstr);

    // 0x08: or r/m8,r8 (MR, dst: r/m, src: r)
    // 0x09: or r/m,r 16/32/64 (MR, dst: r/m, src: r)
    // 0x0A: or r8,r/m8 (RM, dst: r, src: r/m)
    // 0x0B: or r,r/m 16/32/64 (RM, dst: r, src: r/m)
    // 0x0C: or al,imm8 (I)
    // 0x0D: or ax/eax/rax,imm16/32/32se (I) - (se: sign extended)
    setOpc(0x08, IT_OR, VT_8,       parseMR, addBInstr, 0);
    setOpc(0x09, IT_OR, VT_Default, parseMR, addBInstr, 0);
    setOpc(0x0A, IT_OR, VT_8,       parseRM, addBInstr, 0);
    setOpc(0x0B, IT_OR, VT_Default, parseRM, addBInstr, 0);
    setOpc(0x0C, IT_OR, VT_8,       setO1RegA, parseI2, addBInstr);
    setOpc(0x0D, IT_OR, VT_Default, setO1RegA, parseI2, addBInstr);

    // 0x10: adc r/m8,r8 (MR, dst: r/m, src: r)
    // 0x11: adc r/m,r 16/32/64 (MR, dst: r/m, src: r)
    // 0x12: adc r8,r/m8 (RM, dst: r, src: r/m)
    // 0x13: adc r,r/m 16/32/64 (RM, dst: r, src: r/m)
    // 0x14: adc al,imm8 (I)
    // 0x15: adc ax/eax/rax,imm16/32/32se (I) - (se: sign extended)
    setOpc(0x10, IT_ADC, VT_8,       parseMR, addBInstr, 0);
    setOpc(0x11, IT_ADC, VT_Default, parseMR, addBInstr, 0);
    setOpc(0x12, IT_ADC, VT_8,       parseRM, addBInstr, 0);
    setOpc(0x13, IT_ADC, VT_Default, parseRM, addBInstr, 0);
    setOpc(0x14, IT_ADC, VT_8,       setO1RegA, parseI2, addBInstr);
    setOpc(0x15, IT_ADC, VT_Default, setO1RegA, parseI2, addBInstr);

    // 0x18: sbb r/m8,r8 (MR, dst: r/m, src: r)
    // 0x19: sbb r/m,r 16/32/64 (MR, dst: r/m, src: r)
    // 0x1A: sbb r8,r/m8 (RM, dst: r, src: r/m)
    // 0x1B: sbb r,r/m 16/32/64 (RM, dst: r, src: r/m)
    // 0x1C: sbb al,imm8 (I)
    // 0x1D: sbb ax/eax/rax,imm16/32/32se (I) - (se: sign extended)
    setOpc(0x18, IT_SBB, VT_8,       parseMR, addBInstr, 0);
    setOpc(0x19, IT_SBB, VT_Default, parseMR, addBInstr, 0);
    setOpc(0x1A, IT_SBB, VT_8,       parseRM, addBInstr, 0);
    setOpc(0x1B, IT_SBB, VT_Default, parseRM, addBInstr, 0);
    setOpc(0x1C, IT_SBB, VT_8,       setO1RegA, parseI2, addBInstr);
    setOpc(0x1D, IT_SBB, VT_Default, setO1RegA, parseI2, addBInstr);

    // 0x20: and r/m8,r8 (MR, dst: r/m, src: r)
    // 0x21: and r/m,r 16/32/64 (MR, dst: r/m, src: r)
    // 0x22: and r8,r/m8 (RM, dst: r, src: r/m)
    // 0x23: and r,r/m 16/32/64 (RM, dst: r, src: r/m)
    // 0x24: and al,imm8 (I)
    // 0x25: and ax/eax/rax,imm16/32/32se (I) - (se: sign extended)
    setOpc(0x20, IT_AND, VT_8,       parseMR, addBInstr, 0);
    setOpc(0x21, IT_AND, VT_Default, parseMR, addBInstr, 0);
    setOpc(0x22, IT_AND, VT_8,       parseRM, addBInstr, 0);
    setOpc(0x23, IT_AND, VT_Default, parseRM, addBInstr, 0);
    setOpc(0x24, IT_AND, VT_8,       setO1RegA, parseI2, addBInstr);
    setOpc(0x25, IT_AND, VT_Default, setO1RegA, parseI2, addBInstr);

    // 0x28: sub r/m8,r8 (MR, dst: r/m, src: r)
    // 0x29: sub r/m,r 16/32/64 (MR, dst: r/m, src: r)
    // 0x2A: sub r8,r/m8 (RM, dst: r, src: r/m)
    // 0x2B: sub r,r/m 16/32/64 (RM, dst: r, src: r/m)
    // 0x2C: sub al,imm8 (I)
    // 0x2D: sub ax/eax/rax,imm16/32/32se (I) - (se: sign extended)
    setOpc(0x28, IT_SUB, VT_8,       parseMR, addBInstr, 0);
    setOpc(0x29, IT_SUB, VT_Default, parseMR, addBInstr, 0);
    setOpc(0x2A, IT_SUB, VT_8,       parseRM, addBInstr, 0);
    setOpc(0x2B, IT_SUB, VT_Default, parseRM, addBInstr, 0);
    setOpc(0x2C, IT_SUB, VT_8,       setO1RegA, parseI2, addBInstr);
    setOpc(0x2D, IT_SUB, VT_Default, setO1RegA, parseI2, addBInstr);

    // 0x30: xor r/m8,r8 (MR, dst: r/m, src: r)
    // 0x31: xor r/m,r 16/32/64 (MR, dst: r/m, src: r)
    // 0x32: xor r8,r/m8 (RM, dst: r, src: r/m)
    // 0x33: xor r,r/m 16/32/64 (RM, dst: r, src: r/m)
    // 0x34: xor al,imm8 (I)
    // 0x35: xor ax/eax/rax,imm16/32/32se (I) - (se: sign extended)
    setOpc(0x30, IT_XOR, VT_8,       parseMR, addBInstr, 0);
    setOpc(0x31, IT_XOR, VT_Default, parseMR, addBInstr, 0);
    setOpc(0x32, IT_XOR, VT_8,       parseRM, addBInstr, 0);
    setOpc(0x33, IT_XOR, VT_Default, parseRM, addBInstr, 0);
    setOpc(0x34, IT_XOR, VT_8,       setO1RegA, parseI2, addBInstr);
    setOpc(0x35, IT_XOR, VT_Default, setO1RegA, parseI2, addBInstr);

    // 0x38: cmp r/m8,r8 (MR, dst: r/m, src: r)
    // 0x39: cmp r/m,r 16/32/64 (MR, dst: r/m, src: r)
    // 0x3A: cmp r8,r/m8 (RM, dst: r, src: r/m)
    // 0x3B: cmp r,r/m 16/32/64 (RM, dst: r, src: r/m)
    // 0x3C: cmp al,imm8 (I)
    // 0x3D: cmp ax/eax/rax,imm16/32/32se (I) - (se: sign extended)
    setOpc(0x38, IT_CMP, VT_8,       parseMR, addBInstr, 0);
    setOpc(0x39, IT_CMP, VT_Default, parseMR, addBInstr, 0);
    setOpc(0x3A, IT_CMP, VT_8,       parseRM, addBInstr, 0);
    setOpc(0x3B, IT_CMP, VT_Default, parseRM, addBInstr, 0);
    setOpc(0x3C, IT_CMP, VT_8,       setO1RegA, parseI2, addBInstr);
    setOpc(0x3D, IT_CMP, VT_Default, setO1RegA, parseI2, addBInstr);

    // 0x50-57: push r16/r64
    setOpcH(0x50, decode_50);
    setOpcH(0x51, decode_50);
    setOpcH(0x52, decode_50);
    setOpcH(0x53, decode_50);
    setOpcH(0x54, decode_50);
    setOpcH(0x55, decode_50);
    setOpcH(0x56, decode_50);
    setOpcH(0x57, decode_50);

    // 0x58-5F: pop r16/r64
    setOpcH(0x58, decode_58);
    setOpcH(0x59, decode_58);
    setOpcH(0x5A, decode_58);
    setOpcH(0x5B, decode_58);
    setOpcH(0x5C, decode_58);
    setOpcH(0x5D, decode_58);
    setOpcH(0x5E, decode_58);
    setOpcH(0x5F, decode_58);

     // movsx r64,r/m32
    setOpcH(0x63, decode_63);

    // 0x68: push imm32 (imm16 possible, but not generated by "as" - no test)
    // 0x6A: push imm8
    setOpc(0x68, IT_PUSH, VT_32, parseI1, addUInstr, 0);
    setOpc(0x6A, IT_PUSH, VT_8,  parseI1, addUInstr, 0);

    // 0x69: imul r,r/m16/32/64,imm16/32/32se (RMI)
    // 0x6B: imul r,r/m16/32/64,imm8se (RMI)
    setOpc(0x69, IT_IMUL, VT_Default,  parseRM, parseI3, addTInstr);
    setOpc(0x6B, IT_IMUL, VT_Default,  parseRM, parseI3_8se, addTInstr);

    // 0x70-7F: jcc rel8
    setOpcH(0x70, decode_70);
    setOpcH(0x71, decode_70);
    setOpcH(0x72, decode_70);
    setOpcH(0x73, decode_70);
    setOpcH(0x74, decode_70);
    setOpcH(0x75, decode_70);
    setOpcH(0x76, decode_70);
    setOpcH(0x77, decode_70);
    setOpcH(0x78, decode_70);
    setOpcH(0x79, decode_70);
    setOpcH(0x7A, decode_70);
    setOpcH(0x7B, decode_70);
    setOpcH(0x7C, decode_70);
    setOpcH(0x7D, decode_70);
    setOpcH(0x7E, decode_70);
    setOpcH(0x7F, decode_70);

    // Immediate Grp 1
    // 0x80: add/or/... r/m8,imm8
    // 0x81: add/or/... r/m16/32/64,imm16/32/32se
    // 0x83: add/or/... r/m16/32/64,imm8se
    setOpcH(0x80, decode_80);
    setOpcH(0x81, decode_81);
    setOpcH(0x83, decode_83);

    // 0x84: test r/m8,r8 (MR) - r/m8 "and" r8, set SF, ZF, PF
    // 0x85: test r/m,r16/32/64 (MR)
    setOpc(0x84, IT_TEST, VT_8,       parseMR, addBInstr, 0);
    setOpc(0x85, IT_TEST, VT_Default, parseMR, addBInstr, 0);

    // 0x88: mov r/m8,r8 (MR)
    // 0x89: mov r/m,r16/32/64 (MR)
    // 0x8A: mov r8,r/m8,r8 (RM)
    // 0x8B: mov r,r/m16/32/64 (RM)
    setOpc(0x88, IT_MOV, VT_8,       parseMR, addBInstr, 0);
    setOpc(0x89, IT_MOV, VT_Default, parseMR, addBInstr, 0);
    setOpc(0x8A, IT_MOV, VT_8,       parseRM, addBInstr, 0);
    setOpc(0x8B, IT_MOV, VT_Default, parseRM, addBInstr, 0);

    // 0x8D: lea r16/32/64,m (RM)
    setOpcH(0x8D, decode_8D);
    // 0x8F: Grp1A
    setOpcH(0x8F, decode_8F);
    // 0x90: nop
    setOpc(0x90, IT_NOP, VT_None, addSInstr, 0, 0);

    setOpcH(0x98, decode_98); // cltq
    setOpcH(0x99, decode_99); // cqto

    // 0xA8: test al,imm8
    // 0xA9: test ax/eax/rax,imm16/32/32se
    setOpc(0xA8, IT_TEST, VT_8,       setO1RegA, parseI2, addBInstr);
    setOpc(0xA9, IT_TEST, VT_Default, setO1RegA, parseI2, addBInstr);

    // 0xB0-B7: mov r8,imm8
    // 0xB8-BF: mov r32/64,imm32/64
    setOpcH(0xB0, decode_B0);
    setOpcH(0xB1, decode_B0);
    setOpcH(0xB2, decode_B0);
    setOpcH(0xB3, decode_B0);
    setOpcH(0xB4, decode_B0);
    setOpcH(0xB5, decode_B0);
    setOpcH(0xB6, decode_B0);
    setOpcH(0xB7, decode_B0);
    setOpcH(0xB8, decode_B0);
    setOpcH(0xB9, decode_B0);
    setOpcH(0xBA, decode_B0);
    setOpcH(0xBB, decode_B0);
    setOpcH(0xBC, decode_B0);
    setOpcH(0xBD, decode_B0);
    setOpcH(0xBE, decode_B0);
    setOpcH(0xBF, decode_B0);

    // 0xC0/C1: Grp1A
    setOpcH(0xC0, decode_C0);
    setOpcH(0xC1, decode_C1);

    // 0xC3: ret
    setOpc(0xC3, IT_RET, VT_None, addSInstr, reqExit, 0);

    // 0xC6/C7: Grp 11
    setOpcH(0xC6, decode_C6);
    setOpcH(0xC7, decode_C7);

    // 0xC9: leave ( = mov rbp,rsp + pop rbp)
    setOpc(0xC9, IT_LEAVE, VT_None, addSInstr, 0, 0);

    // 0xD0-D3: Grp1A
    setOpcH(0xD0, decode_D0);
    setOpcH(0xD1, decode_D1);
    setOpcH(0xD2, decode_D2);
    setOpcH(0xD3, decode_D3);

    setOpcH(0xE8, decode_E8); // call rel32
    setOpcH(0xE9, decode_E9); // jmp rel32
    setOpcH(0xEB, decode_EB); // jmp rel8

    // 0xF6/F7: Grp 3, 0xFE: Grp 4, 0xFE: Grp 5
    setOpcH(0xF6, decode_F6);
    setOpcH(0xF7, decode_F7);
    setOpcH(0xFE, decode_FE);
    setOpcH(0xFF, decode_FF);

    // 0x0F10/No: movups xmm1,xmm2/m128 (RM)
    // 0x0F10/66: movupd xmm1,xmm2/m128 (RM)
    // 0x0F10/F3: movss xmm1,xmm2/m32 (RM)
    // 0x0F10/F2: movsd xmm1,xmm2/m64 (RM)
    setOpcP(0x0F10, PS_No, IT_MOVUPS, VT_128, parseRMVV, addBInsImp, attach);
    setOpcP(0x0F10, PS_66, IT_MOVUPD, VT_128, parseRMVV, addBInsImp, attach);
    setOpcP(0x0F10, PS_F3, IT_MOVSS,  VT_32,  parseRMVV, addBInsImp, attach);
    setOpcP(0x0F10, PS_F2, IT_MOVSD,  VT_64,  parseRMVV, addBInsImp, attach);

    // 0x0F10/No: movups xmm1/m128,xmm2 (MR)
    // 0x0F10/66: movupd xmm1/m128,xmm2 (MR)
    // 0x0F10/F3: movss xmm1/m32,xmm2 (MR)
    // 0x0F10/F2: movsd xmm1/m64,xmm2 (MR)
    setOpcP(0x0F11, PS_No, IT_MOVUPS, VT_128, parseMRVV, addBInsImp, attach);
    setOpcP(0x0F11, PS_66, IT_MOVUPD, VT_128, parseMRVV, addBInsImp, attach);
    setOpcP(0x0F11, PS_F3, IT_MOVSS,  VT_32,  parseMRVV, addBInsImp, attach);
    setOpcP(0x0F11, PS_F2, IT_MOVSD,  VT_64,  parseMRVV, addBInsImp, attach);

    setOpcH(0x0F12, decode0F_12);
    setOpcH(0x0F13, decode0F_13);
    setOpcH(0x0F14, decode0F_14);
    setOpcH(0x0F15, decode0F_15);
    setOpcH(0x0F16, decode0F_16);
    setOpcH(0x0F17, decode0F_17);
    setOpcH(0x0F1F, decode0F_1F);
    setOpcH(0x0F28, decode0F_28);
    setOpcH(0x0F29, decode0F_29);
    setOpcH(0x0F2E, decode0F_2E);

    // 0x0F40-0x0F4F: cmovcc r,r/m 16/32/64
    setOpcH(0x0F40, decode0F_40);
    setOpcH(0x0F41, decode0F_40);
    setOpcH(0x0F42, decode0F_40);
    setOpcH(0x0F43, decode0F_40);
    setOpcH(0x0F44, decode0F_40);
    setOpcH(0x0F45, decode0F_40);
    setOpcH(0x0F46, decode0F_40);
    setOpcH(0x0F47, decode0F_40);
    setOpcH(0x0F48, decode0F_40);
    setOpcH(0x0F49, decode0F_40);
    setOpcH(0x0F4A, decode0F_40);
    setOpcH(0x0F4B, decode0F_40);
    setOpcH(0x0F4C, decode0F_40);
    setOpcH(0x0F4D, decode0F_40);
    setOpcH(0x0F4E, decode0F_40);
    setOpcH(0x0F4F, decode0F_40);

    setOpcH(0x0F57, decode0F_57);
    setOpcH(0x0F58, decode0F_58);
    setOpcH(0x0F59, decode0F_59);
    setOpcH(0x0F5C, decode0F_5C);
    setOpcH(0x0F6E, decode0F_6E);
    setOpcH(0x0F6F, decode0F_6F);
    setOpcH(0x0F74, decode0F_74);
    setOpcH(0x0F7E, decode0F_7E);
    setOpcH(0x0F7F, decode0F_7F);

    // 0x0F80-0F8F: jcc rel32
    setOpcH(0x0F80, decode0F_80);
    setOpcH(0x0F81, decode0F_80);
    setOpcH(0x0F82, decode0F_80);
    setOpcH(0x0F83, decode0F_80);
    setOpcH(0x0F84, decode0F_80);
    setOpcH(0x0F85, decode0F_80);
    setOpcH(0x0F86, decode0F_80);
    setOpcH(0x0F87, decode0F_80);
    setOpcH(0x0F88, decode0F_80);
    setOpcH(0x0F89, decode0F_80);
    setOpcH(0x0F8A, decode0F_80);
    setOpcH(0x0F8B, decode0F_80);
    setOpcH(0x0F8C, decode0F_80);
    setOpcH(0x0F8D, decode0F_80);
    setOpcH(0x0F8E, decode0F_80);
    setOpcH(0x0F8F, decode0F_80);

    // 0x0FAF: imul r,rm16/32/64 (RM), signed mul (d/q)word by r/m
    setOpc(0x0FAF, IT_IMUL, VT_Default, parseRM, addBInstr, 0);

    setOpcH(0x0FB6, decode0F_B6); // movzbl r16/32/64,r/m8 (RM)
    setOpcH(0x0FB7, decode0F_B7); // movzbl r32/64,r/m16 (RM)

    // 0x0FBC: bsf r,r/m 16/32/64 (RM): bit scan forward
    setOpc(0x0FBC, IT_BSF, VT_Default, parseRM, addBInstr, 0);

    setOpcH(0x0FBE, decode0F_BE); // movsx r16/32/64,r/m8 (RM)
    setOpcH(0x0FBF, decode0F_BF); // movsx r32/64,r/m16 (RM)
    setOpcH(0x0FD4, decode0F_D4); // paddq xmm1,xmm2/m 64/128 (RM)
    setOpcH(0x0FD6, decode0F_D6); // movq xmm2/m64,xmm1 (MR)
    setOpcH(0x0FD7, decode0F_D7); // pmovmskb r,xmm 64/128 (RM)
    setOpcH(0x0FDA, decode0F_DA); // pminub xmm,xmm/m 64/128 (RM)
    setOpcH(0x0FEF, decode0F_EF); // pxor xmm1,xmm2/m 64/128 (RM)
}

// decode the basic block starting at f (automatically triggered by emulator)
DBB* dbrew_decode(Rewriter* r, uint64_t f)
{
    DContext cxt;
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

        // parse opcode by running handlers defined in opcode tables
        opc = cxt.f[cxt.off++];
        cxt.opc1 = opc;
        if (opc == 0x0F) {
            // opcode starting with 0x0F
            opc = cxt.f[cxt.off++];
            cxt.opc2 = opc;
            processOpc(&(opcTable0F[opc]), &cxt);
            continue;
        }
        processOpc(&(opcTable[opc]), &cxt);
    }

    assert(dbb->addr == dbb->instr->addr);
    dbb->count = r->decInstrCount - old_icount;
    dbb->size = cxt.off;

    if (r->showDecoding)
        dbrew_print_decoded(dbb);

    return dbb;
}

