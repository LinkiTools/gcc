#include <stdarg.h>

#ifdef DBG
#include <stdio.h>
#define DEBUG_FPUTS(x) fputs (x, stdout)
#define DEBUG_DOT putc ('.', stdout)
#define DEBUG_NL putc ('\n', stdout)
#else
#define DEBUG_FPUTS(x)
#define DEBUG_DOT
#define DEBUG_NL
#endif

/* Turn off checking for variable arguments with -DSKIPVA.  */
#ifdef SKIPVA
const int test_va = 0;
#else
const int test_va = 1;
#endif

#include "fp-struct-defs.h"
#include "fp-struct-init.h"
#include "fp-struct-test-by-value-y.h"

DEFS(f,float)
INITS(f, float)

TEST(Sf1, float)
TEST(Sf2, float)
TEST(Sf3, float)
TEST(Sf4, float)
TEST(Sf5, float)
TEST(Sf6, float)
TEST(Sf7, float)
TEST(Sf8, float)
TEST(Sf9, float)
TEST(Sf10, float)
TEST(Sf11, float)
TEST(Sf12, float)
TEST(Sf13, float)
TEST(Sf14, float)
TEST(Sf15, float)
TEST(Sf16, float)
