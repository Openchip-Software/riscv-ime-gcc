/* { dg-do compile } */
/* { dg-options "-O3 -march=rv64gcv_zvfh_zvbb_zvmma -mabi=lp64d" } */

/* Plain RVV code (from autovec/reduc/pr123501.c) gets the same vsetvl
   placement with IME enabled as without it.  */

long long b;
_Bool a = 1;
char e[13];

int
main ()
{
  for (long h = 0; h < 13; ++h)
    e[h] = 110;
  for (int i = 5; i < 9; i++)
    for (int k = 0; k < 1031; k++)
      {
	int l = e[0] ? e[i] : 0;
	a = l ? a : l;
      }
  b = (int) a;
  if (b != 1)
    __builtin_abort ();
}

/* { dg-final { scan-assembler-times {\mvseti?vli\M} 6 } } */
