#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "toplev.h"
#include "real.h"
#include "tm_p.h"
#include "dfp.h"

/* The order of the following headers is important for making sure 
   decNumber structure is large enough to hold decimal128 digits. */

#include "decimal128.h"
#include "decimal64.h"
#include "decimal32.h"
#include "decNumber.h"
#include "decDPD.h"

/* Useful for munging between formats. */
static decNumber dn;
static decNumber dn2;
static decNumber dn3;
static decContext set;

/* Create decimal encoded r from string. */
static void
decimal_from_string (REAL_VALUE_TYPE *r, const char *s)
{
  memset (r, 0, sizeof(r));

  decContextDefault(&set, DEC_INIT_DECIMAL128);
  set.traps=0;
  decNumberFromString(&dn, (char *)s, &set);

  /* Really, it would be more efficient to store directly in decNumber
    format, but that is impractical from data structure size. 
    Encoding as a decimal128 is much more compact. */
  decimal128FromNumber((decimal128 *)r->sig, &dn, &set);

  if (decNumberIsNegative(&dn))
    r->sign = 1;
  r->cl = rvc_normal;
  if (decNumberIsZero(&dn))
    r->cl = rvc_zero;
  if (decNumberIsNaN(&dn))
    r->cl = rvc_nan;
  if (decNumberIsInfinite(&dn))
    r->cl = rvc_inf;
  r->decimal = 1;
}

/* Wrapper to handle decimal strings too.  
   FIXME:  Should be able to accomplish this with 
   fmt->real_from_string(). */
void
decimal_real_from_string (REAL_VALUE_TYPE *r, const char *s, 
			  enum machine_mode mode)
{
  if (mode != SDmode && mode != DDmode && mode != TDmode) {
    real_from_string (r, s);
  } else
    decimal_from_string (r, s);
  r->decimal = 1;
}

void 
encode_decimal32 (const struct real_format *fmt ATTRIBUTE_UNUSED,
		  long *buf, const REAL_VALUE_TYPE *r)
{
  decimal32 d32;

  decContextDefault(&set, DEC_INIT_DECIMAL128);
  set.traps=0;

  decimal128ToNumber((decimal128 *)r->sig, &dn);

  /* Still in intermediate representation, so sign
     is seperate from encoding. */
  /* FIXME: This is WRONG place.  Should come through
     REAL_NEGATE path. */
  if (r->sign != decNumberIsNegative(&dn))
    decNumberNegate(&dn);

  decimal32FromNumber(&d32, &dn, &set);

  buf[0] = *(long *)d32.bytes;
}

void decode_decimal32 (const struct real_format *fmt ATTRIBUTE_UNUSED,
		       REAL_VALUE_TYPE *r ATTRIBUTE_UNUSED,
		       const long *buf ATTRIBUTE_UNUSED)
{  
  /* FIXME: Do something. */
}

static char string[256];

void 
encode_decimal64 (const struct real_format *fmt ATTRIBUTE_UNUSED,
		  long *buf, const REAL_VALUE_TYPE *r)
{
  decimal64 d64;

  decContextDefault(&set, DEC_INIT_DECIMAL128);
  set.traps=0;
  decimal128ToNumber((decimal128 *)r->sig, &dn);

  /* Still in intermediate representation, so sign
     is seperate from encoding. */
  /* FIXME: This is WRONG place.  Should come through
     REAL_NEGATE path. */
  if (r->sign != decNumberIsNegative(&dn))
    decNumberNegate(&dn);

  decimal64FromNumber(&d64, &dn, &set);

  if (FLOAT_WORDS_BIG_ENDIAN)
    {
      buf[0] = *(long *)&d64.bytes[0];
      buf[1] = *(long *)&d64.bytes[4];
    }
  else
    {
      buf[1] = *(long *)&d64.bytes[0];
      buf[0] = *(long *)&d64.bytes[4];
    }
}

