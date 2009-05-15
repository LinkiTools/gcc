/* { dg-do compile } */
/* { dg-options "-O -fno-tree-sra -fdump-tree-fre-details" } */

/* Should be optimized, propagating &a into (*p)[i].  */

struct Foo
{
  void *data;
  double size;
};
void foo(double (*q)[4])
{
  struct Foo tmp1;
  double a[4];
  int i;
  tmp1.data = &a;
  tmp1.size = 4;
  for (i=0; i<4; ++i)
    {
      double (*p)[4] = tmp1.data;
      (*p)[i] = (*q)[i];
      /* We want a PHI for the VOP for accessing tmp1.data, so place
 	 this store to tmp1 here.  */
      tmp1.size -= 1.0;
    }
}

/* { dg-final { scan-tree-dump "Inserted .* &a" "fre" } } */
/* { dg-final { scan-tree-dump "Replaced tmp1.data" "fre" } } */
/* { dg-final { cleanup-tree-dump "fre" } } */
