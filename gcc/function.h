/* Structure for saving state for a nested function.
   Copyright (C) 1989, 92-97, 1998 Free Software Foundation, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */


#if !defined(NULL_TREE) && !defined(tree)
typedef union union_node *_function_tree;
#define tree _function_tree
#endif
#if !defined(NULL_RTX) && !defined(rtx)
typedef struct rtx_def *_function_rtx;
#define rtx _function_rtx
#endif

/* Stack of pending (incomplete) sequences saved by `start_sequence'.
   Each element describes one pending sequence.
   The main insn-chain is saved in the last element of the chain,
   unless the chain is empty.  */

struct sequence_stack
{
  /* First and last insns in the chain of the saved sequence.  */
  rtx first, last;
  tree sequence_rtl_expr;
  struct sequence_stack *next;
};

extern struct sequence_stack *sequence_stack;

/* Stack of single obstacks.  */

struct simple_obstack_stack
{
  struct obstack *obstack;
  struct simple_obstack_stack *next;
};

struct emit_status
{
  /* This is reset to LAST_VIRTUAL_REGISTER + 1 at the start of each function.
     After rtl generation, it is 1 plus the largest register number used.  */
  int x_reg_rtx_no;

  /* Lowest label number in current function.  */
  int x_first_label_num;

  /* The ends of the doubly-linked chain of rtl for the current function.
     Both are reset to null at the start of rtl generation for the function.
   
     start_sequence saves both of these on `sequence_stack' along with
     `sequence_rtl_expr' and then starts a new, nested sequence of insns.  */
  rtx x_first_insn;
  rtx x_last_insn;

  /* RTL_EXPR within which the current sequence will be placed.  Use to
     prevent reuse of any temporaries within the sequence until after the
     RTL_EXPR is emitted.  */
  tree sequence_rtl_expr;

  /* Stack of pending (incomplete) sequences saved by `start_sequence'.
     Each element describes one pending sequence.
     The main insn-chain is saved in the last element of the chain,
     unless the chain is empty.  */
  struct sequence_stack *sequence_stack;

  /* INSN_UID for next insn emitted.
     Reset to 1 for each function compiled.  */
  int x_cur_insn_uid;

  /* Line number and source file of the last line-number NOTE emitted.
     This is used to avoid generating duplicates.  */
  int x_last_linenum;
  char *x_last_filename;

  /* A vector indexed by pseudo reg number.  The allocated length
     of this vector is regno_pointer_flag_length.  Since this
     vector is needed during the expansion phase when the total
     number of registers in the function is not yet known,
     it is copied and made bigger when necessary.  */
  char *x_regno_pointer_flag;
  int x_regno_pointer_flag_length;

  /* Indexed by pseudo register number, if nonzero gives the known alignment
     for that pseudo (if regno_pointer_flag is set).
     Allocated in parallel with regno_pointer_flag.  */
  char *x_regno_pointer_align;

  /* Indexed by pseudo register number, gives the rtx for that pseudo.
     Allocated in parallel with regno_pointer_flag.  */
  rtx *x_regno_reg_rtx;
};

/* For backward compatibility... eventually these should all go away.  */
#define reg_rtx_no (current_function->emit->x_reg_rtx_no)
#define seq_rtl_expr (current_function->emit->sequence_rtl_expr)
#define regno_pointer_flag (current_function->emit->x_regno_pointer_flag)
#define regno_pointer_flag_length (current_function->emit->x_regno_pointer_flag_length)
#define regno_pointer_align (current_function->emit->x_regno_pointer_align)
#define regno_reg_rtx (current_function->emit->x_regno_reg_rtx)
#define seq_stack (current_function->emit->sequence_stack)

#define REGNO_POINTER_ALIGN(REGNO) regno_pointer_align[REGNO]
#define REGNO_POINTER_FLAG(REGNO) regno_pointer_flag[REGNO]

struct expr_status
{
  /* Number of units that we should eventually pop off the stack.
     These are the arguments to function calls that have already returned.  */
  int x_pending_stack_adjust;

