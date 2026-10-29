/* function_shape implementation for RISC-V 'V' Extension for GNU compiler.
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
#include "insn-codes.h"
#include "optabs.h"
#include "riscv-vector-builtins.h"
#include "riscv-vector-builtins-shapes.h"

namespace riscv_vector {

/* Check whether the RETURN_TYPE and ARGUMENT_TYPES are
   valid for the function.  */

static bool
check_type (tree return_type, vec<tree> &argument_types)
{
  tree arg;
  unsigned i;

  if (!return_type)
    return false;

  FOR_EACH_VEC_ELT (argument_types, i, arg)
    if (!arg)
      return false;

  return true;
}

/* Add one function instance for GROUP, using operand suffix at index OI,
   mode suffix at index PAIR && bi and predication suffix at index pred_idx.
   OPS, if nonnull, replaces the operands of GROUP; VARIANT is the
   function_instance::variant.  */
static void
build_one (function_builder &b, const function_group_info &group,
	   unsigned int pred_idx, unsigned int vec_type_idx,
	   const rvv_op_info *ops = nullptr, unsigned variant = 0)
{
  if (!ops)
    ops = &group.ops_infos;
  /* Byte forms of non-tuple vlxusegei take 21 arguments.  */
  auto_vec<tree, 21> argument_types;
  function_instance function_instance (group.base_name, *group.base,
				       *group.shape,
				       ops->types[vec_type_idx],
				       group.preds[pred_idx], ops, variant);
  tree return_type = ops->ret.get_tree_type (ops->types[vec_type_idx].index);
  b.allocate_argument_types (function_instance, argument_types);
  b.apply_predication (function_instance, return_type, argument_types);

  /* Skip the function when any of its types are unresolved.  This happens
     for IME `m16' accumulator types when the active `-march' has no
     Zvvm extension with their accumulator format: `register_vector_type'
     leaves their builtin entry NULL_TREE, and `build_function_type_array'
     would segfault on the null arg/return tree before
     `check_required_extensions' could gate the function out.  Used to
     be `TARGET_XTHEADVECTOR'-only.  */
  if (!check_type (return_type, argument_types))
    return;

  b.add_overloaded_function (function_instance, *group.shape,
			     group.required_extensions);
  b.add_unique_function (function_instance, (*group.shape), return_type,
			 argument_types, group.required_extensions);
}

/* Determine whether the intrinsic supports the currently
   processed vector type */
static bool
supports_vectype_p (const function_group_info &group, unsigned int vec_type_idx)
{
  int index = group.ops_infos.types[vec_type_idx].index;
  if (index < VECTOR_TYPE_vbfloat16mf4_t || index > VECTOR_TYPE_vbfloat16m8_t)
    return true;
  /* Only judge for bf16 vector type  */
  if (*group.shape == shapes::loadstore
      || *group.shape == shapes::indexed_loadstore
      || *group.shape == shapes::vundefined
      || *group.shape == shapes::misc
      || *group.shape == shapes::vset
      || *group.shape == shapes::vget
      || *group.shape == shapes::vcreate
      || *group.shape == shapes::fault_load
      || *group.shape == shapes::seg_loadstore
      || *group.shape == shapes::seg_indexed_loadstore
      || *group.shape == shapes::seg_fault_load
      || *group.shape == shapes::ime_mmacc
      || *group.shape == shapes::ime_tile)
    return true;
  return false;
}

/* Add a function instance for every operand && predicate && args
   combination in GROUP.  Take the function base name from GROUP && operand
   suffix from operand_suffixes && mode suffix from type_suffixes && predication
   suffix from predication_suffixes. Use apply_predication to add in
   the predicate.  */
static void
build_all (function_builder &b, const function_group_info &group)
{
  for (unsigned int pred_idx = 0; group.preds[pred_idx] != NUM_PRED_TYPES;
       ++pred_idx)
    for (unsigned int vec_type_idx = 0;
	 group.ops_infos.types[vec_type_idx].index != NUM_VECTOR_TYPES;
	 ++vec_type_idx)
      {
	if (supports_vectype_p (group, vec_type_idx))
	  build_one (b, group, pred_idx, vec_type_idx);
      }
}

/* Declare the function shape NAME, pointing it to an instance
   of class <NAME>_def.  */
#define SHAPE(DEF, VAR) \
  static CONSTEXPR const DEF##_def VAR##_obj; \
  namespace shapes { const function_shape *const VAR = &VAR##_obj; }

#define BASE_NAME_MAX_LEN 20

/* Base class for build.  */
struct build_base : public function_shape
{
  void build (function_builder &b,
	      const function_group_info &group) const override
  {
    build_all (b, group);
  }
};

/* vsetvl_def class.  */
struct vsetvl_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    /* vsetvl* instruction doesn't have C++ overloaded functions.  */
    if (overloaded_p)
      return nullptr;
    b.append_base_name (instance.base_name);
    b.append_name (type_suffixes[instance.type.index].vsetvl);
    return b.finish_name ();
  }
};

