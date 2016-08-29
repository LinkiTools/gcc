/* Test the `vreinterpretp64_p8' ARM Neon intrinsic.  */
/* This file was autogenerated by neon-testgen.  */

/* { dg-do assemble } */
/* { dg-require-effective-target arm_crypto_ok } */
/* { dg-options "-save-temps -O0" } */
/* { dg-add-options arm_crypto } */

#include "arm_neon.h"

void test_vreinterpretp64_p8 (void)
{
  poly64x1_t out_poly64x1_t;
  poly8x8_t arg0_poly8x8_t;

  out_poly64x1_t = vreinterpret_p64_p8 (arg0_poly8x8_t);
}

