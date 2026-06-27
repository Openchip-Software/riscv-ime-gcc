/* Builtins implementation for RISC-V 'V' Extension for GNU compiler.
   Copyright (C) 2022-2026 Free Software Foundation, Inc.
   Contributed by Ju-Zhe Zhong (juzhe.zhong@rivai.ai), RiVAI Technologies Ltd.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "memmodel.h"
#include "insn-codes.h"
#include "optabs.h"
#include "recog.h"
#include "diagnostic.h"
#include "expr.h"
#include "function.h"
#include "fold-const.h"
#include "gimplify.h"
#include "explow.h"
#include "stor-layout.h"
#include "alias.h"
#include "langhooks.h"
#include "stringpool.h"
#include "attribs.h"
#include "targhooks.h"
#include "regs.h"
#include "emit-rtl.h"
#include "basic-block.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "riscv-vector-builtins.h"
#include "riscv-vector-builtins-shapes.h"
#include "riscv-vector-builtins-bases.h"
#include "sifive-vector-builtins-bases.h"
#include "andes-vector-builtins-bases.h"

using namespace riscv_vector;

namespace riscv_vector {

/* Static information about each vector type.  */
struct vector_type_info
{
  /* The name of the type as declared by riscv_vector.h
     which is recommend to use. For example: 'vint32m1_t'.  */
  const char *name;

  /* ABI name of vector type. The type is always available
     under this name, even when riscv_vector.h isn't included.
     For example:  '__rvv_int32m1_t'.  */
  const char *abi_name;

  /* The C++ mangling of ABI_NAME.  */
  const char *mangled_name;
};

/* Describes a function decl.  */
class GTY (()) registered_function
{
public:
  function_instance GTY ((skip)) instance;

  /* The decl itself.  */
  tree GTY ((skip)) decl;

  /* The overload hash of non-overloaded intrinsic is determined by
     the overload name and argument list. Adding the overload name to
     the hash is also to address the following situations:
     vint16mf4_t  __riscv_vreinterpret_i16mf4 (vfloat16mf4_t src);
     vuint16mf4_t __riscv_vreinterpret_u16mf4 (vfloat16mf4_t src);
     The base, shape and argument list of the vreinterpret instance are
     the same, only the overload name is different. Therefore, it is
     enough to add overload_name and argument list to the hash value.*/
  const char *overload_name;

  /* The argument list part of the hash value. Add the unsigned/signed type
     and machine mode of each argument to the hash value. */
  vec<tree> GTY ((skip)) argument_types;

  /* True if the decl represents an overloaded function that needs to be
     resolved. */
  bool overloaded_p;

  /* The hash value to indicate the non-overloaded function. Generate hash value
     based on overload_name and argument_types. */
  hashval_t overloaded_hash () const;

  /* Generate hash value based on the overload_name and the argument list passed
     by the user when calling. */
  hashval_t overloaded_hash (const vec<tree, va_gc> &);

  /* The required extension for the register function.  */
  enum required_ext required;
};

/* Hash traits for registered_function.  */
struct registered_function_hasher : nofree_ptr_hash<registered_function>
{
  typedef function_instance compare_type;

  static hashval_t hash (value_type);
  static bool equal (value_type, const compare_type &);
};

/* Hash traits for overload registered_function. */
struct non_overloaded_registered_function_hasher
  : nofree_ptr_hash<registered_function>
{
  static hashval_t hash (value_type);
  static bool equal (value_type, const compare_type &);
};

/* Static information about each RVV type.  */
static CONSTEXPR const vector_type_info vector_types[] = {
#define DEF_RVV_TYPE(NAME, NCHARS, ABI_NAME, ARGS...)                          \
  {#NAME, #ABI_NAME, "u" #NCHARS #ABI_NAME},
#define DEF_RVV_TUPLE_TYPE(NAME, NCHARS, ABI_NAME, ARGS...)                    \
  {#NAME, #ABI_NAME, "u" #NCHARS #ABI_NAME},
#include "riscv-vector-builtins.def"
};

/* Static information about operand suffix for each RVV type.  */
const char *const operand_suffixes[NUM_OP_TYPES] = {
  "", /* OP_TYPE_none.  */
#define DEF_RVV_OP_TYPE(NAME) "_" # NAME,
#include "riscv-vector-builtins.def"
};

/* Static information about type suffix for each RVV type.  */
const rvv_builtin_suffixes type_suffixes[NUM_VECTOR_TYPES + 1] = {
#define DEF_RVV_TYPE(NAME, NCHARS, ABI_NAME, SCALAR_TYPE, VECTOR_MODE,         \
		     VECTOR_SUFFIX, SCALAR_SUFFIX, VSETVL_SUFFIX)              \
  {#VECTOR_SUFFIX, #SCALAR_SUFFIX, #VSETVL_SUFFIX},
#define DEF_RVV_TUPLE_TYPE(NAME, NCHARS, ABI_NAME, SUBPART_TYPE, SCALAR_TYPE,  \
			   NF, VECTOR_SUFFIX)                                  \
  {#VECTOR_SUFFIX, "", ""},
#include "riscv-vector-builtins.def"
};

/* Static information about predication suffix for each RVV type.  */
const char *const predication_suffixes[NUM_PRED_TYPES] = {
  "", /* PRED_TYPE_none.  */
#define DEF_RVV_PRED_TYPE(NAME) "_" # NAME,
#include "riscv-vector-builtins.def"
};

/* A list of all signed integer will be registered for intrinsic functions.  */
static const rvv_type_info none_ops[] = {{NUM_VECTOR_TYPES, 0}};

/* A list of all signed integer will be registered for intrinsic functions.  */
static const rvv_type_info i_ops[] = {
#define DEF_RVV_I_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* IME `m16' signed / unsigned integer accumulator types.  These are
   used only by IME mmacc intrinsics with an EMUL_C=16 accumulator
   (i.e. `__riscv_vmmacc_vv_*m16').  */
static const rvv_type_info i_m16_ops[] = {
#define DEF_RVV_I_M16_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

static const rvv_type_info u_m16_ops[] = {
#define DEF_RVV_U_M16_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all signed integer can be widened will be registered for intrinsic
 * functions.  */
static const rvv_type_info wi_ops[] = {
#define DEF_RVV_WI_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all unsigned integer can be widened will be registered for
 * intrinsic functions.  */
static const rvv_type_info wu_ops[] = {
#define DEF_RVV_WU_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all floating-point can be widened will be registered for intrinsic
 * functions.  */
static const rvv_type_info wf_ops[] = {
#define DEF_RVV_WF_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all signed integer that SEW = 64 require full 'V' extension will be
   registered for intrinsic functions.  */
static const rvv_type_info full_v_i_ops[] = {
#define DEF_RVV_FULL_V_I_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all unsigned integer that SEW = 64 require full 'V' extension will
   be registered for intrinsic functions.  */
static const rvv_type_info full_v_u_ops[] = {
#define DEF_RVV_FULL_V_U_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all unsigned integer will be registered for intrinsic functions.  */
static const rvv_type_info u_ops[] = {
#define DEF_RVV_U_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all signed integer will be registered for intrinsic functions.  */
static const rvv_type_info convert_i_ops[] = {
#define DEF_RVV_CONVERT_I_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all unsigned integer will be registered for intrinsic functions.  */
static const rvv_type_info convert_u_ops[] = {
#define DEF_RVV_CONVERT_U_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all signed integer will be registered for intrinsic functions.  */
static const rvv_type_info wconvert_i_ops[] = {
#define DEF_RVV_WCONVERT_I_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all unsigned integer will be registered for intrinsic functions. */
static const rvv_type_info wconvert_u_ops[] = {
#define DEF_RVV_WCONVERT_U_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all floating-point will be registered for intrinsic functions. */
static const rvv_type_info wconvert_f_ops[] = {
#define DEF_RVV_WCONVERT_F_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all floating-point will be registered for intrinsic functions. */
static const rvv_type_info f32_ops[] = {
#define DEF_RVV_F32_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all integer will be registered for intrinsic functions.  */
static const rvv_type_info iu_ops[] = {
#define DEF_RVV_I_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#define DEF_RVV_U_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all types will be registered for intrinsic functions.  */
static const rvv_type_info all_ops[] = {
#define DEF_RVV_I_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#define DEF_RVV_U_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#define DEF_RVV_F_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all types will be registered for intrinsic functions.  */
static const rvv_type_info ei16_ops[] = {
#define DEF_RVV_EI16_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all bool will be registered for intrinsic functions.  */
static const rvv_type_info b_ops[] = {
#define DEF_RVV_B_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all float will be registered for intrinsic functions.  */
static const rvv_type_info f_ops[] = {
#define DEF_RVV_F_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of Double-Widening signed integer will be registered for intrinsic
 * functions.  */
static const rvv_type_info wexti_ops[] = {
#define DEF_RVV_WEXTI_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of Double-Widening float will be registered for intrinsic functions.
 */
static const rvv_type_info wextf_ops[] = {
#define DEF_RVV_WEXTF_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of Quad-Widening signed integer will be registered for intrinsic
 * functions.  */
static const rvv_type_info qexti_ops[] = {
#define DEF_RVV_QEXTI_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of Oct-Widening signed integer will be registered for intrinsic
 * functions.  */
static const rvv_type_info oexti_ops[] = {
#define DEF_RVV_OEXTI_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of Double-Widening unsigned integer will be registered for intrinsic
 * functions.  */
static const rvv_type_info wextu_ops[] = {
#define DEF_RVV_WEXTU_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of Double-Widening all integer will be registered for intrinsic
 * functions.  */
static const rvv_type_info wextiu_ops[] = {
#define DEF_RVV_WEXTI_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#define DEF_RVV_WEXTU_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of Quad-Widening unsigned integer will be registered for intrinsic
 * functions.  */
static const rvv_type_info qextu_ops[] = {
#define DEF_RVV_QEXTU_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of Oct-Widening unsigned integer will be registered for intrinsic
 * functions.  */
static const rvv_type_info oextu_ops[] = {
#define DEF_RVV_OEXTU_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of eew8 interpret will be registered for intrinsic functions.  */
static const rvv_type_info eew8_interpret_ops[] = {
#define DEF_RVV_EEW8_INTERPRET_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of eew16 interpret will be registered for intrinsic functions.  */
static const rvv_type_info eew16_interpret_ops[] = {
#define DEF_RVV_EEW16_INTERPRET_OPS(TYPE, REQUIRE)                             \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of eew32 interpret will be registered for intrinsic functions.  */
static const rvv_type_info eew32_interpret_ops[] = {
#define DEF_RVV_EEW32_INTERPRET_OPS(TYPE, REQUIRE)                             \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of eew64 interpret will be registered for intrinsic functions.  */
static const rvv_type_info eew64_interpret_ops[] = {
#define DEF_RVV_EEW64_INTERPRET_OPS(TYPE, REQUIRE)                             \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of bool1 interpret will be registered for intrinsic functions.  */
static const rvv_type_info bool1_interpret_ops[] = {
#define DEF_RVV_BOOL1_INTERPRET_OPS(TYPE, REQUIRE)                             \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of bool2 interpret will be registered for intrinsic functions.  */
static const rvv_type_info bool2_interpret_ops[] = {
#define DEF_RVV_BOOL2_INTERPRET_OPS(TYPE, REQUIRE)                             \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of bool4 interpret will be registered for intrinsic functions.  */
static const rvv_type_info bool4_interpret_ops[] = {
#define DEF_RVV_BOOL4_INTERPRET_OPS(TYPE, REQUIRE)                             \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of bool8 interpret will be registered for intrinsic functions.  */
static const rvv_type_info bool8_interpret_ops[] = {
#define DEF_RVV_BOOL8_INTERPRET_OPS(TYPE, REQUIRE)                             \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of bool16 interpret will be registered for intrinsic functions.  */
static const rvv_type_info bool16_interpret_ops[] = {
#define DEF_RVV_BOOL16_INTERPRET_OPS(TYPE, REQUIRE)                            \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of bool32 interpret will be registered for intrinsic functions.  */
static const rvv_type_info bool32_interpret_ops[] = {
#define DEF_RVV_BOOL32_INTERPRET_OPS(TYPE, REQUIRE)                            \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of bool64 interpret will be registered for intrinsic functions.  */
static const rvv_type_info bool64_interpret_ops[] = {
#define DEF_RVV_BOOL64_INTERPRET_OPS(TYPE, REQUIRE)                            \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of vint8m1 interpret will be registered for intrinsic functions.  */
static const rvv_type_info signed_eew8_lmul1_interpret_ops[] = {
#define DEF_RVV_SIGNED_EEW8_LMUL1_INTERPRET_OPS(TYPE, REQUIRE)                 \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of vint16m1 interpret will be registered for intrinsic functions.  */
static const rvv_type_info signed_eew16_lmul1_interpret_ops[] = {
#define DEF_RVV_SIGNED_EEW16_LMUL1_INTERPRET_OPS(TYPE, REQUIRE)                \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of vint32m1 interpret will be registered for intrinsic functions.  */
static const rvv_type_info signed_eew32_lmul1_interpret_ops[] = {
#define DEF_RVV_SIGNED_EEW32_LMUL1_INTERPRET_OPS(TYPE, REQUIRE)                \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of vint64m1 interpret will be registered for intrinsic functions.  */
static const rvv_type_info signed_eew64_lmul1_interpret_ops[] = {
#define DEF_RVV_SIGNED_EEW64_LMUL1_INTERPRET_OPS(TYPE, REQUIRE)                \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of vuint8m1 interpret will be registered for intrinsic functions.  */
static const rvv_type_info unsigned_eew8_lmul1_interpret_ops[] = {
#define DEF_RVV_UNSIGNED_EEW8_LMUL1_INTERPRET_OPS(TYPE, REQUIRE)               \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of vuint16m1 interpret will be registered for intrinsic functions.  */
static const rvv_type_info unsigned_eew16_lmul1_interpret_ops[] = {
#define DEF_RVV_UNSIGNED_EEW16_LMUL1_INTERPRET_OPS(TYPE, REQUIRE)              \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of vuint32m1 interpret will be registered for intrinsic functions.  */
static const rvv_type_info unsigned_eew32_lmul1_interpret_ops[] = {
#define DEF_RVV_UNSIGNED_EEW32_LMUL1_INTERPRET_OPS(TYPE, REQUIRE)              \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of vuint64m1 interpret will be registered for intrinsic functions.  */
static const rvv_type_info unsigned_eew64_lmul1_interpret_ops[] = {
#define DEF_RVV_UNSIGNED_EEW64_LMUL1_INTERPRET_OPS(TYPE, REQUIRE)              \
  {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of x2 vlmul ext will be registered for intrinsic functions.  */
static const rvv_type_info vlmul_ext_x2_ops[] = {
#define DEF_RVV_X2_VLMUL_EXT_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of x4 vlmul ext will be registered for intrinsic functions.  */
static const rvv_type_info vlmul_ext_x4_ops[] = {
#define DEF_RVV_X4_VLMUL_EXT_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of x8 vlmul ext will be registered for intrinsic functions.  */
static const rvv_type_info vlmul_ext_x8_ops[] = {
#define DEF_RVV_X8_VLMUL_EXT_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of x16 vlmul ext will be registered for intrinsic functions.  */
static const rvv_type_info vlmul_ext_x16_ops[] = {
#define DEF_RVV_X16_VLMUL_EXT_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of x32 vlmul ext will be registered for intrinsic functions.  */
static const rvv_type_info vlmul_ext_x32_ops[] = {
#define DEF_RVV_X32_VLMUL_EXT_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of x64 vlmul ext will be registered for intrinsic functions.  */
static const rvv_type_info vlmul_ext_x64_ops[] = {
#define DEF_RVV_X64_VLMUL_EXT_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of LMUL1 will be registered for intrinsic functions.  */
static const rvv_type_info lmul1_ops[] = {
#define DEF_RVV_LMUL1_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of LMUL2 will be registered for intrinsic functions.  */
static const rvv_type_info lmul2_ops[] = {
#define DEF_RVV_LMUL2_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of LMUL4 will be registered for intrinsic functions.  */
static const rvv_type_info lmul4_ops[] = {
#define DEF_RVV_LMUL4_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of Tuple types will be registered for intrinsic functions.  */
static const rvv_type_info tuple_ops[] = {
#define DEF_RVV_TUPLE_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* Below types will be registered for vector-crypto intrinsic functions*/
/* A list of sew32 will be registered for vector-crypto intrinsic functions.  */
static const rvv_type_info crypto_sew32_ops[] = {
#define DEF_RVV_CRYPTO_SEW32_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of sew64 will be registered for vector-crypto intrinsic functions.  */
static const rvv_type_info crypto_sew64_ops[] = {
#define DEF_RVV_CRYPTO_SEW64_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of signed integer will be registered for Sifive Xsfvcp intrinsic*/
/* functions.  */
static const rvv_type_info x2_u_ops[] = {
#define DEF_RVV_X2_U_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of signed integer will be registered for Sifive Xsfvcp intrinsic*/
/* functions.  */
static const rvv_type_info x2_wu_ops[] = {
#define DEF_RVV_X2_WU_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of signed integer will be registered for intrinsic
 * functions.  */
static const rvv_type_info qmacc_ops[] = {
#define DEF_RVV_QMACC_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of signed integer will be registered for intrinsic functions. */
static const rvv_type_info xfqf_ops[] = {
#define DEF_RVV_XFQF_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all vint8m_t will be registered for intrinsic functions.  */
static const rvv_type_info q_ops[] = {
#define DEF_RVV_Q_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of all vuint8m_t will be registered for intrinsic functions.  */
static const rvv_type_info qu_ops[] = {
#define DEF_RVV_QU_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

/* A list of FP16 will be registered for intrinsic functions.  */
static const rvv_type_info f16_ops[] = {
#define DEF_RVV_F16_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
  {NUM_VECTOR_TYPES, 0}};

static CONSTEXPR const rvv_arg_type_info rvv_arg_type_info_end
  = rvv_arg_type_info (NUM_BASE_TYPES);

/* A list of args for size_t func () function.  */
static CONSTEXPR const rvv_arg_type_info void_args[] = {rvv_arg_type_info_end};

/* A list of args for size_t func () function.  */
static CONSTEXPR const rvv_arg_type_info end_args[]
  = {rvv_arg_type_info_end};

/* A list of args for size_t func (size_t) function.  */
static CONSTEXPR const rvv_arg_type_info size_args[]
  = {rvv_arg_type_info (RVV_BASE_size), rvv_arg_type_info_end};

/* A list of args for vector_type func (const scalar_type *) function.  */
static CONSTEXPR const rvv_arg_type_info scalar_const_ptr_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_const_ptr), rvv_arg_type_info_end};

/* A list of args for vector_type func (const scalar_type *, size_t *) function.
 */
static CONSTEXPR const rvv_arg_type_info scalar_const_ptr_size_ptr_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_const_ptr),
     rvv_arg_type_info (RVV_BASE_size_ptr), rvv_arg_type_info_end};

/* A list of args for void func (scalar_type *, vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info scalar_ptr_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_ptr),
     rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (const scalar_type *, ptrdiff_t)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_const_ptr_ptrdiff_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_const_ptr),
     rvv_arg_type_info (RVV_BASE_ptrdiff), rvv_arg_type_info_end};

/* A list of args for void func (scalar_type *, ptrdiff_t, vector_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_ptr_ptrdiff_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_ptr),
     rvv_arg_type_info (RVV_BASE_ptrdiff), rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (const scalar_type *, eew8_index_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_const_ptr_eew8_index_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_const_ptr),
     rvv_arg_type_info (RVV_BASE_eew8_index), rvv_arg_type_info_end};

/* A list of args for vector_type func (const scalar_type *, eew16_index_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_const_ptr_eew16_index_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_const_ptr),
     rvv_arg_type_info (RVV_BASE_eew16_index), rvv_arg_type_info_end};

/* A list of args for vector_type func (const scalar_type *, eew32_index_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_const_ptr_eew32_index_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_const_ptr),
     rvv_arg_type_info (RVV_BASE_eew32_index), rvv_arg_type_info_end};

/* A list of args for vector_type func (const scalar_type *, eew64_index_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_const_ptr_eew64_index_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_const_ptr),
     rvv_arg_type_info (RVV_BASE_eew64_index), rvv_arg_type_info_end};

/* A list of args for void func (scalar_type *, eew8_index_type, vector_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_ptr_eew8_index_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_ptr),
     rvv_arg_type_info (RVV_BASE_eew8_index),
     rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info_end};

/* A list of args for void func (scalar_type *, eew16_index_type, vector_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_ptr_eew16_index_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_ptr),
     rvv_arg_type_info (RVV_BASE_eew16_index),
     rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info_end};

/* A list of args for void func (scalar_type *, eew32_index_type, vector_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_ptr_eew32_index_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_ptr),
     rvv_arg_type_info (RVV_BASE_eew32_index),
     rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info_end};

/* A list of args for void func (scalar_type *, eew64_index_type, vector_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_ptr_eew64_index_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_ptr),
     rvv_arg_type_info (RVV_BASE_eew64_index),
     rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info vv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, vector_type, vector_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info vvv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info_end};

/* A list of args for an IME multiply-accumulate intrinsic: the accumulator
   (RVV_BASE_vector -- matches the instance type index, which encodes the
   SEW and the accumulator-specific register-group multiplier MUL_C) plus
   two inputs fixed at LMUL=1 (RVV_BASE_lmul1_vector -- same SEW as vd, but
   LMUL=1 regardless of MUL_C).  This expresses the spec's default
   (LMUL=1) non-widening vmmacc.vv variant:

       vint32m2_t __riscv_vmmacc_vv_i32m2 (vint32m2_t vd,
                                           vint32m1_t vs1,
                                           vint32m1_t vs2,
                                           size_t vl);

   where m2 on vd denotes MUL_C=2 and the m1 on vs1/vs2 denotes LMUL=1.  */
static CONSTEXPR const rvv_arg_type_info ime_vss_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_lmul1_vector), rvv_arg_type_info_end};

/* Args for the W=1 mmacc _lm{N} variants: vs1/vs2 use LMUL=N at the same
   SEW as the accumulator, independent of the accumulator's MUL_C.  */
static CONSTEXPR const rvv_arg_type_info ime_vss_lm2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_lmul2_vector),
     rvv_arg_type_info (RVV_BASE_lmul2_vector), rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_vss_lm4_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_lmul4_vector),
     rvv_arg_type_info (RVV_BASE_lmul4_vector), rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_vss_lm8_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_lmul8_vector),
     rvv_arg_type_info (RVV_BASE_lmul8_vector), rvv_arg_type_info_end};

/* Args for the W=1 mixed-sign vmmacc (_su / _us) variants.  The
   accumulator sits at SEW with MUL_C; vs1 and vs2 share the SEW and
   LMUL but use opposite signedness.  Spec mandates a signed
   accumulator for all four sign combinations.  */
static CONSTEXPR const rvv_arg_type_info ime_vss_su_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_lmul1_signed_vector),
     rvv_arg_type_info (RVV_BASE_lmul1_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_vss_us_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_lmul1_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_lmul1_signed_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_vss_su_lm2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_lmul2_signed_vector),
     rvv_arg_type_info (RVV_BASE_lmul2_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_vss_us_lm2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_lmul2_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_lmul2_signed_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_vss_su_lm4_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_lmul4_signed_vector),
     rvv_arg_type_info (RVV_BASE_lmul4_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_vss_us_lm4_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_lmul4_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_lmul4_signed_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_vss_su_lm8_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_lmul8_signed_vector),
     rvv_arg_type_info (RVV_BASE_lmul8_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_vss_us_lm8_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_lmul8_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_lmul8_signed_vector),
     rvv_arg_type_info_end};

/* Args for the W=2 widening IME multiply-accumulate (vwmmacc.vv).  The
   accumulator sits at SEW with MUL_C; the two inputs are SEW/2 and
   LMUL=1 via ime_double_trunc_lmul1_vector.  Four variants cover the
   four sign combinations; for same-sign cases the inputs inherit
   signedness from the accumulator type index, for mixed-sign we force
   each side.  */
static CONSTEXPR const rvv_arg_type_info ime_wvss_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul1_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_wvss_su_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul1_signed_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul1_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_wvss_us_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul1_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul1_signed_vector),
     rvv_arg_type_info_end};

/* Args for the W=4 widening IME multiply-accumulate (vqwmmacc.vv).
   The accumulator is at SEW with MUL_C; inputs are SEW/4 and LMUL=1
   (ime_quad_trunc_lmul1_*_vector).  */
static CONSTEXPR const rvv_arg_type_info ime_qwvss_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul1_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_qwvss_su_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul1_signed_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul1_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_qwvss_us_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul1_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul1_signed_vector),
     rvv_arg_type_info_end};

/* Args for the W=8 widening IME multiply-accumulate (v8wmmacc.vv).
   The accumulator is at SEW with MUL_C; inputs are SEW/8 and LMUL=1
   (ime_oct_trunc_lmul1_*_vector).  */
static CONSTEXPR const rvv_arg_type_info ime_8wvss_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul1_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_8wvss_su_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul1_signed_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul1_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_8wvss_us_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul1_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul1_signed_vector),
     rvv_arg_type_info_end};

/* Args for the W=2 widening IME multiply-accumulate _lm{N} variants.
   The accumulator sits at SEW with MUL_C; the two inputs are SEW/2 and
   LMUL=N (independent of MUL_C) via ime_double_trunc_lmul{N}_*_vector.
   Three input LMULs (2, 4, 8) crossed with three sign combinations
   (default / _su / _us) gives 9 args arrays per width.  */
static CONSTEXPR const rvv_arg_type_info ime_wvss_lm2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul2_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul2_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_wvss_su_lm2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul2_signed_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul2_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_wvss_us_lm2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul2_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul2_signed_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_wvss_lm4_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul4_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul4_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_wvss_su_lm4_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul4_signed_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul4_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_wvss_us_lm4_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul4_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul4_signed_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_wvss_lm8_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul8_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul8_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_wvss_su_lm8_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul8_signed_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul8_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_wvss_us_lm8_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul8_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_ime_double_trunc_lmul8_signed_vector),
     rvv_arg_type_info_end};

/* W=4 (vqwmmacc.vv) widening _lm{N} args.  */
static CONSTEXPR const rvv_arg_type_info ime_qwvss_lm2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul2_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul2_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_qwvss_su_lm2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul2_signed_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul2_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_qwvss_us_lm2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul2_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul2_signed_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_qwvss_lm4_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul4_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul4_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_qwvss_su_lm4_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul4_signed_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul4_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_qwvss_us_lm4_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul4_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul4_signed_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_qwvss_lm8_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul8_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul8_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_qwvss_su_lm8_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul8_signed_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul8_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_qwvss_us_lm8_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul8_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_ime_quad_trunc_lmul8_signed_vector),
     rvv_arg_type_info_end};

/* W=8 (v8wmmacc.vv) widening _lm{N} args.  */
static CONSTEXPR const rvv_arg_type_info ime_8wvss_lm2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul2_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul2_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_8wvss_su_lm2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul2_signed_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul2_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_8wvss_us_lm2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul2_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul2_signed_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_8wvss_lm4_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul4_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul4_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_8wvss_su_lm4_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul4_signed_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul4_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_8wvss_us_lm4_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul4_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul4_signed_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_8wvss_lm8_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul8_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul8_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_8wvss_su_lm8_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul8_signed_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul8_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_8wvss_us_lm8_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul8_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_ime_oct_trunc_lmul8_signed_vector),
     rvv_arg_type_info_end};

/* Args for FP IME vfmmacc (W=1): acc, lmul1 same-type, lmul1 same-type.
   Reuses ime_vss_args since the operand layout is identical.  */

/* Args for FP IME widening variants: acc is FP at some SEW, inputs are
   a fixed FP type at LMUL=1.  */
static CONSTEXPR const rvv_arg_type_info ime_fwvss_f16_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_fp16_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_fp16_lmul1_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_fwvss_bf16_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_bf16_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_bf16_lmul1_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_fwvss_f32_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_fp32_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_fp32_lmul1_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_fwvss_f8e4m3_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_f8e4m3_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_f8e4m3_lmul1_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_fwvss_f8e5m2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_f8e5m2_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_f8e5m2_lmul1_vector),
     rvv_arg_type_info_end};

/* Mixed-input OFP8 args: vs1 / vs2 select different OFP8 formats per
   the IME spec.  Both register groups use RVVM*QI at the GCC level;
   the C type system distinguishes E4M3 from E5M2 via the
   ime_f8e{4m3,5m2}_lmul1_vector base types.  */
static CONSTEXPR const rvv_arg_type_info ime_fwvss_f8e4m3_f8e5m2_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_f8e4m3_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_f8e5m2_lmul1_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_fwvss_f8e5m2_f8e4m3_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_f8e5m2_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_f8e4m3_lmul1_vector),
     rvv_arg_type_info_end};

/* Same-format OFP8 args: vs1 = vs2.  Used by W=1 vfmmacc OFP8 (the
   accumulator format is determined by the type set, not by the args
   list).  These reuse `ime_fwvss_f8e{4m3,5m2}_args' from above
   since the operand layout is identical to the W=4 same-format case.  */

static CONSTEXPR const rvv_arg_type_info ime_fwvss_f4e2m1_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_f4e2m1_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_f4e2m1_lmul1_vector),
     rvv_arg_type_info_end};

/* Args for microscaled FP-input scaled variants (section 8): same as
   the non-scaled variant but with an extra vuint16m1_t scales operand.  */
static CONSTEXPR const rvv_arg_type_info ime_scaled_fwvss_f16_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_fp16_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_fp16_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_scales_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_scaled_fwvss_f8e4m3_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_f8e4m3_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_f8e4m3_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_scales_vector),
     rvv_arg_type_info_end};

/* Args for microscaled integer-input variants (section 8): FP accumulator,
   two int8 inputs, plus vuint16m1_t scales.  */
static CONSTEXPR const rvv_arg_type_info ime_immacc_i8_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_ime_i8_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_i8_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_ime_scales_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, vector_type, vector_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info vqq_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_quad_fixed_vector),
     rvv_arg_type_info (RVV_BASE_quad_fixed_vector), rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info su_vqq_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_quad_fixed_vector),
     rvv_arg_type_info (RVV_BASE_quad_fixed_unsigned_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, vector_type, vector_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info vxv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, vector_type, mask_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info vvm_args[]
  = {rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_mask), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, mask_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info vm_args[]
  = {rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info (RVV_BASE_mask),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, scalar_type, mask_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info vxm_args[]
  = {rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_mask), rvv_arg_type_info_end};

/* A list of args for vector_type func (signed vector_type, unsigned
 * vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info su_vv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_unsigned_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, scalar_type) function.  */
static CONSTEXPR const rvv_arg_type_info vx_args[]
  = {rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (signed vector_type, unsigned
 * scalar_type) function.  */
static CONSTEXPR const rvv_arg_type_info su_vx_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_unsigned_scalar), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, shift_type) function.  */
static CONSTEXPR const rvv_arg_type_info shift_vv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_shift_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, shift_type) function.  */
static CONSTEXPR const rvv_arg_type_info gather_vv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_unsigned_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, shift_type) function.  */
static CONSTEXPR const rvv_arg_type_info gatherei16_vv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_eew16_index), rvv_arg_type_info_end};

/* A list of args for double demote type func (vector_type, shift_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info shift_wv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info clip_args[]
  = {rvv_arg_type_info (RVV_BASE_xfqf_vector),
     rvv_arg_type_info (RVV_BASE_xfqf_float),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info v_args[]
  = {rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info v_x2_trunc_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x2), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info v_x4_trunc_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x4), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info v_x8_trunc_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x8), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info v_x16_trunc_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x16), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info v_x32_trunc_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x32), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info v_x64_trunc_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x64), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, lmul1_type) function.  */
static CONSTEXPR const rvv_arg_type_info vs_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_lmul1_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, widen_lmul1_type) function.
 */