/* loadstore_def class.  */
struct loadstore_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;

    b.append_base_name (instance.base_name);

    tree type = builtin_types[instance.type.index].vector;
    machine_mode mode = TYPE_MODE (type);
    int sew = GET_MODE_BITSIZE (GET_MODE_INNER (mode));
    /* vop --> vop<sew>.  */
    if (GET_MODE_CLASS (mode) != MODE_VECTOR_BOOL)
      b.append_sew (sew);

    /* vop<sew>_v --> vop<sew>_v_<type>.  */
    if (!overloaded_p)
      {
	/* vop<sew> --> vop<sew>_v.  */
	b.append_name (operand_suffixes[instance.op_info->op]);
	/* vop<sew>_v --> vop<sew>_v_<type>.  */
	b.append_name (type_suffixes[instance.type.index].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* indexed_loadstore_def class.  */
struct indexed_loadstore_def : public function_shape
{
  void build (function_builder &b,
	      const function_group_info &group) const override
  {
    for (unsigned int pred_idx = 0; group.preds[pred_idx] != NUM_PRED_TYPES;
	 ++pred_idx)
      {
	for (unsigned int vec_type_idx = 0;
	     group.ops_infos.types[vec_type_idx].index != NUM_VECTOR_TYPES;
	     ++vec_type_idx)
	  {
	    tree index_type = group.ops_infos.args[1].get_tree_type (
	      group.ops_infos.types[vec_type_idx].index);
	    if (!index_type)
	      continue;
	    build_one (b, group, pred_idx, vec_type_idx);
	  }
      }
  }

  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;

    b.append_base_name (instance.base_name);
    /* vop<sew>_v --> vop<sew>_v_<type>.  */
    if (!overloaded_p)
      {
	/* vop<sew> --> vop<sew>_v.  */
	b.append_name (operand_suffixes[instance.op_info->op]);
	/* vop<sew>_v --> vop<sew>_v_<type>.  */
	b.append_name (type_suffixes[instance.type.index].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* Add one function instance for GROUP, using operand suffix at index OI,
   mode suffix at index PAIR && bi and predication suffix at index pred_idx.  */
static void
build_th_loadstore (function_builder &b, const function_group_info &group,
		    unsigned int pred_idx, unsigned int vec_type_idx)
{
  auto_vec<tree, 5> argument_types;
  function_instance function_instance (group.base_name, *group.base,
				       *group.shape,
				       group.ops_infos.types[vec_type_idx],
				       group.preds[pred_idx], &group.ops_infos);
  tree return_type = group.ops_infos.ret.get_tree_type (
    group.ops_infos.types[vec_type_idx].index);
  b.allocate_argument_types (function_instance, argument_types);
  b.apply_predication (function_instance, return_type, argument_types);

  /* Skip the function when any of its types are unresolved.  This happens
     for IME `m16' accumulator types when the active `-march' has no
     Zvvm extension with their accumulator format: `register_vector_type'
     leaves their builtin entry NULL_TREE, and `build_function_type_array'
     would segfault on the null arg/return tree before
     `check_required_extensions' could gate the function out.  Used to
     be `TARGET_XTHEADVECTOR'-only.  */
  if (!check_type (return_type, argument_types))
    return;

  tree type = builtin_types[group.ops_infos.types[vec_type_idx].index].vector;
  if (strstr (group.base_name, "l")
      && strstr (group.base_name, "u")
      && !TYPE_UNSIGNED (TREE_TYPE (type)))
    return;

  if (strstr (group.base_name, "l")
      && !strstr (group.base_name, "u")
      && TYPE_UNSIGNED (TREE_TYPE (type)))
    return;

  machine_mode mode = TYPE_MODE (type);
  int sew = GET_MODE_BITSIZE (GET_MODE_INNER (mode));
  if (strstr (group.base_name, "h") && sew == 8)
    return;

  if (strstr (group.base_name, "w") && (sew == 8 || sew ==16))
    return;

  b.add_overloaded_function (function_instance, *group.shape,
			     group.required_extensions);
  b.add_unique_function (function_instance, (*group.shape), return_type,
			 argument_types, group.required_extensions);
}

/* th_loadstore_width_def class.  */
struct th_loadstore_width_def : public build_base
{
   void build (function_builder &b,
	      const function_group_info &group) const override
  {
    for (unsigned int pred_idx = 0; group.preds[pred_idx] != NUM_PRED_TYPES;
       ++pred_idx)
      {
	for (unsigned int vec_type_idx = 0;
	     group.ops_infos.types[vec_type_idx].index != NUM_VECTOR_TYPES;
	     ++vec_type_idx)
	  {
	    build_th_loadstore (b, group, pred_idx, vec_type_idx);
	  }
      }
  }

  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;

    b.append_name ("__riscv_th_");
    b.append_name (instance.base_name);

    /* vop_v --> vop_v_<type>.  */
    if (!overloaded_p)
      {
	/* vop --> vop_v.  */
	b.append_name (operand_suffixes[instance.op_info->op]);
	/* vop_v --> vop_v_<type>.  */
	b.append_name (type_suffixes[instance.type.index].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};


/* th_indexed_loadstore_width_def class.  */
struct th_indexed_loadstore_width_def : public function_shape
{
  void build (function_builder &b,
	      const function_group_info &group) const override
  {
    for (unsigned int pred_idx = 0; group.preds[pred_idx] != NUM_PRED_TYPES;
	 ++pred_idx)
      {
	for (unsigned int vec_type_idx = 0;
	     group.ops_infos.types[vec_type_idx].index != NUM_VECTOR_TYPES;
	     ++vec_type_idx)
	  {
	   tree index_type = group.ops_infos.args[1].get_tree_type (
	      group.ops_infos.types[vec_type_idx].index);
	   if (!index_type)
	      continue;
	   build_th_loadstore (b, group, pred_idx, vec_type_idx);
	  }
      }
  }

  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {

    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;

    b.append_name ("__riscv_th_");
    b.append_name (instance.base_name);
    /* vop_v --> vop_v_<type>.  */
    if (!overloaded_p)
      {
	/* vop --> vop_v.  */
	b.append_name (operand_suffixes[instance.op_info->op]);
	/* vop_v --> vop_v_<type>.  */
	b.append_name (type_suffixes[instance.type.index].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* alu_def class.  */
struct alu_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;

    b.append_base_name (instance.base_name);

    /* vop<sew>_<op> --> vop<sew>_<op>_<type>.  */
    if (!overloaded_p)
      {
	b.append_name (operand_suffixes[instance.op_info->op]);
	b.append_name (type_suffixes[instance.type.index].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }

  bool check (function_checker &c) const override
  {
    /* Check whether rounding mode argument is a valid immediate.  */
    if (c.base->has_rounding_mode_operand_p ())
      {
	/* Some invalid overload intrinsic like below will have zero for
	   c.arg_num ().  Thus, make sure arg_num is big enough here.
	   __riscv_vaadd () will make c.arg_num () == 0.  */
	if (!c.any_type_float_p () && c.arg_num () >= 2)
	  return c.require_immediate (c.arg_num () - 2, VXRM_RNU, VXRM_ROD);
	/* TODO: We will support floating-point intrinsic modeling
	   rounding mode in the future.  */
      }
    return true;
  }
};

/* The base class for frm build.  */
struct build_frm_base : public build_base
{
  /* Normalize vf<op>_frm to vf<op>.  */
  static void normalize_base_name (char *to, const char *from, int limit)
  {
    strncpy (to, from, limit - 1);
    char *suffix = strstr (to, "_frm");

    if (suffix)
      *suffix = '\0';

    to[limit - 1] = '\0';
  }

  bool check (function_checker &c) const override
  {
    gcc_assert (c.any_type_float_p ());

    /* Check whether rounding mode argument is a valid immediate.
       Some invalid overload intrinsic like below will have zero for
       c.arg_num ().  Thus, make sure arg_num is big enough here.
       __riscv_vaadd () will make c.arg_num () == 0.  */
    if (c.base->has_rounding_mode_operand_p () && c.arg_num () >= 2)
      {
	unsigned int frm_num = c.arg_num () - 2;

	return c.require_immediate (frm_num, FRM_STATIC_MIN, FRM_STATIC_MAX);
      }

    return true;
  }
};

/* alu_frm_def class.  */
struct alu_frm_def : public build_frm_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    char base_name[BASE_NAME_MAX_LEN] = {};

    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;

    normalize_base_name (base_name, instance.base_name, sizeof (base_name));

    b.append_base_name (base_name);

    /* vop<sew>_<op> --> vop<sew>_<op>_<type>.  */
    if (!overloaded_p)
      {
	b.append_name (operand_suffixes[instance.op_info->op]);
	b.append_name (type_suffixes[instance.type.index].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_rm" suffix
       for vop_rm C++ overloaded API.  */
    if (!overloaded_p)
      b.append_name ("_rm");

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();

    b.append_name (predication_suffixes[instance.pred]);

    return b.finish_name ();
  }
};

/* widen_alu_frm_def class.  */
struct widen_alu_frm_def : public build_frm_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    char base_name[BASE_NAME_MAX_LEN] = {};

    normalize_base_name (base_name, instance.base_name, sizeof (base_name));

    b.append_base_name (base_name);

    /* vop<sew> --> vop<sew>_<op>.  */
    b.append_name (operand_suffixes[instance.op_info->op]);

    /* vop<sew>_<op> --> vop<sew>_<op>_<type>.  */
    if (!overloaded_p)
      b.append_name (type_suffixes[instance.type.index].vector);

    /* According to rvv-intrinsic-doc, it does not add "_rm" suffix
       for vop_rm C++ overloaded API.  */
    if (!overloaded_p)
      b.append_name ("_rm");

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();

    b.append_name (predication_suffixes[instance.pred]);

    return b.finish_name ();
  }
};

/* narrow_alu_frm_def class.  */
struct narrow_alu_frm_def : public build_frm_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    char base_name[BASE_NAME_MAX_LEN] = {};

    normalize_base_name (base_name, instance.base_name, sizeof (base_name));

    b.append_base_name (base_name);

    if (!overloaded_p)
      {
	/* vop --> vop_<op>.  */
	b.append_name (operand_suffixes[instance.op_info->op]);
	/* vop_<op> --> vop_<op>_<type>.  */
	vector_type_index ret_type_idx
	  = instance.op_info->ret.get_function_type_index (instance.type.index);
	b.append_name (type_suffixes[ret_type_idx].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_rm" suffix
       for vop_rm C++ overloaded API.  */
    if (!overloaded_p)
      b.append_name ("_rm");

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();

    b.append_name (predication_suffixes[instance.pred]);

    return b.finish_name ();
  }
};

/* reduc_alu_frm_def class.  */
struct reduc_alu_frm_def : public build_frm_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    char base_name[BASE_NAME_MAX_LEN] = {};

    normalize_base_name (base_name, instance.base_name, sizeof (base_name));

    b.append_base_name (base_name);

    /* vop_<op> --> vop<sew>_<op>_<type>.  */
    if (!overloaded_p)
      {
	b.append_name (operand_suffixes[instance.op_info->op]);
	b.append_name (type_suffixes[instance.type.index].vector);
	vector_type_index ret_type_idx
	  = instance.op_info->ret.get_function_type_index (instance.type.index);
	b.append_name (type_suffixes[ret_type_idx].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_rm" suffix
       for vop_rm C++ overloaded API.  */
    if (!overloaded_p)
      b.append_name ("_rm");

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();

    b.append_name (predication_suffixes[instance.pred]);

    return b.finish_name ();
  }
};

/* widen_alu_def class. Handle vwadd/vwsub. Unlike
   vadd.vx/vadd.vv/vwmul.vv/vwmul.vx, vwadd.vv/vwadd.vx/vwadd.wv/vwadd.wx has
   'OP' suffix in overloaded API.  */
struct widen_alu_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    b.append_base_name (instance.base_name);

    /* vop<sew> --> vop<sew>_<op>.  */
    b.append_name (operand_suffixes[instance.op_info->op]);

    /* vop<sew>_<op> --> vop<sew>_<op>_<type>.  */
    if (!overloaded_p)
      b.append_name (type_suffixes[instance.type.index].vector);

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* no_mask_policy_def class. Such instructions belong to this class
   doesn't need mask policy.  */
struct no_mask_policy_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    b.append_base_name (instance.base_name);

    if (!overloaded_p)
      b.append_name (operand_suffixes[instance.op_info->op]);

    /* vop<sew>_<op> --> vop<sew>_<op>_<type>.  */
    if (!overloaded_p)
      b.append_name (type_suffixes[instance.type.index].vector);

    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* return_mask_def class. Such instructions belong to this class
   is returning mask value.  */
struct return_mask_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    b.append_base_name (instance.base_name);

    if (!overloaded_p)
      b.append_name (operand_suffixes[instance.op_info->op]);

    /* vop<sew>_<op> --> vop<sew>_<op>_<type1>_<type2>.  */
    if (!overloaded_p)
      {
	b.append_name (type_suffixes[instance.type.index].vector);
	vector_type_index ret_type_idx
	  = instance.op_info->ret.get_function_type_index (instance.type.index);
	b.append_name (type_suffixes[ret_type_idx].vector);
      }

    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* narrow_alu_def class. Handle narrowing instructions like vnsrl.wv.  */
struct narrow_alu_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    b.append_base_name (instance.base_name);

    if (!overloaded_p)
      {
	/* vop --> vop_<op>.  */
	b.append_name (operand_suffixes[instance.op_info->op]);
	/* vop_<op> --> vop_<op>_<type>.  */
	vector_type_index ret_type_idx
	  = instance.op_info->ret.get_function_type_index (instance.type.index);
	b.append_name (type_suffixes[ret_type_idx].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }

  bool check (function_checker &c) const override
  {
    /* Check whether rounding mode argument is a valid immediate.  */
    if (c.base->has_rounding_mode_operand_p ())
      {
	/* Some invalid overload intrinsic like below will have zero for
	   c.arg_num ().  Thus, make sure arg_num is big enough here.
	   __riscv_vaadd () will make c.arg_num () == 0.  */
	if (!c.any_type_float_p () && c.arg_num () >= 2)
	  return c.require_immediate (c.arg_num () - 2, VXRM_RNU, VXRM_ROD);
	/* TODO: We will support floating-point intrinsic modeling
	   rounding mode in the future.  */
      }
    return true;
  }
};

/* move_def class. Handle vmv.v.v/vmv.v.x.  */
struct move_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    /* vmv.v.x/vfmv.v.f (PRED_none) can not be overloaded.  */
    if ((instance.op_info->op == OP_TYPE_x || instance.op_info->op == OP_TYPE_f)
	&& overloaded_p && instance.pred == PRED_TYPE_none)
      return nullptr;

    b.append_base_name (instance.base_name);

    if (!overloaded_p)
      {
	b.append_name (operand_suffixes[instance.op_info->op]);
	b.append_name (type_suffixes[instance.type.index].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* mask_alu_def class.  */
struct mask_alu_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;

    b.append_base_name (instance.base_name);

    if (instance.op_info->op == OP_TYPE_mm || instance.op_info->op == OP_TYPE_m)
      if (!overloaded_p)
	b.append_name (operand_suffixes[instance.op_info->op]);

    /* vop<sew>_<op> --> vop<sew>_<op>_<type>.  */
    if (!overloaded_p)
      b.append_name (type_suffixes[instance.type.index].vector);

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* reduc_alu_def class.  */
struct reduc_alu_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    b.append_base_name (instance.base_name);

    /* vop_<op> --> vop<sew>_<op>_<type>.  */
    if (!overloaded_p)
      {
	b.append_name (operand_suffixes[instance.op_info->op]);
	b.append_name (type_suffixes[instance.type.index].vector);
	vector_type_index ret_type_idx
	  = instance.op_info->ret.get_function_type_index (instance.type.index);
	b.append_name (type_suffixes[ret_type_idx].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* th_extract_def class.  */
struct th_extract_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
      bool overloaded_p) const override
  {
    b.append_name ("__riscv_th_");
    b.append_name (instance.base_name);

    if (overloaded_p)
      return b.finish_name ();
    b.append_name (type_suffixes[instance.type.index].vector);
    b.append_name (type_suffixes[instance.type.index].scalar);
    return b.finish_name ();
  }
};

/* scalar_move_def class.  */
struct scalar_move_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    b.append_base_name (instance.base_name);
    if (overloaded_p)
      return b.finish_name ();
    b.append_name (operand_suffixes[instance.op_info->op]);
    b.append_name (type_suffixes[instance.type.index].vector);
    b.append_name (type_suffixes[instance.type.index].scalar);
    return b.finish_name ();
  }
};

/* vundefined_def class.  */
struct vundefined_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    if (overloaded_p)
      return nullptr;
    b.append_base_name (instance.base_name);
    b.append_name (type_suffixes[instance.type.index].vector);
    return b.finish_name ();
  }
};

/* ime_pair_unpair_def class.

   Shape for the IME `pair' / `unpairlo' / `unpairhi' primitives that
   compose an m16 accumulator from two m8 halves or extract a half
   from one.  The user-visible name follows the
   `__riscv_<base>_<m16-type-suffix>' pattern -- one builtin per m16
   element type per direction.  These primitives are not overloaded:
   the type suffix selects the m16 element type unambiguously and the
   builtin's m8 argument types follow from it, so a no-suffix overloaded form would have no useful type to
   resolve.  */
struct ime_pair_unpair_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    if (overloaded_p)
      return nullptr;
    b.append_base_name (instance.base_name);
    b.append_name (type_suffixes[instance.type.index].vector);
    return b.finish_name ();
  }
};

/* misc_def class.  */
struct misc_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    b.append_base_name (instance.base_name);

    if (!overloaded_p)
      {
	b.append_name (operand_suffixes[instance.op_info->op]);
	vector_type_index arg0_type_idx
	  = instance.op_info->args[0].get_function_type_index (
	    instance.type.index);
	b.append_name (type_suffixes[arg0_type_idx].vector);
      }

    vector_type_index ret_type_idx
      = instance.op_info->ret.get_function_type_index (instance.type.index);
    b.append_name (type_suffixes[ret_type_idx].vector);
    return b.finish_name ();
  }
};

/* vset_def class.  */
struct vset_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    b.append_base_name (instance.base_name);

    if (!overloaded_p)
      {
	b.append_name (operand_suffixes[instance.op_info->op]);
	vector_type_index arg_type_idx
	  = instance.op_info->args[2].get_function_type_index (
	    instance.type.index);
	b.append_name (type_suffixes[arg_type_idx].vector);

	vector_type_index ret_type_idx
	  = instance.op_info->ret.get_function_type_index (instance.type.index);
	b.append_name (type_suffixes[ret_type_idx].vector);
      }
    return b.finish_name ();
  }

  bool check (function_checker &c) const override
  {
    poly_int64 outer_size = GET_MODE_SIZE (c.arg_mode (0));
    poly_int64 inner_size = GET_MODE_SIZE (c.arg_mode (2));
    if (maybe_eq (inner_size, 0))
      return false;
    unsigned int nvecs = exact_div (outer_size, inner_size).to_constant ();
    return c.require_immediate (1, 0, nvecs - 1);
  }
};

/* vget_def class.  */
struct vget_def : public misc_def
{
  bool check (function_checker &c) const override
  {
    poly_int64 outer_size = GET_MODE_SIZE (c.arg_mode (0));
    poly_int64 inner_size = GET_MODE_SIZE (c.ret_mode ());
    if (maybe_eq (inner_size, 0))
      return false;
    unsigned int nvecs = exact_div (outer_size, inner_size).to_constant ();
    return c.require_immediate (1, 0, nvecs - 1);
  }
};

/* vcreate_def class.  */
struct vcreate_def : public build_base
{
  void build (function_builder &b,
	      const function_group_info &group) const override
  {
    for (unsigned int vec_type_idx = 0;
	 group.ops_infos.types[vec_type_idx].index != NUM_VECTOR_TYPES;
	 ++vec_type_idx)
      {
	  auto_vec<tree, 8> argument_types;
	  function_instance function_instance (group.base_name, *group.base,
					      *group.shape,
					      group.ops_infos.types[vec_type_idx],
					      group.preds[0], &group.ops_infos);

	  tree return_type = group.ops_infos.ret.get_tree_type (
	    group.ops_infos.types[vec_type_idx].index);

	  if (!return_type)
	    continue;

	  tree arg_type = function_instance.op_info->args[0].get_tree_type (
	    function_instance.type.index);

	  machine_mode outer_mode = TYPE_MODE (return_type);
	  machine_mode inner_mode = TYPE_MODE (arg_type);
	  unsigned int nargs
	    = exact_div (GET_MODE_SIZE (outer_mode), GET_MODE_SIZE (inner_mode))
		.to_constant ();

	  for (unsigned int i = 0; i < nargs; i++)
	    argument_types.quick_push (arg_type);

	  b.add_unique_function (function_instance, (*group.shape), return_type,
	    argument_types, group.required_extensions);
     }
  }

  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    if (overloaded_p)
      return nullptr;
    b.append_base_name (instance.base_name);
    b.append_name (operand_suffixes[instance.op_info->op]);

    if (instance.op_info->ret.base_type != RVV_BASE_vector)
      {
	vector_type_index arg_type_idx
	  = instance.op_info->args[0].get_function_type_index (
	    instance.type.index);
	b.append_name (type_suffixes[arg_type_idx].vector);
      }

    vector_type_index ret_type_idx
      = instance.op_info->ret.get_function_type_index (instance.type.index);
    b.append_name (type_suffixes[ret_type_idx].vector);
    return b.finish_name ();
  }
};

