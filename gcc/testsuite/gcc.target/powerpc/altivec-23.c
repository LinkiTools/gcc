/* Verify that it is possible to define variables of composite types
   containing vector types.  We used to crash handling the
   initializer of automatic ones.  */

/* { dg-do compile } */
/* { dg-require-effective-target powerpc_altivec_ok } */
/* { dg-options "-maltivec -mabi=altivec" } */

#include <altivec.h>

typedef int bt;
typedef vector bt vt;
typedef struct { vt x; bt y[sizeof(vt) / sizeof (bt)]; } st;
#define INIT { 1, 2, 3, 4 }

void f ()
{
  vt x = INIT;
  vt y[1] = { INIT };
  st s = { INIT, INIT };
}

vt x = INIT;
vt y[1] = { INIT };
st s = { INIT, INIT };