static CONSTEXPR const rvv_arg_type_info wvs_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_widen_lmul1_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info f_v_args[]
  = {rvv_arg_type_info (RVV_BASE_float_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info trunc_f_v_args[]
  = {rvv_arg_type_info (RVV_BASE_double_trunc_float_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info w_v_args[]
  = {rvv_arg_type_info (RVV_BASE_double_trunc_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info bf_w_v_args[]
  = {rvv_arg_type_info (RVV_BASE_double_trunc_bfloat_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info bf_wwvv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_bfloat_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_bfloat_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info bf_wwxv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_bfloat_scalar),
     rvv_arg_type_info (RVV_BASE_double_trunc_bfloat_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info m_args[]
  = {rvv_arg_type_info (RVV_BASE_mask), rvv_arg_type_info_end};

/* A list of args for vector_type func (scalar_type/sf.vc) function.  */
static CONSTEXPR const rvv_arg_type_info x_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, size) function.  */
static CONSTEXPR const rvv_arg_type_info v_size_args[]
  = {rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info (RVV_BASE_size),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (double demote_type, size_t) function.  */
static CONSTEXPR const rvv_arg_type_info wv_size_args[]
  = {rvv_arg_type_info (RVV_BASE_double_trunc_vector),
    rvv_arg_type_info (RVV_BASE_size),rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, vector_type, size)
 * function.  */
static CONSTEXPR const rvv_arg_type_info vv_size_args[]
  = {rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_size), rvv_arg_type_info_end};

/* A list of args for vector_type func (double demote type) function.  */
static CONSTEXPR const rvv_arg_type_info vf2_args[]
  = {rvv_arg_type_info (RVV_BASE_double_trunc_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (double demote type, double demote type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info wvv_args[]
  = {rvv_arg_type_info (RVV_BASE_double_trunc_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, double demote type, double
 * demote type) function.  */
static CONSTEXPR const rvv_arg_type_info wwvv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, double demote type, double
 * demote type) function.  */
static CONSTEXPR const rvv_arg_type_info wwxv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_scalar),
     rvv_arg_type_info (RVV_BASE_double_trunc_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, double demote type, double
 * demote type) function.  */
static CONSTEXPR const rvv_arg_type_info su_wwvv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_unsigned_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, double demote type, double
 * demote type) function.  */
static CONSTEXPR const rvv_arg_type_info su_wwxv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_scalar),
     rvv_arg_type_info (RVV_BASE_double_trunc_unsigned_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, double demote type, double
 * demote type) function.  */
static CONSTEXPR const rvv_arg_type_info us_wwxv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_unsigned_scalar),
     rvv_arg_type_info (RVV_BASE_double_trunc_vector),
     rvv_arg_type_info_end};

/* A static operand information for vector_type func (vector_type, quad lmul1
 * type, quad half lmul type) function registration. */
static CONSTEXPR const rvv_arg_type_info qqvv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_quad_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_quad_emul_vector), rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info uqqvv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_quad_lmul1_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_quad_emul_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info su_qqvv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_quad_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_quad_emul_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info us_qqvv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_quad_lmul1_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_quad_emul_vector), rvv_arg_type_info_end};

/* A static operand information for vector_type func (vector_type, quad lmul1
 * type, quad emul type) function registration. */
static CONSTEXPR const rvv_arg_type_info qdvv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_quad_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_quad_fixed_vector), rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info uqdvv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_quad_lmul1_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_quad_fixed_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info su_qdvv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_quad_lmul1_vector),
     rvv_arg_type_info (RVV_BASE_quad_fixed_unsigned_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info us_qdvv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_quad_lmul1_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_quad_fixed_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (signed double demote type,
 * unsigneddouble demote type) function.  */
static CONSTEXPR const rvv_arg_type_info su_wvv_args[]
  = {rvv_arg_type_info (RVV_BASE_double_trunc_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_unsigned_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (double demote type, double demote type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info wvx_args[]
  = {rvv_arg_type_info (RVV_BASE_double_trunc_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_scalar), rvv_arg_type_info_end};

/* A list of args for vector_type func (signed double demote type, unsigned
 * double demote type) function.  */
static CONSTEXPR const rvv_arg_type_info su_wvx_args[]
  = {rvv_arg_type_info (RVV_BASE_double_trunc_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_unsigned_scalar),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (double demote type, double demote type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info wwv_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (double demote type, double demote type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info wwx_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_double_trunc_scalar), rvv_arg_type_info_end};

/* A list of args for vector_type func (quad demote type) function.  */
static CONSTEXPR const rvv_arg_type_info vf4_args[]
  = {rvv_arg_type_info (RVV_BASE_quad_trunc_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (oct demote type) function.  */
static CONSTEXPR const rvv_arg_type_info vf8_args[]
  = {rvv_arg_type_info (RVV_BASE_oct_trunc_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (double demote type) function.  */
static CONSTEXPR const rvv_arg_type_info x_x_v_args[]
  = {rvv_arg_type_info (RVV_BASE_double_trunc_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info x_v_args[]
  = {rvv_arg_type_info (RVV_BASE_signed_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info xu_v_args[]
  = {rvv_arg_type_info (RVV_BASE_unsigned_vector), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info w_x_v_args[]
  = {rvv_arg_type_info (RVV_BASE_double_trunc_signed_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info w_xu_v_args[]
  = {rvv_arg_type_info (RVV_BASE_double_trunc_unsigned_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info ext_x2_vset_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x2),
     rvv_arg_type_info (RVV_BASE_size), rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info ext_x4_vset_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x4),
     rvv_arg_type_info (RVV_BASE_size), rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info ext_x8_vset_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x8),
     rvv_arg_type_info (RVV_BASE_size), rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info ext_x2_vget_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x2),
     rvv_arg_type_info (RVV_BASE_size), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info ext_x4_vget_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x4),
     rvv_arg_type_info (RVV_BASE_size), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info ext_x8_vget_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x8),
     rvv_arg_type_info (RVV_BASE_size), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info tuple_vset_args[]
  = {rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info (RVV_BASE_size),
     rvv_arg_type_info (RVV_BASE_tuple_subpart), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info tuple_vcreate_args[]
  = {rvv_arg_type_info (RVV_BASE_tuple_subpart), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type) function.  */
static CONSTEXPR const rvv_arg_type_info ext_vcreate_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (const scalar_type *, size_t)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_const_ptr_size_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_const_ptr),
     rvv_arg_type_info (RVV_BASE_size), rvv_arg_type_info_end};

/* A list of args for vector_type func (const scalar_type *, eew8_index_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_const_ptr_index_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_const_ptr),
     rvv_arg_type_info (RVV_BASE_unsigned_vector), rvv_arg_type_info_end};

/* A list of args for void func (scalar_type *, eew8_index_type, vector_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_ptr_index_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_ptr),
     rvv_arg_type_info (RVV_BASE_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info_end};

/* A list of args for void func (scalar_type *, size_t, vector_type)
 * function.  */
static CONSTEXPR const rvv_arg_type_info scalar_ptr_size_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar_ptr),
     rvv_arg_type_info (RVV_BASE_size), rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.x) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_x_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.v.x) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_v_x_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.i) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_i_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info_end};


/* A list of args for vector_type func (sf.vc.i) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_v_i_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.vv) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_vv_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.v.vv) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_v_vv_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.xv) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_xv_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.v.xv) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_v_xv_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.iv) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_iv_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.v.iv) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_v_iv_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.fv) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_fv_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_scalar_float),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.v.fv) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_v_fv_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_scalar_float),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.vvv/sf.vc.v.vvv) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_vvv_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.xvv/sf.vc.v.xvv) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_xvv_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.ivv/sf.vc.v.ivv) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_ivv_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_unsigned_vector),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.fvv/sf.vc.v.fvv) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_fvv_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_scalar_float),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.vvw/sf.vc.v.vvw) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_vvw_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_x2_vector),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.xvw/sf.vc.v.xvw) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_xvw_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_x2_vector),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.ivw/sf.vc.v.ivw) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_ivw_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_x2_vector),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (sf.vc.fvw/sf.vc.v.fvw) function.  */
static CONSTEXPR const rvv_arg_type_info sf_vc_fvw_args[]
  = {rvv_arg_type_info (RVV_BASE_scalar),
     rvv_arg_type_info (RVV_BASE_x2_vector),
     rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_scalar_float),
     rvv_arg_type_info_end};

/* A list of args for vector_type func (const void_type *) function.  */
static CONSTEXPR const rvv_arg_type_info void_const_ptr_args[]
  = {rvv_arg_type_info (RVV_BASE_void_const_ptr), rvv_arg_type_info_end};

/* A list of args for vector_type func (vector_type, widen_lmul1_scalar)
   function.  */
static CONSTEXPR const rvv_arg_type_info vw_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info (RVV_BASE_float32), rvv_arg_type_info_end};

/* A list of none preds that will be registered for intrinsic functions.  */
static CONSTEXPR const predication_type_index none_preds[]
  = {PRED_TYPE_none, NUM_PRED_TYPES};

/* vop/vop_m/vop_tu/vop_tum/vop_tumu/vop_mu will be registered.  */
static CONSTEXPR const predication_type_index full_preds[]
  = {PRED_TYPE_none, PRED_TYPE_m,  PRED_TYPE_tu,  PRED_TYPE_tum,
     PRED_TYPE_tumu, PRED_TYPE_mu, NUM_PRED_TYPES};

/* vop/vop_m/vop_tu/vop_tum/ will be registered.  */
static CONSTEXPR const predication_type_index no_mu_preds[]
  = {PRED_TYPE_none, PRED_TYPE_m, PRED_TYPE_tu, PRED_TYPE_tum, NUM_PRED_TYPES};

/* vop/vop_tu will be registered.  */
static CONSTEXPR const predication_type_index none_tu_preds[]
  = {PRED_TYPE_none, PRED_TYPE_tu, NUM_PRED_TYPES};

/* vop/vop_m will be registered.  */
static CONSTEXPR const predication_type_index none_m_preds[]
  = {PRED_TYPE_none, PRED_TYPE_m, NUM_PRED_TYPES};

/* vop/vop_m/vop_mu will be registered.  */
static CONSTEXPR const predication_type_index none_m_mu_preds[]
  = {PRED_TYPE_none, PRED_TYPE_m, PRED_TYPE_mu, NUM_PRED_TYPES};

/* A static operand information for size_t func () function registration. */
static CONSTEXPR const rvv_op_info i_none_size_void_ops
  = {i_ops,				/* Types */
     OP_TYPE_none,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_size), /* Return type */
     void_args /* Args */};

/* A static operand information for size_t func (size_t) function registration.
 */
static CONSTEXPR const rvv_op_info i_none_size_size_ops
  = {i_ops,				/* Types */
     OP_TYPE_none,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_size), /* Return type */
     size_args /* Args */};

/* A static operand information for vector_type func () function registration.
 */
static CONSTEXPR const rvv_op_info all_none_void_ops
  = {all_ops,				  /* Types */
     OP_TYPE_none,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     void_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_scalar_const_ptr_ops
  = {all_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_scalar_const_ptr_size_ptr_ops
  = {all_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_size_ptr_args /* Args */};

/* A static operand information for void func (scalar_type *, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_scalar_ptr_ops
  = {all_ops,				/* Types */
     OP_TYPE_v,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     scalar_ptr_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *)
 * function registration. */
static CONSTEXPR const rvv_op_info b_v_scalar_const_ptr_ops
  = {b_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_args /* Args */};

/* A static operand information for void func (scalar_type *, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info b_v_scalar_ptr_ops
  = {b_ops,				/* Types */
     OP_TYPE_v,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     scalar_ptr_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info b_mmm_ops
  = {b_ops,				  /* Types */
     OP_TYPE_mm,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vv_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info b_mm_ops
  = {b_ops,				  /* Types */
     OP_TYPE_m,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_vm_ops
  = {u_ops,				  /* Types */
     OP_TYPE_m,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     m_args /* Args */};

/* A static operand information for vector_type func ()
 * function registration. */
static CONSTEXPR const rvv_op_info b_m_ops
  = {b_ops,				  /* Types */
     OP_TYPE_m,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     end_args /* Args */};

/* A static operand information for vector_type func ()
 * function registration. */
static CONSTEXPR const rvv_op_info u_v_ops
  = {u_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     end_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_vv_ops
  = {u_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for unsigned long func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info b_ulong_m_ops
  = {b_ops,					 /* Types */
     OP_TYPE_m,					 /* Suffix */
     rvv_arg_type_info (RVV_BASE_unsigned_long), /* Return type */
     v_args /* Args */};

/* A static operand information for long func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info b_long_m_ops
  = {b_ops,				/* Types */
     OP_TYPE_m,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_long), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *,
 * ptrdiff_t) function registration. */
static CONSTEXPR const rvv_op_info all_v_scalar_const_ptr_ptrdiff_ops
  = {all_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_ptrdiff_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *,
 * eew8_index_type) function registration. */
static CONSTEXPR const rvv_op_info all_v_scalar_const_ptr_eew8_index_ops
  = {all_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_eew8_index_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *,
 * eew16_index_type) function registration. */
static CONSTEXPR const rvv_op_info all_v_scalar_const_ptr_eew16_index_ops
  = {all_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_eew16_index_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *,
 * eew32_index_type) function registration. */
static CONSTEXPR const rvv_op_info all_v_scalar_const_ptr_eew32_index_ops
  = {all_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_eew32_index_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *,
 * eew64_index_type) function registration. */
static CONSTEXPR const rvv_op_info all_v_scalar_const_ptr_eew64_index_ops
  = {all_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_eew64_index_args /* Args */};

/* A static operand information for void func (scalar_type *, ptrdiff_t,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info all_v_scalar_ptr_ptrdiff_ops
  = {all_ops,				/* Types */
     OP_TYPE_v,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     scalar_ptr_ptrdiff_args /* Args */};

/* A static operand information for void func (scalar_type *, eew8_index_type,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info all_v_scalar_ptr_eew8_index_ops
  = {all_ops,				/* Types */
     OP_TYPE_v,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     scalar_ptr_eew8_index_args /* Args */};

/* A static operand information for void func (scalar_type *, eew16_index_type,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info all_v_scalar_ptr_eew16_index_ops
  = {all_ops,				/* Types */
     OP_TYPE_v,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     scalar_ptr_eew16_index_args /* Args */};

/* A static operand information for void func (scalar_type *, eew32_index_type,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info all_v_scalar_ptr_eew32_index_ops
  = {all_ops,				/* Types */
     OP_TYPE_v,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     scalar_ptr_eew32_index_args /* Args */};

/* A static operand information for void func (scalar_type *, eew64_index_type,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info all_v_scalar_ptr_eew64_index_ops
  = {all_ops,				/* Types */
     OP_TYPE_v,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     scalar_ptr_eew64_index_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_vvv_ops
  = {iu_ops,				/* Types */
     OP_TYPE_vv,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vv_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info iu_vvvv_ops
  = {iu_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, scalar_type,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info iu_vvxv_ops
  = {iu_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vxv_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info f_vvvv_ops
  = {f_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, scalar_type,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info f_vvfv_ops
  = {f_ops,				  /* Types */
     OP_TYPE_vf,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vxv_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type,
 * mask_type) function registration. */
static CONSTEXPR const rvv_op_info iu_vvvm_ops
  = {iu_ops,				  /* Types */
     OP_TYPE_vvm,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vvm_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type,
 * mask_type) function registration. */
static CONSTEXPR const rvv_op_info all_vvvm_ops
  = {all_ops,				  /* Types */
     OP_TYPE_vvm,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vvm_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type,
 * mask_type) function registration. */
static CONSTEXPR const rvv_op_info all_vvm_ops
  = {all_ops,				  /* Types */
     OP_TYPE_vm,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vm_args /* Args */};

/* A static operand information for vector_type func (vector_type, scalar_type,
 * mask_type) function registration. */
static CONSTEXPR const rvv_op_info iu_vvxm_ops
  = {iu_ops,				  /* Types */
     OP_TYPE_vxm,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vxm_args /* Args */};

/* A static operand information for vector_type func (vector_type, scalar_type,
 * mask_type) function registration. */
static CONSTEXPR const rvv_op_info f_vvfm_ops
  = {f_ops,				  /* Types */
     OP_TYPE_vfm,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vxm_args /* Args */};

/* A static operand information for mask_type func (vector_type, vector_type,
 * mask_type) function registration. */
static CONSTEXPR const rvv_op_info iu_mvvm_ops
  = {iu_ops,				  /* Types */
     OP_TYPE_vvm,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_mask), /* Return type */
     vvm_args /* Args */};

/* A static operand information for mask_type func (vector_type, scalar_type,
 * mask_type) function registration. */
static CONSTEXPR const rvv_op_info iu_mvxm_ops
  = {iu_ops,				  /* Types */
     OP_TYPE_vxm,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_mask), /* Return type */
     vxm_args /* Args */};

/* A static operand information for mask_type func (vector_type, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_mvv_ops
  = {iu_ops,				/* Types */
     OP_TYPE_vv,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_mask), /* Return type */
     vv_args /* Args */};

/* A static operand information for mask_type func (vector_type, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_mvv_ops
  = {i_ops,				/* Types */
     OP_TYPE_vv,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_mask), /* Return type */
     vv_args /* Args */};

/* A static operand information for mask_type func (vector_type, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_mvv_ops
  = {u_ops,				/* Types */
     OP_TYPE_vv,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_mask), /* Return type */
     vv_args /* Args */};

/* A static operand information for mask_type func (vector_type, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_mvv_ops
  = {f_ops,				/* Types */
     OP_TYPE_vv,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_mask), /* Return type */
     vv_args /* Args */};

/* A static operand information for mask_type func (vector_type, scalar_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_mvx_ops
  = {iu_ops,				/* Types */
     OP_TYPE_vx,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_mask), /* Return type */
     vx_args /* Args */};

/* A static operand information for mask_type func (vector_type, scalar_type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_mvx_ops
  = {i_ops,				/* Types */
     OP_TYPE_vx,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_mask), /* Return type */
     vx_args /* Args */};

/* A static operand information for mask_type func (vector_type, scalar_type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_mvx_ops
  = {u_ops,				/* Types */
     OP_TYPE_vx,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_mask), /* Return type */
     vx_args /* Args */};

/* A static operand information for mask_type func (vector_type, scalar_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_mvf_ops
  = {f_ops,				/* Types */
     OP_TYPE_vf,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_mask), /* Return type */
     vx_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_vvv_ops
  = {i_ops,				/* Types */
     OP_TYPE_vv,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vv_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_vvv_ops
  = {u_ops,				/* Types */
     OP_TYPE_vv,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vv_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_vvv_ops
  = {f_ops,				/* Types */
     OP_TYPE_vv,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vv_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_vvf_ops
  = {f_ops,				/* Types */
     OP_TYPE_vf,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vx_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info full_v_i_vvv_ops
  = {full_v_i_ops,			/* Types */
     OP_TYPE_vv,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vv_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info full_v_u_vvv_ops
  = {full_v_u_ops,			/* Types */
     OP_TYPE_vv,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vv_args /* Args */};

/* A static operand information for vector_type func (signed vector_type,
 * unsigned vector_type) function registration. */
static CONSTEXPR const rvv_op_info full_v_i_su_vvv_ops
  = {full_v_i_ops,			  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     su_vv_args /* Args */};

/* A static operand information for vector_type func (vector_type, scalar_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_vvx_ops
  = {iu_ops,				/* Types */
     OP_TYPE_vx,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vx_args /* Args */};

/* A static operand information for vector_type func (vector_type, scalar_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_vvx_ops
  = {all_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_size_args /* Args */};

/* A static operand information for vector_type func (vector_type, vector_type,
 * scalar_type) function registration. */
static CONSTEXPR const rvv_op_info all_vvvx_ops
  = {all_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vv_size_args /* Args */};

/* A static operand information for vector_type func (vector_type, scalar_type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_vvx_ops
  = {i_ops,				/* Types */
     OP_TYPE_vx,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vx_args /* Args */};

/* A static operand information for vector_type func (vector_type, scalar_type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_vvx_ops
  = {u_ops,				/* Types */
     OP_TYPE_vx,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vx_args /* Args */};

/* A static operand information for vector_type func (vector_type, scalar_type)
 * function registration that require full 'V' extension. */
static CONSTEXPR const rvv_op_info full_v_i_vvx_ops
  = {full_v_i_ops,			  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vx_args /* Args */};

/* A static operand information for vector_type func (vector_type, scalar_type)
 * function registration that require full 'V' extension. */
static CONSTEXPR const rvv_op_info full_v_u_vvx_ops
  = {full_v_u_ops,			  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vx_args /* Args */};

/* A static operand information for vector_type func (signed vector_type,
 * unsigned scalar_type) function registration that require full 'V' extension.
 */
static CONSTEXPR const rvv_op_info full_v_i_su_vvx_ops
  = {full_v_i_ops,			  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     su_vx_args /* Args */};

/* A static operand information for vector_type func (vector_type, shift_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_shift_vvv_ops
  = {iu_ops,				/* Types */
     OP_TYPE_vv,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     shift_vv_args /* Args */};

/* A static operand information for scalar_type func (vector_type, size_t)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_x_s_u_ops
  = {iu_ops,          /* Types */
     OP_TYPE_vx,        /* Suffix */
     rvv_arg_type_info (RVV_BASE_scalar), /* Return type */
     v_size_args /* Args */};

/* A static operand information for vector_type func (vector_type, size_t)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_shift_vvx_ops
  = {iu_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_size_args /* Args */};

/* A static operand information for vector_type func (vector_type, shift_type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_shift_vvv_ops
  = {i_ops,				/* Types */
     OP_TYPE_vv,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     shift_vv_args /* Args */};

/* A static operand information for vector_type func (vector_type, size_t)
 * function registration. */
static CONSTEXPR const rvv_op_info i_shift_vvx_ops
  = {i_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_size_args /* Args */};

/* A static operand information for vector_type func (vector_type, shift_type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_shift_vvv_ops
  = {u_ops,				/* Types */
     OP_TYPE_vv,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     shift_vv_args /* Args */};

/* A static operand information for vector_type func (vector_type, size_t)
 * function registration. */
static CONSTEXPR const rvv_op_info u_shift_vvx_ops
  = {u_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_size_args /* Args */};

/* A static operand information for vector_type func (vector_type, index_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_gather_vvv_ops
  = {all_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     gather_vv_args /* Args */};

/* A static operand information for vector_type func (vector_type, size_t)
 * function registration. */
static CONSTEXPR const rvv_op_info all_gather_vvx_ops
  = {all_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_size_args /* Args */};

/* A static operand information for vector_type func (vector_type, index_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_gatherei16_vvv_ops
  = {ei16_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     gatherei16_vv_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_v_ops
  = {iu_ops,			/* Types */
     OP_TYPE_v,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for scalar_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_x_s_ops
  = {iu_ops,				  /* Types */
     OP_TYPE_s,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_scalar), /* Return type */
     v_args /* Args */};

