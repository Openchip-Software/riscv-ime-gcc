/* function_base implementation for RISC-V 'V' Extension for GNU compiler.
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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "memmodel.h"
#include "diagnostic-core.h"
#include "insn-codes.h"
#include "optabs.h"
#include "recog.h"
#include "expr.h"
#include "basic-block.h"
#include "function.h"
#include "cfg.h"
#include "fold-const.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimplify.h"
#include "bitmap.h"
#include "ssa.h"
#include "explow.h"
#include "emit-rtl.h"
#include "tree-vector-builder.h"
#include "rtx-vector-builder.h"
#include "riscv-vector-builtins.h"
#include "riscv-vector-builtins-shapes.h"
#include "riscv-vector-builtins-bases.h"
#include "riscv-v.h"

using namespace riscv_vector;

namespace riscv_vector {

/* Enumerates types of loads/stores operations.
   It's only used in here so we don't define it
   in riscv-vector-builtins-bases.h.  */
enum lst_type
{
  LST_UNIT_STRIDE,
  LST_STRIDED,
  LST_INDEXED,
};

/* Implements vsetvl<mode> && vsetvlmax<mode>.  */
template<bool VLMAX_P>
class vsetvl : public function_base
{
public:
  bool apply_vl_p () const override
  {
    return false;
  }

  rtx expand (function_expander &e) const override
  {
    if (VLMAX_P)
      e.add_input_operand (Pmode, gen_rtx_REG (Pmode, 0));
    else
      e.add_input_operand (0);

    tree type = builtin_types[e.type.index].vector;
    machine_mode mode = TYPE_MODE (type);

    if (TARGET_XTHEADVECTOR)
      {
	machine_mode inner_mode = GET_MODE_INNER (mode);
	/* SEW.  */
	e.add_input_operand (Pmode,
	  gen_int_mode (GET_MODE_BITSIZE (inner_mode), Pmode));
	/* LMUL.  */
	e.add_input_operand (Pmode,
	  gen_int_mode (get_vlmul (mode), Pmode));
      }
    else
      {
	/* Normalize same RATO (SEW/LMUL) into same vsetvl instruction.

	     - e8,mf8/e16,mf4/e32,mf2/e64,m1 --> e8mf8
	     - e8,mf4/e16,mf2/e32,m1/e64,m2  --> e8mf4
	     - e8,mf2/e16,m1/e32,m2/e64,m4   --> e8mf2
	     - e8,m1/e16,m2/e32,m4/e64,m8    --> e8m1
	     - e8,m2/e16,m4/e32,m8           --> e8m2
	     - e8,m4/e16,m8                  --> e8m4
	     - e8,m8                         --> e8m8
	*/
	/* SEW.  */
	e.add_input_operand (Pmode, gen_int_mode (8, Pmode));

	/* LMUL.  */
	machine_mode e8_mode
	  = get_vector_mode (QImode, GET_MODE_NUNITS (mode)).require ();
	e.add_input_operand (Pmode, gen_int_mode (get_vlmul (e8_mode), Pmode));
      }

    /* TAIL_ANY.  */
    e.add_input_operand (Pmode,
			 gen_int_mode (get_prefer_tail_policy (), Pmode));

    /* MASK_ANY.  */
    e.add_input_operand (Pmode,
			 gen_int_mode (get_prefer_mask_policy (), Pmode));
    return e.generate_insn (code_for_vsetvl_no_side_effects (Pmode));
  }
};

/* Implements
 * vle.v/vse.v/vlm.v/vsm.v/vlse.v/vsse.v/vluxei.v/vloxei.v/vsuxei.v/vsoxei.v
 * codegen.  */
template<bool STORE_P, lst_type LST_TYPE, bool ORDERED_P>
class loadstore : public function_base
{
public:
  bool apply_tail_policy_p () const override { return !STORE_P; }
  bool apply_mask_policy_p () const override { return !STORE_P; }

  unsigned int call_properties (const function_instance &) const override
  {
    if (STORE_P)
      return CP_WRITE_MEMORY;
    else
      return CP_READ_MEMORY;
  }

  bool can_be_overloaded_p (enum predication_type_index pred) const override
  {
    if (STORE_P || LST_TYPE == LST_INDEXED)
      return true;
    return pred != PRED_TYPE_none;
  }

  rtx expand (function_expander &e) const override
  {
    if (LST_TYPE == LST_INDEXED)
      {
	int unspec = ORDERED_P ? UNSPEC_ORDERED : UNSPEC_UNORDERED;
	if (STORE_P)
	  {
	    unsigned src_eew_bitsize
	      = GET_MODE_BITSIZE (GET_MODE_INNER (e.index_mode ()));
	    unsigned dst_eew_bitsize
	      = GET_MODE_BITSIZE (GET_MODE_INNER (e.vector_mode ()));
	    if (dst_eew_bitsize == src_eew_bitsize)
	      return e.use_exact_insn (
		code_for_pred_indexed_store_same_eew (unspec, e.vector_mode ()));
	    else if (dst_eew_bitsize > src_eew_bitsize)
	      {
		unsigned factor = dst_eew_bitsize / src_eew_bitsize;
		switch (factor)
		  {
		  case 2:
		    return e.use_exact_insn (
		      code_for_pred_indexed_store_x2_greater_eew (
			unspec, e.vector_mode ()));
		  case 4:
		    return e.use_exact_insn (
		      code_for_pred_indexed_store_x4_greater_eew (
			unspec, e.vector_mode ()));
		  case 8:
		    return e.use_exact_insn (
		      code_for_pred_indexed_store_x8_greater_eew (
			unspec, e.vector_mode ()));
		  default:
		    gcc_unreachable ();
		  }
	      }
	    else
	      {
		unsigned factor = src_eew_bitsize / dst_eew_bitsize;
		switch (factor)
		  {
		  case 2:
		    return e.use_exact_insn (
		      code_for_pred_indexed_store_x2_smaller_eew (
			unspec, e.vector_mode ()));
		  case 4:
		    return e.use_exact_insn (
		      code_for_pred_indexed_store_x4_smaller_eew (
			unspec, e.vector_mode ()));
		  case 8:
		    return e.use_exact_insn (
		      code_for_pred_indexed_store_x8_smaller_eew (
			unspec, e.vector_mode ()));
		  default:
		    gcc_unreachable ();
		  }
	      }
	  }
	else
	  {
	    unsigned src_eew_bitsize
	      = GET_MODE_BITSIZE (GET_MODE_INNER (e.index_mode ()));
	    unsigned dst_eew_bitsize
	      = GET_MODE_BITSIZE (GET_MODE_INNER (e.vector_mode ()));
	    if (dst_eew_bitsize == src_eew_bitsize)
	      return e.use_exact_insn (
		code_for_pred_indexed_load_same_eew (unspec, e.vector_mode ()));
	    else if (dst_eew_bitsize > src_eew_bitsize)
	      {
		unsigned factor = dst_eew_bitsize / src_eew_bitsize;
		switch (factor)
		  {
		  case 2:
		    return e.use_exact_insn (
		      code_for_pred_indexed_load_x2_greater_eew (
			unspec, e.vector_mode ()));
		  case 4:
		    return e.use_exact_insn (
		      code_for_pred_indexed_load_x4_greater_eew (
			unspec, e.vector_mode ()));
		  case 8:
		    return e.use_exact_insn (
		      code_for_pred_indexed_load_x8_greater_eew (
			unspec, e.vector_mode ()));
		  default:
		    gcc_unreachable ();
		  }
	      }
	    else
	      {
		unsigned factor = src_eew_bitsize / dst_eew_bitsize;
		switch (factor)
		  {
		  case 2:
		    return e.use_exact_insn (
		      code_for_pred_indexed_load_x2_smaller_eew (
			unspec, e.vector_mode ()));
		  case 4:
		    return e.use_exact_insn (
		      code_for_pred_indexed_load_x4_smaller_eew (
			unspec, e.vector_mode ()));
		  case 8:
		    return e.use_exact_insn (
		      code_for_pred_indexed_load_x8_smaller_eew (
			unspec, e.vector_mode ()));
		  default:
		    gcc_unreachable ();
		  }
	      }
	  }
      }
    else if (LST_TYPE == LST_STRIDED)
      {
	if (STORE_P)
	  return e.use_contiguous_store_insn (
	    code_for_pred_strided_store (e.vector_mode ()));
	else
	  return e.use_contiguous_load_insn (
	    code_for_pred_strided_load (e.vector_mode ()));
      }
    else
      {
	if (STORE_P)
	  return e.use_contiguous_store_insn (
	    code_for_pred_store (e.vector_mode ()));
	else
	  return e.use_contiguous_load_insn (
	    code_for_pred_mov (e.vector_mode ()));
      }
  }
};

/* Implements
   vadd/vsub/vand/vor/vxor/vsll/vsra/vsrl/
   vmin/vmax/vminu/vmaxu/vdiv/vrem/vdivu/
   vremu/vsadd/vsaddu/vssub/vssubu
   vfadd/vfsub/
*/
template <rtx_code CODE, bool MAY_REQUIRE_FRM = false,
	  enum frm_op_type FRM_OP = NO_FRM>
class binop : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return MAY_REQUIRE_FRM; }

  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vx:
	gcc_assert (FRM_OP == NO_FRM);
      case OP_TYPE_vf:
	return e.use_exact_insn (code_for_pred_scalar (CODE, e.vector_mode ()));
      case OP_TYPE_vv:
	return e.use_exact_insn (code_for_pred (CODE, e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vrsub.  */
class vrsub : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (
      code_for_pred_sub_reverse_scalar (e.vector_mode ()));
  }
};

/* Implements vneg/vnot.  */
template<rtx_code CODE, enum frm_op_type FRM_OP = NO_FRM>
class unop : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred (CODE, e.vector_mode ()));
  }
};

/* Implements vsext.vf2/vsext.vf4/vsext.vf8/vzext.vf2/vzext.vf4/vzext.vf8.  */
template<rtx_code CODE>
class ext : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vf2:
	return e.use_exact_insn (code_for_pred_vf2 (CODE, e.vector_mode ()));
      case OP_TYPE_vf4:
	return e.use_exact_insn (code_for_pred_vf4 (CODE, e.vector_mode ()));
      case OP_TYPE_vf8:
	return e.use_exact_insn (code_for_pred_vf8 (CODE, e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vmulh/vmulhu/vmulhsu.  */
template<int UNSPEC>
class vmulh : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vx:
	return e.use_exact_insn (
	  code_for_pred_mulh_scalar (UNSPEC, e.vector_mode ()));
      case OP_TYPE_vv:
	return e.use_exact_insn (
	  code_for_pred_mulh (UNSPEC, e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vwadd/vwsub/vwmul.  */
template<rtx_code CODE1, rtx_code CODE2 = FLOAT_EXTEND>
class widen_binop : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vv:
	return e.use_exact_insn (
	  code_for_pred_dual_widen (CODE1, CODE2, e.vector_mode ()));
      case OP_TYPE_vx:
	return e.use_exact_insn (
	  code_for_pred_dual_widen_scalar (CODE1, CODE2, e.vector_mode ()));
      case OP_TYPE_wv:
	if (CODE1 == PLUS)
	  return e.use_exact_insn (
	    code_for_pred_single_widen_add (CODE2, e.vector_mode ()));
	else
	  return e.use_exact_insn (
	    code_for_pred_single_widen_sub (CODE2, e.vector_mode ()));
      case OP_TYPE_wx:
	return e.use_exact_insn (
	  code_for_pred_single_widen_scalar (CODE1, CODE2, e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implement vfwadd/vfwsub/vfwmul.  */
template<rtx_code CODE, enum frm_op_type FRM_OP = NO_FRM>
class widen_binop_fp : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vv:
	return e.use_exact_insn (
	  code_for_pred_dual_widen (CODE, e.vector_mode ()));
      case OP_TYPE_vf:
	return e.use_exact_insn (
	  code_for_pred_dual_widen_scalar (CODE, e.vector_mode ()));
      case OP_TYPE_wv:
	if (CODE == PLUS)
	  return e.use_exact_insn (
	    code_for_pred_single_widen_add (e.vector_mode ()));
	else
	  return e.use_exact_insn (
	    code_for_pred_single_widen_sub (e.vector_mode ()));
      case OP_TYPE_wf:
	return e.use_exact_insn (
	  code_for_pred_single_widen_scalar (CODE, e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vwmulsu.  */
class vwmulsu : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vv:
	return e.use_exact_insn (code_for_pred_widen_mulsu (e.vector_mode ()));
      case OP_TYPE_vx:
	return e.use_exact_insn (
	  code_for_pred_widen_mulsu_scalar (e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vwcvt.  */
template<rtx_code CODE>
class vwcvt : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred (CODE, e.vector_mode ()));
  }
};

/* Implements vadc.  */
class vadc : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vvm:
	return e.use_exact_insn (code_for_pred_adc (e.vector_mode ()));
      case OP_TYPE_vxm:
	return e.use_exact_insn (code_for_pred_adc_scalar (e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vsbc.  */
class vsbc : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vvm:
	return e.use_exact_insn (code_for_pred_sbc (e.vector_mode ()));
      case OP_TYPE_vxm:
	return e.use_exact_insn (code_for_pred_sbc_scalar (e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vmadc.  */
class vmadc : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vvm:
	return e.use_exact_insn (code_for_pred_madc (e.vector_mode ()));
      case OP_TYPE_vxm:
	return e.use_exact_insn (code_for_pred_madc_scalar (e.vector_mode ()));
      case OP_TYPE_vv:
	return e.use_exact_insn (
	  code_for_pred_madc_overflow (e.vector_mode ()));
      case OP_TYPE_vx:
	return e.use_exact_insn (
	  code_for_pred_madc_overflow_scalar (e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vmsbc.  */
class vmsbc : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vvm:
	return e.use_exact_insn (code_for_pred_msbc (e.vector_mode ()));
      case OP_TYPE_vxm:
	return e.use_exact_insn (code_for_pred_msbc_scalar (e.vector_mode ()));
      case OP_TYPE_vv:
	return e.use_exact_insn (
	  code_for_pred_msbc_overflow (e.vector_mode ()));
      case OP_TYPE_vx:
	return e.use_exact_insn (
	  code_for_pred_msbc_overflow_scalar (e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vnsrl/vnsra.  */
template<rtx_code CODE>
class vnshift : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_wx:
	return e.use_exact_insn (
	  code_for_pred_narrow_scalar (CODE, e.vector_mode ()));
      case OP_TYPE_wv:
	return e.use_exact_insn (code_for_pred_narrow (CODE, e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vncvt.  */
class vncvt_x : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_trunc (e.vector_mode ()));
  }
};

/* Implements vmerge/vfmerge.  */
class vmerge : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vvm:
	return e.use_exact_insn (code_for_pred_merge (e.vector_mode ()));
      case OP_TYPE_vxm:
      case OP_TYPE_vfm:
	return e.use_exact_insn (code_for_pred_merge_scalar (e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vmv.v.x/vmv.v.v/vfmv.v.f.  */
class vmv_v : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_v:
	return e.use_exact_insn (code_for_pred_mov (e.vector_mode ()));
      case OP_TYPE_x:
      case OP_TYPE_f:
	return e.use_scalar_broadcast_insn
	  (code_for_pred_broadcast (e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vaadd/vasub/vsmul/vssra/vssrl.  */
template<int UNSPEC>
class sat_op : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override { return true; }

  bool may_require_vxrm_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vx:
	return e.use_exact_insn (
	  code_for_pred_scalar (UNSPEC, e.vector_mode ()));
      case OP_TYPE_vv:
	return e.use_exact_insn (code_for_pred (UNSPEC, e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vnclip/vnclipu.  */
template<int UNSPEC>
class vnclip : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override { return true; }

  bool may_require_vxrm_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_wx:
	return e.use_exact_insn (
	  code_for_pred_narrow_clip_scalar (UNSPEC, e.vector_mode ()));
      case OP_TYPE_wv:
	return e.use_exact_insn (
	  code_for_pred_narrow_clip (UNSPEC, e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vmseq/vmsne/vmslt/vmsgt/vmsle/vmsge.  */
template<rtx_code CODE>
class icmp : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
	case OP_TYPE_vx: {
	  if (CODE == GE || CODE == GEU)
	    return e.use_compare_insn (CODE, code_for_pred_ge_scalar (
					       e.vector_mode ()));
	  else
	    return e.use_compare_insn (CODE, code_for_pred_cmp_scalar (
					       e.vector_mode ()));
	}
	case OP_TYPE_vv: {
	  if (CODE == LT || CODE == LTU || CODE == GE || CODE == GEU)
	    return e.use_compare_insn (CODE,
				       code_for_pred_ltge (e.vector_mode ()));
	  else
	    return e.use_compare_insn (CODE,
				       code_for_pred_cmp (e.vector_mode ()));
	}
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vmacc/vnmsac/vmadd/vnmsub.  */
class vmacc : public function_base
{
public:
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vx)
      return e.use_ternop_insn (true, code_for_pred_mul_plus_scalar (
					e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_ternop_insn (true,
				code_for_pred_mul_plus (e.vector_mode ()));
    gcc_unreachable ();
  }
};

class vnmsac : public function_base
{
public:
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vx)
      return e.use_ternop_insn (true, code_for_pred_minus_mul_scalar (
					e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_ternop_insn (true,
				code_for_pred_minus_mul (e.vector_mode ()));
    gcc_unreachable ();
  }
};

class vmadd : public function_base
{
public:
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vx)
      return e.use_ternop_insn (false, code_for_pred_mul_plus_scalar (
					 e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_ternop_insn (false,
				code_for_pred_mul_plus (e.vector_mode ()));
    gcc_unreachable ();
  }
};

class vnmsub : public function_base
{
public:
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vx)
      return e.use_ternop_insn (false, code_for_pred_minus_mul_scalar (
					 e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_ternop_insn (false,
				code_for_pred_minus_mul (e.vector_mode ()));
    gcc_unreachable ();
  }
};

/* Implements vwmacc<su><su>.  */
class vwmacc : public function_base
{
public:
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vx)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul_plus_scalar (SIGN_EXTEND, e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul_plus (SIGN_EXTEND, e.vector_mode ()));
    gcc_unreachable ();
  }
};

class vwmaccu : public function_base
{
public:
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vx)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul_plus_scalar (ZERO_EXTEND, e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul_plus (ZERO_EXTEND, e.vector_mode ()));
    gcc_unreachable ();
  }
};

class vwmaccsu : public function_base
{
public:
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vx)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul_plussu_scalar (e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul_plussu (e.vector_mode ()));
    gcc_unreachable ();
  }
};

class vwmaccus : public function_base
{
public:
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_widen_ternop_insn (
      code_for_pred_widen_mul_plusus_scalar (e.vector_mode ()));
  }
};

/* Implements vmand/vmnand/vmandn/vmxor/vmor/vmnor/vmorn/vmxnor  */
template<rtx_code CODE>
class mask_logic : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred (CODE, e.vector_mode ()));
  }
};
template<rtx_code CODE>
class mask_nlogic : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_n (CODE, e.vector_mode ()));
  }
};
template<rtx_code CODE>
class mask_notlogic : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_not (CODE, e.vector_mode ()));
  }
};

/* Implements vmmv.  */
class vmmv : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_mov (e.vector_mode ()));
  }
};

/* Implements vmclr.  */
class vmclr : public function_base
{
public:
  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return false;
  }

  rtx expand (function_expander &e) const override
  {
    machine_mode mode = TYPE_MODE (TREE_TYPE (e.exp));
    e.add_all_one_mask_operand (mode);
    e.add_vundef_operand (mode);
    e.add_input_operand (mode, CONST0_RTX (mode));
    e.add_input_operand (call_expr_nargs (e.exp) - 1);
    e.add_input_operand (Pmode, get_avl_type_rtx (avl_type::NONVLMAX));
    return e.generate_insn (code_for_pred_mov (e.vector_mode ()));
  }
};

/* Implements vmset.  */
class vmset : public function_base
{
public:
  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return false;
  }

  rtx expand (function_expander &e) const override
  {
    machine_mode mode = TYPE_MODE (TREE_TYPE (e.exp));
    e.add_all_one_mask_operand (mode);
    e.add_vundef_operand (mode);
    e.add_input_operand (mode, CONSTM1_RTX (mode));
    e.add_input_operand (call_expr_nargs (e.exp) - 1);
    e.add_input_operand (Pmode, get_avl_type_rtx (avl_type::NONVLMAX));
    return e.generate_insn (code_for_pred_mov (e.vector_mode ()));
  }
};

/* Implements vmnot.  */
class vmnot : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_not (e.vector_mode ()));
  }
};

