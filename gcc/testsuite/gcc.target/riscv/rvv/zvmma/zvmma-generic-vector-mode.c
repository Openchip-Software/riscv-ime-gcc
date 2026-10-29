/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvvfp32mm -mabi=lp64d -O2" } */

/* With only part of IME enabled, the disabled m16 modes must not be
   chosen for a one-element generic vector (from autovec/pr111486.c):
   it keeps its fixed-length vector mode.  */

typedef char __attribute__((__vector_size__ (1))) V;

V
foo (V v, long x)
{
  x &= v[0];
  return v + (char) x;
}

/* { dg-final { scan-assembler {\mvadd\.vx\M} } } */
