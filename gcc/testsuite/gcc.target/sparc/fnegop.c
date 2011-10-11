/* { dg-do compile { target { vis3 } } } */
/* { dg-options "-O2 -mcpu=niagara3 -mvis" } */

float test_fnadds(float x, float y)
{
  return -(x + y);
}

double test_fnaddd(double x, double y)
{
  return -(x + y);
}

float test_fnmuls(float x, float y)
{
  return -(x * y);
}

double test_fnmuld(double x, double y)
{
  return -(x * y);
}

double test_fnsmuld(float x, float y)
{
  return -((double)x * (double)y);
}

/* { dg-final { scan-assembler "fnadds\t%" } } */
/* { dg-final { scan-assembler "fnaddd\t%" } } */
/* { dg-final { scan-assembler "fnmuls\t%" } } */
/* { dg-final { scan-assembler "fnmuld\t%" } } */
/* { dg-final { scan-assembler "fnsmuld\t%" } } */