/* Implements vcpop.  */
class vcpop : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_popcount (e.vector_mode (), Pmode));
  }
};

/* Implements vfirst.  */
class vfirst : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_ffs (e.vector_mode (), Pmode));
  }
};

/* Implements vmsbf/vmsif/vmsof.  */
template<int UNSPEC>
class mask_misc : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred (UNSPEC, e.vector_mode ()));
  }
};

/* Implements viota.  */
class viota : public function_base
{
public:
  bool can_be_overloaded_p (enum predication_type_index pred) const override
  {
    return pred == PRED_TYPE_tu || pred == PRED_TYPE_tum
	   || pred == PRED_TYPE_tumu || pred == PRED_TYPE_mu;
  }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_iota (e.vector_mode ()));
  }
};

/* Implements vid.  */
class vid : public function_base
{
public:
  bool can_be_overloaded_p (enum predication_type_index pred) const override
  {
    return pred == PRED_TYPE_tu || pred == PRED_TYPE_tum
	   || pred == PRED_TYPE_tumu || pred == PRED_TYPE_mu;
  }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_series (e.vector_mode ()));
  }
};

/* Implements vfrsub/vfrdiv.  */
template<rtx_code CODE, enum frm_op_type FRM_OP = NO_FRM>
class reverse_binop : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (
      code_for_pred_reverse_scalar (CODE, e.vector_mode ()));
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfmacc : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_ternop_insn (true,
				code_for_pred_mul_scalar (PLUS,
							  e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_ternop_insn (true,
				code_for_pred_mul (PLUS, e.vector_mode ()));
    gcc_unreachable ();
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfnmsac : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_ternop_insn (
	true, code_for_pred_mul_neg_scalar (PLUS, e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_ternop_insn (true,
				code_for_pred_mul_neg (PLUS, e.vector_mode ()));
    gcc_unreachable ();
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfmadd : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_ternop_insn (false,
				code_for_pred_mul_scalar (PLUS,
							  e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_ternop_insn (false,
				code_for_pred_mul (PLUS, e.vector_mode ()));
    gcc_unreachable ();
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfnmsub : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_ternop_insn (
	false, code_for_pred_mul_neg_scalar (PLUS, e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_ternop_insn (false,
				code_for_pred_mul_neg (PLUS, e.vector_mode ()));
    gcc_unreachable ();
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfnmacc : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_ternop_insn (
	true, code_for_pred_mul_neg_scalar (MINUS, e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_ternop_insn (true,
				code_for_pred_mul_neg (MINUS, e.vector_mode ()));
    gcc_unreachable ();
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfmsac : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_ternop_insn (true,
				code_for_pred_mul_scalar (MINUS,
							  e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_ternop_insn (true,
				code_for_pred_mul (MINUS, e.vector_mode ()));
    gcc_unreachable ();
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfnmadd : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_ternop_insn (
	false, code_for_pred_mul_neg_scalar (MINUS, e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_ternop_insn (false,
				code_for_pred_mul_neg (MINUS, e.vector_mode ()));
    gcc_unreachable ();
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfmsub : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_ternop_insn (false,
				code_for_pred_mul_scalar (MINUS,
							  e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_ternop_insn (false,
				code_for_pred_mul (MINUS, e.vector_mode ()));
    gcc_unreachable ();
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfwmacc : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul_scalar (PLUS, e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul (PLUS, e.vector_mode ()));
    gcc_unreachable ();
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfwnmacc : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul_neg_scalar (MINUS, e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul_neg (MINUS, e.vector_mode ()));
    gcc_unreachable ();
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfwmsac : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul_scalar (MINUS, e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul (MINUS, e.vector_mode ()));
    gcc_unreachable ();
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfwnmsac : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul_neg_scalar (PLUS, e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_mul_neg (PLUS, e.vector_mode ()));
    gcc_unreachable ();
  }
};

/* Implements vfsqrt7/vfrec7/vfclass/vfsgnj/vfsgnjx.  */
template<int UNSPEC, enum frm_op_type FRM_OP = NO_FRM>
class float_misc : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_exact_insn (code_for_pred_scalar (UNSPEC, e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv || e.op_info->op == OP_TYPE_v)
      return e.use_exact_insn (code_for_pred (UNSPEC, e.vector_mode ()));
    gcc_unreachable ();
  }
};

/* Implements vfsgnjn.  */
class vfsgnjn : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_exact_insn (code_for_pred_ncopysign_scalar (e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_exact_insn (code_for_pred_ncopysign (e.vector_mode ()));
    gcc_unreachable ();
  }
};

/* Implements vmfeq/vmfne/vmflt/vmfgt/vmfle/vmfge.  */
template<rtx_code CODE>
class fcmp : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
	case OP_TYPE_vf: {
	  return e.use_compare_insn (CODE, code_for_pred_cmp_scalar (
					     e.vector_mode ()));
	}
	case OP_TYPE_vv: {
	  return e.use_compare_insn (CODE,
				     code_for_pred_cmp (e.vector_mode ()));
	}
      default:
	gcc_unreachable ();
      }
  }
};

/* Implements vfclass.  */
class vfclass : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_class (e.arg_mode (0)));
  }
};

/* Implements vfcvt.x.  */
template<int UNSPEC, enum frm_op_type FRM_OP = NO_FRM>
class vfcvt_x : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_fcvt_x_f (UNSPEC, e.arg_mode (0)));
  }
};

/* Implements vfcvt.rtz.x.  */
template<rtx_code CODE>
class vfcvt_rtz_x : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred (CODE, e.arg_mode (0)));
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfcvt_f : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_x_v)
      return e.use_exact_insn (code_for_pred (FLOAT, e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_xu_v)
      return e.use_exact_insn (
	code_for_pred (UNSIGNED_FLOAT, e.vector_mode ()));
    gcc_unreachable ();
  }
};

/* Implements vfwcvt.x.  */
template<int UNSPEC, enum frm_op_type FRM_OP = NO_FRM>
class vfwcvt_x : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (
      code_for_pred_widen_fcvt_x_f (UNSPEC, e.vector_mode ()));
  }
};

/* Implements vfwcvt.rtz.x.  */
template<rtx_code CODE>
class vfwcvt_rtz_x : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_widen (CODE, e.vector_mode ()));
  }
};

class vfwcvt_f : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_f_v)
      return e.use_exact_insn (code_for_pred_extend (e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_x_v)
      return e.use_exact_insn (code_for_pred_widen (FLOAT, e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_xu_v)
      return e.use_exact_insn (
	code_for_pred_widen (UNSIGNED_FLOAT, e.vector_mode ()));
    gcc_unreachable ();
  }
};

/* Implements vfncvt.x.  */
template<int UNSPEC, enum frm_op_type FRM_OP = NO_FRM>
class vfncvt_x : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (
      code_for_pred_narrow_fcvt_x_f (UNSPEC, e.arg_mode (0)));
  }
};

/* Implements vfncvt.rtz.x.  */
template<rtx_code CODE>
class vfncvt_rtz_x : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_narrow (CODE, e.vector_mode ()));
  }
};

template<enum frm_op_type FRM_OP = NO_FRM>
class vfncvt_f : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_f_w)
      return e.use_exact_insn (code_for_pred_trunc (e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_x_w)
      return e.use_exact_insn (code_for_pred_narrow (FLOAT, e.arg_mode (0)));
    if (e.op_info->op == OP_TYPE_xu_w)
      return e.use_exact_insn (
	code_for_pred_narrow (UNSIGNED_FLOAT, e.arg_mode (0)));
    gcc_unreachable ();
  }
};

class vfncvt_rod_f : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_rod_trunc (e.vector_mode ()));
  }
};

/* Implements reduction instructions.  */
template<unsigned UNSPEC>
class reducop : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred (UNSPEC, e.vector_mode ()));
  }
};

/* Implements floating-point reduction instructions.  */
template<unsigned UNSPEC, enum frm_op_type FRM_OP = NO_FRM>
class freducop : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool apply_mask_policy_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred (UNSPEC, e.vector_mode ()));
  }
};

/* Implements vmv/vfmv instructions.  */
class vmv : public function_base
{
public:
  bool apply_vl_p () const override { return false; }
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_extract_first (e.vector_mode ()));
  }
};

/* Implements vmv.s.x/vfmv.s.f.  */
class vmv_s : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    return e.use_scalar_move_insn (code_for_pred_broadcast (e.vector_mode ()));
  }
};

template<int UNSPEC>
class slideop : public function_base
{
public:
  bool has_merge_operand_p () const override
  {
    if (UNSPEC == UNSPEC_VSLIDEUP)
      return false;
    return true;
  }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_slide (UNSPEC, e.vector_mode ()));
  }
};

class vrgather : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vx:
	return e.use_exact_insn (
	  code_for_pred_gather_scalar (e.vector_mode ()));
      case OP_TYPE_vv:
	return e.use_exact_insn (code_for_pred_gather (e.vector_mode ()));
      default:
	gcc_unreachable ();
      }
  }
};

class vrgatherei16 : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_gatherei16 (e.vector_mode ()));
  }
};

class vcompress : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_compress (e.vector_mode ()));
  }
};

class vundefined : public function_base
{
public:
  bool apply_vl_p () const override
  {
    return false;
  }

  rtx expand (function_expander &e) const override
  {
    return e.generate_insn (code_for_vundefined (e.vector_mode ()));
  }
};

class vreinterpret : public function_base
{
public:
  bool apply_vl_p () const override
  {
    return false;
  }

  rtx expand (function_expander &e) const override
  {
    e.add_input_operand (0);
    return e.generate_insn (code_for_vreinterpret (e.ret_mode ()));
  }
};

class vlmul_ext : public function_base
{
public:
  bool apply_vl_p () const override
  {
    return false;
  }

  rtx expand (function_expander &e) const override
  {
    if (!e.target)
      return NULL_RTX;
    tree arg = CALL_EXPR_ARG (e.exp, 0);
    rtx src = expand_normal (arg);
    emit_move_insn (gen_lowpart (e.vector_mode (), e.target), src);
    return e.target;
  }
};

class vlmul_trunc : public function_base
{
public:
  bool apply_vl_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (!e.target)
      return NULL_RTX;
    rtx src = expand_normal (CALL_EXPR_ARG (e.exp, 0));
    emit_move_insn (e.target, gen_lowpart (GET_MODE (e.target), src));
    return e.target;
  }
};

class vset : public function_base
{
public:
  bool apply_vl_p () const override { return false; }

  gimple *fold (gimple_folder &f) const override
  {
    tree rhs_tuple = gimple_call_arg (f.call, 0);
    /* LMUL > 1 non-tuple vector types are not structure,
       we can't use __val[index] to set the subpart.  */
    if (!riscv_tuple_mode_p (TYPE_MODE (TREE_TYPE (rhs_tuple))))
      return NULL;
    tree index = gimple_call_arg (f.call, 1);
    tree rhs_vector = gimple_call_arg (f.call, 2);

    /* Replace the call with two statements: a copy of the full tuple
       to the call result, followed by an update of the individual vector.

       The fold routines expect the replacement statement to have the
       same lhs as the original call, so return the copy statement
       rather than the field update.  */
    gassign *copy = gimple_build_assign (f.lhs, rhs_tuple);

    /* Get a reference to the individual vector.  */
    tree field = tuple_type_field (TREE_TYPE (f.lhs));
    tree lhs_array
      = build3 (COMPONENT_REF, TREE_TYPE (field), unshare_expr (f.lhs),
		field, NULL_TREE);
    tree lhs_vector = build4 (ARRAY_REF, TREE_TYPE (rhs_vector), lhs_array,
			      index, NULL_TREE, NULL_TREE);
    gassign *update = gimple_build_assign (lhs_vector, rhs_vector);
    gsi_insert_after (f.gsi, update, GSI_SAME_STMT);

    return copy;
  }

  rtx expand (function_expander &e) const override
  {
    if (!e.target)
      return NULL_RTX;
    rtx dest = expand_normal (CALL_EXPR_ARG (e.exp, 0));
    gcc_assert (riscv_vla_mode_p (GET_MODE (dest)));
    rtx index = expand_normal (CALL_EXPR_ARG (e.exp, 1));
    rtx src = expand_normal (CALL_EXPR_ARG (e.exp, 2));
    poly_int64 offset = INTVAL (index) * GET_MODE_SIZE (GET_MODE (src));
    emit_move_insn (e.target, dest);
    rtx subreg = simplify_gen_subreg (GET_MODE (src), e.target,
				      GET_MODE (e.target), offset);
    emit_move_insn (subreg, src);
    return e.target;
  }
};

class vget : public function_base
{
public:
  bool apply_vl_p () const override { return false; }

  gimple *fold (gimple_folder &f) const override
  {
    /* Fold into a normal gimple component access.  */
    tree rhs_tuple = gimple_call_arg (f.call, 0);
    /* LMUL > 1 non-tuple vector types are not structure,
       we can't use __val[index] to get the subpart.  */
    if (!riscv_tuple_mode_p (TYPE_MODE (TREE_TYPE (rhs_tuple))))
      return NULL;
    tree index = gimple_call_arg (f.call, 1);
    tree field = tuple_type_field (TREE_TYPE (rhs_tuple));
    tree rhs_array
      = build3 (COMPONENT_REF, TREE_TYPE (field), rhs_tuple, field, NULL_TREE);
    tree rhs_vector = build4 (ARRAY_REF, TREE_TYPE (f.lhs), rhs_array, index,
			      NULL_TREE, NULL_TREE);
    return gimple_build_assign (f.lhs, rhs_vector);
  }

  rtx expand (function_expander &e) const override
  {
    if (!e.target)
      return NULL_RTX;
    rtx src = expand_normal (CALL_EXPR_ARG (e.exp, 0));
    gcc_assert (riscv_vla_mode_p (GET_MODE (src)));
    rtx index = expand_normal (CALL_EXPR_ARG (e.exp, 1));
    poly_int64 offset = INTVAL (index) * GET_MODE_SIZE (GET_MODE (e.target));
    rtx subreg
      = simplify_gen_subreg (GET_MODE (e.target), src, GET_MODE (src), offset);
    return subreg;
  }
};

class vcreate : public function_base
{
public:
  gimple *fold (gimple_folder &f) const override
  {
    unsigned int nargs = gimple_call_num_args (f.call);
    tree lhs_type = TREE_TYPE (f.lhs);
    /* LMUL > 1 non-tuple vector types are not structure,
   we can't use __val[index] to set the subpart.  */
    if (!riscv_tuple_mode_p (TYPE_MODE (lhs_type)))
      return NULL;

    /* Replace the call with a clobber of the result (to prevent it from
       becoming upwards exposed) followed by stores into each individual
       vector of tuple.

       The fold routines expect the replacement statement to have the
       same lhs as the original call, so return the clobber statement
       rather than the final vector store.  */
    gassign *clobber = gimple_build_assign (f.lhs, build_clobber (lhs_type));

    for (unsigned int i = nargs; i-- > 0; )
      {
	tree rhs_vector = gimple_call_arg (f.call, i);
	tree field = tuple_type_field (TREE_TYPE (f.lhs));
	tree lhs_array = build3 (COMPONENT_REF, TREE_TYPE (field),
				 unshare_expr (f.lhs), field, NULL_TREE);
	tree lhs_vector = build4 (ARRAY_REF, TREE_TYPE (rhs_vector),
				  lhs_array, size_int (i),
				  NULL_TREE, NULL_TREE);
	gassign *assign = gimple_build_assign (lhs_vector, rhs_vector);
	gsi_insert_after (f.gsi, assign, GSI_SAME_STMT);
      }
    return clobber;
  }

  rtx expand (function_expander &e) const override
  {
    if (!e.target)
      return NULL_RTX;
    gcc_assert (riscv_vla_mode_p (GET_MODE (e.target)));
    unsigned int nargs = call_expr_nargs (e.exp);
    for (unsigned int i = 0; i < nargs; i++)
      {
	rtx src = expand_normal (CALL_EXPR_ARG (e.exp, i));
	poly_int64 offset = i * GET_MODE_SIZE (GET_MODE (src));
	rtx subreg = simplify_gen_subreg (GET_MODE (src), e.target,
					  GET_MODE (e.target), offset);
	emit_move_insn (subreg, src);
      }

    return e.target;
  }
};

class read_vl : public function_base
{
public:
  unsigned int call_properties (const function_instance &) const override
  {
    return CP_READ_CSR;
  }

  rtx expand (function_expander &e) const override
  {
    emit_insn (gen_read_vl (Pmode, e.target));
    return e.target;
  }
};

class vleff : public function_base
{
public:
  unsigned int call_properties (const function_instance &) const override
  {
    return CP_READ_MEMORY | CP_WRITE_CSR;
  }

  bool can_be_overloaded_p (enum predication_type_index pred) const override
  {
    return pred != PRED_TYPE_none;
  }

  rtx expand (function_expander &e) const override
  {
    return e.use_fof_load_insn ();
  }
};

/* Implements vlenb.  */
class vlenb : public function_base
{
public:
  bool apply_vl_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    machine_mode mode = GET_MODE (e.target);
    rtx vlenb = gen_int_mode (BYTES_PER_RISCV_VECTOR, mode);
    emit_move_insn (e.target, vlenb);
    return e.target;
  }
};

/* Implements vlseg.v.  */
class vlseg : public function_base
{
public:
  unsigned int call_properties (const function_instance &) const override
  {
    return CP_READ_MEMORY;
  }

  bool can_be_overloaded_p (enum predication_type_index pred) const override
  {
    return pred != PRED_TYPE_none;
  }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (
      code_for_pred_unit_strided_load (e.vector_mode ()));
  }
};

