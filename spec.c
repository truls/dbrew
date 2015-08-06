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


typedef struct _EmuState EmuState;
typedef struct _CaptureConfig CaptureConfig;

typedef enum { False, True } Bool;

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
    //
    Reg_Max
} Reg;

typedef enum _InstrType {
    IT_None = 0, IT_Invalid,
    IT_NOP,
    IT_PUSH, IT_POP,
    IT_MOV, IT_LEA,
    IT_ADD, IT_SUB,
    IT_CALL, IT_RET,
    IT_Max
} InstrType;

typedef enum _ValType {
    VT_None = 0,
    VT_8, VT_16, VT_32, VT_64,
    VT_Max
} ValType;

typedef enum _OpType {
    OT_None = 0,
    OT_Imm8, OT_Imm16, OT_Imm32, OT_Imm64,
    OT_Reg8, OT_Reg16, OT_Reg32, OT_Reg64,
    // mem (64bit addr): register indirect + displacement
    OT_Ind8, OT_Ind16, OT_Ind32, OT_Ind64,
    OT_MAX
} OpType;

typedef struct _Operand
{
    OpType type;
    Reg reg;
    Reg ireg; // with SIB
    uint64_t val; // imm or displacement
    int scale; // with SIB
} Operand;

typedef struct _Instr {
    uint64_t addr;
    int len;
    InstrType type;
    Operand dst, src;
} Instr;

typedef struct _Code {
    // decoded instructions
    int count, capacity;
    Instr* instr;

    // buffer/config to capture emulation (see below)
    CodeStorage* cs;
    CaptureConfig* cc;
    EmuState* es;
} Code;

// REX prefix, used in parseModRM
#define REX_MASK_B 1
#define REX_MASK_X 2
#define REX_MASK_R 4
#define REX_MASK_W 8

Code* allocCode(int capacity, int capture_capacity)
{
    Code* c = (Code*) malloc(sizeof(Code));
    c->count = 0;
    c->capacity = capacity;
    c->instr = (Instr*) malloc(sizeof(Instr) * capacity);

    if (capture_capacity >0)
	c->cs = initCodeStorage(capture_capacity);
    else
	c->cs = 0;

    c->cc = 0;
    c->es = 0;

    return c;
}

uint64_t capturedCode(Code* c)
{
    if ((c->cs == 0) || (c->cs->used == 0))
	return 0;

    return (uint64_t) c->cs->buf;
}

int capturedCodeSize(Code* c)
{
    if ((c->cs == 0) || (c->cs->used == 0))
	return 0;

    return c->cs->used;
}

void freeCode(Code* c)
{
    free(c->instr);
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
    default: assert(0);
    }
    return 0; // invalid;
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
        return True;
    }
    return False;
}

Bool opIsInd(Operand* o)
{
    switch(o->type) {
    case OT_Ind8:
    case OT_Ind16:
    case OT_Ind32:
    case OT_Ind64:
        return True;
    }
    return False;
}

