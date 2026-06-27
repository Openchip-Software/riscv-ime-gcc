/* { dg-do run { target { riscv_v } } } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Execution test for the IME pair / unpair primitives.

   Verify the value-level round trip:

       lo, hi              # two m8 halves
        |    |
        +-vpair-+          # compose into an m16 accumulator
            |
        +-vunpair-+        # decompose back
        |         |
        r_lo == lo  AND  r_hi == hi

   The pair/unpair primitives are no-ops or single `vmv8r.v' moves at
   the asm level; the test exercises that the lo/hi halves end up at
   the right placement after the splitter's (regno, regno+8)
   decomposition.  In particular, the splitter must handle the case
   where RA places lo and hi such that a true swap would be needed
   (loregno == dregno + 8 AND hiregno == dregno), which used to
   `gcc_assert' out.  */

#pragma riscv intrinsic "vector"
typedef __SIZE_TYPE__ size_t;

int
main (void)
{
  /* VLEN=512, SEW=32 → 16 elements per LMUL=1; m8 = 128 elements.  */
  enum { N = 128 };
  int lo_buf[N], hi_buf[N], r_lo_buf[N], r_hi_buf[N];

  for (int i = 0; i < N; i++)
    {
      lo_buf[i] = 0x10000 + i;
      hi_buf[i] = 0x20000 + i;
    }

  size_t vl = N;
  vint32m8_t lo = __riscv_vle32_v_i32m8 (lo_buf, vl);
  vint32m8_t hi = __riscv_vle32_v_i32m8 (hi_buf, vl);

  vint32m16_t acc = __riscv_ime_vpair_i32m16 (lo, hi);

  vint32m8_t r_lo = __riscv_ime_vunpairlo_i32m16 (acc);
  vint32m8_t r_hi = __riscv_ime_vunpairhi_i32m16 (acc);

  __riscv_vse32_v_i32m8 (r_lo_buf, r_lo, vl);
  __riscv_vse32_v_i32m8 (r_hi_buf, r_hi, vl);

  for (int i = 0; i < N; i++)
    if (r_lo_buf[i] != lo_buf[i] || r_hi_buf[i] != hi_buf[i])
      __builtin_abort ();

  return 0;
}