  /* Nonzero means stack pops must not be deferred, and deferred stack
     pops must not be output.  It is nonzero inside a function call,
     inside a conditional expression, inside a statement expression,
     and in other cases as well.  */
  int x_inhibit_defer_pop;

  /* Nonzero means __builtin_saveregs has already been done in this function.
     The value is the pseudoreg containing the value __builtin_saveregs
     returned.  */
  rtx x_saveregs_value;

  /* Similarly for __builtin_apply_args.  */
  rtx x_apply_args_value;

  /* List of labels that must never be deleted.  */
  rtx x_forced_labels;
};

#define pending_stack_adjust (current_function->expr->x_pending_stack_adjust)
#define inhibit_defer_pop (current_function->expr->x_inhibit_defer_pop)
#define saveregs_value (current_function->expr->x_saveregs_value)
#define apply_args_value (current_function->expr->x_apply_args_value)
#define forced_labels (current_function->expr->x_forced_labels)

/* This structure can save all the important global and static variables
   describing the status of the current function.  */

struct function
{
  /* Global list of all functions.  */
  struct function *next_global;

  /* Chain of nested functions.  */
  struct function *next;

  /* The FUNCTION_DECL for this function.  */
  tree decl;

  /* Name of function now being compiled.  */
  char *name;

  /* Number of bytes of args popped by function being compiled on its return.
     Zero if no bytes are to be popped.
     May affect compilation of return insn or of function epilogue.  */
  int pops_args;

  /* Nonzero if function being compiled needs to be given an address
     where the value should be stored.  */
  int returns_struct;

  /* Nonzero if function being compiled needs to
     return the address of where it has put a structure value.  */
  int returns_pcc_struct;

  /* Nonzero if the current function returns a pointer type */
  int returns_pointer;

  /* Nonzero if function being compiled needs to be passed a static chain.  */
  int needs_context;

  /* Nonzero if function being compiled can call setjmp.  */
  int calls_setjmp;

  /* Nonzero if function being compiled can call longjmp.  */
  int calls_longjmp;

  /* Nonzero if function being compiled can call alloca,
     either as a subroutine or builtin.  */
  int calls_alloca;

  /* Nonzero if function being compiled contains nested functions.  */
  int contains_functions;

  /* Nonzero if the current function is a thunk (a lightweight function that
     just adjusts one of its arguments and forwards to another function), so
     we should try to cut corners where we can.  */
  int is_thunk;

  /* If function's args have a fixed size, this is that size, in bytes.
     Otherwise, it is -1.
     May affect compilation of return insn or of function epilogue.  */
  int args_size;
  
  /* # bytes the prologue should push and pretend that the caller pushed them.
     The prologue must do this, but only if parms can be passed in registers.  */
  int pretend_args_size;

  /* This is the offset from the arg pointer to the place where the first
     anonymous arg can be found, if there is one.  */
  rtx arg_offset_rtx;

  /* Nonzero if current function uses varargs.h or equivalent.
     Zero for functions that use stdarg.h.  */
  int varargs;
  
  /* Nonzero if current function uses stdarg.h or equivalent.
     Zero for functions that use varargs.h.  */
  int stdarg;

  /* The arg pointer hard register, or the pseudo into which it was copied.  */
  rtx internal_arg_pointer;

  /* Language-specific reason why the current function cannot be made inline.  */
  char *cannot_inline;
  
  /* # of bytes of outgoing arguments.  If ACCUMULATE_OUTGOING_ARGS is
     defined, the needed space is pushed by the prologue.  */
  int outgoing_args_size;

  /* If non-zero, an RTL expression for the location at which the current 
     function returns its result.  If the current function returns its
     result in a register, current_function_return_rtx will always be
     the hard register containing the result.  */
  rtx return_rtx;

  /* Nonzero if the current function uses pic_offset_table_rtx.  */
  int uses_pic_offset_table;

  /* Nonzero if the current function uses the constant pool.  */
  int uses_const_pool;