/* Implements vsseg.v.  */
class vsseg : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }

  unsigned int call_properties (const function_instance &) const override
  {
    return CP_WRITE_MEMORY;
  }

  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return true;
  }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (
      code_for_pred_unit_strided_store (e.vector_mode ()));
  }
};

/* Implements vlsseg.v.  */
class vlsseg : public function_base
{
public:
  unsigned int call_properties (const function_instance &) const override
  {
    return CP_READ_MEMORY;
  }

  bool can_be_overloaded_p (enum predication_type_index pred) const override
  {
    return pred != PRED_TYPE_none;
  }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (
      code_for_pred_strided_load (e.vector_mode ()));
  }
};

/* Implements vssseg.v.  */
class vssseg : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }

  unsigned int call_properties (const function_instance &) const override
  {
    return CP_WRITE_MEMORY;
  }

  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return true;
  }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (
      code_for_pred_strided_store (e.vector_mode ()));
  }
};

template<int UNSPEC>
class seg_indexed_load : public function_base
{
public:
  unsigned int call_properties (const function_instance &) const override
  {
    return CP_READ_MEMORY;
  }

  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return true;
  }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (
      code_for_pred_indexed_load (UNSPEC, e.vector_mode (), e.index_mode ()));
  }
};

template<int UNSPEC>
class seg_indexed_store : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }

  unsigned int call_properties (const function_instance &) const override
  {
    return CP_WRITE_MEMORY;
  }

  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return true;
  }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (
      code_for_pred_indexed_store (UNSPEC, e.vector_mode (), e.index_mode ()));
  }
};

/* Implements vlsegff.v.  */
class vlsegff : public function_base
{
public:
  unsigned int call_properties (const function_instance &) const override
  {
    return CP_READ_MEMORY | CP_WRITE_CSR;
  }

  bool can_be_overloaded_p (enum predication_type_index pred) const override
  {
    return pred != PRED_TYPE_none;
  }

  rtx expand (function_expander &e) const override
  {
    return e.use_fof_load_insn ();
  }
};

/* Implements
 * th.vl(b/h/w)[u].v/th.vs(b/h/w)[u].v/th.vls(b/h/w)[u].v/th.vss(b/h/w)[u].v/
 * th.vlx(b/h/w)[u].v/th.vs[u]x(b/h/w).v
 * codegen.  */
template<bool STORE_P, lst_type LST_TYPE, int UNSPEC>
class th_loadstore_width : public function_base
{
public:
  bool apply_tail_policy_p () const override { return !STORE_P; }
  bool apply_mask_policy_p () const override { return !STORE_P; }

  unsigned int call_properties (const function_instance &) const override
  {
    if (STORE_P)
      return CP_WRITE_MEMORY;
    else
      return CP_READ_MEMORY;
  }

  bool can_be_overloaded_p (enum predication_type_index pred) const override
  {
    if (STORE_P || LST_TYPE == LST_INDEXED)
      return true;
    return pred != PRED_TYPE_none;
  }

  rtx expand (function_expander &e) const override
  {
    gcc_assert (TARGET_XTHEADVECTOR);
    if (LST_TYPE == LST_INDEXED)
      {
	if (STORE_P)
	  return e.use_exact_insn (
	    code_for_pred_indexed_store_width (UNSPEC, UNSPEC,
					       e.vector_mode ()));
	else
	  return e.use_exact_insn (
	    code_for_pred_indexed_load_width (UNSPEC, e.vector_mode ()));
      }
    else if (LST_TYPE == LST_STRIDED)
      {
	if (STORE_P)
	  return e.use_contiguous_store_insn (
	    code_for_pred_strided_store_width (UNSPEC, e.vector_mode ()));
	else
	  return e.use_contiguous_load_insn (
	    code_for_pred_strided_load_width (UNSPEC, e.vector_mode ()));
      }
    else
      {
	if (STORE_P)
	  return e.use_contiguous_store_insn (
	    code_for_pred_store_width (UNSPEC, e.vector_mode ()));
	else
	  return e.use_contiguous_load_insn (
	    code_for_pred_mov_width (UNSPEC, e.vector_mode ()));
      }
  }
};

/* Implements vext.x.v.  */
class th_extract : public function_base
{
public:
  bool apply_vl_p () const override { return false; }
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    gcc_assert (TARGET_XTHEADVECTOR);
    return e.use_exact_insn (code_for_pred_th_extract (e.vector_mode ()));
  }
};

/* Below implements are vector crypto */
/* Implements vandn.[vv,vx] */
class vandn : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vv:
        return e.use_exact_insn (code_for_pred_vandn (e.vector_mode ()));
      case OP_TYPE_vx:
        return e.use_exact_insn (code_for_pred_vandn_scalar (e.vector_mode ()));
      default:
        gcc_unreachable ();
      }
  }
};

/* Implements vrol/vror/clz/ctz.  */
template<rtx_code CODE>
class bitmanip : public function_base
{
public:
  bool apply_tail_policy_p () const override
  {
    return (CODE == CLZ || CODE == CTZ) ? false : true;
  }
  bool apply_mask_policy_p () const override
  {
    return (CODE == CLZ || CODE == CTZ) ? false : true;
  }
  bool has_merge_operand_p () const override
  {
    return (CODE == CLZ || CODE == CTZ) ? false : true;
  }

  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
    {
      case OP_TYPE_v:
      case OP_TYPE_vv:
        return e.use_exact_insn (code_for_pred_v (CODE, e.vector_mode ()));
      case OP_TYPE_vx:
        return e.use_exact_insn (code_for_pred_v_scalar (CODE, e.vector_mode ()));
      default:
        gcc_unreachable ();
    }
  }
};

/* Implements vbrev/vbrev8/vrev8.  */
template<int UNSPEC>
class b_reverse : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
      return e.use_exact_insn (code_for_pred_v (UNSPEC, e.vector_mode ()));
  }
};

class vwsll : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vv:
        return e.use_exact_insn (code_for_pred_vwsll (e.vector_mode ()));
      case OP_TYPE_vx:
        return e.use_exact_insn (code_for_pred_vwsll_scalar (e.vector_mode ()));
      default:
        gcc_unreachable ();
      }
  }
};

/* Implements clmul */
template<int UNSPEC>
class clmul : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    switch (e.op_info->op)
      {
      case OP_TYPE_vv:
        return e.use_exact_insn (
                 code_for_pred_vclmul (UNSPEC, e.vector_mode ()));
      case OP_TYPE_vx:
        return e.use_exact_insn
                 (code_for_pred_vclmul_scalar (UNSPEC, e.vector_mode ()));
      default:
        gcc_unreachable ();
      }
  }
};

/* Implements vghsh/vsh2ms/vsha2c[hl]. */
template<int UNSPEC>
class vg_nhab : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_v (UNSPEC, e.vector_mode ()));
  }
};

/* Implements vgmul/vaes*. */
template<int UNSPEC>
class crypto_vv : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    poly_uint64 nunits = 0U;
    switch (e.op_info->op)
    {
      case OP_TYPE_vv:
        if (UNSPEC == UNSPEC_VGMUL)
          return e.use_exact_insn
                   (code_for_pred_crypto_vv (UNSPEC, UNSPEC, e.vector_mode ()));
        else
          return e.use_exact_insn
                   (code_for_pred_crypto_vv (UNSPEC + 1, UNSPEC + 1, e.vector_mode ()));
      case OP_TYPE_vs:
        /* Calculate the ratio between arg0 and arg1*/
        gcc_assert (multiple_p (GET_MODE_BITSIZE (e.arg_mode (0)),
                                GET_MODE_BITSIZE (e.arg_mode (1)), &nunits));
        if (maybe_eq (nunits, 1U))
          return e.use_exact_insn (code_for_pred_crypto_vvx1_scalar
                                   (UNSPEC + 2, UNSPEC + 2, e.vector_mode ()));
        else if (maybe_eq (nunits, 2U))
          return e.use_exact_insn (code_for_pred_crypto_vvx2_scalar
                                   (UNSPEC + 2, UNSPEC + 2, e.vector_mode ()));
        else if (maybe_eq (nunits, 4U))
          return e.use_exact_insn (code_for_pred_crypto_vvx4_scalar
                                   (UNSPEC + 2, UNSPEC + 2, e.vector_mode ()));
        else if (maybe_eq (nunits, 8U))
          return e.use_exact_insn (code_for_pred_crypto_vvx8_scalar
                                   (UNSPEC + 2, UNSPEC + 2, e.vector_mode ()));
        else
          return e.use_exact_insn (code_for_pred_crypto_vvx16_scalar
                                   (UNSPEC + 2, UNSPEC + 2, e.vector_mode ()));
      default:
        gcc_unreachable ();
    }
  }
};

/* Implements vaeskf1/vsm4k. */
template<int UNSPEC>
class crypto_vi : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn
             (code_for_pred_crypto_vi_scalar (UNSPEC, e.vector_mode ()));
  }
};

/* Implements vaeskf2/vsm3c. */
template<int UNSPEC>
class vaeskf2_vsm3c : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn
             (code_for_pred_vi_nomaskedoff_scalar (UNSPEC, e.vector_mode ()));
  }
};

/* Implements vsm3me. */
class vsm3me : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_vsm3me (e.vector_mode ()));
  }
};

/* Implements vfncvtbf16_f. */
template <enum frm_op_type FRM_OP = NO_FRM>
class vfncvtbf16_f : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_trunc_to_bf16 (e.vector_mode ()));
  }
};

/* Implements vfwcvtbf16_f. */
class vfwcvtbf16_f : public function_base
{
public:
  rtx expand (function_expander &e) const override
  {
    return e.use_exact_insn (code_for_pred_extend_bf16_to (e.vector_mode ()));
  }
};

/* Implements vfwmaccbf16. */
template <enum frm_op_type FRM_OP = NO_FRM>
class vfwmaccbf16 : public function_base
{
public:
  bool has_rounding_mode_operand_p () const override
  {
    return FRM_OP == HAS_FRM;
  }

  bool may_require_frm_p () const override { return true; }

  bool has_merge_operand_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    if (e.op_info->op == OP_TYPE_vf)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_bf16_mul_scalar (e.vector_mode ()));
    if (e.op_info->op == OP_TYPE_vv)
      return e.use_widen_ternop_insn (
	code_for_pred_widen_bf16_mul (e.vector_mode ()));
    gcc_unreachable ();
  }
};

