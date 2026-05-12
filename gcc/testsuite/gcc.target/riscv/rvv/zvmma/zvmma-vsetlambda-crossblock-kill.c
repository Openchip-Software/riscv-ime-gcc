/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Negative cross-block test: when a function call sits between the
   user's `__riscv_vsetlambda' and the IME consumer, the lattice's
   transfer function kills lambda (calls are conservative may-DEFs).
   The auto-vsetvl_ime must therefore emit despite the static
   user-set value upstream.  */

typedef unsigned long size_t;
extern void external (void);
#pragma riscv intrinsic "vector"

vint32m1_t f (vint32m1_t acc, vint32m1_t a, vint32m1_t b, size_t vl)
{
  __riscv_vsetlambda (4);
  external ();                                    /* kills lambda */
  return __riscv_vmmacc_vv_i32m1 (acc, a, b, vl); /* needs auto-vsetvl */
}

/* Two vsetvls: user's pre-call install + auto-emitted IME vsetvl
   post-call.  */
/* { dg-final { scan-assembler-times {vsetvl\t} 2 } } */
