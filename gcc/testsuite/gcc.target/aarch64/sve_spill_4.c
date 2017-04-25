/* { dg-do compile } */
/* { dg-options "-O2 -ftree-vectorize -march=armv8-a+sve" } */

#include <stdint.h>

void consumer (void *);

#define TEST_LOOP(TYPE, VAL)			\
  void						\
  multi_loop_##TYPE (TYPE *x)			\
  {						\
    for (int i = 0; i < 100; ++i)		\
      x[i] += VAL;				\
    consumer (x);				\
    for (int i = 0; i < 100; ++i)		\
      x[i] += VAL;				\
    consumer (x);				\
    for (int i = 0; i < 100; ++i)		\
      x[i] += VAL;				\
    consumer (x);				\
  }

TEST_LOOP (uint16_t, 0x1234);
TEST_LOOP (uint32_t, 0x12345);
TEST_LOOP (uint64_t, 0x123456);

/* { dg-final { scan-assembler-times {\tptrue\tp[0-9]+\.h,} 3 } } */
/* { dg-final { scan-assembler-times {\tptrue\tp[0-9]+\.s,} 3 } } */
/* { dg-final { scan-assembler-times {\tptrue\tp[0-9]+\.d,} 3 } } */
/* { dg-final { scan-assembler-times {\tld1rh\tz[0-9]+\.h,} 3 } } */
/* { dg-final { scan-assembler-times {\tld1rw\tz[0-9]+\.s,} 3 } } */
/* { dg-final { scan-assembler-times {\tld1rd\tz[0-9]+\.d,} 3 } } */
/* { dg-final { scan-assembler-not {\tldr\tz[0-9]} } } */
/* { dg-final { scan-assembler-not {\tstr\tz[0-9]} } } */
/* { dg-final { scan-assembler-not {\tldr\tp[0-9]} } } */
/* { dg-final { scan-assembler-not {\tstr\tp[0-9]} } } */