Operand* getRegOp(ValType t, Reg r)
{
    static Operand o;

    switch(t) {
    case VT_32:
	assert((r >= Reg_AX) && (r <= Reg_15));
	o.type = OT_Reg32;
	o.reg = r;
	o.scale = 0;
	break;

    case VT_64:
	assert((r >= Reg_AX) && (r <= Reg_15));
	o.type = OT_Reg64;
	o.reg = r;
	o.scale = 0;
	break;

    default: assert(0);
    }

    return &o;
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
    case OT_Imm32:
	assert(src->val < (1l<<32));
	// fall-trough
    case OT_Imm64:
	dst->val = src->val;
	break;
    case OT_Reg32:
    case OT_Reg64:
	assert((src->reg >= Reg_AX) && (src->reg <= Reg_15));
	dst->reg = src->reg;
	break;
    case OT_Ind32:
    case OT_Ind64:
	dst->reg = src->reg;
	dst->val = src->val;
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

void initSimpleInstr(Instr* i, InstrType it)
{
    i->addr = 0; // unknown: created, not parsed
    i->len = 0;
    i->type = it;
    i->dst.type = OT_None;
    i->src.type = OT_None;
}

void initUnaryInstr(Instr* i, InstrType it, Operand* o)
{
    initSimpleInstr(i, it);
    copyOperand( &(i->dst), o);
}

void initBinaryInstr(Instr* i, InstrType it, Operand *o1, Operand *o2)
{
    initSimpleInstr(i, it);
    copyOperand( &(i->dst), o1);
    copyOperand( &(i->src), o2);
}


Instr* nextInstr(Code* c, uint64_t a, int len)
{
    Instr* i = c->instr + c->count;
    assert(c->count < c->capacity);
    c->count++;

    i->addr = a;
    i->len = len;
    return i;
}

void addSimple(Code* c, uint64_t a, uint64_t a2, InstrType it)
{
    Instr* i = nextInstr(c, a, a2 - a);
    i->type = it;
}

void addUnaryOp(Code* c, uint64_t a, uint64_t a2,
		InstrType it, Operand* o)
{
    Instr* i = nextInstr(c, a, a2 - a);
    i->type = it;
    copyOperand( &(i->dst), o);
}

void addBinaryOp(Code* c, uint64_t a, uint64_t a2,
		 InstrType it, Operand* o1, Operand* o2)
{
    Instr* i = nextInstr(c, a, a2 - a);
    i->type = it;
    copyOperand( &(i->dst), o1);
    copyOperand( &(i->src), o2);
}


// r/m, r. r parsed as op2/reg and digit. Encoding see SDM 2.1
// return number of bytes parsed
int parseModRM(uint8_t* p, int rex, Operand* o1, Operand* o2, int* digit)
{
    int modrm, mod, rm, reg; // modRM byte
    int sib, scale, idx, base; // SIB byte
    int64_t disp;
    Reg r;
    int o = 0;
    int hasRex = (rex>0);
    int hasDisp8 = 0, hasDisp32 = 0;

    modrm = p[o++];
    mod = (modrm & 192) >> 6;
    reg = (modrm & 56) >> 3;
    rm = modrm & 7;

    OpType reg_ot = (hasRex && (rex & REX_MASK_W)) ? OT_Reg64 : OT_Reg32;

    // r part: reg or digit, give both back to caller
    if (digit) *digit = reg;
    if (o2) {
        r = Reg_AX + reg;
        if (hasRex && (rex & REX_MASK_R)) r += 8;
        o2->type = reg_ot;
        o2->reg = r;
    }

    if (mod == 3) {
	// r, r
	r = Reg_AX + rm;
	if (hasRex && (rex & REX_MASK_B)) r += 8;
	o1->type = reg_ot;
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

    o1->type = (hasRex && (rex & REX_MASK_W)) ? OT_Ind64 : OT_Ind32;
    o1->scale = scale;
    o1->val = (uint64_t) disp;
    if (scale == 0) {
	r = Reg_AX + rm;
	if (hasRex && (rex & REX_MASK_B)) r += 8;
	o1->reg = ((mod == 0) && (rm == 5)) ? Reg_IP : r;
	return o;
    }

    r = Reg_AX + idx;
    if (hasRex && (rex & REX_MASK_X)) r += 8;
    o1->ireg = (idx == 4) ? Reg_None : r;


    r = Reg_AX + base;
    if (hasRex && (rex & REX_MASK_B)) r += 8;
    o1->reg = ((base == 5) && (mod == 0)) ? Reg_None : r;

    // no need to use SIB if index register not used
    if (o1->ireg == Reg_None) o1->scale = 0;

    return o;
}

void decodeFunc(Code* c, uint64_t f, int max, int stopAtRet)
{
    int hasRex, rex; // REX prefix
    uint64_t a;
    int o, retFound, opc;
    uint8_t* fp;

    fp = (uint8_t*) f;
    c->count = 0;

    o = 0;
    hasRex = 0;
    retFound = 0;
    while((o < max) && !retFound) {
	a = (uint64_t)(fp + o);

	// prefixes
	while(1) {
	    if ((fp[o] >= 0x40) && (fp[o] <= 0x4F)) {
		rex = fp[o] & 15;
		hasRex = 1;
		o++;
		continue;
	    }
	    break;
	}

	opc = fp[o++];
	switch(opc) {
	case 0xc3:
	    // ret
	    addSimple(c, a, (uint64_t)(fp + o), IT_RET);
	    if (stopAtRet) retFound = 1;
	    break;

	case 0x50: case 0x51: case 0x52: case 0x53:
	case 0x54: case 0x55: case 0x56: case 0x57:
	    // push
	    addUnaryOp(c, a, (uint64_t)(fp + o),
		       IT_PUSH, getRegOp(VT_64, Reg_AX+(opc-0x50)));
	    break;

	case 0x58: case 0x59: case 0x5A: case 0x5B:
	case 0x5C: case 0x5D: case 0x5E: case 0x5F:
	    // pop
	    addUnaryOp(c, a, (uint64_t)(fp + o),
		       IT_POP, getRegOp(VT_64, Reg_AX+(opc-0x58)));
	    break;

	case 0x89: {
	    // mov r/m,r 32/64 (dst: r/m, src: r)
	    Operand o1, o2;
            o += parseModRM(fp+o, hasRex ? rex:0, &o1, &o2, 0);
            addBinaryOp(c, a, (uint64_t)(fp + o), IT_MOV, &o1, &o2);
	    break;
	}

	case 0x8B: {
	    // mov r,r/m 32/64 (dst: r, src: r/m)
	    Operand o1, o2;
            o += parseModRM(fp+o, hasRex ? rex:0, &o2, &o1, 0);
            addBinaryOp(c, a, (uint64_t)(fp + o), IT_MOV, &o1, &o2);
	    break;
	}

	case 0x01: {
	    // add r/m,r 32/64 (dst: r/m, src: r)
	    Operand o1, o2;
            o += parseModRM(fp+o, hasRex ? rex:0, &o1, &o2, 0);
            addBinaryOp(c, a, (uint64_t)(fp + o), IT_ADD, &o1, &o2);
	    break;
	}

	case 0x8d: {
	    // lea r32/64,m
	    Operand o1, o2;
            o += parseModRM(fp+o, hasRex ? rex:0, &o2, &o1, 0);
	    assert(opIsInd(&o2)); // TODO: bad code error
	    addBinaryOp(c, a, (uint64_t)(fp + o),
			IT_LEA, &o1, &o2);
	    break;
	}

        case 0xC7: {
            Operand o1, o2;
            int digit;
            o += parseModRM(fp+o, hasRex ? rex:0, &o1, 0, &digit);
            switch(digit) {
            case 0:
                // mov r/m 32/64, imm32
                o2.type = OT_Imm32;
                o2.val = *(uint32_t*)(fp + o);
                o += 4;
                addBinaryOp(c, a, (uint64_t)(fp + o), IT_MOV, &o1, &o2);
                break;

            default: assert(0);
            }
            break;
        }

        case 0x81: {
            Operand o1, o2;
            int digit;
            o += parseModRM(fp+o, hasRex ? rex:0, &o1, 0, &digit);
            switch(digit) {
            case 0:
                // add r/m 32/64, imm32
                o2.type = OT_Imm32;
                o2.val = *(uint32_t*)(fp + o);
                o += 4;
                addBinaryOp(c, a, (uint64_t)(fp + o), IT_ADD, &o1, &o2);
                break;

            default: assert(0);
            }
            break;
        }

	default:
	    addSimple(c, a, (uint64_t)(fp + o), IT_Invalid);
	    break;
	}
	hasRex = 0;
    }
}

/*------------------------------------------------------------*/
/* x86_64 printer
 */

char* regName(Reg r)
{
    switch(r) {
    case Reg_AX: return "ax";
    case Reg_BX: return "bx";
    case Reg_CX: return "cx";
    case Reg_DX: return "dx";
    case Reg_DI: return "di";
    case Reg_SI: return "si";
    case Reg_BP: return "bp";
    case Reg_SP: return "sp";
    case Reg_8:  return "8";
    case Reg_9:  return "9";
    case Reg_10: return "10";
    case Reg_11: return "11";
    case Reg_12: return "12";
    case Reg_13: return "13";
    case Reg_14: return "14";
    case Reg_15: return "15";
    case Reg_IP: return "ip";
    }
    assert(0);
}

char* op2string(Operand* o)
{
    static char buf[30];
    int off = 0;

    switch(o->type) {
    case OT_Reg32:
	sprintf(buf, "%%e%s", regName(o->reg));
	break;
    case OT_Reg64:
	sprintf(buf, "%%r%s", regName(o->reg));
	break;
    case OT_Imm32:
	assert(o->val < (1l<<32));
    case OT_Imm64:
	sprintf(buf, "$0x%lx", o->val);
	break;
    case OT_Ind8:
    case OT_Ind16:
    case OT_Ind32:
    case OT_Ind64:
	if (o->val != 0) {
	    if (o->val & (1l<<63))
		off = sprintf(buf, "-0x%lx", (~ o->val)+1);
	    else
		off = sprintf(buf, "0x%lx", o->val);
	}
        if ((o->scale == 0) || (o->ireg == Reg_None)) {
            if (o->reg != Reg_None)
                sprintf(buf+off,"(%%r%s)", regName(o->reg));
        }
	else {
            char* ri = regName(o->ireg);
            if (o->reg == Reg_None) {
                sprintf(buf+off,"(,%%r%s,%d)", ri, o->scale);
            }
            else
                sprintf(buf+off,"(%%r%s,%%r%s,%d)",
                        regName(o->reg), ri, o->scale);
	}
	break;
    default: assert(0);
    }
    return buf;
}

char* instr2string(Instr* instr, int align)
{
    static char buf[100];
    char* n = "<Invalid>";
    int oc = 0, off = 0;

    switch(instr->type) {
    case IT_NOP:  n = "nop"; break;
    case IT_RET:  n = "ret"; break;
    case IT_PUSH: n = "push"; oc = 1; break;
    case IT_POP:  n = "pop";  oc = 1; break;
    case IT_MOV:  n = "mov";  oc = 2; break;
    case IT_ADD:  n = "add";  oc = 2; break;
    case IT_SUB:  n = "sub";  oc = 2; break;
    case IT_LEA:  n = "lea";  oc = 2; break;
    }
    if (align)
        off += sprintf(buf, "%-6s", n);
    else
        off += sprintf(buf, "%s", n);
    if (oc == 1)
        off += sprintf(buf+off, " %s", op2string(&(instr->dst)));
    if (oc == 2) {
        off += sprintf(buf+off, " %s", op2string(&(instr->src)));
	off += sprintf(buf+off, ",%s", op2string(&(instr->dst)));
    }
    return buf;
}

char* bytes2string(Instr* instr, int start, int count)
{
    static char buf[100];
    int off = 0, i, j;
    for(i = start, j=0; i < instr->len && j<count; i++, j++) {
	uint8_t b = ((uint8_t*) instr->addr)[i];
	off += sprintf(buf+off, " %02x", b);
    }
    for(;j<count;j++)
	off += sprintf(buf+off, "   ");
    return buf;
}

void printCode(Code* c)
{
    int i;
    for(i=0; i<c->count; i++) {
	printf("  %p %s  %s\n", (void*)c->instr[i].addr,
               bytes2string(c->instr + i, 0, 7), instr2string(c->instr + i, 1));
        if (c->instr[i].len > 7)
            printf("  %p %s\n", (void*)c->instr[i].addr + 7,
                   bytes2string(c->instr + i, 7, 7));
        if (c->instr[i].len > 14)
            printf("  %p %s\n", (void*)c->instr[i].addr + 14,
                   bytes2string(c->instr + i, 14, 7));
    }
}

/*------------------------------------------------------------*/
/* x86_64 code generation
 */

// generator helpers: return number of bytes written

int genRet(uint8_t* buf)
{
    buf[0] = 0xc3;
    return 1;
}

int genPush(uint8_t* buf, Operand* o)
{
    assert(o->type == OT_Reg64);
    assert((o->reg >= Reg_AX) && (o->reg <= Reg_DI));
    buf[0] = 0x50 + (o->reg - Reg_AX);
    return 1;
}

int genPop(uint8_t* buf, Operand* o)
{
    assert(o->type == OT_Reg64);
    assert((o->reg >= Reg_AX) && (o->reg <= Reg_DI));
    buf[0] = 0x58 + (o->reg - Reg_AX);
    return 1;
}

uint8_t* calcModRMDigit(Operand* o1, int digit, int* prex, int* plen)
{
    static uint8_t buf[10];
    int modrm, r1;
    int o = 0;

    assert((digit>=0) && (digit<8));
    assert((opValType(o1) == VT_32) || (opValType(o1) == VT_64));
    assert(opIsReg(o1) || opIsInd(o1));

    if (opValType(o1) == VT_64) *prex |= REX_MASK_W;

    modrm = (digit & 7) << 3;

    if (opIsReg(o1)) {
	// r,r: mod 3
	modrm |= 192;
	r1 = o1->reg - Reg_AX;
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

	if (o1->scale == 0) {
	    assert(o1->reg != Reg_SP); // rm 4 reserved for SIB encoding
            if (o1->reg == Reg_None) {
                useDisp32 = 1; // encoding needs disp32
                useDisp8 = 0;
                modrm &= 63; // mod needs to be 00
                useSIB = 1;
                sib = (4 << 3) + 5; // index 4 (= none) + base 5 (= none)
            }
            else {
                r1 = o1->reg - Reg_AX;
                assert((modrm >63) || (r1 != 5)); // do not use RIP encoding
                if (o1->reg == Reg_IP) {
                    // RIP relative
                    // BUG: Should be relative to original code, not generated
                    r1 = 5;
                    modrm &= 63;
                    useDisp32 = 1;
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
		assert(o1->scale == 1);

            assert(o1->ireg != Reg_None);
	    ri = o1->ireg - Reg_AX;
	    if (ri & 8) *prex |= REX_MASK_X;
	    sib |= (ri & 7) <<3;

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
                    if (modrm & 192 == 0) {
                        modrm |= 64;
                        useDisp8 = 1;
                    }
                }
                rb = o1->reg - Reg_AX;
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
    assert(opValType(o1) == opValType(o2));
    assert((opValType(o1) == VT_32) || (opValType(o1) == VT_64));
    assert(opIsReg(o1) || opIsInd(o1));
    assert(opIsReg(o2));

    // o2 always r
    int r2 = o2->reg - Reg_AX;
    if (r2 & 8) *prex |= REX_MASK_R;

    return calcModRMDigit(o1, r2 & 7, prex, plen);
}


// Operand o1: r/m, o2: r
int genModRM(uint8_t* buf, int opc, Operand* o1, Operand* o2)
{
    int rex = 0, len = 0;
    int o = 0;
    uint8_t* rmBuf;

    rmBuf = calcModRM(o1, o2, &rex, &len);
    if (rex)
	buf[o++] = 0x40 | rex;
    buf[o++] = (uint8_t) opc;
    while(len>0) {
	buf[o++] = *rmBuf++;
	len--;
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
    case OT_Imm32:
        *(uint32_t*)(buf + o) = (uint32_t) o2->val;
        o += 4;
        break;

    default: assert(0);
    }

    return o;
}


int genMov(uint8_t* buf, Operand* src, Operand* dst)
{
    assert(opValType(src) == opValType(dst));
    switch(dst->type) {
    case OT_Ind32:
    case OT_Ind64:
	// dst memory
	switch(src->type) {
	case OT_Reg32:
	case OT_Reg64:
	    // use 'mov r/m,r 32/64' (0x89)
	    return genModRM(buf, 0x89, dst, src);

        case OT_Imm32:
            // use 'mov r/m 32/64, imm 32' (0xC7/0)
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
	    // use 'mov r,r/m 32/64' (0x8B)
	    return genModRM(buf, 0x8B, src, dst);

        case OT_Imm32:
            // use 'mov r/m 32/64, imm 32' (0xC7/0)
            return genDigitMI(buf, 0xC7, 0, dst, src);

        case OT_Imm64: {
            // try to convert 64-bit immediate to 32bit if value fits
            Operand o;
            assert(src->val < (1l<<32));
            o.type = OT_Imm32;
            o.val = (uint32_t) src->val;
            return genDigitMI(buf, 0xC7, 0, dst, &o);
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
    assert(opValType(src) == opValType(dst));
    switch(src->type) {
    case OT_Reg32:
    case OT_Reg64:
	// src reg
	switch(dst->type) {
	case OT_Reg32:
	case OT_Reg64:
	case OT_Ind32:
	case OT_Ind64:
	    // use 'add r/m,r 32/64' (0x01)
	    return genModRM(buf, 0x01, dst, src);

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
            // use 'add r/m 32/64, imm32' (0x81/0)
            return genDigitMI(buf, 0x81, 0, dst, src);

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
    assert(opIsReg(dst));
    switch(dst->type) {
    case OT_Reg32:
    case OT_Reg64:
	// use 'lea r/m,r 32/64' (0x8d)
	return genModRM(buf, 0x8d, src, dst);

    default: assert(0);
    }
    return 0;
}



void capture(CodeStorage* cs, Instr* instr)
{
    uint8_t* buf;
    int used;

    if (cs == 0) return;

    printf("Capture '%s'\n", instr2string(instr, 0));

    buf = reserveCodeStorage(cs,15);
    switch(instr->type) {
    case IT_PUSH:
	used = genPush(buf, &(instr->dst));
	break;
    case IT_POP:
	used = genPop(buf, &(instr->dst));
	break;
    case IT_MOV:
	used = genMov(buf, &(instr->src), &(instr->dst));
	break;
    case IT_ADD:
	used = genAdd(buf, &(instr->src), &(instr->dst));
	break;
    case IT_LEA:
	used = genLea(buf, &(instr->src), &(instr->dst));
	break;
    case IT_RET:
	used = genRet(buf);
	break;
    default: assert(0);
    }
    assert(used < 15);
    useCodeStorage(cs, used);
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
 */

// emulator capture states
typedef enum _CaptureState {
    CS_DEAD = 0,      // uninitialized, should be invalid to access
    CS_DYNAMIC,       // data unknown at code generation time
    CS_STATIC,        // data known at code generation time
    CS_STACKRELATIVE, // address with known offset from stack top at start
    CS_Max
} CaptureState;


// emulator state. for memory, use the real memory apart from stack
typedef struct _EmuState {

    // general registers: Reg_AX .. Reg_R15
    uint64_t reg[Reg_Max];

    // stack
    int stacksize;
    uint8_t* stack;

    // capture state
    CaptureState reg_state[Reg_Max];
    CaptureState *stack_state;
} EmuState;

// a single value with type and capture state
typedef struct _EmuValue {
    uint64_t val;
    ValType type;
    CaptureState state;
} EmuValue;

#define CC_MAXPARAM 4
typedef struct _CaptureConfig
{
    CaptureState par_state[CC_MAXPARAM];
} CaptureConfig;

char captureState2Char(CaptureState s)
{
    assert((s >= 0) && (s < CS_Max));
    assert(CS_Max == 4);
    return "-DSR"[s];
}


void setCaptureConfig(Code* c, int constPos)
{
    CaptureConfig* cc;
    int i;

    if (c->cc)
        free(c->cc);

    cc = (CaptureConfig*) malloc(sizeof(CaptureConfig));
    for(i=0; i < CC_MAXPARAM; i++)
	cc->par_state[i] = CS_DYNAMIC;
    assert(constPos < CC_MAXPARAM);
    if (constPos >= 0)
        cc->par_state[constPos] = CS_STATIC;

    c->cc = cc;
}

void setCaptureConfig2(Code* c, int constPos1, int constPos2)
{
    CaptureConfig* cc;
    int i;

    if (c->cc)
        free(c->cc);

    cc = (CaptureConfig*) malloc(sizeof(CaptureConfig));
    for(i=0; i < CC_MAXPARAM; i++)
        cc->par_state[i] = CS_DYNAMIC;
    assert(constPos1 < CC_MAXPARAM);
    assert(constPos2 < CC_MAXPARAM);
    if (constPos1 >= 0)
        cc->par_state[constPos1] = CS_STATIC;
    if (constPos2 >= 0)
        cc->par_state[constPos2] = CS_STATIC;

    c->cc = cc;
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

    for(i=0; i< es->stacksize; i++)
        es->stack[i] = 0;
    for(i=0; i< es->stacksize; i++)
        es->stack_state[i] = CS_DEAD;

    for(i=0; i<Reg_Max; i++)
        es->reg[i] = 0;
    for(i=0; i<Reg_Max; i++)
        es->reg_state[i] = CS_DEAD;

    // calling convention:
    //  rbp, rbx, r12-r15 have to be preserved by callee
    for(i=0; calleeSave[i] != Reg_None; i++)
        es->reg_state[calleeSave[i]] = CS_DYNAMIC;
}

// use stack from cc emulator in c emulator
void useSameStack(Code* c, Code* cc)
{
    assert(cc->es != 0);
    assert(c->es == 0);

    c->es = (EmuState*) malloc(sizeof(EmuState));
    c->es->stacksize   = cc->es->stacksize;
    c->es->stack       = cc->es->stack;
    c->es->stack_state = cc->es->stack_state;
    resetEmuState(c->es);
}

void configEmuState(Code* c, int stacksize)
{
    if (c->es && c->es->stacksize != stacksize) {
        free(c->es->stack);
        free(c->es->stack_state);
        c->es->stack = 0;
    }
    if (!c->es) {
        c->es = (EmuState*) malloc(sizeof(EmuState));
        c->es->stacksize = stacksize;
        c->es->stack = 0;
    }
    if (!c->es->stack) {
        c->es->stack = (uint8_t*) malloc(stacksize);
        c->es->stack_state = (CaptureState*) malloc(sizeof(CaptureState) * stacksize);
    }

    resetEmuState(c->es);
}

void printEmuState(EmuState* es)
{
    int i;
    uint8_t *sp, *smin, *smax, *a, *aa;

    printf("Registers:\n");
    for(i=Reg_AX; i<Reg_8; i++) {
        printf(" %%r%-2s = 0x%016lx %c\n", regName(i), es->reg[i],
               captureState2Char( es->reg_state[i] ));
    }
    printf("Stack:\n");
    sp   = (uint8_t*) es->reg[Reg_SP];
    smax = (uint8_t*) (es->reg[Reg_SP]/8*8 + 24);
    smin = (uint8_t*) (es->reg[Reg_SP]/8*8 - 32);
    if (smin < es->stack)
	smin = es->stack;
    if (smax >= es->stack + es->stacksize)
        smax = es->stack + es->stacksize -1;
    for(a = smin; a <= smax; a += 8) {
	printf(" %016lx ", (uint64_t)a);
	for(aa = a; aa < a+8 && aa <= smax; aa++) {
            printf(" %s%02x %c", (aa == sp) ? "*" : " ", *aa,
                   captureState2Char(es->stack_state[aa - es->stack]));
	}
	printf("\n");
    }
    printf(" %016lx  %s\n", (uint64_t)a, (a == sp) ? "*" : " ");
}

char combineState(CaptureState s1, CaptureState s2, Bool isSameValue)
{
    // dead/invalid: combining with something invalid makes result invalid
    if ((s1 == CS_DEAD) || (s2 == CS_DEAD)) return CS_DEAD;

    // if both are static, static-ness is preserved
    if ((s1 == CS_STATIC) && (s2 == CS_STATIC)) return CS_STATIC;

    // stack-relative handling:
    // depends on combining of sub-state of one value or combining two values
    if (isSameValue) {
      // if both are stack-relative, it is preserved
      if ((s1 == CS_STACKRELATIVE) && (s2 == CS_STACKRELATIVE))
          return CS_STACKRELATIVE;
    }
    else {
        // STACKRELATIVE is preserved if other is STATIC
        if ((s1 == CS_STACKRELATIVE) && (s2 == CS_STATIC))
            return CS_STACKRELATIVE;
        if ((s1 == CS_STATIC) && (s2 == CS_STACKRELATIVE))
            return CS_STACKRELATIVE;
    }

    return CS_DYNAMIC;
}

// if addr on stack, return true and stack offset in <off>,
//  otherwise return false
// the returned offset is static only if address is stack-relative
Bool getStackOffset(EmuState* es, EmuValue* addr, EmuValue* off)
{
    uint8_t* a = (uint8_t*) addr->val;
    if ((a >= es->stack) && (a < es->stack + es->stacksize)) {
        off->type = VT_32;
        off->state = (addr->state == CS_STACKRELATIVE) ? CS_STATIC : CS_DYNAMIC;
        off->val = a - es->stack;
        return True;
    }
    return False;
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
    CaptureState state;
    int i, isOnStack;

    isOnStack = getStackOffset(es, addr, &off);
    if (isOnStack) {
        if (off.state == CS_STATIC)
            state = es->stack_state[off.val];
        else
            state = CS_DYNAMIC;
    }
    else {
        assert(!shouldBeStack);
        state = CS_DYNAMIC;
    }

    switch(t) {
    case VT_32:
        v->val = *(uint32_t*) addr->val;
        if (isOnStack && (off.state == CS_STATIC)) {
            for(i=1; i<4; i++)
                state = combineState(state, es->stack_state[off.val + i], 1);
        }
	break;

    case VT_64:
        v->val = *(uint64_t*) addr->val;
        if (isOnStack && (off.state == CS_STATIC)) {
            for(i=1; i<8; i++)
                state = combineState(state, es->stack_state[off.val + i], 1);
        }
	break;

    default: assert(0);
    }
    v->type = t;
    v->state = state;
}

void setMemValue(EmuValue* v, EmuValue* addr, EmuState* es, ValType t,
                 int shouldBeStack)
{
    EmuValue off;
    uint32_t* a32;
    uint64_t* a64;
    int i;
    Bool isOnStack;

    isOnStack = getStackOffset(es, addr, &off);
    if (!isOnStack)
        assert(!shouldBeStack);

    assert(v->type == t);
    switch(t) {
    case VT_32:
        a32 = (uint32_t*) addr->val;
	*a32 = (uint32_t) v->val;
        if (isOnStack && (off.state == CS_STATIC)) {
            for(i=0; i<4; i++)
                es->stack_state[off.val + i] = v->state;
        }
	break;

    case VT_64:
        a64 = (uint64_t*) addr->val;
	*a64 = (uint64_t) v->val;
        if (isOnStack && (off.state == CS_STATIC)) {
            for(i=0; i<8; i++)
                es->stack_state[off.val + i] = v->state;
        }
	break;

    default: assert(0);
    }
}

void addRegToValue(EmuValue* v, EmuState* es, Reg r, int scale)
{
    if (r == Reg_None) return;

    v->state = combineState(v->state, es->reg_state[r], 0);
    v->val += scale * es->reg[r];
}


void getOpAddr(EmuValue* v, EmuState* es, Operand* o)
{
    assert(opIsInd(o));

     v->type = VT_64;
     v->val = o->val;
     v->state = CS_STATIC;

     if (o->reg != Reg_None)
         addRegToValue(v, es, o->reg, 1);

     if (o->scale > 0)
         addRegToValue(v, es, o->ireg, o->scale);
}

// returned value should be casted to expected type (8/16/32 bit)
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

// false if not on stack or stack offset not static/known
Bool keepsCaptureState(EmuState* es, Operand* o)
{
    EmuValue addr;
    EmuValue off;
    Bool isOnStack;

    assert(!opIsImm(o));
    if (opIsReg(o)) return 1;

    getOpAddr(&addr, es, o);
    isOnStack = getStackOffset(es, &addr, &off);
    if (!isOnStack) return 0;
    return (off.state == CS_STATIC);
}

void applyStaticToInd(Operand* o, EmuState* es)
{
    if (!opIsInd(o)) return;

    if ((o->reg != Reg_None) && (es->reg_state[o->reg] == CS_STATIC)) {
        o->val += es->reg[o->reg];
        o->reg = Reg_None;
    }
    if ((o->scale > 0) && (es->reg_state[o->ireg] == CS_STATIC)) {
        o->val += o->scale * es->reg[o->ireg];
        o->scale = 0;
    }
}

void captureMov(CodeStorage* cs, Instr* orig, EmuState* es, EmuValue* res)
{
    Instr i;
    Operand *o;

    // data movement from orig->src to orig->dst, value is res

    if (res->state == CS_DEAD) return;

    o = &(orig->src);
    if (res->state == CS_STATIC) {
        // no need to update data if capture state is maintained
        if (keepsCaptureState(es, &(orig->dst))) return;

	// source is static, use immediate
        o = getImmOp(res->type, res->val);
    }
    initBinaryInstr(&i, IT_MOV, &(orig->dst), o);
    applyStaticToInd(&(i.dst), es);
    applyStaticToInd(&(i.src), es);
    capture(cs, &i);
}

void captureAdd(CodeStorage* cs, Instr* orig, EmuState* es, EmuValue* res)
{
    EmuValue opval;
    Instr i;
    Operand *o;

    if (res->state == CS_DEAD) return;

    if (res->state == CS_STATIC) {
        // no need to update data if capture state is maintained
        if (keepsCaptureState(es, &(orig->dst))) return;

        // if result is known and goes to memory, generate imm store
        initBinaryInstr(&i, IT_MOV,
                        &(orig->dst), getImmOp(res->type, res->val));
        applyStaticToInd(&(i.dst), es);
	capture(cs, &i);
	return;
    }

    // if 2nd source (=dst) is known/constant and a reg, we need to update it
    getOpValue(&opval, es, &(orig->dst));
    if (!opIsInd(&(orig->dst)) && (opval.state == CS_STATIC)) {
        initBinaryInstr(&i, IT_MOV,
                        &(orig->dst), getImmOp(opval.type, opval.val));
	capture(cs, &i);
    }

    o = &(orig->src);
    getOpValue(&opval, es, &(orig->src));
    if (!opIsInd(&(orig->src)) && (opval.state == CS_STATIC)) {
	// if 1st source (=src) is known/constant and a reg, make it immediate
        o = getImmOp(opval.type, opval.val);
    }
    initBinaryInstr(&i, IT_ADD, &(orig->dst), o);
    applyStaticToInd(&(i.dst), es);
    applyStaticToInd(&(i.src), es);
    capture(cs, &i);
}

void captureLea(CodeStorage* cs, Instr* orig, EmuState* es, EmuValue* res)
{
    Instr i;

    if (res->state == CS_STATIC) return;
    initBinaryInstr(&i, IT_LEA, &(orig->dst), &(orig->src));
    applyStaticToInd(&(i.src), es);
    capture(cs, &i);
}

void captureRet(CodeStorage* cs, Instr* orig, EmuState* es)
{
    EmuValue v;
    Instr i;

    getRegValue(&v, es, Reg_AX, VT_64);
    if (v.state == CS_STATIC) {
        initBinaryInstr(&i, IT_MOV,
                        getRegOp(VT_64, Reg_AX), getImmOp(v.type, v.val));
	capture(cs, &i);
    }
    capture(cs, orig);
}


uint64_t emulate(Code* c, ...)
{
    // calling convention x86-64: parameters are stored in registers
    static Reg parReg[4] = { Reg_DI, Reg_SI, Reg_DX, Reg_CX };

    EmuValue v1, v2, addr;
    int i, foundRet;
    uint64_t par[4];
    EmuState* es;

    // setup int parameters for virtual CPU according to x86_64 calling conv.
    // see https://en.wikipedia.org/wiki/X86_calling_conventions
    asm("mov %%rsi, %0;" : "=r" (par[0]) : );
    asm("mov %%rdx, %0;" : "=r" (par[1]) : );
    asm("mov %%rcx, %0;" : "=r" (par[2]) : );
    asm("mov %%r8, %0;"  : "=r" (par[3]) : );

    if (!c->es) configEmuState(c, 1024);
    resetEmuState(c->es);
    if (c->cs) c->cs->used = 0;
    es = c->es;

    for(i=0;i<4;i++) {
        es->reg[parReg[i]] = par[i];
        es->reg_state[parReg[i]] = c->cc ? c->cc->par_state[i] : CS_DYNAMIC;
    }

    es->reg[Reg_SP] = (uint64_t) (es->stack + es->stacksize);
    es->reg_state[Reg_SP] = CS_STACKRELATIVE;

    foundRet = 0;
    i = 0;
    while((i < c->count) && !foundRet) {

	Instr* instr = c->instr + i;
        printEmuState(es);
        printf("Emulate '%s'\n", instr2string(instr, 0));

	// for RIP-relative accesses
        es->reg[Reg_IP] = instr->addr + instr->len;

	switch(instr->type) {
	case IT_PUSH:
	    switch(instr->dst.type) {
	    case OT_Reg32:
                es->reg[Reg_SP] -= 4;
                addr = emuValue(es->reg[Reg_SP], VT_64, es->reg_state[Reg_SP]);
                getOpValue(&v1, es, &(instr->dst));
                setMemValue(&v1, &addr, es, VT_32, 1);
                if (v1.state == CS_DYNAMIC)
		    capture(c->cs, instr);
		break;

	    case OT_Reg64:
                es->reg[Reg_SP] -= 8;
                addr = emuValue(es->reg[Reg_SP], VT_64, es->reg_state[Reg_SP]);
                getOpValue(&v1, es, &(instr->dst));
                setMemValue(&v1, &addr, es, VT_64, 1);
                if (v1.state == CS_DYNAMIC)
		    capture(c->cs, instr);
		break;

	    default: assert(0);
	    }
	    break;

	case IT_POP:
	    switch(instr->dst.type) {
	    case OT_Reg32:
                addr = emuValue(es->reg[Reg_SP], VT_64, es->reg_state[Reg_SP]);
                getMemValue(&v1, &addr, es, VT_32, 1);
                setOpValue(&v1, es, &(instr->dst));
                es->reg[Reg_SP] += 4;
		if (v1.state == CS_DYNAMIC)
		    capture(c->cs, instr);
		break;

	    case OT_Reg64:
                addr = emuValue(es->reg[Reg_SP], VT_64, es->reg_state[Reg_SP]);
                getMemValue(&v1, &addr, es, VT_64, 1);
                setOpValue(&v1, es, &(instr->dst));
                es->reg[Reg_SP] += 8;
		if (v1.state == CS_DYNAMIC)
		    capture(c->cs, instr);
		break;

	    default: assert(0);
	    }
	    break;

	case IT_MOV:
	    switch(instr->src.type) {
	    case OT_Reg32:
	    case OT_Ind32:
            case OT_Imm32: {
                ValType dst_t = opValType(&(instr->dst));
                assert(dst_t == VT_32 || dst_t == VT_64);
                getOpValue(&v1, es, &(instr->src));
                if (dst_t == VT_64) {
                    // sign extend lower 32 bit to 64 bit
                    v1.val = (int32_t) v1.val;
                    v1.type = VT_64;
                }
                setOpValue(&v1, es, &(instr->dst));
                captureMov(c->cs, instr, es, &v1);
		break;
            }

	    case OT_Reg64:
	    case OT_Ind64:
            case OT_Imm64:
		assert(opValType(&(instr->dst)) == VT_64);
                getOpValue(&v1, es, &(instr->src));
                setOpValue(&v1, es, &(instr->dst));
                captureMov(c->cs, instr, es, &v1);
		break;

	    default:assert(0);
	    }
	    break;

	case IT_ADD:
	    switch(instr->src.type) {
	    case OT_Reg32:
	    case OT_Ind32:
		assert(opValType(&(instr->dst)) == VT_32);
                getOpValue(&v1, es, &(instr->src));
                getOpValue(&v2, es, &(instr->dst));
		v1.val = ((uint32_t) v1.val + (uint32_t) v2.val);
                v1.state = combineState(v1.state, v2.state, 0);
                // for capture we need state of dst, do before setting dst
                captureAdd(c->cs, instr, es, &v1);
                setOpValue(&v1, es, &(instr->dst));
		break;

	    case OT_Reg64:
	    case OT_Ind64:
		assert(opValType(&(instr->dst)) == VT_64);
                getOpValue(&v1, es, &(instr->src));
                getOpValue(&v2, es, &(instr->dst));
		v1.val = v1.val + v2.val;
                v1.state = combineState(v1.state, v2.state, 0);
                // for capture we need state of dst, do before setting dst
                captureAdd(c->cs, instr, es, &v1);
                setOpValue(&v1, es, &(instr->dst));

            case OT_Imm32: {
                ValType dst_ot = opValType(&(instr->dst));
                assert(dst_ot == VT_32 || dst_ot == VT_64);
                getOpValue(&v1, es, &(instr->src));
                getOpValue(&v2, es, &(instr->dst));
                if (dst_ot == VT_32)
                    v1.val = ((uint32_t) v1.val + (uint32_t) v2.val);
                else
                    v1.val = (uint64_t) ((int32_t) v1.val + (int64_t) v2.val);
                v1.state = combineState(v1.state, v2.state, 0);
                // for capture we need state of dst, do before setting dst
                captureAdd(c->cs, instr, es, &v1);
                setOpValue(&v1, es, &(instr->dst));
                break;
            }

	    default:assert(0);
	    }
	    break;

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
                setOpValue(&v1, es, &(instr->dst));
                captureLea(c->cs, instr, es, &v1);
		break;

	    default:assert(0);
	    }
	    break;

	case IT_RET:
	    // TODO: if AX constant, generate mov imm, rax
            captureRet(c->cs, instr, es);
	    foundRet = 1;
	    break;

	default: assert(0);
	}
	i++;
    }

    printEmuState(es);

    // return value according to calling convention
    return es->reg[Reg_AX];
}