static CONSTEXPR const vsetvl<false> vsetvl_obj;
static CONSTEXPR const vsetvl<true> vsetvlmax_obj;
static CONSTEXPR const loadstore<false, LST_UNIT_STRIDE, false> vle_obj;
static CONSTEXPR const loadstore<true, LST_UNIT_STRIDE, false> vse_obj;
static CONSTEXPR const loadstore<false, LST_UNIT_STRIDE, false> vlm_obj;
static CONSTEXPR const loadstore<true, LST_UNIT_STRIDE, false> vsm_obj;
static CONSTEXPR const loadstore<false, LST_STRIDED, false> vlse_obj;
static CONSTEXPR const loadstore<true, LST_STRIDED, false> vsse_obj;
static CONSTEXPR const loadstore<false, LST_INDEXED, false> vluxei8_obj;
static CONSTEXPR const loadstore<false, LST_INDEXED, false> vluxei16_obj;
static CONSTEXPR const loadstore<false, LST_INDEXED, false> vluxei32_obj;
static CONSTEXPR const loadstore<false, LST_INDEXED, false> vluxei64_obj;
static CONSTEXPR const loadstore<false, LST_INDEXED, true> vloxei8_obj;
static CONSTEXPR const loadstore<false, LST_INDEXED, true> vloxei16_obj;
static CONSTEXPR const loadstore<false, LST_INDEXED, true> vloxei32_obj;
static CONSTEXPR const loadstore<false, LST_INDEXED, true> vloxei64_obj;
static CONSTEXPR const loadstore<true, LST_INDEXED, false> vsuxei8_obj;
static CONSTEXPR const loadstore<true, LST_INDEXED, false> vsuxei16_obj;
static CONSTEXPR const loadstore<true, LST_INDEXED, false> vsuxei32_obj;
static CONSTEXPR const loadstore<true, LST_INDEXED, false> vsuxei64_obj;
static CONSTEXPR const loadstore<true, LST_INDEXED, true> vsoxei8_obj;
static CONSTEXPR const loadstore<true, LST_INDEXED, true> vsoxei16_obj;
static CONSTEXPR const loadstore<true, LST_INDEXED, true> vsoxei32_obj;
static CONSTEXPR const loadstore<true, LST_INDEXED, true> vsoxei64_obj;
static CONSTEXPR const binop<PLUS> vadd_obj;
static CONSTEXPR const binop<MINUS> vsub_obj;
static CONSTEXPR const vrsub vrsub_obj;
static CONSTEXPR const binop<AND> vand_obj;
static CONSTEXPR const binop<IOR> vor_obj;
static CONSTEXPR const binop<XOR> vxor_obj;
static CONSTEXPR const binop<ASHIFT> vsll_obj;
static CONSTEXPR const binop<ASHIFTRT> vsra_obj;
static CONSTEXPR const binop<LSHIFTRT> vsrl_obj;
static CONSTEXPR const binop<SMIN> vmin_obj;
static CONSTEXPR const binop<SMAX> vmax_obj;
static CONSTEXPR const binop<UMIN> vminu_obj;
static CONSTEXPR const binop<UMAX> vmaxu_obj;
static CONSTEXPR const binop<MULT> vmul_obj;
static CONSTEXPR const vmulh<UNSPEC_VMULHS> vmulh_obj;
static CONSTEXPR const vmulh<UNSPEC_VMULHU> vmulhu_obj;
static CONSTEXPR const vmulh<UNSPEC_VMULHSU> vmulhsu_obj;
static CONSTEXPR const binop<DIV> vdiv_obj;
static CONSTEXPR const binop<MOD> vrem_obj;
static CONSTEXPR const binop<UDIV> vdivu_obj;
static CONSTEXPR const binop<UMOD> vremu_obj;
static CONSTEXPR const unop<NEG> vneg_obj;
static CONSTEXPR const unop<NOT> vnot_obj;
static CONSTEXPR const ext<SIGN_EXTEND> vsext_obj;
static CONSTEXPR const ext<ZERO_EXTEND> vzext_obj;
static CONSTEXPR const widen_binop<PLUS, SIGN_EXTEND>vwadd_obj;
static CONSTEXPR const widen_binop<MINUS, SIGN_EXTEND>vwsub_obj;
static CONSTEXPR const widen_binop<MULT, SIGN_EXTEND>vwmul_obj;
static CONSTEXPR const widen_binop<PLUS, ZERO_EXTEND>vwaddu_obj;
static CONSTEXPR const widen_binop<MINUS, ZERO_EXTEND>vwsubu_obj;
static CONSTEXPR const widen_binop<MULT, ZERO_EXTEND>vwmulu_obj;
static CONSTEXPR const vwmulsu vwmulsu_obj;
static CONSTEXPR const vwcvt<SIGN_EXTEND> vwcvt_x_obj;
static CONSTEXPR const vwcvt<ZERO_EXTEND> vwcvtu_x_obj;
static CONSTEXPR const vadc vadc_obj;
static CONSTEXPR const vsbc vsbc_obj;
static CONSTEXPR const vmadc vmadc_obj;
static CONSTEXPR const vmsbc vmsbc_obj;
static CONSTEXPR const vnshift<LSHIFTRT> vnsrl_obj;
static CONSTEXPR const vnshift<ASHIFTRT> vnsra_obj;
static CONSTEXPR const vncvt_x vncvt_x_obj;
static CONSTEXPR const vmerge vmerge_obj;
static CONSTEXPR const vmv_v vmv_v_obj;
static CONSTEXPR const icmp<EQ> vmseq_obj;
static CONSTEXPR const icmp<NE> vmsne_obj;
static CONSTEXPR const icmp<LT> vmslt_obj;
static CONSTEXPR const icmp<GT> vmsgt_obj;
static CONSTEXPR const icmp<LE> vmsle_obj;
static CONSTEXPR const icmp<GE> vmsge_obj;
static CONSTEXPR const icmp<LTU> vmsltu_obj;
static CONSTEXPR const icmp<GTU> vmsgtu_obj;
static CONSTEXPR const icmp<LEU> vmsleu_obj;
static CONSTEXPR const icmp<GEU> vmsgeu_obj;
static CONSTEXPR const vmacc vmacc_obj;
static CONSTEXPR const vnmsac vnmsac_obj;
static CONSTEXPR const vmadd vmadd_obj;
static CONSTEXPR const vnmsub vnmsub_obj;
static CONSTEXPR const vwmacc vwmacc_obj;
static CONSTEXPR const vwmaccu vwmaccu_obj;
static CONSTEXPR const vwmaccsu vwmaccsu_obj;
static CONSTEXPR const vwmaccus vwmaccus_obj;
static CONSTEXPR const binop<SS_PLUS> vsadd_obj;
static CONSTEXPR const binop<SS_MINUS> vssub_obj;
static CONSTEXPR const binop<US_PLUS> vsaddu_obj;
static CONSTEXPR const binop<US_MINUS> vssubu_obj;
static CONSTEXPR const sat_op<UNSPEC_VAADDU> vaaddu_obj;
static CONSTEXPR const sat_op<UNSPEC_VAADD> vaadd_obj;
static CONSTEXPR const sat_op<UNSPEC_VASUBU> vasubu_obj;
static CONSTEXPR const sat_op<UNSPEC_VASUB> vasub_obj;
static CONSTEXPR const sat_op<UNSPEC_VSMUL> vsmul_obj;
static CONSTEXPR const sat_op<UNSPEC_VSSRL> vssrl_obj;
static CONSTEXPR const sat_op<UNSPEC_VSSRA> vssra_obj;
static CONSTEXPR const vnclip<UNSPEC_VNCLIP> vnclip_obj;
static CONSTEXPR const vnclip<UNSPEC_VNCLIPU> vnclipu_obj;
static CONSTEXPR const mask_logic<AND> vmand_obj;
static CONSTEXPR const mask_nlogic<AND> vmnand_obj;
static CONSTEXPR const mask_notlogic<AND> vmandn_obj;
static CONSTEXPR const mask_logic<XOR> vmxor_obj;
static CONSTEXPR const mask_logic<IOR> vmor_obj;
static CONSTEXPR const mask_nlogic<IOR> vmnor_obj;
static CONSTEXPR const mask_notlogic<IOR> vmorn_obj;
static CONSTEXPR const mask_nlogic<XOR> vmxnor_obj;
static CONSTEXPR const vmmv vmmv_obj;
static CONSTEXPR const vmclr vmclr_obj;
static CONSTEXPR const vmset vmset_obj;
static CONSTEXPR const vmnot vmnot_obj;
static CONSTEXPR const vcpop vcpop_obj;
static CONSTEXPR const vfirst vfirst_obj;
static CONSTEXPR const mask_misc<UNSPEC_VMSBF> vmsbf_obj;
static CONSTEXPR const mask_misc<UNSPEC_VMSIF> vmsif_obj;
static CONSTEXPR const mask_misc<UNSPEC_VMSOF> vmsof_obj;
static CONSTEXPR const viota viota_obj;
static CONSTEXPR const vid vid_obj;
static CONSTEXPR const binop<PLUS, true> vfadd_obj;
static CONSTEXPR const binop<MINUS, true> vfsub_obj;
static CONSTEXPR const binop<PLUS, true, HAS_FRM> vfadd_frm_obj;
static CONSTEXPR const binop<MINUS, true, HAS_FRM> vfsub_frm_obj;
static CONSTEXPR const reverse_binop<MINUS> vfrsub_obj;
static CONSTEXPR const reverse_binop<MINUS, HAS_FRM> vfrsub_frm_obj;
static CONSTEXPR const widen_binop_fp<PLUS> vfwadd_obj;
static CONSTEXPR const widen_binop_fp<PLUS, HAS_FRM> vfwadd_frm_obj;
static CONSTEXPR const widen_binop_fp<MINUS> vfwsub_obj;
static CONSTEXPR const widen_binop_fp<MINUS, HAS_FRM> vfwsub_frm_obj;
static CONSTEXPR const binop<MULT, true> vfmul_obj;
static CONSTEXPR const binop<MULT,  true, HAS_FRM> vfmul_frm_obj;
static CONSTEXPR const binop<DIV, true> vfdiv_obj;
static CONSTEXPR const binop<DIV,  true, HAS_FRM> vfdiv_frm_obj;
static CONSTEXPR const reverse_binop<DIV> vfrdiv_obj;
static CONSTEXPR const reverse_binop<DIV, HAS_FRM> vfrdiv_frm_obj;
static CONSTEXPR const widen_binop_fp<MULT> vfwmul_obj;
static CONSTEXPR const widen_binop_fp<MULT, HAS_FRM> vfwmul_frm_obj;
static CONSTEXPR const vfmacc<NO_FRM> vfmacc_obj;
static CONSTEXPR const vfmacc<HAS_FRM> vfmacc_frm_obj;
static CONSTEXPR const vfnmsac<NO_FRM> vfnmsac_obj;
static CONSTEXPR const vfnmsac<HAS_FRM> vfnmsac_frm_obj;
static CONSTEXPR const vfmadd<NO_FRM> vfmadd_obj;
static CONSTEXPR const vfmadd<HAS_FRM> vfmadd_frm_obj;
static CONSTEXPR const vfnmsub<NO_FRM> vfnmsub_obj;
static CONSTEXPR const vfnmsub<HAS_FRM> vfnmsub_frm_obj;
static CONSTEXPR const vfnmacc<NO_FRM> vfnmacc_obj;
static CONSTEXPR const vfnmacc<HAS_FRM> vfnmacc_frm_obj;
static CONSTEXPR const vfmsac<NO_FRM> vfmsac_obj;
static CONSTEXPR const vfmsac<HAS_FRM> vfmsac_frm_obj;
static CONSTEXPR const vfnmadd<NO_FRM> vfnmadd_obj;
static CONSTEXPR const vfnmadd<HAS_FRM> vfnmadd_frm_obj;
static CONSTEXPR const vfmsub<NO_FRM> vfmsub_obj;
static CONSTEXPR const vfmsub<HAS_FRM> vfmsub_frm_obj;
static CONSTEXPR const vfwmacc<NO_FRM> vfwmacc_obj;
static CONSTEXPR const vfwmacc<HAS_FRM> vfwmacc_frm_obj;
static CONSTEXPR const vfwnmacc<NO_FRM> vfwnmacc_obj;
static CONSTEXPR const vfwnmacc<HAS_FRM> vfwnmacc_frm_obj;
static CONSTEXPR const vfwmsac<NO_FRM> vfwmsac_obj;
static CONSTEXPR const vfwmsac<HAS_FRM> vfwmsac_frm_obj;
static CONSTEXPR const vfwnmsac<NO_FRM> vfwnmsac_obj;
static CONSTEXPR const vfwnmsac<HAS_FRM> vfwnmsac_frm_obj;
static CONSTEXPR const unop<SQRT> vfsqrt_obj;
static CONSTEXPR const unop<SQRT, HAS_FRM> vfsqrt_frm_obj;
static CONSTEXPR const float_misc<UNSPEC_VFRSQRT7> vfrsqrt7_obj;
static CONSTEXPR const float_misc<UNSPEC_VFREC7> vfrec7_obj;
static CONSTEXPR const float_misc<UNSPEC_VFREC7, HAS_FRM> vfrec7_frm_obj;
static CONSTEXPR const binop<SMIN> vfmin_obj;
static CONSTEXPR const binop<SMAX> vfmax_obj;
static CONSTEXPR const float_misc<UNSPEC_VCOPYSIGN> vfsgnj_obj;
static CONSTEXPR const vfsgnjn vfsgnjn_obj;
static CONSTEXPR const float_misc<UNSPEC_VXORSIGN> vfsgnjx_obj;
static CONSTEXPR const unop<NEG> vfneg_obj;
static CONSTEXPR const unop<ABS> vfabs_obj;
static CONSTEXPR const fcmp<EQ> vmfeq_obj;
static CONSTEXPR const fcmp<NE> vmfne_obj;
static CONSTEXPR const fcmp<LT> vmflt_obj;
static CONSTEXPR const fcmp<GT> vmfgt_obj;
static CONSTEXPR const fcmp<LE> vmfle_obj;
static CONSTEXPR const fcmp<GE> vmfge_obj;
static CONSTEXPR const vfclass vfclass_obj;
static CONSTEXPR const vmerge vfmerge_obj;
static CONSTEXPR const vmv_v vfmv_v_obj;
static CONSTEXPR const vfcvt_x<UNSPEC_VFCVT> vfcvt_x_obj;
static CONSTEXPR const vfcvt_x<UNSPEC_VFCVT, HAS_FRM> vfcvt_x_frm_obj;
static CONSTEXPR const vfcvt_x<UNSPEC_UNSIGNED_VFCVT> vfcvt_xu_obj;
static CONSTEXPR const vfcvt_x<UNSPEC_UNSIGNED_VFCVT, HAS_FRM> vfcvt_xu_frm_obj;
static CONSTEXPR const vfcvt_rtz_x<FIX> vfcvt_rtz_x_obj;
static CONSTEXPR const vfcvt_rtz_x<UNSIGNED_FIX> vfcvt_rtz_xu_obj;
static CONSTEXPR const vfcvt_f<NO_FRM> vfcvt_f_obj;
static CONSTEXPR const vfcvt_f<HAS_FRM> vfcvt_f_frm_obj;
static CONSTEXPR const vfwcvt_x<UNSPEC_VFCVT> vfwcvt_x_obj;
static CONSTEXPR const vfwcvt_x<UNSPEC_VFCVT, HAS_FRM> vfwcvt_x_frm_obj;
static CONSTEXPR const vfwcvt_x<UNSPEC_UNSIGNED_VFCVT> vfwcvt_xu_obj;
static CONSTEXPR const vfwcvt_x<UNSPEC_UNSIGNED_VFCVT, HAS_FRM> vfwcvt_xu_frm_obj;
static CONSTEXPR const vfwcvt_rtz_x<FIX> vfwcvt_rtz_x_obj;
static CONSTEXPR const vfwcvt_rtz_x<UNSIGNED_FIX> vfwcvt_rtz_xu_obj;
static CONSTEXPR const vfwcvt_f vfwcvt_f_obj;
static CONSTEXPR const vfncvt_x<UNSPEC_VFCVT> vfncvt_x_obj;
static CONSTEXPR const vfncvt_x<UNSPEC_VFCVT, HAS_FRM> vfncvt_x_frm_obj;
static CONSTEXPR const vfncvt_x<UNSPEC_UNSIGNED_VFCVT> vfncvt_xu_obj;
static CONSTEXPR const vfncvt_x<UNSPEC_UNSIGNED_VFCVT, HAS_FRM> vfncvt_xu_frm_obj;
static CONSTEXPR const vfncvt_rtz_x<FIX> vfncvt_rtz_x_obj;
static CONSTEXPR const vfncvt_rtz_x<UNSIGNED_FIX> vfncvt_rtz_xu_obj;
static CONSTEXPR const vfncvt_f<NO_FRM> vfncvt_f_obj;
static CONSTEXPR const vfncvt_f<HAS_FRM> vfncvt_f_frm_obj;
static CONSTEXPR const vfncvt_rod_f vfncvt_rod_f_obj;
static CONSTEXPR const reducop<UNSPEC_REDUC_SUM> vredsum_obj;
static CONSTEXPR const reducop<UNSPEC_REDUC_MAXU> vredmaxu_obj;
static CONSTEXPR const reducop<UNSPEC_REDUC_MAX> vredmax_obj;
static CONSTEXPR const reducop<UNSPEC_REDUC_MINU> vredminu_obj;
static CONSTEXPR const reducop<UNSPEC_REDUC_MIN> vredmin_obj;
static CONSTEXPR const reducop<UNSPEC_REDUC_AND> vredand_obj;
static CONSTEXPR const reducop<UNSPEC_REDUC_OR> vredor_obj;
static CONSTEXPR const reducop<UNSPEC_REDUC_XOR> vredxor_obj;
static CONSTEXPR const reducop<UNSPEC_WREDUC_SUM> vwredsum_obj;
static CONSTEXPR const reducop<UNSPEC_WREDUC_SUMU> vwredsumu_obj;
static CONSTEXPR const freducop<UNSPEC_REDUC_SUM_UNORDERED> vfredusum_obj;
static CONSTEXPR const freducop<UNSPEC_REDUC_SUM_UNORDERED, HAS_FRM> vfredusum_frm_obj;
static CONSTEXPR const freducop<UNSPEC_REDUC_SUM_ORDERED> vfredosum_obj;
static CONSTEXPR const freducop<UNSPEC_REDUC_SUM_ORDERED, HAS_FRM> vfredosum_frm_obj;
static CONSTEXPR const reducop<UNSPEC_REDUC_MAX> vfredmax_obj;
static CONSTEXPR const reducop<UNSPEC_REDUC_MIN> vfredmin_obj;
static CONSTEXPR const freducop<UNSPEC_WREDUC_SUM_UNORDERED> vfwredusum_obj;
static CONSTEXPR const freducop<UNSPEC_WREDUC_SUM_UNORDERED, HAS_FRM> vfwredusum_frm_obj;
static CONSTEXPR const freducop<UNSPEC_WREDUC_SUM_ORDERED> vfwredosum_obj;
static CONSTEXPR const freducop<UNSPEC_WREDUC_SUM_ORDERED, HAS_FRM> vfwredosum_frm_obj;
static CONSTEXPR const vmv vmv_x_obj;
static CONSTEXPR const vmv_s vmv_s_obj;
static CONSTEXPR const vmv vfmv_f_obj;
static CONSTEXPR const vmv_s vfmv_s_obj;
static CONSTEXPR const slideop<UNSPEC_VSLIDEUP> vslideup_obj;
static CONSTEXPR const slideop<UNSPEC_VSLIDEDOWN> vslidedown_obj;
static CONSTEXPR const slideop<UNSPEC_VSLIDE1UP> vslide1up_obj;
static CONSTEXPR const slideop<UNSPEC_VSLIDE1DOWN> vslide1down_obj;
static CONSTEXPR const slideop<UNSPEC_VFSLIDE1UP> vfslide1up_obj;
static CONSTEXPR const slideop<UNSPEC_VFSLIDE1DOWN> vfslide1down_obj;
static CONSTEXPR const vrgather vrgather_obj;
static CONSTEXPR const vrgatherei16 vrgatherei16_obj;
static CONSTEXPR const vcompress vcompress_obj;
static CONSTEXPR const vundefined vundefined_obj;
static CONSTEXPR const vreinterpret vreinterpret_obj;
static CONSTEXPR const vlmul_ext vlmul_ext_obj;
static CONSTEXPR const vlmul_trunc vlmul_trunc_obj;
static CONSTEXPR const vset vset_obj;
static CONSTEXPR const vget vget_obj;
static CONSTEXPR const vcreate vcreate_obj;
static CONSTEXPR const read_vl read_vl_obj;
static CONSTEXPR const vleff vleff_obj;
static CONSTEXPR const vlenb vlenb_obj;
static CONSTEXPR const vlseg vlseg_obj;
static CONSTEXPR const vsseg vsseg_obj;
static CONSTEXPR const vlsseg vlsseg_obj;
static CONSTEXPR const vssseg vssseg_obj;
static CONSTEXPR const seg_indexed_load<UNSPEC_UNORDERED> vluxseg_obj;
static CONSTEXPR const seg_indexed_load<UNSPEC_ORDERED> vloxseg_obj;
static CONSTEXPR const seg_indexed_store<UNSPEC_UNORDERED> vsuxseg_obj;
static CONSTEXPR const seg_indexed_store<UNSPEC_ORDERED> vsoxseg_obj;
static CONSTEXPR const vlsegff vlsegff_obj;
static CONSTEXPR const th_loadstore_width<false, LST_UNIT_STRIDE, UNSPEC_TH_VLB> vlb_obj;
static CONSTEXPR const th_loadstore_width<false, LST_UNIT_STRIDE, UNSPEC_TH_VLBU> vlbu_obj;
static CONSTEXPR const th_loadstore_width<false, LST_UNIT_STRIDE, UNSPEC_TH_VLH> vlh_obj;
static CONSTEXPR const th_loadstore_width<false, LST_UNIT_STRIDE, UNSPEC_TH_VLHU> vlhu_obj;
static CONSTEXPR const th_loadstore_width<false, LST_UNIT_STRIDE, UNSPEC_TH_VLW> vlw_obj;
static CONSTEXPR const th_loadstore_width<false, LST_UNIT_STRIDE, UNSPEC_TH_VLWU> vlwu_obj;
static CONSTEXPR const th_loadstore_width<true, LST_UNIT_STRIDE, UNSPEC_TH_VLB> vsb_obj;
static CONSTEXPR const th_loadstore_width<true, LST_UNIT_STRIDE, UNSPEC_TH_VLH> vsh_obj;
static CONSTEXPR const th_loadstore_width<true, LST_UNIT_STRIDE, UNSPEC_TH_VLW> vsw_obj;
static CONSTEXPR const th_loadstore_width<false, LST_STRIDED, UNSPEC_TH_VLSB> vlsb_obj;
static CONSTEXPR const th_loadstore_width<false, LST_STRIDED, UNSPEC_TH_VLSBU> vlsbu_obj;
static CONSTEXPR const th_loadstore_width<false, LST_STRIDED, UNSPEC_TH_VLSH> vlsh_obj;
static CONSTEXPR const th_loadstore_width<false, LST_STRIDED, UNSPEC_TH_VLSHU> vlshu_obj;
static CONSTEXPR const th_loadstore_width<false, LST_STRIDED, UNSPEC_TH_VLSW> vlsw_obj;
static CONSTEXPR const th_loadstore_width<false, LST_STRIDED, UNSPEC_TH_VLSWU> vlswu_obj;
static CONSTEXPR const th_loadstore_width<true, LST_STRIDED, UNSPEC_TH_VLSB> vssb_obj;
static CONSTEXPR const th_loadstore_width<true, LST_STRIDED, UNSPEC_TH_VLSH> vssh_obj;
static CONSTEXPR const th_loadstore_width<true, LST_STRIDED, UNSPEC_TH_VLSW> vssw_obj;
static CONSTEXPR const th_loadstore_width<false, LST_INDEXED, UNSPEC_TH_VLXB> vlxb_obj;
static CONSTEXPR const th_loadstore_width<false, LST_INDEXED, UNSPEC_TH_VLXBU> vlxbu_obj;
static CONSTEXPR const th_loadstore_width<false, LST_INDEXED, UNSPEC_TH_VLXH> vlxh_obj;
static CONSTEXPR const th_loadstore_width<false, LST_INDEXED, UNSPEC_TH_VLXHU> vlxhu_obj;
static CONSTEXPR const th_loadstore_width<false, LST_INDEXED, UNSPEC_TH_VLXW> vlxw_obj;
static CONSTEXPR const th_loadstore_width<false, LST_INDEXED, UNSPEC_TH_VLXWU> vlxwu_obj;
static CONSTEXPR const th_loadstore_width<true, LST_INDEXED, UNSPEC_TH_VLXB> vsxb_obj;
static CONSTEXPR const th_loadstore_width<true, LST_INDEXED, UNSPEC_TH_VLXH> vsxh_obj;
static CONSTEXPR const th_loadstore_width<true, LST_INDEXED, UNSPEC_TH_VLXW> vsxw_obj;
static CONSTEXPR const th_loadstore_width<true, LST_INDEXED, UNSPEC_TH_VSUXB> vsuxb_obj;
static CONSTEXPR const th_loadstore_width<true, LST_INDEXED, UNSPEC_TH_VSUXH> vsuxh_obj;
static CONSTEXPR const th_loadstore_width<true, LST_INDEXED, UNSPEC_TH_VSUXW> vsuxw_obj;
static CONSTEXPR const th_extract vext_x_v_obj;

/* Crypto Vector */
static CONSTEXPR const vandn vandn_obj;
static CONSTEXPR const bitmanip<ROTATE>   vrol_obj;
static CONSTEXPR const bitmanip<ROTATERT> vror_obj;
static CONSTEXPR const b_reverse<UNSPEC_VBREV>   vbrev_obj;
static CONSTEXPR const b_reverse<UNSPEC_VBREV8>  vbrev8_obj;
static CONSTEXPR const b_reverse<UNSPEC_VREV8>   vrev8_obj;
static CONSTEXPR const bitmanip<CLZ> vclz_obj;
static CONSTEXPR const bitmanip<CTZ> vctz_obj;
static CONSTEXPR const vwsll vwsll_obj;
static CONSTEXPR const clmul<UNSPEC_VCLMUL>      vclmul_obj;
static CONSTEXPR const clmul<UNSPEC_VCLMULH>     vclmulh_obj;
static CONSTEXPR const vg_nhab<UNSPEC_VGHSH>     vghsh_obj;
static CONSTEXPR const crypto_vv<UNSPEC_VGMUL>   vgmul_obj;
static CONSTEXPR const crypto_vv<UNSPEC_VAESEF>  vaesef_obj;
static CONSTEXPR const crypto_vv<UNSPEC_VAESEM>  vaesem_obj;
static CONSTEXPR const crypto_vv<UNSPEC_VAESDF>  vaesdf_obj;
static CONSTEXPR const crypto_vv<UNSPEC_VAESDM>  vaesdm_obj;
static CONSTEXPR const crypto_vv<UNSPEC_VAESZ>   vaesz_obj;
static CONSTEXPR const crypto_vi<UNSPEC_VAESKF1> vaeskf1_obj;
static CONSTEXPR const vaeskf2_vsm3c<UNSPEC_VAESKF2> vaeskf2_obj;
static CONSTEXPR const vg_nhab<UNSPEC_VSHA2MS>   vsha2ms_obj;
static CONSTEXPR const vg_nhab<UNSPEC_VSHA2CH>   vsha2ch_obj;
static CONSTEXPR const vg_nhab<UNSPEC_VSHA2CL>   vsha2cl_obj;
static CONSTEXPR const crypto_vi<UNSPEC_VSM4K>   vsm4k_obj;
static CONSTEXPR const crypto_vv<UNSPEC_VSM4R>   vsm4r_obj;
static CONSTEXPR const vsm3me vsm3me_obj;
static CONSTEXPR const vaeskf2_vsm3c<UNSPEC_VSM3C>   vsm3c_obj;

