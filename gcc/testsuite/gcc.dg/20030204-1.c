/* PR optimization/8555 */
/* { dg-do compile } */
/* { dg-options "-O -ffast-math -funroll-loops" } */
/* { dg-options "-march=pentium3 -O -ffast-math -funroll-loops" { target i?86-*-* } } */
/* { dg-forbid-option "-m64" {target i?86-*-* } } */

float foo (float *a, int i)
{
  int j;
  float x = a[j = i - 1], y;

  for (j = i; --j >= 0; )
    if ((y = a[j]) > x)
      x = y;

  return x;
}
