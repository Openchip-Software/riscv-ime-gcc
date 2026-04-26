/* VSETVL pass for RISC-V 'V' Extension for GNU compiler.
   Copyright (C) 2022-2026 Free Software Foundation, Inc.
   Contributed by Juzhe Zhong (juzhe.zhong@rivai.ai), RiVAI Technologies Ltd.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or(at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* The values of the vl and vtype registers will affect the behavior of RVV
   insns. That is, when we need to execute an RVV instruction, we need to set
   the correct vl and vtype values by executing the vsetvl instruction before.
   Executing the fewest number of vsetvl instructions while keeping the behavior
   the same is the problem this pass is trying to solve. This vsetvl pass is
   divided into 5 phases:

     - Phase 1 (fuse local vsetvl infos): traverses each Basic Block, parses
       each instruction in it that affects vl and vtype state and generates an
       array of vsetvl_info objects. Then traverse the vsetvl_info array from
       front to back and perform fusion according to the fusion rules. The fused
       vsetvl infos are stored in the vsetvl_block_info object's `infos` field.

     - Phase 2 (earliest fuse global vsetvl infos): The header_info and
       footer_info of vsetvl_block_info are used as expressions, and the
       earliest of each expression is computed. Based on the earliest
       information, try to lift up the corresponding vsetvl info to the src
       basic block of the edge (mainly to reduce the total number of vsetvl
       instructions, this uplift will cause some execution paths to execute
       vsetvl instructions that shouldn't be there).

     - Phase 3 (pre global vsetvl info): The header_info and footer_info of
       vsetvl_block_info are used as expressions, and the LCM algorithm is used
       to compute the header_info that needs to be deleted and the one that
       needs to be inserted in some edges.

     - Phase 4 (emit vsetvl insns) : Based on the fusion result of Phase 1 and
       the deletion and insertion information of Phase 3, the mandatory vsetvl
       instruction insertion, modification and deletion are performed.

     - Phase 5 (cleanup): Clean up the avl operand in the RVV operator
       instruction and cleanup the unused dest operand of the vsetvl insn.

     After the Phase 1 a virtual CFG of vsetvl_info is generated. The virtual
     basic block is represented by vsetvl_block_info, and the virtual vsetvl
     statements inside are represented by vsetvl_info. The later phases 2 and 3
     are constantly modifying and adjusting this virtual CFG. Phase 4 performs
     insertion, modification and deletion of vsetvl instructions based on the
     optimized virtual CFG. The Phase 1, 2 and 3 do not involve modifications to
     the RTL.
*/

#define IN_TARGET_CODE 1
#define INCLUDE_ALGORITHM
#define INCLUDE_FUNCTIONAL
#define INCLUDE_ARRAY

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "backend.h"
#include "rtl.h"
#include "target.h"
#include "tree-pass.h"
#include "df.h"
#include "rtl-ssa.h"
#include "cfgcleanup.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-opinit.h"
#include "tm-constrs.h"
#include "cfgrtl.h"
#include "cfganal.h"
#include "lcm.h"
#include "predict.h"
#include "profile-count.h"
#include "gcse.h"
#include "cfgloop.h"
#include "diagnostic-core.h"

using namespace rtl_ssa;
using namespace riscv_vector;

/* Set the bitmap DST to the union of SRC of predecessors of
   basic block B.
   It's a bit different from bitmap_union_of_preds in cfganal.cc. This function
   takes into account the case where pred is ENTRY basic block. The main reason
   for this difference is to make it easier to insert some special value into
   the ENTRY base block. For example, vsetvl_info with a status of UNKNOWN.  */
static void
bitmap_union_of_preds_with_entry (sbitmap dst, sbitmap *src, basic_block b)
{
  /* Handle case with no predecessors (including ENTRY block).  */
  if (EDGE_COUNT (b->preds) == 0)
    {
      bitmap_clear (dst);
      return;
    }

  edge e;
  edge_iterator ei;
  /* Union remaining predecessors' bitmaps.  */
  FOR_EACH_EDGE (e, ei, b->preds)
    {
      /* Initialize with first predecessor's bitmap.  */
      if (ei.index == 0)
	{
	  bitmap_copy (dst, src[e->src->index]);
	  continue;
	}

      /* Perform bitmap OR operation element-wise.  */
      bitmap_ior (dst, dst, src[e->src->index]);
    }
}

/* Compute the reaching definition in and out based on the gen and KILL
   information's in each Base Blocks.
   This function references the compute_available implementation in lcm.cc  */
static void
compute_reaching_definition (sbitmap *gen, sbitmap *kill, sbitmap *in,
			    sbitmap *out)
{
  edge e;
  basic_block *worklist, *qin, *qout, *qend, bb;
  unsigned int qlen;
  edge_iterator ei;

  /* Allocate a worklist array/queue.  Entries are only added to the
     list if they were not already on the list.  So the size is
     bounded by the number of basic blocks.  */
  qin = qout = worklist
    = XNEWVEC (basic_block, n_basic_blocks_for_fn (cfun) - NUM_FIXED_BLOCKS);

  /* Put every block on the worklist; this is necessary because of the
     optimistic initialization of AVOUT above.  Use reverse postorder
     to make the forward dataflow problem require less iterations.  */
  int *rpo = XNEWVEC (int, n_basic_blocks_for_fn (cfun) - NUM_FIXED_BLOCKS);
  int n = pre_and_rev_post_order_compute_fn (cfun, NULL, rpo, false);
  for (int i = 0; i < n; ++i)
    {
      bb = BASIC_BLOCK_FOR_FN (cfun, rpo[i]);
      *qin++ = bb;
      bb->aux = bb;
    }
  free (rpo);

  qin = worklist;
  qend = &worklist[n_basic_blocks_for_fn (cfun) - NUM_FIXED_BLOCKS];
  qlen = n_basic_blocks_for_fn (cfun) - NUM_FIXED_BLOCKS;

  /* Mark blocks which are successors of the entry block so that we
     can easily identify them below.  */
  FOR_EACH_EDGE (e, ei, ENTRY_BLOCK_PTR_FOR_FN (cfun)->succs)
    e->dest->aux = ENTRY_BLOCK_PTR_FOR_FN (cfun);

  /* Iterate until the worklist is empty.  */
  while (qlen)
    {
      /* Take the first entry off the worklist.  */
      bb = *qout++;
      qlen--;

      if (qout >= qend)
	qout = worklist;

      /* Do not clear the aux field for blocks which are successors of the
	 ENTRY block.  That way we never add then to the worklist again.  */
      if (bb->aux != ENTRY_BLOCK_PTR_FOR_FN (cfun))
	bb->aux = NULL;

      bitmap_union_of_preds_with_entry (in[bb->index], out, bb);

      if (bitmap_ior_and_compl (out[bb->index], gen[bb->index], in[bb->index],
				kill[bb->index]))
	/* If the out state of this block changed, then we need
	   to add the successors of this block to the worklist
	   if they are not already on the worklist.  */
	FOR_EACH_EDGE (e, ei, bb->succs)
	  if (!e->dest->aux && e->dest != EXIT_BLOCK_PTR_FOR_FN (cfun))
	    {
	      *qin++ = e->dest;
	      e->dest->aux = e;
	      qlen++;

	      if (qin >= qend)
		qin = worklist;
	    }
    }

  clear_aux_for_edges ();
  clear_aux_for_blocks ();
  free (worklist);
}

/* Classification of vsetvl instruction.  */
enum vsetvl_type
{
  VSETVL_NORMAL,
  VSETVL_VTYPE_CHANGE_ONLY,
  VSETVL_DISCARD_RESULT,
  NUM_VSETVL_TYPE
};

enum emit_type
{
  /* emit_insn directly.  */
  EMIT_DIRECT,
  EMIT_BEFORE,
  EMIT_AFTER,
};

static const int MAX_LMUL = 8;

/* dump helper functions */
static const char *
vlmul_to_str (vlmul_type vlmul)
{
  switch (vlmul)
    {
    case LMUL_1:
      return "m1";
    case LMUL_2:
      return "m2";
    case LMUL_4:
      return "m4";
    case LMUL_8:
      return "m8";
    case LMUL_RESERVED:
      return "INVALID LMUL";
    case LMUL_F8:
      return "mf8";
    case LMUL_F4:
      return "mf4";
    case LMUL_F2:
      return "mf2";

    default:
      gcc_unreachable ();
    }
}

static const char *
policy_to_str (bool agnostic_p)
{
  return agnostic_p ? "agnostic" : "undisturbed";
}

/* Return true if it is an RVV instruction depends on VTYPE global
   status register.  */
bool
has_vtype_op (rtx_insn *rinsn)
{
  return recog_memoized (rinsn) >= 0 && get_attr_has_vtype_op (rinsn);
}

/* Return true if the instruction ignores VLMUL field of VTYPE.  */
static bool
ignore_vlmul_insn_p (rtx_insn *rinsn)
{
  return get_attr_type (rinsn) == TYPE_VIMOVVX
	 || get_attr_type (rinsn) == TYPE_VFMOVVF
	 || get_attr_type (rinsn) == TYPE_VIMOVXV
	 || get_attr_type (rinsn) == TYPE_VFMOVFV;
}

/* Return true if the instruction is scalar move instruction.  */
static bool
scalar_move_insn_p (rtx_insn *rinsn)
{
  return get_attr_type (rinsn) == TYPE_VIMOVXV
	 || get_attr_type (rinsn) == TYPE_VFMOVFV;
}

/* Return true if the instruction is fault first load instruction.  */
static bool
fault_first_load_p (rtx_insn *rinsn)
{
  return recog_memoized (rinsn) >= 0
	 && (get_attr_type (rinsn) == TYPE_VLDFF
	     || get_attr_type (rinsn) == TYPE_VLSEGDFF);
}

/* Return the VL output register from a fault-only-first load with VL
   output (pred_fault_load_set_vl pattern) if RINSN is such an insn
   or NULL_RTX otherwise.
   The pattern has: (set vl_output (unspec:P [(reg:SI VL_REGNUM)]
					     UNSPEC_READ_VL))  */
static rtx
get_fof_set_vl_reg (rtx_insn *rinsn)
{
  if (!fault_first_load_p (rinsn))
    return NULL_RTX;

  rtx pat = PATTERN (rinsn);
  if (GET_CODE (pat) != PARALLEL)
    return NULL_RTX;

  if (XVECLEN (pat, 0) != 3)
    return NULL_RTX;

  rtx sub = XVECEXP (pat, 0, 2);
  if (GET_CODE (sub) == SET
      && GET_CODE (SET_SRC (sub)) == UNSPEC
      && XINT (SET_SRC (sub), 1) == UNSPEC_READ_VL)
    return SET_DEST (sub);

  return NULL_RTX;
}

/* Initialize RTL SSA and related infrastructure for vsetvl analysis.  */
static void
init_rtl_ssa ()
{
  calculate_dominance_info (CDI_DOMINATORS);
  loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
  connect_infinite_loops_to_exit ();
  df_analyze ();
  crtl->ssa = new function_info (cfun);
}

/* Finalize RTL SSA and cleanup.  */
static void
finish_rtl_ssa ()
{
  free_dominance_info (CDI_DOMINATORS);
  loop_optimizer_finalize ();
  if (crtl->ssa->perform_pending_updates ())
    cleanup_cfg (0);
  delete crtl->ssa;
  crtl->ssa = nullptr;
}

/* Emit read_vl instructions after fault-only-first loads that have
   a VL output register.
   This needs to happen last, i.e. when we made the VL dataflow
   explicit by inserting vsetvls.  */

static void
emit_fof_read_vls ()
{
  basic_block bb;
  rtx_insn *rinsn;

  FOR_EACH_BB_FN (bb, cfun)
    FOR_BB_INSNS (bb, rinsn)
      {
	if (!NONDEBUG_INSN_P (rinsn))
	  continue;

	rtx vl_dest = get_fof_set_vl_reg (rinsn);
	if (!vl_dest)
	  continue;

	if (dump_file)
	  fprintf (dump_file,
		   "  Inserting read_vl after FoF insn %d into r%d\n",
		   INSN_UID (rinsn), REGNO (vl_dest));

	rtx read_vl_pat = gen_read_vl (Pmode, vl_dest);
	emit_insn_after (read_vl_pat, rinsn);
      }
}

/* Return true if the instruction is read vl instruction.  */
static bool
read_vl_insn_p (rtx_insn *rinsn)
{
  return recog_memoized (rinsn) >= 0 && get_attr_type (rinsn) == TYPE_RDVL;
}

/* Return true if it is a vsetvl instruction.  */
static bool
vector_config_insn_p (rtx_insn *rinsn)
{
  return recog_memoized (rinsn) >= 0 && get_attr_type (rinsn) == TYPE_VSETVL;
}

/* Return true if it is vsetvldi or vsetvlsi.  */
bool
vsetvl_insn_p (rtx_insn *rinsn)
{
  if (!rinsn || !vector_config_insn_p (rinsn))
    return false;
  return (INSN_CODE (rinsn) == CODE_FOR_vsetvldi
	  || INSN_CODE (rinsn) == CODE_FOR_vsetvlsi);
}

/* Return true if it is the bogus vsetvl_pre instruction:

   (define_insn "@vlmax_avl<mode>"
     [(set (match_operand:P 0 "register_operand" "=r")
	(unspec:P [(match_operand:P 1 "const_int_operand" "i")] UNSPEC_VLMAX))]
     "TARGET_VECTOR"
     ""
     [(set_attr "type" "vsetvl_pre")])

   As described above, it's the bogus instruction which doesn't any assembler
   and should be removed eventually.  It's used for occupying a scalar register
   for VLMAX avl RVV instruction before register allocation.

   Before RA:

   ...
   vsetvl_pre (set r136)
   vadd.vv (use r136 with VLMAX avl)
   ...

   After RA:

   ...
   vsetvl_pre (set a5)
   vadd.vv (use r136 with VLMAX avl)
   ...

   VSETVL PASS:

   ...
   vsetvl_pre (set a5) ---> removed.
   vsetvl a5,zero,...  ---> Inserted.
   vadd.vv
   ...
*/
static bool
vsetvl_pre_insn_p (rtx_insn *rinsn)
{
  return recog_memoized (rinsn) >= 0
	 && get_attr_type (rinsn) == TYPE_VSETVL_PRE;
}

/* Return true if it is vsetvl zero, rs1.  */
static bool
vsetvl_discard_result_insn_p (rtx_insn *rinsn)
{
  if (!vector_config_insn_p (rinsn))
    return false;
  return (INSN_CODE (rinsn) == CODE_FOR_vsetvl_discard_resultdi
	  || INSN_CODE (rinsn) == CODE_FOR_vsetvl_discard_resultsi);
}

static bool
real_insn_and_same_bb_p (const insn_info *insn, const bb_info *bb)
{
  return insn != nullptr && insn->is_real () && insn->bb () == bb;
}

/* Helper function to get VL operand for VLMAX insn.  */
static rtx
get_vl (rtx_insn *rinsn)
{
  if (has_vl_op (rinsn))
    {
      extract_insn_cached (rinsn);
      return recog_data.operand[get_attr_vl_op_idx (rinsn)];
    }
  return SET_DEST (XVECEXP (PATTERN (rinsn), 0, 0));
}

/* Helper function to get AVL operand.  */
rtx
get_avl (rtx_insn *rinsn)
{
  if (vsetvl_insn_p (rinsn) || vsetvl_discard_result_insn_p (rinsn))
    return XVECEXP (SET_SRC (XVECEXP (PATTERN (rinsn), 0, 0)), 0, 0);

  if (!has_vl_op (rinsn))
    return NULL_RTX;
  if (vlmax_avl_type_p (rinsn))
    return RVV_VLMAX;
  extract_insn_cached (rinsn);
  return recog_data.operand[get_attr_vl_op_idx (rinsn)];
}

/* Get default mask policy.  */
static bool
get_default_ma ()
{
  /* For the instruction that doesn't require MA, we still need a default value
     to emit vsetvl. We pick up the default value according to prefer policy. */
  return (bool) (get_prefer_mask_policy () & 0x1
		 || (get_prefer_mask_policy () >> 1 & 0x1));
}

/* Helper function to get MA operand.  */
bool
mask_agnostic_p (rtx_insn *rinsn)
{
  /* If it doesn't have MA, we return agnostic by default.  */
  extract_insn_cached (rinsn);
  int ma = get_attr_ma (rinsn);
  return ma == INVALID_ATTRIBUTE ? get_default_ma () : IS_AGNOSTIC (ma);
}

/* Return true if FN has a vector instruction that use VL/VTYPE.  */
static bool
has_vector_insn (function *fn)
{
  basic_block cfg_bb;
  rtx_insn *rinsn;
  FOR_ALL_BB_FN (cfg_bb, fn)
    FOR_BB_INSNS (cfg_bb, rinsn)
      if (NONDEBUG_INSN_P (rinsn) && has_vtype_op (rinsn))
	return true;
  return false;
}

static vlmul_type
calculate_vlmul (unsigned int sew, unsigned int ratio)
{
  const vlmul_type ALL_LMUL[]
    = {LMUL_1, LMUL_2, LMUL_4, LMUL_8, LMUL_F8, LMUL_F4, LMUL_F2};
  for (const vlmul_type vlmul : ALL_LMUL)
    if (calculate_ratio (sew, vlmul) == ratio)
      return vlmul;
  return LMUL_RESERVED;
}

/* Get the currently supported maximum sew used in the int rvv instructions. */
static uint8_t
get_max_int_sew ()
{
  if (TARGET_VECTOR_ELEN_64)
    return 64;
  else if (TARGET_VECTOR_ELEN_32)
    return 32;
  gcc_unreachable ();
}

/* Get the currently supported maximum sew used in the float rvv instructions.
 */
static uint8_t
get_max_float_sew ()
{
  if (TARGET_VECTOR_ELEN_FP_64)
    return 64;
  else if (TARGET_VECTOR_ELEN_FP_32)
    return 32;
  else if (TARGET_VECTOR_ELEN_FP_16)
    return 16;
  gcc_unreachable ();
}

enum def_type
{
  REAL_SET = 1 << 0,
  PHI_SET = 1 << 1,
  BB_HEAD_SET = 1 << 2,
  BB_END_SET = 1 << 3,
  /* ??? TODO: In RTL_SSA framework, we have REAL_SET,
     PHI_SET, BB_HEAD_SET, BB_END_SET and
     CLOBBER_DEF def_info types. Currently,
     we conservatively do not optimize clobber
     def since we don't see the case that we
     need to optimize it.  */
  CLOBBER_DEF = 1 << 4
};

static bool
insn_should_be_added_p (const insn_info *insn, unsigned int types)
{
  if (insn->is_real () && (types & REAL_SET))
    return true;
  if (insn->is_phi () && (types & PHI_SET))
    return true;
  if (insn->is_bb_head () && (types & BB_HEAD_SET))
    return true;
  if (insn->is_bb_end () && (types & BB_END_SET))
    return true;
  return false;
}

static const hash_set<use_info *>
get_all_real_uses (insn_info *insn, unsigned regno)
{
  gcc_assert (insn->is_real ());

  hash_set<use_info *> uses;
  auto_vec<phi_info *> work_list;
  hash_set<phi_info *> visited_list;

  for (def_info *def : insn->defs ())
    {
      if (!def->is_reg () || def->regno () != regno)
	continue;
      set_info *set = safe_dyn_cast<set_info *> (def);
      if (!set)
	continue;
      for (use_info *use : set->nondebug_insn_uses ())
	if (use->insn ()->is_real ())
	  uses.add (use);
      for (use_info *use : set->phi_uses ())
	work_list.safe_push (use->phi ());
    }

  while (!work_list.is_empty ())
    {
      phi_info *phi = work_list.pop ();
      visited_list.add (phi);

      for (use_info *use : phi->nondebug_insn_uses ())
	if (use->insn ()->is_real ())
	  uses.add (use);
      for (use_info *use : phi->phi_uses ())
	if (!visited_list.contains (use->phi ()))
	  work_list.safe_push (use->phi ());
    }
  return uses;
}

/* Recursively find all define instructions. The kind of instruction is
   specified by the DEF_TYPE.  */
static hash_set<set_info *>
get_all_sets (phi_info *phi, unsigned int types)
{
  hash_set<set_info *> insns;
  auto_vec<phi_info *> work_list;
  hash_set<phi_info *> visited_list;
  if (!phi)
    return hash_set<set_info *> ();
  work_list.safe_push (phi);

  while (!work_list.is_empty ())
    {
      phi_info *phi = work_list.pop ();
      visited_list.add (phi);
      for (use_info *use : phi->inputs ())
	{
	  def_info *def = use->def ();
	  set_info *set = safe_dyn_cast<set_info *> (def);
	  if (!set)
	    return hash_set<set_info *> ();

	  gcc_assert (!set->insn ()->is_debug_insn ());

	  if (insn_should_be_added_p (set->insn (), types))
	    insns.add (set);
	  if (set->insn ()->is_phi ())
	    {
	      phi_info *new_phi = as_a<phi_info *> (set);
	      if (!visited_list.contains (new_phi))
		work_list.safe_push (new_phi);
	    }
	}
    }
  return insns;
}