/* read_vl_def class.  */
struct read_vl_def : public function_shape
{
  void build (function_builder &b,
	      const function_group_info &group) const override
  {
    auto_vec<tree> argument_types;
    b.add_unique_function (get_read_vl_instance (), (*group.shape),
			   size_type_node, argument_types,
			   group.required_extensions);
  }

  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    if (overloaded_p)
      return nullptr;
    b.append_base_name (instance.base_name);
    return b.finish_name ();
  }
};

/* fault_load_def class.  */
struct fault_load_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;
    tree type = builtin_types[instance.type.index].vector;
    machine_mode mode = TYPE_MODE (type);
    int sew = GET_MODE_BITSIZE (GET_MODE_INNER (mode));
    b.append_name ("__riscv_");
    b.append_name ("vle");
    b.append_sew (sew);
    b.append_name ("ff");

    /* vop<sew>_v --> vop<sew>_v_<type>.  */
    if (!overloaded_p)
      {
	/* vop<sew> --> vop<sew>_v.  */
	b.append_name (operand_suffixes[instance.op_info->op]);
	/* vop<sew>_v --> vop<sew>_v_<type>.  */
	b.append_name (type_suffixes[instance.type.index].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* vlenb_def class.  */
struct vlenb_def : public function_shape
{
  void build (function_builder &b,
	      const function_group_info &group) const override
  {
    auto_vec<tree> argument_types;
    function_instance function_instance (group.base_name, *group.base,
					 *group.shape, group.ops_infos.types[0],
					 group.preds[0], &group.ops_infos);
    b.add_unique_function (function_instance, (*group.shape),
			   long_unsigned_type_node, argument_types,
			   group.required_extensions);
  }

  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    if (overloaded_p)
      return nullptr;
    b.append_base_name (instance.base_name);
    return b.finish_name ();
  }
};

/* vsetlambda_def class.  Single non-overloaded function taking one
   size_t and returning size_t -- the C surface for
   `__riscv_vsetlambda(requested_lambda)'.  */
struct vsetlambda_def : public function_shape
{
  void build (function_builder &b,
	      const function_group_info &group) const override
  {
    auto_vec<tree> argument_types;
    argument_types.safe_push (size_type_node);
    function_instance function_instance (group.base_name, *group.base,
					 *group.shape, group.ops_infos.types[0],
					 group.preds[0], &group.ops_infos);
    b.add_unique_function (function_instance, (*group.shape),
			   long_unsigned_type_node, argument_types,
			   group.required_extensions);
  }

  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    if (overloaded_p)
      return nullptr;
    b.append_base_name (instance.base_name);
    return b.finish_name ();
  }
};

