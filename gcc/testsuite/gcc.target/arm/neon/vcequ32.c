/* Test the `vcequ32' ARM Neon intrinsic.  */
/* This file was autogenerated by neon-testgen.  */

/* { dg-do assemble } */
/* { dg-require-effective-target arm_neon_ok } */
/* { dg-options "-save-temps -O0 -mfpu=neon -mfloat-abi=softfp" } */

#include "arm_neon.h"

void test_vcequ32 (void)
{
  uint32x2_t out_uint32x2_t;
  uint32x2_t arg0_uint32x2_t;
  uint32x2_t arg1_uint32x2_t;

  out_uint32x2_t = vceq_u32 (arg0_uint32x2_t, arg1_uint32x2_t);
}

/* { dg-final { scan-assembler "vceq\.i32\[ 	\]+\[dD\]\[0-9\]+, \[dD\]\[0-9\]+, \[dD\]\[0-9\]+!?\(\[ 	\]+@\[a-zA-Z0-9 \]+\)?\n" } } */
/* { dg-final { cleanup-saved-temps } } */