  /* If some insns can be deferred to the delay slots of the epilogue, the
     delay list for them is recorded here.  */
  rtx epilogue_delay_list;

  /* Quantities of various kinds of registers
     used for the current function's args.  */
  CUMULATIVE_ARGS args_info;
  
  /* Nonzero if instrumentation calls for function entry and exit should be
     generated.  */
  int instrument_entry_exit;

  /* 1 + last pseudo register number possibly used for loading a copy
     of a parameter of this function. */
  int saved_max_parm_reg;

  /* Vector indexed by REGNO, containing location on stack in which
     to put the parm which is nominally in pseudo register REGNO,
     if we discover that that parm must go in the stack.  The highest
     element in this vector is one less than MAX_PARM_REG, above.  */
  rtx *saved_parm_reg_stack_loc;

  /* Label that will go on parm cleanup code, if any.
     Jumping to this label runs cleanup code for parameters, if
     such code must be run.  Following this code is the logical return label.  */
  rtx saved_cleanup_label;

  /* Label that will go on function epilogue.
     Jumping to this label serves as a "return" instruction
     on machines which require execution of the epilogue on all returns.  */
  rtx saved_return_label;

  /* List (chain of EXPR_LISTs) of pseudo-regs of SAVE_EXPRs.
     So we can mark them all live at the end of the function, if nonopt.  */
  rtx saved_save_expr_regs;

  /* List (chain of EXPR_LISTs) of all stack slots in this function.
     Made for the sake of unshare_all_rtl.  */
  rtx saved_stack_slot_list;

  /* Insn after which register parms and SAVE_EXPRs are born, if nonopt.  */
  rtx saved_parm_birth_insn;

  /* Offset to end of allocated area of stack frame.
     If stack grows down, this is the address of the last stack slot allocated.
     If stack grows up, this is the address for the next slot.  */
  HOST_WIDE_INT saved_frame_offset;

  /* Label to jump back to for tail recursion, or 0 if we have
     not yet needed one for this function.  */
  rtx saved_tail_recursion_label;

  /* Place after which to insert the tail_recursion_label if we need one.  */
  rtx saved_tail_recursion_reentry;

  /* Location at which to save the argument pointer if it will need to be
     referenced.  There are two cases where this is done: if nonlocal gotos
     exist, or if vars stored at an offset from the argument pointer will be
     needed by inner routines.  */
  rtx saved_arg_pointer_save_area;

  /* Chain of all RTL_EXPRs that have insns in them.  */
  tree saved_rtl_expr_chain;

  /* Last insn of those whose job was to put parms into their nominal homes.  */
  rtx saved_last_parm_insn;

  /* List (chain of TREE_LISTs) of static chains for containing functions.
     Each link has a FUNCTION_DECL in the TREE_PURPOSE and a reg rtx
     in an RTL_EXPR in the TREE_VALUE.  */
  tree saved_context_display;

  /* List (chain of TREE_LISTs) of trampolines for nested functions.
     The trampoline sets up the static chain and jumps to the function.
     We supply the trampoline's address when the function's address is requested.

     Each link has a FUNCTION_DECL in the TREE_PURPOSE and a reg rtx
     in an RTL_EXPR in the TREE_VALUE.  */
  tree saved_trampoline_list;

  /* Number of function calls seen so far in current function.  */
  int saved_function_call_count;

  /* List of all temporaries allocated, both available and in use.  */
  struct temp_slot *saved_temp_slots;

  /* Current nesting level for temporaries.  */
  int saved_temp_slot_level;

  /* When temporaries are created by TARGET_EXPRs, they are created at
     this level of temp_slot_level, so that they can remain allocated
     until no longer needed.  CLEANUP_POINT_EXPRs define the lifetime
     of TARGET_EXPRs.  */
  int saved_target_temp_slot_level;

  /* Current nesting level for variables in a block.  */
  int saved_var_temp_slot_level;

