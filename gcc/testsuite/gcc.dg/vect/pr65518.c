#include "tree-vect.h"

#if VECTOR_BITS > 256
#define NINTS (VECTOR_BITS / 32)
#else
#define NINTS 8
#endif

#define N (NINTS * 2)
#define RESULT (NINTS * (NINTS - 1) / 2 * N + NINTS)

extern void abort (void);

typedef struct giga
{
  unsigned int g[N];
} giga;

unsigned long __attribute__((noinline,noclone))
addfst(giga const *gptr, int num)
{
  unsigned int retval = 0;
  int i;
  for (i = 0; i < num; i++)
    retval += gptr[i].g[0];
  return retval;
}

int main ()
{
  struct giga g[NINTS];
  unsigned int n = 1;
  int i, j;
  check_vect ();
  for (i = 0; i < NINTS; ++i)
    for (j = 0; j < N; ++j)
      {
	g[i].g[j] = n++;
	__asm__ volatile ("");
      }
  if (addfst (g, NINTS) != RESULT)
    abort ();
  return 0;
}

/* { dg-final { scan-tree-dump-times "vectorized 1 loops in function" 1 "vect" } } */
