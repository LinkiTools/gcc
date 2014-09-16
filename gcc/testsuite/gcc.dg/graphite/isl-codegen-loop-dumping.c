/* { dg-options "-O2 -fdump-tree-graphite-all -fgraphite-identity -fgraphite-code-generator=isl" } */

int
main (int n, int *a)
{
  int i, j;

  for (i = 0; i < n - 1; i++)
    for (j = 0; j < n; j++)
      a[j] = i + n;

  return 0;
}

/* { dg-final { scan-tree-dump-times "ISL AST generated by ISL: \nfor \\(int c1 = 0; c1 < n - 1; c1 \\+= 1\\)\n  for \\(int c3 = 0; c3 < n; c3 \\+= 1\\)\n    S_4\\(c1, c3\\);" 1 "graphite"} } */
/* { dg-final { cleanup-tree-dump "graphite" } } */
