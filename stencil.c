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

#ifdef USEINT
#define COEFF1 (-4)
#define COEFF2 (1)

StencilPoint s5[] = {{0,0,-4},
		     {-1,0,1}, {1,0,1}, {0,-1,1}, {0,1,1},
		     {0,0,0}};
#else
#define COEFF1 (-1.0)
#define COEFF2 (.25)

StencilPoint s5[] = {{0,0,-1.0},
                     {-1,0,.25}, {1,0,.25}, {0,-1,.25}, {0,1,.25},
                     {0,0,0}};
#endif


typedef TYPE (*apply_func)(TYPE*, int, StencilPoint*);

TYPE apply(TYPE *m, int xsize, StencilPoint* s)
{
    TYPE f, res;

    res = 0;
    while(1) {
	f = s->factor;
	if (f == 0) break;
	res += f * m[s->xdiff + s->ydiff * xsize];
	s++;
    }
    return res;
}

TYPE apply2(TYPE *m, int xsize, StencilPoint* s)
{
    return COEFF1 * m[0] + COEFF2 * (m[-1] + m[1] + m[-xsize] + m[xsize]);
}

int main(int argc, char* argv[])
{
    int i, x, y;
    TYPE *m1, *m2, diff;
    int size = 0, iter = 0, av = 0;
    apply_func af;

    if (argc>1) av = atoi(argv[1]);
    if (argc>2) size = atoi(argv[2]);
    if (argc>3) iter = atoi(argv[3]);

    if (size == 0) size = 1000;
    if (iter == 0) iter = 100;
    if (av == 0) av = 1;


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

    af = (av == 1) ? apply : apply2;
    if (av == 3) {
        Rewriter* r = allocRewriter();
        setFunc(r, (uint64_t) apply);
        setVerbosity(r, True, True, True);
        //setCaptureConfig(r, 2);
        setRewriteConfig2(r, 1,2);
        rewrite(r, m1 + size + 1, size, s5);
        af = (apply_func) generatedCode(r);

        {
            // use another rewriter to show generated code
            Rewriter* r2 = allocRewriter();
            setFunc(r2, generatedCode(r));
            decodeBB(r2, generatedCode(r));
            printCode(r2);
        }
    }

    printf("Width %d, matrix size %d, %d iterations, apply V %d\n",
           size, (int)(size*size*sizeof(TYPE)), iter, av);
    iter = iter/2;

    for(i=0;i<iter;i++) {
	for(y=1;y<size-1;y++)
	    for(x=1;x<size-1;x++)
		m2[x+y*size] = af(&(m1[x+y*size]), size, s5);
	for(y=1;y<size-1;y++)
	    for(x=1;x<size-1;x++)
		m1[x+y*size] = af(&(m2[x+y*size]), size, s5);
    }

    diff = 0;
    for(y=1;y<size-1;y++) {
	for(x=1;x<size-1;x++) {
	    i = m2[x+y*size] - af(&(m1[x+y*size]), size, s5);
	    diff += (i>0) ? i : -i;
	}
    }
#ifdef USEINT
    printf("Residuum after %d iterations: %d\n", 2*iter, diff);
#else
    printf("Residuum after %d iterations: %f\n", 2*iter, diff);
#endif
}