/* Zvfbfmin */
static CONSTEXPR const vfncvtbf16_f<NO_FRM> vfncvtbf16_f_obj;
static CONSTEXPR const vfncvtbf16_f<HAS_FRM> vfncvtbf16_f_frm_obj;
static CONSTEXPR const vfwcvtbf16_f vfwcvtbf16_f_obj;
/* Zvfbfwma; */
static CONSTEXPR const vfwmaccbf16<NO_FRM> vfwmaccbf16_obj;
static CONSTEXPR const vfwmaccbf16<HAS_FRM> vfwmaccbf16_frm_obj;

/* Emit the runtime IME vtype setup sequence when VLEN is not
   compile-time known.  Called by IME intrinsic expanders before
   emitting the IME instruction.  If VLEN IS known at compile time,
   this is a no-op (the vsetvl pass handles the static path).

   ACC_MODE is the accumulator vector mode (determines SEW and LMUL).
   IS_TILE_OP should be true for tile load/store instructions where
   MUL_C is always 1 regardless of LMUL.
   ALTFMT_A, ALTFMT_B, ALTFMT, BS are the IME-specific vtype fields.
   E is the function_expander, used to extract the AVL argument.  */

/* Return the altfmt_A value for an integer IME UNSPEC.  altfmt_A
   encodes the signedness of the A operand: false = signed, true =
   unsigned.  */

static bool
ime_unspec_altfmt_a (int unspec)
{
  switch (unspec)
    {
    case UNSPEC_VMMACC_U:
    case UNSPEC_VMMACC_US:
    case UNSPEC_VWMMACC_U:
    case UNSPEC_VWMMACC_US:
    case UNSPEC_VQWMMACC_U:
    case UNSPEC_VQWMMACC_US:
    case UNSPEC_V8WMMACC_U:
    case UNSPEC_V8WMMACC_US:
      return true;
    default:
      return false;
    }
}

/* Return the altfmt_B value for an integer IME UNSPEC.  */

static bool
ime_unspec_altfmt_b (int unspec)
{
  switch (unspec)
    {
    case UNSPEC_VMMACC_U:
    case UNSPEC_VMMACC_SU:
    case UNSPEC_VWMMACC_U:
    case UNSPEC_VWMMACC_SU:
    case UNSPEC_VQWMMACC_U:
    case UNSPEC_VQWMMACC_SU:
    case UNSPEC_V8WMMACC_U:
    case UNSPEC_V8WMMACC_SU:
      return true;
    default:
      return false;
    }
}

/* (altfmt_A, altfmt_B, altfmt) triple for OFP8 mmacc UNSPECs.  Mirrors
   the per-pattern ime_fp_altfmt MD attribute that the static-VLEN path
   reads in riscv_extract_ime_requirements (see riscv-vsetvl.cc).  Both
   paths must agree, otherwise the runtime probe writes a vtype with
   the wrong altfmt and the WARL readback traps (or worse, silently
   computes in the wrong format).

   Encoding per token: false = E4M3 (= 0), true = E5M2 (= 1).  The acc
   bit (val) carries E4M3/E5M2 at SEW=8 (W=1) and FP16/BF16 at SEW=16
   (W=2); at SEW>=32 (W=4, W=8) the acc altfmt is fixed at 0.  */

struct ime_ofp8_altfmt
{
  bool a;    /* altfmt_A -- vs1 input format.  */
  bool b;    /* altfmt_B -- vs2 input format.  */
  bool val;  /* altfmt   -- accumulator/output format.  */
};

static ime_ofp8_altfmt
ime_unspec_ofp8_altfmt (int unspec)
{
  switch (unspec)
    {
    /* W=1 vfmmacc OFP8 -> OFP8 (UNSPEC name: F8_<acc>_<vs1>_<vs2>).  */
    case UNSPEC_VFMMACC_F8_E4M3_E4M3_E4M3: return { false, false, false };
    case UNSPEC_VFMMACC_F8_E4M3_E4M3_E5M2: return { false, true,  false };
    case UNSPEC_VFMMACC_F8_E4M3_E5M2_E4M3: return { true,  false, false };
    case UNSPEC_VFMMACC_F8_E4M3_E5M2_E5M2: return { true,  true,  false };
    case UNSPEC_VFMMACC_F8_E5M2_E4M3_E4M3: return { false, false, true  };
    case UNSPEC_VFMMACC_F8_E5M2_E4M3_E5M2: return { false, true,  true  };
    case UNSPEC_VFMMACC_F8_E5M2_E5M2_E4M3: return { true,  false, true  };
    case UNSPEC_VFMMACC_F8_E5M2_E5M2_E5M2: return { true,  true,  true  };
    /* W=2 vfwmmacc OFP8 -> FP16 (acc altfmt = 0).  */
    case UNSPEC_VFWMMACC_OFP8_FP16_E4M3:      return { false, false, false };
    case UNSPEC_VFWMMACC_OFP8_FP16_E4M3_E5M2: return { false, true,  false };
    case UNSPEC_VFWMMACC_OFP8_FP16_E5M2_E4M3: return { true,  false, false };
    case UNSPEC_VFWMMACC_OFP8_FP16_E5M2:      return { true,  true,  false };
    /* W=2 vfwmmacc OFP8 -> BF16 (acc altfmt = 1).  */
    case UNSPEC_VFWMMACC_OFP8_BF16_E4M3:      return { false, false, true };
    case UNSPEC_VFWMMACC_OFP8_BF16_E4M3_E5M2: return { false, true,  true };
    case UNSPEC_VFWMMACC_OFP8_BF16_E5M2_E4M3: return { true,  false, true };
    case UNSPEC_VFWMMACC_OFP8_BF16_E5M2:      return { true,  true,  true };
    /* W=4 / W=8 OFP8 (acc altfmt fixed at 0 for SEW>=32).  */
    case UNSPEC_VFQWMMACC_F8E4M3:      return { false, false, false };
    case UNSPEC_VFQWMMACC_F8E5M2:      return { true,  true,  false };
    case UNSPEC_VFQWMMACC_F8E4M3_E5M2: return { false, true,  false };
    case UNSPEC_VFQWMMACC_F8E5M2_E4M3: return { true,  false, false };
    case UNSPEC_VF8WMMACC_F8E4M3:      return { false, false, false };
    case UNSPEC_VF8WMMACC_F8E5M2:      return { true,  true,  false };
    case UNSPEC_VF8WMMACC_F8E4M3_E5M2: return { false, true,  false };
    case UNSPEC_VF8WMMACC_F8E5M2_E4M3: return { true,  false, false };
    default:
      gcc_unreachable ();
    }
}


/* Anchor an IME consumer in scheduling order on the runtime-VLEN path.

   The instruction scheduler runs at sched1 (RTL pass 324r), well
   before the vsetvl pass (348r) post-reload-splits
   `@vsetvl<mode>_no_side_effects' (vector.md:1843) into a real vsetvl
   that visibly writes vtype.  Until that split the scheduler sees the
   pattern as having no vtype side-effect -- no anti-dependency from
   an IME consumer (which reads VL_REGNUM/VTYPE_REGNUM) to a following
   user-emitted `__riscv_vsetvl_*' -- and feels free to hoist the
   user vsetvl past the IME consumer for register-pressure reasons.
   That breaks the IME setup the runtime probe established.

   `gen_blockage ()' emits an `unspec_volatile' ghost insn (no
   codegen, no latency, type=ghost) that the scheduler treats as a
   full barrier: nothing crosses it in either direction.  Emitted
   immediately AFTER each IME consumer it pins the consumer's
   scheduling position relative to surrounding vsetvls.

   Static-VLEN compilation produces no runtime probe and the user
   vsetvl ends up correctly ordered without help, so we limit the
   blockage emission to the runtime path.  See the gap-9 reproducer
   in examples/src/repro-vfwmmacc-vmtl-sew.cxx and the corresponding
   plan in .claude/plans/gap9-anchor-runtime-ime-vsetvl.md.  */

static void
emit_ime_post_consumer_blockage ()
{
  if (!BYTES_PER_RISCV_VECTOR.is_constant ())
    emit_insn (gen_blockage ());
}

/* Emit the runtime-VLEN IME vtype setup for an mmacc expander.
   `acc_mode' is the accumulator's mode (gives SEW and MUL_C).  The
   altfmt_* / bs flags carry per-UNSPEC IME format bits (defaulted to
   false at the call site for integer / non-microscaled mmacc).  No
   work is needed on the static-VLEN path: the vsetvl pass derives
   the same vtype from `requires_ime_vtype = yes' on the mmacc
   pattern.

   IME tile load/store ops do *not* call this helper.  Per the IME
   spec they read vtype.lambda dynamically (or override it via the
   `Lλ' immediate); the surrounding mmacc -- not the tile op --
   programs the lambda field.  An ordinary `vsetvli' for the tile
   op's vsew/vlmul/vl is enough; vtype.lambda survives across it via
   WARL.  See the commit history for the rationale (the previous
   "tile ops infer their MUL_C from a function-wide mmacc walker"
   approach was both conceptually wrong and a source of silent
   miscompares when the walker hit ambiguous geometries).  */

static void
maybe_emit_ime_runtime_vtype (function_expander &e,
			      machine_mode acc_mode,
			      bool altfmt_a, bool altfmt_b,
			      bool altfmt_val, bool bs_val)
{
  if (BYTES_PER_RISCV_VECTOR.is_constant ())
    return;  /* Static path: vsetvl pass handles it.  */

  int sew_bits = GET_MODE_UNIT_BITSIZE (acc_mode);
  vlmul_type acc_vlmul = get_vlmul (acc_mode);

  /* MUL_C equals the accumulator's register-group multiplier.  */
  int mul_c;
  switch (acc_vlmul)
    {
    case LMUL_1: mul_c = 1; break;
    case LMUL_2: mul_c = 2; break;
    case LMUL_4: mul_c = 4; break;
    case LMUL_8: mul_c = 8; break;
    default:
      /* Fractional LMUL: invalid for IME accumulators.  */
      gcc_unreachable ();
    }

  /* vs1's LMUL is the source-side LMUL (the _lm{N} variant choice).  */
  vlmul_type src_vlmul = get_vlmul (e.arg_mode (1));

  /* For FP mmacc, derive altfmt bits from BFmode operands per the IME
     spec (mirrors the vsetvl pass logic in riscv_extract_ime_requirements).
     Per-input inspection lets a future mixed-format intrinsic (e.g.
     fp16 x bf16) drop in without further changes.  */
  if (GET_MODE_INNER (acc_mode) == E_BFmode)
    altfmt_val = true;
  if (GET_MODE_INNER (e.arg_mode (1)) == E_BFmode)
    altfmt_a = true;
  if (e.op_info->args[2].base_type != NUM_BASE_TYPES
      && GET_MODE_INNER (e.arg_mode (2)) == E_BFmode)
    altfmt_b = true;

  /* The VL argument is always the last call argument.  */
  int vl_argno = call_expr_nargs (e.exp) - 1;
  rtx avl = expand_normal (CALL_EXPR_ARG (e.exp, vl_argno));

  emit_ime_runtime_vtype_setup (sew_bits, mul_c, src_vlmul,
				altfmt_a, altfmt_b,
				altfmt_val, bs_val,
				/* ta = */ true, /* ma = */ true,
				avl, /* static_lambda_log2 = */ -1);
}

/* IME vector integer matrix multiply-accumulate (W=1).  Expands to
   @pred_vmmacc_s<mode> or @pred_vmmacc_u<mode>, templated on the
   UNSPEC code.  The mode iterator is the accumulator mode (operand 0)
   and the input operands are at LMUL=1 via V_LMUL1.  Signed and
   unsigned emit the same vmmacc.vv instruction; they differ only in
   which UNSPEC is on the RTL, so the vsetvl pass can read the
   ime_altfmt attribute to populate altfmt_A/altfmt_B correctly.  No
   mask/merge/policy operands.  */
