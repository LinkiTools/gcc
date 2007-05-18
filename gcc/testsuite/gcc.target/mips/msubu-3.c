/* { dg-do compile } */
/* { dg-mips-options "-O2 -mips32 -mgp32" } */
/* { dg-final { scan-assembler-times "\tmsubu\t" 2 } } */

typedef unsigned int ui;
typedef unsigned long long ull;

ull
f1 (ui x, ui y, ull z)
{
  return z - (ull) y * x;
}

ull
f2 (ui x, ui y, ull z)
{
  ull t = (ull) x * y;
  int temp = 5;
  if (temp == 5)
    z -= t;
  return z;
}