static hash_set<set_info *>
get_all_sets (set_info *set, bool /* get_real_inst */ real_p,
	      bool /*get_phi*/ phi_p, bool /* get_function_parameter*/ param_p)
{
  if (real_p && phi_p && param_p)
    return get_all_sets (safe_dyn_cast<phi_info *> (set),
			 REAL_SET | PHI_SET | BB_HEAD_SET | BB_END_SET);

  else if (real_p && param_p)
    return get_all_sets (safe_dyn_cast<phi_info *> (set),
			 REAL_SET | BB_HEAD_SET | BB_END_SET);

  else if (real_p)
    return get_all_sets (safe_dyn_cast<phi_info *> (set), REAL_SET);
  return hash_set<set_info *> ();
}

static bool
source_equal_p (insn_info *insn1, insn_info *insn2)
{
  if (!insn1 || !insn2)
    return false;
  rtx_insn *rinsn1 = insn1->rtl ();
  rtx_insn *rinsn2 = insn2->rtl ();
  if (!rinsn1 || !rinsn2)
    return false;

  rtx note1 = find_reg_equal_equiv_note (rinsn1);
  rtx note2 = find_reg_equal_equiv_note (rinsn2);
  /* We could handle the case of similar-looking REG_EQUALs as well but
     would need to verify that no insn in between modifies any of the source
     operands.  */
  if (note1 && note2 && rtx_equal_p (note1, note2)
      && REG_NOTE_KIND (note1) == REG_EQUIV)
    return true;
  return false;
}

static insn_info *
extract_single_source (set_info *set)
{
  if (!set)
    return nullptr;
  if (set->insn ()->is_real ())
    return set->insn ();
  if (!set->insn ()->is_phi ())
    return nullptr;
  hash_set<set_info *> sets = get_all_sets (set, true, false, true);
  if (sets.is_empty ())
    return nullptr;

  insn_info *first_insn = (*sets.begin ())->insn ();
  if (first_insn->is_artificial ())
    return nullptr;
  for (const set_info *set : sets)
    {
      /* If there is a head or end insn, we conservative return
	 NULL so that VSETVL PASS will insert vsetvl directly.  */
      if (set->insn ()->is_artificial ())
	return nullptr;
      if (set != *sets.begin () && !source_equal_p (set->insn (), first_insn))
	return nullptr;
    }

  return first_insn;
}

static bool
same_equiv_note_p (set_info *set1, set_info *set2)
{
  insn_info *insn1 = extract_single_source (set1);
  insn_info *insn2 = extract_single_source (set2);
  if (!insn1 || !insn2)
    return false;
  return source_equal_p (insn1, insn2);
}

/* Return true if the SET result is not used by any instructions.  */
static bool
has_no_uses (basic_block cfg_bb, rtx_insn *rinsn, int regno)
{
  if (bitmap_bit_p (df_get_live_out (cfg_bb), regno))
    return false;

  rtx_insn *iter;
  for (iter = NEXT_INSN (rinsn); iter && iter != NEXT_INSN (BB_END (cfg_bb));
       iter = NEXT_INSN (iter))
    if (df_find_use (iter, regno_reg_rtx[regno]))
      return false;

  return true;
}

/* Return true for the special block that we can't apply LCM optimization.  */
static bool
invalid_opt_bb_p (basic_block cfg_bb)
{
  edge e;
  edge_iterator ei;

  /* We don't do LCM optimizations on complex edges.  */
  FOR_EACH_EDGE (e, ei, cfg_bb->preds)
    if (e->flags & EDGE_COMPLEX)
      return true;

  /* We only do LCM optimizations on blocks that are post dominated by
     EXIT block, that is, we don't do LCM optimizations on infinite loop.  */
  FOR_EACH_EDGE (e, ei, cfg_bb->succs)
    if ((e->flags & EDGE_FAKE) || (e->flags & EDGE_ABNORMAL))
      return true;

  return false;
}

/* Get all predecessors of BB.  */
static hash_set<basic_block>
get_all_predecessors (basic_block bb)
{
  hash_set<basic_block> blocks;
  auto_vec<basic_block> work_list;
  hash_set<basic_block> visited_list;
  work_list.safe_push (bb);

  while (!work_list.is_empty ())
    {
      basic_block new_bb = work_list.pop ();
      visited_list.add (new_bb);
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, new_bb->preds)
	{
	  if (!visited_list.contains (e->src))
	    work_list.safe_push (e->src);
	  blocks.add (e->src);
	}
    }
  return blocks;
}

/* This flags indicates the minimum demand of the vl and vtype values by the
   RVV instruction. For example, DEMAND_RATIO_P indicates that this RVV
   instruction only needs the SEW/LMUL ratio to remain the same, and does not
   require SEW and LMUL to be fixed.
   Therefore, if the former RVV instruction needs DEMAND_RATIO_P and the latter
   instruction needs DEMAND_SEW_LMUL_P and its SEW/LMUL is the same as that of
   the former instruction, then we can make the minimum demand of the former
   instruction strict to DEMAND_SEW_LMUL_P, and its required SEW and LMUL are
   the SEW and LMUL of the latter instruction, and the vsetvl instruction
   generated according to the new demand can also be used for the latter
   instruction, so there is no need to insert a separate vsetvl instruction for
   the latter instruction.  */
enum demand_flags : unsigned
{
  DEMAND_EMPTY_P = 0,
  DEMAND_SEW_P = 1 << 0,
  DEMAND_LMUL_P = 1 << 1,
  DEMAND_RATIO_P = 1 << 2,
  DEMAND_GE_SEW_P = 1 << 3,
  DEMAND_TAIL_POLICY_P = 1 << 4,
  DEMAND_MASK_POLICY_P = 1 << 5,
  DEMAND_AVL_P = 1 << 6,
  DEMAND_NON_ZERO_AVL_P = 1 << 7,
  DEMAND_IME_P = 1 << 8,
};

/* We split the demand information into three parts. They are sew and lmul
   related (sew_lmul_demand_type), tail and mask policy related
   (policy_demand_type) and avl related (avl_demand_type). Then we define three
   interfaces available_p, compatible_p and merge. available_p is
   used to determine whether the two vsetvl infos prev_info and next_info are
   available or not. If prev_info is available for next_info, it means that the
   RVV insn corresponding to next_info on the path from prev_info to next_info
   can be used without inserting a separate vsetvl instruction. compatible_p
   is used to determine whether prev_info is compatible with next_info, and if
   so, merge can be used to merge the stricter demand information from
   next_info into prev_info so that prev_info becomes available to next_info.
 */

enum class sew_lmul_demand_type : unsigned
{
  sew_lmul = demand_flags::DEMAND_SEW_P | demand_flags::DEMAND_LMUL_P,
  ratio_only = demand_flags::DEMAND_RATIO_P,
  sew_only = demand_flags::DEMAND_SEW_P,
  ge_sew = demand_flags::DEMAND_GE_SEW_P,
  ratio_and_ge_sew
  = demand_flags::DEMAND_RATIO_P | demand_flags::DEMAND_GE_SEW_P,
};

enum class policy_demand_type : unsigned
{
  tail_mask_policy
  = demand_flags::DEMAND_TAIL_POLICY_P | demand_flags::DEMAND_MASK_POLICY_P,
  tail_policy_only = demand_flags::DEMAND_TAIL_POLICY_P,
  mask_policy_only = demand_flags::DEMAND_MASK_POLICY_P,
  ignore_policy = demand_flags::DEMAND_EMPTY_P,
};

enum class avl_demand_type : unsigned
{
  avl = demand_flags::DEMAND_AVL_P,
  non_zero_avl = demand_flags::DEMAND_NON_ZERO_AVL_P,
  ignore_avl = demand_flags::DEMAND_EMPTY_P,
};

/* Dimensionally separate demand track for the IME vtype bits (lambda,
   altfmt, altfmt_A, altfmt_B, bs).  Non-IME instructions do not care and
   use ignore_ime; IME instructions use ime_vtype and carry the concrete
   tuple in vsetvl_info::m_ime_req.  */
enum class ime_demand_type : unsigned
{
  ime_vtype = demand_flags::DEMAND_IME_P,
  ignore_ime = demand_flags::DEMAND_EMPTY_P,
};

/* IME-specific vtype requirements for an instruction.  Populated only
   when the instruction pattern has its `requires_ime_vtype' attribute
   set to "yes".  The fields reflect the vtype CSR state the IME spec
   demands: the accumulator width (sew / mul_c), the input register-group
   multiplier (lmul), the per-instruction widening factor W, the per-side
   alternate-format selectors, the output format selector, and the
   microscaling block-size selector.

   LAMBDA_LOG2 holds the log2 of the required lambda when VLEN is known
   at compile time, and -1 otherwise (in which case the vsetvl pass must
   emit runtime computation).  */
struct ime_vtype_req
{
  uint8_t sew;
  vlmul_type mul_c;
  uint8_t lmul;
  uint8_t widen_w;
  bool altfmt_a;
  bool altfmt_b;
  bool altfmt;
  bool bs;
  /* True when this instruction treats vtype.lambda as a wildcard -- it
     encodes 0b000 ("fall back to vtype.lambda") in its own lambda-
     select field.  Non-_L tile loads/stores set this.  When a wildcard
     demand is fused with a specific-lambda demand (e.g. a surrounding
     mmacc), the specific lambda wins.  When a wildcard demand stands
     alone, its own lambda_log2 (computed from SEW and MUL_C=1) is
     used.  */
  bool lambda_wildcard;
  int8_t lambda_log2;

  ime_vtype_req ()
    : sew (0), mul_c (LMUL_RESERVED), lmul (0), widen_w (0),
      altfmt_a (false), altfmt_b (false), altfmt (false), bs (false),
      lambda_wildcard (false), lambda_log2 (-1)
  {}

  bool operator== (const ime_vtype_req &o) const
  {
    return sew == o.sew && mul_c == o.mul_c && lmul == o.lmul
	   && widen_w == o.widen_w && altfmt_a == o.altfmt_a
	   && altfmt_b == o.altfmt_b && altfmt == o.altfmt && bs == o.bs
	   && lambda_wildcard == o.lambda_wildcard
	   && lambda_log2 == o.lambda_log2;
  }
  bool operator!= (const ime_vtype_req &o) const { return !(*this == o); }
};

/* Compute log2(lambda) for an IME instruction with the given accumulator
   SEW_BITS and register-group multiplier MUL_C, using the compile-time
   knowledge of VLEN (if any).  Returns:

     >= 0 : the log2 of the required lambda (so lambda = 1 << result);
	    lambda is always in {1, 2, 4, 8, 16, 32, 64} so the result is
	    in the range [0, 6].
     -1   : VLEN is not compile-time known; the caller must emit a
	    runtime computation.
     -2   : the (VLEN, SEW, MUL_C) triple cannot yield an integer lambda.
	    The caller must raise a diagnostic.  */

static int
compute_ime_lambda_log2 (int sew_bits, int mul_c)
{
  if (!BYTES_PER_RISCV_VECTOR.is_constant ())
    return -1;

  HOST_WIDE_INT vlenb = BYTES_PER_RISCV_VECTOR.to_constant ();
  HOST_WIDE_INT vlen_bits = vlenb * 8;
  HOST_WIDE_INT denom = (HOST_WIDE_INT) sew_bits * mul_c;
  if (denom <= 0 || vlen_bits < denom || vlen_bits % denom != 0)
    return -2;
  HOST_WIDE_INT lambda_sq = vlen_bits / denom;
  if (lambda_sq <= 0 || (lambda_sq & (lambda_sq - 1)) != 0)
    return -2;
  int log_sq = exact_log2 (lambda_sq);
  if (log_sq < 0 || (log_sq & 1) != 0)
    return -2;
  int lambda_log2 = log_sq >> 1;
  if (lambda_log2 < 0 || lambda_log2 > 6)
    return -2;
  return lambda_log2;
}

/* Extract the IME vtype requirements for RINSN.  RINSN must have its
   `requires_ime_vtype' attribute set to yes.  The returned record is
   fully determined by RINSN's operand modes; see §C-Language Intrinsics
   of the IME specification for the mapping rules.

   The current implementation handles the integer vmmacc.vv family
   (Zvvmm, W=1).  Integer signedness (altfmt_A / altfmt_B) is currently
   hardcoded to 0 (signed) because the RTL pattern does not yet
   distinguish between vint/vuint operands; that split will come with a
   signed/unsigned UNSPEC separation in a follow-up.  Widening variants,
   FP mmacc and microscaled variants each extend this function with
   their own derivation rules.  */
static ime_vtype_req
riscv_extract_ime_requirements (rtx_insn *rinsn)
{
  ime_vtype_req r;

  /* Callers may have invalidated recog_data for other insns between
     the original extract_insn_cached(rinsn) and us.  Re-extract.  */
  extract_insn_cached (rinsn);

  /* The data-vector mode tells us SEW and (for mmacc) MUL_C.  For
     load-shaped patterns (mmacc, vmtl) operand 0 is the vector
     destination.  For store-shaped patterns (vmts) operand 0 is a
     memory_operand -- fall back to the first vector register
     operand instead.  */
  machine_mode acc_mode = VOIDmode;
  for (int i = 0; i < recog_data.n_operands; i++)
    if (REG_P (recog_data.operand[i])
	&& VECTOR_MODE_P (GET_MODE (recog_data.operand[i])))
      {
	acc_mode = GET_MODE (recog_data.operand[i]);
	break;
      }
  gcc_assert (acc_mode != VOIDmode);
  r.sew = GET_MODE_UNIT_BITSIZE (acc_mode);

  /* Tile load/store instructions (vmtl.v, vmts.v, vmttl.v, vmtts.v)
     move data between memory and a plain vector register group; there
     is no accumulator, so the lambda constraint is computed with a
     notional MUL_C=1.  The LMUL of the data vector is carried through
     the vtype LMUL field separately, not folded into lambda.
     The mmacc family uses the data register's LMUL as MUL_C because
     its vd really is the accumulator register group.  */
  enum attr_type t = get_attr_type (rinsn);
  bool is_tile = (t == TYPE_VLDS || t == TYPE_VSTS);
  if (is_tile)
    r.mul_c = riscv_vector::LMUL_1;
  else
    r.mul_c = get_vlmul (acc_mode);

  /* For the mmacc family operand 2 is vs1 and may have a different
     LMUL (input LMUL) and/or a narrower SEW (widening variants).
     For tile load/store the stride operand is a scalar register, so
     fall back to the data-vector mode.  */
  machine_mode in_mode = acc_mode;
  if (recog_data.n_operands > 2
      && REG_P (recog_data.operand[2])
      && VECTOR_MODE_P (GET_MODE (recog_data.operand[2])))
    in_mode = GET_MODE (recog_data.operand[2]);
  uint8_t in_sew = GET_MODE_UNIT_BITSIZE (in_mode);
  r.lmul = get_vlmul (in_mode);
  gcc_assert (r.sew > 0 && in_sew > 0 && r.sew % in_sew == 0);
  r.widen_w = r.sew / in_sew;

  /* Integer: altfmt (output) always 0.  Signedness comes from the
     per-pattern ime_altfmt attribute set by the RTL pattern.  For
     now we only support same-sign (ss, uu); mixed-sign (su, us)
     will be added with the widening mmacc variants.  */
  r.altfmt = false;

  /* Read the block-scale size from the ime_bs attribute.  */
  switch (get_attr_ime_bs (rinsn))
    {
    case IME_BS_BS32:
      r.bs = true;
      break;
    case IME_BS_BS16:
    case IME_BS_NONE:
      r.bs = false;
      break;
    default:
      gcc_unreachable ();
    }

  switch (get_attr_ime_altfmt (rinsn))
    {
    case IME_ALTFMT_SS:
      r.altfmt_a = false;
      r.altfmt_b = false;
      break;
    case IME_ALTFMT_UU:
      r.altfmt_a = true;
      r.altfmt_b = true;
      break;
    case IME_ALTFMT_SU:
      r.altfmt_a = false;
      r.altfmt_b = true;
      break;
    case IME_ALTFMT_US:
      r.altfmt_a = true;
      r.altfmt_b = false;
      break;
    default:
      gcc_unreachable ();
    }

  /* A "wildcard" IME pattern (non-_L tile load/store) encodes 0b000 in
     its own lambda-select field; it reads vtype.lambda rather than
     overriding it.  In demand fusion the wildcard can accept any
     specific lambda; standing alone it computes its own fallback
     lambda from SEW and MUL_C=1 (tile loads/stores have no
     accumulator).  */
  if (get_attr_ime_lambda (rinsn) == IME_LAMBDA_WILDCARD)
    r.lambda_wildcard = true;

  /* Static lambda if VLEN is known; runtime otherwise.  */
  int mul_c_val;
  switch (r.mul_c)
    {
    case LMUL_1: mul_c_val = 1; break;
    case LMUL_2: mul_c_val = 2; break;
    case LMUL_4: mul_c_val = 4; break;
    case LMUL_8: mul_c_val = 8; break;
    default:     mul_c_val = 0; break;  /* Fractional or reserved: invalid.  */
    }
  if (mul_c_val == 0)
    r.lambda_log2 = -2;
  else
    r.lambda_log2 = compute_ime_lambda_log2 (r.sew, mul_c_val);

  return r;
}

/* Forward declarations for helpers that need vsetvl_info; the
   definitions come right after the class, below.  */
class vsetvl_info;
static rtx emit_ime_vtype_build (const vsetvl_info &info);
static rtx emit_ime_vtype_runtime_pat (const vsetvl_info &info,
				       rtx dest_vl, rtx avl);

/* Go through all uses of INSN looking for a single use of register REG.
   Return true if we find
    - Uses in a non-RVV insn
    - More than one use in an RVV insn
    - A single use in the VL operand of an RVV insn
   and false otherwise.
   A single use in the AVL operand does not count as use as we take care of
   those separately in the pass.  */

static bool
reg_used (insn_info *insn, rtx reg)
{
  unsigned int regno = REGNO (reg);
  const hash_set<use_info *> vl_uses = get_all_real_uses (insn, regno);
  for (use_info *use : vl_uses)
    {
      gcc_assert (use->insn ()->is_real ());
      rtx_insn *rinsn = use->insn ()->rtl ();
      if (!has_vl_op (rinsn)
	  || count_regno_occurrences (rinsn, regno) != 1)
	return true;

      rtx avl = ::get_avl (rinsn);
      if (!avl || !REG_P (avl) || regno != REGNO (avl))
	return true;
    }
  return false;
}


class vsetvl_info
{
private:
  insn_info *m_insn;
  bb_info *m_bb;
  rtx m_avl;
  rtx m_vl;
  set_info *m_avl_def;
  uint8_t m_sew;
  uint8_t m_max_sew;
  vlmul_type m_vlmul;
  uint8_t m_ratio;
  bool m_ta;
  bool m_ma;

  sew_lmul_demand_type m_sew_lmul_demand;
  policy_demand_type m_policy_demand;
  avl_demand_type m_avl_demand;

  enum class state_type
  {
    UNINITIALIZED,
    VALID,
    UNKNOWN,
    EMPTY,
  };
  state_type m_state;

  bool m_delete;
  bool m_change_vtype_only;
  insn_info *m_read_vl_insn;
  bool m_vl_used_by_non_rvv_insn;

  /* IME state.  M_IME_REQUIRED is true when the instruction pattern has
     `requires_ime_vtype=yes'; M_IME_REQ holds the concrete tuple in that
     case.  M_IME_DEMAND is the demand dimension: ime_vtype when the
     instruction needs the IME vtype bits established, ignore_ime
     otherwise.  */
  bool m_ime_required;
  ime_vtype_req m_ime_req;
  ime_demand_type m_ime_demand;

public:
  vsetvl_info ()
    : m_insn (nullptr), m_bb (nullptr), m_avl (NULL_RTX), m_vl (NULL_RTX),
      m_avl_def (nullptr), m_sew (0), m_max_sew (0), m_vlmul (LMUL_RESERVED),
      m_ratio (0), m_ta (false), m_ma (false),
      m_sew_lmul_demand (sew_lmul_demand_type::sew_lmul),
      m_policy_demand (policy_demand_type::tail_mask_policy),
      m_avl_demand (avl_demand_type::avl), m_state (state_type::UNINITIALIZED),
      m_delete (false), m_change_vtype_only (false), m_read_vl_insn (nullptr),
      m_vl_used_by_non_rvv_insn (false), m_ime_required (false), m_ime_req (),
      m_ime_demand (ime_demand_type::ignore_ime)
  {}

  vsetvl_info (insn_info *insn) : vsetvl_info () { parse_insn (insn); }

  vsetvl_info (rtx_insn *insn) : vsetvl_info () { parse_insn (insn); }