/* seg_loadstore_def class.  */
struct seg_loadstore_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;

    b.append_base_name (instance.base_name);

    tree type = builtin_types[instance.type.index].vector;
    machine_mode mode = TYPE_MODE (type);

    int nf = get_nf (mode);
    /* vop --> vop<nf>.  */
    b.append_nf (nf);

    /* vop<nf> --> vop<nf>e.  */
    b.append_name ("e");

    int sew = GET_MODE_BITSIZE (GET_MODE_INNER (mode));
    /* vop<nf>e --> vop<nf>e<sew>.  */
    b.append_sew (sew);

    if (!overloaded_p)
      {
	/* vop<nf>e<sew> --> vop<nf>e<sew>_v.  */
	b.append_name (operand_suffixes[instance.op_info->op]);
	/* vop<nf>e<sew>_v --> vop<nf>e<sew>_v_<type>.  */
	b.append_name (type_suffixes[instance.type.index].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* seg_indexed_loadstore_def class.  */
struct seg_indexed_loadstore_def : public indexed_loadstore_def
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;

    b.append_base_name (instance.base_name);

    tree type = builtin_types[instance.type.index].vector;
    machine_mode mode = TYPE_MODE (type);

    int nf = get_nf (mode);
    /* vop --> vop<nf>.  */
    b.append_nf (nf);

    /* vop<nf> --> vop<nf>ei.  */
    b.append_name ("ei");

    /* vop<nf>ei --> vop<nf>ei<eew>.  */
    vector_type_index arg1_type_idx
      = instance.op_info->args[1].get_function_type_index (instance.type.index);
    tree index_type = builtin_types[arg1_type_idx].vector;
    machine_mode index_mode = TYPE_MODE (index_type);
    int eew = GET_MODE_BITSIZE (GET_MODE_INNER (index_mode));
    b.append_sew (eew);

    if (!overloaded_p)
      {
	/* vop<sew> --> vop<sew>_v.  */
	b.append_name (operand_suffixes[instance.op_info->op]);
	/* vop<sew>_v --> vop<sew>_v_<type>.  */
	b.append_name (type_suffixes[instance.type.index].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* seg_fault_load_def class.  */
struct seg_fault_load_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;

    b.append_name ("__riscv_vlseg");

    tree type = builtin_types[instance.type.index].vector;
    machine_mode mode = TYPE_MODE (type);

    int nf = get_nf (mode);
    /* vop --> vop<nf>.  */
    b.append_nf (nf);

    /* vop<nf> --> vop<nf>e.  */
    b.append_name ("e");

    int sew = GET_MODE_BITSIZE (GET_MODE_INNER (mode));
    /* vop<nf>e --> vop<nf>e<sew>.  */
    b.append_sew (sew);

    /* vop<nf>e<sew> --> vop<nf>e<sew>ff.  */
    b.append_name ("ff");

    if (!overloaded_p)
      {
	/* vop<nf>e<sew>ff --> vop<nf>e<sew>ff_v.  */
	b.append_name (operand_suffixes[instance.op_info->op]);
	/* vop<nf>e<sew>ff_v --> vop<nf>e<sew>ff_v_<type>.  */
	b.append_name (type_suffixes[instance.type.index].vector);
      }

    /* According to rvv-intrinsic-doc, it does not add "_m" suffix
       for vop_m C++ overloaded API.  */
    if (overloaded_p && instance.pred == PRED_TYPE_m)
      return b.finish_name ();
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* vsm4r/vaes* class.  */
struct crypto_vv_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
                  bool overloaded_p) const override
  {
    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;
    b.append_base_name (instance.base_name);
    b.append_name (operand_suffixes[instance.op_info->op]);

    if (!overloaded_p)
    {
      if (instance.op_info->op == OP_TYPE_vv)
        b.append_name (type_suffixes[instance.type.index].vector);
      else
      {
        vector_type_index arg0_type_idx
          = instance.op_info->args[1].get_function_type_index
            (instance.type.index);
        b.append_name (type_suffixes[arg0_type_idx].vector);
        vector_type_index ret_type_idx
          = instance.op_info->ret.get_function_type_index
            (instance.type.index);
        b.append_name (type_suffixes[ret_type_idx].vector);
      }
    }

    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* vaeskf1/vaeskf2/vsm4k/vsm3c class.  */
struct crypto_vi_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
                  bool overloaded_p) const override
  {
    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;
    b.append_base_name (instance.base_name);
    if (!overloaded_p)
    {
      b.append_name (operand_suffixes[instance.op_info->op]);
      b.append_name (type_suffixes[instance.type.index].vector);
    }
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* vaesz class.  */
struct crypto_vv_no_op_type_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
                  bool overloaded_p) const override
  {
    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;
    b.append_base_name (instance.base_name);

    if (!overloaded_p)
    {
      b.append_name (operand_suffixes[instance.op_info->op]);
      vector_type_index arg0_type_idx
        = instance.op_info->args[1].get_function_type_index
          (instance.type.index);
      b.append_name (type_suffixes[arg0_type_idx].vector);
      vector_type_index ret_type_idx
        = instance.op_info->ret.get_function_type_index
          (instance.type.index);
      b.append_name (type_suffixes[ret_type_idx].vector);
    }

    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* sf_vqmacc_def class.  */
struct sf_vqmacc_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    b.append_base_name (instance.base_name);

    /* vop --> vop_v.  */
    b.append_name (operand_suffixes[instance.op_info->op]);

    /* Return nullptr if it can not be overloaded.  */
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return b.finish_name ();

    if (!overloaded_p)
      {
	/* vop_v --> vop_v_<type>.  */
	b.append_name (type_suffixes[instance.type.index].vector);
      }

    /* According to SIFIVE vector-intrinsic-doc, it adds "_tu" suffix
       for vop_m C++ overloaded API.*/
    b.append_name (predication_suffixes[instance.pred]);

    return b.finish_name ();
  }
};

/* sf_vfnrclip_def class. Handle instructions like vfnrclip.  */
struct sf_vfnrclip_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    b.append_base_name (instance.base_name);

    if (overloaded_p && (!instance.base->can_be_overloaded_p (instance.pred)
			  || instance.pred == PRED_TYPE_m))
      return b.finish_name ();

    if (!overloaded_p)
      {
	vector_type_index ret_type_idx
	  = instance.op_info->ret.get_function_type_index (instance.type.index);
	/* v --> v_<type>.  */
	b.append_name (type_suffixes[ret_type_idx].vector);
      }

    /* According to SIFIVE vector-intrinsic-doc, it adds "_m\_tu\
       _tum\_tumu\_mu" suffixes for vop_m C++ overloaded API.*/
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* sf_vcix_se_def class.  */
struct sf_vcix_se_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    /* Return nullptr if it is overloaded.  */
    if (overloaded_p)
      return nullptr;

    b.append_base_name (instance.base_name);

    /* vop --> vop<op>_se_<type>.  */
    if (!overloaded_p)
      {
	b.append_name (operand_suffixes[instance.op_info->op]);
	b.append_name ("_se");
	b.append_name (type_suffixes[instance.type.index].vector);
      }
    return b.finish_name ();
  }
};

