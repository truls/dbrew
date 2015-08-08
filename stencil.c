#include <stdio.h>
#include <stdlib.h>
#include "spec.h"

typedef struct {
    int xdiff, ydiff, factor;
} StencilPoint;

StencilPoint s5[] = {{0,0,4},
		     {-1,0,-1}, {1,0,-1}, {0,-1,-1}, {0,1,-1},
		     {0,0,0}};

typedef int (*apply_func)(int*, int, StencilPoint*);

int apply(int *m, int xsize, StencilPoint* s)
{
    int f, res;

    res = 0;
    while(1) {
	f = s->factor;
	if (f == 0) break;
	res += f * m[s->xdiff + s->ydiff * xsize];
	s++;
    }
    return res;
}

int apply2(int *m, int xsize, StencilPoint* s)
{
    return 4 * m[0] - m[-1] - m[1] - m[-xsize] - m[xsize];
}

int main(int argc, char* argv[])
{
    int i, x, y, diff;
    int *m1, *m2;
    int size = 0, iter = 0, av = 0;
    apply_func af;

    if (argc>1) av = atoi(argv[1]);
    if (argc>2) size = atoi(argv[2]);
    if (argc>3) iter = atoi(argv[3]);

    if (size == 0) size = 1000;
    if (iter == 0) iter = 100;
    if (av == 0) av = 1;


    m1 = (int*) malloc(sizeof(int) * size * size);
    m2 = (int*) malloc(sizeof(int) * size * size);

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
	Code* c = allocCode(200, 20, 1000);
	Code* c2 = allocCode(200, 20, 0);

	configEmuState(c, 1000);
	setFunc(c, (uint64_t) apply);
	setCaptureConfig(c, 2);
	emulate(c, m1 + size + 1, size, s5);
	af = (apply_func) capturedCode(c);

	setFunc(c2, capturedCode(c));
	setCodeVerbosity(c2, False, False, False);
	decodeBB(c2, capturedCode(c));
	printCode(c2);
    }

    printf("Width %d, matrix size %d, %d iterations, apply V %d\n",
	   size, (int)(size*size*sizeof(int)), iter, av);
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
    printf("Residuum after %d iterations: %d\n", 2*iter, diff);
}