  void set_avl (rtx avl) { m_avl = avl; }
  void set_vl (rtx vl) { m_vl = vl; }
  void set_avl_def (set_info *avl_def) { m_avl_def = avl_def; }
  void set_sew (uint8_t sew) { m_sew = sew; }
  void set_vlmul (vlmul_type vlmul) { m_vlmul = vlmul; }
  void set_ratio (uint8_t ratio) { m_ratio = ratio; }
  void set_ta (bool ta) { m_ta = ta; }
  void set_ma (bool ma) { m_ma = ma; }
  void set_delete () { m_delete = true; }
  void set_bb (bb_info *bb) { m_bb = bb; }
  void set_max_sew (uint8_t max_sew) { m_max_sew = max_sew; }
  void set_change_vtype_only () { m_change_vtype_only = true; }
  void set_read_vl_insn (insn_info *insn) { m_read_vl_insn = insn; }

  rtx get_avl () const { return m_avl; }
  rtx get_vl () const { return m_vl; }
  set_info *get_avl_def () const { return m_avl_def; }
  uint8_t get_sew () const { return m_sew; }
  vlmul_type get_vlmul () const { return m_vlmul; }
  uint8_t get_ratio () const { return m_ratio; }
  bool get_ta () const { return m_ta; }
  bool get_ma () const { return m_ma; }
  insn_info *get_insn () const { return m_insn; }
  bool delete_p () const { return m_delete; }
  bb_info *get_bb () const { return m_bb; }
  uint8_t get_max_sew () const { return m_max_sew; }
  insn_info *get_read_vl_insn () const { return m_read_vl_insn; }
  bool vl_used_by_non_rvv_insn_p () const { return m_vl_used_by_non_rvv_insn; }

  bool ime_required_p () const { return m_ime_required; }
  const ime_vtype_req &get_ime_req () const { return m_ime_req; }
  void set_ime_required (bool r) { m_ime_required = r; }
  void set_ime_req (const ime_vtype_req &r) { m_ime_req = r; }
  ime_demand_type get_ime_demand () const { return m_ime_demand; }
  void set_ime_demand (ime_demand_type demand) { m_ime_demand = demand; }

  bool has_imm_avl () const { return m_avl && CONST_INT_P (m_avl); }
  bool has_vlmax_avl () const { return vlmax_avl_p (m_avl); }
  bool has_nonvlmax_reg_avl () const
  {
    return m_avl && REG_P (m_avl) && !has_vlmax_avl ();
  }
  bool has_non_zero_avl () const
  {
    if (has_imm_avl ())
      return INTVAL (m_avl) > 0;
    return has_vlmax_avl ();
  }
  bool has_vl () const
  {
    /* The VL operand can only be either a NULL_RTX or a register.   */
    gcc_assert (!m_vl || REG_P (m_vl));
    return m_vl != NULL_RTX;
  }
  bool has_same_ratio (const vsetvl_info &other) const
  {
    return get_ratio () == other.get_ratio ();
  }

  /* The block of INSN isn't always same as the block of the VSETVL_INFO,
      meaning we may have 'get_insn ()->bb () != get_bb ()'.

	E.g.  BB 2 (Empty) ---> BB 3 (VALID, has rvv insn 1)

     BB 2 has empty VSETVL_INFO, wheras BB 3 has VSETVL_INFO that satisfies
     get_insn ()->bb () == get_bb (). In earliest fusion, we may fuse bb 3 and
     bb 2 so that the 'get_bb ()' of BB2 VSETVL_INFO will be BB2 wheras the
     'get_insn ()' of BB2 VSETVL INFO will be the rvv insn 1 (which is located
     at BB3).  */
  bool insn_inside_bb_p () const { return get_insn ()->bb () == get_bb (); }
  void update_avl (const vsetvl_info &other)
  {
    m_avl = other.get_avl ();
    m_vl = other.get_vl ();
    m_avl_def = other.get_avl_def ();
  }

  bool uninit_p () const { return m_state == state_type::UNINITIALIZED; }
  bool valid_p () const { return m_state == state_type::VALID; }
  bool unknown_p () const { return m_state == state_type::UNKNOWN; }
  bool empty_p () const { return m_state == state_type::EMPTY; }
  bool change_vtype_only_p () const { return m_change_vtype_only
					     && !TARGET_XTHEADVECTOR; }

  void set_valid () { m_state = state_type::VALID; }
  void set_unknown () { m_state = state_type::UNKNOWN; }
  void set_empty () { m_state = state_type::EMPTY; }

  void set_sew_lmul_demand (sew_lmul_demand_type demand)
  {
    m_sew_lmul_demand = demand;
  }
  void set_policy_demand (policy_demand_type demand)
  {
    m_policy_demand = demand;
  }
  void set_avl_demand (avl_demand_type demand) { m_avl_demand = demand; }

  sew_lmul_demand_type get_sew_lmul_demand () const
  {
    return m_sew_lmul_demand;
  }
  policy_demand_type get_policy_demand () const { return m_policy_demand; }
  avl_demand_type get_avl_demand () const { return m_avl_demand; }

  void normalize_demand (unsigned demand_flags)
  {
    switch (demand_flags
	    & (DEMAND_SEW_P | DEMAND_LMUL_P | DEMAND_RATIO_P | DEMAND_GE_SEW_P))
      {
      case (unsigned) sew_lmul_demand_type::sew_lmul:
	m_sew_lmul_demand = sew_lmul_demand_type::sew_lmul;
	break;
      case (unsigned) sew_lmul_demand_type::ratio_only:
	m_sew_lmul_demand = sew_lmul_demand_type::ratio_only;
	break;
      case (unsigned) sew_lmul_demand_type::sew_only:
	m_sew_lmul_demand = sew_lmul_demand_type::sew_only;
	break;
      case (unsigned) sew_lmul_demand_type::ge_sew:
	m_sew_lmul_demand = sew_lmul_demand_type::ge_sew;
	break;
      case (unsigned) sew_lmul_demand_type::ratio_and_ge_sew:
	m_sew_lmul_demand = sew_lmul_demand_type::ratio_and_ge_sew;
	break;
      default:
	gcc_unreachable ();
      }

    switch (demand_flags & (DEMAND_TAIL_POLICY_P | DEMAND_MASK_POLICY_P))
      {
      case (unsigned) policy_demand_type::tail_mask_policy:
	m_policy_demand = policy_demand_type::tail_mask_policy;
	break;
      case (unsigned) policy_demand_type::tail_policy_only:
	m_policy_demand = policy_demand_type::tail_policy_only;
	break;
      case (unsigned) policy_demand_type::mask_policy_only:
	m_policy_demand = policy_demand_type::mask_policy_only;
	break;
      case (unsigned) policy_demand_type::ignore_policy:
	m_policy_demand = policy_demand_type::ignore_policy;
	break;
      default:
	gcc_unreachable ();
      }

    switch (demand_flags & (DEMAND_AVL_P | DEMAND_NON_ZERO_AVL_P))
      {
      case (unsigned) avl_demand_type::avl:
	m_avl_demand = avl_demand_type::avl;
	break;
      case (unsigned) avl_demand_type::non_zero_avl:
	m_avl_demand = avl_demand_type::non_zero_avl;
	break;
      case (unsigned) avl_demand_type::ignore_avl:
	m_avl_demand = avl_demand_type::ignore_avl;
	break;
      default:
	gcc_unreachable ();
      }

    m_ime_demand = (demand_flags & DEMAND_IME_P)
		     ? ime_demand_type::ime_vtype
		     : ime_demand_type::ignore_ime;
  }

  void parse_insn (rtx_insn *rinsn)
  {
    if (!NONDEBUG_INSN_P (rinsn))
      return;
    if (optimize == 0 && !has_vtype_op (rinsn))
      return;
    gcc_assert (!vsetvl_discard_result_insn_p (rinsn));
    set_valid ();
    extract_insn_cached (rinsn);
    m_avl = ::get_avl (rinsn);
    if (has_vlmax_avl () || vsetvl_insn_p (rinsn))
      m_vl = ::get_vl (rinsn);
    m_sew = ::get_sew (rinsn);
    m_vlmul = ::get_vlmul (rinsn);
    m_ta = tail_agnostic_p (rinsn);
    m_ma = mask_agnostic_p (rinsn);
    if (get_attr_requires_ime_vtype (rinsn) == REQUIRES_IME_VTYPE_YES)
      {
	m_ime_required = true;
	m_ime_req = riscv_extract_ime_requirements (rinsn);
	m_ime_demand = ime_demand_type::ime_vtype;
      }
  }

  void parse_insn (insn_info *insn)
  {
    /* The VL dest of the insn */
    rtx dest_vl = NULL_RTX;

    m_insn = insn;
    m_bb = insn->bb ();
    /* Return if it is debug insn for the consistency with optimize == 0.  */
    if (insn->is_debug_insn ())
      return;

    /* We set it as unknown since we don't what will happen in CALL or ASM.  */
    if (insn->is_call () || insn->is_asm ())
      {
	set_unknown ();
	return;
      }

    /* If this is something that updates VL/VTYPE that we don't know about, set
       the state to unknown.  */
    if (!vector_config_insn_p (insn->rtl ()) && !has_vtype_op (insn->rtl ())
	&& (find_access (insn->defs (), VL_REGNUM)
	    || find_access (insn->defs (), VTYPE_REGNUM)))
      {
	set_unknown ();
	return;
      }

    if (!vector_config_insn_p (insn->rtl ()) && !has_vtype_op (insn->rtl ()))
      /* uninitialized */
      return;

    set_valid ();

    m_avl = ::get_avl (insn->rtl ());
    if (m_avl)
      {
	if (vsetvl_insn_p (insn->rtl ()) || has_vlmax_avl ())
	  {
	    m_vl = ::get_vl (insn->rtl ());
	    dest_vl = m_vl;
	  }

	if (has_nonvlmax_reg_avl ())
	  m_avl_def = find_access (insn->uses (), REGNO (m_avl))->def ();
      }

    m_sew = ::get_sew (insn->rtl ());
    m_vlmul = ::get_vlmul (insn->rtl ());
    m_ratio = get_attr_ratio (insn->rtl ());
    /* when get_attr_ratio is invalid, this kind of instructions
       doesn't care about ratio. However, we still need this value
       in demand info backward analysis.  */
    if (m_ratio == INVALID_ATTRIBUTE)
      m_ratio = calculate_ratio (m_sew, m_vlmul);
    m_ta = tail_agnostic_p (insn->rtl ());
    m_ma = mask_agnostic_p (insn->rtl ());

    if (get_attr_requires_ime_vtype (insn->rtl ()) == REQUIRES_IME_VTYPE_YES)
      {
	m_ime_required = true;
	m_ime_req = riscv_extract_ime_requirements (insn->rtl ());
	/* For IME mmacc instructions, the vtype.LMUL field encodes the
	   SOURCE register-group size (vs1/vs2), not the accumulator's
	   MUL_C.  get_vlmul() read the accumulator's mode (operand 0),
	   which produces the wrong LMUL for mmacc (e.g. m2 for
	   vfmmacc_vv_f32m2 with m1 sources).  Override with the source
	   LMUL so demand-system sew/lmul fusion lines up with what the
	   emitted vsetvl actually writes.  */
	m_vlmul = (vlmul_type) m_ime_req.lmul;
	/* Re-derive m_ratio from the corrected VLMUL.  */
	m_ratio = calculate_ratio (m_sew, m_vlmul);
      }

    /* If merge operand is undef value, we prefer agnostic.  */
    int merge_op_idx = get_attr_merge_op_idx (insn->rtl ());
    if (merge_op_idx != INVALID_ATTRIBUTE
	&& satisfies_constraint_vu (recog_data.operand[merge_op_idx]))
      {
	m_ta = true;
	m_ma = true;
      }

    /* Determine the demand info of the RVV insn.  */
    m_max_sew = get_max_int_sew ();
    unsigned dflags = 0;
    if (vector_config_insn_p (insn->rtl ()))
      {
	dflags |= demand_flags::DEMAND_AVL_P;
	dflags |= demand_flags::DEMAND_RATIO_P;
      }
    else
      {
	if (has_vl_op (insn->rtl ()))
	  {
	    if (scalar_move_insn_p (insn->rtl ()))
	      {
		/* If the avl for vmv.s.x comes from the vsetvl instruction, we
		   don't know if the avl is non-zero, so it is set to
		   DEMAND_AVL_P for now. it may be corrected to
		   DEMAND_NON_ZERO_AVL_P later when more information is
		   available.
		 */
		if (has_non_zero_avl ())
		  dflags |= demand_flags::DEMAND_NON_ZERO_AVL_P;
		else
		  dflags |= demand_flags::DEMAND_AVL_P;
	      }
	    else
	      dflags |= demand_flags::DEMAND_AVL_P;
	  }

	if (get_attr_ratio (insn->rtl ()) != INVALID_ATTRIBUTE)
	  dflags |= demand_flags::DEMAND_RATIO_P;
	else
	  {
	    if (scalar_move_insn_p (insn->rtl ()) && m_ta)
	      {
		dflags |= demand_flags::DEMAND_GE_SEW_P;
		m_max_sew = get_attr_type (insn->rtl ()) == TYPE_VFMOVFV
			      ? get_max_float_sew ()
			      : get_max_int_sew ();
	      }
	    else
	      dflags |= demand_flags::DEMAND_SEW_P;

	    if (!ignore_vlmul_insn_p (insn->rtl ()))
	      dflags |= demand_flags::DEMAND_LMUL_P;
	  }

	/* Demand policy for agnostic if the uarch has a preference.  */
	if (!m_ta || riscv_prefer_agnostic_p ())
	  dflags |= demand_flags::DEMAND_TAIL_POLICY_P;
	if (!m_ma || riscv_prefer_agnostic_p ())
	  dflags |= demand_flags::DEMAND_MASK_POLICY_P;
      }

    if (m_ime_required)
      dflags |= demand_flags::DEMAND_IME_P;

    normalize_demand (dflags);

    /* Optimize AVL from the vsetvl instruction.  */
    insn_info *def_insn = extract_single_source (get_avl_def ());
    if (def_insn && vsetvl_insn_p (def_insn->rtl ()))
      {
	vsetvl_info def_info = vsetvl_info (def_insn);
	if ((scalar_move_insn_p (insn->rtl ())
	     || def_info.get_ratio () == get_ratio ())
	    && (def_info.has_vlmax_avl () || def_info.has_imm_avl ()))
	  {
	    update_avl (def_info);
	    if (scalar_move_insn_p (insn->rtl ()) && has_non_zero_avl ())
	      m_avl_demand = avl_demand_type::non_zero_avl;
	  }
      }

    /* Determine if dest operand(vl) has been used by non-RVV instructions.  */
    if (dest_vl)
      m_vl_used_by_non_rvv_insn = reg_used (get_insn (), dest_vl);

    /* Collect the read vl insn for the fault-only-first rvv loads.  */
    if (fault_first_load_p (insn->rtl ()))
      {
	for (insn_info *i = insn->next_nondebug_insn ();
	     i && i->bb () == insn->bb (); i = i->next_nondebug_insn ())
	  {
	    if (find_access (i->defs (), VL_REGNUM))
	      break;
	    if (i->rtl () && read_vl_insn_p (i->rtl ()))
	      {
		m_read_vl_insn = i;
		break;
	      }
	  }
	/* If no csrr found but this is a _set_vl style fault-only-first
	   load, use the insn itself as the VL source.
	   If we have two identical vector configs that just differ in
	   AVL and the AVL is just "modified" by a read_vl we
	   can consider them equal and elide the second one.  */
	if (!m_read_vl_insn && get_fof_set_vl_reg (insn->rtl ()))
	  m_read_vl_insn = insn;
      }
  }

  /* Returns the corresponding vsetvl rtx pat.  */
  rtx get_vsetvl_pat (bool ignore_vl = false) const
  {
    rtx avl = get_avl ();
    /* if optimization == 0 and the instruction is vmv.x.s/vfmv.f.s,
       set the value of avl to (const_int 0) so that VSETVL PASS will
       insert vsetvl correctly.*/
    if (!get_avl ())
      avl = GEN_INT (0);
    rtx sew = gen_int_mode (get_sew (), Pmode);
    rtx vlmul = gen_int_mode (get_vlmul (), Pmode);
    rtx ta = gen_int_mode (get_ta (), Pmode);
    rtx ma = gen_int_mode (get_ma (), Pmode);

    if (change_vtype_only_p ())
      return gen_vsetvl_vtype_change_only (sew, vlmul, ta, ma);
    else if (has_vl () && !ignore_vl)
      return gen_vsetvl (Pmode, get_vl (), avl, sew, vlmul, ta, ma);
    else
      return gen_vsetvl_discard_result (Pmode, avl, sew, vlmul, ta, ma);
  }

  /* Return true that the non-AVL operands of THIS will be modified
     if we fuse the VL modification from OTHER into THIS.  */
  bool vl_modify_non_avl_op_p (const vsetvl_info &other) const
  {
    /* We don't need to worry about any operands from THIS be
       modified by OTHER vsetvl since we OTHER vsetvl doesn't
       modify any operand.  */
    if (!other.has_vl ())
      return false;

    /* THIS VL operand always preempt OTHER VL operand.  */
    if (this->has_vl ())
      return false;

    /* If THIS has non IMM AVL and THIS is AVL compatible with
       OTHER, the AVL value of THIS is same as VL value of OTHER.  */
    if (!this->has_imm_avl ())
      return false;
    return find_access (this->get_insn ()->uses (), REGNO (other.get_vl ()));
  }

  bool operator== (const vsetvl_info &other) const
  {
    gcc_assert (!uninit_p () && !other.uninit_p ()
		&& "Uninitialization should not happen");

    if (empty_p ())
      return other.empty_p ();
    if (unknown_p ())
      return other.unknown_p ();

    return get_insn () == other.get_insn () && get_bb () == other.get_bb ()
	   && get_avl () == other.get_avl () && get_vl () == other.get_vl ()
	   && get_avl_def () == other.get_avl_def ()
	   && get_sew () == other.get_sew ()
	   && get_vlmul () == other.get_vlmul () && get_ta () == other.get_ta ()
	   && get_ma () == other.get_ma ()
	   && get_avl_demand () == other.get_avl_demand ()
	   && get_sew_lmul_demand () == other.get_sew_lmul_demand ()
	   && get_policy_demand () == other.get_policy_demand ()
	   && get_ime_demand () == other.get_ime_demand ()
	   && ime_required_p () == other.ime_required_p ()
	   && (!ime_required_p () || get_ime_req () == other.get_ime_req ());
  }

  void dump (FILE *file, const char *indent = "") const
  {
    if (uninit_p ())
      {
	fprintf (file, "UNINITIALIZED.\n");
	return;
      }
    else if (unknown_p ())
      {
	fprintf (file, "UNKNOWN.\n");
	return;
      }
    else if (empty_p ())
      {
	fprintf (file, "EMPTY.\n");
	return;
      }
    else if (valid_p ())
      fprintf (file, "VALID (insn %u, bb %u)%s\n", get_insn ()->uid (),
	       get_bb ()->index (), delete_p () ? " (deleted)" : "");
    else
      gcc_unreachable ();

    fprintf (file, "%sDemand fields:", indent);
    if (m_sew_lmul_demand == sew_lmul_demand_type::sew_lmul)
      fprintf (file, " demand_sew_lmul");
    else if (m_sew_lmul_demand == sew_lmul_demand_type::ratio_only)
      fprintf (file, " demand_ratio_only");
    else if (m_sew_lmul_demand == sew_lmul_demand_type::sew_only)
      fprintf (file, " demand_sew_only");
    else if (m_sew_lmul_demand == sew_lmul_demand_type::ge_sew)
      fprintf (file, " demand_ge_sew");
    else if (m_sew_lmul_demand == sew_lmul_demand_type::ratio_and_ge_sew)
      fprintf (file, " demand_ratio_and_ge_sew");

    if (m_policy_demand == policy_demand_type::tail_mask_policy)
      fprintf (file, " demand_tail_mask_policy");
    else if (m_policy_demand == policy_demand_type::tail_policy_only)
      fprintf (file, " demand_tail_policy_only");
    else if (m_policy_demand == policy_demand_type::mask_policy_only)
      fprintf (file, " demand_mask_policy_only");

    if (m_avl_demand == avl_demand_type::avl)
      fprintf (file, " demand_avl");
    else if (m_avl_demand == avl_demand_type::non_zero_avl)
      fprintf (file, " demand_non_zero_avl");

    if (m_ime_demand == ime_demand_type::ime_vtype)
      fprintf (file, " demand_ime_vtype");
    fprintf (file, "\n");

    fprintf (file, "%sSEW=%d, ", indent, get_sew ());
    fprintf (file, "VLMUL=%s, ", vlmul_to_str (get_vlmul ()));
    fprintf (file, "RATIO=%d, ", get_ratio ());
    fprintf (file, "MAX_SEW=%d\n", get_max_sew ());

    fprintf (file, "%sTAIL_POLICY=%s, ", indent, policy_to_str (get_ta ()));
    fprintf (file, "MASK_POLICY=%s\n", policy_to_str (get_ma ()));

    fprintf (file, "%sAVL=", indent);
    print_rtl_single (file, get_avl ());
    fprintf (file, "%sVL=", indent);
    print_rtl_single (file, get_vl ());
    if (change_vtype_only_p ())
      fprintf (file, "%schange vtype only\n", indent);
    if (get_read_vl_insn ())
      fprintf (file, "%sread_vl_insn: insn %u\n", indent,
	       get_read_vl_insn ()->uid ());
    if (vl_used_by_non_rvv_insn_p ())
      fprintf (file, "%suse_by_non_rvv_insn=true\n", indent);
    if (ime_required_p ())
      {
	const ime_vtype_req &r = get_ime_req ();
	fprintf (file,
		 "%sIME: sew=%u mul_c=%s lmul=%u W=%u altfmt_a=%d altfmt_b=%d"
		 " altfmt=%d bs=%d lambda_log2=%d wildcard=%d\n",
		 indent, r.sew, vlmul_to_str (r.mul_c), r.lmul, r.widen_w,
		 r.altfmt_a, r.altfmt_b, r.altfmt, r.bs, (int) r.lambda_log2,
		 r.lambda_wildcard);
      }
  }
};

