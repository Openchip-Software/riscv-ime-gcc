/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm -mabi=lp64d -O2" } */

/* Without Zbb the expander cannot encode a runtime lambda in two
   instructions; we sorry() rather than synthesise software ctz on a
   hot path.  Callers without Zbb should dispatch through a constant
   switch.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

size_t f (size_t v)
{
  return __riscv_vsetlambda (v); /* { dg-message "sorry, unimplemented: '__riscv_vsetlambda' with a non-constant argument requires the 'Zbb' extension" } */
}
