#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
struct base {
  int basic;
}; 

struct derived { 
  struct base common;
  char extra;
};

struct base b;
struct base *bp;

bp = (struct base *)&b;

bp->basic = 10;
((struct derived *)bp)->extra = 'x';
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*.main. b.*" } */
/* { dg-do run { xfail *-*-* } } */
