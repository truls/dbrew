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

    fprintf(stderr, "Allocated Code Storage (size %d)\n", fullsize);

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
    // buffer to capture emulation
    CodeStorage* cs;
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

    return c;
}

uint8_t* capturedCode(Code* c)
{
    if ((c->cs == 0) || (c->cs->used == 0))
	return 0;

    return c->cs->buf;
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

int opWidth(OpType ot)
{
    switch(ot) {
    case OT_Imm8:
    case OT_Reg8:
    case OT_Ind8:
	return 8;
    case OT_Imm16:
    case OT_Reg16:
    case OT_Ind16:
	return 16;
    case OT_Imm32:
    case OT_Reg32:
    case OT_Ind32:
	return 32;
    case OT_Imm64:
    case OT_Reg64:
    case OT_Ind64:
	return 64;
    default: assert(0);
    }
    return 0; // invalid;
}

int opIsImm(OpType ot)
{
    switch(ot) {
    case OT_Imm8:
    case OT_Imm16:
    case OT_Imm32:
    case OT_Imm64:
	return 1;
    }
    return 0;
}

int opIsReg(OpType ot)
{
    switch(ot) {
    case OT_Reg8:
    case OT_Reg16:
    case OT_Reg32:
    case OT_Reg64:
	return 1;
    }
    return 0;
}

int opIsInd(OpType ot)
{
    switch(ot) {
    case OT_Ind8:
    case OT_Ind16:
    case OT_Ind32:
    case OT_Ind64:
	return 1;
    }
    return 0;
}


Operand* getRegOp(int w, Reg r)
{
    static Operand o;
    switch(w) {
    case 32:
	assert((r >= Reg_AX) && (r <= Reg_15));
	o.type = OT_Reg32;
	o.reg = r;
	o.scale = 0;
	break;

    case 64:
	assert((r >= Reg_AX) && (r <= Reg_15));
	o.type = OT_Reg64;
	o.reg = r;
	o.scale = 0;
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
	assert((src->reg >= Reg_AX) && (src->reg <= Reg_IP));
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


// op2 always reg. Encoding see SDM 2.1
int parseModRM(uint8_t* p, int rex, Operand* o1, Operand* o2)
{
    int modrm, mod, rm, reg; // modRM byte
    int sib, scale, idx, base; // SIB byte
    int64_t disp;
    Reg r;
    int o = 0;
    int hasRex = (rex>0);

    modrm = p[o++];
    mod = (modrm & 192) >> 6;
    reg = (modrm & 56) >> 3;
    rm = modrm & 7;

    // Operand 2: always reg
    r = Reg_AX + reg;
    if (hasRex && (rex & REX_MASK_R)) reg += 8;
    OpType reg_ot = (hasRex && (rex & REX_MASK_W)) ? OT_Reg64 : OT_Reg32;
    o2->type = reg_ot;
    o2->reg = r;

    if (mod == 3) {
	// r, r
	r = Reg_AX + rm;
	if (hasRex && (rex & REX_MASK_B)) r += 8;
	o1->type = reg_ot;
	o1->reg = r;
	return o;
    }

    scale = 0;
    if (rm == 4) {
	// SIB
	sib = p[o++];
	scale = 1 << ((sib & 192) >> 6);
	idx   = (sib & 56) >> 3;
	base  = sib & 7;
    }

    disp = 0;
    if (mod == 1) {
	// 8bit disp: sign extend
	disp = *((signed char*) (p+o));
	o++;
    }
    else if ((mod == 2) || ((mod == 0) && (rm == 5))) {
	// mod 2 + rm 5: RIP relative
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

    return o;
}

void decodeFunc(Code* c, uint8_t* fp, int max, int stopAtRet)
{
    int hasRex, rex; // REX prefix
    uint64_t a;
    int i, o, retFound, opc;

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
		       IT_PUSH, getRegOp(64, Reg_AX+(opc-0x50)));
	    break;

	case 0x58: case 0x59: case 0x5A: case 0x5B:
	case 0x5C: case 0x5D: case 0x5E: case 0x5F:
	    // pop
	    addUnaryOp(c, a, (uint64_t)(fp + o),
		       IT_POP, getRegOp(64, Reg_AX+(opc-0x58)));
	    break;

	case 0x89: {
	    // mov r/m,r 32/64 (dst: r/m, src: r)
	    Operand o1, o2;
	    o += parseModRM(fp+o, hasRex ? rex:0, &o1, &o2);
	    addBinaryOp(c, a, (uint64_t)(fp + o),
			IT_MOV, &o1, &o2);
	    break;
	}

	case 0x8B: {
	    // mov r,r/m 32/64 (dst: r, src: r/m)
	    Operand o1, o2;
	    o += parseModRM(fp+o, hasRex ? rex:0, &o2, &o1);
	    addBinaryOp(c, a, (uint64_t)(fp + o),
			IT_MOV, &o1, &o2);
	    break;
	}

	case 0x01: {
	    // add r/m,r 32/64 (dst: r/m, src: r)
	    Operand o1, o2;
	    o += parseModRM(fp+o, hasRex ? rex:0, &o1, &o2);
	    addBinaryOp(c, a, (uint64_t)(fp + o),
			IT_ADD, &o1, &o2);
	    break;
	}

	case 0x8d: {
	    // lea r32/64,m
	    Operand o1, o2;
	    o += parseModRM(fp+o, hasRex ? rex:0, &o2, &o1);
	    assert(opIsInd(o2.type)); // TODO: bad code error
	    addBinaryOp(c, a, (uint64_t)(fp + o),
			IT_LEA, &o1, &o2);
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
	if (o->scale == 0)
	    sprintf(buf+off,"(%%r%s)", regName(o->reg));
	else {
	    char* rb = (o->reg == Reg_None) ? "" : regName(o->reg);
	    char* ri = regName(o->ireg);
	    sprintf(buf+off,"(%%r%s,%%r%s,%d)", rb, ri, o->scale);
	}
	break;
    default: assert(0);
    }
    return buf;
}

char* instr2string(Instr* instr)
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
    off += sprintf(buf, "%-6s", n);
    if (oc == 1)
	off += sprintf(buf+off, "%s", op2string(&(instr->dst)));
    if (oc == 2) {
	off += sprintf(buf+off, "%s", op2string(&(instr->src)));
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
	       bytes2string(c->instr + i, 0, 6), instr2string(c->instr + i));
	if (c->instr[i].len > 6)
	    printf("  %p %s\n", (void*)c->instr[i].addr + 6,
		   bytes2string(c->instr + i, 6, 6));
	if (c->instr[i].len > 12)
	    printf("  %p %s\n", (void*)c->instr[i].addr + 12,
		   bytes2string(c->instr + i, 12, 6));
    }
}

/*------------------------------------------------------------*/
/* x86_64 code generation
 */

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

uint8_t* calcModRM(Operand* o1, Operand* o2, int* prex, int* plen)
{
    static uint8_t buf[10];
    int modrm, r1, r2;
    int o = 0, useDisp8 = 0, useDisp32 = 0;

    assert(opWidth(o1->type) == opWidth(o2->type));
    assert((opWidth(o1->type) == 32) || (opWidth(o1->type) == 64));
    assert(opIsReg(o1->type) || opIsInd(o1->type));
    assert(opIsReg(o2->type));

    if (opWidth(o1->type) == 64) *prex |= REX_MASK_W;

    // o2 always r
    r2 = o2->reg - Reg_AX;
    if (r2 & 8) *prex |= REX_MASK_R;
    modrm = (r2 & 7) << 3;

    if (opIsReg(o1->type)) {
	// r,r: mod 3
	modrm |= 192;
	r1 = o1->reg - Reg_AX;
	if (r1 & 8) *prex |= REX_MASK_B;
	modrm |= (r1 & 7);
	buf[o++] = modrm;
    }
    else {
	int64_t v = (int64_t) o1->val;
	if (v != 0) {
	    if ((v >= -128) && (v<128)) useDisp8 = 1;
	    else if ((v >= -((int64_t)1<<31)) &&
		     (v < ((int64_t)1<<31))) useDisp32 = 1;
	    else assert(0);
	}
	if (useDisp8) modrm |= 64;
	if (useDisp32) modrm |= 128;

	if (o1->scale == 0) {
	    assert(o1->reg != Reg_SP); // rm 4 reserved for SIB encoding
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
	    buf[o++] = modrm;
	}
	else {
	    // SIB
	    int sib = 0, ri, rb;
	    if      (o1->scale == 2) sib |= 64;
	    else if (o1->scale == 4) sib |= 128;
	    else if (o1->scale == 8) sib |= 192;
	    else
		assert(o1->scale == 1);
	    ri = o1->ireg - Reg_AX;
	    if (ri & 8) *prex |= REX_MASK_X;
	    sib |= (ri & 7) <<3;
	    rb = o1->reg - Reg_AX;
	    if (rb & 8) *prex |= REX_MASK_B;
	    sib |= (rb & 7);
	    modrm |= 4; // signal SIB
	    buf[o++] = modrm;
	    buf[o++] = sib;
	}

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

// Operand o1: r/m, o2: r
int genModRM(uint8_t* buf, int opc, Operand* o1, Operand* o2)
{
    int rex = 0, len = 0;
    int o = 0;
    uint8_t* rmBuf = calcModRM(o1, o2, &rex, &len);

    if (rex)
	buf[o++] = 0x40 | rex;
    buf[o++] = (uint8_t) opc;
    while(len>0) {
	buf[o++] = *rmBuf++;
	len--;
    }
    return o;
}

int genMov(uint8_t* buf, Operand* src, Operand* dst)
{
    assert(opWidth(src->type) == opWidth(dst->type));
    switch(dst->type) {
    case OT_Ind32:
    case OT_Ind64:
	// dst memory
	switch(src->type) {
	case OT_Reg32:
	case OT_Reg64:
	    // use 'mov r/m,r 32/64' (0x89)
	    return genModRM(buf, 0x89, dst, src);

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

	default: assert(0);
	}
	break;

    default: assert(0);
    }
    return 0;
}

int genAdd(uint8_t* buf, Operand* src, Operand* dst)
{
    assert(opWidth(src->type) == opWidth(dst->type));
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

    default: assert(0);
    }
    return 0;
}

int genLea(uint8_t* buf, Operand* src, Operand* dst)
{
    assert(opIsInd(src->type));
    assert(opIsReg(dst->type));
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
/* x86_64 emulator
 */

// emulator state. for memory, use the real memory apart from stack
typedef struct _EState {

    // general registers: Reg_AX .. Reg_R15
    uint64_t r[Reg_Max];

    int stack_capacity;
    uint8_t* stack;

} EmuState;

EmuState emuState;

void initEmulatorState(int stacksize)
{
    int i;

    emuState.stack = (uint8_t*) malloc(stacksize);
    emuState.stack_capacity = stacksize;

    for(i=0; i<stacksize; i++)
	emuState.stack[i] = 0;
    for(i=0; i<Reg_Max; i++)
	emuState.r[i] = 0;
}

void printEState(EmuState* es)
{
    int i;
    uint8_t *sp, *smin, *smax, *a, *aa;

    printf("Registers:\n");
    for(i=Reg_AX; i<Reg_8; i++) {
	printf(" %%r%-2s = 0x%016lx\n", regName(i), es->r[i]);
    }
    printf("Stack:\n");
    sp   = (uint8_t*) es->r[Reg_SP];
    smax = (uint8_t*) (es->r[Reg_SP]/8*8 + 24);
    smin = (uint8_t*) (es->r[Reg_SP]/8*8 - 16);
    if (smin < es->stack)
	smin = es->stack;
    if (smax >= es->stack + es->stack_capacity)
	smax = es->stack + es->stack_capacity -1;
    for(a = smin; a <= smax; a += 8) {
	printf(" %016lx ", (uint64_t)a);
	for(aa = a; aa < a+8 && aa <= smax; aa++)
	    printf(" %s%02x", (aa == sp) ? "*" : " ", *aa);
	printf("\n");
    }
}

uint64_t getOpAddr(EmuState* es, Operand* o)
{
    uint64_t a;

    assert((o->type == OT_Ind8)  || (o->type == OT_Ind16) ||
	   (o->type == OT_Ind32) || (o->type == OT_Ind64));

    a = o->val;
    if (o->reg != Reg_None) a += es->r[o->reg];
    if (o->scale>0) a += o->scale * es->r[o->ireg];

    return a;
}

// returned value should be casted to expected type (8/16/32 bit)
uint64_t getOpValue(EmuState* es, Operand* o)
{
    switch(o->type) {
    case OT_Reg32:
	return (uint32_t) es->r[o->reg];

    case OT_Reg64:
	return es->r[o->reg];

    case OT_Ind32:
	return *(uint32_t*) getOpAddr(es, o);

    case OT_Ind64:
	return *(uint64_t*) getOpAddr(es, o);

    default: assert(0);
    }
    return 0;
}

// only the bits of v are used which are required for operand type
void setOpValue(EmuState* es, Operand* o, uint64_t v)
{
    uint32_t* a32;
    uint64_t* a64;

    switch(o->type) {
    case OT_Reg32:
	es->r[o->reg] = (uint32_t) v;
	return;

    case OT_Reg64:
	es->r[o->reg] = v;
	return;

    case OT_Ind32:
	a32 = (uint32_t*) getOpAddr(es, o);
	*a32 = (uint32_t) v;
	return;

    case OT_Ind64:
	a64 = (uint64_t*) getOpAddr(es, o);
	*a64 = v;
	return;

    default: assert(0);
    }
}

void checkStackAddr(EmuState* es)
{
    uint8_t* a = (uint8_t*) es->r[Reg_SP];
    assert((a >= es->stack) && (a < es->stack + es->stack_capacity));
}


uint64_t emulate(Code* c, ...)
{
    int i, foundRet;
    uint32_t* a32;
    uint64_t* a64;
    uint32_t v32;
    uint64_t v64;

    // setup int parameters for virtual CPU according to x86_64 calling conv.
    // see https://en.wikipedia.org/wiki/X86_calling_conventions
    asm("mov %%rsi, %0;" : "=r" (v64) : ); emuState.r[Reg_DI] = v64;
    asm("mov %%rdx, %0;" : "=r" (v64) : ); emuState.r[Reg_SI] = v64;
    asm("mov %%rcx, %0;" : "=r" (v64) : ); emuState.r[Reg_DX] = v64;
    asm("mov %%r8, %0;" : "=r" (v64) : );  emuState.r[Reg_CX] = v64;
    asm("mov %%r9, %0;" : "=r" (v64) : );  emuState.r[Reg_8] = v64;
    emuState.r[Reg_SP] = (uint64_t) (emuState.stack + emuState.stack_capacity);

    printEState(&emuState);

    foundRet = 0;
    i = 0;
    while((i < c->count) && !foundRet) {

	Instr* instr = c->instr + i;
	// printEState(&emuState);
	printf("Emulating '%s'...\n", instr2string(instr));

	// for RIP-relative accesses
	emuState.r[Reg_IP] = instr->addr + instr->len;

	switch(instr->type) {
	case IT_PUSH:
	    switch(instr->dst.type) {
	    case OT_Reg32:
		emuState.r[Reg_SP] -= 4;
		checkStackAddr(&emuState);
		a32 = (uint32_t*) emuState.r[Reg_SP];
		*a32 = (uint32_t) getOpValue(&emuState, &(instr->dst));
		capture(c->cs, instr);
		break;

	    case OT_Reg64:
		emuState.r[Reg_SP] -= 8;
		checkStackAddr(&emuState);
		a64 = (uint64_t*) emuState.r[Reg_SP];
		*a64 = (uint64_t) getOpValue(&emuState, &(instr->dst));
		capture(c->cs, instr);
		break;

	    default: assert(0);
	    }
	    break;

	case IT_POP:
	    switch(instr->dst.type) {
	    case OT_Reg32:
		checkStackAddr(&emuState);
		a32 = (uint32_t*) emuState.r[Reg_SP];
		setOpValue(&emuState, &(instr->dst), *a32);
		emuState.r[Reg_SP] += 4;
		capture(c->cs, instr);
		break;

	    case OT_Reg64:
		checkStackAddr(&emuState);
		a64 = (uint64_t*) emuState.r[Reg_SP];
		setOpValue(&emuState, &(instr->dst), *a64);
		emuState.r[Reg_SP] += 8;
		capture(c->cs, instr);
		break;

	    default: assert(0);
	    }
	    break;

	case IT_MOV:
	    switch(instr->src.type) {
	    case OT_Reg32:
	    case OT_Ind32:
		assert(opWidth(instr->dst.type) == 32);
		v32 = (uint32_t) getOpValue(&emuState, &(instr->src));
		setOpValue(&emuState, &(instr->dst), v32);
		capture(c->cs, instr);
		break;

	    case OT_Reg64:
	    case OT_Ind64:
		assert(opWidth(instr->dst.type) == 64);
		v64 = (uint64_t) getOpValue(&emuState, &(instr->src));
		setOpValue(&emuState, &(instr->dst), v64);
		capture(c->cs, instr);
		break;

	    default:assert(0);
	    }
	    break;

	case IT_ADD:
	    switch(instr->src.type) {
	    case OT_Reg32:
	    case OT_Ind32:
		assert(opWidth(instr->dst.type) == 32);
		v32 = (uint32_t) getOpValue(&emuState, &(instr->src));
		v32 += (uint32_t) getOpValue(&emuState, &(instr->dst));
		setOpValue(&emuState, &(instr->dst), v32);
		capture(c->cs, instr);
		break;

	    case OT_Reg64:
	    case OT_Ind64:
		assert(opWidth(instr->dst.type) == 64);
		v64 = (uint64_t) getOpValue(&emuState, &(instr->src));
		v64 += (uint64_t) getOpValue(&emuState, &(instr->dst));
		setOpValue(&emuState, &(instr->dst), v64);
		capture(c->cs, instr);
		break;

	    default:assert(0);
	    }
	    break;

	case IT_LEA:
	    switch(instr->dst.type) {
	    case OT_Reg64:
		assert(opIsInd(instr->src.type));
		v64 = (uint64_t) getOpAddr(&emuState, &(instr->src));
		setOpValue(&emuState, &(instr->dst), v64);
		capture(c->cs, instr);
		break;

	    case OT_Reg32:
		assert(opIsInd(instr->src.type));
		v32 = (uint32_t) getOpAddr(&emuState, &(instr->src));
		setOpValue(&emuState, &(instr->dst), v32);
		capture(c->cs, instr);
		break;

	    default:assert(0);
	    }
	    break;

	case IT_RET:
	    capture(c->cs, instr);
	    foundRet = 1;
	    break;

	default: assert(0);
	}
	i++;
    }

    printEState(&emuState);

    // return value according calling convention
    return emuState.r[Reg_AX];
}


/*------------------------------------------------------------*/
/* x86_64 test/specialize functions
 */

void_func spec2(uint8_t* f, ...)
{
    uint8_t* p;
    Code* c;

    c = allocCode(100, 0);
    decodeFunc(c, f, 100, 1);

    CodeStorage* cs = initCodeStorage(4096);
    p = useCodeStorage(cs, 50);

    // TODO: Specialize for constant parameter 2
    memcpy(p, (uint8_t*)f, 50);

    return (void_func)p;
}
