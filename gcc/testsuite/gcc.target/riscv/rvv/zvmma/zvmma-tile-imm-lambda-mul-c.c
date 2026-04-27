/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Verify that immediate-lambda tile-load/store variants (_L{N}) use the
   operation's data LMUL as MUL_C when computing vtype.lambda.  At
   VLEN=256, SEW=8 the m1 divisor (32) is not a perfect-square power of
   2, so a tile op with MUL_C=1 would fail the parity check.  With the
   data LMUL inherited as MUL_C, _i8m2_L{K} uses divisor 16 (lambda=4)
   and _i8m8_L{K} uses divisor 4 (lambda=2) -- both valid.  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#pragma riscv intrinsic "vector"

vint8m2_t load_L2_m2 (const signed char *p, ptrdiff_t s, size_t vl)
{ return __riscv_vmtl_v_i8m2_L2 (p, s, vl); }

void store_L2_m2 (signed char *p, ptrdiff_t s, vint8m2_t v, size_t vl)
{ __riscv_vmts_v_i8m2_L2 (p, s, v, vl); }

vint8m8_t load_L2_m8 (const signed char *p, ptrdiff_t s, size_t vl)
{ return __riscv_vmtl_v_i8m8_L2 (p, s, vl); }

vint8m2_t load_t_L2_m2 (const signed char *p, ptrdiff_t s, size_t vl)
{ return __riscv_vmttl_v_i8m2_L2 (p, s, vl); }

void store_t_L2_m2 (signed char *p, ptrdiff_t s, vint8m2_t v, size_t vl)
{ __riscv_vmtts_v_i8m2_L2 (p, s, v, vl); }

/* { dg-final { scan-assembler-times {\tvmtl\.v\t}  2 } } */
/* { dg-final { scan-assembler-times {\tvmts\.v\t}  1 } } */
/* { dg-final { scan-assembler-times {\tvmttl\.v\t} 1 } } */
/* { dg-final { scan-assembler-times {\tvmtts\.v\t} 1 } } */