/* Encode the vsew field (bits 3..5 of vtype) from an element width in
   bits.  SEW_BITS must be one of {8, 16, 32, 64}.  */

static unsigned
ime_vsew_encoding (int sew_bits)
{
  /* e8 -> 000, e16 -> 001, e32 -> 010, e64 -> 011.  */
  gcc_assert (sew_bits >= 8 && (sew_bits & (sew_bits - 1)) == 0);
  return exact_log2 (sew_bits) - 3;
}

/* Compose the compile-time-known portion of the full vtype value:
   vlmul | vsew | vta | vma | altfmt | bs | altfmt_B | altfmt_A
   (i.e. everything except the lambda field at bits XLEN-4..XLEN-2,
   which may need runtime computation).  */

static HOST_WIDE_INT
compute_static_vtype_without_lambda (const vsetvl_info &info, int xlen)
{
  HOST_WIDE_INT val = 0;
  const ime_vtype_req &r = info.get_ime_req ();

  /* The vtype LMUL field encodes the source register-group size for IME
     mmacc instructions (i.e. the vs1/vs2 LMUL), NOT the accumulator's
     MUL_C.  MUL_C is derived in hardware from VLEN/(SEW*lambda^2) and
     is independent of LMUL -- see IME spec section "Integer matrix
     multiply-accumulate".  For an intrinsic like
     __riscv_vmmacc_vv_i32m2 with m1 sources (default LMUL=1), vtype
     must carry LMUL=m1 even though the accumulator type is m2.  For
     non-IME and for tile load/store, info.get_vlmul() already returns
     the correct LMUL (the data vector's LMUL).  */
  if (info.ime_required_p ())
    val |= (unsigned) r.lmul;
  else
    val |= (unsigned) info.get_vlmul ();
  val |= (HOST_WIDE_INT) ime_vsew_encoding (info.get_sew ()) << 3;
  if (info.get_ta ())
    val |= (HOST_WIDE_INT) 1 << 6;
  if (info.get_ma ())
    val |= (HOST_WIDE_INT) 1 << 7;

  if (r.altfmt)
    val |= (HOST_WIDE_INT) 1 << 8;
  if (r.bs)
    val |= (HOST_WIDE_INT) 1 << (xlen - 7);
  if (r.altfmt_b)
    val |= (HOST_WIDE_INT) 1 << (xlen - 6);
  if (r.altfmt_a)
    val |= (HOST_WIDE_INT) 1 << (xlen - 5);
  return val;
}

/* Convert MUL_C (vlmul_type) to the integer multiplier for the
   accumulator register group.  Returns 0 for fractional / reserved
   encodings (invalid for IME).  */

static int
ime_mul_c_int (vlmul_type m)
{
  switch (m)
    {
    case LMUL_1: return 1;
    case LMUL_2: return 2;
    case LMUL_4: return 4;
    case LMUL_8: return 8;
    default: return 0;
    }
}

/* Return a const_int rtx holding the full vtype value for an IME
   instruction when lambda is known at compile time.  Returns NULL_RTX
   if a runtime sequence is required (lambda_log2 == -1), in which
   case the caller must emit @vsetvl_ime_runtime<mode> instead.  Also
   returns NULL_RTX (after a diagnostic) when the requested
   (VLEN, SEW, MUL_C) combination is invalid.

   The vsetvl pass runs post-reload and cannot create fresh pseudos,
   so the IME setup pattern (@vsetvl_ime<mode>) takes a const_int
   vtype operand and synthesises the load in its own asm template.
   This helper's job is to compute that constant.  */

static rtx
emit_ime_vtype_build (const vsetvl_info &info)
{
  const ime_vtype_req &r = info.get_ime_req ();
  int xlen = GET_MODE_BITSIZE (Pmode);
  HOST_WIDE_INT static_val = compute_static_vtype_without_lambda (info, xlen);

  if (r.lambda_log2 == -2)
    {
      sorry ("IME intrinsic: the %<(VLEN, SEW, MUL_C)%> combination does "
	     "not yield a supported %<lambda%> value");
      return NULL_RTX;
    }

  if (r.lambda_log2 >= 0)
    {
      int lambda_val = 1 << r.lambda_log2;
      if (!riscv_ime_lambda_supported_p (lambda_val))
	{
	  error ("IME intrinsic requires %<lambda=%d%> which is not in the "
		 "set allowed by %<-mrvv-ime-lambda=%>", lambda_val);
	  return NULL_RTX;
	}
      /* Static lambda: the full vtype is a compile-time constant.  */
      int lambda_enc = r.lambda_log2 + 1;
      static_val |= (HOST_WIDE_INT) lambda_enc << (xlen - 4);
      return gen_int_mode (static_val, Pmode);
    }

  /* Runtime lambda path: caller dispatches to @vsetvl_ime_runtime.  */
  return NULL_RTX;
}

/* Build the runtime-lambda @vsetvl_ime_runtime<mode> instruction for
   INFO.  Returns the insn pattern (to be passed to emit_insn / etc.)
   or NULL_RTX if the runtime path cannot be emitted (a diagnostic has
   already been reported).  */

static rtx
emit_ime_vtype_runtime_pat (const vsetvl_info &info, rtx dest_vl, rtx avl)
{
  const ime_vtype_req &r = info.get_ime_req ();
  int xlen = GET_MODE_BITSIZE (Pmode);

  if (!TARGET_ZBB)
    {
      sorry ("IME intrinsic with runtime %<lambda%> requires the "
	     "%<Zbb%> extension; use %<-mrvv-vector-bits=zvl%> with a "
	     "%<zvl*b%> that makes VLEN compile-time known, or enable "
	     "%<Zbb%>");
      return NULL_RTX;
    }

  int mul_c_val = ime_mul_c_int (r.mul_c);
  if (mul_c_val == 0 || r.sew == 0)
    {
      sorry ("IME intrinsic: invalid %<(SEW, MUL_C)%> configuration "
	     "for runtime lambda");
      return NULL_RTX;
    }
  int shift = exact_log2 (r.sew * mul_c_val);
  if (shift < 0)
    {
      sorry ("IME intrinsic: %<SEW * MUL_C%> is not a power of two");
      return NULL_RTX;
    }

  HOST_WIDE_INT static_val = compute_static_vtype_without_lambda (info, xlen);
  rtx static_rtx = gen_int_mode (static_val, Pmode);
  rtx shift_rtx = GEN_INT (shift);

  /* The vsetvl pass runs post-reload; we cannot allocate fresh
     pseudos.  Use three caller-saved GP temporaries (t0/t1/t2) as
     explicit scratches.  The IME insn that follows this sequence is
     an RVV insn using v-registers (plus AVL, already passed in), so
     t0/t1/t2 are safe.  */
  rtx scratch1 = dest_vl;
  rtx scratch2 = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP2_REGNUM);
  rtx scratch3 = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP2_REGNUM + 1);

  return gen_vsetvl_ime_runtime (Pmode, scratch1, avl, static_rtx,
				 shift_rtx, scratch2, scratch3);
}

class vsetvl_block_info
{
public:
  /* The static execute probability of the demand info.  */
  profile_probability probability;

  auto_vec<vsetvl_info> local_infos;
  vsetvl_info global_info;
  bb_info *bb;

  vsetvl_block_info () : bb (nullptr)
  {
    local_infos.safe_grow_cleared (0);
    global_info.set_empty ();
  }
  vsetvl_block_info (const vsetvl_block_info &other)
    : probability (other.probability), local_infos (other.local_infos.copy ()),
      global_info (other.global_info), bb (other.bb)
  {}

  vsetvl_info &get_entry_info ()
  {
    gcc_assert (!empty_p ());
    return local_infos.is_empty () ? global_info : local_infos[0];
  }
  vsetvl_info &get_exit_info ()
  {
    gcc_assert (!empty_p ());
    return local_infos.is_empty () ? global_info
				   : local_infos[local_infos.length () - 1];
  }
  const vsetvl_info &get_entry_info () const
  {
    gcc_assert (!empty_p ());
    return local_infos.is_empty () ? global_info : local_infos[0];
  }
  const vsetvl_info &get_exit_info () const
  {
    gcc_assert (!empty_p ());
    return local_infos.is_empty () ? global_info
				   : local_infos[local_infos.length () - 1];
  }

  bool empty_p () const { return local_infos.is_empty () && !has_info (); }
  bool has_info () const { return !global_info.empty_p (); }
  void set_info (const vsetvl_info &info)
  {
    gcc_assert (local_infos.is_empty ());
    global_info = info;
    global_info.set_bb (bb);
  }
  void set_empty_info () { global_info.set_empty (); }
};

/* Same as REG_USED () but looks for a single use in an RVV insn's AVL
   operand.  */
static bool
reg_single_use_in_avl (insn_info *insn, rtx reg)
{
  if (!reg)
    return false;
  unsigned int regno = REGNO (reg);
  const hash_set<use_info *> vl_uses = get_all_real_uses (insn, regno);
  for (use_info *use : vl_uses)
    {
      gcc_assert (use->insn ()->is_real ());
      rtx_insn *rinsn = use->insn ()->rtl ();
      if (!has_vl_op (rinsn)
	  || count_regno_occurrences (rinsn, regno) != 1)
	return false;

      vsetvl_info info = vsetvl_info (use->insn ());

      if (!info.has_nonvlmax_reg_avl ())
	return false;

      rtx avl = info.get_avl ();
      if (avl && REG_P (avl) && regno == REGNO (avl))
	return true;
    }
  return false;
}

/* Demand system is the RVV-based VSETVL info analysis tools wrapper.
   It defines compatible rules for SEW/LMUL, POLICY and AVL.
   Also, it provides 3 interfaces available_p, compatible_p and
   merge for the VSETVL PASS analysis and optimization.

     - available_p: Determine whether the next info can get the
       available VSETVL status from previous info.
       e.g. bb 2 (demand SEW = 32, LMUL = M2) -> bb 3 (demand RATIO = 16).
       Since bb 2 demand info (SEW/LMUL = 32/2 = 16) satisfies the bb 3
       demand, the VSETVL instruction in bb 3 can be elided.
       available_p (previous, next) is true in such situation.
     - compatible_p: Determine whether prev_info is compatible with next_info
       so that we can have a new merged info that is available to both of them.
     - merge: Merge the stricter demand information from
       next_info into prev_info so that prev_info becomes available to
       next_info.  */
class demand_system
{
private:
  /* predictors.  */

  inline bool always_true (const vsetvl_info &prev ATTRIBUTE_UNUSED,
			   const vsetvl_info &next ATTRIBUTE_UNUSED)
  {
    return true;
  }
  inline bool always_false (const vsetvl_info &prev ATTRIBUTE_UNUSED,
			    const vsetvl_info &next ATTRIBUTE_UNUSED)
  {
    return false;
  }

  /* predictors for sew and lmul */

  inline bool lmul_eq_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    return prev.get_vlmul () == next.get_vlmul ();
  }
  inline bool sew_eq_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    return prev.get_sew () == next.get_sew ();
  }
  inline bool sew_lmul_eq_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    return lmul_eq_p (prev, next) && sew_eq_p (prev, next);
  }
  inline bool sew_ge_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    return prev.get_sew () == next.get_sew ()
	   || (next.get_ta () && prev.get_sew () > next.get_sew ());
  }
  inline bool sew_le_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    return prev.get_sew () == next.get_sew ()
	   || (prev.get_ta () && prev.get_sew () < next.get_sew ());
  }
  inline bool prev_sew_le_next_max_sew_p (const vsetvl_info &prev,
					  const vsetvl_info &next)
  {
    return prev.get_sew () <= next.get_max_sew ();
  }
  inline bool next_sew_le_prev_max_sew_p (const vsetvl_info &prev,
					  const vsetvl_info &next)
  {
    return next.get_sew () <= prev.get_max_sew ();
  }
  inline bool max_sew_overlap_p (const vsetvl_info &prev,
				 const vsetvl_info &next)
  {
    return !(prev.get_sew () > next.get_max_sew ()
	     || next.get_sew () > prev.get_max_sew ());
  }
  inline bool ratio_eq_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    return prev.has_same_ratio (next);
  }
  inline bool prev_ratio_valid_for_next_sew_p (const vsetvl_info &prev,
					       const vsetvl_info &next)
  {
    return prev.get_ratio () >= (next.get_sew () / MAX_LMUL);
  }
  inline bool next_ratio_valid_for_prev_sew_p (const vsetvl_info &prev,
					       const vsetvl_info &next)
  {
    return next.get_ratio () >= (prev.get_sew () / MAX_LMUL);
  }
  inline bool sew_ge_and_ratio_eq_p (const vsetvl_info &prev,
				     const vsetvl_info &next)
  {
    return sew_ge_p (prev, next) && ratio_eq_p (prev, next);
  }
  inline bool sew_ge_and_prev_sew_le_next_max_sew_p (const vsetvl_info &prev,
						     const vsetvl_info &next)
  {
    return sew_ge_p (prev, next) && prev_sew_le_next_max_sew_p (prev, next);
  }
  inline bool
  sew_ge_and_prev_sew_le_next_max_sew_and_next_ratio_valid_for_prev_sew_p (
    const vsetvl_info &prev, const vsetvl_info &next)
  {
    return sew_ge_p (prev, next) && prev_sew_le_next_max_sew_p (prev, next)
	   && next_ratio_valid_for_prev_sew_p (prev, next);
  }
  inline bool
  sew_ge_and_prev_sew_le_next_max_sew_and_ratio_eq_p (
    const vsetvl_info &prev, const vsetvl_info &next)
  {
    return sew_ge_p (prev, next) && prev_sew_le_next_max_sew_p (prev, next)
	   && ratio_eq_p (prev, next);
  }
  inline bool sew_le_and_next_sew_le_prev_max_sew_p (const vsetvl_info &prev,
						     const vsetvl_info &next)
  {
    return sew_le_p (prev, next) && next_sew_le_prev_max_sew_p (prev, next);
  }
  inline bool
  max_sew_overlap_and_next_ratio_valid_for_prev_sew_p (const vsetvl_info &prev,
						       const vsetvl_info &next)
  {
    if (next_ratio_valid_for_prev_sew_p (prev, next)
	&& max_sew_overlap_p (prev, next))
      {
	if (next.get_sew () < prev.get_sew ()
	    && (!next.get_ta () || !next.get_ma ()))
	  return false;
	return true;
      }
    return false;
  }
  inline bool
  sew_le_and_next_sew_le_prev_max_sew_and_ratio_eq_p (const vsetvl_info &prev,
						      const vsetvl_info &next)
  {
    return sew_le_p (prev, next) && ratio_eq_p (prev, next)
	   && next_sew_le_prev_max_sew_p (prev, next);
  }
  inline bool
  max_sew_overlap_and_prev_ratio_valid_for_next_sew_p (const vsetvl_info &prev,
						       const vsetvl_info &next)
  {
    return prev_ratio_valid_for_next_sew_p (prev, next)
	   && max_sew_overlap_p (prev, next);
  }
  inline bool
  sew_le_and_next_sew_le_prev_max_sew_and_prev_ratio_valid_for_next_sew_p (
    const vsetvl_info &prev, const vsetvl_info &next)
  {
    return sew_le_p (prev, next) && prev_ratio_valid_for_next_sew_p (prev, next)
	   && next_sew_le_prev_max_sew_p (prev, next);
  }
  inline bool max_sew_overlap_and_ratio_eq_p (const vsetvl_info &prev,
					      const vsetvl_info &next)
  {
    return ratio_eq_p (prev, next) && max_sew_overlap_p (prev, next);
  }

  /* predictors for tail and mask policy */

  inline bool tail_policy_eq_p (const vsetvl_info &prev,
				const vsetvl_info &next)
  {
    return prev.get_ta () == next.get_ta ();
  }
  inline bool mask_policy_eq_p (const vsetvl_info &prev,
				const vsetvl_info &next)
  {
    return prev.get_ma () == next.get_ma ();
  }
  inline bool tail_mask_policy_eq_p (const vsetvl_info &prev,
				     const vsetvl_info &next)
  {
    return tail_policy_eq_p (prev, next) && mask_policy_eq_p (prev, next);
  }

  /* predictors for avl */

  inline bool modify_or_use_vl_p (insn_info *i, const vsetvl_info &info)
  {
    if (info.has_vl ())
      {
	if (find_access (i->defs (), REGNO (info.get_vl ())))
	  return true;
	if (find_access (i->uses (), REGNO (info.get_vl ())))
	  {
	    resource_info resource = full_register (REGNO (info.get_vl ()));
	    def_lookup dl1 = crtl->ssa->find_def (resource, i);
	    def_lookup dl2 = crtl->ssa->find_def (resource, info.get_insn ());
	    if (dl1.matching_set () || dl2.matching_set ())
	      return true;
	    /* If their VLs are coming from same def, we still want to fuse
	       their VSETVL demand info to gain better performance.  */
	    return dl1.prev_def (i) != dl2.prev_def (i);
	  }
      }
    return false;
  }
  inline bool modify_avl_p (insn_info *i, const vsetvl_info &info)
  {
    return info.has_nonvlmax_reg_avl ()
	   && find_access (i->defs (), REGNO (info.get_avl ()));
  }

  inline bool modify_reg_between_p (insn_info *prev_insn, insn_info *curr_insn,
				    unsigned regno)
  {
    gcc_assert (prev_insn->compare_with (curr_insn) < 0);
    for (insn_info *i = curr_insn->prev_nondebug_insn (); i != prev_insn;
	 i = i->prev_nondebug_insn ())
      {
	// no def of regno
	if (find_access (i->defs (), regno))
	  return true;
      }
    return false;
  }

  inline bool reg_avl_equal_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    if (!prev.has_nonvlmax_reg_avl () || !next.has_nonvlmax_reg_avl ())
      return false;

    if (same_equiv_note_p (prev.get_avl_def (), next.get_avl_def ()))
      return true;

    if (REGNO (prev.get_avl ()) != REGNO (next.get_avl ()))
      return false;

    insn_info *prev_insn = prev.get_insn ();
    if (prev.get_bb () != prev_insn->bb ())
      prev_insn = prev.get_bb ()->end_insn ();

    insn_info *next_insn = next.get_insn ();
    if (next.get_bb () != next_insn->bb ())
      next_insn = next.get_bb ()->end_insn ();

    return avl_vl_unmodified_between_p (prev_insn, next_insn, next, false);
  }

  inline bool avl_equal_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());

    if (next.has_vl () && next.vl_used_by_non_rvv_insn_p ())
      return false;

    if (vector_config_insn_p (prev.get_insn ()->rtl ()) && next.get_avl_def ()
	&& next.get_avl_def ()->insn () == prev.get_insn ())
      return true;

    if (prev.get_read_vl_insn ())
      {
	if (!next.has_nonvlmax_reg_avl () || !next.get_avl_def ())
	  return false;
	insn_info *avl_def_insn = extract_single_source (next.get_avl_def ());
	return avl_def_insn == prev.get_read_vl_insn ();
      }

    if (prev == next && prev.has_nonvlmax_reg_avl ())
      {
	insn_info *insn = prev.get_insn ();
	bb_info *bb = insn->bb ();
	for (insn_info *i = insn; real_insn_and_same_bb_p (i, bb);
	     i = i->next_nondebug_insn ())
	  if (find_access (i->defs (), REGNO (prev.get_avl ())))
	    return false;
      }

    if (prev.has_vlmax_avl () && next.has_vlmax_avl ())
      return true;
    else if (prev.has_imm_avl () && next.has_imm_avl ())
      return INTVAL (prev.get_avl ()) == INTVAL (next.get_avl ());
    else if (prev.has_vl () && next.has_nonvlmax_reg_avl ()
	     && REGNO (prev.get_vl ()) == REGNO (next.get_avl ()))
      {
	insn_info *prev_insn = prev.insn_inside_bb_p ()
				 ? prev.get_insn ()
				 : prev.get_bb ()->end_insn ();

	insn_info *next_insn = next.insn_inside_bb_p ()
				 ? next.get_insn ()
				 : next.get_bb ()->end_insn ();
	return avl_vl_unmodified_between_p (prev_insn, next_insn, next, false);
      }
    else if (prev.has_nonvlmax_reg_avl () && next.has_nonvlmax_reg_avl ())
      return reg_avl_equal_p (prev, next);

    return false;
  }
  inline bool avl_equal_or_prev_avl_non_zero_p (const vsetvl_info &prev,
						const vsetvl_info &next)
  {
    return avl_equal_p (prev, next) || prev.has_non_zero_avl ();
  }

  inline bool can_use_next_avl_p (const vsetvl_info &prev,
				  const vsetvl_info &next)
  {
    /* Forbid the AVL/VL propagation if VL of NEXT is used
       by non-RVV instructions.  This is because:

	 bb 2:
	   PREV: scalar move (no AVL)
	 bb 3:
	   NEXT: vsetvl a5(VL), a4(AVL) ...
	   branch a5,zero

       Since user vsetvl instruction is no side effect instruction
       which should be placed in the correct and optimal location
       of the program by the previous PASS, it is unreasonable that
       VSETVL PASS tries to move it to another places if it used by
       non-RVV instructions.

       Note: We only forbid the cases that VL is used by the following
       non-RVV instructions which will cause issues.  We don't forbid
       other cases since it won't cause correctness issues and we still
       more demand info are fused backward.  The later LCM algorithm
       should know the optimal location of the vsetvl.  */
    if (next.has_vl () && next.vl_used_by_non_rvv_insn_p ())
      return false;

    if (!next.has_nonvlmax_reg_avl () && !next.has_vl ())
      return true;

    insn_info *prev_insn = prev.get_insn ();
    if (prev.get_bb () != prev_insn->bb ())
      prev_insn = prev.get_bb ()->end_insn ();

    insn_info *next_insn = next.get_insn ();
    if (next.get_bb () != next_insn->bb ())
      next_insn = next.get_bb ()->end_insn ();

    return avl_vl_unmodified_between_p (prev_insn, next_insn, next);
  }

  inline bool avl_equal_or_next_avl_non_zero_and_can_use_next_avl_p (
    const vsetvl_info &prev, const vsetvl_info &next)
  {
    return avl_equal_p (prev, next)
	   || (next.has_non_zero_avl () && can_use_next_avl_p (prev, next));
  }

  /* modifiers  */

  inline void nop (const vsetvl_info &prev ATTRIBUTE_UNUSED,
		   const vsetvl_info &next ATTRIBUTE_UNUSED)
  {}

  /* modifiers for sew and lmul */

  inline void use_min_of_max_sew (vsetvl_info &prev, const vsetvl_info &next)
  {
    prev.set_max_sew (MIN (prev.get_max_sew (), next.get_max_sew ()));
  }
  inline void use_next_sew (vsetvl_info &prev, const vsetvl_info &next)
  {
    prev.set_sew (next.get_sew ());
    use_min_of_max_sew (prev, next);
  }
  inline void use_max_sew (vsetvl_info &prev, const vsetvl_info &next)
  {
    bool prev_sew_larger = prev.get_sew () >= next.get_sew ();
    const vsetvl_info from = prev_sew_larger ? prev : next;
    prev.set_sew (from.get_sew ());
    prev.set_vlmul (from.get_vlmul ());
    prev.set_ratio (from.get_ratio ());
    use_min_of_max_sew (prev, next);
  }
  inline void use_next_sew_lmul (vsetvl_info &prev, const vsetvl_info &next)
  {
    use_next_sew (prev, next);
    prev.set_vlmul (next.get_vlmul ());
    prev.set_ratio (next.get_ratio ());
  }
  inline void use_next_sew_with_prev_ratio (vsetvl_info &prev,
					    const vsetvl_info &next)
  {
    use_next_sew (prev, next);
    prev.set_vlmul (calculate_vlmul (next.get_sew (), prev.get_ratio ()));
  }
  inline void modify_lmul_with_next_ratio (vsetvl_info &prev,
					   const vsetvl_info &next)
  {
    prev.set_vlmul (calculate_vlmul (prev.get_sew (), next.get_ratio ()));
    prev.set_ratio (next.get_ratio ());
  }

  inline void use_max_sew_and_lmul_with_next_ratio (vsetvl_info &prev,
						    const vsetvl_info &next)
  {
    int max_sew = MAX (prev.get_sew (), next.get_sew ());
    prev.set_vlmul (calculate_vlmul (max_sew, next.get_ratio ()));
    use_max_sew (prev, next);
    prev.set_ratio (next.get_ratio ());
  }

  inline void use_max_sew_and_lmul_with_prev_ratio (vsetvl_info &prev,
						    const vsetvl_info &next)
  {
    int max_sew = MAX (prev.get_sew (), next.get_sew ());
    prev.set_vlmul (calculate_vlmul (max_sew, prev.get_ratio ()));
    prev.set_sew (max_sew);
  }

  /* modifiers for tail and mask policy */

  inline void use_tail_policy (vsetvl_info &prev, const vsetvl_info &next)
  {
    if (!next.get_ta ())
      prev.set_ta (next.get_ta ());
  }
  inline void use_mask_policy (vsetvl_info &prev, const vsetvl_info &next)
  {
    if (!next.get_ma ())
      prev.set_ma (next.get_ma ());
  }
  inline void use_tail_mask_policy (vsetvl_info &prev, const vsetvl_info &next)
  {
    use_tail_policy (prev, next);
    use_mask_policy (prev, next);
  }

  /* modifiers for avl */

  inline void use_next_avl (vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (can_use_next_avl_p (prev, next));
    prev.update_avl (next);
  }

  inline void use_next_avl_when_not_equal (vsetvl_info &prev,
					   const vsetvl_info &next)
  {
    if (avl_equal_p (prev, next))
      return;
    gcc_assert (next.has_non_zero_avl ());
    use_next_avl (prev, next);
  }

  /* predictors for IME vtype dimension */

  inline bool ime_eq_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    /* Only called via the (ime_vtype, ime_vtype) rule, where both sides
       have m_ime_required=true and a populated m_ime_req.

       A non-_L tile load/store carries lambda_wildcard=true: its own
       lambda-select field is 0b000 ("fall back to vtype.lambda").  The
       tile still reads vtype.sew, vtype.lmul, vtype.ta / vtype.ma from
       the CSR, but its lambda encoding is a don't-care from the
       tile's perspective.  So when exactly one side is a wildcard, we
       ignore MUL_C (which only affects the wildcard's standalone
       fallback-lambda computation, not the emitted vtype) and lambda
       (since the wildcard accepts any lambda from the other side).
       When both sides are wildcards -- or both specific -- we require
       an exact match so semantically distinct tile demands
       (e.g. different SEW) still get separate vsetvls.  */
    const ime_vtype_req &p = prev.get_ime_req ();
    const ime_vtype_req &n = next.get_ime_req ();
    bool common_eq
      = p.sew == n.sew
	&& p.lmul == n.lmul
	&& p.widen_w == n.widen_w
	&& p.altfmt_a == n.altfmt_a
	&& p.altfmt_b == n.altfmt_b
	&& p.altfmt == n.altfmt
	&& p.bs == n.bs;
    if (!common_eq)
      return false;
    if (p.lambda_wildcard != n.lambda_wildcard)
      /* Exactly one side is a wildcard: compatible regardless of the
	 other's specific lambda / mul_c.  */
      return true;
    /* Both specific, or both wildcard: require identical lambda
       and mul_c.  */
    return p.mul_c == n.mul_c && p.lambda_log2 == n.lambda_log2;
  }

  /* modifier for IME vtype dimension: copy next's IME state into prev.
     If one side carries a specific lambda and the other is a wildcard,
     the specific lambda wins so the emitted vsetvl pins it.  */

  inline void use_next_ime (vsetvl_info &prev, const vsetvl_info &next)
  {
    const ime_vtype_req &p = prev.get_ime_req ();
    const ime_vtype_req &n = next.get_ime_req ();
    prev.set_ime_required (next.ime_required_p ());
    /* Prev is specific, next is wildcard: keep prev's lambda_log2
       but adopt next's other metadata (they are geometry-equal per
       ime_eq_p).  */
    if (!p.lambda_wildcard && n.lambda_wildcard)
      {
	ime_vtype_req merged = n;
	merged.lambda_wildcard = false;
	merged.lambda_log2 = p.lambda_log2;
	prev.set_ime_req (merged);
	return;
      }
    prev.set_ime_req (n);
  }

