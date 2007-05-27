/* { dg-require-effective-target vect_int } */

#include <stdarg.h>
#include <signal.h>
#include "tree-vect.h"

#define N 64
#define MAX 42

extern void abort(void); 

int main1 ()
{  
  float A[N] __attribute__ ((__aligned__(16)));
  float B[N] __attribute__ ((__aligned__(16)));
  float C[N] __attribute__ ((__aligned__(16)));
  float D[N] __attribute__ ((__aligned__(16)));
  float s;

  int i, j;

  for (i = 0; i < N; i++)
    {
      A[i] = i;
      B[i] = i;
      C[i] = i;
      D[i] = i;
    }

  /* Outer-loop 1: Vectorizable with respect to dependence distance. */
  for (i = 0; i < N-20; i++)
    {
      s = 0;
      for (j=0; j<N; j+=4)
        s += C[j];
      A[i] = A[i+20] + s;
    }

  /* check results:  */
  for (i = 0; i < N-20; i++)
    {
      s = 0;
      for (j=0; j<N; j+=4)
        s += C[j];
      if (A[i] != D[i+20] + s)
        abort ();
    }

  /* Outer-loop 2: Not vectorizable because of dependence distance. */
  for (i = 0; i < 4; i++)
    {
      s = 0;
      for (j=0; j<N; j+=4)
	s += C[j];
      B[i] = B[i+3] + s;
    }

  /* check results:  */
  for (i = 0; i < 4; i++)
    {
      s = 0;
      for (j=0; j<N; j+=4)
	s += C[j];
      if (B[i] != D[i+3] + s)
	abort ();
    }

  return 0;
}

int main ()
{
  check_vect ();
  return main1();
}

/* { dg-final { scan-tree-dump-times "not vectorized: possible dependence between data-refs" 1 "vect" } } */
/* { dg-final { scan-tree-dump-times "OUTER LOOP VECTORIZED" 1 "vect" } } */
/* { dg-final { scan-tree-dump-times "zero step in outer loop." 1 "vect" } } */
/* { dg-final { cleanup-tree-dump "vect" } } */
