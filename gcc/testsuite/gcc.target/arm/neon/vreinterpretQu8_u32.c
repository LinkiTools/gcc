/* Test the `vreinterpretQu8_u32' ARM Neon intrinsic.  */
/* This file was autogenerated by neon-testgen.  */

/* { dg-do assemble } */
/* { dg-require-effective-target arm_neon_ok } */
/* { dg-options "-save-temps -O0" } */
/* { dg-add-options arm_neon } */

#include "arm_neon.h"

void test_vreinterpretQu8_u32 (void)
{
  uint8x16_t out_uint8x16_t;
  uint32x4_t arg0_uint32x4_t;

  out_uint8x16_t = vreinterpretq_u8_u32 (arg0_uint32x4_t);
}

