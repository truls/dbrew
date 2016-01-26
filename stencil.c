#include <stdio.h>
#include <stdlib.h>
#include "spec.h"

//#define USEINT

#ifdef USEINT
#define TYPE int
#else
#define TYPE double
#endif

typedef struct {
    int xdiff, ydiff;
    TYPE factor;
} StencilPoint;

typedef struct {
    int points;
    StencilPoint p[];
} Stencil;

typedef struct {
    TYPE factor;
    int points;
    StencilPoint* p;
} StencilFactor;

typedef struct {
    int factors;
    StencilFactor f[];
} SortedStencil;

#ifdef USEINT
#define COEFF1 (-4)
#define COEFF2 (1)

Stencil s5 = {5,{{0,0,-4},{-1,0,1},{1,0,1},{0,-1,1},{0,1,1}}};

#else
#define COEFF1 (-.2)
#define COEFF2 (.3)

Stencil s5 = {5, {{0,0,-.2},
                  {-1,0,.3},{1,0,.3},{0,-1,.3},{0,1,.3}}};

SortedStencil s5s = {2, {{-.2,1,&(s5.p[0])},{.3,4,&(s5.p[1])}}};

#endif


typedef TYPE (*apply_func)(TYPE*, int, Stencil*);

TYPE apply(TYPE *m, int xsize, Stencil* s)
{
    TYPE res;
    int i;

    res = 0;
    for(i=0; i<s->points; i++) {
        StencilPoint* p = s->p + i;
        res += p->factor * m[p->xdiff + p->ydiff * xsize];
    }
    return res;
}

TYPE applyS(TYPE *m, int xsize, SortedStencil* s)
{
    TYPE sum, res;
    int f, i;

    res = 0;
    sum = 0;
    for(f=0; f < s->factors; f++) {
        StencilFactor* sf = s->f + f;
        StencilPoint* p = sf->p;
        sum = m[p->xdiff + p->ydiff * xsize];
        for(i=1; i < sf->points; i++) {
            p = sf->p + i;
            sum += m[p->xdiff + p->ydiff * xsize];
        }
        res += sf->factor * sum;
    }
    return res;
}

TYPE apply2(TYPE *m, int xsize, Stencil* s)
{
    return COEFF1 * m[0] + COEFF2 * (m[-1] + m[1] + m[-xsize] + m[xsize]);
}

TYPE apply3(TYPE *m, int xsize, Stencil* s)
{
    return m[0];
}


typedef void (*apply_loop)(int, TYPE*, TYPE*, apply_func, Stencil*);

#if 1
void applyLoop(int size, TYPE* src, TYPE* dst, apply_func af, Stencil* s)
{
    int x,y;

    for(y=1;y<size-1;y++)
        for(x=1;x<size-1;x++)
            dst[x+y*size] = af(&(src[x+y*size]), size, s);
}
#else
void applyLoop(int size, TYPE* src, TYPE* dst, apply_func af, Stencil* s)
{
    int x,y;
    int f = makeDynamic(1), t = makeDynamic(size-1);

    for(y=f;y<t;y++)
        for(x=f;x<t;x++)
            dst[x+y*size] = af(&(src[x+y*size]), size, s);
}
#endif

// only top line
void applyLoop1(int size, TYPE* src, TYPE* dst, apply_func af, Stencil* s)
{
    int x;
    for(x=1;x<size-1;x++)
        dst[x+size] = af(&(src[x+size]), size, s);
}


