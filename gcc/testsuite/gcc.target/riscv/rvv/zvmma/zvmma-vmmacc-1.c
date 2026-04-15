/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* Basic W=1 vmmacc.vv: signed and unsigned flavours.  At VLEN=512
   with SEW=32 and MUL_C=1 the required lambda is 4 (supported), so
   the compiler must emit a register-form vsetvl with the full vtype
   value and the vmmacc.vv instruction -- without a runtime WARL
   probe since VLEN is known.  */

vint32m1_t
mmacc_s (vint32m1_t vd, vint32m1_t a, vint32m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m1 (vd, a, b, vl);
}

vuint32m1_t
mmacc_u (vuint32m1_t vd, vuint32m1_t a, vuint32m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_u32m1 (vd, a, b, vl);
}

/* { dg-final { scan-assembler-times {\tvmmacc\.vv\t} 2 } } */
/* { dg-final { scan-assembler-times {\tvsetvl\t} 2 } } */
/* { dg-final { scan-assembler-not {csrr\tt[0-9]*,vtype} } } */