public:
  /* Can we move vsetvl info between prev_insn and next_insn safe? */
  bool avl_vl_unmodified_between_p (insn_info *prev_insn, insn_info *next_insn,
				    const vsetvl_info &info,
				    bool ignore_vl = false)
  {
    gcc_assert ((ignore_vl && info.has_nonvlmax_reg_avl ())
		|| (info.has_nonvlmax_reg_avl () || info.has_vl ()));

    gcc_assert (!prev_insn->is_debug_insn () && !next_insn->is_debug_insn ());
    if (prev_insn->bb () == next_insn->bb ()
	&& prev_insn->compare_with (next_insn) < 0)
      {
	for (insn_info *i = next_insn->prev_nondebug_insn (); i != prev_insn;
	     i = i->prev_nondebug_insn ())
	  {
	    // no def and use of vl
	    if (!ignore_vl && modify_or_use_vl_p (i, info))
	      return false;

	    // no def of avl
	    if (modify_avl_p (i, info))
	      return false;
	  }
	return true;
      }
    else
      {
	basic_block prev_cfg_bb = prev_insn->bb ()->cfg_bb ();
	if (!ignore_vl && info.has_vl ())
	  {
	    bitmap live_out = df_get_live_out (prev_cfg_bb);
	    if (bitmap_bit_p (live_out, REGNO (info.get_vl ())))
	      return false;
	  }

	/* Find set_info at location of PREV_INSN and NEXT_INSN, Return
	   false if those 2 set_info are different.

	     PREV_INSN --- multiple nested blocks --- NEXT_INSN.

	   Return false if there is any modifications of AVL inside those
	   multiple nested blocks.  */
	if (info.has_nonvlmax_reg_avl ())
	  {
	    resource_info resource = full_register (REGNO (info.get_avl ()));
	    def_lookup dl1 = crtl->ssa->find_def (resource, prev_insn);
	    def_lookup dl2 = crtl->ssa->find_def (resource, next_insn);
	    if (dl2.matching_set ())
	      return false;

	    auto is_phi_or_real
	      = [&] (insn_info *h) { return h->is_real () || h->is_phi (); };

	    def_info *def1 = dl1.matching_set_or_last_def_of_prev_group ();
	    def_info *def2 = dl2.prev_def (next_insn);
	    set_info *set1 = safe_dyn_cast<set_info *> (def1);
	    set_info *set2 = safe_dyn_cast<set_info *> (def2);
	    if (!set1 || !set2)
	      return false;

	    auto is_same_ultimate_def = [&] (set_info *s1, set_info *s2) {
	      return s1->insn ()->is_phi () && s2->insn ()->is_phi ()
		     && look_through_degenerate_phi (s1)
			  == look_through_degenerate_phi (s2);
	    };

	    if (set1 != set2 && !is_same_ultimate_def (set1, set2))
	      {
		if (!is_phi_or_real (set1->insn ())
		    || !is_phi_or_real (set2->insn ()))
		  return false;

		if (set1->insn ()->is_real () && set2->insn ()->is_phi ())
		  {
		    hash_set<set_info *> sets
		      = get_all_sets (set2, true, false, true);
		    if (!sets.contains (set1))
		      return false;
		  }
		else
		  {
		    insn_info *def_insn1 = extract_single_source (set1);
		    insn_info *def_insn2 = extract_single_source (set2);
		    if (!def_insn1 || !def_insn2 || def_insn1 != def_insn2)
		      return false;
		  }
	      }
	  }

	for (insn_info *i = next_insn; i != next_insn->bb ()->head_insn ();
	     i = i->prev_nondebug_insn ())
	  {
	    // no def and use of vl
	    if (!ignore_vl && modify_or_use_vl_p (i, info))
	      return false;

	    // no def of avl
	    if (modify_avl_p (i, info))
	      return false;
	  }

	for (insn_info *i = prev_insn->bb ()->end_insn (); i != prev_insn;
	     i = i->prev_nondebug_insn ())
	  {
	    // no def mad use of vl
	    if (!ignore_vl && modify_or_use_vl_p (i, info))
	      return false;

	    // no def of avl
	    if (modify_avl_p (i, info))
	      return false;
	  }
      }
    return true;
  }

  bool sew_lmul_compatible_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    sew_lmul_demand_type prev_flags = prev.get_sew_lmul_demand ();
    sew_lmul_demand_type next_flags = next.get_sew_lmul_demand ();
#define DEF_SEW_LMUL_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,     \
			  AVAILABLE_P, FUSE)                                   \
  if (prev_flags == sew_lmul_demand_type::PREV_FLAGS                           \
      && next_flags == sew_lmul_demand_type::NEXT_FLAGS)                       \
    return COMPATIBLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool sew_lmul_available_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    sew_lmul_demand_type prev_flags = prev.get_sew_lmul_demand ();
    sew_lmul_demand_type next_flags = next.get_sew_lmul_demand ();
#define DEF_SEW_LMUL_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,     \
			  AVAILABLE_P, FUSE)                                   \
  if (prev_flags == sew_lmul_demand_type::PREV_FLAGS                           \
      && next_flags == sew_lmul_demand_type::NEXT_FLAGS)                       \
    return AVAILABLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  void merge_sew_lmul (vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    sew_lmul_demand_type prev_flags = prev.get_sew_lmul_demand ();
    sew_lmul_demand_type next_flags = next.get_sew_lmul_demand ();
#define DEF_SEW_LMUL_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,     \
			  AVAILABLE_P, FUSE)                                   \
  if (prev_flags == sew_lmul_demand_type::PREV_FLAGS                           \
      && next_flags == sew_lmul_demand_type::NEXT_FLAGS)                       \
    {                                                                          \
      gcc_assert (COMPATIBLE_P (prev, next));                                  \
      FUSE (prev, next);                                                       \
      prev.set_sew_lmul_demand (sew_lmul_demand_type::NEW_FLAGS);              \
      return;                                                                  \
    }

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool policy_compatible_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    policy_demand_type prev_flags = prev.get_policy_demand ();
    policy_demand_type next_flags = next.get_policy_demand ();
#define DEF_POLICY_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,       \
			AVAILABLE_P, FUSE)                                     \
  if (prev_flags == policy_demand_type::PREV_FLAGS                             \
      && next_flags == policy_demand_type::NEXT_FLAGS)                         \
    return COMPATIBLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool policy_available_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    policy_demand_type prev_flags = prev.get_policy_demand ();
    policy_demand_type next_flags = next.get_policy_demand ();
#define DEF_POLICY_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,       \
			AVAILABLE_P, FUSE)                                     \
  if (prev_flags == policy_demand_type::PREV_FLAGS                             \
      && next_flags == policy_demand_type::NEXT_FLAGS)                         \
    return AVAILABLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  void merge_policy (vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    policy_demand_type prev_flags = prev.get_policy_demand ();
    policy_demand_type next_flags = next.get_policy_demand ();
#define DEF_POLICY_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,       \
			AVAILABLE_P, FUSE)                                     \
  if (prev_flags == policy_demand_type::PREV_FLAGS                             \
      && next_flags == policy_demand_type::NEXT_FLAGS)                         \
    {                                                                          \
      gcc_assert (COMPATIBLE_P (prev, next));                                  \
      FUSE (prev, next);                                                       \
      prev.set_policy_demand (policy_demand_type::NEW_FLAGS);                  \
      return;                                                                  \
    }

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool vl_not_in_conflict_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    /* We don't fuse this following case:

	li a5, -1
	vmv.s.x v0, a5         -- PREV
	vsetvli a5, ...        -- NEXT

       Don't fuse NEXT into PREV.
    */
    return !prev.vl_modify_non_avl_op_p (next)
	   && !next.vl_modify_non_avl_op_p (prev);
  }

  bool avl_compatible_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    avl_demand_type prev_flags = prev.get_avl_demand ();
    avl_demand_type next_flags = next.get_avl_demand ();
#define DEF_AVL_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,          \
		     AVAILABLE_P, FUSE)                                        \
  if (prev_flags == avl_demand_type::PREV_FLAGS                                \
      && next_flags == avl_demand_type::NEXT_FLAGS)                            \
    return COMPATIBLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool avl_available_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    avl_demand_type prev_flags = prev.get_avl_demand ();
    avl_demand_type next_flags = next.get_avl_demand ();
#define DEF_AVL_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,          \
		     AVAILABLE_P, FUSE)                                        \
  if (prev_flags == avl_demand_type::PREV_FLAGS                                \
      && next_flags == avl_demand_type::NEXT_FLAGS)                            \
    return AVAILABLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  void merge_avl (vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    avl_demand_type prev_flags = prev.get_avl_demand ();
    avl_demand_type next_flags = next.get_avl_demand ();
#define DEF_AVL_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,          \
		     AVAILABLE_P, FUSE)                                        \
  if (prev_flags == avl_demand_type::PREV_FLAGS                                \
      && next_flags == avl_demand_type::NEXT_FLAGS)                            \
    {                                                                          \
      gcc_assert (COMPATIBLE_P (prev, next));                                  \
      FUSE (prev, next);                                                       \
      prev.set_avl_demand (avl_demand_type::NEW_FLAGS);                        \
      return;                                                                  \
    }

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool ime_compatible_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    ime_demand_type prev_flags = prev.get_ime_demand ();
    ime_demand_type next_flags = next.get_ime_demand ();
#define DEF_IME_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,          \
		     AVAILABLE_P, FUSE)                                        \
  if (prev_flags == ime_demand_type::PREV_FLAGS                                \
      && next_flags == ime_demand_type::NEXT_FLAGS)                            \
    return COMPATIBLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool ime_available_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    ime_demand_type prev_flags = prev.get_ime_demand ();
    ime_demand_type next_flags = next.get_ime_demand ();
#define DEF_IME_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,          \
		     AVAILABLE_P, FUSE)                                        \
  if (prev_flags == ime_demand_type::PREV_FLAGS                                \
      && next_flags == ime_demand_type::NEXT_FLAGS)                            \
    return AVAILABLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  void merge_ime (vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    ime_demand_type prev_flags = prev.get_ime_demand ();
    ime_demand_type next_flags = next.get_ime_demand ();
#define DEF_IME_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,          \
		     AVAILABLE_P, FUSE)                                        \
  if (prev_flags == ime_demand_type::PREV_FLAGS                                \
      && next_flags == ime_demand_type::NEXT_FLAGS)                            \
    {                                                                          \
      gcc_assert (COMPATIBLE_P (prev, next));                                  \
      FUSE (prev, next);                                                       \
      prev.set_ime_demand (ime_demand_type::NEW_FLAGS);                        \
      return;                                                                  \
    }

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool compatible_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    bool compatible_p = sew_lmul_compatible_p (prev, next)
			&& policy_compatible_p (prev, next)
			&& avl_compatible_p (prev, next)
			&& ime_compatible_p (prev, next)
			&& vl_not_in_conflict_p (prev, next);
    return compatible_p;
  }

  bool available_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    bool available_p = sew_lmul_available_p (prev, next)
		       && policy_available_p (prev, next)
		       && avl_available_p (prev, next)
		       && ime_available_p (prev, next)
		       && vl_not_in_conflict_p (prev, next);
    gcc_assert (!available_p || compatible_p (prev, next));
    return available_p;
  }

  void merge (vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (compatible_p (prev, next));
    merge_sew_lmul (prev, next);
    merge_policy (prev, next);
    merge_avl (prev, next);
    merge_ime (prev, next);
    gcc_assert (available_p (prev, next));
  }
};


class pre_vsetvl
{
private:
  demand_system m_dem;
  auto_vec<vsetvl_block_info> m_vector_block_infos;

  /* data for avl reaching definition.  */
  sbitmap *m_reg_def_loc;

  /* Holds register uses per basic block.  Restricted to those registers that
     are used as vsetvl destinations.  */
  sbitmap *m_reg_use_loc;

  /* data for vsetvl info reaching definition.  */
  vsetvl_info m_unknown_info;
  auto_vec<vsetvl_info *> m_vsetvl_def_exprs;
  sbitmap *m_vsetvl_def_in;
  sbitmap *m_vsetvl_def_out;

  /* Reaching data for vsetvl AVL operands.  */
  sbitmap *m_vsetvl_avl_reach_in;
  sbitmap *m_vsetvl_avl_reach_out;

  /* data for lcm */
  auto_vec<vsetvl_info *> m_exprs;
  sbitmap *m_avloc;
  sbitmap *m_avin;
  sbitmap *m_avout;
  sbitmap *m_kill;
  sbitmap *m_antloc;
  sbitmap *m_transp;
  sbitmap *m_insert;
  sbitmap *m_del;
  struct edge_list *m_edges;

