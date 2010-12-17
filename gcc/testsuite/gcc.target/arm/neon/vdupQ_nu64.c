/* Test the `vdupQ_nu64' ARM Neon intrinsic.  */
/* This file was autogenerated by neon-testgen.  */

/* { dg-do assemble } */
/* { dg-require-effective-target arm_neon_ok } */
/* { dg-options "-save-temps -O0" } */
/* { dg-add-options arm_neon } */

#include "arm_neon.h"

void test_vdupQ_nu64 (void)
{
  uint64x2_t out_uint64x2_t;
  uint64_t arg0_uint64_t;

  out_uint64x2_t = vdupq_n_u64 (arg0_uint64_t);
}

/* { dg-final { cleanup-saved-temps } } */