int main(int argc, char* argv[])
{
    int i, x, y;
    TYPE *m1, *m2, diff;
    int size = 0, iter = 0, av = 0;
    apply_func af;
    apply_loop al;
    Stencil* s;
    Rewriter* r = 0;
    int rewriteApplyLoop = 0;
    int verbose = 0;
    int arg = 1;

    while ((argc > arg) && (argv[arg][0] == '-')) {
        if (argv[arg][1] == 'v') verbose++;
        arg++;
    }
    if (argc > arg) { av   = atoi(argv[arg]); arg++; }
    if (argc > arg) { size = atoi(argv[arg]); arg++; }
    if (argc > arg) { iter = atoi(argv[arg]); arg++; }

    if (size == 0) size = 1000;
    if (iter == 0) iter = 100;
    if (av == 0) av = 1;

    al = applyLoop;
    if (av>20) {
        al = applyLoop1;
        av -= 20;
    }
    if (av>10) {
        rewriteApplyLoop = 1;
        av -= 10;
    }

    m1 = (TYPE*) malloc(sizeof(TYPE) * size * size);
    m2 = (TYPE*) malloc(sizeof(TYPE) * size * size);

    // init
    for(i=0;i<size*size;i++)
        m1[i] = 0;
    for(i=0;i<size;i++) {
        m1[i] = 1;                 // upper row
        m1[(size-1)*size + i] = 2; // lower row
        m1[i*size] = 3;            // left column
        m1[i*size + (size-1)] = 4; // right column
    }
    for(i=0;i<size*size;i++)
        m2[i] = m1[i];

    printf("Stencil code version: ");
    switch(av) {
    case 1:
    case 5:
        // generic version
        printf("generic");
        af = apply;
        s = &s5;
        break;
    case 2:
    case 6:
        // generic version grouped by factor
        printf("grouped generic");
        af = (apply_func) applyS;
        s = (Stencil*) &s5s;
        break;
    case 3:
    case 7:
        // manual version
        printf("manual");
        af = apply2;
        s = 0;
        break;
    case 4:
    case 8:
        // no computation, just return center value
        printf("(center)");
        af = apply3;
        s = 0;
        break;
    }



    if (rewriteApplyLoop) {
        printf(", rewriting with loops.\n");
        r = allocRewriter();
        if (verbose>1)
            setVerbosity(r, True, True, True);
        setFunc(r, (uint64_t) al);
        setRewriterStaticPar(r, 0); // size is constant
        setRewriterStaticPar(r, 3); // apply func is constant
        setRewriterStaticPar(r, 4); // stencil is constant
        setRewriterForceUnknown(r, 0); // do not unroll in applyLoop
        emulateAndCapture(r, size, m1, m2, af, s);
        al = (apply_loop) generatedCode(r);
    }
    else {
        printf(",%s rewriting.\n", (av<5) ? " no":"");
        if (av >= 5) {
            r = allocRewriter();
            if (verbose>1)
                setVerbosity(r, True, True, True);
            setFunc(r, (uint64_t) af);
            setRewriterStaticPar(r, 1); // size is constant
            setRewriterStaticPar(r, 2); // stencil is constant
            setRewriterReturnFP(r);
            emulateAndCapture(r, m1 + size + 1, size, s);
            af = (apply_func) generatedCode(r);
        }
    }

    if (r && (verbose>0)) {
        // use another rewriter to show generated code
        Rewriter* r2 = allocRewriter();
        printDecoded(r2, generatedCode(r), generatedCodeSize(r));
        freeRewriter(r2);
    }

    printf("Width %d, matrix size %d, %d iterations, apply V %d\n",
           size, (int)(size*size*sizeof(TYPE)), iter, av);
    iter = iter/2;

#if 1
    for(i=0;i<iter;i++) {
        al(size, m1, m2, af, s);
        al(size, m2, m1, af, s);
    }
#else
    // this version should allow vectorization
    for(i=0;i<iter;i++) {
        for(y=1;y<size-1;y++)
            for(x=1;x<size-1;x++)
                m1[x+y*size] = apply2(&(m2[x+y*size]), size, s);
        for(y=1;y<size-1;y++)
            for(x=1;x<size-1;x++)
                m2[x+y*size] = apply2(&(m1[x+y*size]), size, s);
    }
#endif

    diff = 0;
    for(y=1;y<size-1;y++) {
        for(x=1;x<size-1;x++) {
            i = m2[x+y*size] - af(&(m1[x+y*size]), size, s);
            diff += (i>0) ? i : -i;
        }
    }
#ifdef USEINT
    printf("Residuum after %d iterations: %d\n", 2*iter, diff);
#else
    printf("Residuum after %d iterations: %f\n", 2*iter, diff);
#endif

    free(m1);
    free(m2);
    if (r) freeRewriter(r);
}