/* sf_vcix_def class.  */
struct sf_vcix_def : public build_base
{
  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    /* Return nullptr if it is overloaded.  */
    if (overloaded_p)
      return nullptr;

    b.append_base_name (instance.base_name);

    /* vop --> vop_<type>.  */
    if (!overloaded_p)
      {
	b.append_name (operand_suffixes[instance.op_info->op]);
	b.append_name (type_suffixes[instance.type.index].vector);
      }
    return b.finish_name ();
  }
};

/* Shape for IME tile load/store intrinsics (vmtl.v, vmts.v, vmttl.v,
   vmtts.v).  Each group registers the instruction with every _L{N}
   lambda immediate and _as_e{S} storage width, recorded in the
   function_instance::variant.  Names follow:

     __riscv_{base}_v_{type}[_as_e{S}][_L{N}][_m]  */
struct ime_tile_def : public build_base
{
  void build (function_builder &b,
	      const function_group_info &group) const override
  {
    for (unsigned lambda = 0; lambda < 8; lambda++)
      for (unsigned as_e = 0; as_e < 5; as_e++)
	{
	  const rvv_op_info *ops
	    = as_e ? ime_tile_as_e_op_info (group.ops_infos) : &group.ops_infos;
	  for (unsigned pred_idx = 0; group.preds[pred_idx] != NUM_PRED_TYPES;
	       ++pred_idx)
	    for (unsigned vec_type_idx = 0;
		 ops->types[vec_type_idx].index != NUM_VECTOR_TYPES;
		 ++vec_type_idx)
	      {
		/* The storage width S of _as_e{S} is at least the element
		   width.  */
		tree type = rvv_arg_type_info (RVV_BASE_vector)
			      .get_tree_type (ops->types[vec_type_idx].index);
		if (as_e && type
		    && GET_MODE_UNIT_BITSIZE (TYPE_MODE (type)) > 4u << as_e)
		  continue;
		build_one (b, group, pred_idx, vec_type_idx, ops,
			   ime_tile_variant (lambda, as_e));
	      }
	}
  }

  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    /* The _as_e{S} forms take an untyped pointer, so their arguments do
       not determine the data type.  */
    if (overloaded_p
	&& (!instance.base->can_be_overloaded_p (instance.pred)
	    || ime_tile_as_e (instance.variant)))
      return nullptr;