  /* This slot is initialized as 0 and is added to
     during the nested function.  */
  struct var_refs_queue *saved_fixup_var_refs_queue;

  /* RTX for stack slot that holds the current handler for nonlocal gotos.
     Zero when function does not have nonlocal labels.  */
  rtx saved_nonlocal_goto_handler_slot;

  /* RTX for stack slot that holds the stack pointer value to restore
     for a nonlocal goto.
     Zero when function does not have nonlocal labels.  */
  rtx saved_nonlocal_goto_stack_level;

  /* List (chain of TREE_LIST) of LABEL_DECLs for all nonlocal labels
     (labels to which there can be nonlocal gotos from nested functions)
     in this function.  */
  tree saved_nonlocal_labels;

  /* Nonzero if function being compiled receives nonlocal gotos
     from nested functions.  */
  int has_nonlocal_label;

  /* Nonzero if function being compiled has nonlocal gotos to parent
     function.  */
  int has_nonlocal_goto;

  struct stmt_status *stmt;
  struct eh_status *eh;
  struct emit_status *emit;
  struct expr_status *expr;

  /* For tree.c.  */
  int all_types_permanent;
  struct momentary_level *momentary_stack;
  char *maybepermanent_firstobj;
  char *temporary_firstobj;
  char *momentary_firstobj;
  char *momentary_function_firstobj;
  struct obstack *current_obstack;
  struct obstack *function_obstack;
  struct obstack *function_maybepermanent_obstack;
  struct obstack *expression_obstack;
  struct obstack *saveable_obstack;
  struct obstack *rtl_obstack;
  struct simple_obstack_stack *inline_obstacks;

  /* tm.h can use this to store whatever it likes.  */
  struct machine_function *machine;

  /* Language-specific code can use this to store whatever it likes.  */
  struct language_function *language;

  /* For varasm.  */
  struct constant_descriptor **const_rtx_hash_table;
  struct pool_sym **const_rtx_sym_hash_table;
  struct pool_constant *first_pool, *last_pool;
  int pool_offset;
  rtx const_double_chain;

  /* For inlinable functions.  */
  int inlinable;
  /* This is in fact an rtvec.  */
  void *original_arg_vector;
  tree original_decl_initial;
  /* Highest label number in current function.  */
  int max_label_num;

  /* This is nonzero once this function has been compiled and its data is
     no longer required to be kept around.  */
  int can_garbage_collect;
};

/* The function structure for the currently being compiled function.  */
extern struct function *current_function;

/* For backward compatibility... eventually these should all go away.  */
#define current_function_name (current_function->name)
#define current_function_pops_args (current_function->pops_args)
#define current_function_returns_struct (current_function->returns_struct)
#define current_function_returns_pcc_struct (current_function->returns_pcc_struct)
#define current_function_needs_context (current_function->needs_context)
#define current_function_calls_setjmp (current_function->calls_setjmp)
#define current_function_calls_longjmp (current_function->calls_longjmp)
#define current_function_contains_functions (current_function->contains_functions)
#define current_function_is_thunk (current_function->is_thunk)
#define current_function_calls_alloca (current_function->calls_alloca)
#define current_function_returns_pointer (current_function->returns_pointer)
#define current_function_args_info (current_function->args_info)
#define current_function_args_size (current_function->args_size)
#define current_function_pretend_args_size (current_function->pretend_args_size)
#define current_function_outgoing_args_size (current_function->outgoing_args_size)
#define current_function_arg_offset_rtx (current_function->arg_offset_rtx)
#define current_function_varargs (current_function->varargs)
#define current_function_stdarg (current_function->stdarg)
#define current_function_internal_arg_pointer (current_function->internal_arg_pointer)
#define current_function_return_rtx (current_function->return_rtx)
#define current_function_instrument_entry_exit (current_function->instrument_entry_exit)
#define current_function_uses_pic_offset_table (current_function->uses_pic_offset_table)
#define current_function_uses_const_pool (current_function->uses_const_pool)
#define current_function_cannot_inline (current_function->cannot_inline)
#define current_function_epilogue_delay_list (current_function->epilogue_delay_list)
#define current_function_has_nonlocal_label (current_function->has_nonlocal_label)
#define current_function_has_nonlocal_goto (current_function->has_nonlocal_goto)

