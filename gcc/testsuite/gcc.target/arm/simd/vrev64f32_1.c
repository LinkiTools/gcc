/* Test the `vrev64f32' ARM Neon intrinsic.  */

/* { dg-options "-save-temps -fno-inline" } */
/* { dg-add-options arm_neon } */

#include "arm_neon.h"
#include "../../aarch64/simd/vrev64f32.x"

/* { dg-final { scan-assembler "vrev64\.32\[ \t\]+\[dD\]\[0-9\]+, \[dD\]\[0-9\]+!?\(\[ \t\]+@\[a-zA-Z0-9 \]+\)?\n" } } */
/* { dg-final { cleanup-saved-temps } } */
