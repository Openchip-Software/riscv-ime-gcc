/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvmma -mabi=lp64d -O2" } */

/* Two multiply-accumulates with different geometries at the same SEW: an
   m4 accumulator in a loop and an m1 accumulator after it.  Both runtime
   probes run at function entry and the second leaves its lambda in vtype,
   so the loop needs an IME vsetvl in its preheader: a plain vsetvli there
   would keep the lambda of the m1 probe.  */

#pragma riscv intrinsic "vector"
typedef __SIZE_TYPE__ size_t;

void
f (const int *A, int *C, int *D, int n)
{
  size_t vl = __riscv_vsetvlmax_e32m1 ();

  vint32m4_t c = __riscv_vmv_v_x_i32m4 (0, __riscv_vsetvlmax_e32m4 ());
  for (int k = 0; k < n; k++)
    {
      vint32m1_t a = __riscv_vle32_v_i32m1 (A + k, vl);
      c = __riscv_vmmacc_vv_i32m4 (c, a, a, vl);
    }
  __riscv_vse32_v_i32m4 (C, c, __riscv_vsetvlmax_e32m4 ());

  vint32m1_t a = __riscv_vle32_v_i32m1 (A, vl);
  vint32m1_t d = __riscv_vmv_v_x_i32m1 (0, vl);
  d = __riscv_vmmacc_vv_i32m1 (d, a, a, vl);
  __riscv_vse32_v_i32m1 (D, d, vl);
}

/* Two probes, each with its own IME vsetvl, an IME vsetvl in the loop
   preheader and one for the m1 multiply-accumulate.  */
/* { dg-final { scan-assembler-times {csrr\t[a-z0-9]+,vtype} 2 } } */
/* { dg-final { scan-assembler-times {\tvsetvl\t} 4 } } */
/* { dg-final { scan-assembler {\tble\t[^\n]*\n(?:\t[^\n]*\n)*?\tvsetvl\t[a-z0-9]+,[a-z0-9]+,[a-z0-9]+\n(?:\t[^\n]*\n)*?\.L[0-9]+:\n\tvle32\.v} } } */