#define max_parm_reg (current_function->saved_max_parm_reg)
#define parm_reg_stack_loc (current_function->saved_parm_reg_stack_loc)
#define cleanup_label (current_function->saved_cleanup_label)
#define return_label (current_function->saved_return_label)
#define save_expr_regs (current_function->saved_save_expr_regs)
#define stack_slot_list (current_function->saved_stack_slot_list)
#define parm_birth_insn (current_function->saved_parm_birth_insn)
#define frame_offset (current_function->saved_frame_offset)
#define tail_recursion_label (current_function->saved_tail_recursion_label)
#define tail_recursion_reentry (current_function->saved_tail_recursion_reentry)
#define arg_pointer_save_area (current_function->saved_arg_pointer_save_area)
#define rtl_expr_chain (current_function->saved_rtl_expr_chain)
#define last_parm_insn (current_function->saved_last_parm_insn)
#define context_display (current_function->saved_context_display)
#define trampoline_list (current_function->saved_trampoline_list)
#define function_call_count (current_function->saved_function_call_count)
#define temp_slots (current_function->saved_temp_slots)
#define temp_slot_level (current_function->saved_temp_slot_level)
#define target_temp_slot_level (current_function->saved_target_temp_slot_level)
#define var_temp_slot_level (current_function->saved_var_temp_slot_level)
#define fixup_var_refs_queue (current_function->saved_fixup_var_refs_queue)
#define nonlocal_labels (current_function->saved_nonlocal_labels)
#define nonlocal_goto_handler_slot (current_function->saved_nonlocal_goto_handler_slot)
#define nonlocal_goto_stack_level (current_function->saved_nonlocal_goto_stack_level)

/* The FUNCTION_DECL for an inline function currently being expanded.  */
extern tree inline_function_decl;

/* Given a function decl for a containing function,
   return the `struct function' for it.  */
struct function *find_function_data	PROTO((tree));

/* Pointer to chain of `struct function' for containing functions.  */
extern struct function *outer_function_chain;

/* Put all this function's BLOCK nodes into a vector and return it.
   Also store in each NOTE for the beginning or end of a block
   the index of that block in the vector.  */
extern tree *identify_blocks		PROTO((tree, rtx));

/* Return size needed for stack frame based on slots so far allocated.
   This size counts from zero.  It is not rounded to STACK_BOUNDARY;
   the caller may have to do that.  */
extern HOST_WIDE_INT get_frame_size	PROTO((void));
extern HOST_WIDE_INT get_func_frame_size	PROTO((struct function *));

/* These variables hold pointers to functions to
   save and restore machine-specific data,
   in push_function_context and pop_function_context.  */
extern void (*init_machine_status)	PROTO((struct function *));
extern void (*restore_machine_status)	PROTO((struct function *));
extern void (*mark_machine_status)	PROTO((struct function *));

/* Likewise, but for language-specific data.  */
extern void (*save_lang_status)		PROTO((struct function *));
extern void (*restore_lang_status)	PROTO((struct function *));
extern void (*mark_lang_status)		PROTO((struct function *));

/* Save and restore status information for a nested function.  */
extern void save_tree_status		PROTO((struct function *, tree));
extern void restore_tree_status		PROTO((struct function *, tree));
extern void save_varasm_status		PROTO((struct function *, tree));
extern void restore_varasm_status	PROTO((struct function *));
extern void restore_emit_status		PROTO((struct function *));
extern void free_varasm_status		PROTO((struct function *));
extern void free_emit_status		PROTO((struct function *));
extern void free_after_compilation	PROTO((struct function *));

extern rtx get_first_block_beg		PROTO((void));

#ifdef rtx
#undef rtx
#endif

#ifdef tree
#undef tree
#endif