/* A static operand information for scalar_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_f_s_ops
  = {f_ops,				  /* Types */
     OP_TYPE_s,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_scalar), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_vs_ops
  = {iu_ops,					/* Types */
     OP_TYPE_vs,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_lmul1_vector), /* Return type */
     vs_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_vs_ops
  = {f_ops,					/* Types */
     OP_TYPE_vs,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_lmul1_vector), /* Return type */
     vs_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info wi_vs_ops
  = {wi_ops,					      /* Types */
     OP_TYPE_vs,				      /* Suffix */
     rvv_arg_type_info (RVV_BASE_widen_lmul1_vector), /* Return type */
     wvs_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info wu_vs_ops
  = {wu_ops,					      /* Types */
     OP_TYPE_vs,				      /* Suffix */
     rvv_arg_type_info (RVV_BASE_widen_lmul1_vector), /* Return type */
     wvs_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info wf_vs_ops
  = {wf_ops,					      /* Types */
     OP_TYPE_vs,				      /* Suffix */
     rvv_arg_type_info (RVV_BASE_widen_lmul1_vector), /* Return type */
     wvs_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_v_ops
  = {f_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_to_u_v_ops
  = {convert_u_ops,			  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     f_v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_to_i_f_v_ops
  = {convert_i_ops,			  /* Types */
     OP_TYPE_f_v,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     f_v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_to_wi_f_v_ops
  = {wconvert_i_ops,			  /* Types */
     OP_TYPE_f_v,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     trunc_f_v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_to_ni_f_w_ops
  = {f_ops,						      /* Types */
     OP_TYPE_f_w,					      /* Suffix */
     rvv_arg_type_info (RVV_BASE_double_trunc_signed_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_to_nu_f_w_ops
  = {f_ops,	  /* Types */
     OP_TYPE_f_w, /* Suffix */
     rvv_arg_type_info (
       RVV_BASE_double_trunc_unsigned_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_to_f_x_v_ops
  = {f_ops,				  /* Types */
     OP_TYPE_x_v,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     x_v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_to_f_xu_v_ops
  = {f_ops,				  /* Types */
     OP_TYPE_xu_v,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     xu_v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_to_wf_x_v_ops
  = {f_ops,				  /* Types */
     OP_TYPE_x_v,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     w_x_v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_to_wf_xu_v_ops
  = {f_ops,				  /* Types */
     OP_TYPE_xu_v,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     w_xu_v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_to_nf_x_w_ops
  = {wconvert_i_ops,					     /* Types */
     OP_TYPE_x_w,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_double_trunc_float_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_to_nf_xu_w_ops
  = {wconvert_u_ops,					     /* Types */
     OP_TYPE_xu_w,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_double_trunc_float_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_to_u_f_v_ops
  = {convert_u_ops,			  /* Types */
     OP_TYPE_f_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     f_v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_to_wu_f_v_ops
  = {wconvert_u_ops,			  /* Types */
     OP_TYPE_f_v,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     trunc_f_v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_to_wf_f_v_ops
  = {f_ops,				  /* Types */
     OP_TYPE_f_v,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     w_v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_to_nf_f_w_ops
  = {wconvert_f_ops,					     /* Types */
     OP_TYPE_f_w,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_double_trunc_float_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f32_to_bf16_f_w_ops
  = {f32_ops,						      /* Types */
     OP_TYPE_f_w,					      /* Suffix */
     rvv_arg_type_info (RVV_BASE_double_trunc_bfloat_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info bf16_to_f32_f_v_ops
  = {f32_ops,				  /* Types */
     OP_TYPE_f_v,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     bf_w_v_args /* Args */};

/* A static operand information for vector_type func (vector_type, double demote
 * type, double demote type) function registration. */
static CONSTEXPR const rvv_op_info f32_wwvv_ops
  = {f32_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     bf_wwvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, double demote
 * scalar_type, double demote type) function registration. */
static CONSTEXPR const rvv_op_info f32_wwfv_ops
  = {f32_ops,				  /* Types */
     OP_TYPE_vf,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     bf_wwxv_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_ops
  = {all_ops,			/* Types */
     OP_TYPE_v,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_v_u_ops
  = {i_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_unsigned_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_v_i_ops
  = {u_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_signed_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_v_eew8_interpret_ops
  = {eew8_interpret_ops,			  /* Types */
     OP_TYPE_v,					  /* Suffix */
     rvv_arg_type_info (RVV_BASE_eew8_interpret), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_v_eew16_interpret_ops
  = {eew16_interpret_ops,			   /* Types */
     OP_TYPE_v,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_eew16_interpret), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_v_eew32_interpret_ops
  = {eew32_interpret_ops,			   /* Types */
     OP_TYPE_v,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_eew32_interpret), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_v_eew64_interpret_ops
  = {eew64_interpret_ops,			   /* Types */
     OP_TYPE_v,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_eew64_interpret), /* Return type */
     v_args /* Args */};

/* A static operand information for vbool1_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_v_bool1_interpret_ops
  = {bool1_interpret_ops,			   /* Types */
     OP_TYPE_v,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_bool1_interpret), /* Return type */
     v_args					   /* Args */};

/* A static operand information for vbool2_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_v_bool2_interpret_ops
  = {bool2_interpret_ops,			   /* Types */
     OP_TYPE_v,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_bool2_interpret), /* Return type */
     v_args					   /* Args */};

/* A static operand information for vbool4_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_v_bool4_interpret_ops
  = {bool4_interpret_ops,			   /* Types */
     OP_TYPE_v,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_bool4_interpret), /* Return type */
     v_args					   /* Args */};

/* A static operand information for vbool8_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_v_bool8_interpret_ops
  = {bool8_interpret_ops,			   /* Types */
     OP_TYPE_v,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_bool8_interpret), /* Return type */
     v_args					   /* Args */};

/* A static operand information for vbool16_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_v_bool16_interpret_ops
  = {bool16_interpret_ops,			    /* Types */
     OP_TYPE_v,					    /* Suffix */
     rvv_arg_type_info (RVV_BASE_bool16_interpret), /* Return type */
     v_args					    /* Args */};

/* A static operand information for vbool32_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_v_bool32_interpret_ops
  = {bool32_interpret_ops,			    /* Types */
     OP_TYPE_v,					    /* Suffix */
     rvv_arg_type_info (RVV_BASE_bool32_interpret), /* Return type */
     v_args					    /* Args */};

/* A static operand information for vbool64_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_v_bool64_interpret_ops
  = {bool64_interpret_ops,			    /* Types */
     OP_TYPE_v,					    /* Suffix */
     rvv_arg_type_info (RVV_BASE_bool64_interpret), /* Return type */
     v_args					    /* Args */};

/* A static operand information for vint8_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info b_v_signed_eew8_lmul1_interpret_ops
  = {signed_eew8_lmul1_interpret_ops,			      /* Types */
     OP_TYPE_v,						      /* Suffix */
     rvv_arg_type_info (RVV_BASE_signed_eew8_lmul1_interpret),/* Return type */
     v_args						      /* Args */};

/* A static operand information for vint16_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info b_v_signed_eew16_lmul1_interpret_ops
  = {signed_eew16_lmul1_interpret_ops,			       /* Types */
     OP_TYPE_v,						       /* Suffix */
     rvv_arg_type_info (RVV_BASE_signed_eew16_lmul1_interpret),/* Return type */
     v_args						       /* Args */};

/* A static operand information for vint32_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info b_v_signed_eew32_lmul1_interpret_ops
  = {signed_eew32_lmul1_interpret_ops,			       /* Types */
     OP_TYPE_v,						       /* Suffix */
     rvv_arg_type_info (RVV_BASE_signed_eew32_lmul1_interpret),/* Return type */
     v_args						       /* Args */};

/* A static operand information for vint64_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info b_v_signed_eew64_lmul1_interpret_ops
  = {signed_eew64_lmul1_interpret_ops,			       /* Types */
     OP_TYPE_v,						       /* Suffix */
     rvv_arg_type_info (RVV_BASE_signed_eew64_lmul1_interpret),/* Return type */
     v_args						       /* Args */};

/* A static operand information for vuint8_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info b_v_unsigned_eew8_lmul1_interpret_ops
  = {unsigned_eew8_lmul1_interpret_ops,				/* Types */
     OP_TYPE_v,							/* Suffix */
     rvv_arg_type_info (RVV_BASE_unsigned_eew8_lmul1_interpret),/* Return type */
     v_args							/* Args */};

/* A static operand information for vuint16_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info b_v_unsigned_eew16_lmul1_interpret_ops
  = {unsigned_eew16_lmul1_interpret_ops,			 /* Types */
     OP_TYPE_v,							 /* Suffix */
     rvv_arg_type_info (RVV_BASE_unsigned_eew16_lmul1_interpret),/* Return type */
     v_args							 /* Args */};

/* A static operand information for vuint32_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info b_v_unsigned_eew32_lmul1_interpret_ops
  = {unsigned_eew32_lmul1_interpret_ops,			 /* Types */
     OP_TYPE_v,							 /* Suffix */
     rvv_arg_type_info (RVV_BASE_unsigned_eew32_lmul1_interpret),/* Return type */
     v_args							 /* Args */};

/* A static operand information for vuint64_t func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info b_v_unsigned_eew64_lmul1_interpret_ops
  = {unsigned_eew64_lmul1_interpret_ops,			 /* Types */
     OP_TYPE_v,							 /* Suffix */
     rvv_arg_type_info (RVV_BASE_unsigned_eew64_lmul1_interpret),/* Return type */
     v_args							 /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vlmul_ext_x2_ops
  = {vlmul_ext_x2_ops,				/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x2), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vlmul_ext_x4_ops
  = {vlmul_ext_x4_ops,				/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x4), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vlmul_ext_x8_ops
  = {vlmul_ext_x8_ops,				/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x8), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vlmul_ext_x16_ops
  = {vlmul_ext_x16_ops,				 /* Types */
     OP_TYPE_v,					 /* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x16), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vlmul_ext_x32_ops
  = {vlmul_ext_x32_ops,				 /* Types */
     OP_TYPE_v,					 /* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x32), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vlmul_ext_x64_ops
  = {vlmul_ext_x64_ops,				 /* Types */
     OP_TYPE_v,					 /* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x64), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vlmul_trunc_x2_ops
  = {vlmul_ext_x2_ops,			  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_x2_trunc_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vlmul_trunc_x4_ops
  = {vlmul_ext_x4_ops,			  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_x4_trunc_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vlmul_trunc_x8_ops
  = {vlmul_ext_x8_ops,			  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_x8_trunc_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vlmul_trunc_x16_ops
  = {vlmul_ext_x16_ops,			  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_x16_trunc_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vlmul_trunc_x32_ops
  = {vlmul_ext_x32_ops,			  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_x32_trunc_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vlmul_trunc_x64_ops
  = {vlmul_ext_x64_ops,			  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_x64_trunc_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_v_i_ops
  = {f_ops,					 /* Types */
     OP_TYPE_v,					 /* Suffix */
     rvv_arg_type_info (RVV_BASE_signed_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_v_u_ops
  = {f_ops,					   /* Types */
     OP_TYPE_v,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_unsigned_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_v_f_ops
  = {f_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     x_v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_v_f_ops
  = {f_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     xu_v_args /* Args */};

/* A static operand information for vector_type func (scalar_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_x_ops
  = {iu_ops,			/* Types */
     OP_TYPE_x,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     x_args /* Args */};

/* A static operand information for vector_type func (scalar_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_s_x_ops
  = {iu_ops,				  /* Types */
     OP_TYPE_x,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     x_args /* Args */};

/* A static operand information for vector_type func (scalar_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_f_ops
  = {f_ops,			/* Types */
     OP_TYPE_f,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     x_args /* Args */};

/* A static operand information for vector_type func (scalar_type)
 * function registration. */
static CONSTEXPR const rvv_op_info f_s_f_ops
  = {f_ops,				  /* Types */
     OP_TYPE_f,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     x_args /* Args */};

/* A static operand information for vector_type func (double demote type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_vf2_ops
  = {wexti_ops,				  /* Types */
     OP_TYPE_vf2,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vf2_args /* Args */};

/* A static operand information for vector_type func (quad demote type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_vf4_ops
  = {qexti_ops,				  /* Types */
     OP_TYPE_vf4,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vf4_args /* Args */};

/* A static operand information for vector_type func (oct demote type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_vf8_ops
  = {oexti_ops,				  /* Types */
     OP_TYPE_vf8,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vf8_args /* Args */};

/* A static operand information for vector_type func (double demote type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_vf2_ops
  = {wextu_ops,				  /* Types */
     OP_TYPE_vf2,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vf2_args /* Args */};

/* A static operand information for vector_type func (quad demote type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_vf4_ops
  = {qextu_ops,				  /* Types */
     OP_TYPE_vf4,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vf4_args /* Args */};

/* A static operand information for vector_type func (oct demote type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_vf8_ops
  = {oextu_ops,				  /* Types */
     OP_TYPE_vf8,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vf8_args /* Args */};

/* A static operand information for vector_type func (double demote type, double
 * demote type) function registration. */
static CONSTEXPR const rvv_op_info i_wvv_ops
  = {wexti_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wvv_args /* Args */};

/* A static operand information for vector_type func (double demote type, double
 * demote type) function registration. */
static CONSTEXPR const rvv_op_info f_wvv_ops
  = {wextf_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, double demote
 * type, double demote type) function registration. */
static CONSTEXPR const rvv_op_info i_wwvv_ops
  = {wexti_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wwvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, double demote
 * scalar_type, double demote type) function registration. */
static CONSTEXPR const rvv_op_info i_wwxv_ops
  = {wexti_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wwxv_args /* Args */};

/* A static operand information for vector_type func (vector_type, double demote
 * type, double demote type) function registration. */
static CONSTEXPR const rvv_op_info f_wwvv_ops
  = {wextf_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wwvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, double demote
 * scalar_type, double demote type) function registration. */
static CONSTEXPR const rvv_op_info f_wwfv_ops
  = {wextf_ops,				  /* Types */
     OP_TYPE_vf,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wwxv_args /* Args */};

/* A static operand information for vector_type func (vector_type, double demote
 * type, double demote type) function registration. */
static CONSTEXPR const rvv_op_info u_wwvv_ops
  = {wextu_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wwvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, double demote
 * scalar_type, double demote type) function registration. */
static CONSTEXPR const rvv_op_info u_wwxv_ops
  = {wextu_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wwxv_args /* Args */};

/* A static operand information for vector_type func (vector_type, double demote
 * type, double demote type) function registration. */
static CONSTEXPR const rvv_op_info i_su_wwvv_ops
  = {wexti_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     su_wwvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, double demote
 * scalar_type, double demote type) function registration. */
static CONSTEXPR const rvv_op_info i_su_wwxv_ops
  = {wexti_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     su_wwxv_args /* Args */};

/* A static operand information for vector_type func (vector_type, double demote
 * scalar_type, double demote type) function registration. */
static CONSTEXPR const rvv_op_info i_us_wwxv_ops
  = {wexti_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     us_wwxv_args /* Args */};

/* A static operand information for vector_type func (vector_type, quad demote
 * type, quad demote type) function registration. */
static CONSTEXPR const rvv_op_info i_qqvv_ops
  = {qmacc_ops,				  /* Types */
     OP_TYPE_4x8x4,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     qqvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, quad demote
 * type, quad demote type) function registration. */
static CONSTEXPR const rvv_op_info u_qqvv_ops
  = {qmacc_ops,				  /* Types */
     OP_TYPE_4x8x4,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     uqqvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, quad demote
 * type, quad demote type) function registration. */
static CONSTEXPR const rvv_op_info i_su_qqvv_ops
  = {qmacc_ops,				  /* Types */
     OP_TYPE_4x8x4,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     su_qqvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, quad demote
 * type, quad demote type) function registration. */
static CONSTEXPR const rvv_op_info i_us_qqvv_ops
  = {qmacc_ops,				  /* Types */
     OP_TYPE_4x8x4,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     us_qqvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, quad demote
 * type, quad demote type) function registration. */
static CONSTEXPR const rvv_op_info i_qdvv_ops
  = {qmacc_ops,				  /* Types */
     OP_TYPE_2x8x2,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     qdvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, quad demote
 * type, quad demote type) function registration. */
static CONSTEXPR const rvv_op_info u_qdvv_ops
  = {qmacc_ops,				  /* Types */
     OP_TYPE_2x8x2,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     uqdvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, quad demote
 * type, quad demote type) function registration. */
static CONSTEXPR const rvv_op_info i_su_qdvv_ops
  = {qmacc_ops,				  /* Types */
     OP_TYPE_2x8x2,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     su_qdvv_args /* Args */};

/* A static operand information for vector_type func (vector_type, quad demote
 * type, quad demote type) function registration. */
static CONSTEXPR const rvv_op_info i_us_qdvv_ops
  = {qmacc_ops,				  /* Types */
     OP_TYPE_2x8x2,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     us_qdvv_args /* Args */};

/* A static operand information for vector_type func (signed double demote type,
 * unsigned double demote type) function registration. */
static CONSTEXPR const rvv_op_info i_su_wvv_ops
  = {wexti_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     su_wvv_args /* Args */};

/* A static operand information for vector_type func (double demote type, double
 * demote type) function registration. */
static CONSTEXPR const rvv_op_info u_wvv_ops
  = {wextu_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wvv_args /* Args */};

/* A static operand information for vector_type func (double demote type, size type)
 * function registration. */
static CONSTEXPR const rvv_op_info u_shift_wvx_ops
  = {wextu_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wv_size_args /* Args */};

/* A static operand information for vector_type func (double demote type, double
 * demote scalar_type) function registration. */
static CONSTEXPR const rvv_op_info i_wvx_ops
  = {wexti_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wvx_args /* Args */};

/* A static operand information for vector_type func (double demote type, double
 * demote scalar_type) function registration. */
static CONSTEXPR const rvv_op_info f_wvf_ops
  = {wextf_ops,				  /* Types */
     OP_TYPE_vf,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wvx_args /* Args */};

/* A static operand information for vector_type func (signed double demote type,
 * unsigned double demote scalar_type) function registration. */
static CONSTEXPR const rvv_op_info i_su_wvx_ops
  = {wexti_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     su_wvx_args /* Args */};

/* A static operand information for vector_type func (vector_type, double
 * demote type) function registration. */
static CONSTEXPR const rvv_op_info i_wwv_ops
  = {wexti_ops,				  /* Types */
     OP_TYPE_wv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wwv_args /* Args */};

/* A static operand information for vector_type func (vector_type, double
 * demote type) function registration. */
static CONSTEXPR const rvv_op_info f_wwv_ops
  = {wextf_ops,				  /* Types */
     OP_TYPE_wv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wwv_args /* Args */};

/* A static operand information for vector_type func (vector_type, double
 * demote scalar_type) function registration. */
static CONSTEXPR const rvv_op_info i_wwx_ops
  = {wexti_ops,				  /* Types */
     OP_TYPE_wx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wwx_args /* Args */};

/* A static operand information for vector_type func (vector_type, double
 * demote scalar_type) function registration. */
static CONSTEXPR const rvv_op_info f_wwf_ops
  = {wextf_ops,				  /* Types */
     OP_TYPE_wf,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wwx_args /* Args */};

/* A static operand information for vector_type func (vector_type, double
 * demote type) function registration. */
static CONSTEXPR const rvv_op_info u_wwv_ops
  = {wextu_ops,				  /* Types */
     OP_TYPE_wv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wwv_args /* Args */};

/* A static operand information for vector_type func (vector_type, double
 * demote scalar_type) function registration. */
static CONSTEXPR const rvv_op_info u_wwx_ops
  = {wextu_ops,				  /* Types */
     OP_TYPE_wx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wwx_args /* Args */};

/* A static operand information for vector_type func (double demote type, double
 * demote scalar_type) function registration. */
static CONSTEXPR const rvv_op_info u_wvx_ops
  = {wextu_ops,				  /* Types */
     OP_TYPE_vx,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     wvx_args /* Args */};

/* A static operand information for vector_type func (double demote type)
 * function registration. */
static CONSTEXPR const rvv_op_info i_x_x_v_ops
  = {wexti_ops,				  /* Types */
     OP_TYPE_x_v,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     x_x_v_args /* Args */};

/* A static operand information for vector_type func (unsigned double demote
 * type) function registration. */
static CONSTEXPR const rvv_op_info u_x_x_v_ops
  = {wextu_ops,				  /* Types */
     OP_TYPE_x_v,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     x_x_v_args /* Args */};

/* A static operand information for double demote type func (vector_type,
 * shift_type) function registration. */
static CONSTEXPR const rvv_op_info i_narrow_shift_vwv_ops
  = {wexti_ops,					       /* Types */
     OP_TYPE_wv,				       /* Suffix */
     rvv_arg_type_info (RVV_BASE_double_trunc_vector), /* Return type */
     shift_wv_args /* Args */};

/* A static operand information for double demote type func (vector_type,
 * shift_type) function registration. */
static CONSTEXPR const rvv_op_info u_narrow_shift_vwv_ops
  = {wextu_ops,					       /* Types */
     OP_TYPE_wv,				       /* Suffix */
     rvv_arg_type_info (RVV_BASE_double_trunc_vector), /* Return type */
     shift_wv_args /* Args */};

/* A static operand information for double demote type func (vector_type,
 * size_t) function registration. */
static CONSTEXPR const rvv_op_info i_narrow_shift_vwx_ops
  = {wexti_ops,					       /* Types */
     OP_TYPE_wx,				       /* Suffix */
     rvv_arg_type_info (RVV_BASE_double_trunc_vector), /* Return type */
     v_size_args /* Args */};

/* A static operand information for double demote type func (vector_type,
 * shift_type) function registration. */
static CONSTEXPR const rvv_op_info u_clip_qf_ops
  = {xfqf_ops,				      /* Types */
     OP_TYPE_none,			      /* Suffix */
     rvv_arg_type_info (RVV_BASE_unsigned_vector), /* Return type */
     clip_args /* Args */};

/* A static operand information for double demote type func (vector_type,
 * shift_type) function registration. */
static CONSTEXPR const rvv_op_info i_clip_qf_ops
  = {xfqf_ops,					     /* Types */
     OP_TYPE_none,				     /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     clip_args /* Args */};

/* A static operand information for double demote type func (vector_type,
 * size_t) function registration. */
static CONSTEXPR const rvv_op_info u_narrow_shift_vwx_ops
  = {wextu_ops,					       /* Types */
     OP_TYPE_wx,				       /* Suffix */
     rvv_arg_type_info (RVV_BASE_double_trunc_vector), /* Return type */
     v_size_args /* Args */};

/* A static operand information for double demote type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info iu_trunc_ops
  = {wextiu_ops,				       /* Types */
     OP_TYPE_x_w,				       /* Suffix */
     rvv_arg_type_info (RVV_BASE_double_trunc_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vset_lmul1_x2_ops
  = {lmul1_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x2), /* Return type */
     ext_x2_vset_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vset_lmul1_x4_ops
  = {lmul1_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x4), /* Return type */
     ext_x4_vset_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vset_lmul1_x8_ops
  = {lmul1_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x8), /* Return type */
     ext_x8_vset_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vset_lmul2_x2_ops
  = {lmul2_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x2), /* Return type */
     ext_x2_vset_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vset_lmul2_x4_ops
  = {lmul2_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x4), /* Return type */
     ext_x4_vset_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vset_lmul4_x2_ops
  = {lmul4_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x2), /* Return type */
     ext_x2_vset_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vget_lmul1_x2_ops
  = {lmul1_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     ext_x2_vget_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vget_lmul1_x4_ops
  = {lmul1_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     ext_x4_vget_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vget_lmul1_x8_ops
  = {lmul1_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     ext_x8_vget_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vget_lmul2_x2_ops
  = {lmul2_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     ext_x2_vget_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vget_lmul2_x4_ops
  = {lmul2_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     ext_x4_vget_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vget_lmul4_x2_ops
  = {lmul4_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     ext_x2_vget_args /* Args */};

/* A static operand information for size_t func () function registration. */
static CONSTEXPR const rvv_op_info p_none_void_ops
  = {none_ops,				/* Types */
     OP_TYPE_none,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_size), /* Return type */
     void_args /* Args */};

/* A static operand information for unsigned long func () function registration. */
static CONSTEXPR const rvv_op_info ul_none_void_ops
  = {none_ops,				/* Types */
     OP_TYPE_none,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_unsigned_long), /* Return type */
     void_args /* Args */};

/* A static operand information for unsigned long func (size_t) function
   registration.  Used by `__riscv_vsetlambda', whose argument is the
   requested lambda value (a positive power of two in {1,2,4,8,16,32,64},
   or 0 for read-only query) and whose return value is the actually
   established lambda after WARL clamping.  */
static CONSTEXPR const rvv_op_info ul_none_size_ops
  = {none_ops,				/* Types */
     OP_TYPE_none,			/* Suffix */
     rvv_arg_type_info (RVV_BASE_unsigned_long), /* Return type */
     size_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vset_tuple_ops
  = {tuple_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     tuple_vset_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vget_tuple_ops
  = {tuple_ops,					 /* Types */
     OP_TYPE_v,					 /* Suffix */
     rvv_arg_type_info (RVV_BASE_tuple_subpart), /* Return type */
     v_size_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *)
 * function registration. */
static CONSTEXPR const rvv_op_info tuple_v_scalar_const_ptr_ops
  = {tuple_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_args /* Args */};

/* A static operand information for void func (scalar_type *, vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info tuple_v_scalar_ptr_ops
  = {tuple_ops,				/* Types */
     OP_TYPE_v,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     scalar_ptr_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *,
 * ptrdiff_t) function registration. */
static CONSTEXPR const rvv_op_info tuple_v_scalar_const_ptr_ptrdiff_ops
  = {tuple_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_ptrdiff_args /* Args */};

/* A static operand information for void func (scalar_type *, ptrdiff_t,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info tuple_v_scalar_ptr_ptrdiff_ops
  = {tuple_ops,				/* Types */
     OP_TYPE_v,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     scalar_ptr_ptrdiff_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *,
 * eew8_index_type) function registration. */
static CONSTEXPR const rvv_op_info tuple_v_scalar_const_ptr_eew8_index_ops
  = {tuple_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_eew8_index_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *,
 * eew16_index_type) function registration. */
static CONSTEXPR const rvv_op_info tuple_v_scalar_const_ptr_eew16_index_ops
  = {tuple_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_eew16_index_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *,
 * eew32_index_type) function registration. */
static CONSTEXPR const rvv_op_info tuple_v_scalar_const_ptr_eew32_index_ops
  = {tuple_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_eew32_index_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *,
 * eew64_index_type) function registration. */
static CONSTEXPR const rvv_op_info tuple_v_scalar_const_ptr_eew64_index_ops
  = {tuple_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_eew64_index_args /* Args */};

/* A static operand information for void func (scalar_type *, eew8_index_type,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info tuple_v_scalar_ptr_eew8_index_ops
  = {tuple_ops,				/* Types */
     OP_TYPE_v,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     scalar_ptr_eew8_index_args /* Args */};

/* A static operand information for void func (scalar_type *, eew16_index_type,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info tuple_v_scalar_ptr_eew16_index_ops
  = {tuple_ops,				/* Types */
     OP_TYPE_v,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     scalar_ptr_eew16_index_args /* Args */};

/* A static operand information for void func (scalar_type *, eew32_index_type,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info tuple_v_scalar_ptr_eew32_index_ops
  = {tuple_ops,				/* Types */
     OP_TYPE_v,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     scalar_ptr_eew32_index_args /* Args */};

/* A static operand information for void func (scalar_type *, eew64_index_type,
 * vector_type) function registration. */
static CONSTEXPR const rvv_op_info tuple_v_scalar_ptr_eew64_index_ops
  = {tuple_ops,				/* Types */
     OP_TYPE_v,				/* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     scalar_ptr_eew64_index_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *)
 * function registration. */
static CONSTEXPR const rvv_op_info tuple_v_scalar_const_ptr_size_ptr_ops
  = {tuple_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     scalar_const_ptr_size_ptr_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vcreate_tuple_ops
  = {tuple_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     tuple_vcreate_args /* Args */};

/* A static operand information for vector_type func () function registration.
 */
static CONSTEXPR const rvv_op_info all_none_void_tuple_ops
  = {tuple_ops,				  /* Types */
     OP_TYPE_none,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     void_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vcreate_lmul1_x2_ops
  = {lmul1_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x2), /* Return type */
     ext_vcreate_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vcreate_lmul1_x4_ops
  = {lmul1_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x4), /* Return type */
     ext_vcreate_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vcreate_lmul1_x8_ops
  = {lmul1_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x8), /* Return type */
     ext_vcreate_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vcreate_lmul2_x2_ops
  = {lmul2_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x2), /* Return type */
     ext_vcreate_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vcreate_lmul2_x4_ops
  = {lmul2_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x4), /* Return type */
     ext_vcreate_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration. */
static CONSTEXPR const rvv_op_info all_v_vcreate_lmul4_x2_ops
  = {lmul4_ops,					/* Types */
     OP_TYPE_v,					/* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x2), /* Return type */
     ext_vcreate_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *,
 * size_t) function registration.  */
static CONSTEXPR const rvv_op_info all_v_scalar_const_ptr_size_ops
  = {all_ops,				  /* Types  */
     OP_TYPE_v,				  /* Suffix  */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type  */
     scalar_const_ptr_size_args /* Args */};

/* A static operand information for void func (scalar_type *, size_t,
 * vector_type) function registration.  */
static CONSTEXPR const rvv_op_info all_v_scalar_ptr_size_ops
  = {all_ops,				/* Types  */
     OP_TYPE_v,				/* Suffix  */
     rvv_arg_type_info (RVV_BASE_void), /* Return type  */
     scalar_ptr_size_args /* Args */};

/* A static operand information for vector_type func (const scalar_type *,
 * index_type) function registration.  */
static CONSTEXPR const rvv_op_info all_v_scalar_const_ptr_index_ops
  = {all_ops,				  /* Types  */
     OP_TYPE_v,				  /* Suffix  */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type  */
     scalar_const_ptr_index_args /* Args */};

/* A static operand information for void func (scalar_type *, index_type,
 * vector_type) function registration.  */
static CONSTEXPR const rvv_op_info all_v_scalar_ptr_index_ops
  = {all_ops,				/* Types  */
     OP_TYPE_v,				/* Suffix  */
     rvv_arg_type_info (RVV_BASE_void), /* Return type  */
     scalar_ptr_index_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration.  */
static CONSTEXPR const rvv_op_info f32_to_bf16_nf_w_ops
  = {f32_ops,						      /* Types */
     OP_TYPE_s,						      /* Suffix */
     rvv_arg_type_info (RVV_BASE_double_trunc_bfloat_vector), /* Return type */
     v_args /* Args */};

/* A static operand information for vector_type func (vector_type)
 * function registration.  */
static CONSTEXPR const rvv_op_info bf16_to_f32_wf_v_ops
  = {f32_ops,				  /* Types */
     OP_TYPE_bf16,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     bf_w_v_args /* Args */};

/* A static operand information for vector_type func (const void_type *)
 * function registration. */
static CONSTEXPR const rvv_op_info q_v_void_const_ptr_ops
  = {q_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     void_const_ptr_args /* Args */};

/* A static operand information for vector_type func (const void_type *)
 * function registration. */
static CONSTEXPR const rvv_op_info qu_v_void_const_ptr_ops
  = {qu_ops,				  /* Types */
     OP_TYPE_v,				  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     void_const_ptr_args /* Args */};

static CONSTEXPR const rvv_op_info f16_vvw_ops
  = {f16_ops,				  /* Types */
     OP_TYPE_vf,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vw_args /* Args */};

static CONSTEXPR const rvv_op_info qexti_vvvv_ops
  = {qexti_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vqq_args /* Args */};

static CONSTEXPR const rvv_op_info qexti_su_vvvv_ops
  = {qexti_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     su_vqq_args /* Args */};

static CONSTEXPR const rvv_op_info qextu_vvvv_ops
  = {qextu_ops,				  /* Types */
     OP_TYPE_vv,			  /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vqq_args /* Args */};

/* A static operand information for vector_type func (vector_type).
   Some insns just supports SEW=32, such as the crypto vector Zvkg extension.
 * function registration.  */
static CONSTEXPR const rvv_arg_type_info vs_lmul_x2_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x2),
     rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info vs_lmul_x4_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x4),
     rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info vs_lmul_x8_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x8),
     rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info vs_lmul_x16_args[]
  = {rvv_arg_type_info (RVV_BASE_vlmul_ext_x16),
     rvv_arg_type_info (RVV_BASE_vector), rvv_arg_type_info_end};

static CONSTEXPR const rvv_op_info u_vvv_crypto_sew32_ops
  = {crypto_sew32_ops,			   /* Types */
     OP_TYPE_vv,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vv_args /* Args */};

static CONSTEXPR const rvv_op_info u_vvvv_crypto_sew32_ops
  = {crypto_sew32_ops,			   /* Types */
     OP_TYPE_vv,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vvv_args /* Args */};

static CONSTEXPR const rvv_op_info u_vvv_size_crypto_sew32_ops
  = {crypto_sew32_ops,			   /* Types */
     OP_TYPE_vi,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vv_size_args /* Args */};

static CONSTEXPR const rvv_op_info u_vv_size_crypto_sew32_ops
  = {crypto_sew32_ops,			   /* Types */
     OP_TYPE_vi,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     v_size_args /* Args */};

static CONSTEXPR const rvv_op_info u_vvs_crypto_sew32_ops
  = {crypto_sew32_ops,			   /* Types */
     OP_TYPE_vs,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vv_args /* Args */};

static CONSTEXPR const rvv_op_info u_vvs_crypto_sew32_lmul_x2_ops
  = {crypto_sew32_ops,			   /* Types */
     OP_TYPE_vs,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x2), /* Return type */
     vs_lmul_x2_args /* Args */};

static CONSTEXPR const rvv_op_info u_vvs_crypto_sew32_lmul_x4_ops
  = {crypto_sew32_ops,			   /* Types */
     OP_TYPE_vs,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x4), /* Return type */
     vs_lmul_x4_args /* Args */};

static CONSTEXPR const rvv_op_info u_vvs_crypto_sew32_lmul_x8_ops
  = {crypto_sew32_ops,			   /* Types */
     OP_TYPE_vs,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x8), /* Return type */
     vs_lmul_x8_args /* Args */};

static CONSTEXPR const rvv_op_info u_vvs_crypto_sew32_lmul_x16_ops
  = {crypto_sew32_ops,			   /* Types */
     OP_TYPE_vs,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_vlmul_ext_x16), /* Return type */
     vs_lmul_x16_args /* Args */};

/* A static operand information for vector_type func (vector_type).
   Some insns just supports SEW=64, such as the crypto vector Zvbc extension
   vclmul.vv, vclmul.vx.
 * function registration.  */
static CONSTEXPR const rvv_op_info u_vvv_crypto_sew64_ops
  = {crypto_sew64_ops,			   /* Types */
     OP_TYPE_vv,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vv_args /* Args */};

static CONSTEXPR const rvv_op_info u_vvx_crypto_sew64_ops
  = {crypto_sew64_ops,			   /* Types */
     OP_TYPE_vx,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vx_args /* Args */};

static CONSTEXPR const rvv_op_info u_vvvv_crypto_sew64_ops
  = {crypto_sew64_ops,			   /* Types */
     OP_TYPE_vv,					   /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     vvv_args /* Args */};

/* IME integer matrix multiply-accumulate, default (LMUL=1) variant.
   The accumulator type (i_ops / u_ops) carries both SEW and MUL_C via the
   vector type index; vs1/vs2 are fixed at LMUL=1 via ime_vss_args.  */
static CONSTEXPR const rvv_op_info i_ime_vss_ops
  = {i_ops,
     OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector),
     ime_vss_args};

static CONSTEXPR const rvv_op_info u_ime_vss_ops
  = {u_ops,
     OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector),
     ime_vss_args};

/* Op-info structs for the W=1 mmacc _lm{N} variants: same accumulator
   type sets as the default LMUL=1 ops, but vs1/vs2 are at LMUL=N.  */
static CONSTEXPR const rvv_op_info i_ime_vss_lm2_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm2_args};

static CONSTEXPR const rvv_op_info i_ime_vss_lm4_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm4_args};

static CONSTEXPR const rvv_op_info i_ime_vss_lm8_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm8_args};

static CONSTEXPR const rvv_op_info u_ime_vss_lm2_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm2_args};

static CONSTEXPR const rvv_op_info u_ime_vss_lm4_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm4_args};

static CONSTEXPR const rvv_op_info u_ime_vss_lm8_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm8_args};

/* Op-info structs for the W=1 mmacc mixed-sign (_su / _us) variants.
   The spec mandates a signed accumulator for both, so all four use
   i_ops; only the operand args arrays differ.  */
static CONSTEXPR const rvv_op_info i_su_ime_vss_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_su_args};

static CONSTEXPR const rvv_op_info i_us_ime_vss_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_us_args};

static CONSTEXPR const rvv_op_info i_su_ime_vss_lm2_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_su_lm2_args};

static CONSTEXPR const rvv_op_info i_us_ime_vss_lm2_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_us_lm2_args};

static CONSTEXPR const rvv_op_info i_su_ime_vss_lm4_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_su_lm4_args};

static CONSTEXPR const rvv_op_info i_us_ime_vss_lm4_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_us_lm4_args};

static CONSTEXPR const rvv_op_info i_su_ime_vss_lm8_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_su_lm8_args};

static CONSTEXPR const rvv_op_info i_us_ime_vss_lm8_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_us_lm8_args};

/* IME `m16' W=1 mmacc op-info structs.  The accumulator type lives in
   the `i_m16_ops' / `u_m16_ops' tables; vs1/vs2 args reuse the
   existing `ime_vss_args' arrays, which look up `lmul{N}_vector' /
   `lmul{N}_{signed,unsigned}_vector' relative to the accumulator type
   via `ime_lmul_input_idx' (extended above to handle m16 acc).  */
static CONSTEXPR const rvv_op_info i_ime_m16_vss_ops
  = {i_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_args};

static CONSTEXPR const rvv_op_info u_ime_m16_vss_ops
  = {u_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_args};

static CONSTEXPR const rvv_op_info i_ime_m16_vss_lm2_ops
  = {i_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm2_args};

static CONSTEXPR const rvv_op_info i_ime_m16_vss_lm4_ops
  = {i_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm4_args};

static CONSTEXPR const rvv_op_info i_ime_m16_vss_lm8_ops
  = {i_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm8_args};

static CONSTEXPR const rvv_op_info u_ime_m16_vss_lm2_ops
  = {u_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm2_args};

static CONSTEXPR const rvv_op_info u_ime_m16_vss_lm4_ops
  = {u_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm4_args};

static CONSTEXPR const rvv_op_info u_ime_m16_vss_lm8_ops
  = {u_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm8_args};

/* Mixed-sign `_su' / `_us' variants with m16 acc.  */
static CONSTEXPR const rvv_op_info i_su_ime_m16_vss_ops
  = {i_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_su_args};

static CONSTEXPR const rvv_op_info i_us_ime_m16_vss_ops
  = {i_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_us_args};

static CONSTEXPR const rvv_op_info i_su_ime_m16_vss_lm2_ops
  = {i_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_su_lm2_args};

static CONSTEXPR const rvv_op_info i_us_ime_m16_vss_lm2_ops
  = {i_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_us_lm2_args};

static CONSTEXPR const rvv_op_info i_su_ime_m16_vss_lm4_ops
  = {i_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_su_lm4_args};

static CONSTEXPR const rvv_op_info i_us_ime_m16_vss_lm4_ops
  = {i_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_us_lm4_args};

static CONSTEXPR const rvv_op_info i_su_ime_m16_vss_lm8_ops
  = {i_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_su_lm8_args};

static CONSTEXPR const rvv_op_info i_us_ime_m16_vss_lm8_ops
  = {i_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_us_lm8_args};

/* IME `vunpairlo' / `vunpairhi' / `vpair' arg/op tables.

   These primitives compose or decompose an m16 accumulator across
   its two m8 halves.  Iteration is driven by the m16 type list
   (i_m16_ops / u_m16_ops); the m8 halves are resolved via the
   `lmul8_vector' arg kind, which `ime_lmul_input_idx' maps to the
   m8 sibling at the m16 accumulator's SEW.  The return type for
   pair is the iterating m16 type itself (RVV_BASE_vector), the
   return type for unpair is the m8 half (RVV_BASE_lmul8_vector).  */

