#include <stdio.h>
#include <stdlib.h>

extern int h (int i, int j);

int main ()
{
  int z = h (4, 10);
  return 0;
}
int *p;
__attribute__((noinline))
int h (int i, int j)
{
  int k[i];
  k[j] = i;
  p = k;
  return j;
}

/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.*\(h\).*k" } */
/* { dg-do run { xfail *-*-* } } */
