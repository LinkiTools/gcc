/* { dg-do compile } */
/* { dg-options "-O3 -march=armv8-a+sve -msve-vector-bits=256" } */

#define N 32
#define MAX_START 8
#define COUNT 16

int x[MAX_START][N] __attribute__((aligned(32)));

void __attribute__((weak))
foo (unsigned int start)
{
  for (unsigned int i = start; i < start + COUNT; ++i)
    x[start][i] = i;
}

/* We should operate on aligned vectors.  */
/* { dg-final { scan-assembler {\tadrp\tx[0-9]+, x\n} } } */
/* { dg-final { scan-assembler {\tubfx\t} } } */
