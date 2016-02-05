/* { dg-do compile { target { ! ia32 } } } */
/* { dg-options "-O2 -mno-mpx -mno-sse -mno-mmx -mno-80387 -mno-cld -mred-zone" } */

void
__attribute__((interrupt))
fn (void *frame)
{
  /* No need to adjust stack if less than 128 bytes are used on stack
     with a 128-byte red zone.  */
  long long int i0;
  long long int i1;
  long long int i2;
  long long int i3;
  long long int i4;
  long long int i5;
  long long int i6;
  long long int i7;
  long long int i8;
  long long int i9;
  long long int i10;
  long long int i11;
  long long int i12;
  long long int i13;
  asm ("# %0, %1, %2, %3, %4, %5, %6, %7"
       : "=m" (i0), "=m" (i1), "=m" (i2), "=m" (i3),
         "=m" (i4), "=m" (i5), "=m" (i6), "=m" (i7),
         "=m" (i8), "=m" (i9), "=m" (i10), "=m" (i11),
	 "=m" (i12), "=m" (i13));
}

/* { dg-final { scan-assembler-not "(sub|add)(l|q)\[\\t \]*\\$\[0-9\]*,\[\\t \]*%\[re\]?sp" } } */
/* { dg-final { scan-assembler-not "\tcld" } } */
