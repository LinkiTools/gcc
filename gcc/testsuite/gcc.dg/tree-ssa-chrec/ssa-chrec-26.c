/* { dg-do compile } */ 
/* { dg-options "-O1 -fscalar-evolutions -fdump-tree-scev-stats" } */


int bar (void);

int foo ()
{
  int a = -100;
  int b = -10;
  
  /* This exercises a code with two loop nests.  */
  
  while (a)
    a++;
  
  while (b)
    b++;
}

/* a  ->  {-100, +, 1}_1
   b  ->  {-10, +, 1}_2
*/

/* FIXME. */