static CONSTEXPR const rvv_arg_type_info ime_vunpair_args[]
  = {rvv_arg_type_info (RVV_BASE_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_arg_type_info ime_vpair_args[]
  = {rvv_arg_type_info (RVV_BASE_lmul8_vector),
     rvv_arg_type_info (RVV_BASE_lmul8_vector),
     rvv_arg_type_info_end};

static CONSTEXPR const rvv_op_info i_ime_vunpair_ops
  = {i_m16_ops, OP_TYPE_v,
     rvv_arg_type_info (RVV_BASE_lmul8_vector), ime_vunpair_args};

static CONSTEXPR const rvv_op_info u_ime_vunpair_ops
  = {u_m16_ops, OP_TYPE_v,
     rvv_arg_type_info (RVV_BASE_lmul8_vector), ime_vunpair_args};

static CONSTEXPR const rvv_op_info i_ime_vpair_ops
  = {i_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vpair_args};

static CONSTEXPR const rvv_op_info u_ime_vpair_ops
  = {u_m16_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vpair_args};

/* Op-info structs for vwmmacc.vv (W=2) -- same-sign (ss, uu) and
   mixed-sign (su, us).  Signed accumulator is mandated by the spec
   for both _su and _us.  */

static CONSTEXPR const rvv_op_info i_ime_wvss_ops
  = {i_ops,
     OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector),
     ime_wvss_args};

static CONSTEXPR const rvv_op_info u_ime_wvss_ops
  = {u_ops,
     OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector),
     ime_wvss_args};

static CONSTEXPR const rvv_op_info i_su_ime_wvss_ops
  = {i_ops,
     OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector),
     ime_wvss_su_args};

static CONSTEXPR const rvv_op_info i_us_ime_wvss_ops
  = {i_ops,
     OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector),
     ime_wvss_us_args};

/* Op-info structs for the W=2 widening mmacc _lm{N} variants.  Same
   accumulator type sets as the LMUL=1 ops, but vs1/vs2 are at LMUL=N
   (and at SEW/2 narrower than the accumulator).  Mixed-sign uses
   i_ops per the spec.  */
static CONSTEXPR const rvv_op_info i_ime_wvss_lm2_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_wvss_lm2_args};

static CONSTEXPR const rvv_op_info u_ime_wvss_lm2_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_wvss_lm2_args};

static CONSTEXPR const rvv_op_info i_su_ime_wvss_lm2_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_wvss_su_lm2_args};

static CONSTEXPR const rvv_op_info i_us_ime_wvss_lm2_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_wvss_us_lm2_args};

static CONSTEXPR const rvv_op_info i_ime_wvss_lm4_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_wvss_lm4_args};

static CONSTEXPR const rvv_op_info u_ime_wvss_lm4_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_wvss_lm4_args};

static CONSTEXPR const rvv_op_info i_su_ime_wvss_lm4_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_wvss_su_lm4_args};

static CONSTEXPR const rvv_op_info i_us_ime_wvss_lm4_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_wvss_us_lm4_args};

static CONSTEXPR const rvv_op_info i_ime_wvss_lm8_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_wvss_lm8_args};

static CONSTEXPR const rvv_op_info u_ime_wvss_lm8_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_wvss_lm8_args};

static CONSTEXPR const rvv_op_info i_su_ime_wvss_lm8_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_wvss_su_lm8_args};

static CONSTEXPR const rvv_op_info i_us_ime_wvss_lm8_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_wvss_us_lm8_args};

/* Op-info structs for vqwmmacc.vv (W=4).  */

static CONSTEXPR const rvv_op_info i_ime_qwvss_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_args};

static CONSTEXPR const rvv_op_info u_ime_qwvss_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_args};

static CONSTEXPR const rvv_op_info i_su_ime_qwvss_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_su_args};

static CONSTEXPR const rvv_op_info i_us_ime_qwvss_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_us_args};

/* W=4 widening mmacc _lm{N} op-info.  */
static CONSTEXPR const rvv_op_info i_ime_qwvss_lm2_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_lm2_args};

static CONSTEXPR const rvv_op_info u_ime_qwvss_lm2_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_lm2_args};

static CONSTEXPR const rvv_op_info i_su_ime_qwvss_lm2_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_su_lm2_args};

static CONSTEXPR const rvv_op_info i_us_ime_qwvss_lm2_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_us_lm2_args};

static CONSTEXPR const rvv_op_info i_ime_qwvss_lm4_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_lm4_args};

static CONSTEXPR const rvv_op_info u_ime_qwvss_lm4_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_lm4_args};

static CONSTEXPR const rvv_op_info i_su_ime_qwvss_lm4_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_su_lm4_args};

static CONSTEXPR const rvv_op_info i_us_ime_qwvss_lm4_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_us_lm4_args};

static CONSTEXPR const rvv_op_info i_ime_qwvss_lm8_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_lm8_args};

static CONSTEXPR const rvv_op_info u_ime_qwvss_lm8_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_lm8_args};

static CONSTEXPR const rvv_op_info i_su_ime_qwvss_lm8_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_su_lm8_args};

static CONSTEXPR const rvv_op_info i_us_ime_qwvss_lm8_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_qwvss_us_lm8_args};

/* Type set used by the IME tile load/store: all signed and unsigned
   integer vector types at LMUL in {1, 2, 4, 8}.  Fractional LMULs
   (mf8/mf4/mf2) are excluded -- the IME vtype lambda geometry only
   admits whole-LMUL register groups for tile transfer.  */
static const rvv_type_info iu_lmul_ge1_ops[]
  = {{VECTOR_TYPE_vint8m1_t, 0},   {VECTOR_TYPE_vint8m2_t, 0},
     {VECTOR_TYPE_vint8m4_t, 0},   {VECTOR_TYPE_vint8m8_t, 0},
     {VECTOR_TYPE_vint16m1_t, 0},  {VECTOR_TYPE_vint16m2_t, 0},
     {VECTOR_TYPE_vint16m4_t, 0},  {VECTOR_TYPE_vint16m8_t, 0},
     {VECTOR_TYPE_vint32m1_t, 0},  {VECTOR_TYPE_vint32m2_t, 0},
     {VECTOR_TYPE_vint32m4_t, 0},  {VECTOR_TYPE_vint32m8_t, 0},
     {VECTOR_TYPE_vint64m1_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vint64m2_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vint64m4_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vint64m8_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vuint8m1_t, 0},  {VECTOR_TYPE_vuint8m2_t, 0},
     {VECTOR_TYPE_vuint8m4_t, 0},  {VECTOR_TYPE_vuint8m8_t, 0},
     {VECTOR_TYPE_vuint16m1_t, 0}, {VECTOR_TYPE_vuint16m2_t, 0},
     {VECTOR_TYPE_vuint16m4_t, 0}, {VECTOR_TYPE_vuint16m8_t, 0},
     {VECTOR_TYPE_vuint32m1_t, 0}, {VECTOR_TYPE_vuint32m2_t, 0},
     {VECTOR_TYPE_vuint32m4_t, 0}, {VECTOR_TYPE_vuint32m8_t, 0},
     {VECTOR_TYPE_vuint64m1_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vuint64m2_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vuint64m4_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vuint64m8_t, RVV_REQUIRE_ELEN_64},
     {NUM_VECTOR_TYPES, 0}};

/* Type set used by the IME tile load/store, combining integer + FP
   element types at LMUL in {1, 2, 4, 8}.  The tile load/store
   instructions treat memory as opaque bytes, so they support all
   element types uniformly; the element-type distinction is purely
   for C front-end type checking.  Sub-byte element types (Int4,
   OFP4) are not included -- they would require a new scalar pointer
   base type and are less common in practice.  */
static const rvv_type_info fiu_lmul_ge1_ops[]
  = {{VECTOR_TYPE_vint8m1_t, 0},   {VECTOR_TYPE_vint8m2_t, 0},
     {VECTOR_TYPE_vint8m4_t, 0},   {VECTOR_TYPE_vint8m8_t, 0},
     {VECTOR_TYPE_vint16m1_t, 0},  {VECTOR_TYPE_vint16m2_t, 0},
     {VECTOR_TYPE_vint16m4_t, 0},  {VECTOR_TYPE_vint16m8_t, 0},
     {VECTOR_TYPE_vint32m1_t, 0},  {VECTOR_TYPE_vint32m2_t, 0},
     {VECTOR_TYPE_vint32m4_t, 0},  {VECTOR_TYPE_vint32m8_t, 0},
     {VECTOR_TYPE_vint64m1_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vint64m2_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vint64m4_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vint64m8_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vuint8m1_t, 0},  {VECTOR_TYPE_vuint8m2_t, 0},
     {VECTOR_TYPE_vuint8m4_t, 0},  {VECTOR_TYPE_vuint8m8_t, 0},
     {VECTOR_TYPE_vuint16m1_t, 0}, {VECTOR_TYPE_vuint16m2_t, 0},
     {VECTOR_TYPE_vuint16m4_t, 0}, {VECTOR_TYPE_vuint16m8_t, 0},
     {VECTOR_TYPE_vuint32m1_t, 0}, {VECTOR_TYPE_vuint32m2_t, 0},
     {VECTOR_TYPE_vuint32m4_t, 0}, {VECTOR_TYPE_vuint32m8_t, 0},
     {VECTOR_TYPE_vuint64m1_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vuint64m2_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vuint64m4_t, RVV_REQUIRE_ELEN_64},
     {VECTOR_TYPE_vuint64m8_t, RVV_REQUIRE_ELEN_64},
     /* FP types.  */
     {VECTOR_TYPE_vfloat16m1_t, RVV_REQUIRE_ELEN_FP_16},
     {VECTOR_TYPE_vfloat16m2_t, RVV_REQUIRE_ELEN_FP_16},
     {VECTOR_TYPE_vfloat16m4_t, RVV_REQUIRE_ELEN_FP_16},
     {VECTOR_TYPE_vfloat16m8_t, RVV_REQUIRE_ELEN_FP_16},
     {VECTOR_TYPE_vbfloat16m1_t, RVV_REQUIRE_ELEN_BF_16},
     {VECTOR_TYPE_vbfloat16m2_t, RVV_REQUIRE_ELEN_BF_16},
     {VECTOR_TYPE_vbfloat16m4_t, RVV_REQUIRE_ELEN_BF_16},
     {VECTOR_TYPE_vbfloat16m8_t, RVV_REQUIRE_ELEN_BF_16},
     {VECTOR_TYPE_vfloat32m1_t, RVV_REQUIRE_ELEN_FP_32},
     {VECTOR_TYPE_vfloat32m2_t, RVV_REQUIRE_ELEN_FP_32},
     {VECTOR_TYPE_vfloat32m4_t, RVV_REQUIRE_ELEN_FP_32},
     {VECTOR_TYPE_vfloat32m8_t, RVV_REQUIRE_ELEN_FP_32},
     {VECTOR_TYPE_vfloat64m1_t, RVV_REQUIRE_ELEN_FP_64},
     {VECTOR_TYPE_vfloat64m2_t, RVV_REQUIRE_ELEN_FP_64},
     {VECTOR_TYPE_vfloat64m4_t, RVV_REQUIRE_ELEN_FP_64},
     {VECTOR_TYPE_vfloat64m8_t, RVV_REQUIRE_ELEN_FP_64},
     /* OFP8 (gap #14): tile load/store treats memory as opaque bytes,
	so the OFP8 vector types share the same RVVM*QI mode as the
	integer u8 entries above; the existing @pred_vmtl<mode> /
	vmts / vmttl / vmtts patterns handle them unchanged.  The
	canonical-type-keyed overload dispatch (commit 41568e53a15e)
	keeps the OFP8 entries from aliasing onto the u8 slot.  */
     {VECTOR_TYPE_vfloat8e4m3m1_t, 0}, {VECTOR_TYPE_vfloat8e4m3m2_t, 0},
     {VECTOR_TYPE_vfloat8e4m3m4_t, 0}, {VECTOR_TYPE_vfloat8e4m3m8_t, 0},
     {VECTOR_TYPE_vfloat8e5m2m1_t, 0}, {VECTOR_TYPE_vfloat8e5m2m2_t, 0},
     {VECTOR_TYPE_vfloat8e5m2m4_t, 0}, {VECTOR_TYPE_vfloat8e5m2m8_t, 0},
     {NUM_VECTOR_TYPES, 0}};

/* FP type sets at LMUL >= 1 for IME FP matrix multiply-accumulate.  */
static const rvv_type_info fp_lmul_ge1_ops[]
  = {
#define DEF_RVV_FP_LMUL_GE1_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
     {NUM_VECTOR_TYPES, 0}};

static const rvv_type_info f16_lmul_ge1_ops[]
  = {
#define DEF_RVV_F16_LMUL_GE1_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
     {NUM_VECTOR_TYPES, 0}};

static const rvv_type_info bf16_lmul_ge1_ops[]
  = {
#define DEF_RVV_BF16_LMUL_GE1_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
     {NUM_VECTOR_TYPES, 0}};

static const rvv_type_info f32_lmul_ge1_ops[]
  = {
#define DEF_RVV_F32_LMUL_GE1_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
     {NUM_VECTOR_TYPES, 0}};

static const rvv_type_info f64_lmul_ge1_ops[]
  = {
#define DEF_RVV_F64_LMUL_GE1_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
     {NUM_VECTOR_TYPES, 0}};

/* OFP8 accumulator type sets for W=1 vfmmacc OFP8 -> OFP8.  Stage 3a
   has only m1; m2/m4/m8 acc registration is a follow-up.  */
static const rvv_type_info f8e4m3_lmul1_ops[]
  = {
#define DEF_RVV_F8E4M3_LMUL1_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
     {NUM_VECTOR_TYPES, 0}};

static const rvv_type_info f8e5m2_lmul1_ops[]
  = {
#define DEF_RVV_F8E5M2_LMUL1_OPS(TYPE, REQUIRE) {VECTOR_TYPE_##TYPE, REQUIRE},
#include "riscv-vector-builtins-types.def"
     {NUM_VECTOR_TYPES, 0}};

/* Op-info for vmtl.v tile load: (const T *base, ptrdiff_t ld) -> vector.
   The ld operand is the leading dimension measured in elements.  */
static CONSTEXPR const rvv_op_info iu_lmul_ge1_v_scalar_const_ptr_ptrdiff_ops
  = {fiu_lmul_ge1_ops,
     OP_TYPE_v,
     rvv_arg_type_info (RVV_BASE_vector),
     scalar_const_ptr_ptrdiff_args};

/* Op-info for vmts.v tile store: (T *base, ptrdiff_t ld, vector_t) -> void.
   The ld operand is the leading dimension measured in elements.  */
static CONSTEXPR const rvv_op_info iu_lmul_ge1_v_scalar_ptr_ptrdiff_ops
  = {fiu_lmul_ge1_ops,
     OP_TYPE_v,
     rvv_arg_type_info (RVV_BASE_void),
     scalar_ptr_ptrdiff_args};

/* Op-info structs for v8wmmacc.vv (W=8).  */

static CONSTEXPR const rvv_op_info i_ime_8wvss_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_args};

static CONSTEXPR const rvv_op_info u_ime_8wvss_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_args};

static CONSTEXPR const rvv_op_info i_su_ime_8wvss_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_su_args};

static CONSTEXPR const rvv_op_info i_us_ime_8wvss_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_us_args};

/* W=8 widening mmacc _lm{N} op-info.  */
static CONSTEXPR const rvv_op_info i_ime_8wvss_lm2_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_lm2_args};

static CONSTEXPR const rvv_op_info u_ime_8wvss_lm2_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_lm2_args};

static CONSTEXPR const rvv_op_info i_su_ime_8wvss_lm2_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_su_lm2_args};

static CONSTEXPR const rvv_op_info i_us_ime_8wvss_lm2_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_us_lm2_args};

static CONSTEXPR const rvv_op_info i_ime_8wvss_lm4_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_lm4_args};

static CONSTEXPR const rvv_op_info u_ime_8wvss_lm4_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_lm4_args};

static CONSTEXPR const rvv_op_info i_su_ime_8wvss_lm4_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_su_lm4_args};

static CONSTEXPR const rvv_op_info i_us_ime_8wvss_lm4_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_us_lm4_args};

static CONSTEXPR const rvv_op_info i_ime_8wvss_lm8_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_lm8_args};

static CONSTEXPR const rvv_op_info u_ime_8wvss_lm8_ops
  = {u_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_lm8_args};

static CONSTEXPR const rvv_op_info i_su_ime_8wvss_lm8_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_su_lm8_args};

static CONSTEXPR const rvv_op_info i_us_ime_8wvss_lm8_ops
  = {i_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_8wvss_us_lm8_args};

/* FP IME matrix multiply-accumulate op_info structs.  */

/* vfmmacc (W=1): all FP types at LMUL >= 1, inputs same as acc.  */
static CONSTEXPR const rvv_op_info f_ime_vss_ops
  = {fp_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_args};

/* Op-info structs for the W=1 vfmmacc _lm{N} variants.  */
static CONSTEXPR const rvv_op_info f_ime_vss_lm2_ops
  = {fp_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm2_args};

static CONSTEXPR const rvv_op_info f_ime_vss_lm4_ops
  = {fp_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm4_args};

static CONSTEXPR const rvv_op_info f_ime_vss_lm8_ops
  = {fp_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_vss_lm8_args};

/* vfwmmacc_f16: FP16 -> FP32 accumulator.  */
static CONSTEXPR const rvv_op_info f32_ime_wvss_f16_ops
  = {f32_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f16_args};

/* vfwmmacc_bf16: BF16 -> FP32 accumulator.  */
static CONSTEXPR const rvv_op_info f32_ime_wvss_bf16_ops
  = {f32_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_bf16_args};

/* vfwmmacc_f32: FP32 -> FP64 accumulator.  */
static CONSTEXPR const rvv_op_info f64_ime_wvss_f32_ops
  = {f64_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f32_args};

/* vfqwmmacc_f16: FP16 -> FP64 accumulator.  */
static CONSTEXPR const rvv_op_info f64_ime_qwvss_f16_ops
  = {f64_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f16_args};

/* vfqwmmacc_bf16: BF16 -> FP64 accumulator.  */
static CONSTEXPR const rvv_op_info f64_ime_qwvss_bf16_ops
  = {f64_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_bf16_args};

/* W=2 vfwmmacc OFP8 -> fp16 / bf16 op_infos.  Four (vs1, vs2) input
   combinations per acc format = 8 entries.  Reuses the OFP8 args
   lists from the W=4 / W=8 path; the type set picks the acc format.  */
static CONSTEXPR const rvv_op_info f16_ime_wvss_f8e4m3_ops
  = {f16_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e4m3_args};
static CONSTEXPR const rvv_op_info f16_ime_wvss_f8e4m3_f8e5m2_ops
  = {f16_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e4m3_f8e5m2_args};
static CONSTEXPR const rvv_op_info f16_ime_wvss_f8e5m2_f8e4m3_ops
  = {f16_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e5m2_f8e4m3_args};
static CONSTEXPR const rvv_op_info f16_ime_wvss_f8e5m2_ops
  = {f16_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e5m2_args};
static CONSTEXPR const rvv_op_info bf16_ime_wvss_f8e4m3_ops
  = {bf16_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e4m3_args};
static CONSTEXPR const rvv_op_info bf16_ime_wvss_f8e4m3_f8e5m2_ops
  = {bf16_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e4m3_f8e5m2_args};
static CONSTEXPR const rvv_op_info bf16_ime_wvss_f8e5m2_f8e4m3_ops
  = {bf16_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e5m2_f8e4m3_args};
static CONSTEXPR const rvv_op_info bf16_ime_wvss_f8e5m2_ops
  = {bf16_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e5m2_args};

/* W=1 vfmmacc OFP8 -> OFP8 op_infos.  Eight entries cover the full
   (acc, vs1, vs2) altfmt cube; type set picks the acc format.  */
static CONSTEXPR const rvv_op_info f8e4m3_ime_vss_f8e4m3_f8e4m3_ops
  = {f8e4m3_lmul1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e4m3_args};
static CONSTEXPR const rvv_op_info f8e4m3_ime_vss_f8e4m3_f8e5m2_ops
  = {f8e4m3_lmul1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e4m3_f8e5m2_args};
static CONSTEXPR const rvv_op_info f8e4m3_ime_vss_f8e5m2_f8e4m3_ops
  = {f8e4m3_lmul1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e5m2_f8e4m3_args};
static CONSTEXPR const rvv_op_info f8e4m3_ime_vss_f8e5m2_f8e5m2_ops
  = {f8e4m3_lmul1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e5m2_args};
static CONSTEXPR const rvv_op_info f8e5m2_ime_vss_f8e4m3_f8e4m3_ops
  = {f8e5m2_lmul1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e4m3_args};
static CONSTEXPR const rvv_op_info f8e5m2_ime_vss_f8e4m3_f8e5m2_ops
  = {f8e5m2_lmul1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e4m3_f8e5m2_args};
static CONSTEXPR const rvv_op_info f8e5m2_ime_vss_f8e5m2_f8e4m3_ops
  = {f8e5m2_lmul1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e5m2_f8e4m3_args};
static CONSTEXPR const rvv_op_info f8e5m2_ime_vss_f8e5m2_f8e5m2_ops
  = {f8e5m2_lmul1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e5m2_args};

/* vfqwmmacc_f8e4m3: OFP8_E4M3 -> FP32 accumulator.  */
static CONSTEXPR const rvv_op_info f32_ime_qwvss_f8e4m3_ops
  = {f32_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e4m3_args};

/* vfqwmmacc_f8e5m2: OFP8_E5M2 -> FP32 accumulator.  */
static CONSTEXPR const rvv_op_info f32_ime_qwvss_f8e5m2_ops
  = {f32_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e5m2_args};

/* vfqwmmacc_f8e4m3_f8e5m2: vs1=E4M3, vs2=E5M2 -> FP32 accumulator.  */
static CONSTEXPR const rvv_op_info f32_ime_qwvss_f8e4m3_f8e5m2_ops
  = {f32_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e4m3_f8e5m2_args};

/* vfqwmmacc_f8e5m2_f8e4m3: vs1=E5M2, vs2=E4M3 -> FP32 accumulator.  */
static CONSTEXPR const rvv_op_info f32_ime_qwvss_f8e5m2_f8e4m3_ops
  = {f32_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e5m2_f8e4m3_args};

/* vf8wmmacc_f8e4m3: OFP8_E4M3 -> FP64 accumulator.  */
static CONSTEXPR const rvv_op_info f64_ime_8wvss_f8e4m3_ops
  = {f64_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e4m3_args};

/* vf8wmmacc_f8e5m2: OFP8_E5M2 -> FP64 accumulator.  */
static CONSTEXPR const rvv_op_info f64_ime_8wvss_f8e5m2_ops
  = {f64_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e5m2_args};

/* vf8wmmacc_f8e4m3_f8e5m2: vs1=E4M3, vs2=E5M2 -> FP64 accumulator.  */
static CONSTEXPR const rvv_op_info f64_ime_8wvss_f8e4m3_f8e5m2_ops
  = {f64_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e4m3_f8e5m2_args};

/* vf8wmmacc_f8e5m2_f8e4m3: vs1=E5M2, vs2=E4M3 -> FP64 accumulator.  */
static CONSTEXPR const rvv_op_info f64_ime_8wvss_f8e5m2_f8e4m3_ops
  = {f64_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f8e5m2_f8e4m3_args};

/* vf8wmmacc_f4e2m1: OFP4_E2M1 -> FP32 accumulator.  */
static CONSTEXPR const rvv_op_info f32_ime_8wvss_f4e2m1_ops
  = {f32_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_fwvss_f4e2m1_args};

/* Microscaled (section 8) FP-input scaled op_info structs.  */

/* vfwmmacc_scaled_f16: FP16 -> FP32 accumulator, with scales.  */
static CONSTEXPR const rvv_op_info f32_ime_scaled_wvss_f16_ops
  = {f32_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_scaled_fwvss_f16_args};

/* vfqwmmacc_scaled_f8e4m3: OFP8_E4M3 -> FP32 accumulator, with scales.  */
static CONSTEXPR const rvv_op_info f32_ime_scaled_qwvss_f8e4m3_ops
  = {f32_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_scaled_fwvss_f8e4m3_args};

/* vf8wmmacc_scaled_f8e4m3: OFP8_E4M3 -> FP64 accumulator, with scales.  */
static CONSTEXPR const rvv_op_info f64_ime_scaled_8wvss_f8e4m3_ops
  = {f64_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_scaled_fwvss_f8e4m3_args};

/* Microscaled (section 8) integer-input op_info structs.  */

/* vfwimmacc_i8: Int8 -> FP16 accumulator, with scales.  */
static CONSTEXPR const rvv_op_info f16_ime_immacc_i8_ops
  = {f16_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_immacc_i8_args};

/* vfqwimmacc_i8: Int8 -> FP32 accumulator, with scales.  */
static CONSTEXPR const rvv_op_info f32_ime_immacc_i8_ops
  = {f32_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_immacc_i8_args};

/* vf8wimmacc_i8: Int8 -> FP64 accumulator, with scales.  */
static CONSTEXPR const rvv_op_info f64_ime_immacc_i8_ops
  = {f64_lmul_ge1_ops, OP_TYPE_vv,
     rvv_arg_type_info (RVV_BASE_vector), ime_immacc_i8_args};