    b.append_base_name (instance.base_name);
    /* "_v" operand suffix.  */
    b.append_name (operand_suffixes[instance.op_info->op]);
    if (!overloaded_p)
      b.append_name (type_suffixes[instance.type.index].vector);
    char buf[16];
    if (unsigned as_e = ime_tile_as_e (instance.variant))
      {
	snprintf (buf, sizeof (buf), "_as_e%u", 4u << as_e);
	b.append_name (buf);
      }
    if (unsigned lambda = ime_tile_lambda (instance.variant))
      {
	snprintf (buf, sizeof (buf), "_L%u", 1u << (lambda - 1));
	b.append_name (buf);
      }
    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* Return the type of input ARG of the IME multiply-accumulate
   INSTANCE.  */
static vector_type_index
ime_input_type (const function_instance &instance,
		const rvv_arg_type_info &arg)
{
  if (arg.base_type == RVV_BASE_ime_i8_lmul1_vector)
    return VECTOR_TYPE_vint8m1_t;
  return arg.get_function_type_index (instance.type.index);
}

/* Return the format of the elements of TYPE, for microscaled inputs if
   MX.  */
static riscv_zvvm_format
ime_format (vector_type_index type, bool mx)
{
  const char *s = type_suffixes[type].scalar + 1;
  int bits = atoi (s + (*s == 'b' ? 2 : 1));
  int fmt;
  if (*s == 'i' || *s == 'u')
    fmt = (mx ? ZVVM_FMT_MXINT4 : ZVVM_FMT_Int4) + exact_log2 (bits / 4);
  else if (*s == 'b')
    fmt = ZVVM_FMT_BF16;
  else if (strchr (s, 'e'))
    fmt = (mx ? ZVVM_FMT_MXFP4 : ZVVM_FMT_OFP4) + (bits == 8);
  else
    fmt = ZVVM_FMT_FP16 + exact_log2 (bits / 16);
  return (riscv_zvvm_format) fmt;
}

/* Return the Zvvm extension of the multiply-accumulate with inputs of
   type A and an accumulator of type C, microscaled with block size BS
   if BS is nonzero.  */
static required_ext
ime_mm_required_ext (vector_type_index a, vector_type_index c, int bs)
{
  riscv_zvvm_format fa = ime_format (a, bs), fc = ime_format (c, false);
#define ZVV_IMM(NAME, UPPER, A, C, ZVE)					\
  if (!bs && fa == ZVVM_FMT_##A && fc == ZVVM_FMT_##C)			\
    return UPPER##_EXT;
#define ZVV_FMM ZVV_IMM
#define ZVV_XMM(NAME, UPPER, A, C, BS, DEP)				\
  if (bs == BS && fa == ZVVM_FMT_##A && fc == ZVVM_FMT_##C)		\
    return UPPER##_EXT;
#include "riscv-ext-zvv.def"
  gcc_unreachable ();
}

struct ime_mmacc_def : public build_base
{
  /* IME intrinsics derive their vs1/vs2 operand types by chaining
     type-info lookups (double_trunc + lmul1 + signed/unsigned).  Not all
     accumulator types in the i_ops/u_ops type sets have a valid chain
     (e.g. narrowest SEW has no double_trunc, fractional LMULs have no
     lmul1 sibling).  This `build' override iterates the type list
     directly so the `continue' on `check_type' failure cleanly skips
     individual unresolved instances.  */
  void build (function_builder &b,
	      const function_group_info &group) const override
  {
    build_ops (b, group, group.ops_infos);
  }

  /* Register the functions of GROUP with the operands in OPS.  */
  static void build_ops (function_builder &b, const function_group_info &group,
			 const rvv_op_info &ops)
  {
    for (unsigned int pred_idx = 0; group.preds[pred_idx] != NUM_PRED_TYPES;
	 ++pred_idx)
      for (unsigned int vec_type_idx = 0;
	   ops.types[vec_type_idx].index != NUM_VECTOR_TYPES;
	   ++vec_type_idx)
	{
	  auto_vec<tree, 8> argument_types;
	  function_instance function_instance (group.base_name, *group.base,
					       *group.shape,
					       ops.types[vec_type_idx],
					       group.preds[pred_idx], &ops);
	  tree return_type
	    = ops.ret.get_tree_type (ops.types[vec_type_idx].index);
	  b.allocate_argument_types (function_instance, argument_types);
	  b.apply_predication (function_instance, return_type, argument_types);
	  if (!check_type (return_type, argument_types))
	    continue;
	  /* The accumulator register group is never fractional:
	     EMUL_C is 1, 2, 4, 8 or 16.  */
	  enum vlmul_type lmul = get_vlmul (TYPE_MODE (return_type));
	  if (lmul == LMUL_F8 || lmul == LMUL_F4 || lmul == LMUL_F2)
	    continue;
	  const char *bs = strstr (group.base_name, "_bs");
	  required_ext required
	    = ime_mm_required_ext (ime_input_type (function_instance,
						   ops.args[1]),
				   ops.types[vec_type_idx].index,
				   bs ? atoi (bs + 3) : 0);
	  b.add_overloaded_function (function_instance, *group.shape,
				     required);
	  b.add_unique_function (function_instance, (*group.shape), return_type,
				 argument_types, required);
	}
  }

  /* Canonical IME suffix order (per revised intrinsics spec):

     __riscv_{base}_{op}{type-suffix}[_{inputA}[_{inputB}]]
              [_su|_us][_lm{N}][_bs{16,32}][_L{N}]{pred}

     The _su / _us suffix comes from the input types (see
     ime_int_mmacc_input); the other suffixes are decoded from tokens
     at the end of the internal base name.  */
  /* Known FP format tokens that appear as trailing suffixes in the
     internal base name for FP mmacc variants.  Ordered longest-first
     so that e.g. "_f8e4m3" is matched before a hypothetical shorter
     prefix.  */
  static constexpr const char *fp_format_tokens[] = {
    "_f8e4m3", "_f8e5m2", "_f4e2m1",
    "_bf16", "_f16", "_fp16", "_f32",
    "_i8",
    nullptr
  };

  /* Block-scale suffix tokens for microscaled variants.  */
  static constexpr const char *bs_tokens[] = {
    "_bs32", "_bs16",
    nullptr
  };

  /* Source-LMUL suffix tokens for the W=1 mmacc _lm{N} variants.  These
     are stripped from the internal base name and re-appended to the
     user-visible name after the accumulator type suffix; see the
     canonical ordering in get_name() below.  */
  static constexpr const char *lm_tokens[] = {
    "_lm2", "_lm4", "_lm8",
    nullptr
  };

  static void
  decode_base_name (const char *internal, char *base_buf,
		    size_t base_buf_sz, const char *&bs_suffix,
		    const char *&lm_suffix)
  {
    size_t ilen = strlen (internal);
    size_t base_len = ilen;
    bs_suffix = "";
    lm_suffix = "";

    /* First strip any trailing "_bs32" / "_bs16" block-scale suffix.  */
    for (const char *const *bp = bs_tokens; *bp; ++bp)
      {
	size_t tlen = strlen (*bp);
	if (ilen > tlen
	    && memcmp (internal + ilen - tlen, *bp, tlen) == 0)
	  {
	    bs_suffix = *bp;
	    ilen -= tlen;
	    base_len = ilen;
	    break;
	  }
      }

    /* Then strip any trailing "_lm2" / "_lm4" / "_lm8" source-LMUL
       suffix.  The token is recorded for re-emission in the canonical
       user-visible name.  */
    for (const char *const *lp = lm_tokens; *lp; ++lp)
      {
	size_t tlen = strlen (*lp);
	if (ilen > tlen
	    && memcmp (internal + ilen - tlen, *lp, tlen) == 0)
	  {
	    lm_suffix = *lp;
	    ilen -= tlen;
	    base_len = ilen;
	    break;
	  }
      }

    /* Strip any trailing "_m16" accumulator-MUL_C tag.  This is an
       internal disambiguator that routes to the m16-acc RTL pattern;
       the user-visible name carries the "m16" inside the accumulator
       type suffix (e.g. "_i32m16") emitted later in get_name(), so the
       tag itself is consumed silently here.  */
    {
      static constexpr const char m16_tag[] = "_m16";
      static constexpr size_t m16_len = sizeof (m16_tag) - 1;
      if (ilen > m16_len
	  && memcmp (internal + ilen - m16_len, m16_tag, m16_len) == 0)
	{
	  ilen -= m16_len;
	  base_len = ilen;
	}
    }

      {
	/* FP/integer format token: "_f16", "_bf16", "_f32", "_f8e4m3",
	   "_i8", etc.  These are stripped from the user-visible base
	   name; the input type suffix is emitted separately by
	   get_name().

	   Mixed-input variants (e.g. "vfqmmacc_f8e4m3_f8e5m2") encode
	   the vs1 / vs2 formats as two consecutive trailing tokens.
	   Strip them iteratively so the base name keeps its mnemonic
	   only.  get_name() re-emits each input suffix from
	   args[1] / args[2].  */
	bool stripped_any = false;
	while (ilen > 0)
	  {
	    bool stripped = false;
	    for (const char *const *fp = fp_format_tokens; *fp; ++fp)
	      {
		size_t tlen = strlen (*fp);
		if (ilen > tlen
		    && memcmp (internal + ilen - tlen, *fp, tlen) == 0)
		  {
		    ilen -= tlen;
		    stripped = true;
		    stripped_any = true;
		    break;
		  }
	      }
	    if (!stripped)
	      break;
	  }
	if (stripped_any)
	  base_len = ilen;
      }

    gcc_assert (base_len + 1 <= base_buf_sz);
    memcpy (base_buf, internal, base_len);
    base_buf[base_len] = '\0';
  }

  /* Return the LMUL (1, 2, 4 or 8) of the vs1 input operand of an IME
     multiply-accumulate instance.  */
  static unsigned
  ime_input_lmul (const function_instance &instance)
  {
    const rvv_arg_type_info &a = instance.op_info->args[1];
    if (a.base_type == RVV_BASE_ime_input
	|| a.base_type == RVV_BASE_ime_fixed_input)
      return 1 << ime_input_lm_log2 (a);
    return 1;
  }

  char *get_name (function_builder &b, const function_instance &instance,
		  bool overloaded_p) const override
  {
    if (overloaded_p && !instance.base->can_be_overloaded_p (instance.pred))
      return nullptr;

    char base_buf[32];
    const char *bs_suffix;
    const char *lm_suffix;
    decode_base_name (instance.base_name, base_buf, sizeof (base_buf),
		      bs_suffix, lm_suffix);
    (void) lm_suffix;  /* Consumed via the args base type in
			  ime_input_lmul ().  */

    /* The _su / _us suffix of the integer multiply-accumulates.  */
    const char *sign_suffix = "";
    ime_int_input in;
    if (ime_int_mmacc_input (instance, &in))
      sign_suffix = (in.sign == IME_SIGN_SU ? "_su"
		     : in.sign == IME_SIGN_US ? "_us" : "");

    /* "__riscv_" + <base>.  */
    b.append_base_name (base_buf);

    /* Operand suffix ("_vv").  */
    b.append_name (operand_suffixes[instance.op_info->op]);

    if (!overloaded_p)
      {
	/* Type suffix -- encodes accumulator SEW and MUL_C, e.g. "_i16m2".  */
	b.append_name (type_suffixes[instance.type.index].vector);

	/* For FP widening variants, append the input type suffix.
	   The input base type tells us which fixed-type LMUL=1 vector
	   is used; map it to the corresponding type suffix string.

	   For mixed-input variants (vs1 and vs2 different formats), also
	   append the vs2 suffix.  Same-format and integer cases keep just
	   the single vs1 suffix (or none for integer).  */
	auto append_input_suffix = [&] (const rvv_arg_type_info &arg) -> bool
	  {
	    vector_type_index idx = ime_input_type (instance, arg);
	    /* Integer inputs are named only if they are Int4, in the
	       signedness of the accumulator; the _su or _us suffix gives
	       that of each input.  */
	    if (arg.base_type == RVV_BASE_ime_input)
	      {
		idx = ime_input (ime_input_w_log2 (arg), ime_input_lm_log2 (arg))
			.get_function_type_index (instance.type.index);
		if (atoi (type_suffixes[idx].scalar + 2) != 4)
		  return false;
	      }
	    b.append_name (type_suffixes[idx].vector);
	    return true;
	  };
	const rvv_arg_type_info &vs1 = instance.op_info->args[1];
	const rvv_arg_type_info &vs2 = instance.op_info->args[2];
	/* Always include the input-format suffix when one is available.
	   It disambiguates between FP widening variants that share an
	   accumulator type (e.g. `vfwmmacc_vv_f32m1_f16m2_lm2' vs
	   `..._f32m1_bf16m2_lm2' -- without the explicit `_f16m2' or
	   `_bf16m2' token the two intrinsics would collide on the same
	   canonical name).  The `_lm{N}' token is then appended below
	   for LMUL>1.  */
	bool emitted_vs1 = append_input_suffix (vs1);
	if (emitted_vs1
	    && vs1.base_type == RVV_BASE_ime_fixed_input
	    && vs1.ime_param != vs2.ime_param)
	  append_input_suffix (vs2);

	/* Mixed-sign token ("_su" / "_us").  */
	if (*sign_suffix)
	  b.append_name (sign_suffix);

	/* "_lm{N}" when the input LMUL is not 1.  Default (LMUL=1) emits
	   nothing, matching the current spec's overloaded-short-form
	   rule and the shipped intrinsics.  */
	unsigned input_lmul = ime_input_lmul (instance);
	if (input_lmul > 1)
	  {
	    char buf[8];
	    snprintf (buf, sizeof (buf), "_lm%u", input_lmul);
	    b.append_name (buf);
	  }

	/* Block-scale suffix ("_bs16" / "_bs32") for microscaled variants.  */
	if (*bs_suffix)
	  b.append_name (bs_suffix);
      }

    b.append_name (predication_suffixes[instance.pred]);
    return b.finish_name ();
  }
};

/* ime_int_mmacc: the IME integer multiply-accumulates (vmmacc, vwmmacc,
   vqmmacc and v8wmmacc).  The function group gives the instruction's
   default form (signed inputs at LMUL=1); this registers every variant
   of it: signed, unsigned, _su and _us inputs, each at input LMUL 1, 2,
   4 and 8, with m1..m8 and with m16 accumulators.  */
struct ime_int_mmacc_def : public ime_mmacc_def
{
  void build (function_builder &b,
	      const function_group_info &group) const override
  {
    function_instance def (group.base_name, *group.base, *group.shape,
			   group.ops_infos.types[0], group.preds[0],
			   &group.ops_infos);
    ime_int_input in;
    gcc_assert (ime_int_mmacc_input (def, &in));
    for (unsigned m16 = 0; m16 < 2; m16++)
      for (unsigned sign = IME_SIGN_SS; sign <= IME_SIGN_US; sign++)
	for (unsigned lm_log2 = 0; lm_log2 < 4; lm_log2++)
	  build_ops (b, group,
		     *ime_int_mmacc_op_info (in.w_log2, (ime_int_sign) sign,
					     lm_log2, m16));
  }
};

/* ime_fp_mmacc: an IME FP multiply-accumulate with given accumulator and
   input formats.  The function group gives its default form (LMUL=1
   inputs, m1..m8 accumulator); this also registers the forms with input
   LMUL 2, 4 and 8, and all four with m16 accumulators.  */
struct ime_fp_mmacc_def : public ime_mmacc_def
{
  void build (function_builder &b,
	      const function_group_info &group) const override
  {
    for (unsigned m16 = 0;; m16++)
      for (unsigned lm_log2 = 0; lm_log2 < 4; lm_log2++)
	{
	  const rvv_op_info *ops
	    = ime_fp_mmacc_op_info (group.ops_infos, lm_log2, m16);
	  if (!ops)
	    return;
	  build_ops (b, group, *ops);
	}
  }
};


SHAPE(vsetvl, vsetvl)
SHAPE(vsetvl, vsetvlmax)
SHAPE(loadstore, loadstore)
SHAPE(indexed_loadstore, indexed_loadstore)
SHAPE(th_loadstore_width, th_loadstore_width)
SHAPE(th_indexed_loadstore_width, th_indexed_loadstore_width)
SHAPE(alu, alu)
SHAPE(alu_frm, alu_frm)
SHAPE(widen_alu, widen_alu)
SHAPE(widen_alu_frm, widen_alu_frm)
SHAPE(no_mask_policy, no_mask_policy)
SHAPE(return_mask, return_mask)
SHAPE(narrow_alu, narrow_alu)
SHAPE(narrow_alu_frm, narrow_alu_frm)
SHAPE(move, move)
SHAPE(mask_alu, mask_alu)
SHAPE(reduc_alu, reduc_alu)
SHAPE(reduc_alu_frm, reduc_alu_frm)
SHAPE(th_extract, th_extract)
SHAPE(scalar_move, scalar_move)
SHAPE(vundefined, vundefined)
SHAPE(ime_pair_unpair, ime_pair_unpair)
SHAPE(misc, misc)
SHAPE(vset, vset)
SHAPE(vget, vget)
SHAPE(vcreate, vcreate)
SHAPE(read_vl, read_vl)
SHAPE(fault_load, fault_load)
SHAPE(vlenb, vlenb)
SHAPE(seg_loadstore, seg_loadstore)
SHAPE(seg_indexed_loadstore, seg_indexed_loadstore)
SHAPE(seg_fault_load, seg_fault_load)
SHAPE(crypto_vv, crypto_vv)
SHAPE(crypto_vi, crypto_vi)
SHAPE(crypto_vv_no_op_type, crypto_vv_no_op_type)
SHAPE (sf_vqmacc, sf_vqmacc)
SHAPE (sf_vfnrclip, sf_vfnrclip)
SHAPE(sf_vcix_se, sf_vcix_se)
SHAPE(sf_vcix, sf_vcix)
SHAPE (ime_mmacc, ime_mmacc)
SHAPE (ime_int_mmacc, ime_int_mmacc)
SHAPE (ime_fp_mmacc, ime_fp_mmacc)
SHAPE (ime_tile, ime_tile)
SHAPE (vsetlambda, vsetlambda)
} // end namespace riscv_vector