template<int UNSPEC, insn_code (*CODE_FOR)(int, machine_mode) = code_for_pred_vmmacc>
class vmmacc_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  ime_unspec_altfmt_a (UNSPEC),
				  ime_unspec_altfmt_b (UNSPEC),
				  /* altfmt = */ false,
				  /* bs = */ false);
    rtx result = e.use_exact_insn (CODE_FOR (UNSPEC, e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_S> vmmacc_s_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_U> vmmacc_u_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_S, code_for_pred_vmmacc_lm2>
  vmmacc_s_lm2_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_S, code_for_pred_vmmacc_lm4>
  vmmacc_s_lm4_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_S, code_for_pred_vmmacc_lm8>
  vmmacc_s_lm8_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_U, code_for_pred_vmmacc_lm2>
  vmmacc_u_lm2_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_U, code_for_pred_vmmacc_lm4>
  vmmacc_u_lm4_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_U, code_for_pred_vmmacc_lm8>
  vmmacc_u_lm8_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_SU> vmmacc_su_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_US> vmmacc_us_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_SU, code_for_pred_vmmacc_lm2>
  vmmacc_su_lm2_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_SU, code_for_pred_vmmacc_lm4>
  vmmacc_su_lm4_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_SU, code_for_pred_vmmacc_lm8>
  vmmacc_su_lm8_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_US, code_for_pred_vmmacc_lm2>
  vmmacc_us_lm2_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_US, code_for_pred_vmmacc_lm4>
  vmmacc_us_lm4_obj;
static CONSTEXPR const vmmacc_ime_op<UNSPEC_VMMACC_US, code_for_pred_vmmacc_lm8>
  vmmacc_us_lm8_obj;

/* IME vector widening integer matrix multiply-accumulate (W=2).  Same
   structure as vmmacc_ime_op but dispatches to @pred_vwmmacc*<mode>.
   Four instances cover ss, uu, su, us.  The optional CODE_FOR template
   parameter selects between the default LMUL=1 pattern and the
   _lm{2,4,8} source-LMUL siblings.  */
template<int UNSPEC,
	 insn_code (*CODE_FOR)(int, machine_mode) = code_for_pred_vwmmacc>
class vwmmacc_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  ime_unspec_altfmt_a (UNSPEC),
				  ime_unspec_altfmt_b (UNSPEC),
				  /* altfmt = */ false,
				  /* bs = */ false);
    rtx result = e.use_exact_insn (CODE_FOR (UNSPEC, e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const vwmmacc_ime_op<UNSPEC_VWMMACC_S>  vwmmacc_s_obj;
static CONSTEXPR const vwmmacc_ime_op<UNSPEC_VWMMACC_U>  vwmmacc_u_obj;
static CONSTEXPR const vwmmacc_ime_op<UNSPEC_VWMMACC_SU> vwmmacc_su_obj;
static CONSTEXPR const vwmmacc_ime_op<UNSPEC_VWMMACC_US> vwmmacc_us_obj;
static CONSTEXPR const
  vwmmacc_ime_op<UNSPEC_VWMMACC_S, code_for_pred_vwmmacc_lm2> vwmmacc_s_lm2_obj;
static CONSTEXPR const
  vwmmacc_ime_op<UNSPEC_VWMMACC_S, code_for_pred_vwmmacc_lm4> vwmmacc_s_lm4_obj;
static CONSTEXPR const
  vwmmacc_ime_op<UNSPEC_VWMMACC_S, code_for_pred_vwmmacc_lm8> vwmmacc_s_lm8_obj;
static CONSTEXPR const
  vwmmacc_ime_op<UNSPEC_VWMMACC_U, code_for_pred_vwmmacc_lm2> vwmmacc_u_lm2_obj;
static CONSTEXPR const
  vwmmacc_ime_op<UNSPEC_VWMMACC_U, code_for_pred_vwmmacc_lm4> vwmmacc_u_lm4_obj;
static CONSTEXPR const
  vwmmacc_ime_op<UNSPEC_VWMMACC_U, code_for_pred_vwmmacc_lm8> vwmmacc_u_lm8_obj;
static CONSTEXPR const
  vwmmacc_ime_op<UNSPEC_VWMMACC_SU, code_for_pred_vwmmacc_lm2>
    vwmmacc_su_lm2_obj;
static CONSTEXPR const
  vwmmacc_ime_op<UNSPEC_VWMMACC_SU, code_for_pred_vwmmacc_lm4>
    vwmmacc_su_lm4_obj;
static CONSTEXPR const
  vwmmacc_ime_op<UNSPEC_VWMMACC_SU, code_for_pred_vwmmacc_lm8>
    vwmmacc_su_lm8_obj;
static CONSTEXPR const
  vwmmacc_ime_op<UNSPEC_VWMMACC_US, code_for_pred_vwmmacc_lm2>
    vwmmacc_us_lm2_obj;
static CONSTEXPR const
  vwmmacc_ime_op<UNSPEC_VWMMACC_US, code_for_pred_vwmmacc_lm4>
    vwmmacc_us_lm4_obj;
static CONSTEXPR const
  vwmmacc_ime_op<UNSPEC_VWMMACC_US, code_for_pred_vwmmacc_lm8>
    vwmmacc_us_lm8_obj;

/* IME vector widening integer matrix multiply-accumulate (W=4).  */
template<int UNSPEC,
	 insn_code (*CODE_FOR)(int, machine_mode) = code_for_pred_vqwmmacc>
class vqwmmacc_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  ime_unspec_altfmt_a (UNSPEC),
				  ime_unspec_altfmt_b (UNSPEC),
				  /* altfmt = */ false,
				  /* bs = */ false);
    rtx result = e.use_exact_insn (CODE_FOR (UNSPEC, e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const vqwmmacc_ime_op<UNSPEC_VQWMMACC_S>  vqwmmacc_s_obj;
static CONSTEXPR const vqwmmacc_ime_op<UNSPEC_VQWMMACC_U>  vqwmmacc_u_obj;
static CONSTEXPR const vqwmmacc_ime_op<UNSPEC_VQWMMACC_SU> vqwmmacc_su_obj;
static CONSTEXPR const vqwmmacc_ime_op<UNSPEC_VQWMMACC_US> vqwmmacc_us_obj;
static CONSTEXPR const
  vqwmmacc_ime_op<UNSPEC_VQWMMACC_S, code_for_pred_vqwmmacc_lm2>
    vqwmmacc_s_lm2_obj;
static CONSTEXPR const
  vqwmmacc_ime_op<UNSPEC_VQWMMACC_S, code_for_pred_vqwmmacc_lm4>
    vqwmmacc_s_lm4_obj;
static CONSTEXPR const
  vqwmmacc_ime_op<UNSPEC_VQWMMACC_S, code_for_pred_vqwmmacc_lm8>
    vqwmmacc_s_lm8_obj;
static CONSTEXPR const
  vqwmmacc_ime_op<UNSPEC_VQWMMACC_U, code_for_pred_vqwmmacc_lm2>
    vqwmmacc_u_lm2_obj;
static CONSTEXPR const
  vqwmmacc_ime_op<UNSPEC_VQWMMACC_U, code_for_pred_vqwmmacc_lm4>
    vqwmmacc_u_lm4_obj;
static CONSTEXPR const
  vqwmmacc_ime_op<UNSPEC_VQWMMACC_U, code_for_pred_vqwmmacc_lm8>
    vqwmmacc_u_lm8_obj;
static CONSTEXPR const
  vqwmmacc_ime_op<UNSPEC_VQWMMACC_SU, code_for_pred_vqwmmacc_lm2>
    vqwmmacc_su_lm2_obj;
static CONSTEXPR const
  vqwmmacc_ime_op<UNSPEC_VQWMMACC_SU, code_for_pred_vqwmmacc_lm4>
    vqwmmacc_su_lm4_obj;
static CONSTEXPR const
  vqwmmacc_ime_op<UNSPEC_VQWMMACC_SU, code_for_pred_vqwmmacc_lm8>
    vqwmmacc_su_lm8_obj;
static CONSTEXPR const
  vqwmmacc_ime_op<UNSPEC_VQWMMACC_US, code_for_pred_vqwmmacc_lm2>
    vqwmmacc_us_lm2_obj;
static CONSTEXPR const
  vqwmmacc_ime_op<UNSPEC_VQWMMACC_US, code_for_pred_vqwmmacc_lm4>
    vqwmmacc_us_lm4_obj;
static CONSTEXPR const
  vqwmmacc_ime_op<UNSPEC_VQWMMACC_US, code_for_pred_vqwmmacc_lm8>
    vqwmmacc_us_lm8_obj;

/* IME vector widening integer matrix multiply-accumulate (W=8).  */
template<int UNSPEC,
	 insn_code (*CODE_FOR)(int, machine_mode) = code_for_pred_v8wmmacc>
class v8wmmacc_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  ime_unspec_altfmt_a (UNSPEC),
				  ime_unspec_altfmt_b (UNSPEC),
				  /* altfmt = */ false,
				  /* bs = */ false);
    rtx result = e.use_exact_insn (CODE_FOR (UNSPEC, e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const v8wmmacc_ime_op<UNSPEC_V8WMMACC_S>  v8wmmacc_s_obj;
static CONSTEXPR const v8wmmacc_ime_op<UNSPEC_V8WMMACC_U>  v8wmmacc_u_obj;
static CONSTEXPR const v8wmmacc_ime_op<UNSPEC_V8WMMACC_SU> v8wmmacc_su_obj;
static CONSTEXPR const v8wmmacc_ime_op<UNSPEC_V8WMMACC_US> v8wmmacc_us_obj;
static CONSTEXPR const
  v8wmmacc_ime_op<UNSPEC_V8WMMACC_S, code_for_pred_v8wmmacc_lm2>
    v8wmmacc_s_lm2_obj;
static CONSTEXPR const
  v8wmmacc_ime_op<UNSPEC_V8WMMACC_S, code_for_pred_v8wmmacc_lm4>
    v8wmmacc_s_lm4_obj;
static CONSTEXPR const
  v8wmmacc_ime_op<UNSPEC_V8WMMACC_S, code_for_pred_v8wmmacc_lm8>
    v8wmmacc_s_lm8_obj;
static CONSTEXPR const
  v8wmmacc_ime_op<UNSPEC_V8WMMACC_U, code_for_pred_v8wmmacc_lm2>
    v8wmmacc_u_lm2_obj;
static CONSTEXPR const
  v8wmmacc_ime_op<UNSPEC_V8WMMACC_U, code_for_pred_v8wmmacc_lm4>
    v8wmmacc_u_lm4_obj;
static CONSTEXPR const
  v8wmmacc_ime_op<UNSPEC_V8WMMACC_U, code_for_pred_v8wmmacc_lm8>
    v8wmmacc_u_lm8_obj;
static CONSTEXPR const
  v8wmmacc_ime_op<UNSPEC_V8WMMACC_SU, code_for_pred_v8wmmacc_lm2>
    v8wmmacc_su_lm2_obj;
static CONSTEXPR const
  v8wmmacc_ime_op<UNSPEC_V8WMMACC_SU, code_for_pred_v8wmmacc_lm4>
    v8wmmacc_su_lm4_obj;
static CONSTEXPR const
  v8wmmacc_ime_op<UNSPEC_V8WMMACC_SU, code_for_pred_v8wmmacc_lm8>
    v8wmmacc_su_lm8_obj;
static CONSTEXPR const
  v8wmmacc_ime_op<UNSPEC_V8WMMACC_US, code_for_pred_v8wmmacc_lm2>
    v8wmmacc_us_lm2_obj;
static CONSTEXPR const
  v8wmmacc_ime_op<UNSPEC_V8WMMACC_US, code_for_pred_v8wmmacc_lm4>
    v8wmmacc_us_lm4_obj;
static CONSTEXPR const
  v8wmmacc_ime_op<UNSPEC_V8WMMACC_US, code_for_pred_v8wmmacc_lm8>
    v8wmmacc_us_lm8_obj;

/* Zvvfmm FP matrix multiply-accumulate, W=1 (vfmmacc.vv).  Parametrised
   on the icode-getter so the same class drives the default LMUL=1
   variant (vfmmacc) and the source-LMUL _lm{N} variants.  */
template<insn_code (*CODE_FOR)(machine_mode) = code_for_pred_vfmmacc>
class vfmmacc_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  /* altfmt_a = */ false,
				  /* altfmt_b = */ false,
				  /* altfmt = */ false,
				  /* bs = */ false);
    rtx result = e.use_exact_insn (CODE_FOR (e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const vfmmacc_ime_op<> vfmmacc_obj;
static CONSTEXPR const vfmmacc_ime_op<code_for_pred_vfmmacc_lm2>
  vfmmacc_lm2_obj;
static CONSTEXPR const vfmmacc_ime_op<code_for_pred_vfmmacc_lm4>
  vfmmacc_lm4_obj;
static CONSTEXPR const vfmmacc_ime_op<code_for_pred_vfmmacc_lm8>
  vfmmacc_lm8_obj;

/* Zvvfmm FP matrix multiply-accumulate, W=2 (vfwmmacc.vv).  */
class vfwmmacc_f16_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  false, false, false, false);
    rtx result = e.use_exact_insn (code_for_pred_vfwmmacc_f16 (e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const vfwmmacc_f16_ime_op vfwmmacc_f16_obj;

class vfwmmacc_bf16_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  false, false, false, false);
    rtx result = e.use_exact_insn (code_for_pred_vfwmmacc_bf16 (e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const vfwmmacc_bf16_ime_op vfwmmacc_bf16_obj;

class vfwmmacc_f32_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  false, false, false, false);
    rtx result = e.use_exact_insn (code_for_pred_vfwmmacc_f32 (e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const vfwmmacc_f32_ime_op vfwmmacc_f32_obj;

/* Zvvfmm FP matrix multiply-accumulate, W=4 (vfqwmmacc.vv).  */
class vfqwmmacc_f16_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  false, false, false, false);
    rtx result = e.use_exact_insn (code_for_pred_vfqwmmacc_f16 (e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const vfqwmmacc_f16_ime_op vfqwmmacc_f16_obj;

class vfqwmmacc_bf16_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }

  rtx expand (function_expander &e) const override
  {
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  false, false, false, false);
    rtx result = e.use_exact_insn (code_for_pred_vfqwmmacc_bf16 (e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const vfqwmmacc_bf16_ime_op vfqwmmacc_bf16_obj;

/* W=2 vfwmmacc OFP8 -> fp16.  Acc mode is HF (altfmt = 0); inputs are
   OFP8 (E4M3 / E5M2) at LMUL=1 and the per-instance UNSPEC selects
   the (vs1, vs2) altfmt combination.  */
template<int UNSPEC>
class vfwmmacc_ofp8_fp16_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }
  bool can_be_overloaded_p (enum predication_type_index) const override
  { return false; }

  rtx expand (function_expander &e) const override
  {
    ime_ofp8_altfmt af = ime_unspec_ofp8_altfmt (UNSPEC);
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  af.a, af.b, af.val,
				  /* bs = */ false);
    rtx result = e.use_exact_insn (
      code_for_pred_vfwmmacc_ofp8_fp16 (UNSPEC, e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const
  vfwmmacc_ofp8_fp16_ime_op<UNSPEC_VFWMMACC_OFP8_FP16_E4M3>
  vfwmmacc_fp16_f8e4m3_obj;
static CONSTEXPR const
  vfwmmacc_ofp8_fp16_ime_op<UNSPEC_VFWMMACC_OFP8_FP16_E4M3_E5M2>
  vfwmmacc_fp16_f8e4m3_f8e5m2_obj;
static CONSTEXPR const
  vfwmmacc_ofp8_fp16_ime_op<UNSPEC_VFWMMACC_OFP8_FP16_E5M2_E4M3>
  vfwmmacc_fp16_f8e5m2_f8e4m3_obj;
static CONSTEXPR const
  vfwmmacc_ofp8_fp16_ime_op<UNSPEC_VFWMMACC_OFP8_FP16_E5M2>
  vfwmmacc_fp16_f8e5m2_obj;

/* W=2 vfwmmacc OFP8 -> bf16.  Mirror of the fp16 class above with
   acc altfmt = 1 and acc mode = BF.  */
template<int UNSPEC>
class vfwmmacc_ofp8_bf16_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }
  bool can_be_overloaded_p (enum predication_type_index) const override
  { return false; }

  rtx expand (function_expander &e) const override
  {
    ime_ofp8_altfmt af = ime_unspec_ofp8_altfmt (UNSPEC);
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  af.a, af.b, af.val,
				  /* bs = */ false);
    rtx result = e.use_exact_insn (
      code_for_pred_vfwmmacc_ofp8_bf16 (UNSPEC, e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const
  vfwmmacc_ofp8_bf16_ime_op<UNSPEC_VFWMMACC_OFP8_BF16_E4M3>
  vfwmmacc_bf16_f8e4m3_obj;
static CONSTEXPR const
  vfwmmacc_ofp8_bf16_ime_op<UNSPEC_VFWMMACC_OFP8_BF16_E4M3_E5M2>
  vfwmmacc_bf16_f8e4m3_f8e5m2_obj;
static CONSTEXPR const
  vfwmmacc_ofp8_bf16_ime_op<UNSPEC_VFWMMACC_OFP8_BF16_E5M2_E4M3>
  vfwmmacc_bf16_f8e5m2_f8e4m3_obj;
static CONSTEXPR const
  vfwmmacc_ofp8_bf16_ime_op<UNSPEC_VFWMMACC_OFP8_BF16_E5M2>
  vfwmmacc_bf16_f8e5m2_obj;

/* W=1 vfmmacc OFP8 -> OFP8.  Both E4M3 and E5M2 acc share RVVM*QI
   at the GCC level; the per-instance UNSPEC selects the
   (acc, vs1, vs2) altfmt combination.  */
template<int UNSPEC>
class vfmmacc_ofp8_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }
  /* The acc, vs1 and vs2 OFP8 formats are all on QImode; overloaded
     dispatch cannot tell the variants apart.  */
  bool can_be_overloaded_p (enum predication_type_index) const override
  { return false; }

  rtx expand (function_expander &e) const override
  {
    ime_ofp8_altfmt af = ime_unspec_ofp8_altfmt (UNSPEC);
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  af.a, af.b, af.val,
				  /* bs = */ false);
    rtx result = e.use_exact_insn (
      code_for_pred_vfmmacc_ofp8 (UNSPEC, e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const vfmmacc_ofp8_ime_op<UNSPEC_VFMMACC_F8_E4M3_E4M3_E4M3>
  vfmmacc_f8e4m3_f8e4m3_f8e4m3_obj;
static CONSTEXPR const vfmmacc_ofp8_ime_op<UNSPEC_VFMMACC_F8_E4M3_E4M3_E5M2>
  vfmmacc_f8e4m3_f8e4m3_f8e5m2_obj;
static CONSTEXPR const vfmmacc_ofp8_ime_op<UNSPEC_VFMMACC_F8_E4M3_E5M2_E4M3>
  vfmmacc_f8e4m3_f8e5m2_f8e4m3_obj;
static CONSTEXPR const vfmmacc_ofp8_ime_op<UNSPEC_VFMMACC_F8_E4M3_E5M2_E5M2>
  vfmmacc_f8e4m3_f8e5m2_f8e5m2_obj;
static CONSTEXPR const vfmmacc_ofp8_ime_op<UNSPEC_VFMMACC_F8_E5M2_E4M3_E4M3>
  vfmmacc_f8e5m2_f8e4m3_f8e4m3_obj;
static CONSTEXPR const vfmmacc_ofp8_ime_op<UNSPEC_VFMMACC_F8_E5M2_E4M3_E5M2>
  vfmmacc_f8e5m2_f8e4m3_f8e5m2_obj;
static CONSTEXPR const vfmmacc_ofp8_ime_op<UNSPEC_VFMMACC_F8_E5M2_E5M2_E4M3>
  vfmmacc_f8e5m2_f8e5m2_f8e4m3_obj;
static CONSTEXPR const vfmmacc_ofp8_ime_op<UNSPEC_VFMMACC_F8_E5M2_E5M2_E5M2>
  vfmmacc_f8e5m2_f8e5m2_f8e5m2_obj;

template<int UNSPEC>
class vfqwmmacc_ofp8_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }
  /* OFP8 E4M3 and E5M2 share RVVM1QI mode; the overloaded-dispatch
     hash cannot distinguish them, so disable overloading.  */
  bool can_be_overloaded_p (enum predication_type_index) const override
  { return false; }

  rtx expand (function_expander &e) const override
  {
    ime_ofp8_altfmt af = ime_unspec_ofp8_altfmt (UNSPEC);
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  af.a, af.b, af.val,
				  /* bs = */ false);
    rtx result = e.use_exact_insn (
      code_for_pred_vfqwmmacc_ofp8 (UNSPEC, e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const vfqwmmacc_ofp8_ime_op<UNSPEC_VFQWMMACC_F8E4M3>
  vfqwmmacc_f8e4m3_obj;
static CONSTEXPR const vfqwmmacc_ofp8_ime_op<UNSPEC_VFQWMMACC_F8E5M2>
  vfqwmmacc_f8e5m2_obj;
static CONSTEXPR const vfqwmmacc_ofp8_ime_op<UNSPEC_VFQWMMACC_F8E4M3_E5M2>
  vfqwmmacc_f8e4m3_f8e5m2_obj;
static CONSTEXPR const vfqwmmacc_ofp8_ime_op<UNSPEC_VFQWMMACC_F8E5M2_E4M3>
  vfqwmmacc_f8e5m2_f8e4m3_obj;

/* Zvvfmm FP matrix multiply-accumulate, W=8 (vf8wmmacc.vv).  */
template<int UNSPEC>
class vf8wmmacc_ofp8_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }
  bool can_be_overloaded_p (enum predication_type_index) const override
  { return false; }

  rtx expand (function_expander &e) const override
  {
    ime_ofp8_altfmt af = ime_unspec_ofp8_altfmt (UNSPEC);
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  af.a, af.b, af.val,
				  /* bs = */ false);
    rtx result = e.use_exact_insn (
      code_for_pred_vf8wmmacc_ofp8 (UNSPEC, e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const vf8wmmacc_ofp8_ime_op<UNSPEC_VF8WMMACC_F8E4M3>
  vf8wmmacc_f8e4m3_obj;
static CONSTEXPR const vf8wmmacc_ofp8_ime_op<UNSPEC_VF8WMMACC_F8E5M2>
  vf8wmmacc_f8e5m2_obj;
static CONSTEXPR const vf8wmmacc_ofp8_ime_op<UNSPEC_VF8WMMACC_F8E4M3_E5M2>
  vf8wmmacc_f8e4m3_f8e5m2_obj;
static CONSTEXPR const vf8wmmacc_ofp8_ime_op<UNSPEC_VF8WMMACC_F8E5M2_E4M3>
  vf8wmmacc_f8e5m2_f8e4m3_obj;

class vf8wmmacc_f4e2m1_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }
  bool can_be_overloaded_p (enum predication_type_index) const override
  { return false; }

  rtx expand (function_expander &e) const override
  {
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  false, false, false, false);
    rtx result
      = e.use_exact_insn (code_for_pred_vf8wmmacc_f4e2m1 (e.vector_mode ()));
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

static CONSTEXPR const vf8wmmacc_f4e2m1_ime_op vf8wmmacc_f4e2m1_obj;

/* Microscaled (section 8) function base class.

   The microscaled intrinsics have an extra vuint16m1_t scales operand
   (the second-to-last argument, before vl) that must be placed in v0.
   The RTL pattern does not have a match_operand for the scales; instead
   it has a (use (reg:RVVM1HI V0_REGNUM)) side-effect.

   This base class provides a custom expand() that delegates to the
   function_expander::use_microscaled_insn() helper, which moves the
   scales into v0 and builds the pattern operands skipping the scales
   argument.  */
/* Return true if the microscaled UNSPEC uses BS=32 (bs vtype bit = 1).
   BS=16 variants have bs=0.  */

static bool
ime_unspec_bs32_p (int unspec)
{
  switch (unspec)
    {
    case UNSPEC_VFWMMACC_F16_BS32:
    case UNSPEC_VFQWMMACC_F8E4M3_BS32:
    case UNSPEC_VF8WMMACC_F8E4M3_BS32:
    case UNSPEC_VFWIMMACC_I8_BS32:
    case UNSPEC_VFQWIMMACC_I8_BS32:
    case UNSPEC_VF8WIMMACC_I8_BS32:
      return true;
    default:
      return false;
    }
}

template<insn_code (*CODE_FOR)(int, machine_mode), int UNSPEC>
class microscaled_ime_op : public function_base
{
public:
  bool apply_mask_policy_p () const override { return false; }
  bool use_mask_predication_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }
  bool apply_ime_vtype_op_p () const override { return true; }
  bool can_be_overloaded_p (enum predication_type_index) const override
  { return false; }

  rtx expand (function_expander &e) const override
  {
    maybe_emit_ime_runtime_vtype (e, e.vector_mode (),
				  /* altfmt_a = */ false,
				  /* altfmt_b = */ false,
				  /* altfmt = */ false,
				  /* bs = */ ime_unspec_bs32_p (UNSPEC));
    /* The scales argument is the second-to-last (before vl).  */
    int scales_idx = call_expr_nargs (e.exp) - 2;
    insn_code icode = CODE_FOR (UNSPEC, e.vector_mode ());
    rtx result = e.use_microscaled_insn (icode, scales_idx);
    emit_ime_post_consumer_blockage ();
    return result;
  }
};

/* FP-input scaled variant instances.  */
static CONSTEXPR const microscaled_ime_op<code_for_pred_vfwmmacc_scaled_f16,
  UNSPEC_VFWMMACC_F16_BS32> vfwmmacc_scaled_f16_bs32_obj;
static CONSTEXPR const microscaled_ime_op<code_for_pred_vfwmmacc_scaled_f16,
  UNSPEC_VFWMMACC_F16_BS16> vfwmmacc_scaled_f16_bs16_obj;

static CONSTEXPR const microscaled_ime_op<code_for_pred_vfqwmmacc_scaled_f8e4m3,
  UNSPEC_VFQWMMACC_F8E4M3_BS32> vfqwmmacc_scaled_f8e4m3_bs32_obj;
static CONSTEXPR const microscaled_ime_op<code_for_pred_vfqwmmacc_scaled_f8e4m3,
  UNSPEC_VFQWMMACC_F8E4M3_BS16> vfqwmmacc_scaled_f8e4m3_bs16_obj;

static CONSTEXPR const microscaled_ime_op<code_for_pred_vf8wmmacc_scaled_f8e4m3,
  UNSPEC_VF8WMMACC_F8E4M3_BS32> vf8wmmacc_scaled_f8e4m3_bs32_obj;
static CONSTEXPR const microscaled_ime_op<code_for_pred_vf8wmmacc_scaled_f8e4m3,
  UNSPEC_VF8WMMACC_F8E4M3_BS16> vf8wmmacc_scaled_f8e4m3_bs16_obj;

/* Integer-input microscaled variant instances.  */
static CONSTEXPR const microscaled_ime_op<code_for_pred_vfwimmacc_i8,
  UNSPEC_VFWIMMACC_I8_BS32> vfwimmacc_i8_bs32_obj;
static CONSTEXPR const microscaled_ime_op<code_for_pred_vfwimmacc_i8,
  UNSPEC_VFWIMMACC_I8_BS16> vfwimmacc_i8_bs16_obj;

static CONSTEXPR const microscaled_ime_op<code_for_pred_vfqwimmacc_i8,
  UNSPEC_VFQWIMMACC_I8_BS32> vfqwimmacc_i8_bs32_obj;
static CONSTEXPR const microscaled_ime_op<code_for_pred_vfqwimmacc_i8,
  UNSPEC_VFQWIMMACC_I8_BS16> vfqwimmacc_i8_bs16_obj;

static CONSTEXPR const microscaled_ime_op<code_for_pred_vf8wimmacc_i8,
  UNSPEC_VF8WIMMACC_I8_BS32> vf8wimmacc_i8_bs32_obj;
static CONSTEXPR const microscaled_ime_op<code_for_pred_vf8wimmacc_i8,
  UNSPEC_VF8WIMMACC_I8_BS16> vf8wimmacc_i8_bs16_obj;

/* Implements __riscv_ime_vlen(): returns VLEN in bits.
   Compile-time constant when VLEN is known, otherwise csrr vlenb << 3.  */
class ime_vlen : public function_base
{
public:
  bool apply_vl_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    machine_mode mode = GET_MODE (e.target);
    rtx vlenb = gen_reg_rtx (mode);
    emit_move_insn (vlenb, gen_int_mode (BYTES_PER_RISCV_VECTOR, mode));
    rtx vlen_bits = expand_simple_binop (mode, ASHIFT, vlenb, GEN_INT (3),
					 e.target, 1, OPTAB_DIRECT);
    if (vlen_bits != e.target)
      emit_move_insn (e.target, vlen_bits);
    return e.target;
  }
};

static CONSTEXPR const ime_vlen ime_vlen_obj;

/* Implements __riscv_ime_lambda(): returns the IME lambda value.
   When VLEN is compile-time known and -mrvv-ime-lambda= has a single
   value, returns that constant.  Otherwise computes at runtime:
   lambda = 1 << (ctz(VLEN / 8) - log2(SEW_min * 1) ... but lambda
   depends on the instruction's SEW and MUL_C which aren't known here.

   For a geometry-query builtin, we return VLEN-derived lambda for the
   minimum configuration (SEW=8, MUL_C=1): lambda = sqrt(VLEN/8).
   User code typically uses this in loop bounds where the exact lambda
   for their SEW/MUL_C is implicit from the intrinsic they call.

   Actually, the most useful definition is: lambda is an intrinsic
   property of the hardware, not dependent on SEW/MUL_C.  The hardware
   supports a SET of lambda values.  For __riscv_ime_lambda() we return
   the maximum supported lambda: sqrt(VLEN / min_SEW) where min_SEW=8.
   This gives the user the tile dimension for the largest possible tiles.

   Simpler approach: just return sqrt(VLEN/8) = sqrt(VLEN) / sqrt(8).
   Since VLEN is a power of 2, sqrt(VLEN) = 1 << (log2(VLEN) / 2).
   VLEN in bits = vlenb * 8.  log2(vlenb*8) = log2(vlenb) + 3.
   sqrt(VLEN) = 1 << ((log2(vlenb) + 3) / 2).

   But this is complex.  Simplest: return vlenb, shift, ctz, shift.  */
class ime_lambda : public function_base
{
public:
  bool apply_vl_p () const override { return false; }

  rtx expand (function_expander &e) const override
  {
    machine_mode mode = GET_MODE (e.target);

    if (BYTES_PER_RISCV_VECTOR.is_constant ())
      {
	/* Static path: compute lambda at compile time.
	   lambda = sqrt(VLEN / 8) for the default SEW=8, MUL_C=1.
	   More precisely: VLEN_bits = vlenb * 8; lambda_sq = VLEN_bits / 8
	   = vlenb; lambda = sqrt(vlenb).  */
	HOST_WIDE_INT vlenb_val = BYTES_PER_RISCV_VECTOR.to_constant ();
	/* For the default query (SEW=8, MUL_C=1):
	   lambda_sq = VLEN_bits / (8 * 1) = VLEN_bits / 8 = vlenb.  */
	HOST_WIDE_INT lambda_sq = vlenb_val;
	int log_sq = exact_log2 (lambda_sq);
	if (log_sq >= 0 && (log_sq & 1) == 0)
	  {
	    int lambda = 1 << (log_sq >> 1);
	    emit_move_insn (e.target, gen_int_mode (lambda, mode));
	    return e.target;
	  }
	/* Fallback: vlenb is not a perfect square power of 2.
	   Just compute at runtime.  */
      }

    /* Runtime path: lambda = 1 << (ctz(vlenb) / 2)
       where vlenb = VLEN/8, and lambda_sq = vlenb (for SEW=8, MUL_C=1).  */
    rtx vlenb = gen_reg_rtx (mode);
    emit_move_insn (vlenb, gen_int_mode (BYTES_PER_RISCV_VECTOR, mode));

    /* ctz(vlenb) = log2(lambda^2)  */
    rtx log2_lsq = gen_reg_rtx (mode);
    if (TARGET_64BIT)
      emit_insn (gen_ctzdi2 (log2_lsq, vlenb));
    else
      emit_insn (gen_ctzsi2 (log2_lsq, vlenb));

    /* log2_lsq >> 1 = log2(lambda)  */
    rtx log2_lambda = expand_simple_binop (mode, LSHIFTRT, log2_lsq,
					   GEN_INT (1), NULL_RTX, 1,
					   OPTAB_DIRECT);

    /* 1 << log2(lambda) = lambda  */
    rtx lambda = expand_simple_binop (mode, ASHIFT, const1_rtx,
				      log2_lambda, e.target, 1,
				      OPTAB_DIRECT);
    if (lambda != e.target)
      emit_move_insn (e.target, lambda);
    return e.target;
  }
};

static CONSTEXPR const ime_lambda ime_lambda_obj;

/* Implements __riscv_vsetlambda(size_t requested_lambda).

   Two argument forms, distinguished at expand time:

     requested_lambda == 0
	Read-only query.  No write to vtype.  Sequence:
	  csrr  Rd, vtype
	  srli  Rd, Rd, XLEN-4
	  andi  Rd, Rd, 7
	  sll   t,  1, Rd
	  srli  Rd, t, 1
	The trailing two ops decode the 3-bit encoding back to its
	power-of-two value: enc=0 yields 0, enc=N>0 yields 1<<(N-1).

     requested_lambda is a compile-time positive power of two in
     {1, 2, 4, 8, 16, 32, 64}
	Write-half: RMW the lambda field of vtype with the 3-bit
	encoding, install via `vsetvl x0, x0, t' (preserves VL), then
	emit the read-only readback+decode sequence above and return
	the (possibly WARL-clamped) result.

     anything else as a compile-time constant
	error_at; the user wrote an out-of-range request and the
	spec contract is to error rather than vill-clamp.

     runtime value
	Not implemented in this commit -- emit a sorry().  A later
	commit will add the branch-on-zero path (requires Zbb for the
	encoding via ctz).

   Per the IME spec section "Establishing lambda from C", vtype.lambda
   is WARL; the read-only return value is the value the hardware
   actually established.  A non-zero requested_lambda may still read
   back as 0 if the current (VLEN, SEW, LMUL) supports no lambda --
   the caller inspects the return value.  */
class vsetlambda : public function_base
{
public:
  bool apply_vl_p () const override { return false; }

  /* Reads vtype unconditionally (return value) and writes vtype on a
     non-zero argument.  Reporting both prevents the middle-end from
     DCE'ing a discarded-return call and from CSE'ing two distinct
     requested-lambda calls.  */
  unsigned int call_properties (const function_instance &) const override
  {
    return CP_READ_CSR | CP_WRITE_CSR;
  }

  /* Map a positive power-of-two lambda value to its 3-bit encoding.
     The not-configured / read-only encoding (0) maps to 0.  */
  static int encode_lambda (HOST_WIDE_INT v)
  {
    switch (v)
      {
      case 0:  return 0;
      case 1:  return 1;
      case 2:  return 2;
      case 4:  return 3;
      case 8:  return 4;
      case 16: return 5;
      case 32: return 6;
      case 64: return 7;
      default: return -1;
      }
  }

  /* Emit the readback+decode sequence into TARGET (Pmode).  If TARGET
     is null the caller is discarding the return value -- skip the
     entire sequence; the only observable effect of vsetlambda is the
     vtype write, which the install pattern (when emitted) provides.  */
  static void emit_readback_decode (rtx target)
  {
    if (!target)
      return;
    machine_mode m = Pmode;
    int xlen = GET_MODE_BITSIZE (m).to_constant ();

    rtx vt = gen_reg_rtx (m);
    emit_insn (gen_read_vtype (m, vt));

    /* field = (vtype >> (xlen-4)) & 7  */
    rtx field = expand_simple_binop (m, LSHIFTRT, vt, GEN_INT (xlen - 4),
				     NULL_RTX, 1, OPTAB_DIRECT);
    field = expand_simple_binop (m, AND, field, GEN_INT (7),
				 NULL_RTX, 1, OPTAB_DIRECT);

    /* decoded = (1 << field) >> 1
       enc==0 -> 0; enc==N>0 -> 1 << (N-1).  */
    rtx one = const1_rtx;
    rtx shifted = expand_simple_binop (m, ASHIFT, one, field,
				       NULL_RTX, 1, OPTAB_DIRECT);
    rtx decoded = expand_simple_binop (m, LSHIFTRT, shifted, const1_rtx,
				       target, 1, OPTAB_DIRECT);
    if (decoded != target)
      emit_move_insn (target, decoded);
  }

  rtx expand (function_expander &e) const override
  {
    tree arg = CALL_EXPR_ARG (e.exp, 0);
    machine_mode m = Pmode;
    int xlen = GET_MODE_BITSIZE (m).to_constant ();

    if (!CONSTANT_CLASS_P (arg))
      {
	sorry ("%<__riscv_vsetlambda%> with a non-constant argument "
	       "is not yet supported");
	if (e.target)
	  emit_move_insn (e.target, const0_rtx);
	return e.target;
      }

    HOST_WIDE_INT req = tree_to_shwi (arg);
    int enc = encode_lambda (req);
    if (enc < 0)
      {
	error ("argument to %<__riscv_vsetlambda%> must be 0 or a positive "
	       "power of two in %<{1, 2, 4, 8, 16, 32, 64}%>");
	if (e.target)
	  emit_move_insn (e.target, const0_rtx);
	return e.target;
      }

    /* Write half: emit only when requested_lambda is non-zero.  The
       compile-time-0 form is a pure read.  */
    if (req != 0)
      {
	rtx vt_in = gen_reg_rtx (m);
	emit_insn (gen_read_vtype (m, vt_in));

	/* clear_mask = ~(7 << (xlen-4)).  */
	unsigned HOST_WIDE_INT clear_mask
	  = ~((unsigned HOST_WIDE_INT) 7 << (xlen - 4));
	rtx masked = expand_simple_binop (m, AND, vt_in,
					  gen_int_mode (clear_mask, m),
					  NULL_RTX, 1, OPTAB_DIRECT);

	/* set_bits = enc << (xlen-4).  */
	unsigned HOST_WIDE_INT set_bits
	  = (unsigned HOST_WIDE_INT) enc << (xlen - 4);
	rtx new_vt = expand_simple_binop (m, IOR, masked,
					  gen_int_mode (set_bits, m),
					  NULL_RTX, 1, OPTAB_DIRECT);

	/* Force into a single register; the install pattern wants a
	   plain register operand.  */
	new_vt = force_reg (m, new_vt);
	/* The install pattern carries the 3-bit lambda encoding as a
	   metadata operand so the vsetvl pass can recognise it as a
	   DEF of vtype.lambda with a statically known value (used by
	   the lattice introduced in commit 2 of this series).  */
	emit_insn (gen_riscv_vsetlambda_install (m, new_vt, GEN_INT (enc)));
      }

    /* Read-only half: decode the (possibly WARL-clamped) readback and
       return it.  This is the return value of both forms.  */
    emit_readback_decode (e.target);
    return e.target;
  }
};

static CONSTEXPR const vsetlambda vsetlambda_obj;

/* Zvvmtls order-preserving tile load (vmtl.v), MVP: LMUL=1, unmasked,
   dynamic lambda (no immediate Lλ override).  The intrinsic signature
   is (const T *base, ptrdiff_t ld, size_t vl) -> vector, where ld is
   the leading dimension in ELEMENTS.  */
class vmtl_ime : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  /* IME tile loads (vmtl, vmttl, vmtl_L<N>, vmttl_L<N>): the data
     vector is the RETURN type; the args are (T*, ptrdiff_t [, mask],
     vl).  For OFP8 / OFP4 vs u8 the args are bit-identical, so
     overloaded dispatch from arg types alone is genuinely ambiguous.
     Force the fully-qualified name.  */
  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return false;
  }

  unsigned int call_properties (const function_instance &) const override
  {
    return CP_READ_MEMORY;
  }

  rtx expand (function_expander &e) const override
  {
    rtx result
      = e.use_contiguous_load_insn (code_for_pred_vmtl (e.vector_mode ()));
    return result;
  }
};

static CONSTEXPR const vmtl_ime vmtl_obj;

/* Zvvmtls vmtl.v with immediate "Lλ" override (one of
   1/2/4/8/16/32/64).  Each Lλ value maps to its own UNSPEC so the
   RTL operand layout matches @pred_vmtl<mode> and the existing
   use_contiguous_load_insn path can be reused unchanged.  */
template <int UNSPEC>
class vmtl_L_ime : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  /* See vmtl_ime: tile loads cannot be overloaded — args carry no
     information about the data vector type, which is the return.  */
  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return false;
  }

  unsigned int call_properties (const function_instance &) const override
  {
    return CP_READ_MEMORY;
  }

  rtx expand (function_expander &e) const override
  {
    rtx result = e.use_contiguous_load_insn (
      code_for_pred_vmtl (UNSPEC, e.vector_mode ()));
    return result;
  }
};

static CONSTEXPR const vmtl_L_ime<UNSPEC_VMTL_L1>  vmtl_L1_obj;
static CONSTEXPR const vmtl_L_ime<UNSPEC_VMTL_L2>  vmtl_L2_obj;
static CONSTEXPR const vmtl_L_ime<UNSPEC_VMTL_L4>  vmtl_L4_obj;
static CONSTEXPR const vmtl_L_ime<UNSPEC_VMTL_L8>  vmtl_L8_obj;
static CONSTEXPR const vmtl_L_ime<UNSPEC_VMTL_L16> vmtl_L16_obj;
static CONSTEXPR const vmtl_L_ime<UNSPEC_VMTL_L32> vmtl_L32_obj;
static CONSTEXPR const vmtl_L_ime<UNSPEC_VMTL_L64> vmtl_L64_obj;

/* Zvvmtls vmts.v with immediate "Lλ" override.  */
template <int UNSPEC>
class vmts_L_ime : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  unsigned int call_properties (const function_instance &) const override
  {
    return CP_WRITE_MEMORY;
  }

  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return true;
  }

  rtx expand (function_expander &e) const override
  {
    rtx result = e.use_contiguous_store_insn (
      code_for_pred_vmts (UNSPEC, e.vector_mode ()));
    return result;
  }
};

static CONSTEXPR const vmts_L_ime<UNSPEC_VMTS_L1>  vmts_L1_obj;
static CONSTEXPR const vmts_L_ime<UNSPEC_VMTS_L2>  vmts_L2_obj;
static CONSTEXPR const vmts_L_ime<UNSPEC_VMTS_L4>  vmts_L4_obj;
static CONSTEXPR const vmts_L_ime<UNSPEC_VMTS_L8>  vmts_L8_obj;
static CONSTEXPR const vmts_L_ime<UNSPEC_VMTS_L16> vmts_L16_obj;
static CONSTEXPR const vmts_L_ime<UNSPEC_VMTS_L32> vmts_L32_obj;
static CONSTEXPR const vmts_L_ime<UNSPEC_VMTS_L64> vmts_L64_obj;

/* Zvvmttls vmttl.v with immediate "Lλ" override.  */
template <int UNSPEC>
class vmttl_L_ime : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  /* See vmtl_ime: tile loads cannot be overloaded — args carry no
     information about the data vector type, which is the return.  */
  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return false;
  }

  unsigned int call_properties (const function_instance &) const override
  {
    return CP_READ_MEMORY;
  }

  rtx expand (function_expander &e) const override
  {
    rtx result = e.use_contiguous_load_insn (
      code_for_pred_vmttl (UNSPEC, e.vector_mode ()));
    return result;
  }
};

static CONSTEXPR const vmttl_L_ime<UNSPEC_VMTTL_L1>  vmttl_L1_obj;
static CONSTEXPR const vmttl_L_ime<UNSPEC_VMTTL_L2>  vmttl_L2_obj;
static CONSTEXPR const vmttl_L_ime<UNSPEC_VMTTL_L4>  vmttl_L4_obj;
static CONSTEXPR const vmttl_L_ime<UNSPEC_VMTTL_L8>  vmttl_L8_obj;
static CONSTEXPR const vmttl_L_ime<UNSPEC_VMTTL_L16> vmttl_L16_obj;
static CONSTEXPR const vmttl_L_ime<UNSPEC_VMTTL_L32> vmttl_L32_obj;
static CONSTEXPR const vmttl_L_ime<UNSPEC_VMTTL_L64> vmttl_L64_obj;

/* Zvvmttls vmtts.v with immediate "Lλ" override.  */
template <int UNSPEC>
class vmtts_L_ime : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  unsigned int call_properties (const function_instance &) const override
  {
    return CP_WRITE_MEMORY;
  }

  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return true;
  }

  rtx expand (function_expander &e) const override
  {
    rtx result = e.use_contiguous_store_insn (
      code_for_pred_vmtts (UNSPEC, e.vector_mode ()));
    return result;
  }
};

static CONSTEXPR const vmtts_L_ime<UNSPEC_VMTTS_L1>  vmtts_L1_obj;
static CONSTEXPR const vmtts_L_ime<UNSPEC_VMTTS_L2>  vmtts_L2_obj;
static CONSTEXPR const vmtts_L_ime<UNSPEC_VMTTS_L4>  vmtts_L4_obj;
static CONSTEXPR const vmtts_L_ime<UNSPEC_VMTTS_L8>  vmtts_L8_obj;
static CONSTEXPR const vmtts_L_ime<UNSPEC_VMTTS_L16> vmtts_L16_obj;
static CONSTEXPR const vmtts_L_ime<UNSPEC_VMTTS_L32> vmtts_L32_obj;
static CONSTEXPR const vmtts_L_ime<UNSPEC_VMTTS_L64> vmtts_L64_obj;

/* Zvvmtls order-preserving tile store (vmts.v), MVP: LMUL=1,
   unmasked, dynamic lambda.  Intrinsic signature is
   (T *base, ptrdiff_t ld, vector_t data, size_t vl) -> void.  */
class vmts_ime : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  unsigned int call_properties (const function_instance &) const override
  {
    return CP_WRITE_MEMORY;
  }

  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return true;
  }

  rtx expand (function_expander &e) const override
  {
    rtx result
      = e.use_contiguous_store_insn (code_for_pred_vmts (e.vector_mode ()));
    return result;
  }
};

static CONSTEXPR const vmts_ime vmts_obj;

/* Zvvmttls transposing tile load (vmttl.v), MVP.  Same shape as
   vmtl_ime; dispatches to @pred_vmttl<mode>.  */
class vmttl_ime : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  /* See vmtl_ime: tile loads cannot be overloaded — args carry no
     information about the data vector type, which is the return.  */
  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return false;
  }

  unsigned int call_properties (const function_instance &) const override
  {
    return CP_READ_MEMORY;
  }

  rtx expand (function_expander &e) const override
  {
    rtx result
      = e.use_contiguous_load_insn (code_for_pred_vmttl (e.vector_mode ()));
    return result;
  }
};

