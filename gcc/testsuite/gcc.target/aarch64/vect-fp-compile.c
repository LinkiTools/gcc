/* { dg-do compile } */
/* { dg-options "-O3 -march=armv8-a" } */

#include "vect-fp.x"

/* { dg-final { scan-assembler "fadd\\tv" } } */
/* { dg-final { scan-assembler "fsub\\tv" } } */
/* { dg-final { scan-assembler "fmul\\tv" } } */
/* { dg-final { scan-assembler "fdiv\\tv" } } */
/* { dg-final { scan-assembler "fneg\\tv" } } */
/* { dg-final { scan-assembler "fabs\\tv" } } */
/* { dg-final { scan-assembler "fabd\\tv" } } */
