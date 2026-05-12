/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Negative case for commit 5: when two mmaccs have different
   geometries (different SEW yielding different lambdas), the lattice
   correctly says DEFINED(N1) -> DEFINED(N2) is NOT a hit on N2; the
   second auto-vsetvl_ime must emit.

   At VLEN=512:
     i32m1 mmacc: lambda = sqrt(512/(32*1)) = 4   (log2 = 2)
     i8m1  mmacc: lambda = sqrt(512/(8*1))  = 8   (log2 = 3)  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

void f (vint32m1_t *acc1, vint8m1_t *acc2,
	vint32m1_t a, vint32m1_t b,
	vint8m1_t c, vint8m1_t d, size_t vl)
{
  *acc1 = __riscv_vmmacc_vv_i32m1 (*acc1, a, b, vl);
  *acc2 = __riscv_vmmacc_vv_i8m1  (*acc2, c, d, vl);
}

/* Two distinct vsetvl_imes, one per mmacc geometry.  */
/* { dg-final { scan-assembler-times {vsetvl\t} 2 } } */
/* { dg-final { scan-assembler-times {vmmacc\.vv} 2 } } */
