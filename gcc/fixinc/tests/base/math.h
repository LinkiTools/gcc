/*  DO NOT EDIT THIS FILE.

    It has been auto-edited by fixincludes from:

	"fixinc/tests/inc/math.h"

    This had to be done to correct non-standard usages in the
    original, manufacturer supplied header file.  */

#ifndef FIXINC_WRAP_MATH_H_SUNOS_MATHERR_DECL
#define FIXINC_WRAP_MATH_H_SUNOS_MATHERR_DECL 1

struct exception;
#ifndef FIXINC_WRAP_MATH_H_MATH_EXCEPTION
#define FIXINC_WRAP_MATH_H_MATH_EXCEPTION 1

#ifdef __cplusplus
#define exception __math_exception
#endif


#if defined( BROKEN_CABS_CHECK )
#ifdef __STDC__

#else

#endif
 /* This is a comment
                         and it ends here. */
#endif  /* BROKEN_CABS_CHECK */


#if defined( HPUX11_CPP_POW_INLINE_CHECK )

#endif  /* HPUX11_CPP_POW_INLINE_CHECK */


#if defined( HPUX11_FABSF_CHECK )
#ifdef _PA_RISC
#ifndef __cplusplus
#  define fabsf(x) ((float)fabs((double)(float)(x)))
#endif
#endif
#endif  /* HPUX11_FABSF_CHECK */


#if defined( HPUX8_BOGUS_INLINES_CHECK )
extern "C" int abs(int);

#endif  /* HPUX8_BOGUS_INLINES_CHECK */


#if defined( ISC_FMOD_CHECK )
extern double	fmod(double, double);
#endif  /* ISC_FMOD_CHECK */


#if defined( MATH_EXCEPTION_CHECK )
typedef struct exception t_math_exception;
#endif  /* MATH_EXCEPTION_CHECK */


#if defined( MATH_HUGE_VAL_FROM_DBL_MAX_CHECK )

#ifndef HUGE_VAL
#define HUGE_VAL 3.1415e+9 /* really big */
#endif
#endif  /* MATH_HUGE_VAL_FROM_DBL_MAX_CHECK */


#if defined( MATH_HUGE_VAL_IFNDEF_CHECK )
#ifndef HUGE_VAL
# define	HUGE_VAL 3.4e+40
#endif
#endif  /* MATH_HUGE_VAL_IFNDEF_CHECK */


#if defined( RS6000_DOUBLE_CHECK )
#ifndef __cplusplus
extern int class();
#endif
#endif  /* RS6000_DOUBLE_CHECK */


#if defined( SCO_MATH_CHECK )
#define __fp_class(a) \
 __builtin_generic(a,"ld:__fplcassifyl;f:__fpclassifyf;:__fpclassify")

#endif  /* SCO_MATH_CHECK */


#if defined( STRICT_ANSI_NOT_CTD_CHECK )
#if 1 && \
&& defined(mumbling) |& ( !defined(__STRICT_ANSI__)) \
(  !defined(__STRICT_ANSI__) && !defined(_XOPEN_SOURCE) \
||  !defined(__STRICT_ANSI__) ) /* not std C */
int foo;
#endif
#endif  /* STRICT_ANSI_NOT_CTD_CHECK */


#if defined( SUNOS_MATHERR_DECL_CHECK )
extern int matherr();
#endif  /* SUNOS_MATHERR_DECL_CHECK */


#if defined( SVR4__P_CHECK )
#ifndef __P
#define __P(a) a
#endif
#endif  /* SVR4__P_CHECK */


#if defined( ULTRIX_ATOF_PARAM_CHECK )
extern double atof(const char *__nptr);

#endif  /* ULTRIX_ATOF_PARAM_CHECK */


#if defined( WINDISS_MATH1_CHECK )
#ifndef __GNUC__
#endif  /* WINDISS_MATH1_CHECK */


#if defined( WINDISS_MATH2_CHECK )
#endif /* __GNUC__ */
#endif  /* WINDISS_MATH2_CHECK */
#ifdef __cplusplus
#undef exception
#endif

#endif  /* FIXINC_WRAP_MATH_H_MATH_EXCEPTION */

#endif  /* FIXINC_WRAP_MATH_H_SUNOS_MATHERR_DECL */