  auto_vec<vsetvl_info> m_delete_list;

  vsetvl_block_info &get_block_info (const bb_info *bb)
  {
    return m_vector_block_infos[bb->index ()];
  }
  const vsetvl_block_info &get_block_info (const basic_block bb) const
  {
    return m_vector_block_infos[bb->index];
  }

  vsetvl_block_info &get_block_info (const basic_block bb)
  {
    return m_vector_block_infos[bb->index];
  }

  void add_expr (auto_vec<vsetvl_info *> &m_exprs, vsetvl_info &info)
  {
    for (vsetvl_info *item : m_exprs)
      {
	if (*item == info)
	  return;
      }
    m_exprs.safe_push (&info);
  }

  unsigned get_expr_index (auto_vec<vsetvl_info *> &m_exprs,
			   const vsetvl_info &info)
  {
    for (size_t i = 0; i < m_exprs.length (); i += 1)
      {
	if (*m_exprs[i] == info)
	  return i;
      }
    gcc_unreachable ();
  }

  bool anticipated_exp_p (const vsetvl_info &header_info)
  {
    if (!header_info.has_nonvlmax_reg_avl () && !header_info.has_vl ())
      return true;

    bb_info *bb = header_info.get_bb ();
    insn_info *prev_insn = bb->head_insn ();
    insn_info *next_insn = header_info.insn_inside_bb_p ()
			     ? header_info.get_insn ()
			     : header_info.get_bb ()->end_insn ();

    return m_dem.avl_vl_unmodified_between_p (prev_insn, next_insn,
					      header_info);
  }

  bool available_exp_p (const vsetvl_info &prev_info,
			const vsetvl_info &next_info)
  {
    return m_dem.available_p (prev_info, next_info);
  }

  void compute_probabilities ()
  {
    edge e;
    edge_iterator ei;

    for (const bb_info *bb : crtl->ssa->bbs ())
      {
	basic_block cfg_bb = bb->cfg_bb ();
	auto &curr_prob = get_block_info (cfg_bb).probability;

	/* GCC assume entry block (bb 0) are always so
	   executed so set its probability as "always".  */
	if (ENTRY_BLOCK_PTR_FOR_FN (cfun) == cfg_bb)
	  curr_prob = profile_probability::always ();
	/* Exit block (bb 1) is the block we don't need to process.  */
	if (EXIT_BLOCK_PTR_FOR_FN (cfun) == cfg_bb)
	  continue;

	gcc_assert (curr_prob.initialized_p ());
	FOR_EACH_EDGE (e, ei, cfg_bb->succs)
	  {
	    auto &new_prob = get_block_info (e->dest).probability;
	    /* Normally, the edge probability should be initialized.
	       However, some special testing code which is written in
	       GIMPLE IR style force the edge probability uninitialized,
	       we conservatively set it as never so that it will not
	       affect PRE (Phase 3 && Phase 4).  */
	    if (!e->probability.initialized_p ())
	      new_prob = profile_probability::never ();
	    else if (!new_prob.initialized_p ())
	      new_prob = curr_prob * e->probability;
	    else if (new_prob == profile_probability::always ())
	      continue;
	    else
	      new_prob += curr_prob * e->probability;
	  }
      }
  }

  /* Emit a single register-form vsetvl instruction to establish the
     full IME vtype.  The vtype value is passed as a const_int; the
     @vsetvl_ime<mode> pattern expands it into a "li + vsetvl"
     two-instruction sequence at assembly time, reusing its dest VL
     register as the temporary for the li.  No new pseudos are
     required, which is essential because this runs post-reload.  */

  void insert_ime_vsetvl_insn (enum emit_type emit_type,
			       const vsetvl_info &info)
  {
    rtx avl = info.get_avl ();
    if (!avl)
      avl = const0_rtx;

    /* The @vsetvl_ime* patterns require AVL to be either a register or
       const_int 0 (pmode_reg_or_0_operand).  A non-zero const_int AVL
       (e.g. from a literal `vl` argument in the user's intrinsic call)
       must be materialised into a register first.  Running post-reload
       we cannot create fresh pseudos -- emit an explicit `li` into a
       caller-saved scratch (PROLOGUE_TEMP2) that we know is free at
       the insertion site.  */
    if (CONST_INT_P (avl) && avl != const0_rtx)
      {
	/* Use t3 (x28): outside the {t0, t1, t2} scratch set used by
	   the runtime vsetvl sequence AND outside typical dest_vl
	   allocations.  */
	rtx avl_reg = gen_rtx_REG (Pmode, GP_REG_FIRST + 28);
	rtx li_insn = gen_rtx_SET (avl_reg, avl);
	if (emit_type == EMIT_DIRECT)
	  emit_insn (li_insn);
	else if (emit_type == EMIT_BEFORE)
	  emit_insn_before (li_insn, info.get_insn ()->rtl ());
	else
	  emit_insn_after (li_insn, info.get_insn ()->rtl ());
	avl = avl_reg;
      }

    /* Pick a destination/scratch register for the vsetvl_ime insn.  The
       @vsetvl_ime pattern reuses its dest register as the scratch for
       the "li" that loads the vtype constant.  If the IME instruction
       we're setting up has its own VL destination (rare -- only for
       vsetvl intrinsics), reuse that; otherwise fall back to the
       dedicated prologue temp (x5 / t0), which is caller-saved and not
       reserved across the function body.  The new VL value written by
       the register-form vsetvl is dead in the latter case, which is
       fine because downstream RVV insns read VL via VL_REGNUM, not via
       a GPR.  */
    /* For the IME runtime path, dest_vl is used both as the vsetvl
       destination AND as an internal scratch in the asm template.
       The template also hardcodes scratch2/scratch3 to TEMP2/TEMP2+1
       (t1/t2).  To avoid register conflicts when info.has_vl() is
       true (e.g. the VL pseudo got allocated to t1), always pick
       PROLOGUE_TEMP (t0) for dest_vl.  VL is delivered via VL_REGNUM
       anyway; the pseudo is typically dead at the use site.  */
    rtx dest_vl = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP_REGNUM);

    rtx pat;
    if (info.get_ime_req ().lambda_log2 == -1)
      {
	pat = emit_ime_vtype_runtime_pat (info, dest_vl, avl);
	if (pat == NULL_RTX)
	  return;  /* sorry() already reported.  */
      }
    else
      {
	rtx vtype_const = emit_ime_vtype_build (info);
	if (vtype_const == NULL_RTX)
	  return;  /* sorry() / error() already reported.  */
	pat = gen_vsetvl_ime (Pmode, dest_vl, avl, vtype_const);
      }

    rtx_insn *rinsn = info.get_insn ()->rtl ();
    rtx_insn *inserted = NULL;

    if (emit_type == EMIT_DIRECT)
      {
	emit_insn (pat);
	inserted = get_last_insn ();
      }
    else if (emit_type == EMIT_BEFORE)
      {
	emit_insn_before (pat, rinsn);
	inserted = PREV_INSN (rinsn);
      }
    else
      {
	emit_insn_after (pat, rinsn);
	inserted = NEXT_INSN (rinsn);
      }

    if (dump_file && inserted)
      {
	fprintf (dump_file, "  Insert IME vsetvl insn %d:\n",
		 INSN_UID (inserted));
	print_rtl_single (dump_file, inserted);
      }
  }

  void insert_vsetvl_insn (enum emit_type emit_type, const vsetvl_info &info)
  {
    if (info.ime_required_p ())
      {
	insert_ime_vsetvl_insn (emit_type, info);
	return;
      }

    rtx pat = info.get_vsetvl_pat ();
    rtx_insn *rinsn = info.get_insn ()->rtl ();

    if (emit_type == EMIT_DIRECT)
      {
	emit_insn (pat);
	if (dump_file)
	  {
	    fprintf (dump_file, "  Insert vsetvl insn %d:\n",
		     INSN_UID (get_last_insn ()));
	    print_rtl_single (dump_file, get_last_insn ());
	  }
      }
    else if (emit_type == EMIT_BEFORE)
      {
	emit_insn_before (pat, rinsn);
	if (dump_file)
	  {
	    fprintf (dump_file, "  Insert vsetvl insn before insn %d:\n",
		     INSN_UID (rinsn));
	    print_rtl_single (dump_file, PREV_INSN (rinsn));
	  }
      }
    else
      {
	emit_insn_after (pat, rinsn);
	if (dump_file)
	  {
	    fprintf (dump_file, "  Insert vsetvl insn after insn %d:\n",
		     INSN_UID (rinsn));
	    print_rtl_single (dump_file, NEXT_INSN (rinsn));
	  }
      }
  }

  void change_vsetvl_insn (const vsetvl_info &info)
  {
    /* IME setups are multi-insn sequences (constant load + register-form
       vsetvl) that cannot be dropped into an existing insn via
       validate_change_or_fail.  They are always inserted fresh, never
       produced by changing an existing vsetvli.  If this trips, the
       caller tried to rewrite a plain vsetvli to an IME setup, which is
       not supported.  */
    gcc_assert (!info.ime_required_p ());

    rtx_insn *rinsn = info.get_insn ()->rtl ();
    rtx new_pat = info.get_vsetvl_pat ();

    if (dump_file)
      {
	fprintf (dump_file, "  Change insn %d from:\n", INSN_UID (rinsn));
	print_rtl_single (dump_file, rinsn);
      }

    validate_change_or_fail (rinsn, &PATTERN (rinsn), new_pat, false);

    if (dump_file)
      {
	fprintf (dump_file, "\n  to:\n");
	print_rtl_single (dump_file, rinsn);
      }
  }

  void remove_vsetvl_insn (rtx_insn *rinsn)
  {
    if (dump_file)
      {
	fprintf (dump_file, "  Eliminate insn %d:\n", INSN_UID (rinsn));
	print_rtl_single (dump_file, rinsn);
      }
    if (in_sequence_p ())
      remove_insn (rinsn);
    else
      delete_insn (rinsn);
  }

  bool successors_probability_equal_p (const basic_block cfg_bb) const
  {
    edge e;
    edge_iterator ei;
    profile_probability prob = profile_probability::uninitialized ();
    FOR_EACH_EDGE (e, ei, cfg_bb->succs)
      {
	if (prob == profile_probability::uninitialized ())
	  prob = m_vector_block_infos[e->dest->index].probability;
	else if (prob == m_vector_block_infos[e->dest->index].probability)
	  continue;
	else
	  /* We pick the highest probability among those incompatible VSETVL
	     infos. When all incompatible VSETVL infos have same probability, we
	     don't pick any of them.  */
	  return false;
      }
    return true;
  }

  bool has_compatible_reaching_vsetvl_p (vsetvl_info info)
  {
    unsigned int index;
    sbitmap_iterator sbi;
    EXECUTE_IF_SET_IN_BITMAP (m_vsetvl_def_in[info.get_bb ()->index ()], 0,
			      index, sbi)
      {
	const auto prev_info = *m_vsetvl_def_exprs[index];
	if (!prev_info.valid_p ())
	  continue;
	if (m_dem.compatible_p (prev_info, info))
	  return true;
      }
    return false;
  }

  bool preds_all_same_avl_and_ratio_p (const vsetvl_info &curr_info)
  {
    gcc_assert (
      !bitmap_empty_p (m_vsetvl_def_in[curr_info.get_bb ()->index ()]));

    unsigned expr_index;
    sbitmap_iterator sbi;
    EXECUTE_IF_SET_IN_BITMAP (m_vsetvl_def_in[curr_info.get_bb ()->index ()], 0,
			      expr_index, sbi)
      {
	const vsetvl_info &prev_info = *m_vsetvl_def_exprs[expr_index];
	if (!prev_info.valid_p ()
	    || !m_dem.avl_available_p (prev_info, curr_info)
	    || prev_info.get_ratio () != curr_info.get_ratio ())
	  return false;
      }

    return true;
  }

public:
  pre_vsetvl ()
    : m_reg_def_loc (nullptr), m_reg_use_loc (nullptr),
      m_vsetvl_def_in (nullptr), m_vsetvl_def_out (nullptr),
      m_vsetvl_avl_reach_in (nullptr), m_vsetvl_avl_reach_out (nullptr),
      m_avloc (nullptr),
      m_avin (nullptr), m_avout (nullptr), m_kill (nullptr), m_antloc (nullptr),
      m_transp (nullptr), m_insert (nullptr), m_del (nullptr), m_edges (nullptr)
  {
    init_rtl_ssa ();
    m_vector_block_infos.safe_grow_cleared (last_basic_block_for_fn (cfun));
    compute_probabilities ();
    m_unknown_info.set_unknown ();
  }

  void finish ()
  {
    finish_rtl_ssa ();

    if (m_reg_def_loc)
      sbitmap_vector_free (m_reg_def_loc);
    if (m_reg_use_loc)
      sbitmap_vector_free (m_reg_use_loc);

    if (m_vsetvl_def_in)
      sbitmap_vector_free (m_vsetvl_def_in);
    if (m_vsetvl_def_out)
      sbitmap_vector_free (m_vsetvl_def_out);

    if (m_vsetvl_avl_reach_in)
      sbitmap_vector_free (m_vsetvl_avl_reach_in);
    if (m_vsetvl_avl_reach_out)
      sbitmap_vector_free (m_vsetvl_avl_reach_out);

    if (m_avloc)
      sbitmap_vector_free (m_avloc);
    if (m_kill)
      sbitmap_vector_free (m_kill);
    if (m_antloc)
      sbitmap_vector_free (m_antloc);
    if (m_transp)
      sbitmap_vector_free (m_transp);
    if (m_insert)
      sbitmap_vector_free (m_insert);
    if (m_del)
      sbitmap_vector_free (m_del);
    if (m_avin)
      sbitmap_vector_free (m_avin);
    if (m_avout)
      sbitmap_vector_free (m_avout);

    if (m_edges)
      free_edge_list (m_edges);
  }

  void compute_vsetvl_def_data ();
  void compute_transparent (const bb_info *);
  void compute_lcm_local_properties ();

  void fuse_local_vsetvl_info ();
  bool earliest_fuse_vsetvl_info (int iter);
  void pre_global_vsetvl_info ();
  void emit_vsetvl ();
  void cleanup ();
  void remove_avl_operand ();
  void remove_unused_dest_operand ();
  void remove_vsetvl_pre_insns ();

  void dump (FILE *file, const char *title) const
  {
    fprintf (file, "\nVSETVL infos after %s\n\n", title);
    for (const bb_info *bb : crtl->ssa->bbs ())
      {
	const auto &block_info = m_vector_block_infos[bb->index ()];
	fprintf (file, "  bb %d:\n", bb->index ());
	fprintf (file, "    probability: ");
	block_info.probability.dump (file);
	fprintf (file, "\n");
	if (!block_info.empty_p ())
	  {
	    fprintf (file, "    Header vsetvl info:");
	    block_info.get_entry_info ().dump (file, "      ");
	    fprintf (file, "    Footer vsetvl info:");
	    block_info.get_exit_info ().dump (file, "      ");
	    for (const auto &info : block_info.local_infos)
	      {
		fprintf (file,
			 "    insn %d vsetvl info:", info.get_insn ()->uid ());
		info.dump (file, "      ");
	      }
	  }
      }
  }
};

void
pre_vsetvl::compute_vsetvl_def_data ()
{
  m_vsetvl_def_exprs.truncate (0);
  add_expr (m_vsetvl_def_exprs, m_unknown_info);
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      vsetvl_block_info &block_info = get_block_info (bb);
      if (block_info.empty_p ())
	continue;
      vsetvl_info &footer_info = block_info.get_exit_info ();
      gcc_assert (footer_info.valid_p () || footer_info.unknown_p ());
      add_expr (m_vsetvl_def_exprs, footer_info);
    }

  if (m_vsetvl_def_in)
    sbitmap_vector_free (m_vsetvl_def_in);
  if (m_vsetvl_def_out)
    sbitmap_vector_free (m_vsetvl_def_out);
  if (m_vsetvl_avl_reach_in)
    sbitmap_vector_free (m_vsetvl_avl_reach_in);
  if (m_vsetvl_avl_reach_out)
    sbitmap_vector_free (m_vsetvl_avl_reach_out);

  sbitmap *def_loc = sbitmap_vector_alloc (last_basic_block_for_fn (cfun),
					   m_vsetvl_def_exprs.length ());
  sbitmap *m_kill = sbitmap_vector_alloc (last_basic_block_for_fn (cfun),
					  m_vsetvl_def_exprs.length ());

  m_vsetvl_def_in = sbitmap_vector_alloc (last_basic_block_for_fn (cfun),
					  m_vsetvl_def_exprs.length ());
  m_vsetvl_def_out = sbitmap_vector_alloc (last_basic_block_for_fn (cfun),
					   m_vsetvl_def_exprs.length ());

  m_vsetvl_avl_reach_in
    = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), GP_REG_LAST + 1);
  m_vsetvl_avl_reach_out
    = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), GP_REG_LAST + 1);

  bitmap_vector_clear (def_loc, last_basic_block_for_fn (cfun));
  bitmap_vector_clear (m_kill, last_basic_block_for_fn (cfun));
  bitmap_vector_clear (m_vsetvl_def_out, last_basic_block_for_fn (cfun));

  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      vsetvl_block_info &block_info = get_block_info (bb);
      if (block_info.empty_p ())
	{
	  for (unsigned i = 0; i < m_vsetvl_def_exprs.length (); i += 1)
	    {
	      auto *info = m_vsetvl_def_exprs[i];
	      if (info->has_nonvlmax_reg_avl ()
		  && bitmap_bit_p (m_reg_def_loc[bb->index ()],
				   REGNO (info->get_avl ())))
		{
		  bitmap_set_bit (m_kill[bb->index ()], i);
		  bitmap_set_bit (def_loc[bb->index ()],
				  get_expr_index (m_vsetvl_def_exprs,
						  m_unknown_info));
		}
	    }
	  continue;
	}

      vsetvl_info &footer_info = block_info.get_exit_info ();
      bitmap_ones (m_kill[bb->index ()]);
      bitmap_set_bit (def_loc[bb->index ()],
		      get_expr_index (m_vsetvl_def_exprs, footer_info));
    }

  /* Set the def_out of the ENTRY basic block to m_unknown_info expr.  */
  basic_block entry = ENTRY_BLOCK_PTR_FOR_FN (cfun);
  bitmap_set_bit (m_vsetvl_def_out[entry->index],
		  get_expr_index (m_vsetvl_def_exprs, m_unknown_info));

  compute_reaching_definition (def_loc, m_kill, m_vsetvl_def_in,
			       m_vsetvl_def_out);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file,
	       "\n  Compute vsetvl info reaching definition data:\n\n");
      fprintf (dump_file, "    Expression List (%d):\n",
	       m_vsetvl_def_exprs.length ());
      for (unsigned i = 0; i < m_vsetvl_def_exprs.length (); i++)
	{
	  const auto &info = *m_vsetvl_def_exprs[i];
	  fprintf (dump_file, "      Expr[%u]: ", i);
	  info.dump (dump_file, "        ");
	}
      fprintf (dump_file, "\n    bitmap data:\n");
      for (const bb_info *bb : crtl->ssa->bbs ())
	{
	  unsigned int i = bb->index ();
	  fprintf (dump_file, "      BB %u:\n", i);
	  fprintf (dump_file, "        def_loc: ");
	  dump_bitmap_file (dump_file, def_loc[i]);
	  fprintf (dump_file, "        kill: ");
	  dump_bitmap_file (dump_file, m_kill[i]);
	  fprintf (dump_file, "        vsetvl_def_in: ");
	  dump_bitmap_file (dump_file, m_vsetvl_def_in[i]);
	  fprintf (dump_file, "        vsetvl_def_out: ");
	  dump_bitmap_file (dump_file, m_vsetvl_def_out[i]);
	}
    }

  sbitmap_vector_free (def_loc);
  sbitmap_vector_free (m_kill);

  /* Now compute the reaching definitions for AVL operands.
     We can reuse def_loc but index it by regnos now.  */
  def_loc = sbitmap_vector_alloc (last_basic_block_for_fn (cfun),
				  GP_REG_LAST + 1);

  bitmap_vector_clear (def_loc, last_basic_block_for_fn (cfun));
  bitmap_vector_clear (m_vsetvl_avl_reach_out, last_basic_block_for_fn (cfun));

  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      vsetvl_block_info &block_info = get_block_info (bb);
      if (block_info.empty_p ())
	continue;
      vsetvl_info &info = block_info.get_exit_info ();
      if (info.has_vl ())
	bitmap_set_bit (def_loc[bb->index ()], REGNO (info.get_vl ()));
    }

  compute_reaching_definition (def_loc, m_reg_def_loc, m_vsetvl_avl_reach_in,
			       m_vsetvl_avl_reach_out);
}

/* Subroutine of compute_lcm_local_properties which Compute local transparent
   BB. Note that the compile time is very sensitive to compute_transparent and
   compute_lcm_local_properties, any change of these 2 functions should be
   aware of the compile time changing of the program which has a large number of
   blocks, e.g SPEC 2017 wrf.

   Current compile time profile of SPEC 2017 wrf:

     1. scheduling - 27%
     2. machine dep reorg (VSETVL PASS) - 18%

   VSETVL pass should not spend more time than scheduling in compilation.  */
