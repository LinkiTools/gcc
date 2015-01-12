#include <arm_neon.h>
#include "arm-neon-ref.h"
#include "compute-ref-data.h"

/* Expected results.  */
VECT_VAR_DECL(expected,hfloat,32,2) [] = { 0xc440ca3d, 0xc4408a3d };
VECT_VAR_DECL(expected,hfloat,32,4) [] = { 0xc48a9eb8, 0xc48a7eb8, 0xc48a5eb8, 0xc48a3eb8 };
VECT_VAR_DECL(expected,hfloat,64,2) [] = { 0xc08a06e1532b8520, 0xc089fee1532b8520 };

#define TEST_MSG "VFMA/VFMAQ"
void exec_vfms (void)
{
  /* Basic test: v4=vfms(v1,v2), then store the result.  */
#define TEST_VFMA(Q, T1, T2, W, N)					\
  VECT_VAR(vector_res, T1, W, N) =					\
    vfms##Q##_##T2##W(VECT_VAR(vector1, T1, W, N),			\
		      VECT_VAR(vector2, T1, W, N),			\
			  VECT_VAR(vector3, T1, W, N));			\
  vst1##Q##_##T2##W(VECT_VAR(result, T1, W, N), VECT_VAR(vector_res, T1, W, N))

#define CHECK_VFMA_RESULTS(test_name,comment)				\
  {									\
    CHECK_FP(test_name, float, 32, 2, PRIx32, expected, comment);	\
    CHECK_FP(test_name, float, 32, 4, PRIx32, expected, comment);	\
	CHECK_FP(test_name, float, 64, 2, PRIx64, expected, comment);	\
  }	

#define DECL_VABD_VAR(VAR)			\
  DECL_VARIABLE(VAR, float, 32, 2);		\
  DECL_VARIABLE(VAR, float, 32, 4);		\
  DECL_VARIABLE(VAR, float, 64, 2);		

  DECL_VABD_VAR(vector1);
  DECL_VABD_VAR(vector2);
  DECL_VABD_VAR(vector3);
  DECL_VABD_VAR(vector_res);

  clean_results ();

  /* Initialize input "vector1" from "buffer".  */
  VLOAD(vector1, buffer, , float, f, 32, 2);
  VLOAD(vector1, buffer, q, float, f, 32, 4);
  VLOAD(vector1, buffer, q, float, f, 64, 2);

  /* Choose init value arbitrarily.  */
  VDUP(vector2, , float, f, 32, 2, 9.3f);
  VDUP(vector2, q, float, f, 32, 4, 29.7f);
  VDUP(vector2, q, float, f, 64, 2, 15.8f);
  
  /* Choose init value arbitrarily.  */
  VDUP(vector3, , float, f, 32, 2, 81.2f);
  VDUP(vector3, q, float, f, 32, 4, 36.8f);
  VDUP(vector3, q, float, f, 64, 2, 51.7f);

  /* Execute the tests.  */
  TEST_VFMA(, float, f, 32, 2);
  TEST_VFMA(q, float, f, 32, 4);
  TEST_VFMA(q, float, f, 64, 2);

  CHECK_VFMA_RESULTS (TEST_MSG, "");
}

int main (void)
{
  exec_vfms ();
  return 0;
}
