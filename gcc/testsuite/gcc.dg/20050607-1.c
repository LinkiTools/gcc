/* APPLE LOCAL file mainline */
/* { dg-do compile } */
/* APPLE LOCAL testsuite nested functions */
/* { dg-options "-Wpadded -Wno-nested-funcs" }
/* The struct internally constructed for the nested function should
   not result in a warning from -Wpadded. */
extern int baz(int (*) (int));
int foo(void)
{
  int k = 3;
  int bar(int x) {
    return x + k;
  }
  return baz(bar);
}