void
pre_vsetvl::compute_transparent (const bb_info *bb)
{
  int num_exprs = m_exprs.length ();
  unsigned bb_index = bb->index ();
  for (int i = 0; i < num_exprs; i++)
    {
      auto *info = m_exprs[i];
      if (info->has_nonvlmax_reg_avl ()
	  && bitmap_bit_p (m_reg_def_loc[bb_index], REGNO (info->get_avl ())))
	bitmap_clear_bit (m_transp[bb_index], i);
      else if (info->has_vl ())
	{
	  /* If the VL reg is redefined, we cannot move a vsetvl past it.  */
	  if (bitmap_bit_p (m_reg_def_loc[bb_index],
			    REGNO (info->get_vl ())))
	    bitmap_clear_bit (m_transp[bb_index], i);
	  /* Same if there is a VL reg use that didn't come from a vsetvl.  */
	  else if (bitmap_bit_p (m_reg_use_loc[bb_index],
				 REGNO (info->get_vl ()))
		   && !bitmap_bit_p (m_vsetvl_avl_reach_in[bb_index],
				     REGNO (info->get_vl ())))
	    bitmap_clear_bit (m_transp[bb_index], i);
	}
    }
}

/* Compute the local properties of each recorded expression.

   Local properties are those that are defined by the block, irrespective of
   other blocks.

   An expression is transparent in a block if its operands are not modified
   in the block.

   An expression is computed (locally available) in a block if it is computed
   at least once and expression would contain the same value if the
   computation was moved to the end of the block.

   An expression is locally anticipatable in a block if it is computed at
   least once and expression would contain the same value if the computation
   was moved to the beginning of the block.  */
void
pre_vsetvl::compute_lcm_local_properties ()
{
  m_exprs.truncate (0);
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      vsetvl_block_info &block_info = get_block_info (bb);
      if (block_info.empty_p ())
	continue;
      vsetvl_info &header_info = block_info.get_entry_info ();
      vsetvl_info &footer_info = block_info.get_exit_info ();
      gcc_assert (footer_info.valid_p () || footer_info.unknown_p ());
      if (header_info.valid_p ())
	add_expr (m_exprs, header_info);
      if (footer_info.valid_p ())
	add_expr (m_exprs, footer_info);
    }

  int num_exprs = m_exprs.length ();
  if (m_avloc)
    sbitmap_vector_free (m_avloc);
  if (m_kill)
    sbitmap_vector_free (m_kill);
  if (m_antloc)
    sbitmap_vector_free (m_antloc);
  if (m_transp)
    sbitmap_vector_free (m_transp);
  if (m_avin)
    sbitmap_vector_free (m_avin);
  if (m_avout)
    sbitmap_vector_free (m_avout);

  m_avloc = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);
  m_kill = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);
  m_antloc = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);
  m_transp = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);
  m_avin = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);
  m_avout = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);

  bitmap_vector_clear (m_avloc, last_basic_block_for_fn (cfun));
  bitmap_vector_clear (m_kill, last_basic_block_for_fn (cfun));
  bitmap_vector_clear (m_antloc, last_basic_block_for_fn (cfun));
  bitmap_vector_ones (m_transp, last_basic_block_for_fn (cfun));

  /* -  If T is locally available at the end of a block, then T' must be
	available at the end of the same block. Since some optimization has
	occurred earlier, T' might not be locally available, however, it must
	have been previously computed on all paths. As a formula, T at AVLOC(B)
	implies that T' at AVOUT(B).
	An "available occurrence" is one that is the last occurrence in the
	basic block and the operands are not modified by following statements in
	the basic block [including this insn].

     -  If T is locally anticipated at the beginning of a block, then either
	T', is locally anticipated or it is already available from previous
	blocks. As a formula, this means that T at ANTLOC(B) implies that T' at
	ANTLOC(B) at AVIN(B).
	An "anticipatable occurrence" is one that is the first occurrence in the
	basic block, the operands are not modified in the basic block prior
	to the occurrence and the output is not used between the start of
	the block and the occurrence.  */
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      unsigned bb_index = bb->index ();
      vsetvl_block_info &block_info = get_block_info (bb);

      /* Compute m_transp */
      if (block_info.empty_p ())
	compute_transparent (bb);
      else
	{
	  bitmap_clear (m_transp[bb_index]);
	  vsetvl_info &header_info = block_info.get_entry_info ();
	  vsetvl_info &footer_info = block_info.get_exit_info ();

	  if (header_info.valid_p () && anticipated_exp_p (header_info))
	    bitmap_set_bit (m_antloc[bb_index],
			    get_expr_index (m_exprs, header_info));

	  if (footer_info.valid_p ())
	    for (int i = 0; i < num_exprs; i += 1)
	      {
		const vsetvl_info &info = *m_exprs[i];
		if (!info.valid_p ())
		  continue;
		if (available_exp_p (footer_info, info))
		  bitmap_set_bit (m_avloc[bb_index], i);
	      }
	}

      if (invalid_opt_bb_p (bb->cfg_bb ()))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "\n --- skipping bb %u due to weird edge",
		     bb->index ());

	  bitmap_clear (m_antloc[bb_index]);
	  bitmap_clear (m_transp[bb_index]);
	}

      /* Compute ae_kill for each basic block using:

	 ~(TRANSP | COMP)
      */
      bitmap_ior (m_kill[bb_index], m_transp[bb_index], m_avloc[bb_index]);
      bitmap_not (m_kill[bb_index], m_kill[bb_index]);
    }
}

void
pre_vsetvl::fuse_local_vsetvl_info ()
{
  m_reg_def_loc
    = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), GP_REG_LAST + 1);
  bitmap_vector_clear (m_reg_def_loc, last_basic_block_for_fn (cfun));
  bitmap_ones (m_reg_def_loc[ENTRY_BLOCK_PTR_FOR_FN (cfun)->index]);

  m_reg_use_loc
    = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), GP_REG_LAST + 1);
  bitmap_vector_clear (m_reg_use_loc, last_basic_block_for_fn (cfun));

  /* No need to track all GPRs, just use those that are VL destinations.
     Store them in a bitmap for filtering the uses later on.  */
  auto_bitmap vsetvl_dest_regs;
  for (bb_info *bb : crtl->ssa->bbs ())
    for (insn_info *insn : bb->real_nondebug_insns ())
      {
	vsetvl_info info = vsetvl_info (insn);
	if (info.valid_p () && info.has_vl ())
	  bitmap_set_bit (vsetvl_dest_regs, REGNO (info.get_vl ()));
      }

  for (bb_info *bb : crtl->ssa->bbs ())
    {
      auto &block_info = get_block_info (bb);
      block_info.bb = bb;
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "  Try fuse basic block %d\n", bb->index ());
	}
      auto_vec<vsetvl_info> infos;
      for (insn_info *insn : bb->real_nondebug_insns ())
	{
	  vsetvl_info curr_info = vsetvl_info (insn);
	  if (curr_info.valid_p () || curr_info.unknown_p ())
	    infos.safe_push (curr_info);

	  if (insn->is_real ())
	    {
	      /* Collect GPRs modified by the current bb.  */
	      for (def_info *def : insn->defs ())
		if (def->is_reg () && GP_REG_P (def->regno ()))
		  bitmap_set_bit (m_reg_def_loc[bb->index ()], def->regno ());
	      /* Collect non-vsetvl uses of GPRs.  */
	      if (!curr_info.valid_p ())
		{
		  for (use_info *use : insn->uses ())
		    if (use->is_reg () && GP_REG_P (use->regno ())
			&& bitmap_bit_p (vsetvl_dest_regs, use->regno ()))
		      bitmap_set_bit (m_reg_use_loc[bb->index ()],
				      use->regno ());
		}
	    }
	}

      vsetvl_info prev_info = vsetvl_info ();
      prev_info.set_empty ();
      for (auto &curr_info : infos)
	{
	  if (prev_info.empty_p ())
	    prev_info = curr_info;
	  else if ((curr_info.unknown_p () && prev_info.valid_p ())
		   || (curr_info.valid_p () && prev_info.unknown_p ()))
	    {
	      block_info.local_infos.safe_push (prev_info);
	      prev_info = curr_info;
	    }
	  else if (curr_info.valid_p () && prev_info.valid_p ())
	    {
	      if (m_dem.available_p (prev_info, curr_info))
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file,
			       "    Ignore curr info since prev info "
			       "available with it:\n");
		      fprintf (dump_file, "      prev_info: ");
		      prev_info.dump (dump_file, "        ");
		      fprintf (dump_file, "      curr_info: ");
		      curr_info.dump (dump_file, "        ");
		      fprintf (dump_file, "\n");
		    }
		  /* Even though prev_info is available with curr_info,
		     we need to update the MAX_SEW of prev_info since
		     we don't check MAX_SEW in available_p check.

		     prev_info:
		     Demand fields: demand_ratio_and_ge_sew demand_avl
		     SEW=16, VLMUL=mf4, RATIO=64, MAX_SEW=64

		     curr_info:
		     Demand fields: demand_ge_sew demand_non_zero_avl
		     SEW=16, VLMUL=m1, RATIO=16, MAX_SEW=32

		     In the example above, prev_info is available with
		     curr_info, we need to update prev_info MAX_SEW from
		     64 into 32.  */
		  prev_info.set_max_sew (
		    MIN (prev_info.get_max_sew (), curr_info.get_max_sew ()));

		  /* If we fuse and the current, to be deleted vsetvl has uses
		     of its VL as AVL operand in other vsetvls those will be
		     orphaned.  Therefore only delete if that's not the case.
		     */
		  rtx cur_dest = curr_info.has_vl ()
		    ? curr_info.get_vl ()
		    : NULL_RTX;

		  if (vsetvl_insn_p (curr_info.get_insn ()->rtl ())
		      && !reg_single_use_in_avl (curr_info.get_insn (),
						 cur_dest))
		    m_delete_list.safe_push (curr_info);

		  if (curr_info.get_read_vl_insn ())
		    prev_info.set_read_vl_insn (curr_info.get_read_vl_insn ());
		}
	      else if (m_dem.compatible_p (prev_info, curr_info))
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "    Fuse curr info since prev info "
					  "compatible with it:\n");
		      fprintf (dump_file, "      prev_info: ");
		      prev_info.dump (dump_file, "        ");
		      fprintf (dump_file, "      curr_info: ");
		      curr_info.dump (dump_file, "        ");
		    }
		  m_dem.merge (prev_info, curr_info);
		  if (!curr_info.vl_used_by_non_rvv_insn_p ()
		      && vsetvl_insn_p (curr_info.get_insn ()->rtl ()))
		    m_delete_list.safe_push (curr_info);
		  if (curr_info.get_read_vl_insn ())
		    prev_info.set_read_vl_insn (curr_info.get_read_vl_insn ());
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "      prev_info after fused: ");
		      prev_info.dump (dump_file, "        ");
		      fprintf (dump_file, "\n");
		    }
		}
	      else
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file,
			       "    Cannot fuse incompatible infos:\n");
		      fprintf (dump_file, "      prev_info: ");
		      prev_info.dump (dump_file, "       ");
		      fprintf (dump_file, "      curr_info: ");
		      curr_info.dump (dump_file, "       ");
		    }
		  block_info.local_infos.safe_push (prev_info);
		  prev_info = curr_info;
		}
	    }
	}

      if (prev_info.valid_p () || prev_info.unknown_p ())
	block_info.local_infos.safe_push (prev_info);
    }
}


bool
pre_vsetvl::earliest_fuse_vsetvl_info (int iter)
{
  compute_vsetvl_def_data ();
  compute_lcm_local_properties ();

  unsigned num_exprs = m_exprs.length ();
  struct edge_list *m_edges = create_edge_list ();
  unsigned num_edges = NUM_EDGES (m_edges);
  sbitmap *antin
    = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);
  sbitmap *antout
    = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);

  sbitmap *earliest = sbitmap_vector_alloc (num_edges, num_exprs);

  compute_available (m_avloc, m_kill, m_avout, m_avin);
  compute_antinout_edge (m_antloc, m_transp, antin, antout);
  compute_earliest (m_edges, num_exprs, antin, antout, m_avout, m_kill,
		    earliest);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\n  Compute LCM earliest insert data (lift %d):\n\n",
	       iter);
      fprintf (dump_file, "    Expression List (%u):\n", num_exprs);
      for (unsigned i = 0; i < num_exprs; i++)
	{
	  const auto &info = *m_exprs[i];
	  fprintf (dump_file, "      Expr[%u]: ", i);
	  info.dump (dump_file, "        ");
	}
      fprintf (dump_file, "\n    bitmap data:\n");
      for (const bb_info *bb : crtl->ssa->bbs ())
	{
	  unsigned int i = bb->index ();
	  fprintf (dump_file, "      BB %u:\n", i);
	  fprintf (dump_file, "        avloc: ");
	  dump_bitmap_file (dump_file, m_avloc[i]);
	  fprintf (dump_file, "        kill: ");
	  dump_bitmap_file (dump_file, m_kill[i]);
	  fprintf (dump_file, "        antloc: ");
	  dump_bitmap_file (dump_file, m_antloc[i]);
	  fprintf (dump_file, "        transp: ");
	  dump_bitmap_file (dump_file, m_transp[i]);

	  fprintf (dump_file, "        avin: ");
	  dump_bitmap_file (dump_file, m_avin[i]);
	  fprintf (dump_file, "        avout: ");
	  dump_bitmap_file (dump_file, m_avout[i]);
	  fprintf (dump_file, "        antin: ");
	  dump_bitmap_file (dump_file, antin[i]);
	  fprintf (dump_file, "        antout: ");
	  dump_bitmap_file (dump_file, antout[i]);
	}
      fprintf (dump_file, "\n");
      fprintf (dump_file, "      earliest:\n");
      for (unsigned ed = 0; ed < num_edges; ed++)
	{
	  edge eg = INDEX_EDGE (m_edges, ed);

	  if (bitmap_empty_p (earliest[ed]))
	    continue;
	  fprintf (dump_file, "        Edge(bb %u -> bb %u): ", eg->src->index,
		   eg->dest->index);
	  dump_bitmap_file (dump_file, earliest[ed]);
	}
      fprintf (dump_file, "\n");
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "    Fused global info result (lift %d):\n", iter);
    }

  bool changed = false;
  for (unsigned ed = 0; ed < num_edges; ed++)
    {
      sbitmap e = earliest[ed];
      if (bitmap_empty_p (e))
	continue;

      unsigned int expr_index;
      sbitmap_iterator sbi;
      EXECUTE_IF_SET_IN_BITMAP (e, 0, expr_index, sbi)
	{
	  vsetvl_info &curr_info = *m_exprs[expr_index];
	  edge eg = INDEX_EDGE (m_edges, ed);
	  vsetvl_block_info &src_block_info = get_block_info (eg->src);
	  vsetvl_block_info &dest_block_info = get_block_info (eg->dest);

	  if (!curr_info.valid_p ()
	      || eg->probability == profile_probability::never ()
	      || src_block_info.probability
		   == profile_probability::uninitialized ()
	      /* When multiple set bits in earliest edge, such edge may
		 have infinite loop in preds or succs or multiple conflict
		 vsetvl expression which make such edge is unrelated.  We
		 don't perform fusion for such situation.  */
	      || bitmap_count_bits (e) != 1)
	    continue;

	  if (src_block_info.empty_p ())
	    {
	      vsetvl_info new_curr_info = curr_info;
	      new_curr_info.set_bb (crtl->ssa->bb (eg->dest));
	      bool has_compatible_p
		= has_compatible_reaching_vsetvl_p (new_curr_info);
	      if (!has_compatible_p)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file,
			       "      Forbidden lift up vsetvl info into bb %u "
			       "since there is no vsetvl info that reaching in "
			       "is compatible with it:",
			       eg->src->index);
		      curr_info.dump (dump_file, "        ");
		    }
		  continue;
		}

	      /* We cannot lift a vsetvl into the source block if the block is
		 not transparent WRT to it.
		 This is too restrictive for blocks where a register's use only
		 feeds into vsetvls and no regular insns.  One example is the
		 test rvv/vsetvl/avl_single-68.c which is currently XFAILed for
		 that reason.
		 In order to support this case we'd need to check the vsetvl's
		 AVL operand's uses in the source block and make sure they are
		 only used in other vsetvls.  */
	      if (!bitmap_bit_p (m_transp[eg->src->index], expr_index))
		continue;

	      /* Transparency tells us if we can move upwards without looking
		 down.  It is still possible to clobber non-vsetvl uses
		 that happen to share the vsetvl destination register of the
		 vsetvl we are about to hoist.
		 As we have computed the vsetvl VL dest -> vsetvl AVL reach
		 before, we can check if our VL register is live-in for each
		 successor and not reached by a vsetvl.  If so, we cannot
		 hoist, as that would clobber the use.  */
	      if (curr_info.has_vl ())
		{
		  edge succ;
		  edge_iterator it;
		  bool clobber = false;
		  FOR_EACH_EDGE (succ, it, eg->src->succs)
		    {
		      if (succ->dest == eg->dest)
			continue;
		      if (bitmap_bit_p (df_get_live_in (succ->dest),
					REGNO (curr_info.get_vl ()))
			  && !bitmap_bit_p
			  (m_vsetvl_avl_reach_in[succ->dest->index],
			   REGNO (curr_info.get_vl ())))
			{
			  clobber = true;
			  break;
			}
		    }
		  if (clobber)
		    continue;
		}


	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file,
			   "      Hoisting vsetvl info from bb %u to "
			   "bb %u: ", eg->dest->index, eg->src->index);
		  curr_info.dump (dump_file, "        ");
		}
	      src_block_info.set_info (curr_info);
	      src_block_info.probability = dest_block_info.probability;
	      changed = true;
	    }
	  else if (src_block_info.has_info ())
	    {
	      vsetvl_info &prev_info = src_block_info.get_exit_info ();
	      gcc_assert (prev_info.valid_p ());

	      if (m_dem.compatible_p (prev_info, curr_info))
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "    Fuse curr info since prev info "
					  "compatible with it:\n");
		      fprintf (dump_file, "      prev_info: ");
		      prev_info.dump (dump_file, "        ");
		      fprintf (dump_file, "      curr_info: ");
		      curr_info.dump (dump_file, "        ");
		    }
		  m_dem.merge (prev_info, curr_info);
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "      prev_info after fused: ");
		      prev_info.dump (dump_file, "        ");
		      fprintf (dump_file, "\n");
		    }
		  changed = true;
		  if (src_block_info.has_info ())
		    src_block_info.probability += dest_block_info.probability;
		}
	      else
		{
		  /* Cancel lift up if probabilities are equal.  */
		  if (successors_probability_equal_p (eg->src)
		      || (dest_block_info.probability
			    > src_block_info.probability
			  && !has_compatible_reaching_vsetvl_p (curr_info)))
		    {
		      if (dump_file && (dump_flags & TDF_DETAILS))
			{
			  fprintf (dump_file,
				   "      Reset bb %u:",
				   eg->src->index);
			  prev_info.dump (dump_file, "        ");
			  fprintf (dump_file, "	due to (same probability or no "
					      "compatible reaching):");
			  curr_info.dump (dump_file, "        ");
			}
		      src_block_info.set_empty_info ();
		      src_block_info.probability
			= profile_probability::uninitialized ();
		      /* See PR113696, we should reset immediate dominator to
			 empty since we may uplift ineffective vsetvl which
			 locate at low probability block.  */
		      basic_block dom
			= get_immediate_dominator (CDI_DOMINATORS, eg->src);
		      auto &dom_block_info = get_block_info (dom);
		      if (dom_block_info.has_info ()
			  && !m_dem.compatible_p (
			    dom_block_info.get_exit_info (), curr_info))
			{
			  dom_block_info.set_empty_info ();
			  dom_block_info.probability
			    = profile_probability::uninitialized ();
			  if (dump_file && (dump_flags & TDF_DETAILS))
			    {
			      fprintf (dump_file,
				       "      Reset dominator bb %u:",
				       dom->index);
			      prev_info.dump (dump_file, "        ");
			      fprintf (dump_file,
				       "	due to (same probability or no "
				       "compatible reaching):");
			      curr_info.dump (dump_file, "        ");
			    }
			}
		      changed = true;
		    }
		  /* Choose the one with higher probability. */
		  else if (dest_block_info.probability
			   > src_block_info.probability)
		    {
		      if (dump_file && (dump_flags & TDF_DETAILS))
			{
			  fprintf (dump_file,
				   "      Change bb %u from:",
				   eg->src->index);
			  prev_info.dump (dump_file, "        ");
			  fprintf (dump_file,
				   "        to (higher probability):");
			  curr_info.dump (dump_file, "        ");
			}
		      src_block_info.set_info (curr_info);
		      src_block_info.probability = dest_block_info.probability;
		      changed = true;
		    }
		}
	    }
	  else
	    {
	      vsetvl_info &prev_info = src_block_info.get_exit_info ();
	      if (!prev_info.valid_p ()
		  || m_dem.available_p (prev_info, curr_info)
		  || !m_dem.compatible_p (prev_info, curr_info))
		continue;

	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "    Fuse curr info since prev info "
				      "compatible with it:\n");
		  fprintf (dump_file, "      prev_info: ");
		  prev_info.dump (dump_file, "        ");
		  fprintf (dump_file, "      curr_info: ");
		  curr_info.dump (dump_file, "        ");
		}
	      m_dem.merge (prev_info, curr_info);
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "      prev_info after fused: ");
		  prev_info.dump (dump_file, "        ");
		  fprintf (dump_file, "\n");
		}
	      changed = true;
	    }
	}
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\n");
    }

  sbitmap_vector_free (antin);
  sbitmap_vector_free (antout);
  sbitmap_vector_free (earliest);
  free_edge_list (m_edges);

  return changed;
}

