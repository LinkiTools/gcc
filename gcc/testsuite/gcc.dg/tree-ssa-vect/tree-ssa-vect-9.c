/* APPLE LOCAL file lno */
/* { dg-do run { target powerpc*-*-* } } */
/* { dg-do run { target i?86-*-* x86_64-*-* } } */
/* { dg-options "-O2 -ftree-vectorize -fdump-tree-vect-stats -maltivec" { target powerpc*-*-* } } */
/* { dg-options "-O2 -ftree-vectorize -fdump-tree-vect-stats -msse2" { target i?86-*-* x86_64-*-* } } */

#include <stdarg.h>
#include "tree-vect.h"

#define N 16

int main1 ()
{
  int i;
  short sb[N] = {0,3,6,9,12,15,18,21,24,27,30,33,36,39,42,45};
  int ia[N];

  /* Not vetorizable yet (type cast).  */
  for (i = 0; i < N; i++)
    {
      ia[i] = (int) sb[i];
    }

  /* check results:  */
  for (i = 0; i < N; i++)
    {
      if (ia[i] != (int) sb[i])
        abort ();
    }

  return 0;
}

int main (void)
{ 
  check_vect ();

  return main1 ();
}

/* { dg-final { scan-tree-dump-times "vectorized 1 loops" 1 "vect" { xfail *-*-* } } } */
