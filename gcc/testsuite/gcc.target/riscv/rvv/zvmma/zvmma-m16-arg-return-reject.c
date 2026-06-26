/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb_zvfh_zvfbfmin -mabi=lp64d -O2" } */

/* Diagnostic test for the IME spec's prohibition on using `m16'
   accumulator types as function arguments or return values.

   Spec text (`IME-only m16 accumulator types' section):

      An IME `m16' type is valid as:
        * the accumulator operand and result type of an IME
          multiply-accumulate intrinsic when EMUL_C = 16;
        * the operand or result type of the IME pair/unpair
          intrinsics;
        * a compiler-managed temporary, assignment, or copy object
          when supported by the implementation.

   "Argument" and "return value" are not in that list, so we must
   reject signatures that use m16 in either position.  This test
   guards `riscv_get_arg_info' against silent regressions of the
   diagnostic emission.

   `riscv_get_arg_info' is invoked multiple times per parameter
   across compiler passes (prototype, expand, ...); without the
   (function, direction) dedup hash set, each violation would emit
   2x or more identical diagnostics.  This test exercises the dedup
   via f5 (two m16 parameters of the same type), which must emit
   exactly one argument-side sorry.  */

#pragma riscv intrinsic "vector"

/* Argument-side violation: `f1' takes a vint32m16_t parameter.  */
void
f1 (vint32m16_t a)  /* { dg-message "not allowed as an argument type" } */
{
  (void) a;
}

/* Return-side violation: `f2' returns a vint32m16_t.  The diagnostic
   for a m16 return-only signature fires at the function body's `{'
   line, not at the return-type declaration line.  */
vint32m16_t
f2 (void)
{ /* { dg-message "not allowed as a return type" } */
  vint32m16_t x;
  return x;
}

/* Different element-type variant: vfloat32m16_t as argument.  */
void
f3 (vfloat32m16_t a)  /* { dg-message "not allowed as an argument type" } */
{
  (void) a;
}

/* OFP8 m16 variant.  */
void
f4 (vfloat8e4m3m16_t a)  /* { dg-message "not allowed as an argument type" } */
{
  (void) a;
}

/* Multiple m16 parameters of the same type in one function emit only
   ONE sorry, thanks to the (function, direction) dedup -- the user
   only needs to learn the lesson once per function.  */
void
f5 (vint64m16_t a, vint64m16_t b)  /* { dg-message "not allowed as an argument type" } */
{
  (void) a;
  (void) b;
}

/* { dg-prune-output "compilation terminated" } */