static CONSTEXPR const vmttl_ime vmttl_obj;

/* Zvvmttls transposing tile store (vmtts.v), MVP.  Same shape as
   vmts_ime; dispatches to @pred_vmtts<mode>.  */
class vmtts_ime : public function_base
{
public:
  bool apply_tail_policy_p () const override { return false; }
  bool apply_mask_policy_p () const override { return false; }
  bool has_merge_operand_p () const override { return false; }

  unsigned int call_properties (const function_instance &) const override
  {
    return CP_WRITE_MEMORY;
  }

  bool can_be_overloaded_p (enum predication_type_index) const override
  {
    return true;
  }

  rtx expand (function_expander &e) const override
  {
    rtx result
      = e.use_contiguous_store_insn (code_for_pred_vmtts (e.vector_mode ()));
    return result;
  }
};

static CONSTEXPR const vmtts_ime vmtts_obj;

/* Declare the function base NAME, pointing it to an instance
   of class <NAME>_obj.  */
#define BASE(NAME) \
  namespace bases { const function_base *const NAME = &NAME##_obj; }

BASE (vsetvl)
BASE (vsetvlmax)
BASE (vle)
BASE (vse)
BASE (vlm)
BASE (vsm)
BASE (vlse)
BASE (vsse)
BASE (vluxei8)
BASE (vluxei16)
BASE (vluxei32)
BASE (vluxei64)
BASE (vloxei8)
BASE (vloxei16)
BASE (vloxei32)
BASE (vloxei64)
BASE (vsuxei8)
BASE (vsuxei16)
BASE (vsuxei32)
BASE (vsuxei64)
BASE (vsoxei8)
BASE (vsoxei16)
BASE (vsoxei32)
BASE (vsoxei64)
BASE (vadd)
BASE (vsub)
BASE (vrsub)
BASE (vand)
BASE (vor)
BASE (vxor)
BASE (vsll)
BASE (vsra)
BASE (vsrl)
BASE (vmin)
BASE (vmax)
BASE (vminu)
BASE (vmaxu)
BASE (vmul)
BASE (vmulh)
BASE (vmulhu)
BASE (vmulhsu)
BASE (vdiv)
BASE (vrem)
BASE (vdivu)
BASE (vremu)
BASE (vneg)
BASE (vnot)
BASE (vsext)
BASE (vzext)
BASE (vwadd)
BASE (vwsub)
BASE (vwmul)
BASE (vwaddu)
BASE (vwsubu)
BASE (vwmulu)
BASE (vwmulsu)
BASE (vwcvt_x)
BASE (vwcvtu_x)
BASE (vadc)
BASE (vsbc)
BASE (vmadc)
BASE (vmsbc)
BASE (vnsrl)
BASE (vnsra)
BASE (vncvt_x)
BASE (vmerge)
BASE (vmv_v)
BASE (vmseq)
BASE (vmsne)
BASE (vmslt)
BASE (vmsgt)
BASE (vmsle)
BASE (vmsge)
BASE (vmsltu)
BASE (vmsgtu)
BASE (vmsleu)
BASE (vmsgeu)
BASE (vmacc)
BASE (vnmsac)
BASE (vmadd)
BASE (vnmsub)
BASE (vwmacc)
BASE (vwmaccu)
BASE (vwmaccsu)
BASE (vwmaccus)
BASE (vsadd)
BASE (vssub)
BASE (vsaddu)
BASE (vssubu)
BASE (vaadd)
BASE (vasub)
BASE (vaaddu)
BASE (vasubu)
BASE (vsmul)
BASE (vssra)
BASE (vssrl)
BASE (vnclip)
BASE (vnclipu)
BASE (vmand)
BASE (vmnand)
BASE (vmandn)
BASE (vmxor)
BASE (vmor)
BASE (vmnor)
BASE (vmorn)
BASE (vmxnor)
BASE (vmmv)
BASE (vmclr)
BASE (vmset)
BASE (vmnot)
BASE (vcpop)
BASE (vfirst)
BASE (vmsbf)
BASE (vmsif)
BASE (vmsof)
BASE (viota)
BASE (vid)
BASE (vfadd)
BASE (vfadd_frm)
BASE (vfsub)
BASE (vfsub_frm)
BASE (vfrsub)
BASE (vfrsub_frm)
BASE (vfwadd)
BASE (vfwadd_frm)
BASE (vfwsub)
BASE (vfwsub_frm)
BASE (vfmul)
BASE (vfmul_frm)
BASE (vfdiv)
BASE (vfdiv_frm)
BASE (vfrdiv)
BASE (vfrdiv_frm)
BASE (vfwmul)
BASE (vfwmul_frm)
BASE (vfmacc)
BASE (vfmacc_frm)
BASE (vfnmsac)
BASE (vfnmsac_frm)
BASE (vfmadd)
BASE (vfmadd_frm)
BASE (vfnmsub)
BASE (vfnmsub_frm)
BASE (vfnmacc)
BASE (vfnmacc_frm)
BASE (vfmsac)
BASE (vfmsac_frm)
BASE (vfnmadd)
BASE (vfnmadd_frm)
BASE (vfmsub)
BASE (vfmsub_frm)
BASE (vfwmacc)
BASE (vfwmacc_frm)
BASE (vfwnmacc)
BASE (vfwnmacc_frm)
BASE (vfwmsac)
BASE (vfwmsac_frm)
BASE (vfwnmsac)
BASE (vfwnmsac_frm)
BASE (vfsqrt)
BASE (vfsqrt_frm)
BASE (vfrsqrt7)
BASE (vfrec7)
BASE (vfrec7_frm)
BASE (vfmin)
BASE (vfmax)
BASE (vfsgnj)
BASE (vfsgnjn)
BASE (vfsgnjx)
BASE (vfneg)
BASE (vfabs)
BASE (vmfeq)
BASE (vmfne)
BASE (vmflt)
BASE (vmfgt)
BASE (vmfle)
BASE (vmfge)
BASE (vfclass)
BASE (vfmerge)
BASE (vfmv_v)
BASE (vfcvt_x)
BASE (vfcvt_x_frm)
BASE (vfcvt_xu)
BASE (vfcvt_xu_frm)
BASE (vfcvt_rtz_x)
BASE (vfcvt_rtz_xu)
BASE (vfcvt_f)
BASE (vfcvt_f_frm)
BASE (vfwcvt_x)
BASE (vfwcvt_x_frm)
BASE (vfwcvt_xu)
BASE (vfwcvt_xu_frm)
BASE (vfwcvt_rtz_x)
BASE (vfwcvt_rtz_xu)
BASE (vfwcvt_f)
BASE (vfncvt_x)
BASE (vfncvt_x_frm)
BASE (vfncvt_xu)
BASE (vfncvt_xu_frm)
BASE (vfncvt_rtz_x)
BASE (vfncvt_rtz_xu)
BASE (vfncvt_f)
BASE (vfncvt_f_frm)
BASE (vfncvt_rod_f)
BASE (vredsum)
BASE (vredmaxu)
BASE (vredmax)
BASE (vredminu)
BASE (vredmin)
BASE (vredand)
BASE (vredor)
BASE (vredxor)
BASE (vwredsum)
BASE (vwredsumu)
BASE (vfredusum)
BASE (vfredusum_frm)
BASE (vfredosum)
BASE (vfredosum_frm)
BASE (vfredmax)
BASE (vfredmin)
BASE (vfwredosum)
BASE (vfwredosum_frm)
BASE (vfwredusum)
BASE (vfwredusum_frm)
BASE (vmv_x)
BASE (vmv_s)
BASE (vfmv_f)
BASE (vfmv_s)
BASE (vslideup)
BASE (vslidedown)
BASE (vslide1up)
BASE (vslide1down)
BASE (vfslide1up)
BASE (vfslide1down)
BASE (vrgather)
BASE (vrgatherei16)
BASE (vcompress)
BASE (vundefined)
BASE (vreinterpret)
BASE (vlmul_ext)
BASE (vlmul_trunc)
BASE (vset)
BASE (vget)
BASE (vcreate)
BASE (read_vl)
BASE (vleff)
BASE (vlenb)
BASE (vlseg)
BASE (vsseg)
BASE (vlsseg)
BASE (vssseg)
BASE (vluxseg)
BASE (vloxseg)
BASE (vsuxseg)
BASE (vsoxseg)
BASE (vlsegff)
BASE (vlb)
BASE (vlh)
BASE (vlw)
BASE (vlbu)
BASE (vlhu)
BASE (vlwu)
BASE (vsb)
BASE (vsh)
BASE (vsw)
BASE (vlsb)
BASE (vlsh)
BASE (vlsw)
BASE (vlsbu)
BASE (vlshu)
BASE (vlswu)
BASE (vssb)
BASE (vssh)
BASE (vssw)
BASE (vlxb)
BASE (vlxh)
BASE (vlxw)
BASE (vlxbu)
BASE (vlxhu)
BASE (vlxwu)
BASE (vsxb)
BASE (vsxh)
BASE (vsxw)
BASE (vsuxb)
BASE (vsuxh)
BASE (vsuxw)
BASE (vext_x_v)
/* Crypto vector */
BASE (vandn)
BASE (vbrev)
BASE (vbrev8)
BASE (vrev8)
BASE (vclz)
BASE (vctz)
BASE (vrol)
BASE (vror)
BASE (vwsll)
BASE (vclmul)
BASE (vclmulh)
BASE (vghsh)
BASE (vgmul)
BASE (vaesef)
BASE (vaesem)
BASE (vaesdf)
BASE (vaesdm)
BASE (vaesz)
BASE (vaeskf1)
BASE (vaeskf2)
BASE (vsha2ms)
BASE (vsha2ch)
BASE (vsha2cl)
BASE (vsm4k)
BASE (vsm4r)
BASE (vsm3me)
BASE (vsm3c)
/* Zvfbfmin */
BASE (vfncvtbf16_f)
BASE (vfncvtbf16_f_frm)
BASE (vfwcvtbf16_f)
/* Zvfbfwma */
BASE (vfwmaccbf16)
BASE (vfwmaccbf16_frm)
/* Zvvmm */
BASE (vmmacc_s)
BASE (vmmacc_u)
BASE (vmmacc_s_lm2)
BASE (vmmacc_s_lm4)
BASE (vmmacc_s_lm8)
BASE (vmmacc_u_lm2)
BASE (vmmacc_u_lm4)
BASE (vmmacc_u_lm8)
BASE (vmmacc_su)
BASE (vmmacc_us)
BASE (vmmacc_su_lm2)
BASE (vmmacc_su_lm4)
BASE (vmmacc_su_lm8)
BASE (vmmacc_us_lm2)
BASE (vmmacc_us_lm4)
BASE (vmmacc_us_lm8)
BASE (vwmmacc_s)
BASE (vwmmacc_u)
BASE (vwmmacc_su)
BASE (vwmmacc_us)
BASE (vwmmacc_s_lm2)
BASE (vwmmacc_s_lm4)
BASE (vwmmacc_s_lm8)
BASE (vwmmacc_u_lm2)
BASE (vwmmacc_u_lm4)
BASE (vwmmacc_u_lm8)
BASE (vwmmacc_su_lm2)
BASE (vwmmacc_su_lm4)
BASE (vwmmacc_su_lm8)
BASE (vwmmacc_us_lm2)
BASE (vwmmacc_us_lm4)
BASE (vwmmacc_us_lm8)
BASE (vqwmmacc_s)
BASE (vqwmmacc_u)
BASE (vqwmmacc_su)
BASE (vqwmmacc_us)
BASE (vqwmmacc_s_lm2)
BASE (vqwmmacc_s_lm4)
BASE (vqwmmacc_s_lm8)
BASE (vqwmmacc_u_lm2)
BASE (vqwmmacc_u_lm4)
BASE (vqwmmacc_u_lm8)
BASE (vqwmmacc_su_lm2)
BASE (vqwmmacc_su_lm4)
BASE (vqwmmacc_su_lm8)
BASE (vqwmmacc_us_lm2)
BASE (vqwmmacc_us_lm4)
BASE (vqwmmacc_us_lm8)
BASE (v8wmmacc_s)
BASE (v8wmmacc_u)
BASE (v8wmmacc_su)
BASE (v8wmmacc_us)
BASE (v8wmmacc_s_lm2)
BASE (v8wmmacc_s_lm4)
BASE (v8wmmacc_s_lm8)
BASE (v8wmmacc_u_lm2)
BASE (v8wmmacc_u_lm4)
BASE (v8wmmacc_u_lm8)
BASE (v8wmmacc_su_lm2)
BASE (v8wmmacc_su_lm4)
BASE (v8wmmacc_su_lm8)
BASE (v8wmmacc_us_lm2)
BASE (v8wmmacc_us_lm4)
BASE (v8wmmacc_us_lm8)
/* Zvvfmm */
BASE (vfmmacc)
BASE (vfmmacc_lm2)
BASE (vfmmacc_lm4)
BASE (vfmmacc_lm8)
BASE (vfwmmacc_f16)
BASE (vfwmmacc_bf16)
BASE (vfwmmacc_f32)
BASE (vfqwmmacc_f16)
BASE (vfqwmmacc_bf16)
BASE (vfwmmacc_fp16_f8e4m3)
BASE (vfwmmacc_fp16_f8e4m3_f8e5m2)
BASE (vfwmmacc_fp16_f8e5m2_f8e4m3)
BASE (vfwmmacc_fp16_f8e5m2)
BASE (vfwmmacc_bf16_f8e4m3)
BASE (vfwmmacc_bf16_f8e4m3_f8e5m2)
BASE (vfwmmacc_bf16_f8e5m2_f8e4m3)
BASE (vfwmmacc_bf16_f8e5m2)
BASE (vfmmacc_f8e4m3_f8e4m3_f8e4m3)
BASE (vfmmacc_f8e4m3_f8e4m3_f8e5m2)
BASE (vfmmacc_f8e4m3_f8e5m2_f8e4m3)
BASE (vfmmacc_f8e4m3_f8e5m2_f8e5m2)
BASE (vfmmacc_f8e5m2_f8e4m3_f8e4m3)
BASE (vfmmacc_f8e5m2_f8e4m3_f8e5m2)
BASE (vfmmacc_f8e5m2_f8e5m2_f8e4m3)
BASE (vfmmacc_f8e5m2_f8e5m2_f8e5m2)
BASE (vfqwmmacc_f8e4m3)
BASE (vfqwmmacc_f8e5m2)
BASE (vfqwmmacc_f8e4m3_f8e5m2)
BASE (vfqwmmacc_f8e5m2_f8e4m3)
BASE (vf8wmmacc_f8e4m3)
BASE (vf8wmmacc_f8e5m2)
BASE (vf8wmmacc_f8e4m3_f8e5m2)
BASE (vf8wmmacc_f8e5m2_f8e4m3)
BASE (vf8wmmacc_f4e2m1)
/* Microscaled (section 8) FP-input scaled variants */
BASE (vfwmmacc_scaled_f16_bs32)
BASE (vfwmmacc_scaled_f16_bs16)
BASE (vfqwmmacc_scaled_f8e4m3_bs32)
BASE (vfqwmmacc_scaled_f8e4m3_bs16)
BASE (vf8wmmacc_scaled_f8e4m3_bs32)
BASE (vf8wmmacc_scaled_f8e4m3_bs16)
/* Microscaled (section 8) integer-input variants */
BASE (vfwimmacc_i8_bs32)
BASE (vfwimmacc_i8_bs16)
BASE (vfqwimmacc_i8_bs32)
BASE (vfqwimmacc_i8_bs16)
BASE (vf8wimmacc_i8_bs32)
BASE (vf8wimmacc_i8_bs16)
BASE (ime_vlen)
BASE (ime_lambda)
BASE (vsetlambda)
BASE (vmtl)
BASE (vmts)
BASE (vmttl)
BASE (vmtts)
BASE (vmtl_L1)
BASE (vmtl_L2)
BASE (vmtl_L4)
BASE (vmtl_L8)
BASE (vmtl_L16)
BASE (vmtl_L32)
BASE (vmtl_L64)
BASE (vmts_L1)
BASE (vmts_L2)
BASE (vmts_L4)
BASE (vmts_L8)
BASE (vmts_L16)
BASE (vmts_L32)
BASE (vmts_L64)
BASE (vmttl_L1)
BASE (vmttl_L2)
BASE (vmttl_L4)
BASE (vmttl_L8)
BASE (vmttl_L16)
BASE (vmttl_L32)
BASE (vmttl_L64)
BASE (vmtts_L1)
BASE (vmtts_L2)
BASE (vmtts_L4)
BASE (vmtts_L8)
BASE (vmtts_L16)
BASE (vmtts_L32)
BASE (vmtts_L64)
} // end namespace riscv_vector