void 
decode_decimal64 (const struct real_format *fmt ATTRIBUTE_UNUSED,
		  REAL_VALUE_TYPE *r ATTRIBUTE_UNUSED,
		  const long *buf ATTRIBUTE_UNUSED)
{
  /* FIXME: Do something. */
}

void 
encode_decimal128 (const struct real_format *fmt ATTRIBUTE_UNUSED,
		   long *buf, const REAL_VALUE_TYPE *r)
{
  decimal128 *d128;
  /* Already in decimal128 form. */
  d128 = (decimal128 *)r->sig;

  /* Still in intermediate representation, so sign
     is seperate from encoding. */

  /* FIXME: This is WRONG place.  Should come through
     REAL_NEGATE path. */
  if (r->sign != decimal128Sign(d128)) 
    d128->bytes[0] ^= 1 << 7;  /* Flip high bit */

  if (FLOAT_WORDS_BIG_ENDIAN)
    {
      buf[0] = *(long *)&d128->bytes[0];
      buf[1] = *(long *)&d128->bytes[4];
      buf[2] = *(long *)&d128->bytes[8];
      buf[3] = *(long *)&d128->bytes[12];
    }
  else
    {
      buf[0] = *(long *)&d128->bytes[12];
      buf[1] = *(long *)&d128->bytes[8];
      buf[2] = *(long *)&d128->bytes[4];
      buf[3] = *(long *)&d128->bytes[0];
    }
}

void 
decode_decimal128 (const struct real_format *fmt ATTRIBUTE_UNUSED,
		   REAL_VALUE_TYPE *r ATTRIBUTE_UNUSED,
		   const long *buf ATTRIBUTE_UNUSED)
{
  /* FIXME: Do something. */
}

/* Helper function to convert from a binary real internal representation. */
static void
decimal_from_binary (REAL_VALUE_TYPE *to, const REAL_VALUE_TYPE *from)
{
  decimal128 *d128;
  d128 = (decimal128 *)to->sig;

  /* We convert to string then convert to decNumber then to decimal128. */
  real_to_decimal (string, from, sizeof(string), 0, 1);

  decContextDefault(&set, DEC_INIT_DECIMAL128);
  set.traps=0;

  decNumberFromString(&dn, string, &set);
  decimal128FromNumber(d128, &dn, &set);
  if (from->sign != decimal128Sign(d128)) 
      d128->bytes[0] ^= 1 << 7;  /* Flip high bit */

  to->decimal = 1;
}

/* Helper function to real.c::do_compare to handle decimal internal 
   represenation including when one of the operands is still in the 
   binary internal representation. */
int
decimal_do_compare (const REAL_VALUE_TYPE *a, const REAL_VALUE_TYPE *b,
		    int nan_result)
{
  REAL_VALUE_TYPE a1;
  REAL_VALUE_TYPE b1;

  /* If either op is not a decimal, create a temporary decimal versions. */
  if (!a->decimal) 
    {      
      decimal_from_binary (&a1, a);
      a = &a1;
    }
  if (!b->decimal) 
    {
      decimal_from_binary (&b1, b);
      b = &b1;
    }
    
  /* Convert into decNumber form for comparison operation. */
  decContextDefault(&set, DEC_INIT_DECIMAL128);
  set.traps=0;  
  decimal128ToNumber((decimal128 *)a->sig, &dn);
  decimal128ToNumber((decimal128 *)b->sig, &dn2);

  /* FIXME: The internal representation stores the real sign.
     Remove this once real_arithmetic is fixed up for negate operation. */
  if (a->sign != decNumberIsNegative(&dn))
    decNumberNegate(&dn);
  if (b->sign != decNumberIsNegative(&dn2))
    decNumberNegate(&dn2);

  /* Finally, do the comparison. */
  decNumberCompare (&dn3, &dn, &dn2, &set);

  /* Return the comparison result. */
  if (decNumberIsNaN(&dn))
    return nan_result;
  else if (decNumberIsZero (&dn3))
    return 0;
  else if (decNumberIsNegative(&dn3))
    return -1;
  else 
    return 1;
}