static CONSTEXPR const rvv_op_info sf_vc_x_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_x,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_x_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_x_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_v_x,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     sf_vc_v_x_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_i_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_i,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_i_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_i_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_v_i,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     sf_vc_v_i_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_vv_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_vv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_vv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_vv_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_v_vv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     sf_vc_v_vv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_xv_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_xv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_xv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_xv_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_v_xv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     sf_vc_v_xv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_iv_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_iv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_iv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_iv_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_v_iv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     sf_vc_v_iv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_fv_ops
  = {wextu_ops,					     /* Types */
     OP_TYPE_fv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_fv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_fv_ops
  = {wextu_ops,					     /* Types */
     OP_TYPE_v_fv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     sf_vc_v_fv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_vvv_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_vvv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_vvv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_vvv_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_v_vvv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     sf_vc_vvv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_xvv_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_xvv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_xvv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_xvv_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_v_xvv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     sf_vc_xvv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_ivv_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_ivv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_ivv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_ivv_ops
  = {full_v_u_ops,					     /* Types */
     OP_TYPE_v_ivv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     sf_vc_ivv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_fvv_ops
  = {wextu_ops,					     /* Types */
     OP_TYPE_fvv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_fvv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_fvv_ops
  = {wextu_ops,					     /* Types */
     OP_TYPE_v_fvv,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_vector), /* Return type */
     sf_vc_fvv_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_vvw_ops
  = {x2_u_ops,					     /* Types */
     OP_TYPE_vvw,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_vvw_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_vvw_ops
  = {x2_u_ops,					     /* Types */
     OP_TYPE_v_vvw,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_x2_vector), /* Return type */
     sf_vc_vvw_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_xvw_ops
  = {x2_u_ops,					     /* Types */
     OP_TYPE_xvw,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_xvw_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_xvw_ops
  = {x2_u_ops,					     /* Types */
     OP_TYPE_v_xvw,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_x2_vector), /* Return type */
     sf_vc_xvw_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_ivw_ops
  = {x2_u_ops,					     /* Types */
     OP_TYPE_ivw,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_ivw_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_ivw_ops
  = {x2_u_ops,					     /* Types */
     OP_TYPE_v_ivw,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_x2_vector), /* Return type */
     sf_vc_ivw_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_fvw_ops
  = {x2_wu_ops,					     /* Types */
     OP_TYPE_fvw,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_void), /* Return type */
     sf_vc_fvw_args /* Args */};

static CONSTEXPR const rvv_op_info sf_vc_v_fvw_ops
  = {x2_wu_ops,					     /* Types */
     OP_TYPE_v_fvw,					     /* Suffix */
     rvv_arg_type_info (RVV_BASE_x2_vector), /* Return type */
     sf_vc_fvw_args /* Args */};

/* A list of all RVV base function types.  */
static CONSTEXPR const function_type_info function_types[] = {
#define DEF_RVV_TYPE_INDEX(                                                    \
  VECTOR, MASK, SIGNED, UNSIGNED, SIGNED_EEW8_INDEX, EEW8_INDEX, EEW16_INDEX,  \
  EEW32_INDEX, EEW64_INDEX, SHIFT, DOUBLE_TRUNC, QUAD_TRUNC, QUAD_EMUL,        \
  QUAD_EMUL_SIGNED, QUAD_EMUL_UNSIGNED, QUAD_FIX, QUAD_FIX_SIGNED,             \
  QUAD_FIX_UNSIGNED, OCT_TRUNC, DOUBLE_TRUNC_SCALAR, DOUBLE_TRUNC_SIGNED,      \
  DOUBLE_TRUNC_UNSIGNED, DOUBLE_TRUNC_UNSIGNED_SCALAR,                         \
  DOUBLE_TRUNC_BFLOAT_SCALAR, DOUBLE_TRUNC_BFLOAT, DOUBLE_TRUNC_FLOAT, FLOAT,  \
  LMUL1, WLMUL1, QLMUL1, QLMUL1_SIGNED, QLMUL1_UNSIGNED, XFQF, EEW8_INTERPRET, \
  EEW16_INTERPRET, EEW32_INTERPRET, EEW64_INTERPRET, BOOL1_INTERPRET,          \
  BOOL2_INTERPRET, BOOL4_INTERPRET, BOOL8_INTERPRET, BOOL16_INTERPRET,         \
  BOOL32_INTERPRET, BOOL64_INTERPRET, SIGNED_EEW8_LMUL1_INTERPRET,             \
  SIGNED_EEW16_LMUL1_INTERPRET, SIGNED_EEW32_LMUL1_INTERPRET,                  \
  SIGNED_EEW64_LMUL1_INTERPRET, UNSIGNED_EEW8_LMUL1_INTERPRET,                 \
  UNSIGNED_EEW16_LMUL1_INTERPRET, UNSIGNED_EEW32_LMUL1_INTERPRET,              \
  UNSIGNED_EEW64_LMUL1_INTERPRET, X2, X2_VLMUL_EXT, X4_VLMUL_EXT, X8_VLMUL_EXT,\
  X16_VLMUL_EXT, X32_VLMUL_EXT, X64_VLMUL_EXT, TUPLE_SUBPART)                  \
  {                                                                            \
    VECTOR_TYPE_##VECTOR,                                                      \
    VECTOR_TYPE_INVALID,                                                       \
    VECTOR_TYPE_##MASK,                                                        \
    VECTOR_TYPE_##SIGNED,                                                      \
    VECTOR_TYPE_##UNSIGNED,                                                    \
    VECTOR_TYPE_INVALID,                                                       \
    VECTOR_TYPE_INVALID,                                                       \
    VECTOR_TYPE_INVALID,                                                       \
    VECTOR_TYPE_INVALID,                                                       \
    VECTOR_TYPE_INVALID,                                                       \
    VECTOR_TYPE_INVALID,                                                       \
    VECTOR_TYPE_INVALID,                                                       \
    VECTOR_TYPE_INVALID,                                                       \
    VECTOR_TYPE_INVALID,                                                       \
    VECTOR_TYPE_INVALID,                                                       \
    VECTOR_TYPE_##SIGNED_EEW8_INDEX,                                           \
    VECTOR_TYPE_##EEW8_INDEX,                                                  \
    VECTOR_TYPE_##EEW16_INDEX,                                                 \
    VECTOR_TYPE_##EEW32_INDEX,                                                 \
    VECTOR_TYPE_##EEW64_INDEX,                                                 \
    VECTOR_TYPE_##SHIFT,                                                       \
    VECTOR_TYPE_##DOUBLE_TRUNC,                                                \
    VECTOR_TYPE_##QUAD_TRUNC,                                                  \
    VECTOR_TYPE_##QUAD_EMUL,                                                   \
    VECTOR_TYPE_##QUAD_EMUL_SIGNED,                                            \
    VECTOR_TYPE_##QUAD_EMUL_UNSIGNED,                                          \
    VECTOR_TYPE_##QUAD_FIX,                                                    \
    VECTOR_TYPE_##QUAD_FIX_SIGNED,                                             \
    VECTOR_TYPE_##QUAD_FIX_UNSIGNED,                                           \
    VECTOR_TYPE_##OCT_TRUNC,                                                   \
    VECTOR_TYPE_##DOUBLE_TRUNC_SCALAR,                                         \
    VECTOR_TYPE_##DOUBLE_TRUNC_SIGNED,                                         \
    VECTOR_TYPE_##DOUBLE_TRUNC_UNSIGNED,                                       \
    VECTOR_TYPE_##DOUBLE_TRUNC_UNSIGNED_SCALAR,                                \
    VECTOR_TYPE_##DOUBLE_TRUNC_BFLOAT_SCALAR,                                  \
    VECTOR_TYPE_##DOUBLE_TRUNC_BFLOAT,                                         \
    VECTOR_TYPE_##DOUBLE_TRUNC_FLOAT,                                          \
    VECTOR_TYPE_##FLOAT,                                                       \
    VECTOR_TYPE_##LMUL1,                                                       \
    VECTOR_TYPE_##WLMUL1,                                                      \
    VECTOR_TYPE_##QLMUL1,                                                      \
    VECTOR_TYPE_##QLMUL1_SIGNED,                                               \
    VECTOR_TYPE_##QLMUL1_UNSIGNED,                                             \
    VECTOR_TYPE_##XFQF,                                                        \
    VECTOR_TYPE_##EEW8_INTERPRET,                                              \
    VECTOR_TYPE_##EEW16_INTERPRET,                                             \
    VECTOR_TYPE_##EEW32_INTERPRET,                                             \
    VECTOR_TYPE_##EEW64_INTERPRET,                                             \
    VECTOR_TYPE_##BOOL1_INTERPRET,                                             \
    VECTOR_TYPE_##BOOL2_INTERPRET,                                             \
    VECTOR_TYPE_##BOOL4_INTERPRET,                                             \
    VECTOR_TYPE_##BOOL8_INTERPRET,                                             \
    VECTOR_TYPE_##BOOL16_INTERPRET,                                            \
    VECTOR_TYPE_##BOOL32_INTERPRET,                                            \
    VECTOR_TYPE_##BOOL64_INTERPRET,                                            \
    VECTOR_TYPE_##SIGNED_EEW8_LMUL1_INTERPRET,                                 \
    VECTOR_TYPE_##SIGNED_EEW16_LMUL1_INTERPRET,                                \
    VECTOR_TYPE_##SIGNED_EEW32_LMUL1_INTERPRET,                                \
    VECTOR_TYPE_##SIGNED_EEW64_LMUL1_INTERPRET,                                \
    VECTOR_TYPE_##UNSIGNED_EEW8_LMUL1_INTERPRET,                               \
    VECTOR_TYPE_##UNSIGNED_EEW16_LMUL1_INTERPRET,                              \
    VECTOR_TYPE_##UNSIGNED_EEW32_LMUL1_INTERPRET,                              \
    VECTOR_TYPE_##UNSIGNED_EEW64_LMUL1_INTERPRET,                              \
    VECTOR_TYPE_##X2,                                                          \
    VECTOR_TYPE_##X2_VLMUL_EXT,                                                \
    VECTOR_TYPE_##X4_VLMUL_EXT,                                                \
    VECTOR_TYPE_##X8_VLMUL_EXT,                                                \
    VECTOR_TYPE_##X16_VLMUL_EXT,                                               \
    VECTOR_TYPE_##X32_VLMUL_EXT,                                               \
    VECTOR_TYPE_##X64_VLMUL_EXT,                                               \
    VECTOR_TYPE_INVALID,                                                       \
    VECTOR_TYPE_##TUPLE_SUBPART,                                               \
  },
#include "riscv-vector-builtins.def"
}; // namespace riscv_vector

/* A list of all RVV intrinsic functions.  */
static function_group_info function_groups[] = {
#define DEF_RVV_FUNCTION(NAME, SHAPE, PREDS, OPS_INFO)                         \
  {#NAME, &bases::NAME, &shapes::SHAPE, PREDS, OPS_INFO, REQUIRED_EXTENSIONS},
#include "riscv-vector-builtins-functions.def"
#undef DEF_RVV_FUNCTION
#define DEF_RVV_FUNCTION(NAME, SHAPE, PREDS, OPS_INFO)                         \
  {#NAME, &bases::NAME, &shapes::SHAPE, PREDS, OPS_INFO, REQUIRED_EXTENSIONS},
#include "thead-vector-builtins-functions.def"
#define DEF_RVV_FUNCTION(NAME, SHAPE, PREDS, OPS_INFO)                         \
  {#NAME, &bases::NAME, &shapes::SHAPE, PREDS, OPS_INFO, REQUIRED_EXTENSIONS},
#include "sifive-vector-builtins-functions.def"
#define DEF_RVV_FUNCTION(NAME, SHAPE, PREDS, OPS_INFO)                         \
  {#NAME, &bases::NAME, &shapes::SHAPE, PREDS, OPS_INFO, REQUIRED_EXTENSIONS},
#include "andes-vector-builtins-functions.def"
#define DEF_RVV_FUNCTION(NAME, SHAPE, PREDS, OPS_INFO)                         \
  {#NAME, &bases::NAME, &shapes::SHAPE, PREDS, OPS_INFO, REQUIRED_EXTENSIONS},
#include "zvmma-vector-builtins-functions.def"

};

/* The RVV types, with their built-in
   "__rvv..._t" name.  Allow an index of NUM_VECTOR_TYPES, which always
   yields a null tree.  */
static GTY (()) tree abi_vector_types[NUM_VECTOR_TYPES + 1];

/* Same, but with the riscv_vector.h "v..._t" name.  */
extern GTY (()) rvv_builtin_types_t builtin_types[NUM_VECTOR_TYPES + 1];
rvv_builtin_types_t builtin_types[NUM_VECTOR_TYPES + 1];

/* Per-partition vectors of registered functions.  */
static GTY (()) vec<registered_function *, va_gc>
  *partition_functions[NUM_RVV_EXT_PARTITIONS];

/* Return true if TYPE uses fractional LMUL (mf2, mf4, mf8).  */
static bool
is_fractional_lmul (vector_type_index type)
{
  /* Check if the type suffix contains "mf" indicating fractional LMUL.  */
  if (type >= NUM_VECTOR_TYPES)
    return false;
  const char *suffix = type_suffixes[type].vector;
  return suffix && strstr (suffix, "mf") != NULL;
}

/* Return true if BASE is an intrinsic unsupported by XTheadVector.
   According to
    https://github.com/XUANTIE-RV/thead-extension-spec/blob/master/xtheadvector/intrinsics.adoc#xtheadvector

   XTheadVector lacks support for:
   - Fractional LMUL types (checked separately via is_fractional_lmul)
   - vlm/vsm
   - vzext/vsext
   - vaaddu/vasubu
   - vfrsqrt7/vfrec7
   - vfslide1up/vfslide1down
   - vluxseg/vsuxseg
   - vluxei8/vluxei16/vluxei32/vluxei64
   - vrgatherei16.  */

static bool
xthvector_unsupported_p (const function_base *base)
{
  return base == bases::vlm
	 || base == bases::vsm
	 || base == bases::vzext
	 || base == bases::vsext
	 || base == bases::vaaddu
	 || base == bases::vasubu
	 || base == bases::vfrsqrt7
	 || base == bases::vfrec7
	 || base == bases::vfrec7_frm
	 || base == bases::vfslide1up
	 || base == bases::vfslide1down
	 || base == bases::vluxseg
	 || base == bases::vsuxseg
	 || base == bases::vluxei8
	 || base == bases::vluxei16
	 || base == bases::vluxei32
	 || base == bases::vluxei64
	 || base == bases::vrgatherei16;
}

/* Map required_ext to partition, splitting VECTOR_EXT based on instance.  */
static rvv_builtin_partition
get_builtin_partition (required_ext ext, const function_instance &instance)
{
  switch (ext)
    {
    case VECTOR_EXT:
      if (is_fractional_lmul (instance.type.index)
	  || xthvector_unsupported_p (instance.base))
	return RVV_PARTITION_VECTOR_NO_XTHEAD;
      else
	return RVV_PARTITION_VECTOR;
    case VECTOR_EXT_NO_XTHEAD:
      return RVV_PARTITION_VECTOR_NO_XTHEAD;
    case XTHEADVECTOR_EXT:
      return RVV_PARTITION_XTHEADVECTOR;
    case ZVBB_EXT:
      return RVV_PARTITION_ZVBB;
    case ZVBB_OR_ZVKB_EXT:
      return RVV_PARTITION_ZVBB_OR_ZVKB;
    case ZVBC_EXT:
      return RVV_PARTITION_ZVBC;
    case ZVKG_EXT:
      return RVV_PARTITION_ZVKG;
    case ZVKNED_EXT:
      return RVV_PARTITION_ZVKNED;
    case ZVKNHA_OR_ZVKNHB_EXT:
      return RVV_PARTITION_ZVKNHA_OR_ZVKNHB;
    case ZVKNHB_EXT:
      return RVV_PARTITION_ZVKNHB;
    case ZVKSED_EXT:
      return RVV_PARTITION_ZVKSED;
    case ZVKSH_EXT:
      return RVV_PARTITION_ZVKSH;
    case ZVFBFMIN_EXT:
      return RVV_PARTITION_ZVFBFMIN;
    case ZVFBFWMA_EXT:
      return RVV_PARTITION_ZVFBFWMA;
    case XSFVQMACCQOQ_EXT:
      return RVV_PARTITION_XSFVQMACCQOQ;
    case XSFVQMACCDOD_EXT:
      return RVV_PARTITION_XSFVQMACCDOD;
    case XSFVFNRCLIPXFQF_EXT:
      return RVV_PARTITION_XSFVFNRCLIPXFQF;
    case XSFVCP_EXT:
      return RVV_PARTITION_XSFVCP;
    case XANDESVBFHCVT_EXT:
      return RVV_PARTITION_XANDESVBFHCVT;
    case XANDESVSINTLOAD_EXT:
      return RVV_PARTITION_XANDESVSINTLOAD;
    case XANDESVPACKFPH_EXT:
      return RVV_PARTITION_XANDESVPACKFPH;
    case XANDESVDOT_EXT:
      return RVV_PARTITION_XANDESVDOT;
    case ZVVMM_EXT:
      return RVV_PARTITION_ZVVMM;
    case ZVVFMM_EXT:
      return RVV_PARTITION_ZVVFMM;
    case ZVVMTLS_EXT:
      return RVV_PARTITION_ZVVMTLS;
    case ZVVMTTLS_EXT:
      return RVV_PARTITION_ZVVMTTLS;
    case ZVVI4I8MM_EXT:
      return RVV_PARTITION_ZVVI4I8MM;
    case ZVVI4I16MM_EXT:
      return RVV_PARTITION_ZVVI4I16MM;
    case ZVVI4I32MM_EXT:
      return RVV_PARTITION_ZVVI4I32MM;
    case ZVVI8MM_EXT:
      return RVV_PARTITION_ZVVI8MM;
    case ZVVI8I16MM_EXT:
      return RVV_PARTITION_ZVVI8I16MM;
    case ZVVI8I32MM_EXT:
      return RVV_PARTITION_ZVVI8I32MM;
    case ZVVI8I64MM_EXT:
      return RVV_PARTITION_ZVVI8I64MM;
    case ZVVI16MM_EXT:
      return RVV_PARTITION_ZVVI16MM;
    case ZVVI16I32MM_EXT:
      return RVV_PARTITION_ZVVI16I32MM;
    case ZVVI16I64MM_EXT:
      return RVV_PARTITION_ZVVI16I64MM;
    case ZVVI32MM_EXT:
      return RVV_PARTITION_ZVVI32MM;
    case ZVVI32I64MM_EXT:
      return RVV_PARTITION_ZVVI32I64MM;
    case ZVVI64MM_EXT:
      return RVV_PARTITION_ZVVI64MM;
    case ZVVOFP4OFP8MM_EXT:
      return RVV_PARTITION_ZVVOFP4OFP8MM;
    case ZVVOFP4FP16MM_EXT:
      return RVV_PARTITION_ZVVOFP4FP16MM;
    case ZVVOFP4BF16MM_EXT:
      return RVV_PARTITION_ZVVOFP4BF16MM;
    case ZVVOFP4FP32MM_EXT:
      return RVV_PARTITION_ZVVOFP4FP32MM;
    case ZVVOFP8MM_EXT:
      return RVV_PARTITION_ZVVOFP8MM;
    case ZVVOFP8FP16MM_EXT:
      return RVV_PARTITION_ZVVOFP8FP16MM;
    case ZVVOFP8BF16MM_EXT:
      return RVV_PARTITION_ZVVOFP8BF16MM;
    case ZVVOFP8FP32MM_EXT:
      return RVV_PARTITION_ZVVOFP8FP32MM;
    case ZVVOFP8FP64MM_EXT:
      return RVV_PARTITION_ZVVOFP8FP64MM;
    case ZVVFP16MM_EXT:
      return RVV_PARTITION_ZVVFP16MM;
    case ZVVFP16FP32MM_EXT:
      return RVV_PARTITION_ZVVFP16FP32MM;
    case ZVVFP16FP64MM_EXT:
      return RVV_PARTITION_ZVVFP16FP64MM;
    case ZVVBF16MM_EXT:
      return RVV_PARTITION_ZVVBF16MM;
    case ZVVBF16FP32MM_EXT:
      return RVV_PARTITION_ZVVBF16FP32MM;
    case ZVVBF16FP64MM_EXT:
      return RVV_PARTITION_ZVVBF16FP64MM;
    case ZVVFP32MM_EXT:
      return RVV_PARTITION_ZVVFP32MM;
    case ZVVFP32FP64MM_EXT:
      return RVV_PARTITION_ZVVFP32FP64MM;
    case ZVVFP64MM_EXT:
      return RVV_PARTITION_ZVVFP64MM;
    case ZVVXOFP4OFP8MM_EXT:
      return RVV_PARTITION_ZVVXOFP4OFP8MM;
    case ZVVXOFP4FP16MM_EXT:
      return RVV_PARTITION_ZVVXOFP4FP16MM;
    case ZVVXOFP4BF16MM_EXT:
      return RVV_PARTITION_ZVVXOFP4BF16MM;
    case ZVVXOFP4FP32MM_EXT:
      return RVV_PARTITION_ZVVXOFP4FP32MM;
    case ZVVXOFP8MM_EXT:
      return RVV_PARTITION_ZVVXOFP8MM;
    case ZVVXOFP8FP16MM_EXT:
      return RVV_PARTITION_ZVVXOFP8FP16MM;
    case ZVVXOFP8BF16MM_EXT:
      return RVV_PARTITION_ZVVXOFP8BF16MM;
    case ZVVXOFP8FP32MM_EXT:
      return RVV_PARTITION_ZVVXOFP8FP32MM;
    case ZVVXOFP8FP64MM_EXT:
      return RVV_PARTITION_ZVVXOFP8FP64MM;
    case ZVVXI4FP16MM_EXT:
      return RVV_PARTITION_ZVVXI4FP16MM;
    case ZVVXI4BF16MM_EXT:
      return RVV_PARTITION_ZVVXI4BF16MM;
    case ZVVXI4FP32MM_EXT:
      return RVV_PARTITION_ZVVXI4FP32MM;
    case ZVVXI8FP16MM_EXT:
      return RVV_PARTITION_ZVVXI8FP16MM;
    case ZVVXI8BF16MM_EXT:
      return RVV_PARTITION_ZVVXI8BF16MM;
    case ZVVXI8FP32MM_EXT:
      return RVV_PARTITION_ZVVXI8FP32MM;
    case ZVVXI8FP64MM_EXT:
      return RVV_PARTITION_ZVVXI8FP64MM;
    case ZVVXNOFP4OFP8MM_EXT:
      return RVV_PARTITION_ZVVXNOFP4OFP8MM;
    case ZVVXNOFP4FP16MM_EXT:
      return RVV_PARTITION_ZVVXNOFP4FP16MM;
    case ZVVXNOFP4BF16MM_EXT:
      return RVV_PARTITION_ZVVXNOFP4BF16MM;
    case ZVVXNOFP4FP32MM_EXT:
      return RVV_PARTITION_ZVVXNOFP4FP32MM;
    case ZVVXNOFP8MM_EXT:
      return RVV_PARTITION_ZVVXNOFP8MM;
    case ZVVXNOFP8FP16MM_EXT:
      return RVV_PARTITION_ZVVXNOFP8FP16MM;
    case ZVVXNOFP8BF16MM_EXT:
      return RVV_PARTITION_ZVVXNOFP8BF16MM;
    case ZVVXNOFP8FP32MM_EXT:
      return RVV_PARTITION_ZVVXNOFP8FP32MM;
    case ZVVXNOFP8FP64MM_EXT:
      return RVV_PARTITION_ZVVXNOFP8FP64MM;
    case ZVVXNI4FP16MM_EXT:
      return RVV_PARTITION_ZVVXNI4FP16MM;
    case ZVVXNI4BF16MM_EXT:
      return RVV_PARTITION_ZVVXNI4BF16MM;
    case ZVVXNI4FP32MM_EXT:
      return RVV_PARTITION_ZVVXNI4FP32MM;
    case ZVVXNI8FP16MM_EXT:
      return RVV_PARTITION_ZVVXNI8FP16MM;
    case ZVVXNI8BF16MM_EXT:
      return RVV_PARTITION_ZVVXNI8BF16MM;
    case ZVVXNI8FP32MM_EXT:
      return RVV_PARTITION_ZVVXNI8FP32MM;
    case ZVVXNI8FP64MM_EXT:
      return RVV_PARTITION_ZVVXNI8FP64MM;
    default:
      gcc_unreachable ();
    }
}

/* All registered function decls, hashed on the function_instance
   that they implement.  This is used for looking up implementations of
   overloaded functions.  */
static hash_table<registered_function_hasher> *function_table;

/* All registered function decls, hashed on overload_name and argument list
   of the registered_function.  This is used for looking up implementations
   of non-overloaded functions. */
static hash_table<non_overloaded_registered_function_hasher>
  *non_overloaded_function_table;

struct pragma_intrinsic_flags
{
  int intrinsic_riscv_isa_flags;
  int intrinsic_riscv_base_subext;

  int intrinsic_riscv_vector_elen_flags;
  int intrinsic_riscv_zvl_subext;
  int intrinsic_riscv_zvf_subext;
};

static void
riscv_pragma_intrinsic_flags_pollute (struct pragma_intrinsic_flags *flags)
{
  /* We already defer the required extension checking to expantion time, so we
     only need to pollute those flags that might affect the type registration.

     e.g. zvfbmin and zvfhmin are required to define the vector bf16 and f16,
	  and VECTOR_ELEN* also required for vector interger and floating
	  type.  */
  flags->intrinsic_riscv_isa_flags = riscv_isa_flags;
  flags->intrinsic_riscv_base_subext = riscv_base_subext;
  flags->intrinsic_riscv_vector_elen_flags = riscv_vector_elen_flags;
  flags->intrinsic_riscv_zvl_subext = riscv_zvl_subext;
  flags->intrinsic_riscv_zvf_subext = riscv_zvf_subext;

  riscv_zvf_subext = riscv_zvf_subext
    | MASK_ZVFBFMIN
    | MASK_ZVFHMIN;

  riscv_isa_flags = riscv_isa_flags
    | MASK_VECTOR;

  riscv_base_subext = riscv_base_subext
    | MASK_MUL;

  riscv_zvl_subext = riscv_zvl_subext
    | MASK_ZVL32B
    | MASK_ZVL64B
    | MASK_ZVL128B
    | MASK_ZVL256B
    | MASK_ZVL512B
    | MASK_ZVL1024B
    | MASK_ZVL2048B
    | MASK_ZVL4096B;

  riscv_vector_elen_flags = riscv_vector_elen_flags
    | MASK_VECTOR_ELEN_32
    | MASK_VECTOR_ELEN_64
    | MASK_VECTOR_ELEN_FP_16
    | MASK_VECTOR_ELEN_FP_32
    | MASK_VECTOR_ELEN_FP_64
    | MASK_VECTOR_ELEN_BF_16;
}

static void
riscv_pragma_intrinsic_flags_restore (struct pragma_intrinsic_flags *flags)
{
  riscv_isa_flags = flags->intrinsic_riscv_isa_flags;
  riscv_base_subext = flags->intrinsic_riscv_base_subext;

  riscv_vector_elen_flags = flags->intrinsic_riscv_vector_elen_flags;
  riscv_zvl_subext = flags->intrinsic_riscv_zvl_subext;
  riscv_zvf_subext = flags->intrinsic_riscv_zvf_subext;
}

/* RAII class for enabling enough RVV features to define the built-in
   types and implement the riscv_vector.h pragma.

   Note: According to 'TYPE_MODE' macro implementation, we need set
   have_regs_of_mode[mode] to be true if we want to get the exact mode
   from 'TYPE_MODE'. However, have_regs_of_mode has not been set yet in
   targetm.init_builtins (). We need rvv_switcher to set have_regs_of_mode
   before targetm.init_builtins () and recover back have_regs_of_mode
   after targetm.init_builtins ().  */
class rvv_switcher
{
public:
  rvv_switcher (bool pollute_flags = true);
  ~rvv_switcher ();

private:
  bool m_old_have_regs_of_mode[MAX_MACHINE_MODE];
  struct pragma_intrinsic_flags backup_flags;
  bool m_pollute_flags;
};

static void
register_builtin_types_on_null ();

rvv_switcher::rvv_switcher (bool pollute_flags)
  : m_pollute_flags (pollute_flags)
{
  if (m_pollute_flags)
    {
      riscv_pragma_intrinsic_flags_pollute (&backup_flags);
      riscv_option_override ();
    }

  /* Allow all vector modes during builtin registration so that
     vector_mode_supported_p does not reject fractional LMUL modes
     for xtheadvector.  */
  riscv_registering_builtins = true;

  /* Set have_regs_of_mode before targetm.init_builtins ().  */
  memcpy (m_old_have_regs_of_mode, have_regs_of_mode,
	  sizeof (have_regs_of_mode));
  for (int i = 0; i < NUM_MACHINE_MODES; ++i)
    if (riscv_vla_mode_p ((machine_mode) i))
      have_regs_of_mode[i] = true;

  /* Not necessary to adjust mode and register type if we don't pollute
     flags.  */
  if (m_pollute_flags)
    {
      init_adjust_machine_modes ();

      register_builtin_types_on_null ();
    }
}

rvv_switcher::~rvv_switcher ()
{
  riscv_registering_builtins = false;

  /* Recover back have_regs_of_mode.  */
  memcpy (have_regs_of_mode, m_old_have_regs_of_mode,
	  sizeof (have_regs_of_mode));

  if (m_pollute_flags)
    {
      riscv_pragma_intrinsic_flags_restore (&backup_flags);

      /* Re-initialize after the flags are restored.  */
      riscv_option_override ();
      init_adjust_machine_modes ();
    }
}

/* Add attribute NAME to ATTRS.  */
static tree
add_attribute (const char *name, tree attrs)
{
  return tree_cons (get_identifier (name), NULL_TREE, attrs);
}

/* Add type attributes to builtin type tree, currently only the mangled name. */
static void
add_vector_type_attribute (tree type, const char *mangled_name)
{
  tree mangled_name_tree = get_identifier (mangled_name);
  tree value = tree_cons (NULL_TREE, mangled_name_tree, NULL_TREE);
  TYPE_ATTRIBUTES (type)
    = tree_cons (get_identifier ("RVV type"), value, TYPE_ATTRIBUTES (type));
}

/* Force TYPE to be a sizeless type.  */
static void
make_type_sizeless (tree type)
{
  TYPE_ATTRIBUTES (type) = tree_cons (get_identifier ("RVV sizeless type"),
				      NULL_TREE, TYPE_ATTRIBUTES (type));
}

/* Return true if TYPE is a sizeless type.  */
static bool
sizeless_type_p (const_tree type)
{
  if (type == error_mark_node)
    return NULL_TREE;
  return lookup_attribute ("RVV sizeless type", TYPE_ATTRIBUTES (type));
}

/* If TYPE is an ABI-defined RVV type, return its attribute descriptor,
   otherwise return null.  */
tree
lookup_vector_type_attribute (const_tree type)
{
  if (type == error_mark_node)
    return NULL_TREE;
  return lookup_attribute ("RVV type", TYPE_ATTRIBUTES (type));
}

/* Return a representation of "const T *".  */
static tree
build_const_pointer (tree t)
{
  return build_pointer_type (build_qualified_type (t, TYPE_QUAL_CONST));
}

/* Helper function for register a single built-in RVV ABI type.  */
static void
register_builtin_type (vector_type_index type, tree eltype, machine_mode mode)
{
  builtin_types[type].scalar = eltype;
  builtin_types[type].scalar_ptr = build_pointer_type (eltype);
  builtin_types[type].scalar_const_ptr = build_const_pointer (eltype);
  /* TODO: We currently just skip the register of the illegal RVV type.
     Ideally, we should report error message more friendly instead of
     reporting "unknown" type. Support more friendly error message in
     the future.  */
  if (!riscv_vla_mode_p (mode))
    return;

  tree vectype = build_vector_type_for_mode (eltype, mode);
  /* If MODE's underlying element type is disabled by the current target
     options (e.g. an HF or BF mode under -march without Zvfh / Zvfbfmin),
     `build_vector_type_for_mode' returns a vector type whose backing
     mode bitsize is 0.  Such a type would fail the size-consistency
     assert below.  This can be observed when target options change
     between init and use (for example, multiple `register_builtin_type'
     calls with different effective `TARGET_VECTOR_ELEN_FP_16' values),
     leaving stale modes whose size machinery is not re-finalised.
     Skip the registration in that case -- the type is unusable anyway,
     and downstream callers test `abi_vector_types[type] == NULL_TREE'
     to disable the type at the C level.  */
  if (vectype == NULL_TREE || known_eq (GET_MODE_BITSIZE (mode), 0u))
    return;
  gcc_assert (VECTOR_MODE_P (TYPE_MODE (vectype)) && TYPE_MODE (vectype) == mode
	      && TYPE_MODE_RAW (vectype) == mode && TYPE_ALIGN (vectype) <= 128
	      && known_eq (tree_to_poly_uint64 (TYPE_SIZE (vectype)),
			   GET_MODE_BITSIZE (mode)));
  vectype = build_distinct_type_copy (vectype);
  gcc_assert (vectype == TYPE_MAIN_VARIANT (vectype));
  SET_TYPE_STRUCTURAL_EQUALITY (vectype);
  TYPE_ARTIFICIAL (vectype) = 1;
  TYPE_INDIVISIBLE_P (vectype) = 1;
  add_vector_type_attribute (vectype, vector_types[type].mangled_name);
  make_type_sizeless (vectype);
  abi_vector_types[type] = vectype;
  lang_hooks.types.register_builtin_type (vectype, vector_types[type].abi_name);
}

/* Register the tuple type that contains NUM_VECTORS vectors of type TYPE.  */
static void
register_tuple_type (vector_type_index type, vector_type_index subpart_type,
		     tree eltype, unsigned int nf)
{
  /* TODO: We currently just skip the register of the illegal RVV type.
    Ideally, we should report error message more friendly instead of
    reporting "unknown" type. Support more friendly error message in
    the future.  */
  if (!abi_vector_types[subpart_type])
    return;
  tree tuple_type = lang_hooks.types.make_type (RECORD_TYPE);

  /* The contents of the type are opaque, so we can define them in any
     way that maps to the correct ABI type.

     Here we choose to use the same layout as for riscv_vector.h, with
     "__val":

	struct vfooxN_t { vfoo_t __val[N]; };

     (It wouldn't be possible to write that directly in C or C++ for
     sizeless types, but that's not a problem for this function.)

     Using arrays simplifies the handling of vget and vset for variable
     arguments.  */
  tree array_type = build_array_type_nelts (abi_vector_types[subpart_type], nf);
  gcc_assert (array_type);
  gcc_assert (VECTOR_MODE_P (TYPE_MODE (array_type))
	      && TYPE_MODE_RAW (array_type) == TYPE_MODE (array_type));

  tree field = build_decl (input_location, FIELD_DECL, get_identifier ("__val"),
			   array_type);
  DECL_FIELD_CONTEXT (field) = tuple_type;
  TYPE_FIELDS (tuple_type) = field;
  add_vector_type_attribute (tuple_type, vector_types[type].mangled_name);
  make_type_sizeless (tuple_type);
  layout_type (tuple_type);
  gcc_assert (VECTOR_MODE_P (TYPE_MODE (tuple_type))
	      && TYPE_MODE_RAW (tuple_type) == TYPE_MODE (tuple_type));

  tree decl
    = build_decl (input_location, TYPE_DECL,
		  get_identifier (vector_types[type].abi_name), tuple_type);
  TYPE_NAME (tuple_type) = decl;
  TYPE_STUB_DECL (tuple_type) = decl;
  lang_hooks.decls.pushdecl (decl);
  /* ??? Undo the effect of set_underlying_type for C.  The C frontend
     doesn't recognize DECL as a built-in because (as intended) the decl has
     a real location instead of BUILTINS_LOCATION.  The frontend therefore
     treats the decl like a normal C "typedef struct foo foo;", expecting
     the type for tag "struct foo" to have a dummy unnamed TYPE_DECL instead
     of the named one we attached above.  It then sets DECL_ORIGINAL_TYPE
     on the supposedly unnamed decl, creating a circularity that upsets
     dwarf2out.

     We don't want to follow the normal C model and create "struct foo"
     tags for tuple types since (a) the types are supposed to be opaque
     and (b) they couldn't be defined as a real struct anyway.  Treating
     the TYPE_DECLs as "typedef struct foo foo;" without creating
     "struct foo" would lead to confusing error messages.  */
  DECL_ORIGINAL_TYPE (decl) = NULL_TREE;

  builtin_types[type].scalar = eltype;
  builtin_types[type].scalar_ptr = build_pointer_type (eltype);
  builtin_types[type].scalar_const_ptr = build_const_pointer (eltype);
  abi_vector_types[type] = tuple_type;
}

/* Register the built-in RVV ABI types, such as __rvv_int32m1_t.  */
static void
register_builtin_types ()
{
  /* Don't pollute flags at this stage to make sure we only register type with
     what we want so far, we will register all type if necessary later.   */
  rvv_switcher rvv (/* pollute_flags */ false);
  register_builtin_types_on_null ();
}

/* Similar as register_builtin_types but perform the registration if and
   only if the element of abi_vector_type is NULL_TREE.  */
static void
register_builtin_types_on_null ()
{
  /* Get type node from get_typenode_from_name to prevent we have different type
     node define in different target libraries, e.g. int32_t defined as
     `long` in RV32/newlib-stdint, but `int` for RV32/glibc-stdint.h.
     NOTE: uint[16|32|64]_type_node already defined in tree.h.  */
  tree int8_type_node = get_typenode_from_name (INT8_TYPE);
  tree uint8_type_node = get_typenode_from_name (UINT8_TYPE);
  tree int16_type_node = get_typenode_from_name (INT16_TYPE);
  tree int32_type_node = get_typenode_from_name (INT32_TYPE);
  tree int64_type_node = get_typenode_from_name (INT64_TYPE);

  machine_mode mode;
#define DEF_RVV_TYPE(NAME, NCHARS, ABI_NAME, SCALAR_TYPE, VECTOR_MODE,         \
		     ARGS...)                                                  \
  mode = VECTOR_MODE##mode;                                                    \
  if (abi_vector_types[VECTOR_TYPE_##NAME] == NULL_TREE)                       \
    register_builtin_type (VECTOR_TYPE_##NAME, SCALAR_TYPE##_type_node, mode);

#define DEF_RVV_TUPLE_TYPE(NAME, NCHARS, ABI_NAME, SUBPART_TYPE, SCALAR_TYPE,  \
			   NF, VECTOR_SUFFIX)                                  \
  if (abi_vector_types[VECTOR_TYPE_##NAME] == NULL_TREE)                       \
    register_tuple_type (VECTOR_TYPE_##NAME, VECTOR_TYPE_##SUBPART_TYPE,       \
			 SCALAR_TYPE##_type_node, NF);
#include "riscv-vector-builtins.def"
}

/* Register vector type TYPE under its riscv_vector.h name.  */
static void
register_vector_type (vector_type_index type)
{
  tree vectype = abi_vector_types[type];

  /* When vectype is NULL, the corresponding builtin type
     is disabled according to '-march'.  */
  /* TODO: We currently just skip the register of the illegal RVV type.
     Ideally, we should report error message more friendly instead of
     reporting "unknown" type. Support more friendly error message in
     the future.  */
  if (!vectype)
    return;
  tree id = get_identifier (vector_types[type].name);
  tree decl = build_decl (input_location, TYPE_DECL, id, vectype);
  decl = lang_hooks.decls.pushdecl (decl);

  /* Record the new RVV type if pushdecl succeeded without error.  Use
     the ABI type otherwise, so that the type we record at least has the
     right form, even if it doesn't have the right name.  This should give
     better error recovery behavior than installing error_mark_node or
     installing an incorrect type.  */
  if (decl && TREE_CODE (decl) == TYPE_DECL
      && TREE_TYPE (decl) != error_mark_node
      && TYPE_MAIN_VARIANT (TREE_TYPE (decl)) == vectype)
    vectype = TREE_TYPE (decl);

  builtin_types[type].vector = vectype;
  builtin_types[type].vector_ptr = build_pointer_type (vectype);
}

/* Return true if the type has required_extensions.  */
static bool
required_extensions_p (enum rvv_base_type type)
{
  switch (type)
    {
      case RVV_BASE_eew8_index:
      case RVV_BASE_eew16_index:
      case RVV_BASE_eew32_index:
      case RVV_BASE_eew64_index:
      case RVV_BASE_float_vector:
      case RVV_BASE_double_trunc_float_vector:
      case RVV_BASE_double_trunc_vector:
      case RVV_BASE_widen_lmul1_vector:
      case RVV_BASE_eew8_interpret:
      case RVV_BASE_eew16_interpret:
      case RVV_BASE_eew32_interpret:
      case RVV_BASE_eew64_interpret:
      case RVV_BASE_bool1_interpret:
      case RVV_BASE_bool2_interpret:
      case RVV_BASE_bool4_interpret:
      case RVV_BASE_bool8_interpret:
      case RVV_BASE_bool16_interpret:
      case RVV_BASE_bool32_interpret:
      case RVV_BASE_bool64_interpret:
      case RVV_BASE_signed_eew8_lmul1_interpret:
      case RVV_BASE_signed_eew16_lmul1_interpret:
      case RVV_BASE_signed_eew32_lmul1_interpret:
      case RVV_BASE_signed_eew64_lmul1_interpret:
      case RVV_BASE_unsigned_eew8_lmul1_interpret:
      case RVV_BASE_unsigned_eew16_lmul1_interpret:
      case RVV_BASE_unsigned_eew32_lmul1_interpret:
      case RVV_BASE_unsigned_eew64_lmul1_interpret:
      case RVV_BASE_vlmul_ext_x2:
      case RVV_BASE_vlmul_ext_x4:
      case RVV_BASE_vlmul_ext_x8:
      case RVV_BASE_vlmul_ext_x16:
      case RVV_BASE_vlmul_ext_x32:
      case RVV_BASE_vlmul_ext_x64:
	return true;
      default:
	return false;
    }

  gcc_unreachable ();
}

/* Check whether all the RVV_REQUIRE_* values in REQUIRED_EXTENSIONS are
   enabled.
   TODO: We defer the required extensions to expantion time, this function is
	 only doing the legality now, and we may rename this function and moving
	 to another layer.  */
static bool
check_required_extensions (const function_instance &instance)
{
  rvv_type_info type_info = instance.type;
  const rvv_op_info *op_info = instance.op_info;

  if (required_extensions_p (op_info->ret.base_type))
    {
      enum vector_type_index ret_type_idx
	= op_info->ret.get_function_type_index (type_info.index);
      if (ret_type_idx == NUM_VECTOR_TYPES)
	return false;
    }

  for (unsigned i = 0; op_info->args[i].base_type != NUM_BASE_TYPES; ++i)
    {
      if (!required_extensions_p (op_info->args[i].base_type))
	continue;
      enum vector_type_index vector_type
	= op_info->args[i].get_function_type_index (type_info.index);
      if (vector_type == NUM_VECTOR_TYPES)
	return false;
    }

  return true;
}

/* Return true if predication is using a real mask operand.  */
static bool
use_real_mask_p (enum predication_type_index pred)
{
  return pred == PRED_TYPE_m || pred == PRED_TYPE_tum || pred == PRED_TYPE_tumu
	 || pred == PRED_TYPE_mu;
}

/* Return true if predication is using a real merge operand.  */
static bool
use_real_merge_p (enum predication_type_index pred)
{
  return pred == PRED_TYPE_tu || pred == PRED_TYPE_tum || pred == PRED_TYPE_tumu
	 || pred == PRED_TYPE_mu;
}

/* Get TAIL policy for predication. If predication indicates TU, return the TU.
   Otherwise, return the prefer default configuration.  */
static rtx
get_tail_policy_for_pred (enum predication_type_index pred)
{
  if (pred == PRED_TYPE_tu || pred == PRED_TYPE_tum || pred == PRED_TYPE_tumu)
    return gen_int_mode (TAIL_UNDISTURBED, Pmode);
  return gen_int_mode (get_prefer_tail_policy (), Pmode);
}

/* Get MASK policy for predication. If predication indicates MU, return the MU.
   Otherwise, return the prefer default configuration.  */
static rtx
get_mask_policy_for_pred (enum predication_type_index pred)
{
  if (pred == PRED_TYPE_tumu || pred == PRED_TYPE_mu)
    return gen_int_mode (MASK_UNDISTURBED, Pmode);
  return gen_int_mode (get_prefer_mask_policy (), Pmode);
}

tree
rvv_arg_type_info::get_scalar_ptr_type (vector_type_index type_idx) const
{
  /* According to the latest rvv-intrinsic-doc, it defines vsm.v intrinsic:
   __riscv_vsm (uint8_t *base, vbool1_t value, size_t vl).  */
  if (type_idx >= VECTOR_TYPE_vbool64_t && type_idx <= VECTOR_TYPE_vbool1_t)
    return builtin_types[VECTOR_TYPE_vuint8mf8_t].scalar_ptr;
  else
    return builtin_types[type_idx].scalar_ptr;
}

tree
rvv_arg_type_info::get_scalar_const_ptr_type (vector_type_index type_idx) const
{
  /* According to the latest rvv-intrinsic-doc, it defines vlm.v intrinsic:
   __riscv_vlm_v_b1 (const uint8_t *base, size_t vl).  */
  if (type_idx >= VECTOR_TYPE_vbool64_t && type_idx <= VECTOR_TYPE_vbool1_t)
    return builtin_types[VECTOR_TYPE_vuint8mf8_t].scalar_const_ptr;
  else
    return builtin_types[type_idx].scalar_const_ptr;
}

tree
rvv_arg_type_info::get_xfqf_float_type (vector_type_index type_idx) const
{
  /* Convert vint8 types into float types.
     Note:
     - According to riscv-vector-builtins-types.def, the index of an unsigned
       type is always one greater than its corresponding signed type.  */
  if (type_idx >= VECTOR_TYPE_vint8mf8_t && type_idx <= VECTOR_TYPE_vuint8m2_t)
    return builtin_types[VECTOR_TYPE_vfloat32m1_t].scalar;
  else
    return NULL_TREE;
}

tree
rvv_arg_type_info::get_scalar_float_type (vector_type_index type_idx) const
{
  /* Convert vint types to their corresponding scalar float types.
     Note:
     - According to riscv-vector-builtins-types.def, the index of an unsigned
       type is always one greater than its corresponding signed type.
     - Conversion for vint8 types is not required.  */
  if (type_idx >= VECTOR_TYPE_vint16mf4_t && type_idx <= VECTOR_TYPE_vuint16m8_t)
    return builtin_types[VECTOR_TYPE_vfloat16m1_t].scalar;
  else if (type_idx >= VECTOR_TYPE_vint32mf2_t && type_idx <= VECTOR_TYPE_vuint32m8_t)
    return builtin_types[VECTOR_TYPE_vfloat32m1_t].scalar;
  else if (type_idx >= VECTOR_TYPE_vint64m1_t && type_idx <= VECTOR_TYPE_vuint64m8_t)
    return builtin_types[VECTOR_TYPE_vfloat64m1_t].scalar;
  else
    return NULL_TREE;
}

/* Forward declaration: defined below.  Used here to perform the LMUL=N
   (N in {2, 4, 8}) normalisation step when computing widening input
   types for the _lm{N} source-LMUL variants.  */
static vector_type_index
ime_lmul_input_idx (vector_type_index type_idx, rvv_base_type kind);

/* Compute the vector_type_index for an IME widening input type, i.e.
   a type with SEW/W of the accumulator's SEW (W = 2, 4 or 8) and
   LMUL in {1, 2, 4, 8}.  We obtain it by chaining existing base-type
   lookups: a trunc step (DOUBLE/QUAD/OCT, same SEW/LMUL ratio) followed
   by an LMUL normalisation step (LMUL=1 via the lmul1_vector slot, or
   LMUL={2,4,8} via the ime_lmul_input_idx helper).  The SIGNED / UNSIGNED
   variants first canonicalise the input signedness via the SIGNED /
   UNSIGNED base types.  Returns NUM_VECTOR_TYPES if any step of the
   chain fails.  */

static vector_type_index
ime_widening_input_idx (vector_type_index type_idx, rvv_base_type kind)
{
  if (type_idx == VECTOR_TYPE_INVALID)
    return NUM_VECTOR_TYPES;

  /* Decode the (trunc-kind, sign-kind, lmul-kind) tuple from KIND.
     LMUL_KIND is NUM_BASE_TYPES for the default (LMUL=1) form, in which
     case the LMUL normalisation goes through the lmul1_vector slot
     directly; otherwise it specifies the lmulN_{,signed_,unsigned_}
     vector base type used to look up the LMUL=N sibling at the
     truncated SEW.  */
  rvv_base_type trunc_kind;
  rvv_base_type sign_kind = NUM_BASE_TYPES;
  rvv_base_type lmul_kind = NUM_BASE_TYPES;
  switch (kind)
    {
    case RVV_BASE_ime_double_trunc_lmul1_vector:
      trunc_kind = RVV_BASE_double_trunc_vector;
      break;
    case RVV_BASE_ime_double_trunc_lmul1_signed_vector:
      trunc_kind = RVV_BASE_double_trunc_vector;
      sign_kind = RVV_BASE_signed_vector;
      break;
    case RVV_BASE_ime_double_trunc_lmul1_unsigned_vector:
      trunc_kind = RVV_BASE_double_trunc_vector;
      sign_kind = RVV_BASE_unsigned_vector;
      break;
    case RVV_BASE_ime_quad_trunc_lmul1_vector:
      trunc_kind = RVV_BASE_quad_trunc_vector;
      break;
    case RVV_BASE_ime_quad_trunc_lmul1_signed_vector:
      trunc_kind = RVV_BASE_quad_trunc_vector;
      sign_kind = RVV_BASE_signed_vector;
      break;
    case RVV_BASE_ime_quad_trunc_lmul1_unsigned_vector:
      trunc_kind = RVV_BASE_quad_trunc_vector;
      sign_kind = RVV_BASE_unsigned_vector;
      break;
    case RVV_BASE_ime_oct_trunc_lmul1_vector:
      trunc_kind = RVV_BASE_oct_trunc_vector;
      break;
    case RVV_BASE_ime_oct_trunc_lmul1_signed_vector:
      trunc_kind = RVV_BASE_oct_trunc_vector;
      sign_kind = RVV_BASE_signed_vector;
      break;
    case RVV_BASE_ime_oct_trunc_lmul1_unsigned_vector:
      trunc_kind = RVV_BASE_oct_trunc_vector;
      sign_kind = RVV_BASE_unsigned_vector;
      break;
    /* LMUL=2 widening sources.  */
    case RVV_BASE_ime_double_trunc_lmul2_vector:
      trunc_kind = RVV_BASE_double_trunc_vector;
      lmul_kind = RVV_BASE_lmul2_vector;
      break;
    case RVV_BASE_ime_double_trunc_lmul2_signed_vector:
      trunc_kind = RVV_BASE_double_trunc_vector;
      sign_kind = RVV_BASE_signed_vector;
      lmul_kind = RVV_BASE_lmul2_signed_vector;
      break;
    case RVV_BASE_ime_double_trunc_lmul2_unsigned_vector:
      trunc_kind = RVV_BASE_double_trunc_vector;
      sign_kind = RVV_BASE_unsigned_vector;
      lmul_kind = RVV_BASE_lmul2_unsigned_vector;
      break;
    case RVV_BASE_ime_quad_trunc_lmul2_vector:
      trunc_kind = RVV_BASE_quad_trunc_vector;
      lmul_kind = RVV_BASE_lmul2_vector;
      break;
    case RVV_BASE_ime_quad_trunc_lmul2_signed_vector:
      trunc_kind = RVV_BASE_quad_trunc_vector;
      sign_kind = RVV_BASE_signed_vector;
      lmul_kind = RVV_BASE_lmul2_signed_vector;
      break;
    case RVV_BASE_ime_quad_trunc_lmul2_unsigned_vector:
      trunc_kind = RVV_BASE_quad_trunc_vector;
      sign_kind = RVV_BASE_unsigned_vector;
      lmul_kind = RVV_BASE_lmul2_unsigned_vector;
      break;
    case RVV_BASE_ime_oct_trunc_lmul2_vector:
      trunc_kind = RVV_BASE_oct_trunc_vector;
      lmul_kind = RVV_BASE_lmul2_vector;
      break;
    case RVV_BASE_ime_oct_trunc_lmul2_signed_vector:
      trunc_kind = RVV_BASE_oct_trunc_vector;
      sign_kind = RVV_BASE_signed_vector;
      lmul_kind = RVV_BASE_lmul2_signed_vector;
      break;
    case RVV_BASE_ime_oct_trunc_lmul2_unsigned_vector:
      trunc_kind = RVV_BASE_oct_trunc_vector;
      sign_kind = RVV_BASE_unsigned_vector;
      lmul_kind = RVV_BASE_lmul2_unsigned_vector;
      break;
    /* LMUL=4 widening sources.  */
    case RVV_BASE_ime_double_trunc_lmul4_vector:
      trunc_kind = RVV_BASE_double_trunc_vector;
      lmul_kind = RVV_BASE_lmul4_vector;
      break;
    case RVV_BASE_ime_double_trunc_lmul4_signed_vector:
      trunc_kind = RVV_BASE_double_trunc_vector;
      sign_kind = RVV_BASE_signed_vector;
      lmul_kind = RVV_BASE_lmul4_signed_vector;
      break;
    case RVV_BASE_ime_double_trunc_lmul4_unsigned_vector:
      trunc_kind = RVV_BASE_double_trunc_vector;
      sign_kind = RVV_BASE_unsigned_vector;
      lmul_kind = RVV_BASE_lmul4_unsigned_vector;
      break;
    case RVV_BASE_ime_quad_trunc_lmul4_vector:
      trunc_kind = RVV_BASE_quad_trunc_vector;
      lmul_kind = RVV_BASE_lmul4_vector;
      break;
    case RVV_BASE_ime_quad_trunc_lmul4_signed_vector:
      trunc_kind = RVV_BASE_quad_trunc_vector;
      sign_kind = RVV_BASE_signed_vector;
      lmul_kind = RVV_BASE_lmul4_signed_vector;
      break;
    case RVV_BASE_ime_quad_trunc_lmul4_unsigned_vector:
      trunc_kind = RVV_BASE_quad_trunc_vector;
      sign_kind = RVV_BASE_unsigned_vector;
      lmul_kind = RVV_BASE_lmul4_unsigned_vector;
      break;
    case RVV_BASE_ime_oct_trunc_lmul4_vector:
      trunc_kind = RVV_BASE_oct_trunc_vector;
      lmul_kind = RVV_BASE_lmul4_vector;
      break;
    case RVV_BASE_ime_oct_trunc_lmul4_signed_vector:
      trunc_kind = RVV_BASE_oct_trunc_vector;
      sign_kind = RVV_BASE_signed_vector;
      lmul_kind = RVV_BASE_lmul4_signed_vector;
      break;
    case RVV_BASE_ime_oct_trunc_lmul4_unsigned_vector:
      trunc_kind = RVV_BASE_oct_trunc_vector;
      sign_kind = RVV_BASE_unsigned_vector;
      lmul_kind = RVV_BASE_lmul4_unsigned_vector;
      break;
    /* LMUL=8 widening sources.  */
    case RVV_BASE_ime_double_trunc_lmul8_vector:
      trunc_kind = RVV_BASE_double_trunc_vector;
      lmul_kind = RVV_BASE_lmul8_vector;
      break;
    case RVV_BASE_ime_double_trunc_lmul8_signed_vector:
      trunc_kind = RVV_BASE_double_trunc_vector;
      sign_kind = RVV_BASE_signed_vector;
      lmul_kind = RVV_BASE_lmul8_signed_vector;
      break;
    case RVV_BASE_ime_double_trunc_lmul8_unsigned_vector:
      trunc_kind = RVV_BASE_double_trunc_vector;
      sign_kind = RVV_BASE_unsigned_vector;
      lmul_kind = RVV_BASE_lmul8_unsigned_vector;
      break;
    case RVV_BASE_ime_quad_trunc_lmul8_vector:
      trunc_kind = RVV_BASE_quad_trunc_vector;
      lmul_kind = RVV_BASE_lmul8_vector;
      break;
    case RVV_BASE_ime_quad_trunc_lmul8_signed_vector:
      trunc_kind = RVV_BASE_quad_trunc_vector;
      sign_kind = RVV_BASE_signed_vector;
      lmul_kind = RVV_BASE_lmul8_signed_vector;
      break;
    case RVV_BASE_ime_quad_trunc_lmul8_unsigned_vector:
      trunc_kind = RVV_BASE_quad_trunc_vector;
      sign_kind = RVV_BASE_unsigned_vector;
      lmul_kind = RVV_BASE_lmul8_unsigned_vector;
      break;
    case RVV_BASE_ime_oct_trunc_lmul8_vector:
      trunc_kind = RVV_BASE_oct_trunc_vector;
      lmul_kind = RVV_BASE_lmul8_vector;
      break;
    case RVV_BASE_ime_oct_trunc_lmul8_signed_vector:
      trunc_kind = RVV_BASE_oct_trunc_vector;
      sign_kind = RVV_BASE_signed_vector;
      lmul_kind = RVV_BASE_lmul8_signed_vector;
      break;
    case RVV_BASE_ime_oct_trunc_lmul8_unsigned_vector:
      trunc_kind = RVV_BASE_oct_trunc_vector;
      sign_kind = RVV_BASE_unsigned_vector;
      lmul_kind = RVV_BASE_lmul8_unsigned_vector;
      break;
    default:
      return NUM_VECTOR_TYPES;
    }

  /* Step 1: canonicalise sign if this kind asks for a specific one.  */
  vector_type_index canonical = type_idx;
  if (sign_kind != NUM_BASE_TYPES)
    canonical = function_types[type_idx].type_indexes[sign_kind];
  if (canonical == VECTOR_TYPE_INVALID)
    return NUM_VECTOR_TYPES;

  /* Step 2: narrow the SEW by the widening factor W.  */
  vector_type_index trunc
    = function_types[canonical].type_indexes[trunc_kind];
  if (trunc == VECTOR_TYPE_INVALID)
    return NUM_VECTOR_TYPES;

  /* Step 3: normalise to LMUL=1 first (the trunc step may have produced
     a fractional-LMUL type, e.g. quad_trunc of vint32m2_t yields
     vint8mf2_t; we need to canonicalise to vint8m1_t before any further
     LMUL adjustment).  */
  vector_type_index lmul1
    = function_types[trunc].type_indexes[RVV_BASE_lmul1_vector];
  if (lmul1 == VECTOR_TYPE_INVALID)
    return NUM_VECTOR_TYPES;

  /* Step 4: for LMUL in {2, 4, 8} variants, walk the LMUL=1 row up to
     LMUL=N via ime_lmul_input_idx.  For the default LMUL=1 form, the
     result is already at LMUL=1.  */
  vector_type_index result = lmul1;
  if (lmul_kind != NUM_BASE_TYPES)
    {
      result = ime_lmul_input_idx (lmul1, lmul_kind);
      if (result == NUM_VECTOR_TYPES)
	return NUM_VECTOR_TYPES;
    }
  if (result == VECTOR_TYPE_INVALID)
    return NUM_VECTOR_TYPES;

  return result;
}

/* Helper used from DEF_RVV_BASE_TYPE expansions in get_tree_type() for
   the IME widening-input base types.  Returns the tree for the computed
   vector type, or NULL_TREE if the chain cannot be resolved or the
   target type is not registered for the current -march.  */

static tree
get_ime_widening_input_type (vector_type_index type_idx, rvv_base_type kind)
{
  vector_type_index idx = ime_widening_input_idx (type_idx, kind);
  if (idx == NUM_VECTOR_TYPES)
    return NULL_TREE;
  return builtin_types[idx].vector;
}

/* Compute the vector_type_index for an IME source-LMUL input type, i.e. a
   type with the same SEW as the accumulator's element type but with a
   register-group multiplier of N (independent of the accumulator's MUL_C).
   This is used by the W=1 mmacc family's _lm{N} variants where vs1/vs2 use
   LMUL=N rather than the spec-default LMUL=1.

   Returns NUM_VECTOR_TYPES if the accumulator type does not have a valid
   sibling at the requested LMUL (e.g. because the SEW does not exist as a
   standard integer/FP element, or the resulting register group would
   overflow).  Element-type/signedness identity is preserved across the
   mapping; only the LMUL changes.  */

static vector_type_index
ime_lmul_input_idx (vector_type_index type_idx, rvv_base_type kind)
{
  if (type_idx == VECTOR_TYPE_INVALID)
    return NUM_VECTOR_TYPES;

  vector_type_index target = NUM_VECTOR_TYPES;
  switch (type_idx)
    {
      /* Integer mappings: each element-size group has a signed and an
	 unsigned row.  The signedness-preserving kinds (lmul{N}_vector)
	 stay on the same row; the explicit-signedness kinds
	 (lmul{N}_signed_vector / lmul{N}_unsigned_vector) cross to the
	 sibling row at the same SEW.  */
#define IME_LMUL_INT_MAP(SELEM_M1, SELEM_M2, SELEM_M4, SELEM_M8,	\
			 UELEM_M1, UELEM_M2, UELEM_M4, UELEM_M8)	\
      case VECTOR_TYPE_##SELEM_M1: case VECTOR_TYPE_##SELEM_M2:		\
      case VECTOR_TYPE_##SELEM_M4: case VECTOR_TYPE_##SELEM_M8:		\
      case VECTOR_TYPE_##UELEM_M1: case VECTOR_TYPE_##UELEM_M2:		\
      case VECTOR_TYPE_##UELEM_M4: case VECTOR_TYPE_##UELEM_M8:		\
	switch (kind)							\
	  {								\
	  case RVV_BASE_lmul2_vector:					\
	    target = (type_idx == VECTOR_TYPE_##SELEM_M1			\
		      || type_idx == VECTOR_TYPE_##SELEM_M2		\
		      || type_idx == VECTOR_TYPE_##SELEM_M4		\
		      || type_idx == VECTOR_TYPE_##SELEM_M8)		\
		     ? VECTOR_TYPE_##SELEM_M2				\
		     : VECTOR_TYPE_##UELEM_M2;				\
	    break;							\
	  case RVV_BASE_lmul4_vector:					\
	    target = (type_idx == VECTOR_TYPE_##SELEM_M1			\
		      || type_idx == VECTOR_TYPE_##SELEM_M2		\
		      || type_idx == VECTOR_TYPE_##SELEM_M4		\
		      || type_idx == VECTOR_TYPE_##SELEM_M8)		\
		     ? VECTOR_TYPE_##SELEM_M4				\
		     : VECTOR_TYPE_##UELEM_M4;				\
	    break;							\
	  case RVV_BASE_lmul8_vector:					\
	    target = (type_idx == VECTOR_TYPE_##SELEM_M1			\
		      || type_idx == VECTOR_TYPE_##SELEM_M2		\
		      || type_idx == VECTOR_TYPE_##SELEM_M4		\
		      || type_idx == VECTOR_TYPE_##SELEM_M8)		\
		     ? VECTOR_TYPE_##SELEM_M8				\
		     : VECTOR_TYPE_##UELEM_M8;				\
	    break;							\
	  case RVV_BASE_lmul1_signed_vector:				\
	    target = VECTOR_TYPE_##SELEM_M1; break;			\
	  case RVV_BASE_lmul2_signed_vector:				\
	    target = VECTOR_TYPE_##SELEM_M2; break;			\
	  case RVV_BASE_lmul4_signed_vector:				\
	    target = VECTOR_TYPE_##SELEM_M4; break;			\
	  case RVV_BASE_lmul8_signed_vector:				\
	    target = VECTOR_TYPE_##SELEM_M8; break;			\
	  case RVV_BASE_lmul1_unsigned_vector:				\
	    target = VECTOR_TYPE_##UELEM_M1; break;			\
	  case RVV_BASE_lmul2_unsigned_vector:				\
	    target = VECTOR_TYPE_##UELEM_M2; break;			\
	  case RVV_BASE_lmul4_unsigned_vector:				\
	    target = VECTOR_TYPE_##UELEM_M4; break;			\
	  case RVV_BASE_lmul8_unsigned_vector:				\
	    target = VECTOR_TYPE_##UELEM_M8; break;			\
	  default:							\
	    return NUM_VECTOR_TYPES;					\
	  }								\
	break;
      IME_LMUL_INT_MAP (vint8m1_t, vint8m2_t, vint8m4_t, vint8m8_t,
			vuint8m1_t, vuint8m2_t, vuint8m4_t, vuint8m8_t)
      IME_LMUL_INT_MAP (vint16m1_t, vint16m2_t, vint16m4_t, vint16m8_t,
			vuint16m1_t, vuint16m2_t, vuint16m4_t, vuint16m8_t)
      IME_LMUL_INT_MAP (vint32m1_t, vint32m2_t, vint32m4_t, vint32m8_t,
			vuint32m1_t, vuint32m2_t, vuint32m4_t, vuint32m8_t)
      IME_LMUL_INT_MAP (vint64m1_t, vint64m2_t, vint64m4_t, vint64m8_t,
			vuint64m1_t, vuint64m2_t, vuint64m4_t, vuint64m8_t)
#undef IME_LMUL_INT_MAP

      /* IME `m16' accumulator types route LMUL{1,2,4,8} source-LMUL
	 lookups to the m{1,2,4,8} sibling at the accumulator's SEW.
	 Same signedness-preserving / signed / unsigned semantics as
	 the m1..m8 rows above; only the source row is the m16 type
	 rather than the m{1..8} type.  */
#define IME_LMUL_INT_M16_MAP(SELEM_M16, SELEM_M1, SELEM_M2, SELEM_M4,	\
			     SELEM_M8, UELEM_M16, UELEM_M1, UELEM_M2,	\
			     UELEM_M4, UELEM_M8)			\
      case VECTOR_TYPE_##SELEM_M16: case VECTOR_TYPE_##UELEM_M16:	\
	switch (kind)							\
	  {								\
	  case RVV_BASE_lmul1_vector:					\
	    target = (type_idx == VECTOR_TYPE_##SELEM_M16)		\
		     ? VECTOR_TYPE_##SELEM_M1				\
		     : VECTOR_TYPE_##UELEM_M1;				\
	    break;							\
	  case RVV_BASE_lmul2_vector:					\
	    target = (type_idx == VECTOR_TYPE_##SELEM_M16)		\
		     ? VECTOR_TYPE_##SELEM_M2				\
		     : VECTOR_TYPE_##UELEM_M2;				\
	    break;							\
	  case RVV_BASE_lmul4_vector:					\
	    target = (type_idx == VECTOR_TYPE_##SELEM_M16)		\
		     ? VECTOR_TYPE_##SELEM_M4				\
		     : VECTOR_TYPE_##UELEM_M4;				\
	    break;							\
	  case RVV_BASE_lmul8_vector:					\
	    target = (type_idx == VECTOR_TYPE_##SELEM_M16)		\
		     ? VECTOR_TYPE_##SELEM_M8				\
		     : VECTOR_TYPE_##UELEM_M8;				\
	    break;							\
	  case RVV_BASE_lmul1_signed_vector:				\
	    target = VECTOR_TYPE_##SELEM_M1; break;			\
	  case RVV_BASE_lmul2_signed_vector:				\
	    target = VECTOR_TYPE_##SELEM_M2; break;			\
	  case RVV_BASE_lmul4_signed_vector:				\
	    target = VECTOR_TYPE_##SELEM_M4; break;			\
	  case RVV_BASE_lmul8_signed_vector:				\
	    target = VECTOR_TYPE_##SELEM_M8; break;			\
	  case RVV_BASE_lmul1_unsigned_vector:				\
	    target = VECTOR_TYPE_##UELEM_M1; break;			\
	  case RVV_BASE_lmul2_unsigned_vector:				\
	    target = VECTOR_TYPE_##UELEM_M2; break;			\
	  case RVV_BASE_lmul4_unsigned_vector:				\
	    target = VECTOR_TYPE_##UELEM_M4; break;			\
	  case RVV_BASE_lmul8_unsigned_vector:				\
	    target = VECTOR_TYPE_##UELEM_M8; break;			\
	  default:							\
	    return NUM_VECTOR_TYPES;					\
	  }								\
	break;
      IME_LMUL_INT_M16_MAP (vint8m16_t, vint8m1_t, vint8m2_t, vint8m4_t,
			    vint8m8_t, vuint8m16_t, vuint8m1_t, vuint8m2_t,
			    vuint8m4_t, vuint8m8_t)
      IME_LMUL_INT_M16_MAP (vint16m16_t, vint16m1_t, vint16m2_t, vint16m4_t,
			    vint16m8_t, vuint16m16_t, vuint16m1_t,
			    vuint16m2_t, vuint16m4_t, vuint16m8_t)
      IME_LMUL_INT_M16_MAP (vint32m16_t, vint32m1_t, vint32m2_t, vint32m4_t,
			    vint32m8_t, vuint32m16_t, vuint32m1_t,
			    vuint32m2_t, vuint32m4_t, vuint32m8_t)
      IME_LMUL_INT_M16_MAP (vint64m16_t, vint64m1_t, vint64m2_t, vint64m4_t,
			    vint64m8_t, vuint64m16_t, vuint64m1_t,
			    vuint64m2_t, vuint64m4_t, vuint64m8_t)
#undef IME_LMUL_INT_M16_MAP

      /* Floating-point mappings: signedness-preserving kinds only.
	 The explicit-signedness kinds are not meaningful for FP types
	 and return NUM_VECTOR_TYPES.  */
#define IME_LMUL_FP_MAP(ELEM_M1, ELEM_M2, ELEM_M4, ELEM_M8)		\
      case VECTOR_TYPE_##ELEM_M1: case VECTOR_TYPE_##ELEM_M2:		\
      case VECTOR_TYPE_##ELEM_M4: case VECTOR_TYPE_##ELEM_M8:		\
	switch (kind)							\
	  {								\
	  case RVV_BASE_lmul2_vector:					\
	    target = VECTOR_TYPE_##ELEM_M2; break;			\
	  case RVV_BASE_lmul4_vector:					\
	    target = VECTOR_TYPE_##ELEM_M4; break;			\
	  case RVV_BASE_lmul8_vector:					\
	    target = VECTOR_TYPE_##ELEM_M8; break;			\
	  default:							\
	    return NUM_VECTOR_TYPES;					\
	  }								\
	break;
      IME_LMUL_FP_MAP (vfloat16m1_t, vfloat16m2_t, vfloat16m4_t, vfloat16m8_t)
      IME_LMUL_FP_MAP (vbfloat16m1_t, vbfloat16m2_t, vbfloat16m4_t,
		       vbfloat16m8_t)
      IME_LMUL_FP_MAP (vfloat32m1_t, vfloat32m2_t, vfloat32m4_t, vfloat32m8_t)
      IME_LMUL_FP_MAP (vfloat64m1_t, vfloat64m2_t, vfloat64m4_t, vfloat64m8_t)
#undef IME_LMUL_FP_MAP
      default:
	return NUM_VECTOR_TYPES;
    }
  return target;
}

/* Helper used from DEF_RVV_BASE_TYPE expansions in get_tree_type() for the
   IME source-LMUL base types (lmul2_vector, lmul4_vector, lmul8_vector).
   Returns the tree for the computed vector type, or NULL_TREE if no
   sibling exists or the target type is not registered for the current
   -march.  */

static tree
get_ime_lmul_input_type (vector_type_index type_idx, rvv_base_type kind)
{
  vector_type_index idx = ime_lmul_input_idx (type_idx, kind);
  if (idx == NUM_VECTOR_TYPES)
    return NULL_TREE;
  return builtin_types[idx].vector;
}

vector_type_index
rvv_arg_type_info::get_function_type_index (vector_type_index type_idx) const
{
  if (base_type == RVV_BASE_ime_double_trunc_lmul1_vector
      || base_type == RVV_BASE_ime_double_trunc_lmul1_signed_vector
      || base_type == RVV_BASE_ime_double_trunc_lmul1_unsigned_vector
      || base_type == RVV_BASE_ime_quad_trunc_lmul1_vector
      || base_type == RVV_BASE_ime_quad_trunc_lmul1_signed_vector
      || base_type == RVV_BASE_ime_quad_trunc_lmul1_unsigned_vector
      || base_type == RVV_BASE_ime_oct_trunc_lmul1_vector
      || base_type == RVV_BASE_ime_oct_trunc_lmul1_signed_vector
      || base_type == RVV_BASE_ime_oct_trunc_lmul1_unsigned_vector
      || base_type == RVV_BASE_ime_double_trunc_lmul2_vector
      || base_type == RVV_BASE_ime_double_trunc_lmul2_signed_vector
      || base_type == RVV_BASE_ime_double_trunc_lmul2_unsigned_vector
      || base_type == RVV_BASE_ime_double_trunc_lmul4_vector
      || base_type == RVV_BASE_ime_double_trunc_lmul4_signed_vector
      || base_type == RVV_BASE_ime_double_trunc_lmul4_unsigned_vector
      || base_type == RVV_BASE_ime_double_trunc_lmul8_vector
      || base_type == RVV_BASE_ime_double_trunc_lmul8_signed_vector
      || base_type == RVV_BASE_ime_double_trunc_lmul8_unsigned_vector
      || base_type == RVV_BASE_ime_quad_trunc_lmul2_vector
      || base_type == RVV_BASE_ime_quad_trunc_lmul2_signed_vector
      || base_type == RVV_BASE_ime_quad_trunc_lmul2_unsigned_vector
      || base_type == RVV_BASE_ime_quad_trunc_lmul4_vector
      || base_type == RVV_BASE_ime_quad_trunc_lmul4_signed_vector
      || base_type == RVV_BASE_ime_quad_trunc_lmul4_unsigned_vector
      || base_type == RVV_BASE_ime_quad_trunc_lmul8_vector
      || base_type == RVV_BASE_ime_quad_trunc_lmul8_signed_vector
      || base_type == RVV_BASE_ime_quad_trunc_lmul8_unsigned_vector
      || base_type == RVV_BASE_ime_oct_trunc_lmul2_vector
      || base_type == RVV_BASE_ime_oct_trunc_lmul2_signed_vector
      || base_type == RVV_BASE_ime_oct_trunc_lmul2_unsigned_vector
      || base_type == RVV_BASE_ime_oct_trunc_lmul4_vector
      || base_type == RVV_BASE_ime_oct_trunc_lmul4_signed_vector
      || base_type == RVV_BASE_ime_oct_trunc_lmul4_unsigned_vector
      || base_type == RVV_BASE_ime_oct_trunc_lmul8_vector
      || base_type == RVV_BASE_ime_oct_trunc_lmul8_signed_vector
      || base_type == RVV_BASE_ime_oct_trunc_lmul8_unsigned_vector)
    {
      vector_type_index idx = ime_widening_input_idx (type_idx, base_type);
      if (idx == NUM_VECTOR_TYPES || !builtin_types[idx].vector)
	return NUM_VECTOR_TYPES;
      return idx;
    }
  if (base_type == RVV_BASE_lmul2_vector
      || base_type == RVV_BASE_lmul4_vector
      || base_type == RVV_BASE_lmul8_vector
      || base_type == RVV_BASE_lmul1_signed_vector
      || base_type == RVV_BASE_lmul1_unsigned_vector
      || base_type == RVV_BASE_lmul2_signed_vector
      || base_type == RVV_BASE_lmul2_unsigned_vector
      || base_type == RVV_BASE_lmul4_signed_vector
      || base_type == RVV_BASE_lmul4_unsigned_vector
      || base_type == RVV_BASE_lmul8_signed_vector
      || base_type == RVV_BASE_lmul8_unsigned_vector)
    {
      vector_type_index idx = ime_lmul_input_idx (type_idx, base_type);
      if (idx == NUM_VECTOR_TYPES || !builtin_types[idx].vector)
	return NUM_VECTOR_TYPES;
      return idx;
    }
  tree type
    = builtin_types[function_types[type_idx].type_indexes[base_type]].vector;
  return type ? function_types[type_idx].type_indexes[base_type]
	      : NUM_VECTOR_TYPES;
}

tree
rvv_arg_type_info::get_tree_type (vector_type_index type_idx) const
{
  /* If the builtin type is not registered means '-march' doesn't
     satisfy the require extension of the type. For example,
     vfloat32m1_t require floating-point extension. In this case,
     just return NULL_TREE.  */
  if (type_idx != VECTOR_TYPE_INVALID && !builtin_types[type_idx].vector)
    return NULL_TREE;

  switch (base_type)
    {
#define DEF_RVV_BASE_TYPE(NAME, TYPE)                                          \
  case RVV_BASE_##NAME:                                                        \
    return TYPE;
#include "riscv-vector-builtins.def"
    default:
      gcc_unreachable ();
    }
  gcc_unreachable ();
}

tree
rvv_arg_type_info::get_tuple_subpart_type (vector_type_index type_idx) const
{
  switch (type_idx)
    {
#define DEF_RVV_TUPLE_TYPE(NAME, NCHARS, ABI_NAME, SUBPART_TYPE, ARGS...)      \
  case VECTOR_TYPE_##NAME:                                                     \
    return builtin_types[VECTOR_TYPE_##SUBPART_TYPE].vector;
#include "riscv-vector-builtins.def"
    default:
      gcc_unreachable ();
    }
  gcc_unreachable ();
}

function_instance::function_instance (const char *base_name_in,
				      const function_base *base_in,
				      const function_shape *shape_in,
				      rvv_type_info type_in,
				      predication_type_index pred_in,
				      const rvv_op_info *op_info_in)
  : base_name (base_name_in), base (base_in), shape (shape_in), type (type_in),
    pred (pred_in), op_info (op_info_in)
{
}

bool
function_instance::operator== (const function_instance &other) const
{
  for (unsigned int i = 0; op_info->args[i].base_type != NUM_BASE_TYPES; ++i)
    if (op_info->args[i].base_type != other.op_info->args[i].base_type)
      return false;
  return (base == other.base && shape == other.shape
	  && type.index == other.type.index && op_info->op == other.op_info->op
	  && pred == other.pred
	  && op_info->ret.base_type == other.op_info->ret.base_type);
}

bool
function_instance::any_type_float_p () const
{
  if (riscv_vector_float_type_p (get_return_type ()))
    return true;

  for (int i = 0; op_info->args[i].base_type != NUM_BASE_TYPES; ++i)
    if (riscv_vector_float_type_p (get_arg_type (i)))
      return true;

  return false;
}

tree
function_instance::get_return_type () const
{
  return op_info->ret.get_tree_type (type.index);
}

bool
function_instance::function_returns_void_p () const
{
  return get_return_type () == void_type_node;
}

tree
function_instance::get_arg_type (unsigned opno) const
{
  return op_info->args[opno].get_tree_type (type.index);
}

/* Return a hash code for a function_instance.  */
hashval_t
function_instance::hash () const
{
  inchash::hash h;
  /* BASE uniquely determines BASE_NAME, so we don't need to hash both.  */
  h.add_ptr (base);
  h.add_ptr (shape);
  h.add_int (type.index);
  h.add_int (op_info->op);
  h.add_int (pred);
  h.add_int (op_info->ret.base_type);
  for (unsigned int i = 0; op_info->args[i].base_type != NUM_BASE_TYPES; ++i)
    h.add_int (op_info->args[i].base_type);
  return h.end ();
}

/* Return a set of CP_* flags that describe what the function could do,
   taking the command-line flags into account.  */
unsigned int
function_instance::call_properties () const
{
  unsigned int flags = base->call_properties (*this);

  /* -fno-trapping-math means that we can assume any FP exceptions
     are not user-visible.  */
  if (!flag_trapping_math)
    flags &= ~CP_RAISE_FP_EXCEPTIONS;

  return flags;
}

/* Return true if calls to the function could read some form of
   global state.  */
bool
function_instance::reads_global_state_p () const
{
  unsigned int flags = call_properties ();

  /* Preserve any dependence on rounding mode, flush to zero mode, etc.
     There is currently no way of turning this off; in particular,
     -fno-rounding-math (which is the default) means that we should make
     the usual assumptions about rounding mode, which for intrinsics means
     acting as the instructions do.  */
  if (flags & CP_READ_FPCR)
    return true;

  /* Handle direct reads of global state.  */
  return flags & (CP_READ_MEMORY | CP_READ_CSR);
}

/* Return true if calls to the function could modify some form of
   global state.  */
bool
function_instance::modifies_global_state_p () const
{
  unsigned int flags = call_properties ();

  /* Preserve any exception state written back to the FPCR,
     unless -fno-trapping-math says this is unnecessary.  */
  if (flags & CP_RAISE_FP_EXCEPTIONS)
    return true;

  /* Handle direct modifications of global state.  */
  return flags & (CP_WRITE_MEMORY | CP_WRITE_CSR | CP_USE_COPROCESSORS);
}

/* Return true if calls to the function could raise a signal.  */
bool
function_instance::could_trap_p () const
{
  unsigned int flags = call_properties ();

  /* Handle functions that could raise SIGFPE.  */
  if (flags & CP_RAISE_FP_EXCEPTIONS)
    return true;

  /* Handle functions that could raise SIGBUS or SIGSEGV.  */
  if (flags & (CP_READ_MEMORY | CP_WRITE_MEMORY))
    return true;

  return false;
}

function_builder::function_builder ()
{
  m_direct_overloads = lang_GNU_CXX ();
  gcc_obstack_init (&m_string_obstack);
}

function_builder::~function_builder ()
{
  obstack_free (&m_string_obstack, NULL);
}

/* Allocate arguments of the function.  */
void
function_builder::allocate_argument_types (const function_instance &instance,
					   vec<tree> &argument_types) const
{
  for (unsigned int i = 0;
       instance.op_info->args[i].base_type != NUM_BASE_TYPES; ++i)
    argument_types.quick_push (
      instance.op_info->args[i].get_tree_type (instance.type.index));
}

/* Apply predication into argument_types.  */
void
function_builder::apply_predication (const function_instance &instance,
				     tree return_type,
				     vec<tree> &argument_types) const
{
  /* These predication types need to apply merge type.  */
  if (instance.base->has_merge_operand_p ())
    if (instance.pred == PRED_TYPE_tu || instance.pred == PRED_TYPE_tum
	|| instance.pred == PRED_TYPE_tumu || instance.pred == PRED_TYPE_mu)
      argument_types.quick_insert (0, return_type);

  /* These predication types need to apply mask type.  */
  vector_type_index mask_type_index
    = function_types[instance.type.index].type_indexes[RVV_BASE_mask];
  tree mask_type = builtin_types[mask_type_index].vector;
  if (instance.pred == PRED_TYPE_m || instance.pred == PRED_TYPE_tum
      || instance.pred == PRED_TYPE_tumu || instance.pred == PRED_TYPE_mu)
    argument_types.quick_insert (0, mask_type);

  /* check if rounding mode parameter need  */
  if (instance.base->has_rounding_mode_operand_p ())
    argument_types.quick_push (unsigned_type_node);

  /* check if vl parameter need  */
  if (instance.base->apply_vl_p ())
    argument_types.quick_push (size_type_node);
}

/* Register all the functions in GROUP.  */
void
function_builder::register_function_group (const function_group_info &group)
{
  (*group.shape)->build (*this, group);
}

/* Add NAME to the end of the function name being built.  */
void
function_builder::append_name (const char *name)
{
  obstack_grow (&m_string_obstack, name, strlen (name));
}

/* Add "__riscv_" and "name".  */
void
function_builder::append_base_name (const char *name)
{
  append_name ("__riscv_");
  append_name (name);
}

/* Add SEW into function name.  */
void
function_builder::append_sew (int sew)
{
  switch (sew)
    {
    case 8:
      append_name ("8");
      break;
    case 16:
      append_name ("16");
      break;
    case 32:
      append_name ("32");
      break;
    case 64:
      append_name ("64");
      break;
    default:
      gcc_unreachable ();
    }
}

/* Add NF into function name.  */
void
function_builder::append_nf (int nf)
{
  switch (nf)
    {
    case 2:
      append_name ("2");
      break;
    case 3:
      append_name ("3");
      break;
    case 4:
      append_name ("4");
      break;
    case 5:
      append_name ("5");
      break;
    case 6:
      append_name ("6");
      break;
    case 7:
      append_name ("7");
      break;
    case 8:
      append_name ("8");
      break;
    default:
      gcc_unreachable ();
    }
}

/* Zero-terminate and complete the function name being built.  */
char *
function_builder::finish_name ()
{
  obstack_1grow (&m_string_obstack, 0);
  return (char *) obstack_finish (&m_string_obstack);
}

/* Return the appropriate function attributes for INSTANCE.  */
tree
function_builder::get_attributes (const function_instance &instance)
{
  tree attrs = NULL_TREE;

  if (!instance.modifies_global_state_p ())
    {
      if (instance.reads_global_state_p ())
	attrs = add_attribute ("pure", attrs);
      else
	attrs = add_attribute ("const", attrs);
    }

  if (!flag_non_call_exceptions || !instance.could_trap_p ())
    attrs = add_attribute ("nothrow", attrs);

  return add_attribute ("leaf", attrs);
}

/* Add a function called NAME with type FNTYPE and attributes ATTRS.
   INSTANCE describes what the function does.  */
registered_function &
function_builder::add_function (const function_instance &instance,
				const char *name, tree fntype, tree attrs,
				const char *overload_name,
				const vec<tree> &argument_types,
				enum required_ext required,
				bool overloaded_p = false)
{
  /* Compute the partition for this function.  The per-partition index
     is determined by the current length of that partition's vector.  */
  rvv_builtin_partition ext_partition
    = get_builtin_partition (required, instance);
  unsigned int index = vec_safe_length (partition_functions[ext_partition]);

  unsigned int code = (index << RVV_SUBCODE_SHIFT)
		      | (ext_partition << RVV_EXT_PARTITION_SHIFT)
		      | RISCV_BUILTIN_VECTOR;

  /* We need to be able to generate placeholders to ensure that we have a
     consistent numbering scheme for function codes between the C and C++
     frontends, so that everything ties up in LTO.

     Currently, tree-streamer-in.c:unpack_ts_function_decl_value_fields
     validates that tree nodes returned by TARGET_BUILTIN_DECL are non-NULL and
     some node other than error_mark_node. This is a holdover from when builtin
     decls were streamed by code rather than by value.

     Ultimately, we should be able to remove this validation of BUILT_IN_MD
     nodes and remove the target hook. For now, however, we need to appease the
     validation and return a non-NULL, non-error_mark_node node, so we
     arbitrarily choose integer_zero_node.  */
  tree decl = in_lto_p
		? integer_zero_node
		: simulate_builtin_function_decl (input_location, name, fntype,
						  code, NULL, attrs);

  registered_function &rfn = *ggc_alloc<registered_function> ();
  rfn.instance = instance;
  rfn.decl = decl;
  rfn.overload_name = overload_name ? xstrdup (overload_name) : NULL;
  rfn.argument_types = argument_types;
  rfn.overloaded_p = overloaded_p;
  /* Update required extension based on partition.  Functions placed in
     the NO_XTHEAD partition due to fractional LMUL or unsupported ops
     need VECTOR_EXT_NO_XTHEAD to get proper error messages.  */
  if (ext_partition == RVV_PARTITION_VECTOR_NO_XTHEAD
      && required == VECTOR_EXT)
    rfn.required = VECTOR_EXT_NO_XTHEAD;
  else
    rfn.required = required;
  vec_safe_push (partition_functions[ext_partition], &rfn);

  return rfn;
}

/* Add a built-in function for INSTANCE, with the argument types given
   by ARGUMENT_TYPES and the return type given by RETURN_TYPE. NAME is
   the "full" name for C function. OVERLOAD_NAME is the "short" name for
   C++ overloaded function. OVERLOAD_NAME can be nullptr because some
   instance doesn't have C++ overloaded function.  */
void
function_builder::add_unique_function (const function_instance &instance,
				       const function_shape *shape,
				       tree return_type,
				       vec<tree> &argument_types,
				       enum required_ext required)
{
  /* Do not add this function if it is invalid.  */
  if (!check_required_extensions (instance))
    return;

  /* Also add the function under its overloaded alias, if we want
     a separate decl for each instance of an overloaded function.  */
  char *overload_name = shape->get_name (*this, instance, true);

  /* Add the function under its full (unique) name.  */
  char *name = shape->get_name (*this, instance, false);
  tree fntype
    = build_function_type_array (return_type, argument_types.length (),
				 argument_types.address ());
  tree attrs = get_attributes (instance);
  registered_function &rfn
    = add_function (instance, name, fntype, attrs, overload_name,
		    argument_types.copy (), required);

  /* Enter the function into the hash table.  */
  hashval_t hash = instance.hash ();
  registered_function **rfn_slot
    = function_table->find_slot_with_hash (instance, hash, INSERT);
  gcc_assert (!*rfn_slot);
  *rfn_slot = &rfn;

  if (overload_name)
    {
      /* Attribute lists shouldn't be shared.  */
      tree attrs = get_attributes (instance);
      if (m_direct_overloads)
	add_function (instance, overload_name, fntype, attrs, NULL,
		      vNULL, required);
      else
	{
	  if (!non_overloaded_function_table)
	    non_overloaded_function_table
	      = new hash_table<non_overloaded_registered_function_hasher> (
		1023);
	  /* Enter the function into the non-overloaded hash table.  */
	  hash = rfn.overloaded_hash ();
	  rfn_slot
	    = non_overloaded_function_table->find_slot_with_hash (&rfn, hash,
								  INSERT);
	  gcc_assert (!*rfn_slot);
	  *rfn_slot = &rfn;
	}
    }
  obstack_free (&m_string_obstack, name);
}

/* Add overloaded function for gcc. */
void
function_builder::add_overloaded_function (const function_instance &instance,
					   const function_shape *shape,
					   enum required_ext required)
{
  if (m_direct_overloads)
    return;

  if (!check_required_extensions (instance))
    return;

  char *name = shape->get_name (*this, instance, true);

  if (name)
    {
      /* To avoid API conflicting, take void return type and void argument
	 for the overloaded function.  */
      tree fntype = build_function_type (void_type_node, void_list_node);
      add_function (instance, name, fntype, NULL_TREE, name,
		    vNULL, required, true);
      obstack_free (&m_string_obstack, name);
    }
}

function_call_info::function_call_info (location_t location_in,
					const function_instance &instance_in,
					tree fndecl_in)
  : function_instance (instance_in), location (location_in), fndecl (fndecl_in)
{}

gimple_folder::gimple_folder (const function_instance &instance, tree fndecl,
			      gimple_stmt_iterator *gsi_in, gcall *call_in)
  : function_call_info (gimple_location (call_in), instance, fndecl),
    gsi (gsi_in), call (call_in), lhs (gimple_call_lhs (call_in))
{
}

/* Try to fold the call.  Return the new statement on success and null
   on failure.  */
gimple *
gimple_folder::fold ()
{
  /* Don't fold anything when RVV is disabled; emit an error during
     expansion instead.  */
  if (!TARGET_VECTOR)
    return NULL;

  /* Punt if the function has a return type and no result location is
     provided.  The attributes should allow target-independent code to
     remove the calls if appropriate.  */
  if (!lhs && TREE_TYPE (gimple_call_fntype (call)) != void_type_node)
    return NULL;

  return base->fold (*this);
}

function_expander::function_expander (const function_instance &instance,
				      tree fndecl_in, tree exp_in,
				      rtx target_in)
  : function_call_info (EXPR_LOCATION (exp_in), instance, fndecl_in),
    exp (exp_in), target (target_in), opno (0)
{
  if (!function_returns_void_p ())
    {
      if (target != NULL_RTX && MEM_P (target))
	/* Since there is no intrinsic where target is a mem operand, it
	   should be converted to reg if it is a mem operand.  */
	target = force_reg (GET_MODE (target), target);
      create_output_operand (&m_ops[opno++], target,
			     TYPE_MODE (TREE_TYPE (exp)));
    }
}

/* Take argument ARGNO from EXP's argument list and convert it into
   an expand operand.  Store the operand in *M_OPS.  */
void
function_expander::add_input_operand (unsigned argno)
{
  tree arg = CALL_EXPR_ARG (exp, argno);
  rtx x = expand_normal (arg);
  add_input_operand (TYPE_MODE (TREE_TYPE (arg)), x);
}

/* Since we may normalize vop/vop_tu/vop_m/vop_tumu.. into a single patter.
   We add a undef for the intrinsics that don't need a real merge.  */
void
function_expander::add_vundef_operand (machine_mode mode)
{
  add_input_operand (mode, RVV_VUNDEF (mode));
}

/* Add a memory operand with mode MODE and address ADDR.  */
void
function_expander::add_mem_operand (machine_mode mode, unsigned argno)
{
  gcc_assert (VECTOR_MODE_P (mode));
  rtx addr = expand_normal (CALL_EXPR_ARG (exp, argno));
  rtx mem = gen_rtx_MEM (mode, memory_address (mode, addr));
  /* The memory is only guaranteed to be element-aligned.  */
  set_mem_align (mem, GET_MODE_ALIGNMENT (GET_MODE_INNER (mode)));
  add_fixed_operand (mem);
}

/* Return the machine_mode of the corresponding mask type.  */
machine_mode
function_expander::mask_mode (void) const
{
  vector_type_index mask_type_index
    = function_types[type.index].type_indexes[RVV_BASE_mask];
  return TYPE_MODE (builtin_types[mask_type_index].vector);
}

/* Implement the call using instruction ICODE, with a 1:1 mapping between
   arguments and input operands.  */
rtx
function_expander::use_exact_insn (insn_code icode)
{
  machine_mode mode = TYPE_MODE (TREE_TYPE (exp));

  /* Record the offset to get the argument.  */
  int arg_offset = 0;

  if (base->use_mask_predication_p ())
    {
      if (use_real_mask_p (pred))
	add_input_operand (arg_offset++);
      else
	add_all_one_mask_operand (mask_mode ());
    }

  /* Store operation doesn't have merge operand.  */
  if (!function_returns_void_p () && base->has_merge_operand_p ())
    {
      if (use_real_merge_p (pred))
	add_input_operand (arg_offset++);
      else
	add_vundef_operand (mode);
    }

  for (int argno = arg_offset; argno < call_expr_nargs (exp); argno++)
    {
      if (base->has_rounding_mode_operand_p ()
	  && argno == call_expr_nargs (exp) - 2)
	{
	  /* Since the rounding mode argument position is not consistent with
	     the instruction pattern, we need to skip rounding mode argument
	     here.  */
	  continue;
	}
      add_input_operand (argno);
    }

  if (base->apply_tail_policy_p ())
    add_input_operand (Pmode, get_tail_policy_for_pred (pred));
  if (base->apply_mask_policy_p ())
    add_input_operand (Pmode, get_mask_policy_for_pred (pred));

  if (base->apply_vl_p ())
    add_input_operand (Pmode, get_avl_type_rtx (avl_type::NONVLMAX));

  if (base->has_rounding_mode_operand_p ())
    add_input_operand (call_expr_nargs (exp) - 2);

  /* The RVV floating-point only support dynamic rounding mode in the
     FRM register.  */
  if (base->may_require_frm_p ()
      && opno < insn_data[icode].n_generator_args)
    add_input_operand (Pmode, gen_int_mode (riscv_vector::FRM_DYN, Pmode));

  /* IME consumer patterns carry a hidden Pmode `vtype source' operand
     so the vsetvl pass can re-anchor IME vtype right before each
     consumer (after spill-induced clobbers).  The runtime path
     stashed the full_vtype rtx during `emit_ime_runtime_vtype_setup';
     for the static-VLEN path no stash exists and we pass `const0_rtx'
     as a placeholder (the pass synthesises its own const vtype via
     `vsetvl_ime' rather than reading this operand).  */
  if (base->apply_ime_vtype_op_p ())
    {
      rtx fvX = riscv_ime_get_last_full_vtype ();
      add_input_operand (Pmode, fvX ? fvX : const0_rtx);
      riscv_ime_set_last_full_vtype (NULL_RTX);
    }

  return generate_insn (icode);
}

/* Implement the call using instruction ICODE for a microscaled (section 8)
   IME intrinsic.  SCALES_ARGNO is the index of the block-scale argument
   (vuint16m1_t v0) in the call expression.  This argument is moved into
   the v0 register before the instruction is emitted, and is skipped when
   building the pattern operands (the RTL pattern uses an implicit
   (use (reg:RVVM1HI V0_REGNUM)) instead of a match_operand).  */
rtx
function_expander::use_microscaled_insn (insn_code icode, int scales_argno)
{
  /* Move the scales vector into v0.  */
  rtx scales_rtx = expand_normal (CALL_EXPR_ARG (exp, scales_argno));
  rtx v0 = gen_rtx_REG (GET_MODE (scales_rtx), V_REG_FIRST);
  emit_move_insn (v0, scales_rtx);

  machine_mode mode = TYPE_MODE (TREE_TYPE (exp));

  /* No mask/merge for IME microscaled.  */

  /* Add all call args except the scales argument.  */
  for (int argno = 0; argno < call_expr_nargs (exp); argno++)
    {
      if (argno == scales_argno)
	continue;
      add_input_operand (argno);
    }

  /* Tail policy and AVL type.  */
  add_input_operand (Pmode, get_tail_policy_for_pred (pred));
  add_input_operand (Pmode, get_avl_type_rtx (avl_type::NONVLMAX));

  /* IME microscaled consumer patterns carry the hidden vtype-source
     operand at the same trailing position as the non-scaled variants.  */
  if (base->apply_ime_vtype_op_p ())
    {
      rtx fvX = riscv_ime_get_last_full_vtype ();
      add_input_operand (Pmode, fvX ? fvX : const0_rtx);
      riscv_ime_set_last_full_vtype (NULL_RTX);
    }

  return generate_insn (icode);
}

/* Helper for microscaled expanders that bypasses use_microscaled_insn
   by adding just the tail-policy and avl-type operands.  Called from
   function base expand() methods that build the operand list manually.  */
void
function_expander::add_microscaled_tail_and_avl ()
{
  add_input_operand (Pmode, get_tail_policy_for_pred (pred));
  add_input_operand (Pmode, get_avl_type_rtx (avl_type::NONVLMAX));
}

int
function_expander::prepare_contiguous_load_insn ()
{
  gcc_assert (call_expr_nargs (exp) > 0);
  machine_mode mode = TYPE_MODE (TREE_TYPE (exp));

  /* Record the offset to get the argument.  */
  int arg_offset = 0;

  if (use_real_mask_p (pred))
    add_input_operand (arg_offset++);
  else
    add_all_one_mask_operand (mask_mode ());

  if (use_real_merge_p (pred))
    add_input_operand (arg_offset++);
  else
    add_vundef_operand (mode);

  add_mem_operand (mode, arg_offset++);
  return arg_offset;
}

/* Use contiguous load INSN.  */
rtx
function_expander::use_contiguous_load_insn (insn_code icode)
{
  int arg_offset = prepare_contiguous_load_insn ();

  for (int argno = arg_offset; argno < call_expr_nargs (exp); argno++)
    add_input_operand (argno);

  machine_mode mode = TYPE_MODE (TREE_TYPE (exp));
  if (GET_MODE_CLASS (mode) != MODE_VECTOR_BOOL)
    {
      add_input_operand (Pmode, get_tail_policy_for_pred (pred));
      add_input_operand (Pmode, get_mask_policy_for_pred (pred));
    }

  if (opno != insn_data[icode].n_generator_args)
    add_input_operand (Pmode, get_avl_type_rtx (avl_type::NONVLMAX));

  /* IME tile-load consumer patterns carry a hidden Pmode `vtype source'
     operand (see `use_exact_insn' for the full rationale).  */
  if (base->apply_ime_vtype_op_p ())
    {
      rtx fvX = riscv_ime_get_last_full_vtype ();
      add_input_operand (Pmode, fvX ? fvX : const0_rtx);
      riscv_ime_set_last_full_vtype (NULL_RTX);
    }

  return generate_insn (icode);
}

/* Similar to use_contiguous_load_insn but skips the vector-length destination
   operand that a fault-only-first load intrinsic has.  Then we add tail and
   mask policy as well as AVL operand.  Last, add the vector-length destination
   operand that we skipped initially.  */
rtx
function_expander::use_fof_load_insn ()
{
  int arg_offset = prepare_contiguous_load_insn ();

  int vl_dest_arg = call_expr_nargs (exp) - 2;
  for (int argno = arg_offset; argno < call_expr_nargs (exp); argno++)
    {
      /* Skip argument for VL destination in memory but add the others.  */
      if (argno != vl_dest_arg)
	add_input_operand (argno);
    }

  machine_mode mode = TYPE_MODE (TREE_TYPE (exp));
  if (GET_MODE_CLASS (mode) != MODE_VECTOR_BOOL)
    {
      add_input_operand (Pmode, get_tail_policy_for_pred (pred));
      add_input_operand (Pmode, get_mask_policy_for_pred (pred));
    }

  add_input_operand (Pmode, get_avl_type_rtx (avl_type::NONVLMAX));

  tree arg = CALL_EXPR_ARG (exp, vl_dest_arg);

  /* Use a regular FoF load if the user does not want to store VL.  */
  if (integer_zerop (arg))
    {
      insn_code icode = code_for_pred_fault_load (mode);
      return generate_insn (icode);
    }

  /* The VL-setting FoF load writes the new VL to VL_REG.
     Store it to memory.  */
  rtx vl_reg = gen_reg_rtx (Pmode);
  add_output_operand (Pmode, vl_reg);
  insn_code icode = code_for_pred_fault_load_set_vl (mode, Pmode);
  rtx res = generate_insn (icode);

  rtx addr = expand_normal (arg);
  rtx mem = gen_rtx_MEM (Pmode, memory_address (Pmode, addr));
  emit_move_insn (mem, vl_reg);

  return res;
}

/* Use contiguous store INSN.  */
rtx
function_expander::use_contiguous_store_insn (insn_code icode)
{
  gcc_assert (call_expr_nargs (exp) > 0);
  machine_mode mode = TYPE_MODE (builtin_types[type.index].vector);

  /* Record the offset to get the argument.  */
  int arg_offset = 0;

  add_mem_operand (mode, use_real_mask_p (pred) ? 1 : 0);

  if (use_real_mask_p (pred))
    add_input_operand (arg_offset++);
  else
    add_all_one_mask_operand (mask_mode ());

  arg_offset++;
  for (int argno = arg_offset; argno < call_expr_nargs (exp); argno++)
    add_input_operand (argno);

  add_input_operand (Pmode, get_avl_type_rtx (avl_type::NONVLMAX));

  /* IME tile-store consumer patterns carry a hidden Pmode `vtype source'
     operand (see `use_exact_insn' for the full rationale).  */
  if (base->apply_ime_vtype_op_p ())
    {
      rtx fvX = riscv_ime_get_last_full_vtype ();
      add_input_operand (Pmode, fvX ? fvX : const0_rtx);
      riscv_ime_set_last_full_vtype (NULL_RTX);
    }

  return generate_insn (icode);
}

/* Implement the call using instruction ICODE, with a 1:1 mapping between
   arguments and input operands.  */
rtx
function_expander::use_compare_insn (rtx_code rcode, insn_code icode)
{
  machine_mode mode = TYPE_MODE (builtin_types[type.index].vector);
  machine_mode mask_mode = TYPE_MODE (TREE_TYPE (exp));

  /* Record the offset to get the argument.  */
  int arg_offset = 0;

  if (use_real_mask_p (pred))
    add_input_operand (arg_offset++);
  else
    add_all_one_mask_operand (mask_mode);

  if (use_real_merge_p (pred))
    add_input_operand (arg_offset++);
  else
    add_vundef_operand (mask_mode);

  rtx op1 = expand_normal (CALL_EXPR_ARG (exp, arg_offset++));
  rtx op2 = expand_normal (CALL_EXPR_ARG (exp, arg_offset++));
  if (!insn_operand_matches (icode, opno + 1, op1))
    op1 = force_reg (mode, op1);
  if (!insn_operand_matches (icode, opno + 2, op2))
    {
      if (VECTOR_MODE_P (GET_MODE (op2)))
	op2 = force_reg (mode, op2);
      else
	op2 = force_reg (GET_MODE_INNER (mode), op2);
    }
  rtx comparison = gen_rtx_fmt_ee (rcode, mask_mode, op1, op2);
  if (!VECTOR_MODE_P (GET_MODE (op2)))
    comparison = gen_rtx_fmt_ee (rcode, mask_mode, op1,
				 gen_rtx_VEC_DUPLICATE (mode, op2));
  add_fixed_operand (comparison);
  add_fixed_operand (op1);
  if (CONST_INT_P (op2))
    add_integer_operand (op2);
  else
    add_fixed_operand (op2);
  for (int argno = arg_offset; argno < call_expr_nargs (exp); argno++)
    add_input_operand (argno);

  add_input_operand (Pmode, get_mask_policy_for_pred (pred));
  add_input_operand (Pmode, get_avl_type_rtx (avl_type::NONVLMAX));
  return generate_insn (icode);
}

/* Implement the call using instruction ICODE, with a 1:1 mapping between
   arguments and input operands.  */
rtx
function_expander::use_ternop_insn (bool vd_accum_p, insn_code icode)
{
  machine_mode mode = TYPE_MODE (builtin_types[type.index].vector);

  /* Record the offset to get the argument.  */
  int arg_offset = 0;

  if (use_real_mask_p (pred))
    add_input_operand (arg_offset++);
  else
    add_all_one_mask_operand (mask_mode ());

  rtx vd = expand_normal (CALL_EXPR_ARG (exp, arg_offset++));
  rtx vs1 = expand_normal (CALL_EXPR_ARG (exp, arg_offset++));
  rtx vs2 = expand_normal (CALL_EXPR_ARG (exp, arg_offset++));

  if (VECTOR_MODE_P (GET_MODE (vs1)))
    {
      if (!vd_accum_p)
	add_input_operand (mode, vd);
      add_input_operand (mode, vs1);
      add_input_operand (mode, vs2);
      if (vd_accum_p)
	add_input_operand (mode, vd);
      add_input_operand (mode, vd);
    }
  else
    {
      add_input_operand (GET_MODE_INNER (mode), vs1);
      if (vd_accum_p)
	{
	  add_input_operand (mode, vs2);
	  add_input_operand (mode, vd);
	}
      else
	{
	  add_input_operand (mode, vd);
	  add_input_operand (mode, vs2);
	}
      add_input_operand (mode, vd);
    }

  for (int argno = arg_offset; argno < call_expr_nargs (exp); argno++)
    {
      if (base->has_rounding_mode_operand_p ()
	  && argno == call_expr_nargs (exp) - 2)
	{
	  /* Since the rounding mode argument position is not consistent with
	     the instruction pattern, we need to skip rounding mode argument
	     here.  */
	  continue;
	}
      add_input_operand (argno);
    }

  add_input_operand (Pmode, get_tail_policy_for_pred (pred));
  add_input_operand (Pmode, get_mask_policy_for_pred (pred));
  add_input_operand (Pmode, get_avl_type_rtx (avl_type::NONVLMAX));

  if (base->has_rounding_mode_operand_p ())
    add_input_operand (call_expr_nargs (exp) - 2);

  /* The RVV floating-point only support dynamic rounding mode in the
     FRM register.  */
  if (base->may_require_frm_p ()
      && opno < insn_data[icode].n_generator_args)
    add_input_operand (Pmode, gen_int_mode (riscv_vector::FRM_DYN, Pmode));

  return generate_insn (icode);
}

/* Implement the call using instruction ICODE, with a 1:1 mapping between
   arguments and input operands.
   There are operands that cannot be broadcast using v[f]mv.  In that case
   we switch to a strided broadcast.  */

rtx
function_expander::use_widen_ternop_insn (insn_code icode)
{
  /* Record the offset to get the argument.  */
  int arg_offset = 0;

  if (use_real_mask_p (pred))
    add_input_operand (arg_offset++);
  else
    add_all_one_mask_operand (mask_mode ());

  for (int argno = arg_offset; argno < call_expr_nargs (exp); argno++)
    {
      if (base->has_rounding_mode_operand_p ()
	  && argno == call_expr_nargs (exp) - 2)
	{
	  /* Since the rounding mode argument position is not consistent with
	     the instruction pattern, we need to skip rounding mode argument
	     here.  */
	  continue;
	}
      add_input_operand (argno);
    }

  add_input_operand (Pmode, get_tail_policy_for_pred (pred));
  add_input_operand (Pmode, get_mask_policy_for_pred (pred));
  add_input_operand (Pmode, get_avl_type_rtx (avl_type::NONVLMAX));

  if (base->has_rounding_mode_operand_p ())
    add_input_operand (call_expr_nargs (exp) - 2);

  /* The RVV floating-point only support dynamic rounding mode in the
     FRM register.  */
  if (base->may_require_frm_p ()
      && opno < insn_data[icode].n_generator_args)
    add_input_operand (Pmode, gen_int_mode (riscv_vector::FRM_DYN, Pmode));

  return generate_insn (icode);
}

/* Implement the call using instruction ICODE, with a 1:1 mapping between
   arguments and input operands.
   There are operands that cannot be broadcast using v[f]mv.  In that case
   we switch to a strided broadcast.  */

rtx
function_expander::use_scalar_move_insn (insn_code icode)
{
  machine_mode mode = TYPE_MODE (TREE_TYPE (exp));

  /* Record the offset to get the argument.  */
  int arg_offset = 0;
  add_scalar_move_mask_operand (mask_mode ());

  if (use_real_merge_p (pred))
    add_input_operand (arg_offset++);
  else
    add_vundef_operand (mode);

  for (int argno = arg_offset; argno < call_expr_nargs (exp); argno++)
    add_input_operand (argno);

  if (!can_be_broadcast_p (m_ops[3].value))
    icode = code_for_pred_strided_broadcast (vector_mode ());

  add_input_operand (Pmode, get_tail_policy_for_pred (pred));
  add_input_operand (Pmode, get_mask_policy_for_pred (pred));
  add_input_operand (Pmode, get_avl_type_rtx (avl_type::NONVLMAX));
  return generate_insn (icode);
}

/* Implement the call using instruction ICODE, with a 1:1 mapping between
   arguments and input operands.  */
rtx
function_expander::use_scalar_broadcast_insn (insn_code icode)
{
  machine_mode mode = TYPE_MODE (TREE_TYPE (exp));

  /* Record the offset to get the argument.  */
  int arg_offset = 0;
  add_all_one_mask_operand (mask_mode ());

  if (use_real_merge_p (pred))
    add_input_operand (arg_offset++);
  else
    add_vundef_operand (mode);

  for (int argno = arg_offset; argno < call_expr_nargs (exp); argno++)
    add_input_operand (argno);

  if (!can_be_broadcast_p (m_ops[3].value))
    icode = code_for_pred_strided_broadcast (vector_mode ());

  add_input_operand (Pmode, get_tail_policy_for_pred (pred));
  add_input_operand (Pmode, get_mask_policy_for_pred (pred));
  add_input_operand (Pmode, get_avl_type_rtx (avl_type::NONVLMAX));
  return generate_insn (icode);
}

/* Generate instruction ICODE, given that its operands have already
   been added to M_OPS.  Return the value of the first operand.  */
rtx
function_expander::generate_insn (insn_code icode)
{
  gcc_assert (opno == insn_data[icode].n_generator_args);
  if (!maybe_expand_insn (icode, opno, m_ops))
    {
      error ("invalid argument to built-in function");
      return NULL_RTX;
    }
  return function_returns_void_p () ? const0_rtx : m_ops[0].value;
}

function_checker::function_checker (location_t location,
				    const function_instance &instance,
				    tree fndecl, tree fntype,
				    unsigned int nargs, tree *args)
  : function_call_info (location, instance, fndecl), m_fntype (fntype),
    m_nargs (nargs), m_args (args)
{}

/* Report that LOCATION has a call to FNDECL in which argument ARGNO
   was not an integer constant expression.  ARGNO counts from zero.  */
void
function_checker::report_non_ice (unsigned int argno) const
{
  error_at (location,
	    "argument %d of %qE must be an integer constant"
	    " expression",
	    argno + 1, fndecl);
}

/* Report that LOCATION has a call to FNDECL in which argument ARGNO has
   the value ACTUAL, whereas the function requires a value in the range
   [MIN, MAX].  ARGNO counts from zero.  */
void
function_checker::report_out_of_range (unsigned int argno, HOST_WIDE_INT actual,
				       HOST_WIDE_INT min,
				       HOST_WIDE_INT max) const
{
  error_at (location,
	    "passing %wd to argument %d of %qE, which expects"
	    " a value in the range [%wd, %wd]",
	    actual, argno + 1, fndecl, min, max);
}

/* Report that LOCATION has a call to FNDECL in which argument ARGNO has
   the value ACTUAL, whereas the function requires a value in the range
   [MIN, MAX] or OR_VAL.  ARGNO counts from zero.  */
void
function_checker::report_out_of_range_and_not (unsigned int argno,
					       HOST_WIDE_INT actual,
					       HOST_WIDE_INT min,
					       HOST_WIDE_INT max,
					       HOST_WIDE_INT or_val) const
{
  error_at (location,
	    "passing %wd to argument %d of %qE, which expects"
	    " a value in the range [%wd, %wd] or %wd",
	    actual, argno + 1, fndecl, min, max, or_val);
}


/* Check that argument ARGNO is an integer constant expression and
   store its value in VALUE_OUT if so.  The caller should first
   check that argument ARGNO exists.  */
bool
function_checker::require_immediate (unsigned int argno, HOST_WIDE_INT min,
				     HOST_WIDE_INT max) const
{
  gcc_assert (argno < m_nargs);
  tree arg = m_args[argno];

  /* The type and range are unsigned, so read the argument as an
     unsigned rather than signed HWI.  */
  if (!tree_fits_uhwi_p (arg))
    {
      report_non_ice (argno);
      return false;
    }
  return require_immediate_range (argno, min, max);
}

/* Check that argument REL_ARGNO is an integer constant expression in the
   range [MIN, MAX].  REL_ARGNO counts from the end of the predication
   arguments.  */
bool
function_checker::require_immediate_range (unsigned int argno,
					   HOST_WIDE_INT min,
					   HOST_WIDE_INT max) const
{
  gcc_assert (argno < m_nargs);
  tree arg = m_args[argno];
  HOST_WIDE_INT actual = tree_to_uhwi (arg);

  if (!IN_RANGE (actual, min, max))
    {
      report_out_of_range (argno, actual, min, max);
      return false;
    }

  return true;
}

/* Check that argument REL_ARGNO is an integer constant expression in the
   range [MIN, MAX] or OR_VAL.  REL_ARGNO counts from the end of the
   predication arguments.  */
bool
function_checker::require_immediate_range_or (unsigned int argno,
					      HOST_WIDE_INT min,
					      HOST_WIDE_INT max,
					      HOST_WIDE_INT or_val) const
{
  gcc_assert (min >= 0 && min <= max);
  gcc_assert (argno < m_nargs);

  tree arg = m_args[argno];
  HOST_WIDE_INT actual = tree_to_uhwi (arg);

  if (!IN_RANGE (actual, min, max) && actual != or_val)
    {
      report_out_of_range_and_not (argno, actual, min, max, or_val);
      return false;
    }

  return true;
}

/* Perform semantic checks on the call.  Return true if the call is valid,
   otherwise report a suitable error.  */
bool
function_checker::check ()
{
  return shape->check (*this);
}

inline hashval_t
registered_function_hasher::hash (value_type value)
{
  return value->instance.hash ();
}

inline bool
registered_function_hasher::equal (value_type value, const compare_type &key)
{
  return value->instance == key;
}

hashval_t
registered_function::overloaded_hash () const
{
  inchash::hash h;
  tree type;
  h.add (overload_name, strlen (overload_name));
  for (unsigned int i = 0; i < argument_types.length (); i++)
    {
      type = argument_types[i];

      /* If we're passed something entirely unreasonable, just ignore here.
	 We'll warn later anyway.  */
      if (TREE_CODE_CLASS (TREE_CODE (type)) != tcc_type)
	continue;

      /* Discriminate by the per-DEF_RVV_TYPE TYPE_NAME.  Hashing only
	 on (mode, unsigned_p) -- as the original code did -- collapses
	 several genuinely-distinct RVV types onto the same slot:
	 vuint8m1_t, vfloat8e4m3m1_t, vfloat8e5m2m1_t, vfloat4e2m1m1_t
	 and vuint4m1_t are all (RVVM1QI, unsigned) at the GCC level
	 AND share the same underlying VECTOR_TYPE tree (all built from
	 the same `uint8' element type), so even TYPE_CANONICAL aliases
	 them.  The disambiguator that survives is TYPE_NAME, which
	 each DEF_RVV_TYPE entry sets to its own unique TYPE_DECL
	 (e.g. `__rvv_float8e4m3m1_t' vs `__rvv_uint8m1_t').

	 Fall back through TYPE_CANONICAL and TYPE_MAIN_VARIANT for
	 non-RVV types whose TYPE_NAME may be NULL.  Mode is kept in
	 the mix as a cheap second discriminator.  */
      tree disc = POINTER_TYPE_P (type) ? TREE_TYPE (type) : type;
      tree mv = TYPE_MAIN_VARIANT (disc);
      tree key = TYPE_NAME (mv);
      if (!key)
	key = TYPE_CANONICAL (mv);
      if (!key)
	key = mv;
      machine_mode mode_p = TYPE_MODE (disc);

      if (POINTER_TYPE_P (type) || lookup_vector_type_attribute (disc))
	{
	  h.add_hwi ((HOST_WIDE_INT) (uintptr_t) key);
	  h.add_int ((int) mode_p);
	}
      else if (instance.base->may_require_vxrm_p ()
	       || instance.base->may_require_frm_p ())
	{
	  h.add_int (argument_types.length ());
	  break;
	}
    }

  return h.end ();
}

hashval_t
registered_function::overloaded_hash (const vec<tree, va_gc> &arglist)
{
  argument_types = vNULL;
  unsigned int len = arglist.length ();

  for (unsigned int i = 0; i < len; i++)
    argument_types.safe_push (TREE_TYPE (arglist[i]));

  return overloaded_hash ();
}

inline hashval_t
non_overloaded_registered_function_hasher::hash (value_type value)
{
  return value->overloaded_hash ();
}

inline bool
non_overloaded_registered_function_hasher::equal (value_type value,
						  const compare_type &key)
{
  /* Two registered functions are the same overload iff they share
     overload_name AND every argument the hash function considered
     matches by canonical identity.  This must mirror overloaded_hash
     exactly so the hash-table consistency invariant ("equal => same
     hash") holds.

     overloaded_hash's gating:
       * Only pointer / vector args contribute -- canonical-type
	 pointer is the discriminator.
       * Scalars contribute nothing UNLESS the op is FRM/VXRM-sensitive,
	 in which case the hash adds the arglist length and stops.
       * No length is added for non-FRM ops, so two arglists with the
	 same sequence of vector/pointer args but different scalar
	 trailers hash identically -- and must compare equal.

     This is what the user-side overload-resolution path relies on: a
     short user call like __riscv_vand(vec, vl) -- 2 args -- must
     resolve to e.g. vand_vx_i32m1 -- 3 args -- so the front end can
     emit a meaningful "too few arguments" diagnostic against the
     concrete intrinsic.

     We iterate the longer of the two arglists.  Any "extra" arg on
     the longer side must be a non-discriminating scalar (no
     vector/pointer attribute) for non-FRM ops; an extra
     vector/pointer arg means the hashes legitimately differ and the
     two functions are not equal.  */
  if (strcmp (value->overload_name, key->overload_name) != 0)
    return false;
  unsigned int v_len = value->argument_types.length ();
  unsigned int k_len = key->argument_types.length ();
  unsigned int n = std::max (v_len, k_len);
  bool frm_op = (value->instance.base->may_require_vxrm_p ()
		 || value->instance.base->may_require_frm_p ());
  for (unsigned int i = 0; i < n; i++)
    {
      tree a = (i < v_len) ? value->argument_types[i] : NULL_TREE;
      tree b = (i < k_len) ? key->argument_types[i] : NULL_TREE;

      /* If only one side has an arg here, the other arglist is
	 shorter.  Hashes will agree only if the extra arg contributes
	 nothing to the hash (i.e. a scalar on a non-FRM op).  */
      if (!a || !b)
	{
	  tree extra = a ? a : b;
	  if (TREE_CODE_CLASS (TREE_CODE (extra)) != tcc_type)
	    continue;
	  tree xd = POINTER_TYPE_P (extra) ? TREE_TYPE (extra) : extra;
	  if (POINTER_TYPE_P (extra) || lookup_vector_type_attribute (xd))
	    return false;  /* Vector/pointer -- hash differs.  */
	  if (frm_op)
	    return false;  /* Hash adds length; lengths differ here.  */
	  continue;	   /* Scalar on non-FRM op -- ignored by hash.  */
	}

      if (TREE_CODE_CLASS (TREE_CODE (a)) != tcc_type
	  || TREE_CODE_CLASS (TREE_CODE (b)) != tcc_type)
	continue;
      if (POINTER_TYPE_P (a) != POINTER_TYPE_P (b))
	return false;
      tree ad = POINTER_TYPE_P (a) ? TREE_TYPE (a) : a;
      tree bd = POINTER_TYPE_P (b) ? TREE_TYPE (b) : b;
      bool a_disc = POINTER_TYPE_P (a) || lookup_vector_type_attribute (ad);
      bool b_disc = POINTER_TYPE_P (b) || lookup_vector_type_attribute (bd);
      if (a_disc != b_disc)
	return false;  /* One is a discriminator, the other isn't.  */
      if (!a_disc)
	{
	  if (frm_op)
	    return v_len == k_len;  /* Hash broke after len.  */
	  continue;
	}
      tree am = TYPE_MAIN_VARIANT (ad);
      tree bm = TYPE_MAIN_VARIANT (bd);
      tree ak = TYPE_NAME (am);
      if (!ak) ak = TYPE_CANONICAL (am);
      if (!ak) ak = am;
      tree bk = TYPE_NAME (bm);
      if (!bk) bk = TYPE_CANONICAL (bm);
      if (!bk) bk = bm;
      if (ak != bk)
	return false;
    }
  return true;
}

/* If TYPE is a built-in type defined by the RVV ABI, return the mangled name,
   otherwise return NULL.  */
const char *
mangle_builtin_type (const_tree type)
{
  if (TYPE_NAME (type) && TREE_CODE (TYPE_NAME (type)) == TYPE_DECL)
    type = TREE_TYPE (TYPE_NAME (type));
  if (tree attr = lookup_vector_type_attribute (type))
    if (tree id = TREE_VALUE (chain_index (0, TREE_VALUE (attr))))
      return IDENTIFIER_POINTER (id);
  return NULL;
}

/* Return true if TYPE is a built-in RVV type defined by the ABI.  */
bool
builtin_type_p (const_tree type)
{
  if (!type)
    return false;

  return lookup_vector_type_attribute (type);
}

/* Initialize all compiler built-ins related to RVV that should be
   defined at start-up.  */
void
init_builtins ()
{
  if (in_lto_p)
    /* "pragma vector" will register type during the process. */
    handle_pragma_vector ();
  else
    {
      if (!TARGET_VECTOR)
	return;
      register_builtin_types ();
    }
}

/* Implement TARGET_VERIFY_TYPE_CONTEXT for RVV types.  */
bool
verify_type_context (location_t loc, type_context_kind context, const_tree type,
		     bool silent_p)
{
  const_tree tmp = type;
  if (omp_type_context (context) && POINTER_TYPE_P (type))
    tmp = strip_pointer_types (tmp);

  if (!sizeless_type_p (tmp))
    return true;

  switch (context)
    {
    case TCTX_SIZEOF:
    case TCTX_STATIC_STORAGE:
      if (!silent_p)
	error_at (loc, "RVV type %qT does not have a fixed size", type);

      return false;

    case TCTX_ALIGNOF:
      if (!silent_p)
	error_at (loc, "RVV type %qT does not have a defined alignment", type);

      return false;

    case TCTX_THREAD_STORAGE:
      if (!silent_p)
	error_at (loc,
		  "variables of type %qT cannot have thread-local"
		  " storage duration",
		  type);

      return false;

    case TCTX_POINTER_ARITH:
      if (!silent_p)
	error_at (loc, "arithmetic on pointer to RVV type %qT", type);

      return false;

    case TCTX_FIELD:
      if (silent_p)
	;
      else if (lang_GNU_CXX ())
	error_at (loc, "member variables cannot have RVV type %qT", type);
      else
	error_at (loc, "fields cannot have RVV type %qT", type);

      return false;

    case TCTX_ARRAY_ELEMENT:
      if (!silent_p)
	error_at (loc, "array elements cannot have RVV type %qT", type);

      return false;

    case TCTX_ALLOCATION:
      if (!silent_p)
	error_at (loc, "cannot allocate objects with RVV type %qT", type);

      return false;

    case TCTX_DEALLOCATION:
      if (!silent_p)
	error_at (loc, "cannot delete objects with RVV type %qT", type);

      return false;

    case TCTX_EXCEPTIONS:
      if (!silent_p)
	error_at (loc, "cannot throw or catch RVV type %qT", type);

      return false;

    case TCTX_CAPTURE_BY_COPY:
      if (!silent_p)
	error_at (loc, "capture by copy of RVV type %qT", type);

      return false;

    case TCTX_OMP_MAP:
      if (!silent_p)
	error_at (loc, "RVV type %qT not allowed in %<map%> clause", type);
      return false;

    case TCTX_OMP_MAP_IMP_REF:
      if (!silent_p)
	error ("cannot reference %qT object types in %<target%> region", type);
      return false;

    case TCTX_OMP_PRIVATE:
      if (!silent_p)
	error_at (loc, "RVV type %qT not allowed in"
		  " %<target%> %<private%> clause", type);
      return false;

    case TCTX_OMP_FIRSTPRIVATE:
      if (!silent_p)
	error_at (loc, "RVV type %qT not allowed in"
		  " %<target%> %<firstprivate%> clause", type);
      return false;

    case TCTX_OMP_DEVICE_ADDR:
      if (!silent_p)
	error_at (loc, "RVV type %qT not allowed in"
		  " %<target%> device clauses", type);
      return false;
    }

  gcc_unreachable ();
}

/* Register the vxrm enum.  */
static void
register_vxrm ()
{
  auto_vec<string_int_pair, 4> values;
#define DEF_RVV_VXRM_ENUM(NAME, VALUE)                                          \
  values.quick_push (string_int_pair ("__RISCV_VXRM_" #NAME, VALUE));
#include "riscv-vector-builtins.def"
#undef DEF_RVV_VXRM_ENUM

  lang_hooks.types.simulate_enum_decl (input_location, "__RISCV_VXRM", &values);
}

/* Register the frm enum.  */
static void
register_frm ()
{
  auto_vec<string_int_pair, 5> values;
#define DEF_RVV_FRM_ENUM(NAME, VALUE)                                          \
  values.quick_push (string_int_pair ("__RISCV_FRM_" #NAME, VALUE));
#include "riscv-vector-builtins.def"
#undef DEF_RVV_FRM_ENUM

  lang_hooks.types.simulate_enum_decl (input_location, "__RISCV_FRM", &values);
}

/* Implement #pragma riscv intrinsic vector.  */
void
handle_pragma_vector ()
{
  if (function_table || non_overloaded_function_table)
    {
      error ("duplicate definition of %qs", "riscv_vector.h");
      return;
    }
  rvv_switcher rvv;

  /* Define the vector and tuple types.  */
  for (unsigned int type_i = 0; type_i < NUM_VECTOR_TYPES; ++type_i)
    register_vector_type ((enum vector_type_index) type_i);

  /* Define the enums.  */
  register_vxrm ();
  register_frm ();

  /* Define the functions.  */
  function_table = new hash_table<registered_function_hasher> (1023);
  function_builder builder;
  for (unsigned int i = 0; i < ARRAY_SIZE (function_groups); ++i)
    builder.register_function_group (function_groups[i]);
}

/* Find the registered_function with the given subcode (code without
   the class bit), or return NULL.  */
static registered_function *
lookup_registered_function (unsigned int subcode)
{
  unsigned int partition = subcode & ((1u << RVV_EXT_PARTITION_BITS) - 1);
  unsigned int index = subcode >> RVV_EXT_PARTITION_BITS;

  if (partition >= NUM_RVV_EXT_PARTITIONS)
    return NULL;

  vec<registered_function *, va_gc> *funcs = partition_functions[partition];
  if (!funcs || index >= funcs->length ())
    return NULL;

  return (*funcs)[index];
}

/* Return the function decl with RVV function subcode CODE, or error_mark_node
   if no such function exists.  */
tree
builtin_decl (unsigned int code, bool)
{
  registered_function *rfn = lookup_registered_function (code);
  if (!rfn)
    return error_mark_node;

  return rfn->decl;
}

/* Attempt to fold STMT, given that it's a call to the RVV function
   with subcode CODE.  Return the new statement on success and null
   on failure.  Insert any other new statements at GSI.  */
gimple *
gimple_fold_builtin (unsigned int code, gimple_stmt_iterator *gsi, gcall *stmt)
{
  registered_function *rfn = lookup_registered_function (code);
  if (!rfn)
    return NULL;
  return gimple_folder (rfn->instance, rfn->decl, gsi, stmt).fold ();
}

static bool
validate_instance_type_required_extensions (const rvv_type_info type,
					    tree exp)
{
  uint64_t exts = type.required_extensions;

  if ((exts & RVV_REQUIRE_ELEN_BF_16)
      && !TARGET_VECTOR_ELEN_BF_16_P (riscv_vector_elen_flags))
    {
      error_at (EXPR_LOCATION (exp),
		"built-in function %qE requires the "
		"zvfbfmin or zvfbfwma ISA extension",
		exp);
      return false;
    }

  if ((exts & RVV_REQUIRE_ELEN_FP_16)
    && !TARGET_VECTOR_ELEN_FP_16_P (riscv_vector_elen_flags))
    {
      error_at (EXPR_LOCATION (exp),
		"built-in function %qE requires the "
		"zvfhmin or zvfh ISA extension",
		exp);
      return false;
    }

  if ((exts & RVV_REQUIRE_ELEN_FP_32)
    && !TARGET_VECTOR_ELEN_FP_32_P (riscv_vector_elen_flags))
    {
      error_at (EXPR_LOCATION (exp),
		"built-in function %qE requires the "
		"zve32f, zve64f, zve64d or v ISA extension",
		exp);
      return false;
    }

  if ((exts & RVV_REQUIRE_ELEN_FP_64)
    && !TARGET_VECTOR_ELEN_FP_64_P (riscv_vector_elen_flags))
    {
      error_at (EXPR_LOCATION (exp),
		"built-in function %qE requires the zve64d or v ISA extension",
		exp);
      return false;
    }

  if ((exts & RVV_REQUIRE_ELEN_64)
    && !TARGET_VECTOR_ELEN_64_P (riscv_vector_elen_flags))
    {
      error_at (EXPR_LOCATION (exp),
		"built-in function %qE requires the "
		"zve64x, zve64f, zve64d or v ISA extension",
		exp);
      return false;
    }

  return true;
}

/* Expand a call to the RVV function with subcode CODE.  EXP is the call
   expression and TARGET is the preferred location for the result.
   Return the value of the lhs.  */
rtx
expand_builtin (unsigned int code, tree exp, rtx target)
{
  registered_function *rfn = lookup_registered_function (code);
  if (!rfn)
    {
      error_at (EXPR_LOCATION (exp), "unrecognized RVV builtin");
      return target;
    }

  if (!required_extensions_specified (rfn->required))
    {
      error_at (EXPR_LOCATION (exp),
		"built-in function %qE requires the %qs ISA extension",
		exp,
		required_ext_to_isa_name (rfn->required));
      return target;
    }

  if (!validate_instance_type_required_extensions (rfn->instance.type, exp))
    return target;

  return function_expander (rfn->instance, rfn->decl, exp, target).expand ();
}

/* Perform any semantic checks needed for a call to the RVV function
   with subcode CODE, such as testing for integer constant expressions.
   The call occurs at location LOCATION and has NARGS arguments,
   given by ARGS.  FNDECL is the original function decl, before
   overload resolution.

   Return true if the call is valid, otherwise report a suitable error.  */
bool
check_builtin_call (location_t location, vec<location_t>, unsigned int code,
		    tree fndecl, unsigned int nargs, tree *args)
{
  registered_function *rfn = lookup_registered_function (code);
  if (!rfn)
    return false;
  return function_checker (location, rfn->instance, fndecl,
			   TREE_TYPE (rfn->decl), nargs, args).check ();
}

tree
resolve_overloaded_builtin (location_t loc, unsigned int code, tree fndecl,
			    vec<tree, va_gc> *arglist)
{
  registered_function *rfun = lookup_registered_function (code);

  if (!rfun || !rfun->overloaded_p)
    return NULL_TREE;

  /* According to the rvv intrinsic doc, we have no such overloaded function
     with empty args.  Unfortunately, we register the empty args function as
     overloaded for avoiding conflict.  Thus, there will actual one register
     function after return NULL_TREE back to the middle-end, and finally result
     in ICE when expanding.  For example:

     1. First we registered void __riscv_vfredmax () as the overloaded function.
     2. Then resolve_overloaded_builtin (this func) return NULL_TREE.
     3. The functions register in step 1 bypass the args check as empty args.
     4. Finally, fall into expand_builtin with empty args and meet ICE.

     Here we report error when overloaded function with empty args.  */
  if (rfun->overloaded_p && arglist->length () == 0)
    error_at (loc, "no matching function call to %qE with empty arguments",
	      fndecl);

  hashval_t hash = rfun->overloaded_hash (*arglist);
  registered_function *rfn
    = non_overloaded_function_table->find_with_hash (rfun, hash);

  return rfn ? rfn->decl : NULL_TREE;
}

function_instance
get_read_vl_instance (void)
{
  return function_instance ("read_vl", bases::read_vl, shapes::read_vl,
			    none_ops[0], PRED_TYPE_none, &p_none_void_ops);
}

tree
get_read_vl_decl (void)
{
  function_instance instance = get_read_vl_instance ();
  hashval_t hash = instance.hash ();
  registered_function *rfn = function_table->find_with_hash (instance, hash);
  gcc_assert (rfn);
  return rfn->decl;
}

} // end namespace riscv_vector

inline void
gt_ggc_mx (function_instance *)
{}

inline void
gt_pch_nx (function_instance *)
{}

inline void
gt_pch_nx (function_instance *, gt_pointer_operator, void *)
{}

#include "gt-riscv-vector-builtins.h"
