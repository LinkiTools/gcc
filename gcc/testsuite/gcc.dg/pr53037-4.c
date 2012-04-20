/* PR c/53037.  */
/* { dg-do compile } */
/* { dg-options "-O0" } */

int foo __attribute__((warn_if_not_aligned(8))); /* { dg-error "'warn_if_not_aligned' may not be specified for 'foo'" } */

__attribute__((warn_if_not_aligned(8)))
void
bar (void) /* { dg-error "'warn_if_not_aligned' may not be specified for 'bar'" } */
{
}