void
pre_vsetvl::pre_global_vsetvl_info ()
{
  compute_vsetvl_def_data ();
  compute_lcm_local_properties ();

  unsigned num_exprs = m_exprs.length ();
  m_edges = pre_edge_lcm_avs (num_exprs, m_transp, m_avloc, m_antloc, m_kill,
			      m_avin, m_avout, &m_insert, &m_del);
  unsigned num_edges = NUM_EDGES (m_edges);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\n  Compute LCM insert and delete data:\n\n");
      fprintf (dump_file, "    Expression List (%u):\n", num_exprs);
      for (unsigned i = 0; i < num_exprs; i++)
	{
	  const auto &info = *m_exprs[i];
	  fprintf (dump_file, "      Expr[%u]: ", i);
	  info.dump (dump_file, "        ");
	}
      fprintf (dump_file, "\n    bitmap data:\n");
      for (const bb_info *bb : crtl->ssa->bbs ())
	{
	  unsigned i = bb->index ();
	  fprintf (dump_file, "      BB %u:\n", i);
	  fprintf (dump_file, "        avloc: ");
	  dump_bitmap_file (dump_file, m_avloc[i]);
	  fprintf (dump_file, "        kill: ");
	  dump_bitmap_file (dump_file, m_kill[i]);
	  fprintf (dump_file, "        antloc: ");
	  dump_bitmap_file (dump_file, m_antloc[i]);
	  fprintf (dump_file, "        transp: ");
	  dump_bitmap_file (dump_file, m_transp[i]);

	  fprintf (dump_file, "        avin: ");
	  dump_bitmap_file (dump_file, m_avin[i]);
	  fprintf (dump_file, "        avout: ");
	  dump_bitmap_file (dump_file, m_avout[i]);
	  fprintf (dump_file, "        del: ");
	  dump_bitmap_file (dump_file, m_del[i]);
	}
      fprintf (dump_file, "\n");
      fprintf (dump_file, "      insert:\n");
      for (unsigned ed = 0; ed < num_edges; ed++)
	{
	  edge eg = INDEX_EDGE (m_edges, ed);

	  if (bitmap_empty_p (m_insert[ed]))
	    continue;
	  fprintf (dump_file, "        Edge(bb %u -> bb %u): ", eg->src->index,
		   eg->dest->index);
	  dump_bitmap_file (dump_file, m_insert[ed]);
	}
    }

  /* Remove vsetvl infos as LCM suggest */
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      sbitmap d = m_del[bb->index ()];
      if (bitmap_count_bits (d) == 0)
	continue;
      gcc_assert (bitmap_count_bits (d) == 1);
      unsigned expr_index = bitmap_first_set_bit (d);
      vsetvl_info &info = *m_exprs[expr_index];
      gcc_assert (info.valid_p ());
      gcc_assert (info.get_bb () == bb);
      const vsetvl_block_info &block_info = get_block_info (info.get_bb ());
      gcc_assert (block_info.get_entry_info () == info);
      info.set_delete ();
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file,
		   "\nLCM deleting vsetvl of block %d, it has predecessors: \n",
		   bb->index ());
	  hash_set<basic_block> all_preds
	    = get_all_predecessors (bb->cfg_bb ());
	  int i = 0;
	  for (const auto pred : all_preds)
	    {
	      fprintf (dump_file, "%d ", pred->index);
	      i++;
	      if (i % 32 == 0)
		fprintf (dump_file, "\n");
	    }
	  fprintf (dump_file, "\n");
	}
    }

  /* Remove vsetvl infos if all predecessors are available to the block.  */
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      vsetvl_block_info &block_info = get_block_info (bb);
      if (block_info.empty_p ())
	continue;
      vsetvl_info &curr_info = block_info.get_entry_info ();
      if (!curr_info.valid_p ())
	continue;

      unsigned int expr_index;
      sbitmap_iterator sbi;
      gcc_assert (
	!bitmap_empty_p (m_vsetvl_def_in[curr_info.get_bb ()->index ()]));
      bool full_available = true;
      EXECUTE_IF_SET_IN_BITMAP (m_vsetvl_def_in[bb->index ()], 0, expr_index,
				sbi)
	{
	  vsetvl_info &prev_info = *m_vsetvl_def_exprs[expr_index];
	  if (!prev_info.valid_p ()
	      || !m_dem.available_p (prev_info, curr_info))
	    {
	      full_available = false;
	      break;
	    }
	}
      if (full_available)
	curr_info.set_delete ();
    }

  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      vsetvl_block_info &block_info = get_block_info (bb);
      if (block_info.empty_p ())
	continue;
      vsetvl_info &curr_info = block_info.get_entry_info ();
      if (curr_info.delete_p ())
	{
	  if (block_info.local_infos.is_empty ())
	    continue;
	  curr_info = block_info.local_infos[0];
	}
      if (curr_info.valid_p () && !curr_info.vl_used_by_non_rvv_insn_p ()
	  && preds_all_same_avl_and_ratio_p (curr_info))
	curr_info.set_change_vtype_only ();

      vsetvl_info prev_info = vsetvl_info ();
      prev_info.set_empty ();
      for (auto &curr_info : block_info.local_infos)
	{
	  if (prev_info.valid_p () && curr_info.valid_p ()
	      && m_dem.avl_available_p (prev_info, curr_info)
	      && prev_info.get_ratio () == curr_info.get_ratio ())
	    curr_info.set_change_vtype_only ();
	  prev_info = curr_info;
	}
    }
}

void
pre_vsetvl::emit_vsetvl ()
{
  bool need_commit = false;

  /* Fake edge is created by connect infinite loops to exit function.
     We should commit vsetvl edge after fake edges removes, otherwise,
     it will cause ICE.  */
  remove_fake_exit_edges ();
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      for (const auto &curr_info : get_block_info (bb).local_infos)
	{
	  insn_info *insn = curr_info.get_insn ();
	  if (curr_info.delete_p ())
	    {
	      if (vsetvl_insn_p (insn->rtl ()))
		remove_vsetvl_insn (curr_info.get_insn ()->rtl ());
	      continue;
	    }
	  else if (curr_info.valid_p ())
	    {
	      if (vsetvl_insn_p (insn->rtl ()))
		{
		  const vsetvl_info temp = vsetvl_info (insn);
		  if (!(curr_info == temp))
		    {
		      if (dump_file)
			{
			  fprintf (dump_file, "\n  Change vsetvl info from: ");
			  temp.dump (dump_file, "    ");
			  fprintf (dump_file, "  to: ");
			  curr_info.dump (dump_file, "    ");
			}
		      change_vsetvl_insn (curr_info);
		    }
		}
	      else
		{
		  if (dump_file)
		    {
		      fprintf (dump_file,
			       "\n  Insert vsetvl info before insn %d: ",
			       insn->uid ());
		      curr_info.dump (dump_file, "    ");
		    }
		  insert_vsetvl_insn (EMIT_BEFORE, curr_info);
		}
	    }
	}
    }

  for (const vsetvl_info &item : m_delete_list)
    {
      gcc_assert (vsetvl_insn_p (item.get_insn ()->rtl ()));
      remove_vsetvl_insn (item.get_insn ()->rtl ());
    }

  /* Insert vsetvl info that was not deleted after lift up.  */
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      const vsetvl_block_info &block_info = get_block_info (bb);
      if (!block_info.has_info ())
	continue;

      const vsetvl_info &footer_info = block_info.get_exit_info ();

      if (footer_info.delete_p ())
	continue;

      edge eg;
      edge_iterator eg_iterator;
      FOR_EACH_EDGE (eg, eg_iterator, bb->cfg_bb ()->succs)
	{
	  gcc_assert (!(eg->flags & EDGE_ABNORMAL));
	  if (dump_file)
	    {
	      fprintf (
		dump_file,
		"\n  Insert missed vsetvl info at edge(bb %u -> bb %u): ",
		eg->src->index, eg->dest->index);
	      footer_info.dump (dump_file, "    ");
	    }
	  start_sequence ();
	  insert_vsetvl_insn (EMIT_DIRECT, footer_info);
	  rtx_insn *rinsn = end_sequence ();
	  default_rtl_profile ();
	  insert_insn_on_edge (rinsn, eg);
	  need_commit = true;
	}
    }

  /* m_insert vsetvl as LCM suggest. */
  for (int ed = 0; ed < NUM_EDGES (m_edges); ed++)
    {
      edge eg = INDEX_EDGE (m_edges, ed);
      sbitmap i = m_insert[ed];
      if (bitmap_count_bits (i) != 1)
	/* For code with infinite loop (e.g. pr61634.c), The data flow is
	   completely wrong.  */
	continue;

      unsigned expr_index = bitmap_first_set_bit (i);
      const vsetvl_info &info = *m_exprs[expr_index];
      gcc_assert (info.valid_p ());
      if (dump_file)
	{
	  fprintf (dump_file,
		   "\n  Insert vsetvl info at edge(bb %u -> bb %u): ",
		   eg->src->index, eg->dest->index);
	  info.dump (dump_file, "    ");
	}
      rtl_profile_for_edge (eg);
      start_sequence ();

      insert_vsetvl_insn (EMIT_DIRECT, info);
      rtx_insn *rinsn = end_sequence ();
      default_rtl_profile ();

      /* We should not get an abnormal edge here.  */
      gcc_assert (!(eg->flags & EDGE_ABNORMAL));
      need_commit = true;
      insert_insn_on_edge (rinsn, eg);
    }

  if (need_commit)
    commit_edge_insertions ();
}

void
pre_vsetvl::cleanup ()
{
  remove_avl_operand ();
  remove_unused_dest_operand ();
  remove_vsetvl_pre_insns ();
}

void
pre_vsetvl::remove_avl_operand ()
{
  basic_block cfg_bb;
  rtx_insn *rinsn;
  FOR_ALL_BB_FN (cfg_bb, cfun)
    FOR_BB_INSNS (cfg_bb, rinsn)
      if (NONDEBUG_INSN_P (rinsn) && has_vl_op (rinsn)
	  && REG_P (get_vl (rinsn)))
	{
	  rtx avl = get_vl (rinsn);
	  if (count_regno_occurrences (rinsn, REGNO (avl)) == 1)
	    {
	      rtx new_pat;
	      if (fault_first_load_p (rinsn))
		new_pat
		  = simplify_replace_rtx (PATTERN (rinsn), avl, const0_rtx);
	      else
		{
		  rtx set = single_set (rinsn);
		  rtx src
		    = simplify_replace_rtx (SET_SRC (set), avl, const0_rtx);
		  rtx new_set = gen_rtx_SET (SET_DEST (set), src);
		  /* If the pattern is a PARALLEL (e.g. microscaled IME
		     patterns with a (use ...) side-effect), preserve the
		     PARALLEL structure and replace only the SET inside.  */
		  rtx pat = PATTERN (rinsn);
		  if (GET_CODE (pat) == PARALLEL)
		    {
		      rtvec vec = rtvec_alloc (XVECLEN (pat, 0));
		      for (int i = 0; i < XVECLEN (pat, 0); i++)
			{
			  rtx elt = XVECEXP (pat, 0, i);
			  RTVEC_ELT (vec, i) = (elt == set) ? new_set : elt;
			}
		      new_pat = gen_rtx_PARALLEL (VOIDmode, vec);
		    }
		  else
		    new_pat = new_set;
		}
	      if (dump_file)
		{
		  fprintf (dump_file, "  Cleanup insn %u's avl operand:\n",
			   INSN_UID (rinsn));
		  print_rtl_single (dump_file, rinsn);
		}
	      validate_change_or_fail (rinsn, &PATTERN (rinsn), new_pat, false);
	    }
	}
}

void
pre_vsetvl::remove_unused_dest_operand ()
{
  df_analyze ();
  basic_block cfg_bb;
  rtx_insn *rinsn;
  FOR_ALL_BB_FN (cfg_bb, cfun)
    FOR_BB_INSNS (cfg_bb, rinsn)
      if (NONDEBUG_INSN_P (rinsn) && vsetvl_insn_p (rinsn))
	{
	  rtx vl = get_vl (rinsn);
	  vsetvl_info info = vsetvl_info (rinsn);
	  /* IME vsetvls are register-form sequences; rewriting them
	     in place via validate_change_or_fail is not supported.  */
	  if (info.ime_required_p ())
	    continue;
	  if (has_no_uses (cfg_bb, rinsn, REGNO (vl)))
	    if (!info.has_vlmax_avl ())
	      {
		rtx new_pat = info.get_vsetvl_pat (true);
		if (dump_file)
		  {
		    fprintf (dump_file,
			     "  Remove vsetvl insn %u's dest(vl) operand since "
			     "it unused:\n",
			     INSN_UID (rinsn));
		    print_rtl_single (dump_file, rinsn);
		  }
		validate_change_or_fail (rinsn, &PATTERN (rinsn), new_pat,
					 false);
	      }
	}
}

/* Remove all bogus vsetvl_pre instructions.  */
void
pre_vsetvl::remove_vsetvl_pre_insns ()
{
  basic_block cfg_bb;
  rtx_insn *rinsn;
  FOR_ALL_BB_FN (cfg_bb, cfun)
    FOR_BB_INSNS (cfg_bb, rinsn)
      if (NONDEBUG_INSN_P (rinsn) && vsetvl_pre_insn_p (rinsn))
	{
	  if (dump_file)
	    {
	      fprintf (dump_file, "  Eliminate vsetvl_pre insn %d:\n",
		       INSN_UID (rinsn));
	      print_rtl_single (dump_file, rinsn);
	    }
	  remove_vsetvl_insn (rinsn);
	}
}

const pass_data pass_data_vsetvl = {
  RTL_PASS,	 /* type */
  "vsetvl",	 /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_MACH_DEP,	 /* tv_id */
  0,		 /* properties_required */
  0,		 /* properties_provided */
  0,		 /* properties_destroyed */
  0,		 /* todo_flags_start */
  0,		 /* todo_flags_finish */
};

class pass_vsetvl : public rtl_opt_pass
{
private:
  void simple_vsetvl ();
  void lazy_vsetvl ();

public:
  pass_vsetvl (gcc::context *ctxt) : rtl_opt_pass (pass_data_vsetvl, ctxt) {}

  /* opt_pass methods: */
  virtual bool gate (function *) final override { return TARGET_VECTOR; }
  virtual unsigned int execute (function *) final override;
}; // class pass_vsetvl

void
pass_vsetvl::simple_vsetvl ()
{
  if (dump_file)
    fprintf (dump_file, "\nEntering Simple VSETVL PASS\n");

  basic_block cfg_bb;
  rtx_insn *rinsn;
  FOR_ALL_BB_FN (cfg_bb, cfun)
    {
      FOR_BB_INSNS (cfg_bb, rinsn)
	{
	  if (!NONDEBUG_INSN_P (rinsn))
	    continue;
	  if (has_vtype_op (rinsn))
	    {
	      const auto &info = vsetvl_info (rinsn);
	      if (info.ime_required_p ())
		{
		  rtx avl = info.get_avl () ? info.get_avl () : const0_rtx;
		  /* Normalise const_int AVL into a register -- same
		     fix as in insert_ime_vsetvl_insn above.  */
		  if (CONST_INT_P (avl) && avl != const0_rtx)
		    {
		      /* Use t3 (x28): outside the scratch set used by
			 the runtime vsetvl sequence.  */
		      rtx avl_reg = gen_rtx_REG (Pmode, GP_REG_FIRST + 28);
		      emit_insn_before (gen_rtx_SET (avl_reg, avl), rinsn);
		      avl = avl_reg;
		    }
		  /* Same reason as in insert_ime_vsetvl_insn -- force
		     PROLOGUE_TEMP (t0) for dest_vl.  */
		  rtx dest_vl
		    = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP_REGNUM);
		  rtx pat;
		  if (info.get_ime_req ().lambda_log2 == -1)
		    {
		      pat = emit_ime_vtype_runtime_pat (info, dest_vl, avl);
		      if (pat == NULL_RTX)
			continue;  /* sorry() already reported.  */
		    }
		  else
		    {
		      rtx vtype_const = emit_ime_vtype_build (info);
		      if (vtype_const == NULL_RTX)
			continue;  /* sorry() / error() already reported.  */
		      pat = gen_vsetvl_ime (Pmode, dest_vl, avl, vtype_const);
		    }
		  emit_insn_before (pat, rinsn);
		  if (dump_file)
		    fprintf (dump_file,
			     "  Insert IME vsetvl insn before insn %d\n",
			     INSN_UID (rinsn));
		  continue;
		}
	      rtx pat = info.get_vsetvl_pat ();
	      emit_insn_before (pat, rinsn);
	      if (dump_file)
		{
		  fprintf (dump_file, "  Insert vsetvl insn before insn %d:\n",
			   INSN_UID (rinsn));
		  print_rtl_single (dump_file, PREV_INSN (rinsn));
		}
	    }
	}
    }

  if (dump_file)
    fprintf (dump_file, "\nEmit missing read_vl()s for fault-only-first "
	     "loads\n");
  emit_fof_read_vls ();
}

/* Lazy vsetvl insertion for optimize > 0. */
void
pass_vsetvl::lazy_vsetvl ()
{
  if (dump_file)
    fprintf (dump_file, "\nEntering Lazy VSETVL PASS\n\n");

  pre_vsetvl pre = pre_vsetvl ();

  if (dump_file)
    fprintf (dump_file, "\nPhase 1: Fuse local vsetvl infos.\n\n");
  pre.fuse_local_vsetvl_info ();
  if (dump_file && (dump_flags & TDF_DETAILS))
    pre.dump (dump_file, "phase 1");

  /* Phase 2:  Fuse header and footer vsetvl infos between basic blocks.  */
  if (dump_file)
    fprintf (dump_file, "\nPhase 2: Lift up vsetvl info.\n\n");
  if (vsetvl_strategy != VSETVL_OPT_NO_FUSION)
    {
      bool changed = true;
      int fused_count = 0;
      do
	{
	  if (dump_file)
	    fprintf (dump_file, "  Try lift up %d.\n\n", fused_count);
	  changed = pre.earliest_fuse_vsetvl_info (fused_count);
	  fused_count += 1;
      } while (changed);
    }
  if (dump_file && (dump_flags & TDF_DETAILS))
    pre.dump (dump_file, "phase 2");

  /* Phase 3: Reducing redundant vsetvl infos using LCM.  */
  if (dump_file)
    fprintf (dump_file, "\nPhase 3: Reduce global vsetvl infos.\n\n");
  pre.pre_global_vsetvl_info ();
  if (dump_file && (dump_flags & TDF_DETAILS))
    pre.dump (dump_file, "phase 3");

  /* Phase 4: Insert, modify and remove vsetvl insns.  */
  if (dump_file)
    fprintf (dump_file,
	     "\nPhase 4: Insert, modify and remove vsetvl insns.\n\n");
  pre.emit_vsetvl ();

  /* Phase 4b: Emit read_vl for fault-only-first loads with VL output
     register.  */
  if (dump_file)
    fprintf (dump_file, "\nPhase 4b: Emit missing read_vl()s for "
	     "fault-only-first loads\n");
  emit_fof_read_vls ();

  /* Phase 5: Cleanup */
  if (dump_file)
    fprintf (dump_file, "\nPhase 5: Cleanup\n\n");
  pre.cleanup ();

  pre.finish ();
}

/* Main entry point for this pass.  */
unsigned int
pass_vsetvl::execute (function *)
{
  if (n_basic_blocks_for_fn (cfun) <= 0)
    return 0;

  /* The RVV instruction may change after split which is not a stable
     instruction. We need to split it here to avoid potential issue
     since the VSETVL PASS is insert before split PASS.  */
  split_all_insns ();

  /* Early return for there is no vector instructions.  */
  if (!has_vector_insn (cfun))
    return 0;

  if (!optimize || vsetvl_strategy == VSETVL_SIMPLE)
    simple_vsetvl ();
  else
    lazy_vsetvl ();

  return 0;
}

rtl_opt_pass *
make_pass_vsetvl (gcc::context *ctxt)
{
  return new pass_vsetvl (ctxt);
}
