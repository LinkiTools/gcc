/* APPLE LOCAL begin mainline UCNs 2005-04-17 3892809 */
/* { dg-do run } */
/* { dg-options "-std=c99" } */
#include <stdlib.h>
#include <string.h>

#define str(t) #t

int main (void)
{
  const char s[] = str (\u30b2);

  if (strcmp (s, "\u30b2") != 0)
    abort ();
  
  return 0;
}
/* APPLE LOCAL end mainline UCNs 2005-04-17 3892809 */
