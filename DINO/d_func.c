/*
   Copyright (C) 1997-2015 Vladimir Makarov.

   Written by Vladimir Makarov <vmakarov@gcc.gnu.org>

   This file is part of interpreter of DINO.

   This is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This software is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GNU CC; see the file COPYING.  If not, write to the Free
   Software Foundation, 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.

*/

#include "d_run.h"
#include "d_built.h"
#include "d_conv.h"
#include "d_func.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

#define NEW_VECTOR

#define below_ctop (IVAL (ctop, -1))

/* Value of parameter of FROM_C_CODE_P for the last builtin call.  */
static int curr_from_c_code_p;

/* Where to put the standard function result.  */
static ER_node_t fun_result;

/* PC of implementation function call */
d_restrict pc_t ifun_call_pc;

static char *
getun (void)
{
  struct passwd *p;

  p = getpwuid (getuid ());
#ifdef __CYGWIN__
  if (p == NULL)
    return "Unknown";
#endif
  return p->pw_name;
}

static char *
geteun (void)
{
  struct passwd *p;

  p = getpwuid (geteuid ());
#ifdef __CYGWIN__
  if (p == NULL)
    return "Unknown";
#endif
  return p->pw_name;
}

static char *
getgn (void)
{
  struct group *p;

  p = getgrgid (getgid ());
  if (p == NULL)
    return "Unknown";
  return p->gr_name;
}

static char *
getegn (void)
{
  struct group *p;

  p = getgrgid (getegid ());
  if (p == NULL)
    return "Unknown";
  return p->gr_name;
}

#ifdef HAVE_TIME_H
#include <time.h>
#else
extern clock_t clock (void);
#endif

#ifdef AIX_DLOPEN
#include "d_aixdl.c"
#endif

static ER_node_t create_class_stack (BC_node_t class_block, ER_node_t context,
				     val_t *actuals_start, int actuals_num,
				     int simple_p);

static void
min_max_call (int pars_number, int min_flag)
{
  ER_node_t val, r, v;
  int i;
  val_t res;

  if (pars_number < 2)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, (min_flag ? MIN_NAME : MAX_NAME));
  for (i = 0; i < pars_number; i++)
    {
      implicit_arithmetic_conversion (IVAL (ctop, -i), NULL);
      val = IVAL (ctop, -i);
      if (ER_NODE_MODE (val) != ER_NM_int
	  && ER_NODE_MODE (val) != ER_NM_long
	  && ER_NODE_MODE (val) != ER_NM_float)
	eval_error (partype_bc_decl, call_pos (),
		    DERR_parameter_type, (min_flag ? MIN_NAME : MAX_NAME));
      if (i == 0)
	{
	  res = *(val_t *) val;
	  continue;
	}
      if (ER_NODE_MODE (val) == ER_NODE_MODE ((ER_node_t) &res))
	{
	  r = (ER_node_t) &res;
	  v = val;
	}
      else
	{
	  implicit_conversion_for_binary_arithmetic_op
	    ((ER_node_t) &res, val, &r, &v);
	  d_assert (ER_NODE_MODE (r) != ER_NM_int
		    && ER_NODE_MODE (v) != ER_NM_int);
	}
      d_assert (ER_NODE_MODE (r) == ER_NODE_MODE (v));
      if (ER_NODE_MODE (v) == ER_NM_int)
	{
	  if ((ER_i (v) < ER_i (r)) == min_flag)
	    res = *(val_t *) val;
	}
      else if (ER_NODE_MODE (v) == ER_NM_long)
	{
	  if ((mpz_cmp (*ER_mpz_ptr (ER_l (v)),
			*ER_mpz_ptr (ER_l (r))) < 0) == min_flag)
	    res = *(val_t *) val;
	}
      else if ((ER_f (v) < ER_f (r)) == min_flag)
	res = *(val_t *) val;
    }
  *(val_t *) fun_result = res;
}

void
min_call (int pars_number)
{
  min_max_call (pars_number, TRUE);
}

void
max_call (int pars_number)
{
  min_max_call (pars_number, FALSE);
}

static void
to_lower_upper (int pars_number, int lower_flag)
{
  ER_node_t vect;
  const char *name = (lower_flag ? TOLOWER_NAME : TOUPPER_NAME);
  char *str;
  size_t len;

  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (), DERR_parameters_number, name);
  to_vect_string_conversion (ctop, NULL, NULL);
  if (ER_NODE_MODE (ctop) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (ctop)) != ER_NM_heap_pack_vect
      || ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_byte) // ??? char
    eval_error (partype_bc_decl, call_pos (), DERR_parameter_type, name);
  vect = ER_vect (ctop);
#ifndef NEW_VECTOR
  if (ER_immutable (ER_vect (ctop)))
    eval_error (immutable_bc_decl, invaccesses_bc_decl, call_pos (),
		DERR_immutable_vector_modification);
#endif
  len = strlen (ER_pack_els (vect));
#ifdef NEW_VECTOR
  vect = create_empty_string (len + 1);
  strcpy (ER_pack_els (vect), ER_pack_els (ER_vect (ctop)));
  ER_set_els_number (vect, len);
#endif
  for (str = ER_pack_els (vect); *str != 0; str++)
    if (isalpha (*str))
      *str = (lower_flag ? tolower (*str) : toupper (*str));
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

void
toupper_call (int pars_number)
{
  to_lower_upper (pars_number, FALSE);
}

void
tolower_call (int pars_number)
{
  to_lower_upper (pars_number, TRUE);
}

void
translit_call (int pars_number)
{
  ER_node_t vect, v;
  char *str, *subst, map [256];
  int i;
  size_t len;

  if (pars_number != 3)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, TRANSLIT_NAME);
  to_vect_string_conversion (IVAL (ctop, -2), NULL, NULL);
  if (ER_NODE_MODE (ctop) == ER_NM_vect)
    {
      v = ER_vect (ctop);
      GO_THROUGH_REDIR (v);
      try_full_pack (v);
      ER_set_vect (ctop, v);
    }
  if (ER_NODE_MODE (below_ctop) == ER_NM_vect)
    {
      v = ER_vect (below_ctop);
      GO_THROUGH_REDIR (v);
      try_full_pack (v);
      ER_set_vect (below_ctop, v);
    }
  vect = IVAL (ctop, -2);
  if (ER_NODE_MODE (vect) == ER_NM_vect)
    {
      v = ER_vect (vect);
      GO_THROUGH_REDIR (v);
      try_full_pack (v);
      ER_set_vect (vect, v);
    }
  if (ER_NODE_MODE (ctop) != ER_NM_vect
      || (ER_els_number (ER_vect (ctop)) != 0
	  && (ER_NODE_MODE (ER_vect (ctop)) != ER_NM_heap_pack_vect
	      || ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_byte))
      || ER_NODE_MODE (below_ctop) != ER_NM_vect
      || (ER_els_number (ER_vect (below_ctop)) != 0
	  && (ER_NODE_MODE (ER_vect (below_ctop)) != ER_NM_heap_pack_vect
	      || ER_pack_vect_el_mode (ER_vect (below_ctop)) != ER_NM_byte))
      || ER_els_number (ER_vect (ctop)) != ER_els_number (ER_vect (below_ctop))
      || ER_NODE_MODE (vect) != ER_NM_vect
      || (ER_els_number (ER_vect (vect)) != 0
	  && (ER_NODE_MODE (ER_vect (vect)) != ER_NM_heap_pack_vect
	      || ER_pack_vect_el_mode (ER_vect (vect)) != ER_NM_byte)))
    eval_error (partype_bc_decl, call_pos (),
		DERR_parameter_type, TRANSLIT_NAME);
  vect = ER_vect (vect);
#ifndef NEW_VECTOR
  if (ER_immutable (vect))
    eval_error (immutable_bc_decl, invaccesses_bc_decl, call_pos (),
		DERR_immutable_vector_modification);
#endif
  len = ER_els_number (vect);
#ifdef NEW_VECTOR
  vect = create_empty_string (len);
  ER_set_els_number (vect, len);
#endif
  if (len != 0 && ER_els_number (ER_vect (ctop)) != 0)
    {
#ifdef NEW_VECTOR
      memcpy (ER_pack_els (vect),
	      ER_pack_els (ER_vect (IVAL (ctop, -2))), len);
#endif
      for (i = 0; i < 256; i++)
	map [i] = i;
      subst = ER_pack_els (ER_vect (ctop));
      for (i = 0, str = ER_pack_els (ER_vect (below_ctop));
	   i < ER_els_number (ER_vect (below_ctop));
	   i++)
	map [(unsigned) str [i]] = subst [i];
      for (str = ER_pack_els (vect), i = 0; i < len; i++)
	str [i] = map [(unsigned) str [i]];
    }
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

void
eltype_call (int pars_number)
{
  ER_node_t vect;

  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, ELTYPE_NAME);
  if (ER_NODE_MODE (ctop) != ER_NM_vect)
    eval_error (partype_bc_decl, call_pos (),
		DERR_parameter_type, ELTYPE_NAME);
  vect = ER_vect (ctop);
  GO_THROUGH_REDIR (vect);
  try_full_pack (vect);
  /* Place the result instead of the function. */
  if (ER_NODE_MODE (vect) != ER_NM_heap_pack_vect)
    ER_SET_MODE (fun_result, ER_NM_nil);
  else
    {
      ER_node_mode_t mode = ER_pack_vect_el_mode (vect);
      type_val_t type = mode_to_type (mode);
      
      if (type == type_fun)
	{
	  val_t var;
	  ER_node_t var_ref = (ER_node_t) &var;
	  size_t displ;
	  size_t i, el_size;
	  type_val_t prev_type;

	  d_assert (mode == ER_NM_code);
	  ER_SET_MODE (var_ref, mode);
	  displ = val_displ_table [ER_NM_code];
	  el_size = type_size_table [mode];
	  prev_type = type_fun;
	  for (i = 0; i < ER_els_number (vect); i++)
	    {
	      memcpy ((char *) var_ref + displ,
		      (char *) ER_pack_els (vect) + i * el_size,
		      el_size);
	      type = code_type (ID_TO_CODE (ER_code_id (var_ref)));
	      if (i != 0 && type != prev_type)
		{
		  ER_SET_MODE (fun_result, ER_NM_nil);
		  return;
		}
	      prev_type = type;
	    }
	}
      ER_SET_MODE (fun_result, ER_NM_type);
      ER_set_type (fun_result, type);
    }
}

void
keys_call (int pars_number)
{
  ER_node_t tab;
  ER_node_t vect;
  size_t i, index;

  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, KEYS_NAME);
  if (ER_NODE_MODE (ctop) != ER_NM_tab)
    eval_error (partype_bc_decl, call_pos (),
		DERR_parameter_type, KEYS_NAME);
  tab = ER_tab (ctop);
  GO_THROUGH_REDIR (tab);
  /* Place the result instead of the function. */
  index = 0;
  if (ER_els_number (tab) == 0)
    vect = create_empty_vector ();
  else
    vect = create_unpack_vector (ER_els_number (tab));
  for (i = 0; i < ER_els_bound (tab); i++)
    if (ER_NODE_MODE (INDEXED_EL_KEY (ER_tab_els (tab), i)) != ER_NM_empty_el)
      {
	*(val_t *) IVAL (ER_unpack_els (vect), index)
	  = *(val_t *) INDEXED_EL_KEY (ER_tab_els (tab), i);
	index++;
      }
  try_full_pack (vect);
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

void
closure_call (int pars_number)
{
  ER_node_t val;
  BC_node_t block;

  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, CLOSURE_NAME);
  val = IVAL (ctop, -pars_number + 1);
  /* Place the result instead of the function. */
  if (ER_NODE_MODE (val) == ER_NM_stack
      && (block = ER_block_node (ER_stack (val))) != NULL
      && BC_NODE_MODE (block) == BC_NM_fblock)
    {
      ER_node_t stack;
      
      stack = ER_stack (val);
      ER_SET_MODE (fun_result, ER_NM_code);
      ER_set_code_id (fun_result, CODE_ID (block));
      ER_set_code_context (fun_result, ER_context (stack));
    }
  else
    ER_SET_MODE (fun_result, ER_NM_nil);
}

void
context_call (int pars_number)
{
  ER_node_t val;
  ER_node_t context;

  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, CONTEXT_NAME);
  val = IVAL (ctop, -pars_number + 1);
  if (ER_NODE_MODE (val) == ER_NM_stack)
    context = ER_context (ER_stack (val));
  else if (ER_NODE_MODE (val) == ER_NM_code)
    context = ER_code_context (val);
  else if (ER_NODE_MODE (val) == ER_NM_process)
    context = ER_context (ER_process (val));
  else
    eval_error (partype_bc_decl, call_pos (),
		DERR_parameter_type, CONTEXT_NAME);
  /* Place the result instead of the function. */
  if (context == NULL)
    ER_SET_MODE (fun_result, ER_NM_nil);
  else
    {
      d_assert (ER_NODE_MODE (context) == ER_NM_heap_stack);
      ER_SET_MODE (fun_result, ER_NM_stack);
      ER_set_stack (fun_result, context);
    }
}

int
internal_inside_call (const char **message_ptr, ER_node_t where, ER_node_t what,
		      int context_flag)
{
  ER_node_t code_context;
  ER_node_t code_2_context;
  BC_node_t code;
  BC_node_t code_2;
  int result;

  *message_ptr = NULL;
  code = NULL;
  if (ER_NODE_MODE (what) == ER_NM_stack)
    {
      code_context = ER_stack (what);
      code = ER_block_node (code_context);
    }
  else if (ER_NODE_MODE (what) == ER_NM_code)
    {
      code = ID_TO_CODE (ER_code_id (what));
      code_context = ER_code_context (what);
    }
  else
    {
      *message_ptr = DERR_parameter_type;
      return 0;
    }
  if (ER_IS_OF_TYPE (where, ER_NM_code))
    {
      code_2 = ID_TO_CODE (ER_code_id (where));
      code_2_context = ER_code_context (where);
    }
  else
    {
      *message_ptr = DERR_parameter_type;
      return 0;
    }
  result = (code == code_2
	    && (!context_flag || code_context == code_2_context));
  if (code != NULL && ER_NODE_MODE (what) == ER_NM_code)
    code = BC_scope (code);
  for (; !result && code != NULL;
       code = BC_scope (code), code_context = ER_context (code_context))
    if (code == code_2
	&& (!context_flag
	    || ER_context (code_context) == code_2_context))
      result = 1;
  return result;
}

void
inside_call (int pars_number)
{
  const char *message;
  int result;
  int flag;

  if (pars_number != 2 && pars_number != 3)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, INSIDE_NAME);
  flag = 0;
  if (pars_number == 3)
    {
      implicit_int_conversion (ctop, NULL);
      if (!ER_IS_OF_TYPE (ctop, ER_NM_int))
	eval_error (partype_bc_decl, call_pos (),
		    DERR_parameter_type, INSIDE_NAME);
      flag = ER_i (ctop);
    }
  result = internal_inside_call (&message, IVAL (ctop, 2 - pars_number),
				 IVAL (ctop, 1 - pars_number), flag);
  if (message != NULL)
    eval_error (partype_bc_decl, call_pos (), message, INSIDE_NAME);
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, result);
}

int
code_use_p (BC_node_t code, BC_node_t where)
{
  BC_node_t use;
  int result;

  d_assert (BC_IS_OF_TYPE (code, BC_NM_block)
	    && BC_IS_OF_TYPE (where, BC_NM_block));
  for (result = code == where, use = BC_uses (code);
       ! result && use != NULL;
       use = BC_next_use (use))
    if (BC_use (use) == where)
      result = 1;
  return result;
}

int
internal_isa_call (const char **message_ptr, ER_node_t where, ER_node_t what)
{
  BC_node_t code, code_2;

  if (message_ptr != NULL)
    *message_ptr = NULL;
  if (ER_NODE_MODE (what) == ER_NM_stack)
    {
      ER_node_t code_context = ER_stack (what);

      code = ER_block_node (code_context);
    }
  else if (ER_NODE_MODE (what) == ER_NM_code)
    code = ID_TO_CODE (ER_code_id (what));
  else
    {
      if (message_ptr != NULL)
	*message_ptr = DERR_parameter_type;
      return 0;
    }
  if (ER_IS_OF_TYPE (where, ER_NM_code))
    code_2 = ID_TO_CODE (ER_code_id (where));
  else
    {
      if (message_ptr != NULL)
	*message_ptr = DERR_parameter_type;
      return 0;
    }
  return code_use_p (code, code_2);
}

void
isa_call (int pars_number)
{
  const char *message;
  int result;

  if (pars_number != 2)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, ISA_NAME);
  result = internal_isa_call (&message, IVAL (ctop, 0), IVAL (ctop, -1));
  if (message != NULL)
    eval_error (partype_bc_decl, call_pos (), message, ISA_NAME);
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, result);
}



#include "oniguruma.h"

static void
process_onig_errors (int code, const char *function_name)
{
#if 0
  if (code == REG_EBRACK)
    eval_error (ebrack_bc_decl, call_pos (), DERR_reg_ebrack, function_name);
  else if (code == REG_ERANGE)
    eval_error (reg_erange_bc_decl, call_pos (),
		DERR_reg_erange, function_name);
  else if (code == REG_ECTYPE)
    eval_error (ectype_bc_decl, call_pos (), DERR_reg_ectype, function_name);
  else if (code == REG_EPAREN)
    eval_error (eparen_bc_decl, call_pos (), DERR_reg_eparen, function_name);
  else if (code == REG_ESUBREG)
    eval_error (esubreg_bc_decl, call_pos (), DERR_reg_esubreg, function_name);
  else if (code == REG_EEND)
    eval_error (eend_bc_decl, call_pos (), DERR_reg_eend, function_name);
  else if (code == REG_EESCAPE)
    eval_error (eescape_bc_decl, call_pos (), DERR_reg_eescape, function_name);
  else if (code == REG_BADPAT || code == REG_BADRPT
	   || code == REG_BADBR || code == REG_EBRACE)
    /* We use badpat because I can not find badrpt, badbr, ebrace
       diagnostics for POSIX in GNU Regex. */
    eval_error (badpat_bc_decl, call_pos (), DERR_reg_badpat, function_name);
  else if (code == REG_ESIZE)
    eval_error (esize_bc_decl, call_pos (), DERR_reg_esize, function_name);
  else if (code == REG_ESPACE)
    eval_error (espace_bc_decl, call_pos (), DERR_reg_espace, function_name);
  else
#endif
    /* Internal error: may be something else. */
    eval_error (internal_bc_decl, call_pos (),
		DERR_internal_error, function_name);
}

#define RE_DINO_SYNTAX (ONIG_SYNTAX_RUBY)

/* The following structure is element of the cache of compiled
   regex. */
struct regex_node
{
  /* Compiled regex for combination of encoding pattern and string.  A
     field can be NULL if it is not calculated_yet.  */
  regex_t *latin1_latin1_regex;
  regex_t *latin1_ucode_regex;
  regex_t *ucode_ucode_regex;
  /* Regex string representation.  It is a key of in the cache. */
  void *string;
  /* True if string is ucode_t string.  */
  int ucode_p;
};

/* Temporary structure. */
static struct regex_node regex_node;

/* Hash table which implements the cache. */
static hash_table_t regex_tab;
/* This object stack contains elements of the cache. */
static os_t regex_os;
/* Vector containing pointers to the cache elements. */
static vlo_t regex_vlo;

/* Region used for searches.  */
static OnigRegion *region;

/* Hash of the node. */
static unsigned
regex_node_hash (hash_table_entry_t n)
{
  unsigned hash_value, i;
  struct regex_node *node = (struct regex_node *) n;

  if (node->ucode_p)
    {
      const ucode_t *str = node->string;
      
      for (hash_value = i = 0; *str != '\0'; str++, i++)
	hash_value += *str << (i & 0x7);
    }
  else
    {
      const byte_t *str = node->string;
      
      for (hash_value = i = 0; *str != '\0'; str++, i++)
	hash_value += (*(const unsigned char *) str << (i % CHAR_BIT));
    }
  return hash_value;
}

/* Equality of nodes. */
static int
regex_node_eq (hash_table_entry_t n1, hash_table_entry_t n2)
{
  size_t i;
  int ch1, ch2;
  struct regex_node *node1 = ((struct regex_node *) n1);
  struct regex_node *node2 = ((struct regex_node *) n2);
  
  if (! node1->ucode_p && ! node2->ucode_p)
    return strcmp (node1->string, node2->string) == 0;
  for (i = 0;; i++)
    {
      ch1 = (node1->ucode_p
	     ? ((const ucode_t *) node1->string) [i]
	     : ((const byte_t *) node1->string) [i]);
      ch2 = (node2->ucode_p
	     ? ((const ucode_t *) node2->string) [i]
	     : ((const byte_t *) node2->string) [i]);
      if (ch1 != ch2)
	return FALSE;
      if (ch1 == 0)
	break;
    }
  return TRUE;
}

/* Return size of ucode string STR (if UCODE_P) or byte string
   otherwize.  The size includes trailing zero char if
   ZERO_CHAR_P.  */
static inline size_t
get_str_size (const void *str, int ucode_p, int zero_char_p)
{
  size_t tail_size
    = ! zero_char_p ? 0 : ucode_p ? sizeof (ucode_t) : sizeof (byte_t);
  return (ucode_p
	  ? ucodestrlen ((const ucode_t *) str) * sizeof (ucode_t)
	  : strlen ((const char *) str) * sizeof (byte_t)) + tail_size;
}

/* Find compiled version of regex pattern PAT (latin1 or unicode
   depending on PAT_UCODE_P) in the cache for matching a string
   (latin1 or unicode depending on STR_UCODE_P).  If it is absent,
   compile it and insert it into the cache.  Returns nonzero if there
   were errors during the compilation. */
static int
find_regex (void *pat, int pat_ucode_p, int str_ucode_p, regex_t **result)
{
  hash_table_entry_t *entry;
  struct regex_node *regn;
  regex_t *r;
  OnigErrorInfo einfo;
  size_t len, size;
  int code;
  OnigCompileInfo ci;
  OnigEncoding ucode_enc = (big_endian_p
			    ? ONIG_ENCODING_UTF32_BE : ONIG_ENCODING_UTF32_LE);

  /* Check possible combinations.  */
  d_assert (! pat_ucode_p || str_ucode_p);
  *result = NULL;
  regex_node.string = pat;
  regex_node.ucode_p = pat_ucode_p;
  entry = find_hash_table_entry (regex_tab, &regex_node, FALSE);
  if (*entry != NULL)
    {
      regn = (struct regex_node *) (*entry); 
      *result = (pat_ucode_p ? regn->ucode_ucode_regex
		 : str_ucode_p ? regn->latin1_ucode_regex
		 : regn->latin1_latin1_regex);
      if (*result != NULL)
	return ONIG_NORMAL;
    }
  OS_TOP_EXPAND (regex_os, sizeof (struct regex_node));
  regn = OS_TOP_BEGIN (regex_os);
  len = (pat_ucode_p ? strlen (pat) : ucodestrlen (pat));
  ci.num_of_elements = 5;
  ci.pattern_enc = pat_ucode_p ? ucode_enc : ONIG_ENCODING_ISO_8859_1;
  ci.target_enc  = str_ucode_p ? ucode_enc : ONIG_ENCODING_ISO_8859_1;
  ci.syntax      = RE_DINO_SYNTAX;
  ci.option      = ONIG_OPTION_DEFAULT;
  ci.case_fold_flag  = ONIGENC_CASE_FOLD_DEFAULT;
  code = onig_new_deluxe (&r, pat,
			  (char *) pat + get_str_size (pat, pat_ucode_p, FALSE),
			  &ci, &einfo);
  if (code != ONIG_NORMAL)
    {
      onig_free (r);
      OS_TOP_NULLIFY (regex_os);
      return code;
    }
  if (*entry != NULL)
    regn = (struct regex_node *) (*entry); 
  else
    {
      OS_TOP_FINISH (regex_os);
      VLO_ADD_MEMORY (regex_vlo, &regn, sizeof (regn));
      size = get_str_size (pat, pat_ucode_p, TRUE);
      OS_TOP_EXPAND (regex_os, size);
      regn->string = OS_TOP_BEGIN (regex_os);
      OS_TOP_FINISH (regex_os);
      memcpy (regn->string, pat, size);
      regn->latin1_latin1_regex
	= regn->latin1_ucode_regex = regn->ucode_ucode_regex = NULL;
    }
  if (pat_ucode_p)
    regn->ucode_ucode_regex = r;
  else if (str_ucode_p)
    regn->latin1_ucode_regex = r;
  else
    regn->latin1_latin1_regex = r;
  if (*entry == NULL)
    {
      entry = find_hash_table_entry (regex_tab, regn, TRUE);
      *entry = regn;
    }
  *result = r;
  return ONIG_NORMAL;
}

/* Create the cache of compiled regexs. */
static void
initiate_regex_tab (void)
{
  onig_init ();
  region = onig_region_new ();
  OS_CREATE (regex_os, 0);
  VLO_CREATE (regex_vlo, 0);
  regex_tab = create_hash_table (400, regex_node_hash, regex_node_eq);
}

/* Delete the cache of compiled regexs. */
static void
finish_regex_tab (void)
{
  int i;
  struct regex_node *regn;

  delete_hash_table (regex_tab);
  for (i = 0; i < VLO_LENGTH (regex_vlo) / sizeof (struct regex_node *); i++)
    {
      regn = ((struct regex_node **) VLO_BEGIN (regex_vlo)) [i];
      if (regn->latin1_latin1_regex != NULL)
	onig_free (regn->latin1_latin1_regex);
      if (regn->latin1_ucode_regex != NULL)
	onig_free (regn->latin1_ucode_regex);
      if (regn->ucode_ucode_regex != NULL)
	onig_free (regn->ucode_ucode_regex);
    }
  VLO_DELETE (regex_vlo);
  OS_DELETE (regex_os);
  onig_region_free (region, 1);
  onig_end ();
}

void
match_call (int pars_number)
{
  regex_t *reg;
  ER_node_t result;
  size_t els_number;
  size_t i;
  const char *start, *end;
  int code, pat_ucode_p, str_ucode_p;

  if (pars_number != 2)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, MATCH_NAME);
  to_vect_string_conversion (ctop, NULL, NULL);
  to_vect_string_conversion (below_ctop, NULL, NULL);
  if (ER_NODE_MODE (ctop) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (ctop)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_byte)
      || ER_NODE_MODE (below_ctop) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (below_ctop)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (below_ctop)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (below_ctop)) != ER_NM_byte))
    eval_error (partype_bc_decl, call_pos (), DERR_parameter_type, MATCH_NAME);
  pat_ucode_p = ER_pack_vect_el_mode (ER_vect (below_ctop)) == ER_NM_char;
  str_ucode_p = ER_pack_vect_el_mode (ER_vect (ctop)) == ER_NM_char;
  if (pat_ucode_p && ! str_ucode_p)
    {
      /* Impossible ONIGURUMA combination: ucode pattern and ascii
	 string.  */
      ER_set_vect (ctop, bytevect_to_ucodevect (ER_vect (ctop)));
      str_ucode_p = TRUE;
    }
  code = find_regex (ER_pack_els (ER_vect (below_ctop)),
		     pat_ucode_p, str_ucode_p, &reg);
  if (code != ONIG_NORMAL)
    process_onig_errors (code, MATCH_NAME);
  start = ER_pack_els (ER_vect (ctop));
  end = start + get_str_size (start, str_ucode_p, FALSE);
  code = onig_search (reg, start, end, start, end, region, ONIG_OPTION_NONE);
  if (code == ONIG_MISMATCH)
    result = NULL;
  else if (code >= 0)
    {
      size_t ch_size = str_ucode_p ? sizeof (ucode_t) : sizeof (byte_t);

      els_number = region->num_regs;
      result = create_pack_vector (2 * els_number, ER_NM_int);
      for (i = 0; i < els_number; i++)
	{
	  ((rint_t *) ER_pack_els (result)) [2 * i] = region->beg[i] / ch_size;
	  ((rint_t *) ER_pack_els (result)) [2 * i + 1]
	    = region->end[i] / ch_size;
	}
    }
  else
    process_onig_errors (code, MATCH_NAME);
  if (result == NULL)
    ER_SET_MODE (fun_result, ER_NM_nil);
  else
    {
      ER_SET_MODE (fun_result, ER_NM_vect);
      set_vect_dim (fun_result, result, 0);
    }
}

void
gmatch_call (int pars_number)
{
  regex_t *reg;
  ER_node_t par1, par2, result;
  int code, flag, count, disp, pat_ucode_p, str_ucode_p;
  rint_t el;
  size_t len, ch_size;
  const char *start, *end;

  if (pars_number != 2 && pars_number != 3)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, GMATCH_NAME);
  flag = 0;
  if (pars_number == 3)
    {
      implicit_int_conversion (ctop, NULL);
      if (!ER_IS_OF_TYPE (ctop, ER_NM_int))
	eval_error (partype_bc_decl, call_pos (),
		    DERR_parameter_type, GMATCH_NAME);
      flag = ER_i (ctop);
    }
  par1 = IVAL (ctop, 1 - pars_number);
  par2 = IVAL (ctop, 2 - pars_number);
  to_vect_string_conversion (par2, NULL, NULL);
  to_vect_string_conversion (par1, NULL, NULL);
  if (ER_NODE_MODE (par2) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (par2)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (par2)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (par2)) != ER_NM_byte)
      || ER_NODE_MODE (par1) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (par1)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (par1)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (par1)) != ER_NM_byte))
    eval_error (partype_bc_decl, call_pos (), DERR_parameter_type, GMATCH_NAME);
  pat_ucode_p = ER_pack_vect_el_mode (ER_vect (par1)) == ER_NM_char;
  str_ucode_p = ER_pack_vect_el_mode (ER_vect (par2)) == ER_NM_char;
  if (pat_ucode_p && ! str_ucode_p)
    {
      /* Impossible ONIGURUMA combination: ucode pattern and ascii
	 string.  */
      ER_set_vect (par2, bytevect_to_ucodevect (ER_vect (par2)));
      str_ucode_p = TRUE;
    }
  code = find_regex (ER_pack_els (ER_vect (par1)),
		     pat_ucode_p, str_ucode_p, &reg);
  if (code != ONIG_NORMAL)
    process_onig_errors (code, GMATCH_NAME);
  VLO_NULLIFY (temp_vlobj2);
  start = ER_pack_els (ER_vect (par2));
  end = start + get_str_size (start, str_ucode_p, FALSE);
  disp = 0;
  count = 0;
  ch_size = str_ucode_p ? sizeof (ucode_t) : sizeof (byte_t);
  while (onig_search (reg, start + disp, end, start + disp, end,
		      region, ONIG_OPTION_NONE) >= 0)
    {
      el = (region->beg [0] + disp) / ch_size;
      VLO_ADD_MEMORY (temp_vlobj2, &el, sizeof (el));
      el = (region->end [0] + disp) / ch_size;
      VLO_ADD_MEMORY (temp_vlobj2, &el, sizeof (el));
      if (flag)
	disp += ch_size;
      else
	disp += region->end [0];
      count++;
    }
  if (count == 0)
    ER_SET_MODE (fun_result, ER_NM_nil);
  else
    {
      result = create_pack_vector (2 * count, ER_NM_int);
      memcpy (ER_pack_els (result), VLO_BEGIN (temp_vlobj2),
	      2 * count * sizeof (el));
      ER_SET_MODE (fun_result, ER_NM_vect);
      set_vect_dim (fun_result, result, 0);
    }
}

static void
generall_sub_call (int pars_number, int global_flag)
{
  regex_t *reg;
  size_t len;
  ER_node_t result;
  ER_node_t vect;
  ER_node_t regexp_val;
  size_t ch_size, els_num;
  size_t disp;
  size_t i;
  const char *substitution;
  const char *src;
  const char *str, *end;
  int c, nc;
  ucode_t uc;
  byte_t ac;
  int code, pat_ucode_p, str_ucode_p, subst_ucode_p;

  if (pars_number != 3)
    eval_error (parnumber_bc_decl, call_pos (), DERR_parameters_number,
		global_flag ? GSUB_NAME : SUB_NAME);
  to_vect_string_conversion (ctop, NULL, NULL);
  to_vect_string_conversion (below_ctop, NULL, NULL);
  regexp_val = IVAL (ctop, -2);
  to_vect_string_conversion (regexp_val, NULL, NULL);
  if (ER_NODE_MODE (ctop) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (ctop)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_byte)
      || ER_NODE_MODE (below_ctop) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (below_ctop)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (below_ctop)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (below_ctop)) != ER_NM_byte)
      || ER_NODE_MODE (regexp_val) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (regexp_val)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (regexp_val)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (regexp_val)) != ER_NM_byte))
    eval_error (partype_bc_decl, call_pos (),
		DERR_parameter_type, global_flag ? GSUB_NAME : SUB_NAME);
  pat_ucode_p = ER_pack_vect_el_mode (ER_vect (regexp_val)) == ER_NM_char;
  str_ucode_p = ER_pack_vect_el_mode (ER_vect (below_ctop)) == ER_NM_char;
  if (pat_ucode_p && ! str_ucode_p)
    {
      /* Impossible ONIGURUMA combination: ucode pattern and ascii
	 string.  */
      ER_set_vect (below_ctop, bytevect_to_ucodevect (ER_vect (below_ctop)));
      str_ucode_p = TRUE;
    }
  subst_ucode_p = ER_pack_vect_el_mode (ER_vect (ctop)) == ER_NM_char;
  /* Make original and substitution strings of the same coding.  */
  if (subst_ucode_p && ! str_ucode_p)
    {
      ER_set_vect (below_ctop, bytevect_to_ucodevect (ER_vect (below_ctop)));
      str_ucode_p = TRUE;
    }
  else if (! subst_ucode_p && str_ucode_p)
    {
      ER_set_vect (ctop, bytevect_to_ucodevect (ER_vect (ctop)));
      subst_ucode_p = TRUE;
    }
  code = find_regex (ER_pack_els (ER_vect (regexp_val)),
		     pat_ucode_p, str_ucode_p, &reg);
  if (code != ONIG_NORMAL)
    process_onig_errors (code, global_flag ? GSUB_NAME : SUB_NAME);
  else
    {
      d_assert (str_ucode_p == subst_ucode_p);
      vect = ER_vect (below_ctop);
      str = ER_pack_els (vect);
      end = str + get_str_size (str, str_ucode_p, FALSE);
      ch_size = str_ucode_p ? sizeof (ucode_t) : sizeof (byte_t);
      disp = els_num = 0;
      VLO_NULLIFY (temp_vlobj2);
      substitution = ER_pack_els (ER_vect (ctop));
      while ((disp < ER_els_number (vect) * ch_size || disp == 0)
	     && onig_search (reg, str + disp, end, str + disp, end,
			     region, ONIG_OPTION_NONE) >= 0)
	{
	  VLO_EXPAND (temp_vlobj2, region->beg[0]);
	  if (region->beg[0] != 0)
	    memcpy ((char *) VLO_BOUND (temp_vlobj2) - region->beg[0],
		    str + disp, region->beg[0]);
	  els_num += region->beg[0] / ch_size;
	  src = substitution;
	  while (*src != '\0')
	    {
	      c = (str_ucode_p ? *(ucode_t *) src : *(byte_t *) src);
	      src += ch_size;
	      nc = (str_ucode_p ? *(ucode_t *) src : *(byte_t *) src);
	      if (c == '&')
		i = 0;
	      else if (c == '\\' && '0' <= nc && nc <= '9')
		{
		  i = nc - '0';
		  src += ch_size;
		}
	      else
		i = 10;
	      
	      if (i >= 10)
		{
		  if (c == '\\' && (nc == '\\' || nc == '&'))
		    {
		      c = nc;
		      src += ch_size;
		    }
		  if (str_ucode_p)
		    {
		      uc = c;
		      VLO_ADD_MEMORY (temp_vlobj2, &uc, sizeof (ucode_t));
		    }
		  else
		    {
		      ac = c;
		      VLO_ADD_MEMORY (temp_vlobj2, &ac, sizeof (byte_t));
		    }
		  els_num++;
		}
	      else if (i < region->num_regs
		       && region->end[i] != region->beg[i])
		{
		  len = region->end[i] - region->beg[i];

		  VLO_EXPAND (temp_vlobj2, len);
		  memcpy ((char *) VLO_BOUND (temp_vlobj2) - len,
			  str + disp + region->beg[i], len);
		  els_num += len / ch_size;
		}
	    }
	  if (region->end[0] == 0)
	    {
	      /* Matched empty string */
	      if (ER_els_number (vect) != 0)
		{
		  if (str_ucode_p)
		    {
		      uc = *(str + disp);
		      VLO_ADD_MEMORY (temp_vlobj2, &uc, sizeof (ucode_t));
		    }
		  else
		    {
		      ac = *(str + disp);
		      VLO_ADD_MEMORY (temp_vlobj2, &ac, sizeof (byte_t));
		    }
		  els_num++;
		}
	      disp += ch_size;
	    }
	  else
	    disp += region->end[0];
	  if (!global_flag)
	    break;
	}
      if (disp < ER_els_number (vect) * ch_size)
	{
	  len = ER_els_number (vect) * ch_size - disp;
	  VLO_EXPAND (temp_vlobj2, len);
	  memcpy ((char *) VLO_BOUND (temp_vlobj2) - len, str + disp, len);
	  els_num += len / ch_size;
	}
      if (str_ucode_p)
	{
	  uc = '\0';
	  VLO_ADD_MEMORY (temp_vlobj2, &uc, sizeof (ucode_t));
	}
      else
	{
	  ac = '\0';
	  VLO_ADD_MEMORY (temp_vlobj2, &ac, sizeof (byte_t));
	}
      result = create_pack_vector (els_num + 1,
				   str_ucode_p ? ER_NM_char : ER_NM_byte);
      memcpy (ER_pack_els (result),
	      VLO_BEGIN (temp_vlobj2), (els_num + 1) * ch_size);
      ER_set_els_number (result, els_num);
    }
  if (result == NULL)
    ER_SET_MODE (fun_result, ER_NM_nil);
  else
    {
      ER_SET_MODE (fun_result, ER_NM_vect);
      set_vect_dim (fun_result, result, 0);
    }
}

void
sub_call (int pars_number)
{
  generall_sub_call (pars_number, FALSE);
}

void
gsub_call (int pars_number)
{
  generall_sub_call (pars_number, TRUE);
}

void
split_call (int pars_number)
{
  regex_t *reg;
  size_t len;
  ER_node_t result;
  ER_node_t vect;
  ER_node_t sub_vect;
  size_t i, els_number;
  size_t chars_number;
  size_t disp, ch_size;
  char *split_regex;
  ER_node_t split_var;
  const char *str, *end;
  int ok, pat_ucode_p, str_ucode_p;
  int code;

  if (pars_number != 1 && pars_number != 2)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, SPLIT_NAME);
  if (pars_number != 1)
    to_vect_string_conversion (below_ctop, NULL, NULL);
  to_vect_string_conversion (ctop, NULL, NULL);
  if (ER_NODE_MODE (ctop) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (ctop)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_byte)
      || (pars_number == 2
	  && (ER_NODE_MODE (below_ctop) != ER_NM_vect
	      || ER_NODE_MODE (ER_vect (below_ctop)) != ER_NM_heap_pack_vect
	      || (ER_pack_vect_el_mode (ER_vect (below_ctop)) != ER_NM_char
		  && ER_pack_vect_el_mode (ER_vect (below_ctop)) != ER_NM_byte))))
    eval_error (partype_bc_decl, call_pos (), DERR_parameter_type, SPLIT_NAME);
  str_ucode_p = ER_pack_vect_el_mode (ER_vect (ctop)) == ER_NM_char;
  if (pars_number == 2)
    {
      split_regex = ER_pack_els (ER_vect (ctop));
      pat_ucode_p = ER_pack_vect_el_mode (ER_vect (ctop)) == ER_NM_char;
      str_ucode_p = ER_pack_vect_el_mode (ER_vect (below_ctop)) == ER_NM_char;
    }
  else
    {
      str_ucode_p = ER_pack_vect_el_mode (ER_vect (ctop)) == ER_NM_char;
      split_var = IVAL (ER_stack_vars (uppest_stack),
			BC_var_num (split_regex_bc_decl));
      to_vect_string_conversion (split_var, NULL, NULL);
      split_var = IVAL (ER_stack_vars (uppest_stack),
			BC_var_num (split_regex_bc_decl));
      if (ER_NODE_MODE (split_var) == ER_NM_vect
	  && ER_NODE_MODE (ER_vect (split_var)) == ER_NM_heap_pack_vect
	  && (ER_pack_vect_el_mode (ER_vect (split_var)) == ER_NM_char
	      || ER_pack_vect_el_mode (ER_vect (split_var)) == ER_NM_byte))
	{
	  split_regex = ER_pack_els (ER_vect (split_var));
	  pat_ucode_p = ER_pack_vect_el_mode (ER_vect (split_var)) == ER_NM_char;
	}
      else
	eval_error (invenvar_bc_decl,
		    call_pos (), DERR_corrupted_environment_var,
		    SPLIT_REGEX_NAME);
    }
  if (pat_ucode_p && ! str_ucode_p)
    {
      /* Impossible ONIGURUMA combination: ucode pattern and ascii
	 string.  */
      vect = pars_number == 2 ? below_ctop : ctop;
      ER_set_vect (vect, bytevect_to_ucodevect (ER_vect (vect )));
      str_ucode_p = TRUE;
    }
  code = find_regex (split_regex, pat_ucode_p, str_ucode_p, &reg);
  if (code != 0)
    process_onig_errors (code, SPLIT_NAME);
  else
    {
      vect = ER_vect (pars_number == 2 ? below_ctop : ctop);
      els_number = disp = 0;
      str = ER_pack_els (vect);
      end = str + get_str_size (str, str_ucode_p, FALSE);
      ch_size = str_ucode_p ? sizeof (ucode_t) : sizeof (byte_t);
      VLO_NULLIFY (temp_vlobj2);
      disp = 0;
      while (disp < ER_els_number (vect) * ch_size || disp == 0)
	{
	  ok = onig_search (reg, str + disp, end, str + disp, end,
			    region, ONIG_OPTION_NONE) >= 0;
	  if (ok)
	    {
	      if (region->beg[0] != 0 || region->end[0] == 0)
		{
		  /* Empty pattern case is here too. */
		  if (region->beg[0] == 0)
		    region->beg[0] += ch_size;
		  chars_number = region->beg[0] / ch_size;
		}
	      else
		{
		  /* Pattern by pattern. */
		  disp += region->end[0];
		  continue;
		}
	    }
	  else
	    chars_number = ER_els_number (vect) - disp / ch_size;
	  /* Create substring. */
	  sub_vect = create_pack_vector (chars_number + 1,
					 str_ucode_p ? ER_NM_char : ER_NM_byte);
	  ER_set_els_number (sub_vect, chars_number);
	  ER_set_immutable (sub_vect, TRUE);
	  memcpy (ER_pack_els (sub_vect), ER_pack_els (vect) + disp,
		  chars_number * ch_size);
	  if (str_ucode_p)
	    ((ucode_t *) ER_pack_els (sub_vect)) [chars_number] = '\0';
	  else
	    ((byte_t *) ER_pack_els (sub_vect)) [chars_number] = '\0';
	  VLO_ADD_MEMORY (temp_vlobj2, &sub_vect, sizeof (sub_vect));
	  if (!ok)
	    break;
	  if (region->end[0] == 0)
	    disp += ch_size;
	  else
	    disp += region->end[0];
	}
      els_number = VLO_LENGTH (temp_vlobj2) / sizeof (ER_node_t);
      result = create_pack_vector (els_number, ER_NM_vect);
      ER_set_els_number (result, els_number);
      for (i = 0; i < els_number; i++)
	set_packed_vect_el (result, i,
			    ((ER_node_t *) VLO_BEGIN (temp_vlobj2)) [i]);
    }
  if (result == NULL)
    ER_SET_MODE (fun_result, ER_NM_nil);
  else
    {
      ER_SET_MODE (fun_result, ER_NM_vect);
      set_vect_dim (fun_result, result, 0);
    }
}



static int do_inline
compare_elements (ER_node_mode_t el_type, const void *el1, const void *el2)
{
  switch (el_type)
    {
    case ER_NM_char:
      if (*(ucode_t *) el1 < *(ucode_t *) el2)
	return -1;
      else if (*(ucode_t *) el1 == *(ucode_t *) el2)
	return 0;
      else
	return 1;
    case ER_NM_byte:
      if (*(byte_t *) el1 < *(byte_t *) el2)
	return -1;
      else if (*(byte_t *) el1 == *(byte_t *) el2)
	return 0;
      else
	return 1;
    case ER_NM_int:
      if (*(rint_t *) el1 < *(rint_t *) el2)
	return -1;
      else if (*(rint_t *) el1 == *(rint_t *) el2)
	return 0;
      else
	return 1;
    case ER_NM_long:
      {
	int i = mpz_cmp (*ER_mpz_ptr (*(ER_node_t *) el1),
			 *ER_mpz_ptr (*(ER_node_t *) el2));
	return (i < 0 ? -1 : i > 0 ? 1 : 0);
      }
    case ER_NM_float:
      if (*(rfloat_t *) el1 < *(rfloat_t *) el2)
	return -1;
      else if (*(rfloat_t *) el1 == *(rfloat_t *) el2)
	return 0;
      else
	return 1;
    default:
      d_unreachable ();
    }
}

void
subv_call (int pars_number)
{
  ER_node_t vect;
  ER_node_t res;
  rint_t start;
  rint_t length;
  size_t vect_length;
  size_t el_size;
  ER_node_mode_t el_type;

  if (pars_number < 2 || pars_number > 3)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, SUBV_NAME);
  if (pars_number == 2)
    {
      to_vect_string_conversion (below_ctop, NULL, NULL);
      implicit_int_conversion (ctop, NULL);
    }
  else
    {
      to_vect_string_conversion (IVAL (ctop, -2), NULL, NULL);
      implicit_int_conversion (below_ctop, NULL);
      implicit_int_conversion (ctop, NULL);
    }
  if (ER_NODE_MODE (IVAL (ctop, -pars_number + 1)) != ER_NM_vect
      || ER_NODE_MODE (ctop) != ER_NM_int
      || (pars_number == 3 && ER_NODE_MODE (below_ctop) != ER_NM_int))
    eval_error (partype_bc_decl, call_pos (), DERR_parameter_type, SUBV_NAME);
  if (pars_number == 3)
    {
      start = ER_i (below_ctop);
      length = ER_i (ctop);
    }
  else
    {
      start = ER_i (ctop);
      length = -1;
    }
  vect = ER_vect (IVAL (ctop, -pars_number + 1));
  GO_THROUGH_REDIR (vect);
  vect_length = ER_els_number (vect);
  if (start < 0)
    start = 0;
  if (start < vect_length && (length < 0 || start + length >= vect_length))
    /* Get tail. */
    length = vect_length - start;
  else if (start >= vect_length)
    length = 0;
  if (length == 0)
    {
      if (ER_NODE_MODE (vect) == ER_NM_heap_pack_vect
	  && (ER_pack_vect_el_mode (vect) == ER_NM_char
	      || ER_pack_vect_el_mode (vect) == ER_NM_byte))
	{
	  if (ER_pack_vect_el_mode (vect) == ER_NM_byte)
	    res = create_string ("");
	  else
	    {
	      ucode_t empty [] = {0};
	      create_ucodestr (empty);
	    }
	  ER_set_immutable (res, FALSE);
	}
      else
	res = create_empty_vector ();
    }
  else if (ER_NODE_MODE (vect) == ER_NM_heap_pack_vect)
    {
      el_type = ER_pack_vect_el_mode (vect);
      el_size = type_size_table [el_type];
      res = create_pack_vector (el_type == ER_NM_char || el_type == ER_NM_byte
				? length + 1 : length,
				el_type);
      ER_set_els_number (res, length);
      memcpy (ER_pack_els (res), ER_pack_els (vect) + start * el_size,
	      el_size * length);
      if (el_type == ER_NM_byte)
	((byte_t *) ER_pack_els (res)) [length] = '\0';
      else if (el_type == ER_NM_char)
	((ucode_t *) ER_pack_els (res)) [length] = '\0';
    }
  else
    {
      res = create_unpack_vector (length);
      memcpy (ER_unpack_els (res),
	      (char *) ER_unpack_els (vect) + start * sizeof (val_t),
	      length * sizeof (val_t));
    }
  ER_set_immutable (res, ER_immutable (vect));
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, res, 0);
}

void
cmpv_call (int pars_number)
{
  ER_node_t vect1, vect2;
  size_t i;
  rint_t res;
  ucode_t uc;
  ER_node_mode_t el_type1, el_type2;
  char *addr1, *addr2;
  ER_node_t el;

  if (pars_number != 2)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, CMPV_NAME);
  to_vect_string_conversion (ctop, NULL, 0);
  to_vect_string_conversion (below_ctop, NULL, 0);
  if (ER_NODE_MODE (ctop) != ER_NM_vect
      || ER_NODE_MODE (below_ctop) != ER_NM_vect)
    eval_error (partype_bc_decl, call_pos (), DERR_parameter_type, CMPV_NAME);
  vect1 = ER_vect (below_ctop);
  GO_THROUGH_REDIR (vect1);
  vect2 = ER_vect (ctop);
  GO_THROUGH_REDIR (vect2);
  res = 0;
  for (i = 0; i < ER_els_number (vect1) && i < ER_els_number (vect2); i++)
    {
      if (ER_NODE_MODE (vect1) == ER_NM_heap_pack_vect)
	{
	  el_type1 = ER_pack_vect_el_mode (vect1);
	  addr1 = ER_pack_els (vect1) + i * type_size_table [el_type1];
	}
      else
	{
	  el = IVAL (ER_unpack_els (vect1), i);
	  el_type1 = ER_NODE_MODE (el);
	  addr1
	    = (char *) el + val_displ_table [ER_NODE_MODE ((ER_node_t) el)];
	}
      if (ER_NODE_MODE (vect2) == ER_NM_heap_pack_vect)
	{
	  el_type2 = ER_pack_vect_el_mode (vect2);
	  addr2 = ER_pack_els (vect2) + i * type_size_table [el_type2];
	}
      else
	{
	  el = IVAL (ER_unpack_els (vect2), i);
	  el_type2 = ER_NODE_MODE (el);
	  addr2
	    = (char *) el + val_displ_table [ER_NODE_MODE ((ER_node_t) el)];
	}
      if (el_type1 == ER_NM_byte && el_type2 == ER_NM_char)
	{
	  el_type1 == ER_NM_char;
	  uc = *(byte_t *) addr1;
	  addr1 = (char *) &uc;
	}
      else if (el_type2 == ER_NM_byte && el_type1 == ER_NM_char)
	{
	  el_type2 == ER_NM_char;
	  uc = *(byte_t *) addr2;
	  addr2 = (char *) &uc;
	}
      if (el_type1 != el_type2
	  || (el_type1 != ER_NM_float
	      && el_type1 != ER_NM_int
	      && el_type1 != ER_NM_char
	      && el_type1 != ER_NM_byte
	      && el_type1 != ER_NM_long))
	eval_error (partype_bc_decl, call_pos (),
		    DERR_parameter_type, CMPV_NAME);
      res = compare_elements (el_type1, addr1, addr2);
      if (res)
	break;
    }
  if (res == 0)
    {
      if (i < ER_els_number (vect1))
	res = 1;
      else if (i < ER_els_number (vect2))
	res = (-1);
    }
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, res);
}

void
del_call (int pars_number)
{
  ER_node_t val;
  ER_node_mode_t mode;
  ER_node_t vect;
  ER_node_t tab;
      
  val = IVAL (ctop, -pars_number + 1);
  if (pars_number < 2 || pars_number > 3
      || (ER_NODE_MODE (val) == ER_NM_tab && pars_number != 2))
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, DEL_NAME);
  mode = ER_NODE_MODE (val);
  if (mode == ER_NM_vect)
    {
      ER_node_t start_val;
      ER_node_t length_val;
      rint_t start;
      rint_t length;
      size_t vect_length;
      size_t el_size;
      ER_node_mode_t el_type;
      
      implicit_int_conversion (IVAL (ctop, -pars_number + 2), NULL);
      start_val = IVAL (ctop, -pars_number + 2);
      if (pars_number == 3)
	{
	  implicit_int_conversion (ctop, NULL);
	  length_val = IVAL (ctop, 0);
	}
      else
	length_val = NULL;
      if (ER_NODE_MODE (start_val) != ER_NM_int
	  || (length_val != NULL && ER_NODE_MODE (length_val) != ER_NM_int))
	eval_error (partype_bc_decl, call_pos (),
		    DERR_parameter_type, DEL_NAME);
      start = ER_i (start_val);
      if (length_val != NULL)
	length = ER_i (length_val);
      else
	length = 1;
      vect = ER_vect (val);
      GO_THROUGH_REDIR (vect);
      if (ER_immutable (vect))
	eval_error (immutable_bc_decl, call_pos (),
		    DERR_immutable_vector_modification);
      vect_length = ER_els_number (vect);
      if (start < 0)
	start = 0;
      if (start < vect_length && (length < 0 || start + length >= vect_length))
	{
	  /* Remove tail */
	  ER_set_els_number (vect, start);
	  if (ER_NODE_MODE (vect) == ER_NM_heap_pack_vect)
	    {
	      if (ER_pack_vect_el_mode (vect) == ER_NM_byte)
		((byte_t *) ER_pack_els (vect)) [start] = '\0';
	      else if (ER_pack_vect_el_mode (vect) == ER_NM_char)
		((ucode_t *) ER_pack_els (vect)) [start] = '\0';
	    }
	}
      else if (start == 0 && vect_length != 0)
	{
	  /* Remove head */
	  size_t el_size;

	  if (ER_NODE_MODE (vect) == ER_NM_heap_pack_vect)
	    el_size = type_size_table [ER_pack_vect_el_mode (vect)];
	  else
	    el_size = sizeof (val_t);
	  ER_set_disp (vect, ER_disp (vect) + length * el_size);
	  ER_set_els_number (vect, ER_els_number (vect) - length);
	}
      else if (start >= vect_length || length == 0)
	;
      else if (ER_NODE_MODE (vect) == ER_NM_heap_pack_vect)
	{
	  el_type = ER_pack_vect_el_mode (vect);
	  el_size = type_size_table [el_type];
	  memmove (ER_pack_els (vect) + start * el_size,
		   ER_pack_els (vect) + (start + length) * el_size,
		   el_size * (vect_length - start - length));
	  if (el_type == ER_NM_byte)
	    ((byte_t *) ER_pack_els (vect)) [vect_length - length] = '\0';
	  else if (el_type == ER_NM_char)
	    ((ucode_t *) ER_pack_els (vect)) [vect_length - length] = '\0';
	  ER_set_els_number (vect, vect_length - length);
	}
      else
	{
	  memmove ((char *) ER_unpack_els (vect) + start * sizeof (val_t),
		   (char *) ER_unpack_els (vect)
		   + (start + length) * sizeof (val_t),
		   sizeof (val_t) * (vect_length - start - length));
	  ER_set_els_number (vect, vect_length - length);
	}
    }
  else if (mode == ER_NM_tab)
    {
      tab = ER_tab (val);
      GO_THROUGH_REDIR (tab);
      if (ER_immutable (tab))
	eval_error (immutable_bc_decl, call_pos (),
		    DERR_immutable_table_modification);
      remove_tab_el (tab, IVAL (ctop, -pars_number + 2));
    }
  else
    eval_error (partype_bc_decl,
		call_pos (), DERR_parameter_type, DEL_NAME);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, mode);
  if (mode == ER_NM_tab)
    ER_set_tab (fun_result, tab);
  else
    set_vect_dim (fun_result, vect, 0);
}

/* ????? Use disp. */
static void
general_ins_call (int pars_number, int vector_flag)
{
  ER_node_t vect_val;
  ER_node_t el_val;
  ER_node_t index_val;
  ER_node_t vect;
  ER_node_t el_vect;
  ER_node_mode_t el_type;
  size_t addition;
  size_t vect_length;
  size_t el_size;
  rint_t index;

  if (pars_number != 2  && pars_number != 3)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number,
		(!vector_flag ? INS_NAME : INSV_NAME));
  vect_val = IVAL (ctop, -pars_number + 1);
  el_val = IVAL (ctop, -pars_number + 2);
  if (pars_number == 3)
    {
      implicit_int_conversion (ctop, NULL);
      index_val = IVAL (ctop, 0);
    }
  else
    index_val = NULL;
  if (ER_NODE_MODE (vect_val) != ER_NM_vect
      || (vector_flag && ER_NODE_MODE (el_val) != ER_NM_vect)
      || (index_val != NULL && ER_NODE_MODE (index_val) != ER_NM_int))
    eval_error (partype_bc_decl, call_pos (),
		DERR_parameter_type, (vector_flag ? INSV_NAME : INS_NAME));
  if (index_val != NULL)
    index = ER_i (index_val);
  else
    index = 0;
  vect = ER_vect (vect_val);
  GO_THROUGH_REDIR (vect);
  if (vector_flag)
    {
      el_vect = ER_vect (el_val);
      GO_THROUGH_REDIR (el_vect);
      addition = ER_els_number (el_vect);
    }
  if (ER_immutable (vect))
    eval_error (immutable_bc_decl, call_pos (),
		DERR_immutable_vector_modification);
  if (vector_flag && ER_NODE_MODE (el_vect) == ER_NM_heap_pack_vect)
    {
      if (ER_NODE_MODE (vect) != ER_NM_heap_pack_vect)
	el_vect = unpack_vector (el_vect);
      else if (ER_pack_vect_el_mode (vect) == ER_NM_char
	       && ER_pack_vect_el_mode (el_vect) == ER_NM_byte)
	el_vect = bytevect_to_ucodevect (el_vect);
      else if (ER_pack_vect_el_mode (vect) == ER_NM_byte
	       && ER_pack_vect_el_mode (el_vect) == ER_NM_char)
	vect = bytevect_to_ucodevect (vect);
      else if (ER_pack_vect_el_mode (vect) != ER_pack_vect_el_mode (el_vect))
	el_vect = unpack_vector (el_vect);
    }

  if (ER_NODE_MODE (vect) == ER_NM_heap_pack_vect)
    {
      if (vector_flag)
	{
	  if (ER_NODE_MODE (el_vect) != ER_NM_heap_pack_vect
	      || ER_pack_vect_el_mode (vect) != ER_pack_vect_el_mode (el_vect))
	    vect = unpack_vector (vect);
	}
      else if (ER_pack_vect_el_mode (vect) == ER_NM_byte
	       && ER_NODE_MODE (el_val) == ER_NM_char
	       && ! in_byte_range_p (ER_ch (el_val)))
	vect = bytevect_to_ucodevect (vect);
      else if (ER_pack_vect_el_mode (vect) != ER_NODE_MODE (el_val))
	vect = unpack_vector (vect);
    }
  if (!vector_flag)
    addition = 1;
  else
    addition = ER_els_number (el_vect);
  vect_length = ER_els_number (vect);
  vect = expand_vector (vect, vect_length + addition);
  if (index < 0 || index > vect_length)
    index = vect_length;
  if (index < vect_length)
    {
      /* Move */
      if (ER_NODE_MODE (vect) == ER_NM_heap_pack_vect)
	{
	  el_type = ER_pack_vect_el_mode (vect);
	  el_size = type_size_table [el_type];
	  memmove (ER_pack_els (vect) + (index + addition) * el_size,
		   ER_pack_els (vect) + index * el_size,
		   el_size * (vect_length - index));
	}
      else
	memmove ((char *) ER_unpack_els (vect)
		 + (index + addition) * sizeof (val_t),
		 (char *) ER_unpack_els (vect) + index * sizeof (val_t),
		 sizeof (val_t) * (vect_length - index));
    }
  if (ER_NODE_MODE (vect) == ER_NM_heap_pack_vect)
    {
      el_type = ER_pack_vect_el_mode (vect);
      el_size = type_size_table [el_type];
      if (!vector_flag)
	memcpy (ER_pack_els (vect) + index * el_size,
		(char *) el_val
		+ val_displ_table [ER_NODE_MODE (el_val)], el_size);
      else
	{
	  d_assert (ER_NODE_MODE (el_vect) == ER_NM_heap_pack_vect
		    && el_type == ER_pack_vect_el_mode (el_vect));
	  memcpy (ER_pack_els (vect) + index * el_size,
		  ER_pack_els (el_vect), el_size * addition);
	}
      if (el_type == ER_NM_byte)
	((byte_t *) ER_pack_els (vect)) [vect_length + addition] = '\0';
      else if (el_type == ER_NM_char)
	((ucode_t *) ER_pack_els (vect)) [vect_length + addition] = '\0';
    }
  else
    {
      if (!vector_flag)
	*(val_t *) IVAL (ER_unpack_els (vect), index) = *(val_t *) el_val;
      else
	memcpy (IVAL (ER_unpack_els (vect), index),
		ER_unpack_els (el_vect), addition * sizeof (val_t));
    }
  ER_set_els_number (vect, vect_length + addition);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

void
ins_call (int pars_number)
{
  general_ins_call (pars_number, FALSE);
}

void
insv_call (int pars_number)
{
  general_ins_call (pars_number, TRUE);
}

void
rev_call (int pars_number)
{
  ER_node_t vect;
  ER_node_mode_t el_type;
  size_t vect_length;
  size_t el_size;
  size_t i, j;
  val_t temp_val;
  char temp_el [sizeof (rfloat_t) * 8];

  if (pars_number != 1)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, REV_NAME);
  if (ER_NODE_MODE (ctop) != ER_NM_vect)
    eval_error (partype_bc_decl, call_pos (), DERR_parameter_type, REV_NAME);
  vect = ER_vect (ctop);
  GO_THROUGH_REDIR (vect);
#ifdef NEW_VECTOR
  vect = copy_vector (vect);
#else
  if (ER_immutable (vect))
    eval_error (immutable_bc_decl, call_pos (),
		DERR_immutable_vector_modification);
#endif
  vect_length = ER_els_number (vect);
  if (vect_length != 0)
    {
      if (ER_NODE_MODE (vect) == ER_NM_heap_pack_vect)
	{
	  el_type = ER_pack_vect_el_mode (vect);
	  el_size = type_size_table [el_type];
	  for (i = 0, j = vect_length - 1; i < j; i++, j--)
	    {
	      memcpy (temp_el, ER_pack_els (vect) + i * el_size, el_size);
	      memcpy (ER_pack_els (vect) + i * el_size,
		      ER_pack_els (vect) + j * el_size, el_size);
	      memcpy (ER_pack_els (vect) + j * el_size, temp_el, el_size);
	    }
	}
      else
	{
	  for (i = 0, j = vect_length - 1; i < j; i++, j--)
	    {
	      temp_val = *(val_t *) IVAL (ER_unpack_els (vect), i);
	      *(val_t *) IVAL (ER_unpack_els (vect), i)
		= *(val_t *) IVAL (ER_unpack_els (vect), j);
	      *(val_t *) IVAL (ER_unpack_els (vect), j) = temp_val;
	    }
	}
    }
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

/* The following variable contains type of homogeneous array being
   sorted. */
static ER_node_mode_t sorted_vect_el_type;

/* The function is comparison function for sorting homogeneous
   array. */
static int
homogeneous_array_sort_compare_function (const void *el1, const void *el2)
{
  return compare_elements (sorted_vect_el_type, el1, el2);
}

static BC_node_t dino_compare_fun_block;
static ER_node_t dino_compare_fun_block_context;

static int
array_sort_compare_function (const void *el1, const void *el2)
{
  int res;

  TOP_UP;
  if (sorted_vect_el_type == ER_NM_byte)
    {
      ER_SET_MODE (ctop, ER_NM_char);
      ER_set_ch (ctop, *(byte_t *) el1);
    }
  else if (sorted_vect_el_type != ER_NM_val)
    {
      ER_SET_MODE (ctop, sorted_vect_el_type);
      memcpy ((char *) ctop + val_displ_table [sorted_vect_el_type],
	      (char *) el1, type_size_table [sorted_vect_el_type]);
      if (sorted_vect_el_type == ER_NM_vect)
	ER_set_dim (ctop, 0);
    }
  else
    *(val_t *) ctop = *(val_t *) el1;
  TOP_UP;
  if (sorted_vect_el_type == ER_NM_byte)
    {
      ER_SET_MODE (ctop, ER_NM_char);
      ER_set_ch (ctop, *(byte_t *) el2);
    }
  else if (sorted_vect_el_type != ER_NM_val)
    {
      ER_SET_MODE (ctop, sorted_vect_el_type);
      memcpy ((char *) ctop + val_displ_table [sorted_vect_el_type],
	      (char *) el2, type_size_table [sorted_vect_el_type]);
      if (sorted_vect_el_type == ER_NM_vect)
	ER_set_dim (ctop, 0);
    }
  else
    *(val_t *) ctop = *(val_t *) el2;
  call_fun_class (dino_compare_fun_block, dino_compare_fun_block_context, 2,
		  curr_from_c_code_p);
  TOP_UP;
  implicit_int_conversion (ctop, NULL);
  if (ER_NODE_MODE (ctop) != ER_NM_int)
    eval_error (invresult_bc_decl, call_pos (), DERR_invalid_result, SORT_NAME);
  res = ER_i (ctop);
  TOP_DOWN;
  return res;
}

void
sort_call (int pars_number)
{
  ER_node_t vect;
  ER_node_t var;
  ptrdiff_t offset = (char *) fun_result - (char *) cstack;

  if (pars_number != 1 && pars_number != 2)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, SORT_NAME);
  var = IVAL (ctop, -pars_number + 1);
  if (ER_NODE_MODE (var) == ER_NM_vect)
    {
      vect = ER_vect (var);
      GO_THROUGH_REDIR (vect);
      try_full_pack (vect);
      ER_set_vect (var, vect);
    }
  if (pars_number == 1)
    {
      if (ER_NODE_MODE (ctop) != ER_NM_vect
	  || ER_NODE_MODE (ER_vect (ctop)) != ER_NM_heap_pack_vect
	  || (ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_char
	      && ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_byte
	      && ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_int
	      && ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_long
	      && ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_float))
	eval_error (partype_bc_decl, call_pos (),
		    DERR_parameter_type, SORT_NAME);
      vect = copy_vector (ER_vect (ctop));
      sorted_vect_el_type = ER_pack_vect_el_mode (vect);
      qsort (ER_pack_els (vect), ER_els_number (vect),
	     type_size_table [sorted_vect_el_type],
	     homogeneous_array_sort_compare_function);
    }
  else
    {
      if (ER_NODE_MODE (below_ctop) != ER_NM_vect || ! function_p (ctop))
	eval_error (partype_bc_decl, call_pos (),
		    DERR_parameter_type, SORT_NAME);
      vect = copy_vector (ER_vect (below_ctop));
      dino_compare_fun_block_context = ER_code_context (ctop);
      dino_compare_fun_block = ID_TO_CODE (ER_code_id (ctop));
      if (ER_NODE_MODE (vect) == ER_NM_heap_unpack_vect)
	sorted_vect_el_type = ER_NM_val;
      else
	sorted_vect_el_type = ER_pack_vect_el_mode (vect);
      /* We can not do GC as we can not move the array.  */
      no_gc_p = TRUE;
      DECR_CTOP (2); /* free place for compare function params.  */
      qsort ((sorted_vect_el_type == ER_NM_val
	      ? (char *) ER_unpack_els (vect) : ER_pack_els (vect)),
	     ER_els_number (vect),
	     (sorted_vect_el_type != ER_NM_val
	      ? type_size_table [sorted_vect_el_type]
	      : sizeof (val_t)),
	     array_sort_compare_function);
      DECR_CTOP (-2);
      no_gc_p = FALSE;
    }
  /* Place the result instead of the function. */
  fun_result = (ER_node_t) ((char *) cstack + offset);
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

static int
print_context (ER_node_t context)
{
  BC_node_t block;
  string_t ident;
  char str [100];

  if (context == NULL || ER_context (context) == NULL)
    /* We ignore the uppest implicit block. */
    return FALSE;
  block = ER_block_node (context);
  ident = (BC_NODE_MODE (block) == BC_NM_block
	   ? NULL : BC_ident (BC_fdecl (block)));
  if (print_context (ER_context (context)))
    VLO_ADD_STRING (temp_vlobj, ".");
  if (ident == NULL)
    VLO_ADD_STRING (temp_vlobj, "{}");
  else
    {
      VLO_ADD_STRING (temp_vlobj, ident);
      sprintf (str, "(%ld)", (long int) ER_context_number (context));
      VLO_ADD_STRING (temp_vlobj, str);
    }
  return TRUE;
}

static FILE *
get_file (int pars_number, const char *function_name)
{
  ER_node_t var;
  ER_node_t instance;

  var = IVAL (ctop, -pars_number + 1);
  if (!ER_IS_OF_TYPE (var, ER_NM_stack)
      || ER_stack_block (ER_stack (var)) != file_bc_decl)
    eval_error (partype_bc_decl, call_pos (),
		DERR_parameter_type, function_name);
  instance = ER_stack ((ER_node_t) IVAL (ctop, -pars_number + 1));
  return ER_hide (IVAL (ER_stack_vars (instance),
			BC_var_num (file_ptr_bc_decl)));
}

static void
place_file_instance (FILE *f, ER_node_t result)
{
  ER_node_t var;
  ER_node_t instance;

  instance = create_class_stack (file_bc_decl, uppest_stack,
				 (val_t *) result, 0, TRUE);
  ER_SET_MODE (result, ER_NM_stack);
  ER_set_stack (result, instance);
  var = IVAL (ER_stack_vars (ER_stack (result)), BC_var_num (file_ptr_bc_decl));
  ER_SET_MODE (var, ER_NM_hide);
  ER_set_hide (var, f);
}

static void
two_strings_fun_start (int pars_number, const char *function_name)
{
  if (pars_number != 2)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, function_name);
  to_vect_string_conversion (ctop, NULL, NULL);
  to_vect_string_conversion (below_ctop, NULL, NULL);
  if (ER_NODE_MODE (ctop) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (ctop)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_byte)
      || ER_NODE_MODE (below_ctop) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (below_ctop)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (below_ctop)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (below_ctop)) != ER_NM_byte))
    eval_error (partype_bc_decl,
		call_pos (), DERR_parameter_type, function_name);
}

/* Put byte (if BYTE_P) or unicode string STR into vlo *VLO.  */
static inline const char *
general_str_to_utf8 (void *str, vlo_t *vlo, int byte_p)
{
  if (byte_p)
    return byte_str_to_utf8_vlo ((byte_t *) str, vlo);
  else
    return ucode_str_to_utf8_vlo ((ucode_t *) str, vlo);
}

/* Put byte or unicode vector VECT into temp_vlobj (if FIRST_P) or
   temp_vlobj2.  */
static const char *
strvect_to_utf8 (ER_node_t vect, int first_p)
{
  ER_node_mode_t el_type;
  
  d_assert (ER_NODE_MODE (vect) == ER_NM_heap_pack_vect);
  el_type = ER_pack_vect_el_mode (vect);
  d_assert (el_type == ER_NM_char || el_type == ER_NM_byte);
  return general_str_to_utf8 (ER_pack_els (vect),
			      first_p ? &temp_vlobj : &temp_vlobj2,
			      el_type == ER_NM_byte);
}

void
rename_call (int pars_number)
{
  two_strings_fun_start (pars_number, RENAME_NAME);
  errno = 0;
  rename (strvect_to_utf8 (ER_vect (below_ctop), TRUE),
	  strvect_to_utf8 (ER_vect (ctop), FALSE));
  if (errno)
    process_system_errors (RENAME_NAME);
  /* Pop all actual parameters. */
  ER_SET_MODE (fun_result, ER_NM_undef);
}

static void
string_fun_start (int pars_number, const char *function_name)
{
  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, function_name);
  to_vect_string_conversion (ctop, NULL, NULL);
  if (ER_NODE_MODE (ctop) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (ctop)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_byte))
    eval_error (partype_bc_decl, call_pos (),
		DERR_parameter_type, function_name);
}

void
remove_call (int pars_number)
{
  string_fun_start (pars_number, REMOVE_NAME);
  errno = 0;
  remove (strvect_to_utf8 (ER_vect (ctop), TRUE));
  if (errno)
    process_system_errors (REMOVE_NAME);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_undef);
}

#ifndef S_IRUSR
#ifndef S_IREAD
#define S_IRUSR 00400
#else
#define S_IRUSR S_IREAD
#endif
#endif

#ifndef S_IWUSR
#ifndef S_IWRITE
#define S_IWUSR 00200
#else
#define S_IWUSR S_IWRITE
#endif
#endif

#ifndef S_IXUSR
#ifndef S_IEXEC
#define S_IXUSR 00100
#else
#define S_IXUSR S_IEXEC
#endif
#endif

#ifndef S_ISVTX
#define S_ISVTX 0001000
#endif

#ifndef S_IRGRP
#define S_IRGRP 00040
#endif

#ifndef S_IWGRP
#define S_IWGRP 00020
#endif

#ifndef S_IXGRP
#define S_IXGRP 00010
#endif

#ifndef S_IROTH
#define S_IROTH 00040
#endif

#ifndef S_IWOTH
#define S_IWOTH 00020
#endif

#ifndef S_IXOTH
#define S_IXOTH 00010
#endif

static int
in_str_p (const char *str, int ch)
{
  for (;*str;str++)
    if (*str == ch)
      return TRUE;
  return FALSE;
}

void
mkdir_call (int pars_number)
{
  int mask;

  string_fun_start (pars_number, MKDIR_NAME);
  errno = 0;
  mask = (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP
	  | S_IROTH | S_IWOTH | S_IXOTH);
  mkdir (strvect_to_utf8 (ER_vect (ctop), TRUE), mask);
  if (errno)
    process_system_errors (MKDIR_NAME);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_undef);
}

void
rmdir_call (int pars_number)
{
  string_fun_start (pars_number, RMDIR_NAME);
  errno = 0;
  rmdir (strvect_to_utf8 (ER_vect (ctop), TRUE));
  if (errno)
    process_system_errors (RMDIR_NAME);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_undef);
}

void
getcwd_call (int pars_number)
{
  ER_node_t vect;
  char buf [PATH_MAX + 1], *str;

  if (pars_number != 0)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, GETCWD_NAME);
  errno = 0;
  str = getcwd (buf, PATH_MAX);
  if (errno)
    process_system_errors (GETCWD_NAME);
  vect = create_string (str);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

void
chdir_call (int pars_number)
{
  string_fun_start (pars_number, CHDIR_NAME);
  errno = 0;
  if (chdir (strvect_to_utf8 (ER_vect (ctop), TRUE)) < 0 && errno)
    process_system_errors (CHDIR_NAME);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_undef);
}

static void
get_stat (ER_node_t var, const char *function_name, struct stat *buf)
{
  int result;

  errno = 0;
  if (ER_NODE_MODE (var) == ER_NM_vect
      && ER_NODE_MODE (ER_vect (var)) == ER_NM_heap_pack_vect
      && (ER_pack_vect_el_mode (ER_vect (var)) == ER_NM_char
	  || ER_pack_vect_el_mode (ER_vect (var)) == ER_NM_byte))
    result = stat (strvect_to_utf8 (ER_vect (var), TRUE), buf);
  else if (ER_IS_OF_TYPE (var, ER_NM_stack)
	   && ER_stack_block (ER_stack (var)) == file_bc_decl)
    result
      = fstat (fileno ((FILE *) ER_hide (IVAL (ER_stack_vars (ER_stack (var)),
					       BC_var_num (file_ptr_bc_decl)))),
	       buf);
  else
    eval_error (partype_bc_decl,
		call_pos (), DERR_parameter_type, function_name);
  if (result < 0)
    process_system_errors (function_name);
}

static void
general_chmod (int pars_number, const char *function_name,
	       int clear_mask, int set_mask)
{
  struct stat buf;
  int mask;

  errno = 0;
  get_stat (below_ctop, function_name, &buf);
  mask = (buf.st_mode & ~clear_mask) | set_mask;
  chmod (strvect_to_utf8 (ER_vect (below_ctop), TRUE), mask);
  if (errno)
    process_system_errors (function_name);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_undef);
}

void
chumod_call (int pars_number)
{
  int mask = 0;
  const char *str;

  two_strings_fun_start (pars_number, CHUMOD_NAME);
  str = strvect_to_utf8 (ER_vect (ctop), TRUE);
  if (in_str_p (str, 'r'))
    mask |= S_IRUSR;
  if (in_str_p (str, 'w'))
    mask |= S_IWUSR;
  if (in_str_p (str, 'x'))
    mask |= S_IXUSR;
  if (in_str_p (str, 's'))
    mask |= S_ISVTX;
  general_chmod (pars_number, CHUMOD_NAME,
		 S_IRUSR | S_IWUSR | S_IXUSR | S_ISVTX, mask);
}

void
chgmod_call (int pars_number)
{
  int mask = 0;
  const char *str;

  two_strings_fun_start (pars_number, CHGMOD_NAME);
  str = strvect_to_utf8 (ER_vect (ctop), TRUE);
  if (in_str_p (str, 'r'))
    mask |= S_IRGRP;
  if (in_str_p (str, 'w'))
    mask |= S_IWGRP;
  if (in_str_p (str, 'x'))
    mask |= S_IXGRP;
  general_chmod (pars_number, CHGMOD_NAME, S_IRGRP | S_IWGRP | S_IXGRP, mask);
}

void
chomod_call (int pars_number)
{
  int mask = 0;
  const char *str;

  two_strings_fun_start (pars_number, CHOMOD_NAME);
  str = strvect_to_utf8 (ER_vect (ctop), TRUE);
  if (in_str_p (str, 'r'))
    mask |= S_IROTH;
  if (in_str_p (str, 'w'))
    mask |= S_IWOTH;
  if (in_str_p (str, 'x'))
    mask |= S_IXOTH;
  general_chmod (pars_number, CHOMOD_NAME, S_IROTH | S_IWOTH | S_IXOTH, mask);
}

static FILE *
file_start (int pars_number, const char *function_name)
{
  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, function_name);
  return get_file (pars_number, function_name);
}

void
isatty_call (int pars_number)
{
  rint_t result;
  FILE *f;

  f = file_start (pars_number, ISATTY_NAME);
  result = isatty (fileno (f));
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, result);
}

void
open_call (int pars_number)
{
  FILE *f;

  two_strings_fun_start (pars_number, OPEN_NAME);
  errno = 0;
  f = fopen (strvect_to_utf8 (ER_vect (below_ctop), TRUE),
	     strvect_to_utf8 (ER_vect (ctop), FALSE));
  if (errno)
    process_system_errors (OPEN_NAME);
  else if (f == NULL)
    eval_error (einval_bc_decl, call_pos (), DERR_einval, OPEN_NAME);
  /* Place the result instead of the function. */
  place_file_instance (f, fun_result);
}

void
close_call (int pars_number)
{
  FILE *f;

  f = file_start (pars_number, CLOSE_NAME);
  errno = 0;
  fclose (f);
  if (errno)
    process_system_errors (CLOSE_NAME);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_undef);
}

void
flush_call (int pars_number)
{
  FILE *f;

  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, FLUSH_NAME);
  f = get_file (pars_number, FLUSH_NAME);
  errno = 0;
  fflush (f);
  if (errno)
    process_system_errors (FLUSH_NAME);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_undef);
}

void
popen_call (int pars_number)
{
  FILE *f;
  const char *s;
  
  two_strings_fun_start (pars_number, POPEN_NAME);
  errno = 0;
  s = strvect_to_utf8 (ER_vect (ctop), FALSE);
  if ((*s != 'r' && *s != 'w') || strlen (s) != 1)
    {
      errno = EINVAL;
      process_system_errors (POPEN_NAME);
    }
  f = popen (strvect_to_utf8 (ER_vect (below_ctop), TRUE), s);
  if (errno)
    process_system_errors (POPEN_NAME);
  /* Place the result instead of the function. */
  place_file_instance (f, fun_result);
}

void
pclose_call (int pars_number)
{
  FILE *f;
  int res;

  f = file_start (pars_number, PCLOSE_NAME);
  errno = 0;
  res = pclose (f);
  if (res != 0 && errno)
    process_system_errors (PCLOSE_NAME);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_undef);
}

void
tell_call (int pars_number)
{
  FILE *f;
  rint_t pos;

  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, TELL_NAME);
  f = get_file (pars_number, TELL_NAME);
  errno = 0;
  pos = ftell (f);
  if (errno)
    process_system_errors (TELL_NAME);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, pos);
}

void
seek_call (int pars_number)
{
  FILE *f;
  rint_t pos;
  int whence;
  int ch;

  if (pars_number != 3)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, SEEK_NAME);
  f = get_file (pars_number, SEEK_NAME);
  implicit_arithmetic_conversion (below_ctop, NULL);
  to_vect_string_conversion (ctop, NULL, NULL);
  if (ER_NODE_MODE (below_ctop) != ER_NM_int
      || (ER_NODE_MODE (ctop) != ER_NM_char
	  && (ER_NODE_MODE (ctop) != ER_NM_vect
	      || ER_NODE_MODE (ER_vect (ctop)) != ER_NM_heap_pack_vect
	      || (ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_char
		  && ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_byte))))
    eval_error (partype_bc_decl, call_pos (), DERR_parameter_type, SEEK_NAME);
  pos = ER_i (below_ctop);
  if (ER_NODE_MODE (ctop) == ER_NM_char)
    ch = ER_ch (ctop);
  else
    ch = *strvect_to_utf8 (ER_vect (ctop), TRUE);
  ch = tolower (ch);
  if (ch == 's')
#ifdef SEEK_SET
    whence = SEEK_SET;
#else
    whence = 0;
#endif
  else if (ch == 'c')
#ifdef SEEK_CUR
    whence = SEEK_CUR;
#else
    whence = 1;
#endif
  else if (ch == 'e')
#ifdef SEEK_END
    whence = SEEK_END;
#else
    whence = 2;
#endif
  else
    eval_error (partype_bc_decl, call_pos (), DERR_parameter_type, SEEK_NAME);
  errno = 0;
  fseek (f, pos, whence);
  if (errno)
    process_system_errors (SEEK_NAME);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_undef);
}

static void
print_ch (int ch)
{
  char *str = get_ucode_ascii_repr (ch);

  VLO_ADD_STRING (temp_vlobj, str);
}

#define MAX_REPL_PRINTED_ELEMENTS 50

static void
print_val (ER_node_t val, int quote_flag, int full_p)
{
  BC_node_t code;
  ER_node_t vect;
  ER_node_t tab;
  ER_node_t key;
  size_t i, num;
  int flag;
  char *string;
  static char str [100];

  switch (ER_NODE_MODE (val))
    {
    case ER_NM_nil:
      VLO_ADD_STRING (temp_vlobj, "nil");
      break;
    case ER_NM_hide:
      sprintf (str, "hide value %lx", (long int) ER_hide (val));
      VLO_ADD_STRING (temp_vlobj, str);
      break;
    case ER_NM_hideblock:
      VLO_ADD_STRING (temp_vlobj, "hideblock value (");
      for (i = 0; i < ER_hideblock_length (ER_hideblock (val)); i++)
	{
	  if (i != 0)
	    VLO_ADD_STRING (temp_vlobj, " ");
	  sprintf (str, "%x",
		   (unsigned char)
		   ER_hideblock_start (ER_hideblock (val)) [i]);
	  VLO_ADD_STRING (temp_vlobj, str);
	}
      VLO_ADD_STRING (temp_vlobj, ")");
      break;
    case ER_NM_char:
      if (!quote_flag)
	VLO_ADD_STRING (temp_vlobj,
			ucode_char_to_utf8_vlo (ER_ch (val), &temp_vlobj2));
      else
	{
	  VLO_ADD_STRING (temp_vlobj, "\'");
	  print_ch (ER_ch (val));
	  VLO_ADD_STRING (temp_vlobj, "\'");
	}
      break;
    case ER_NM_int:
      if (sizeof (rint_t) == sizeof (int))
	sprintf (str, "%d", ER_i (val));
      else if (sizeof (rint_t) == sizeof (long))
	sprintf (str, "%ld", ER_i (val));
      else
	sprintf (str, "%lld", ER_i (val));
      VLO_ADD_STRING (temp_vlobj, str);
      break;
    case ER_NM_long:
      {
	ER_node_t heap_mpz = ER_l (val);
	
	VLO_ADD_STRING (temp_vlobj, mpz2a (*ER_mpz_ptr (heap_mpz), 10, FALSE));
	VLO_ADD_STRING (temp_vlobj, "l");
      }
      break;
    case ER_NM_float:
      sprintf (str, "%g", ER_f (val));
      VLO_ADD_STRING (temp_vlobj, str);
      break;
    case ER_NM_vect:
      to_vect_string_conversion (val, NULL, NULL);
      vect = ER_vect (val);
      if (ER_NODE_MODE (vect) == ER_NM_heap_pack_vect
	  && (ER_pack_vect_el_mode (vect) == ER_NM_char
	      || ER_pack_vect_el_mode (vect) == ER_NM_byte))
	{
	  if (!quote_flag)
	    VLO_ADD_STRING (temp_vlobj, strvect_to_utf8 (vect, FALSE));
	  else
	    {
	      VLO_ADD_STRING (temp_vlobj, "\"");
	      if (ER_pack_vect_el_mode (vect) == ER_NM_byte)
		for (string = (char *) ER_pack_els (vect);
		     *string != '\0';
		     string++)
		  print_ch (*string);
	      else
		for (string = ER_pack_els (vect);
		     *(ucode_t *) string != '\0';
		     string += sizeof (ucode_t))
		  print_ch (*(ucode_t *) string);
	      VLO_ADD_STRING (temp_vlobj, "\"");
	    }
	}
      else if (ER_NODE_MODE (vect) == ER_NM_heap_unpack_vect)
	{
	  VLO_ADD_STRING (temp_vlobj, "[");
	  for (i = 0; i < ER_els_number (vect); i++)
	    {
	      if (repl_flag && i >= MAX_REPL_PRINTED_ELEMENTS)
		{
		  VLO_ADD_STRING (temp_vlobj, "...");
		  break;
		}
	      print_val (IVAL (ER_unpack_els (vect), i), TRUE, TRUE);
	      if (i < ER_els_number (vect) - 1)
		VLO_ADD_STRING (temp_vlobj, ", ");
	    }
	  VLO_ADD_STRING (temp_vlobj, "]");
	}
      else
	{
	  ER_node_mode_t el_type = ER_pack_vect_el_mode (vect);
	  val_t temp_val;
	  size_t displ;
	  size_t el_size;

	  VLO_ADD_STRING (temp_vlobj, "[");
	  ER_SET_MODE ((ER_node_t) &temp_val, el_type);
	  displ = val_displ_table [el_type];
	  el_size = type_size_table [el_type];
	  for (i = 0; i < ER_els_number (vect); i++)
	    {
	      if (repl_flag && i >= MAX_REPL_PRINTED_ELEMENTS)
		{
		  VLO_ADD_STRING (temp_vlobj, "...");
		  break;
		}
	      /* We don't care about vector dimension here.  */
	      memcpy ((char *) &temp_val + displ,
		      (char *) ER_pack_els (vect) + i * el_size, el_size);
	      print_val ((ER_node_t) &temp_val, TRUE, TRUE);
	      if (i < ER_els_number (vect) - 1)
		VLO_ADD_STRING (temp_vlobj, ", ");
	    }
	  VLO_ADD_STRING (temp_vlobj, "]");
	}
      break;
    case ER_NM_tab:
      VLO_ADD_STRING (temp_vlobj, "tab [");
      tab = ER_tab (val);
      GO_THROUGH_REDIR (tab);
      flag = FALSE;
      for (num = i = 0; i < ER_els_bound (tab); i++)
	{
	  key = INDEXED_EL_KEY (ER_tab_els (tab), i);
	  if (ER_NODE_MODE (key) == ER_NM_empty_el)
	    continue;
	  num++;
	  if (repl_flag && num > MAX_REPL_PRINTED_ELEMENTS)
	    {
	      VLO_ADD_STRING (temp_vlobj, ", ...");
	      break;
	    }
	  if (flag)
	    VLO_ADD_STRING (temp_vlobj, ", ");
	  print_val (key, TRUE, TRUE);
	  VLO_ADD_STRING (temp_vlobj, ":");
	  print_val (INDEXED_EL_VAL (ER_tab_els (tab), i), TRUE, TRUE);
	  flag = TRUE;
	}
      VLO_ADD_STRING (temp_vlobj, "]");
      break;
    case ER_NM_code:
      code = ID_TO_CODE (ER_code_id (val));
      if (BC_NODE_MODE (code) == BC_NM_block)
	;
      else if (BC_fun_p (code))
	VLO_ADD_STRING (temp_vlobj, "fun ");
      else if (BC_class_p (code))
	VLO_ADD_STRING (temp_vlobj, "class ");
      else if (BC_thread_p (code))
	VLO_ADD_STRING (temp_vlobj, "thread ");
      if (print_context (ER_code_context (val)))
	VLO_ADD_STRING (temp_vlobj, ".");
      if (BC_NODE_MODE (code) == BC_NM_fblock)
	VLO_ADD_STRING (temp_vlobj, BC_ident (BC_fdecl (code)));
      break;
    case ER_NM_stack:
      {
	BC_node_t block = ER_block_node (ER_stack (val));
	
	if (BC_NODE_MODE (block) == BC_NM_fblock && BC_class_p (block))
	  VLO_ADD_STRING (temp_vlobj, "instance ");
	else
	  VLO_ADD_STRING (temp_vlobj, "stack ");
	/* Context may be uppest block stack. */
	print_context (ER_stack (val));
	break;
      }
    case ER_NM_process:
      if (ER_process_block (ER_process (val)) == NULL) /* ??? */
	VLO_ADD_STRING (temp_vlobj, "main thread");
      else
	{
	  ER_node_t stack;

	  for (stack = ER_saved_cstack (ER_process (val));
	       stack != NULL;
	       stack = ER_prev_stack (stack))
	    if (BC_NODE_MODE (ER_stack_block (stack)) == BC_NM_fblock
		&& BC_thread_p (ER_stack_block (stack)))
	      break;
	  sprintf (str, "thread %ld ",
		   (long int) ER_process_number (ER_process (val)));
	  VLO_ADD_STRING (temp_vlobj, str);
	  if (!print_context (stack))
	    d_unreachable ();
	}
      break;
    case ER_NM_type:
      switch (ER_type (val))
	{
	case type_nil:
	  string = "type (nil)";
	  break;
	case type_char:
	  string = "char";
	  break;
	case type_int:
	  string = "int";
	  break;
	case type_long:
	  string = "long";
	  break;
	case type_float:
	  string = "float";
	  break;
	case type_hide:
	  string = "hide";
	  break;
	case type_hideblock:
	  string = "hideblock";
	  break;
	case type_vect:
	  string = "vector";
	  break;
	case type_tab:
	  string = "table";
	  break;
	case type_fun:
	  string = "fun";
	  break;
	case type_class:
	  string = "class";
	  break;
	case type_thread:
	  string = "thread";
	  break;
	case type_obj:
	  string = "obj";
	  break;
	case type_process:
	  string = "process";
	  break;
	case type_type:
	  string = "type";
	  break;
	default:
	  d_unreachable ();
	}
      VLO_ADD_STRING (temp_vlobj, string);
      break;
    case ER_NM_undef:
      d_assert (repl_flag);
      VLO_ADD_STRING (temp_vlobj, "undef");
      break;
    default:
      d_unreachable ();
    }
}

void
repl_print (ER_node_t val, int def_p)
{
  if (def_p && ER_NODE_MODE (val) == ER_NM_undef)
    return;
  VLO_NULLIFY (temp_vlobj);
  print_val (val, TRUE, FALSE);
  puts (VLO_BEGIN (temp_vlobj));
}

static FILE *
file_function_call_start (int pars_number, const char *function_name)
{
  if (pars_number == 0)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, function_name);
  return get_file (pars_number, function_name);
}

enum file_param_type
{
  NO_FILE,
  STANDARD_FILE,
  GIVEN_FILE
};

/* Output byte (if BYTE_P) or ucode string in temp_vlobj to file F as
   utf8 sequence as set fun_result to undef .  If F is null, create
   corresponding byte or ucode vector.  Set fun_result to it.  */
static void
finish_output (FILE *f, int byte_p)
{
  ER_node_t vect;

  if (f != NULL)
    {
      fputs (general_str_to_utf8 (VLO_BEGIN (temp_vlobj),
				  &temp_vlobj2, byte_p), f);
      /* Place the result instead of the function. */
      ER_SET_MODE (fun_result, ER_NM_undef);
      return;
    }
  else if (byte_p)
    vect = create_string (VLO_BEGIN (temp_vlobj));
  else
    vect = create_ucodestr (VLO_BEGIN (temp_vlobj));
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

static void
general_put_call (FILE *f, int pars_number, int ln_flag,
		  enum file_param_type param_type)
{
  int i, byte_p, res_byte_p;
  size_t ch_size;
  const char *function_name, *start;
  ER_node_t var;

  errno = 0;
  if (param_type == NO_FILE)
    {
      function_name = (ln_flag ? SPUTLN_NAME : SPUT_NAME);
      d_assert (f == NULL);
    }
  else if (param_type == STANDARD_FILE)
    function_name = (ln_flag ? PUTLN_NAME : PUT_NAME);
  else
    function_name = (ln_flag ? FPUTLN_NAME : FPUT_NAME);
  res_byte_p = TRUE;
  VLO_NULLIFY (temp_vlobj);
  for (i = -pars_number + (param_type == GIVEN_FILE ? 1 : 0) + 1; i <= 0; i++)
    {
      var = IVAL (ctop, i);
      to_vect_string_conversion (var, NULL, NULL);
      if (ER_NODE_MODE (var) != ER_NM_vect
	  || ER_NODE_MODE (ER_vect (var)) != ER_NM_heap_pack_vect
	  || (ER_pack_vect_el_mode (ER_vect (var)) != ER_NM_char
	      && ER_pack_vect_el_mode (ER_vect (var)) != ER_NM_byte))
	eval_error (partype_bc_decl, call_pos (),
		    DERR_parameter_type, function_name);
      byte_p = ER_pack_vect_el_mode (ER_vect (var)) == ER_NM_byte;
      start = ER_pack_els (ER_vect (var));
      if (res_byte_p && ! byte_p)
	{
	  copy_vlo (&temp_vlobj2, &temp_vlobj);
	  str_to_ucode_vlo (&temp_vlobj, VLO_BEGIN (temp_vlobj2),
			    VLO_LENGTH (temp_vlobj2));
	  res_byte_p = TRUE;
	}
      else if (! res_byte_p && byte_p)
	{
	  str_to_ucode_vlo (&temp_vlobj2, start, strlen (start));
	  start = VLO_BEGIN (temp_vlobj2);
	  byte_p = FALSE;
	}
      ch_size = byte_p ? sizeof (byte_t) : sizeof (ucode_t);
      VLO_ADD_MEMORY (temp_vlobj,
		      start, ER_els_number (ER_vect (var)) * ch_size);
    }
  if (res_byte_p)
    {
      if (ln_flag)
	VLO_ADD_BYTE (temp_vlobj, '\n');
      VLO_ADD_BYTE (temp_vlobj, 0);
    }
  else
    {
      ucode_t uc[] = {'\n', 0};
      
      if (ln_flag)
	VLO_ADD_MEMORY (temp_vlobj, uc, sizeof (ucode_t) * 2);
      else
	VLO_ADD_MEMORY (temp_vlobj, &uc[1], sizeof (ucode_t));
    }
  if (errno != 0)
    process_system_errors (function_name);
  finish_output (f, res_byte_p);
}

void
put_call (int pars_number)
{
  general_put_call (stdout, pars_number, FALSE, STANDARD_FILE);
}

void
putln_call (int pars_number)
{
  general_put_call (stdout, pars_number, TRUE, STANDARD_FILE);
}

void
fput_call (int pars_number)
{
  general_put_call (file_function_call_start (pars_number, FPUT_NAME),
		    pars_number, FALSE, GIVEN_FILE);
}

void
fputln_call (int pars_number)
{
  general_put_call (file_function_call_start (pars_number, FPUTLN_NAME),
		    pars_number, TRUE, GIVEN_FILE);
}

void
sput_call (int pars_number)
{
  general_put_call (NULL, pars_number, FALSE, NO_FILE);
}

void
sputln_call (int pars_number)
{
  general_put_call (NULL, pars_number, TRUE, NO_FILE);
}

static void
general_print_call (FILE *f, int pars_number, int ln_flag,
		    enum file_param_type param_type)
{
  int i;
  const char *function_name;

  errno = 0;
  if (param_type == NO_FILE)
    {
      function_name = (ln_flag ? SPRINTLN_NAME : SPRINT_NAME);
      d_assert (f == NULL);
    }
  else if (param_type == STANDARD_FILE)
    function_name = (ln_flag ? PRINTLN_NAME : PRINT_NAME);
  else
    function_name = (ln_flag ? FPRINTLN_NAME : FPRINT_NAME);
  VLO_NULLIFY (temp_vlobj);
  for (i = -pars_number + (param_type == GIVEN_FILE ? 1 : 0) + 1; i <= 0; i++)
    print_val (IVAL (ctop, i), TRUE, TRUE);
  if (errno != 0)
    process_system_errors (function_name);
  if (ln_flag)
    VLO_ADD_STRING (temp_vlobj, "\n");
  if (errno != 0)
    process_system_errors (function_name);
  finish_output (f, TRUE);
}

void
print_call (int pars_number)
{
  general_print_call (stdout, pars_number, FALSE, STANDARD_FILE);
}

void
println_call (int pars_number)
{
  general_print_call (stdout, pars_number, TRUE, STANDARD_FILE);
}

void
fprint_call (int pars_number)
{
  general_print_call (file_function_call_start (pars_number, FPRINT_NAME),
		      pars_number, FALSE, GIVEN_FILE);
}

void
fprintln_call (int pars_number)
{
  general_print_call (file_function_call_start (pars_number, FPRINTLN_NAME),
		      pars_number, TRUE, GIVEN_FILE);
}

void
sprint_call (int pars_number)
{
  general_print_call (NULL, pars_number, FALSE, NO_FILE);
}

void
sprintln_call (int pars_number)
{
  general_print_call (NULL, pars_number, TRUE, NO_FILE);
}

static void
general_get_call (FILE *f, int file_flag)
{
  ucode_t ch;

  errno = 0;
  ch = get_ucode_from_utf8_stream (read_byte, f); // ???
  if (errno != 0)
    process_system_errors (file_flag ? FGET_NAME : GET_NAME);
  if (ch == EOF)
    eval_error (eof_bc_decl, call_pos (), DERR_eof_occured,
		file_flag ? FGET_NAME : GET_NAME);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_char);
  ER_set_ch (fun_result, ch);
}

static void
general_get_ln_file_call (FILE *f, int param_flag, int ln_flag, int as_lns_p,
			  const char *fun_name)
{
  ER_node_t vect;
  int ch;
  size_t ch_n, els_number, i;

  VLO_NULLIFY (temp_vlobj);
  if (!ln_flag && as_lns_p)
    VLO_NULLIFY (temp_vlobj2);
  errno = 0;
  ch_n = 0;
  for (;;)
    {
      ch  = fgetc (f);
      if (ch != EOF)
	ch_n++;
      if ((ch == '\n' && (ln_flag || as_lns_p)) || ch == EOF)
	{
	  if (ln_flag || !as_lns_p
	      || ch == '\n' || VLO_LENGTH (temp_vlobj) != 0)
	    {
	      VLO_ADD_BYTE (temp_vlobj, '\0');
	      vect = create_string (VLO_BEGIN (temp_vlobj));
	      if (!ln_flag && as_lns_p)
		{
		  VLO_NULLIFY (temp_vlobj);
		  VLO_ADD_MEMORY (temp_vlobj2, &vect, sizeof (vect));
		}
	    }
	  if (ln_flag || ch == EOF)
	    break;
	}
      else
	VLO_ADD_BYTE (temp_vlobj, ch);
    }
  if (!ln_flag && as_lns_p)
    {
      els_number = VLO_LENGTH (temp_vlobj2) / sizeof (ER_node_t);
      vect = create_pack_vector (els_number, ER_NM_vect);
      for (i = 0; i < els_number; i++)
	set_packed_vect_el (vect, i,
			    ((ER_node_t *) VLO_BEGIN (temp_vlobj2)) [i]);
    }
  if (errno != 0)
    process_system_errors (fun_name);
  /* ??? */
  if (ch == EOF && ch_n == 0)
    eval_error (eof_bc_decl, call_pos (), DERR_eof_occured, fun_name);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

void
get_call (int pars_number)
{
  if (pars_number != 0)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, GET_NAME);
  general_get_call (stdin, FALSE);
}

void
getln_call (int pars_number)
{
  if (pars_number != 0)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, GETLN_NAME);
  general_get_ln_file_call (stdin, FALSE, TRUE, FALSE, GETLN_NAME);
}

void
getf_call (int pars_number)
{
  int flag = 0;

  if (pars_number > 1)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, GETF_NAME);
  if (pars_number == 1)
    {
      implicit_int_conversion (ctop, NULL);
      if (!ER_IS_OF_TYPE (ctop, ER_NM_int))
	eval_error (partype_bc_decl, call_pos (),
		    DERR_parameter_type, GETF_NAME);
      flag = ER_i (ctop);
    }
  general_get_ln_file_call (stdin, FALSE, FALSE, flag != 0, GETF_NAME);
}

static FILE *
fget_function_call_start (int pars_number, const char *function_name)
{
  if (pars_number != 1)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, function_name);
  return get_file (pars_number, function_name);
}

void
fget_call (int pars_number)
{
  general_get_call (fget_function_call_start (pars_number, FGET_NAME), TRUE);
}

void
fgetln_call (int pars_number)
{
  general_get_ln_file_call
    (fget_function_call_start (pars_number, FGETLN_NAME),
     TRUE, TRUE, FALSE, FGETLN_NAME);
}

void
fgetf_call (int pars_number)
{
  int flag = 0;

  if (pars_number == 2)
    {
      implicit_int_conversion (ctop, NULL);
      if (!ER_IS_OF_TYPE (ctop, ER_NM_int))
	eval_error (partype_bc_decl,
		    call_pos (), DERR_parameter_type, FGETF_NAME);
      flag = ER_i (ctop);
    }
  else if (pars_number != 1)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, FGETF_NAME);
  general_get_ln_file_call (get_file (pars_number, FGETF_NAME),
			    TRUE, FALSE, flag != 0, FGETF_NAME);
}

#define F_CHAR   (UCODE_MAX + 256)
#define F_INT    (UCODE_MAX + 257)
#define F_FLOAT  (UCODE_MAX + 258)
#define F_LONG   (UCODE_MAX + 259)
#define F_STRING (UCODE_MAX + 260)
#define F_TAB    (UCODE_MAX + 261)

struct token
{
  int token_code;
  union
  {
    ucode_t ch;
    rint_t i;
    rfloat_t f;
    ER_node_t gmp;
    string_t str;
  } val;
};

/* Var length string used by func yylval for text presentation of the
   symbol. */
static vlo_t el_text;

static void
initiate_io (void)
{
  VLO_CREATE (el_text, 0);
}

static void
finish_io (void)
{
  VLO_DELETE (el_text);
}

/* The following function is analogous to `read_dino_string_code' in
   Dino scanner.  If `read_dino_string_code' is changed, please modify
   this function too. */
static ucode_t
get_char_code (FILE *f, ucode_t curr_char,
	       int *correct_newln, int *wrong_escape_code)
{
  ucode_t char_code;

  if (curr_char == EOF || curr_char == '\n')
    {
      ungetc (curr_char, f);
      return (-1);
    }
  *correct_newln = *wrong_escape_code = FALSE;
  if (curr_char == '\\')
    {
      curr_char = get_ucode_from_utf8_stream (read_byte, f);
      if (curr_char == 'n')
        curr_char = '\n';
      else if (curr_char == 't')
        curr_char = '\t';
      else if (curr_char == 'v')
	curr_char = '\v';
      else if (curr_char == 'a')
        curr_char = '\a';
      else if (curr_char == 'b')
        curr_char = '\b';
      else if (curr_char == 'r')
        curr_char = '\r';
      else if (curr_char == 'f')
        curr_char = '\f';
      else if (curr_char == '\\' || curr_char == '\'' || curr_char == '\"')
        ;
      else if (curr_char == '\n')
	*correct_newln = TRUE;
      else if (isdigit (curr_char) && curr_char != '8' && curr_char != '9')
	{
	  char_code = value_of_digit (curr_char);
	  curr_char = get_ucode_from_utf8_stream (read_byte, f);
	  if (!isdigit (curr_char) || curr_char == '8' || curr_char == '9')
	    ungetc (curr_char, f);
	  else
	    {
	      char_code = (char_code * 8 + value_of_digit (curr_char));
	      curr_char = get_ucode_from_utf8_stream (read_byte, f);
	      if (!isdigit (curr_char) || curr_char == '8' || curr_char == '9')
		ungetc (curr_char, f);
	      else
		char_code = (char_code * 8 + value_of_digit (curr_char));
	    }
	  curr_char = char_code;
	}
      else if (curr_char == 'x' || curr_char == 'u' || curr_char == 'U')
	{
	  /* Hex or Unicode escape code.  */
	  int i, c;
	  
	  char_code = 0;
	  for (i = (curr_char == 'x' ? 2 : curr_char == 'u' ? 4 : 8);
	       i > 0;
	       i--)
	    {
	      curr_char = get_ucode_from_utf8_stream (read_byte, f);
	      if (! is_hex_digit (curr_char))
		break;
	      c = value_of_hex_digit (curr_char);
	      char_code = (char_code << 4) | c;
	    }
	  *wrong_escape_code = i > 0;
	  curr_char = char_code;
	}
    }
  return curr_char;
}

static void
invinput_error (FILE *f, const char *function_name, int ln_flag)
{
  int curr_char;

  if (ln_flag)
    do
      {
	curr_char = get_ucode_from_utf8_stream (read_byte, f);
      }
    while (curr_char != EOF && curr_char != '\n');
  eval_error (invinput_bc_decl, call_pos (), DERR_invalid_input, function_name);
}

/* Used by read_dino_number.  */
static FILE *number_file;
static int n_getc (void) { return get_ucode_from_utf8_stream (read_byte, number_file); }
static void n_ungetc (int c) { ungetc (c, number_file); }

/* The following function is analogous to `yylex' in Dino scanner.  If
   `yylex' is changed, please modify this function too. */
static struct token
get_token (FILE *f, const char *function_name, int ln_flag)
{
  int curr_char;
  int wrong_escape_code;
  struct token result;

  VLO_NULLIFY (el_text);
  for (;;)
    {
      curr_char = get_ucode_from_utf8_stream (read_byte, f);
      /* `current_position' corresponds `curr_char' here. */
      switch (curr_char)
        {
          /* Break results in skipping all white spaces. */
        case ' ':
        case '\f':
        case '\t':
        case '\r':
        case '\n':
          break;
        case ':':
        case ',':
        case '[':
        case ']':
        case EOF:
	  result.token_code = curr_char;
	  return result;
	case 't':
	  curr_char = get_ucode_from_utf8_stream (read_byte, f);
	  if (curr_char != 'a')
	    invinput_error (f, function_name, ln_flag);
	  curr_char = get_ucode_from_utf8_stream (read_byte, f);
	  if (curr_char != 'b')
	    invinput_error (f, function_name, ln_flag);
	  result.token_code = F_TAB;
	  return result;
        case '\'':
          {
            int correct_newln, wrong_escape_code;
	    ucode_t char_code;
            
            curr_char = get_ucode_from_utf8_stream (read_byte, f);
            if (curr_char == '\'')
	      invinput_error (f, function_name, ln_flag);
            else
              {
                curr_char = get_char_code (f, curr_char, &correct_newln, &wrong_escape_code);
                if (curr_char < 0 || correct_newln || wrong_escape_code)
		  {
		    if (ln_flag && curr_char == '\n')
		      ungetc (curr_char, f);
		    invinput_error (f, function_name, ln_flag);
		  }
              }
            char_code = get_ucode_from_utf8_stream (read_byte, f);
            if (char_code != '\'')
              {
                ungetc (char_code, f);
		invinput_error (f, function_name, ln_flag);
              }
	    result.val.ch = curr_char;
	    result.token_code = F_CHAR;
            return result;
          }
        case '\"':
          {
            int correct_newln, wrong_escape_code;
            
            for (;;)
              {
                curr_char = get_ucode_from_utf8_stream (read_byte, f);
                if (curr_char == '\"')
                  break;
                curr_char = get_char_code (f, curr_char, &correct_newln,
					   &wrong_escape_code);
                if (curr_char < 0 || wrong_escape_code)
                  {
		    invinput_error (f, function_name, ln_flag);
                    break;
                  }
                if (!correct_newln)
                  VLO_ADD_BYTE (el_text, curr_char);
              }
            VLO_ADD_BYTE (el_text, '\0');
	    result.val.str = VLO_BEGIN (el_text);
	    result.token_code = F_STRING;
            return result;
          }
        default:
	  {
	    int next_char = get_ucode_from_utf8_stream (read_byte, f);

	    ungetc (next_char, f);
	    if (isdigit (curr_char)
		|| ((curr_char == '-' || curr_char == '+')
		    && isdigit (next_char)))
	      {
		enum read_number_code err_code;
		int read_ch_num, float_p, long_p, base;
		const char *repr;

		number_file = f;
		err_code = read_dino_number (curr_char, n_getc, n_ungetc,
					     &read_ch_num, &repr, &base,
					     &float_p, &long_p);
		if (errno)
		  process_system_errors (function_name);
		if (err_code != NUMBER_OK)
		  {
		    curr_char = get_ucode_from_utf8_stream (read_byte, f);
		    if (ln_flag && curr_char == '\n')
		      ungetc (curr_char, f);
		    invinput_error (f, function_name, ln_flag);
		  }
		else if (long_p)
		  {
		    ER_node_t gmp = create_gmp ();

		    result.token_code = F_LONG;
		    mpz_set_str (*ER_mpz_ptr (gmp), repr, base);
		    result.val.gmp = gmp;
		  }
		else if (float_p)
		  {
		    result.token_code = F_FLOAT;
		    result.val.f = a2f (repr);
		    if (errno)
		      process_system_errors ("string-to-float conversion");
		  }
		else
		  {
		    result.token_code = F_INT;
		    result.val.i = a2i (repr, base);
		    if (errno)
		      process_system_errors ("string-to-int conversion");
		  }
		return result;
	      }
	    else
	      invinput_error (f, function_name, ln_flag);
	  }
        }
    }
}

/* This resursive function reads a DINO value according to the
   following syntax:

      element : char
              | int-value
              | float-value
              | long-value
              | string
              | '[' [list] ']'
              | tab '[' [list] ']'

      list : [element ':'] element
           | list ',' [element ':'] element

   If syntax (or semantics) of values is changed, please modify this
   function too. */
static val_t
scanel (FILE *f, struct token token, const char *function_name, int ln_flag)
{
  val_t result;
  ER_node_t ptr = (ER_node_t) &result;

  switch (token.token_code)
    {
    case F_CHAR:
      ER_SET_MODE (ptr, ER_NM_char);
      ER_set_ch (ptr, token.val.ch);
      return result;
    case F_INT:
      ER_SET_MODE (ptr, ER_NM_int);
      ER_set_i (ptr, token.val.i);
      return result;
    case F_FLOAT:
      ER_SET_MODE (ptr, ER_NM_float);
      ER_set_f (ptr, token.val.f);
      return result;
    case F_LONG:
      ER_SET_MODE (ptr, ER_NM_long);
      ER_set_l (ptr, token.val.gmp);
      return result;
    case F_STRING:
      {
	ER_node_t vect;

	ER_SET_MODE (ptr, ER_NM_vect);
	vect = create_string (token.val.str);
	set_vect_dim (ptr, vect, 0);
	return result;
      }
    case '[':
      {
	rint_t repeat;
	rint_t i;
	ER_node_t vect;

	vect = create_empty_vector ();
	token = get_token (f, function_name, ln_flag);
	for (;;)
	  {
	    if (token.token_code == ']')
	      {
		ER_SET_MODE (ptr, ER_NM_vect);
		set_vect_dim (ptr, vect, 0);
		return result;
	      }
	    result = scanel (f, token, function_name, ln_flag);
	    token = get_token (f, function_name, ln_flag);
	    if (token.token_code == ':')
	      {
		implicit_int_conversion (ctop, (ER_node_t) &result);
		if (ER_NODE_MODE (ptr) != ER_NM_int)
		  invinput_error (f, function_name, ln_flag);
		repeat = ER_i (ptr);
		if (repeat < 0)
		  repeat = 0;
		token = get_token (f, function_name, ln_flag);
		result = scanel (f, token, function_name, ln_flag);
		token = get_token (f, function_name, ln_flag);
	      }
	    else
	      repeat = 1;
	    if (ER_NODE_MODE (vect) == ER_NM_heap_pack_vect)
	      vect = unpack_vector (vect);
	    vect = expand_vector (vect, ER_els_number (vect) + repeat);
	    for (i = ER_els_number (vect); repeat > 0; i++, repeat--)
	      *(val_t *) IVAL (ER_unpack_els (vect), i) = result;
	    ER_set_els_number (vect, i);
	    if (token.token_code == ',')
	      token = get_token (f, function_name, ln_flag);
	  }
	}
    case F_TAB:
      {
	ER_node_t tab;
	val_t key_val;
	ER_node_t key = (ER_node_t) &key_val;
	ER_node_t entry;
	ER_node_mode_t mode;

	tab = create_tab (40);
	token = get_token (f, function_name, ln_flag);
	if (token.token_code != '[')
	  invinput_error (f, function_name, ln_flag);
	token = get_token (f, function_name, ln_flag);
	for (;;)
	  {
	    if (token.token_code == ']')
	      {
		ER_SET_MODE (ptr, ER_NM_tab);
		ER_set_tab (ptr, tab);
		return result;
	      }
	    result = scanel (f, token, function_name, ln_flag);
	    token = get_token (f, function_name, ln_flag);
	    if (token.token_code == ':')
	      {
		key_val = result;
		mode = ER_NODE_MODE (key);
		token = get_token (f, function_name, ln_flag);
		result = scanel (f, token, function_name, ln_flag);
		token = get_token (f, function_name, ln_flag);
		if (mode == ER_NM_vect)
		  set_vect_dim (key, ER_vect (key), 0);
		else if (mode == ER_NM_tab)
		  ER_set_tab (key, ER_tab (key));
	      }
	    else
	      key_val = result;
	    GO_THROUGH_REDIR (tab);
	    entry = find_tab_el (tab, key, TRUE);
	    d_assert (entry != NULL);
	    if (ER_NODE_MODE (entry) != ER_NM_empty_el)
	      invinput_error (f, function_name, ln_flag);
	    *(val_t *) entry = key_val;
	    make_immutable (entry);
	    *((val_t *) entry + 1) = result;
	    if (token.token_code == ',')
	      token = get_token (f, function_name, ln_flag);
	  }
      }
    default:
      invinput_error (f, function_name, ln_flag);
    }
  d_unreachable ();
}

static void
general_scan_call (FILE *f, int file_flag, int ln_flag)
{
  const char *function_name;
  struct token token;
  val_t val;
  int curr_char;

  function_name = (file_flag
		   ? (ln_flag ? FSCANLN_NAME : FSCAN_NAME)
		   : (ln_flag ? SCANLN_NAME : SCAN_NAME));
  errno = 0;
  token = get_token (f, function_name, ln_flag);
  if (token.token_code == EOF)
    eval_error (eof_bc_decl, call_pos (), DERR_eof_occured, function_name);
  val = scanel (f, token, function_name, ln_flag);
  /* Skip input to the of line. */
  if (ln_flag)
    do
      {
	curr_char = get_ucode_from_utf8_stream (read_byte, f);
      }
    while (curr_char != EOF && curr_char != '\n');
  if (errno != 0)
    process_system_errors (function_name);
  /* Place the result. */
  *(val_t *) fun_result = val;
}

void
scan_call (int pars_number)
{
  if (pars_number != 0)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, SCAN_NAME);
  general_scan_call (stdin, FALSE, FALSE);
}

void
scanln_call (int pars_number)
{
  if (pars_number != 0)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, SCANLN_NAME);
  general_scan_call (stdin, FALSE, TRUE);
}

void
fscan_call (int pars_number)
{
  general_scan_call (fget_function_call_start (pars_number, FSCAN_NAME), TRUE,
		     FALSE);
}

void
fscanln_call (int pars_number)
{
  general_scan_call (fget_function_call_start (pars_number, FSCANLN_NAME),
		     TRUE, TRUE);
}

static void
int_function_end (rint_t result, int pars_number)
{
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, result);
}

static void
function_without_par (int pars_number, const char *function_name)
{
  if (pars_number != 0)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, function_name);
}

void
getpid_call (int pars_number)
{
  function_without_par (pars_number, GETPID_NAME);
  int_function_end (getpid (), pars_number);
}

static void
str_function_end (char *result, int pars_number)
{
  ER_node_t vect;

  vect = create_string (result);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

static void
general_putf_call (FILE *f, int pars_number, enum file_param_type param_type)
{
  const char *function_name;
  ER_node_t val;
  int start, byte_p;

  start = 0;
  if (param_type == NO_FILE)
    {
      function_name = SPUTF_NAME;
      d_assert (f == NULL);
    }
  else if (param_type == STANDARD_FILE)
    function_name = PUTF_NAME;
  else
    {
      function_name = FPUTF_NAME;
      start = 1;
    }
  if (pars_number - start <= 0)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, function_name);
  val = IVAL (ctop, -pars_number + 1 + start);
  to_vect_string_conversion (val, NULL, NULL);
  if (ER_NODE_MODE (val) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (val)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (val)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (val)) != ER_NM_byte))
    eval_error (partype_bc_decl, call_pos (),
		DERR_parameter_type, function_name);
  byte_p = form_format_string (ER_vect (val),
			       IVAL (ctop, -pars_number + 2 + start),
			       pars_number - 1 - start, function_name, FALSE);
  finish_output (f, byte_p);
}

void
putf_call (int pars_number)
{
  general_putf_call (stdout, pars_number, STANDARD_FILE);
}

void
fputf_call (int pars_number)
{
  general_putf_call (file_function_call_start (pars_number, FPUTF_NAME),
		    pars_number, GIVEN_FILE);
}

void
sputf_call (int pars_number)
{
  general_putf_call (NULL, pars_number, NO_FILE);
}

void
getun_call (int pars_number)
{
  function_without_par (pars_number, GETUN_NAME);
  str_function_end (getun (), pars_number);
}

void
geteun_call (int pars_number)
{
  function_without_par (pars_number, GETEUN_NAME);
  str_function_end (geteun (), pars_number);
}

void
getgn_call (int pars_number)
{
  function_without_par (pars_number, GETGN_NAME);
  str_function_end (getgn (), pars_number);
}

void
getegn_call (int pars_number)
{
  function_without_par (pars_number, GETEGN_NAME);
  str_function_end (getegn (), pars_number);
}

void
getgroups_call (int pars_number)
{
  ER_node_t vect;
  size_t els_number, grs_n;
  size_t i;

  if (pars_number != 0)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, GETGROUPS_NAME);
#if defined(HAVE_GETGROUPS)
  els_number = getgroups (0, NULL);
  VLO_NULLIFY (temp_vlobj);
  VLO_EXPAND (temp_vlobj, sizeof (GETGROUPS_T) * els_number);
  if (getgroups (els_number, (GETGROUPS_T *) VLO_BEGIN (temp_vlobj)) < 0
      && errno)
    process_system_errors (GETGROUPS_NAME);
  for (grs_n = i = 0; i < els_number; i++)
    if (getgrgid (((GETGROUPS_T *) VLO_BEGIN (temp_vlobj)) [i]) != NULL)
      grs_n++;
  if (grs_n == 0)
    vect = create_empty_vector ();
  else
    {
      vect = create_pack_vector (grs_n, ER_NM_vect);
      ER_set_els_number (vect, 0);
    }
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
  if (grs_n != 0)
      for (grs_n = i = 0; i < els_number; i++)
	{
	  struct group *gr;
	  
	  gr = getgrgid (((GETGROUPS_T *) VLO_BEGIN (temp_vlobj)) [i]);
	  if (gr != NULL)
	    {
	      vect = create_string (gr->gr_name);
	      set_packed_vect_el (ER_vect (fun_result), grs_n, vect);
	      grs_n++;
	      ER_set_els_number (ER_vect (fun_result), grs_n);
	    }
	}
#else
  vect = create_empty_vector ();
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
#endif
}

static void do_inline
float_function_start (int pars_number, const char *function_name)
{
  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, function_name);
  implicit_float_conversion (ctop, NULL);
  if (ER_NODE_MODE (ctop) != ER_NM_float)
    eval_error (partype_bc_decl,
		call_pos (), DERR_parameter_type, function_name);
  errno = 0;
}

static void do_inline
float_function_start2 (int pars_number, const char *function_name)
{
  if (pars_number != 2)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, function_name);
  implicit_float_conversion (ctop, NULL);
  implicit_float_conversion (below_ctop, NULL);
  if (ER_NODE_MODE (ctop) != ER_NM_float
      || ER_NODE_MODE (below_ctop) != ER_NM_float)
    eval_error (partype_bc_decl,
		call_pos (), DERR_parameter_type, function_name);
  errno = 0;
}

#ifdef RFLOAT_NAN
static rfloat_t rfloat_nan;
static rfloat_t minus_rfloat_nan;
#endif

static void
float_function_finish (int pars_number, rfloat_t result,
		       const char *function_name)
{
  if (!errno)
    {
#ifdef IS_RFLOAT_NAN
      /* Remember NaN == NaN equals FALSE */
      if (IS_RFLOAT_NAN (result))
	errno = EDOM;
#endif
#ifdef RFLOAT_HUGE_VAL
      if (result == RFLOAT_HUGE_VAL || result == -RFLOAT_HUGE_VAL)
	errno = ERANGE;
#endif
    }
  if (errno)
    process_system_errors (function_name);
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_float);
  ER_set_f (fun_result, result);
}

void
sqrt_call (int pars_number)
{
  float_function_start (pars_number, SQRT_NAME);
  float_function_finish (pars_number, sqrt (ER_f (ctop)), SQRT_NAME);
}

void
exp_call (int pars_number)
{
  float_function_start (pars_number, EXP_NAME);
  float_function_finish (pars_number, exp (ER_f (ctop)), EXP_NAME);
}

void
log_call (int pars_number)
{
  float_function_start (pars_number, LOG_NAME);
  float_function_finish (pars_number, log (ER_f (ctop)), LOG_NAME);
}

void
log10_call (int pars_number)
{
  float_function_start (pars_number, LOG10_NAME);
  float_function_finish (pars_number, log10 (ER_f (ctop)), LOG10_NAME);
}

void
pow_call (int pars_number)
{
  float_function_start2 (pars_number, POW_NAME);
  float_function_finish (pars_number, pow (ER_f (below_ctop), ER_f (ctop)),
			 POW_NAME);
}

void
sin_call (int pars_number)
{
  float_function_start (pars_number, SIN_NAME);
  float_function_finish (pars_number, sin (ER_f (ctop)), SIN_NAME);
}

void
cos_call (int pars_number)
{
  float_function_start (pars_number, COS_NAME);
  float_function_finish (pars_number, cos (ER_f (ctop)), COS_NAME);
}

void
atan2_call (int pars_number)
{
  float_function_start2 (pars_number, ATAN2_NAME);
  float_function_finish (pars_number, atan2 (ER_f (below_ctop), ER_f (ctop)),
			 ATAN2_NAME);
}

static void
general_rand_call (int pars_number, int rand_flag)
{
  rint_t seed;

  if ((rand_flag && pars_number != 0) || (! rand_flag && pars_number > 1))
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number,
		(rand_flag ? RAND_NAME : SRAND_NAME));
  if (!rand_flag &&  pars_number == 1)
    {
      implicit_int_conversion (ctop, NULL);
      if (ER_NODE_MODE (ctop) == ER_NM_int)
	seed = ER_i (ctop);
      else
	eval_error (partype_bc_decl, call_pos (),
		    DERR_parameter_type, SRAND_NAME);
    }
  if (rand_flag)
    {
      /* Place the result instead of the function. */
      ER_SET_MODE (fun_result, ER_NM_float);
      ER_set_f (fun_result, (rand () + 0.0) / RAND_MAX);
    }
  else
    {
      ER_SET_MODE (fun_result, ER_NM_undef);
      if (pars_number == 1)
	srand ((unsigned) seed);
      else
	srand ((unsigned) time (NULL));
    }
}

void
rand_call (int pars_number)
{
  general_rand_call (pars_number, TRUE);
}

void
srand_call (int pars_number)
{
  general_rand_call (pars_number, FALSE);
}

void
process_system_errors (const char *function_name)
{
  switch (errno)
    {
#ifdef EACCES
    case EACCES:
      /* Permission denied. */
      eval_error (eaccess_bc_decl, call_pos (), DERR_eaccess, function_name);
      break;
#endif
#ifdef EAGAIN
    case EAGAIN:
      eval_error (eagain_bc_decl, call_pos (), DERR_eagain, function_name);
      break;
#endif
#ifdef EBADF
    case EBADF:
      eval_error (ebadf_bc_decl, call_pos (), DERR_ebadf, function_name);
      break;
#endif
#ifdef EBUSY
    case EBUSY:
      eval_error (ebusy_bc_decl, call_pos (), DERR_ebusy, function_name);
      break;
#endif
#ifdef ECHILD
    case ECHILD:
      eval_error (echild_bc_decl, call_pos (), DERR_echild, function_name);
      break;
#endif
#ifdef EDEADLK
    case EDEADLK:
      eval_error (edeadlk_bc_decl, call_pos (), DERR_edeadlk, function_name);
      break;
#endif
#ifdef EDOM
    case EDOM:
      eval_error (edom_bc_decl, call_pos (), DERR_edom, function_name);
      break;
#endif
#ifdef EEXIST
    case EEXIST:
      eval_error (eexist_bc_decl, call_pos (), DERR_eexist, function_name);
      break;
#endif
#ifdef EFAULT
    case EFAULT:
      eval_error (efault_bc_decl, call_pos (), DERR_efault, function_name);
      break;
#endif
#ifdef EFBIG
    case EFBIG:
      eval_error (efbig_bc_decl, call_pos (), DERR_efbig, function_name);
      break;
#endif
#ifdef EINTR
    case EINTR:
      eval_error (eintr_bc_decl, call_pos (), DERR_eintr, function_name);
      break;
#endif
#ifdef EINVAL
    case EINVAL:
      eval_error (einval_bc_decl, call_pos (), DERR_einval, function_name);
      break;
#endif
#ifdef EIO
    case EIO:
      eval_error (eio_bc_decl, call_pos (), DERR_eio, function_name);
      break;
#endif
#ifdef EISDIR
    case EISDIR:
      eval_error (eisdir_bc_decl, call_pos (), DERR_eisdir, function_name);
      break;
#endif
#ifdef EMFILE
    case EMFILE:
      eval_error (emfile_bc_decl, call_pos (), DERR_emfile, function_name);
      break;
#endif
#ifdef EMLINK
    case EMLINK:
      eval_error (emlink_bc_decl, call_pos (), DERR_emlink, function_name);
      break;
#endif
#ifdef ENAMETOOLONG
    case ENAMETOOLONG:
      eval_error (enametoolong_bc_decl, call_pos (),
		  DERR_enametoolong, function_name);
      break;
#endif
#ifdef ENFILE
    case ENFILE:
      eval_error (enfile_bc_decl, call_pos (), DERR_enfile, function_name);
      break;
#endif
#ifdef ENODEV
    case ENODEV:
      eval_error (enodev_bc_decl, call_pos (), DERR_enodev, function_name);
      break;
#endif
#ifdef ENOENT
    case ENOENT:
      /* File or directory does not exist, or directory name is an empty
	 string. */
      eval_error (enoent_bc_decl, call_pos (), DERR_enoent, function_name);
      break;
#endif
#ifdef ENOEXEC
    case ENOEXEC:
      eval_error (enoexec_bc_decl, call_pos (), DERR_enoexec, function_name);
      break;
#endif
#ifdef ENOLCK
    case ENOLCK:
      eval_error (enolck_bc_decl, call_pos (), DERR_enolck, function_name);
      break;
#endif
#ifdef ENOMEM
    case ENOMEM:
      eval_error (enomem_bc_decl, call_pos (), DERR_enomem, function_name);
      break;
#endif
#ifdef ENOSPC
    case ENOSPC:
      eval_error (enospc_bc_decl, call_pos (), DERR_enospc, function_name);
      break;
#endif
#ifdef ENOSYS
    case ENOSYS:
      eval_error (enosys_bc_decl, call_pos (), DERR_enosys, function_name);
      break;
#endif
#ifdef ENOTDIR
    case ENOTDIR:
      /* This is not a directory. */
      eval_error (enotdir_bc_decl, call_pos (), DERR_enotdir, function_name);
      break;
#endif
#ifdef ENOTEMPTY
#if defined(EEXIST) && EEXIST!=ENOTEMPTY
    case ENOTEMPTY:
      eval_error (enotempty_bc_decl, call_pos (),
		  DERR_enotempty, function_name);
      break;
#endif
#endif
#ifdef ENOTTY
    case ENOTTY:
      eval_error (enotty_bc_decl, call_pos (), DERR_enotty, function_name);
      break;
#endif
#ifdef ENXIO
    case ENXIO:
      eval_error (enxio_bc_decl, call_pos (), DERR_enxio, function_name);
      break;
#endif
#ifdef EPERM
    case EPERM:
      eval_error (eperm_bc_decl, call_pos (), DERR_eperm, function_name);
      break;
#endif
#ifdef EPIPE
    case EPIPE:
      eval_error (epipe_bc_decl, call_pos (), DERR_epipe, function_name);
      break;
#endif
#ifdef ERANGE
    case ERANGE:
      eval_error (erange_bc_decl, call_pos (), DERR_erange, function_name);
      break;
#endif
#ifdef EROFS
    case EROFS:
      eval_error (erofs_bc_decl, call_pos (), DERR_erofs, function_name);
      break;
#endif
#ifdef ESPIPE
    case ESPIPE:
      eval_error (espipe_bc_decl, call_pos (), DERR_espipe, function_name);
      break;
#endif
#ifdef ESRCH
    case ESRCH:
      eval_error (esrch_bc_decl, call_pos (), DERR_esrch, function_name);
      break;
#endif
#ifdef EXDEV
    case EXDEV:
      eval_error (exdev_bc_decl, call_pos (), DERR_exdev, function_name);
      break;
#endif
    default:
      /* We don't care does strerror exist or not because it is for
         errors.c. */
      d_assert (errno > 0);
      eval_error (syserror_bc_decl, call_pos (),
		  strerror (errno), function_name);
      break;
    }
}

/* The function is not supposed to be used by Dino user.  It should be
   used by developer of Dino external libraries. */
void
process_errno_call (int pars_number)
{
  const char *name;

  if (pars_number > 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, PROCESS_ERRNO_NAME);
  if (pars_number == 0)
    name = "";
  else
    {
      to_vect_string_conversion (ctop, NULL, NULL);
      name = ER_pack_els (ER_vect (ctop)); // ???
    }
  if (errno)
    process_system_errors (name);
  ER_SET_MODE (fun_result, ER_NM_undef);
}

void
readdir_call (int pars_number)
{
  ER_node_t result;
  ER_node_t vect;
  DIR *dir;
  struct dirent *dirent;
  size_t i;
  size_t dir_files_number;

  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, READDIR_NAME);
  to_vect_string_conversion (ctop, NULL, NULL);
  if (ER_NODE_MODE (ctop) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (ctop)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (ctop)) != ER_NM_byte))
    eval_error (partype_bc_decl, call_pos (),
		DERR_parameter_type, READDIR_NAME);
  dir = opendir (strvect_to_utf8 (ER_vect (ctop), TRUE));
  if (dir == NULL)
    process_system_errors (READDIR_NAME);
  else
    {
      errno = 0;
      for (i = 0; readdir (dir) != NULL; i++)
	;
      if (errno != 0)
	/* Internall error: EBADF, EFAULT, EINVAL, ENOENT, ENOTDIR and
           may be something else. */
	eval_error (internal_bc_decl, call_pos (),
		    DERR_internal_error, READDIR_NAME);
      if (closedir (dir) != 0)
	/* Internall error: EBADF and may be something else. */
	eval_error (internal_bc_decl, call_pos (),
		    DERR_internal_error, READDIR_NAME);
      dir = opendir (strvect_to_utf8 (ER_vect (ctop), TRUE));
      if (dir == NULL)
	process_system_errors (READDIR_NAME);
      else
	{
	  dir_files_number = i;
	  result = create_pack_vector (dir_files_number, ER_NM_vect);
	  ER_set_els_number (result, 0);
	  /* We read maximum which may be in the vector.  Remember
             that the directory may be changed during two opendir
             calls. */
	  for (i = 0; i < dir_files_number; i++)
	    {
	      errno = 0;
	      dirent = readdir (dir);
	      if (errno != 0)
		/* Internall error: EBADF, EFAULT, EINVAL, ENOENT,
		   ENOTDIR and may be something else. */
		eval_error (internal_bc_decl, call_pos (),
			    DERR_internal_error, READDIR_NAME);
	      if (dirent == NULL)
		break;
	      vect = create_string (dirent->d_name); // utf8 ???
	      set_packed_vect_el (result, i, vect);
	      ER_set_els_number (result, i + 1);
	    }
	  if (closedir (dir) != 0)
	    /* Internall error: EBADF and may be something else. */
	    eval_error (internal_bc_decl, call_pos (),
			DERR_internal_error, READDIR_NAME);
	}
    }
  d_assert (result != NULL);
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, result, 0);
}

static void
stat_start (int pars_number, const char *function_name, struct stat *buf)
{
  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, function_name);
  to_vect_string_conversion (ctop, NULL, NULL);
  get_stat (ctop, function_name, buf);
}

void
ftype_call (int pars_number)
{
  struct stat buf;
  int result;

  stat_start (pars_number, FTYPE_NAME, &buf);
  if (S_ISREG (buf.st_mode))
    result = 'f';
  else if (S_ISDIR (buf.st_mode))
    result = 'd';
#ifdef S_ISLNK
  else if (S_ISLNK (buf.st_mode))
    result = 'L';
#endif
  else if (S_ISCHR (buf.st_mode))
    result = 'c';
#ifdef S_ISBLK
  else if (S_ISBLK (buf.st_mode))
    result = 'b';
#endif
#ifdef S_ISFIFO
  else if (S_ISFIFO (buf.st_mode))
    result = 'p';
#endif
#ifdef S_ISSOCK
  else if (S_ISSOCK (buf.st_mode))
    result = 'S';
#endif
  else
    result = (-1);
  if (result < 0)
    ER_SET_MODE (fun_result, ER_NM_nil);
  else
    {
      ER_SET_MODE (fun_result, ER_NM_char);
      ER_set_ch (fun_result, result);
    }
}

static void
stat_finish (int pars_number, rint_t result)
{
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, result);
}

void
fuidn_call (int pars_number)
{
  struct stat buf;
  ER_node_t result;
  char *str;
  struct passwd *p;
  
  stat_start (pars_number, FUIDN_NAME, &buf);
  p = getpwuid (buf.st_uid);
  if (p == NULL)
    str = "Unknown";
  else
    str = p->pw_name;
  result = create_string (str);
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, result, 0);
}

void
fgrpn_call (int pars_number)
{
  struct stat buf;
  ER_node_t result;
  
  stat_start (pars_number, FGRPN_NAME, &buf);
  {
    char *str;
    struct group *p;
    
    p = getgrgid (buf.st_gid);
    if (p == NULL)
      str = "Unknown";
    else
      str = p->gr_name;
    result = create_string (str);
  }
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, result, 0);
}

void
fsize_call (int pars_number)
{
  struct stat buf;
  rint_t result;

  stat_start (pars_number, FSIZE_NAME, &buf);
  result = buf.st_size;
  stat_finish (pars_number, result);
}

void
fatime_call (int pars_number)
{
  struct stat buf;
  rint_t result;

  stat_start (pars_number, FATIME_NAME, &buf);
  result = buf.st_atime;
  stat_finish (pars_number, result);
}

void
fmtime_call (int pars_number)
{
  struct stat buf;
  rint_t result;

  stat_start (pars_number, FMTIME_NAME, &buf);
  result = buf.st_mtime;
  stat_finish (pars_number, result);
}

void
fctime_call (int pars_number)
{
  struct stat buf;
  rint_t result;

  stat_start (pars_number, FCTIME_NAME, &buf);
  result = buf.st_ctime;
  stat_finish (pars_number, result);
}

static void
mode_finish (int pars_number, const char *result)
{
  ER_node_t vect;

  vect = create_string (result);
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

void
fumode_call (int pars_number)
{
  struct stat buf;
  char result [5];
  char *str = result;

  stat_start (pars_number, FUMODE_NAME, &buf);
  if (buf.st_mode & S_ISVTX)
    *str++ = 's';
  if (buf.st_mode & S_IRUSR)
    *str++ = 'r';
  if (buf.st_mode & S_IWUSR)
    *str++ = 'w';
  if (buf.st_mode & S_IXUSR)
    *str++ = 'x';
  *str = '\0';
  mode_finish (pars_number, result);
}

void
fgmode_call (int pars_number)
{
  struct stat buf;
  char result [5];
  char *str = result;

  stat_start (pars_number, FGMODE_NAME, &buf);
  if (buf.st_mode & S_IRGRP)
    *str++ = 'r';
  if (buf.st_mode & S_IWGRP)
    *str++ = 'w';
  if (buf.st_mode & S_IXGRP)
    *str++ = 'x';
  *str = '\0';
  mode_finish (pars_number, result);
}

void
fomode_call (int pars_number)
{
  struct stat buf;
  char result [5];
  char *str = result;

  stat_start (pars_number, FOMODE_NAME, &buf);
  if (buf.st_mode & S_IROTH)
    *str++ = 'r';
  if (buf.st_mode & S_IWOTH)
    *str++ = 'w';
  if (buf.st_mode & S_IXOTH)
    *str++ = 'x';
  *str = '\0';
  mode_finish (pars_number, result);
}

void
time_call (int pars_number)
{
  time_t t;

  if (pars_number != 0)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, TIME_NAME);
  t = time (NULL);
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, t);
}

void
strtime_call (int pars_number)
{
  time_t t;
  struct tm *tm;
  const char *format;
  const char *str;
  ER_node_t vect;
  ER_node_t format_var;
  int percents_number;
  size_t max;

  if (pars_number > 2)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, STRTIME_NAME);
  if (pars_number == 2)
    {
      implicit_int_conversion (ctop, NULL);
      if (ER_NODE_MODE (ctop) != ER_NM_int)
	eval_error (partype_bc_decl, call_pos (),
		    DERR_parameter_type, STRTIME_NAME);
      t = ER_i (ctop);
    }
  else
    t = time (NULL);
  if (pars_number >= 1)
    {
      format_var = (pars_number == 1 ? ctop : below_ctop);
      to_vect_string_conversion (format_var, NULL, NULL);
      if (ER_NODE_MODE (format_var) == ER_NM_vect
	  && ER_NODE_MODE (ER_vect (format_var)) == ER_NM_heap_pack_vect
	  && ER_pack_vect_el_mode (ER_vect (format_var)) == ER_NM_byte)
	format = ER_pack_els (ER_vect (format_var));
      else
	eval_error (partype_bc_decl,
		    call_pos (), DERR_parameter_type, STRTIME_NAME);
    }
  else
    {
      format_var = IVAL (ER_stack_vars (uppest_stack),
			 BC_var_num (time_format_bc_decl));
      to_vect_string_conversion (format_var, NULL, NULL);
      if (ER_NODE_MODE (format_var) == ER_NM_vect
	  && ER_NODE_MODE (ER_vect (format_var)) == ER_NM_heap_pack_vect
	  && ER_pack_vect_el_mode (ER_vect (format_var)) == ER_NM_byte)
	format = ER_pack_els (ER_vect (format_var));
      else
	eval_error (invenvar_bc_decl, call_pos (),
		    DERR_corrupted_environment_var, TIME_FORMAT_NAME);
    }
  tm = localtime (&t);
  for (percents_number = 0, str = format; *str != 0; str++)
    if (*str == '%')
      percents_number++;
  max = strlen (format) + 2 + percents_number * 10;
  vect = create_empty_string (max);
  strftime (ER_pack_els (vect), max, format, tm);
  ER_set_els_number (vect, strlen (ER_pack_els (vect)));
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

/* The following macro is necessary for non standard include files of
   SUNOS 4..., linux */

#ifndef CLOCKS_PER_SECOND
#ifdef CLOCKS_PER_SEC
#define CLOCKS_PER_SECOND CLOCKS_PER_SEC
#elif __linux__
#define CLOCKS_PER_SECOND 100
#elif sun
#define CLOCKS_PER_SECOND 1000000
#elif CLK_TCK
#define CLOCKS_PER_SECOND CLK_TCK
#else
#error define macro CLOCKS_PER_SECOND
#endif
#endif /* CLOCKS_PER_SECOND */

void
clock_call (int pars_number)
{
  rfloat_t secs;

  if (pars_number != 0)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, CLOCK_NAME);
  secs = (rfloat_t) (clock () - start_time) / CLOCKS_PER_SECOND;
  ER_SET_MODE (fun_result, ER_NM_float);
  ER_set_f (fun_result, secs);
}

void
gc_call (int pars_number)
{
  ptrdiff_t offset;

  if (pars_number != 0)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, GC_NAME);
  GC_executed_stmts_count = executed_stmts_count;
  offset = (char *) fun_result - (char *) cstack;
  GC ();
  fun_result = (ER_node_t) ((char *) cstack + offset);
  /* Place the free memory. */
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, free_heap_memory);
}

void
system_call (int pars_number)
{
  int code;
  int error_flag;
  ER_node_t val;
  ER_node_t vect;

  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, SYSTEM_NAME);
  else
    {
      val = IVAL (ctop, -pars_number + 1);
      if (ER_NODE_MODE (val) != ER_NM_vect)
	error_flag = TRUE;
      else
	{
	  to_vect_string_conversion (val, NULL, NULL);
	  vect = ER_vect (val);
	  if (ER_NODE_MODE (vect) != ER_NM_heap_pack_vect
	      || (ER_pack_vect_el_mode (vect) != ER_NM_char
		  && ER_pack_vect_el_mode (vect) != ER_NM_byte))
	    error_flag = TRUE;
	  else
	    {
	      code = system (strvect_to_utf8 (vect, TRUE));
	      if (code == 127)
		eval_error (noshell_bc_decl, call_pos (),
			    DERR_no_shell, SYSTEM_NAME);
	      else if (code < 0)
		eval_error (systemfail_bc_decl, call_pos (),
			    DERR_other_fail_in_system_call, SYSTEM_NAME);
	      error_flag = FALSE;
	    }
	}
      if (error_flag)
	eval_error (partype_bc_decl, call_pos (),
		    DERR_parameter_type, SYSTEM_NAME);
    }
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, code);
}

/* Array of pointers to functions/calls which will be reported in the
   trace of calls.  */
vlo_t trace_stack;

/* Output info about stack trace.  */
void
print_trace_stack (void)
{
  struct trace_stack_elem *elem_ptr;

  for (elem_ptr = VLO_BEGIN (trace_stack);
       (char *) elem_ptr <= (char *) VLO_END (trace_stack);
       elem_ptr++)
    fprintf (stderr, "%s:%u:%u:calling %s\n",
	     get_pos (elem_ptr->pc).file_name,
	     get_pos (elem_ptr->pc).line_number,
	     get_pos (elem_ptr->pc).column_number,
	     BC_ident (BC_fdecl (elem_ptr->block)));
}

void
exit_call (int pars_number)
{
  BC_node_t block;
  ER_node_t stack;
  struct trace_stack_elem elem;

  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, EXIT_NAME);
  implicit_int_conversion (ctop, NULL);
  if (ER_NODE_MODE (ctop) != ER_NM_int)
    eval_error (partype_bc_decl, call_pos (), DERR_parameter_type, EXIT_NAME);
  if (trace_flag)
    {
      VLO_NULLIFY (trace_stack);
      
      for (stack = cstack;
	   stack != uppest_stack;
	   stack = ER_prev_stack (stack))
	{
	  block = ER_block_node (stack);
	  if (BC_NODE_MODE (block) == BC_NM_block)
	    continue;
	  elem.block = block;
	  elem.pc = ER_call_pc (stack);
	  VLO_ADD_MEMORY (trace_stack, &elem, sizeof (elem));
	}
    }
  dino_finish (ER_i (ctop));
}

static void
fold_function (const void *el, val_t *fold_initval,
	       BC_node_t fold_el_fun_block,
	       ER_node_mode_t fold_vect_el_type, int fold_dim)
{
  ER_node_t context;
  
  context = GET_TEMP_REF (fold_dim);
  TOP_UP;
  *(val_t *) ctop = *fold_initval;
  TOP_UP;
  if (fold_vect_el_type == ER_NM_val)
    *(val_t *) ctop = *(val_t *) el;
  else if (fold_vect_el_type == ER_NM_byte)
    {
      ER_SET_MODE (ctop, ER_NM_char);
      ER_set_ch (ctop, *(byte_t *) el);
    }
  else
    {
      ER_SET_MODE (ctop, fold_vect_el_type);
      memcpy ((char *) ctop + val_displ_table [fold_vect_el_type],
	      (char *) el, type_size_table [fold_vect_el_type]);
      if (fold_vect_el_type == ER_NM_vect)
	ER_set_dim (ctop, 0);
    }
  call_fun_class (fold_el_fun_block, context, 2, curr_from_c_code_p);
  TOP_UP;
  *fold_initval = *(val_t *) ctop;
  TOP_DOWN;
}

static void
process_fold_vect_op (ER_node_t op, val_t *fold_initval,
		      BC_node_t fold_el_fun_block, int dim, int depth)
{
  size_t i, len;
  char *pack_els;
  ER_node_t v;
  ER_node_t unpack_els;
  ER_node_mode_t fold_vect_el_type;

  d_assert (dim > 0);
  GO_THROUGH_REDIR (op);
  len = ER_els_number (op);
  if (ER_NODE_MODE (op) == ER_NM_heap_pack_vect)
    fold_vect_el_type = ER_pack_vect_el_mode (op);
  else
    fold_vect_el_type = ER_NM_val;
  if (dim > 1 && ER_NODE_MODE (op) == ER_NM_heap_pack_vect
      && fold_vect_el_type != ER_NM_vect)
    eval_error (vecform_bc_decl, call_pos (), DERR_vector_form_type, depth);
  PUSH_TEMP_REF (op);
  for (i = 0; i < len; i++)
    {
      if (ER_NODE_MODE (op) == ER_NM_heap_pack_vect)
	{
	  pack_els = ER_pack_els (op);
	  if (dim > 1)
	    process_fold_vect_op (((ER_node_t *) pack_els) [i], fold_initval,
				  fold_el_fun_block, dim - 1, depth + 1);
	  else
	    {
	      DECR_CTOP (2); /* free place for fold function params.  */
	      fold_function (pack_els + i * type_size_table [fold_vect_el_type],
			     fold_initval, fold_el_fun_block,
			     fold_vect_el_type, dim + depth - 1);
	      DECR_CTOP (-2);
	    }
	}
      else
	{
	  unpack_els = ER_unpack_els (op);
	  v = IVAL (unpack_els, i);
	  if (dim == 1)
	    {
	      DECR_CTOP (2); /* free place for fold function params.  */
	      fold_function (v, fold_initval, fold_el_fun_block,
			     fold_vect_el_type, dim + depth - 1);
	      DECR_CTOP (-2);
	    }
	  else if (ER_NODE_MODE (v) != ER_NM_vect)
	    eval_error (vecform_bc_decl, call_pos (),
			DERR_vector_form_type, depth);
	  else
	    process_fold_vect_op (ER_vect (v), fold_initval, fold_el_fun_block,
				  dim - 1, depth + 1);
	}
      op = GET_TEMP_REF (0);
    }
  POP_TEMP_REF (1);
}

void
fold_call (int pars_number)
{
  ER_node_t context, vect;
  ER_node_t par1, par2;
  int fold_dim, fun_result_offset;
  BC_node_t fold_el_fun_block;
  val_t fold_initval;

  if (pars_number != 3 && pars_number != 4)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, FOLD_NAME);
  par1 = IVAL (ctop, -pars_number + 1);
  par2 = IVAL (ctop, -pars_number + 2);
  if (! function_p (par1) || ER_NODE_MODE (par2) != ER_NM_vect)
    eval_error (partype_bc_decl, call_pos (),
		DERR_parameter_type, FOLD_NAME);
  fun_result_offset = (val_t *) fun_result - (val_t *) cvars;
  vect = ER_vect (par2);
  fold_initval = *(val_t *) IVAL (ctop, -pars_number + 3);
  if (pars_number == 3)
    fold_dim = 1;
  else
    {
      ER_node_t dim_par = IVAL (ctop, -pars_number + 4);
      implicit_int_conversion (dim_par, NULL);
      if (!ER_IS_OF_TYPE (dim_par, ER_NM_int))
	eval_error (partype_bc_decl,
		    call_pos (), DERR_parameter_type, FOLD_NAME);
      fold_dim = ER_i (dim_par);
    }
  if (fold_dim <= 0)
    ;
  else
    {
      GO_THROUGH_REDIR (vect);
      try_full_pack (vect);
      context = ER_code_context (par1);
      fold_el_fun_block = ID_TO_CODE (ER_code_id (par1));
      PUSH_TEMP_REF (context);
      process_fold_vect_op (vect, &fold_initval, fold_el_fun_block, fold_dim, 1);
      POP_TEMP_REF (1);
    }
  /* Place the result. */
  fun_result = IVAL (cvars, fun_result_offset);
  *(val_t *) fun_result = fold_initval;
}

static int
filter_function (const void *el, BC_node_t filter_el_fun_block,
		 ER_node_mode_t filter_vect_el_type, int filter_dim)
{
  int res;
  ER_node_t context;
  
  context = GET_TEMP_REF (2 * filter_dim);
  TOP_UP;
  if (filter_vect_el_type == ER_NM_val)
    *(val_t *) ctop = *(val_t *) el;
  else if (filter_vect_el_type == ER_NM_byte)
    {
      ER_SET_MODE (ctop, ER_NM_char);
      ER_set_ch (ctop, *(byte_t *) el);
    }
  else
    {
      ER_SET_MODE (ctop, filter_vect_el_type);
      memcpy ((char *) ctop + val_displ_table [filter_vect_el_type],
	      (char *) el, type_size_table [filter_vect_el_type]);
      if (filter_vect_el_type == ER_NM_vect)
	ER_set_dim (ctop, 0);
    }
  call_fun_class (filter_el_fun_block, context, 1, curr_from_c_code_p);
  TOP_UP;
  implicit_int_conversion (ctop, NULL);
  if (ER_NODE_MODE (ctop) != ER_NM_int)
    eval_error (invresult_bc_decl,
		call_pos (), DERR_invalid_result, FILTER_NAME);
  res = ER_i (ctop) != 0;
  TOP_DOWN;
  return res;
}

static ER_node_t
process_filter_vect_op (ER_node_t op, BC_node_t filter_el_fun_block,
			int dim, int depth)
{
  size_t i, nel, len, el_size;
  int flag;
  ER_node_t el, result;
  ER_node_mode_t filter_vect_el_type;

  d_assert (dim > 0);
  GO_THROUGH_REDIR (op);
  len = ER_els_number (op);
  if (ER_NODE_MODE (op) == ER_NM_heap_pack_vect)
    filter_vect_el_type = ER_pack_vect_el_mode (op);
  else
    filter_vect_el_type = ER_NM_val;
  if (dim > 1 && ER_NODE_MODE (op) == ER_NM_heap_pack_vect
      && filter_vect_el_type != ER_NM_vect)
    eval_error (vecform_bc_decl, call_pos (), DERR_vector_form_type, depth);
  if (dim > 1)
    result = create_pack_vector (len, ER_NM_vect);
  else if (filter_vect_el_type != ER_NM_val)
    result = create_pack_vector (len, filter_vect_el_type);
  else
    result = create_unpack_vector (len);
  ER_set_els_number (result, 0);
  PUSH_TEMP_REF (result);
  PUSH_TEMP_REF (op);
  el_size = type_size_table [filter_vect_el_type];
  for (i = nel = 0; i < len; i++)
    {
      if (dim > 1)
	{
	  if (ER_NODE_MODE (op) == ER_NM_heap_pack_vect)
	    el = process_filter_vect_op (((ER_node_t *) ER_pack_els (op)) [i],
					 filter_el_fun_block, dim - 1, depth + 1);
	  else if (ER_NODE_MODE (IVAL (ER_unpack_els (op), i)) != ER_NM_vect)
	    eval_error (vecform_bc_decl, call_pos (),
			DERR_vector_form_type, depth);
	  else
	    el = process_filter_vect_op (ER_vect (IVAL (ER_unpack_els (op), i)),
					 filter_el_fun_block, dim - 1, depth + 1);
	  op = GET_TEMP_REF (0);
	  result = GET_TEMP_REF (1);
	  set_packed_vect_el (result, nel, el);
	}
      else if (ER_NODE_MODE (op) == ER_NM_heap_pack_vect)
	{
	  TOP_DOWN; /* free place for filter function param.  */
	  flag = filter_function (ER_pack_els (op) + i * el_size,
				  filter_el_fun_block,
				  filter_vect_el_type, dim + depth - 1);
	  TOP_UP;
	  if (!flag)
	    {
	      op = GET_TEMP_REF (0);
	      result = GET_TEMP_REF (1);
	      continue;
	    }
	  else
	    {
	      op = GET_TEMP_REF (0);
	      result = GET_TEMP_REF (1);
	      memcpy (ER_pack_els (result) + nel * el_size,
		      ER_pack_els (op) + i * el_size, el_size);
	    }
	}
      else
	{
	  TOP_DOWN; /* free place for filter function param.  */
	  flag = filter_function (IVAL (ER_unpack_els (op), i),
				  filter_el_fun_block,
				  filter_vect_el_type, dim + depth - 1);
	  TOP_UP;
	  op = GET_TEMP_REF (0);
	  result = GET_TEMP_REF (1);
	  if (!flag)
	    continue;
	  *(val_t *) IVAL (ER_unpack_els (result), nel)
	    = *(val_t *) IVAL (ER_unpack_els (op), i);
	}
      nel++;
      ER_set_els_number (result, nel);
    }
  POP_TEMP_REF (2);
  if (dim == 1)
    try_full_pack (result);
  return result;
}

void
filter_call (int pars_number)
{
  ER_node_t context, vect;
  ER_node_t par1, par2;
  int filter_dim, fun_result_offset;
  BC_node_t filter_el_fun_block;

  if (pars_number != 2 && pars_number != 3)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, FILTER_NAME);
  par1 = IVAL (ctop, -pars_number + 1);
  par2 = IVAL (ctop, -pars_number + 2);
  if (! function_p (par1) || ER_NODE_MODE (par2) != ER_NM_vect)
    eval_error (partype_bc_decl, call_pos (),
		DERR_parameter_type, FILTER_NAME);
  fun_result_offset = (val_t *) fun_result - (val_t *) cvars;
  vect = ER_vect (par2);
  if (pars_number == 2)
    filter_dim = 1;
  else
    {
      ER_node_t dim_par = IVAL (ctop, -pars_number + 3);
      implicit_int_conversion (dim_par, NULL);
      if (!ER_IS_OF_TYPE (dim_par, ER_NM_int))
	eval_error (partype_bc_decl,
		    call_pos (), DERR_parameter_type, FILTER_NAME);
      filter_dim = ER_i (dim_par);
    }
  if (filter_dim <= 0)
    ;
  else
    {
      GO_THROUGH_REDIR (vect);
      try_full_pack (vect);
      context = ER_code_context (par1);
      filter_el_fun_block = ID_TO_CODE (ER_code_id (par1));
      PUSH_TEMP_REF (context);
      vect = process_filter_vect_op (vect, filter_el_fun_block, filter_dim, 1);
      POP_TEMP_REF (1);
    }
  /* Place the result. */
  fun_result = IVAL (cvars, fun_result_offset);
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

static void
map_function (const void *el, BC_node_t map_el_fun_block,
	      ER_node_mode_t map_vect_el_type, int map_dim)
{
  ER_node_t context;
  
  context = GET_TEMP_REF (2 * map_dim);
  TOP_UP;
  if (map_vect_el_type == ER_NM_val)
    *(val_t *) ctop = *(val_t *) el;
  else if (map_vect_el_type == ER_NM_byte)
    {
      ER_SET_MODE (ctop, ER_NM_char);
      ER_set_ch (ctop, *(byte_t *) el);
    }
  else
    {
      ER_SET_MODE (ctop, map_vect_el_type);
      memcpy ((char *) ctop + val_displ_table [map_vect_el_type],
	      (char *) el, type_size_table [map_vect_el_type]);
      if (map_vect_el_type == ER_NM_vect)
	ER_set_dim (ctop, 0);
    }
  call_fun_class (map_el_fun_block, context, 1, curr_from_c_code_p);
  TOP_UP;
}

static ER_node_t
process_map_vect_op (ER_node_t op, BC_node_t map_el_fun_block,
		     int dim, int depth)
{
  size_t i, len, el_size;
  ER_node_t el, result;
  ER_node_mode_t map_vect_el_type;

  d_assert (dim > 0);
  GO_THROUGH_REDIR (op);
  len = ER_els_number (op);
  if (ER_NODE_MODE (op) == ER_NM_heap_pack_vect)
    map_vect_el_type = ER_pack_vect_el_mode (op);
  else
    map_vect_el_type = ER_NM_val;
  if (dim > 1 && ER_NODE_MODE (op) == ER_NM_heap_pack_vect
      && map_vect_el_type != ER_NM_vect)
    eval_error (vecform_bc_decl, call_pos (), DERR_vector_form_type, depth);
  if (dim == 1)
    result = create_unpack_vector (len);
  else
    result = create_pack_vector (len, ER_NM_vect);
  ER_set_els_number (result, 0);
  PUSH_TEMP_REF (result);
  PUSH_TEMP_REF (op);
  el_size = type_size_table [map_vect_el_type];
  for (i = 0; i < len; i++)
    {
      if (dim == 1)
	{
	  TOP_DOWN; /* free place for filter function param.  */
	  if (ER_NODE_MODE (op) == ER_NM_heap_pack_vect)
	    map_function (ER_pack_els (op) + i * el_size, map_el_fun_block,
			  map_vect_el_type, dim + depth - 1);
	  else
	    map_function (IVAL (ER_unpack_els (op), i), map_el_fun_block,
			  map_vect_el_type, dim + depth - 1);
	  op = GET_TEMP_REF (0);
	  result = GET_TEMP_REF (1);
	  *(val_t *) IVAL (ER_unpack_els (result), i) = *(val_t *) ctop;
	}
      else
	{
	  if (ER_NODE_MODE (op) == ER_NM_heap_pack_vect)
	    el = process_map_vect_op (((ER_node_t *) ER_pack_els (op)) [i],
				      map_el_fun_block, dim - 1, depth + 1);
	  else if (ER_NODE_MODE (IVAL (ER_unpack_els (op), i)) != ER_NM_vect)
	    eval_error (vecform_bc_decl, call_pos (),
			DERR_vector_form_type, depth);
	  else
	    el = process_map_vect_op (ER_vect (IVAL (ER_unpack_els (op), i)),
				      map_el_fun_block, dim - 1, depth + 1);
	  op = GET_TEMP_REF (0);
	  result = GET_TEMP_REF (1);
	  set_packed_vect_el (result, i, el);
	}
      ER_set_els_number (result, i + 1);
    }
  if (dim == 1)
    try_full_pack (result);
  POP_TEMP_REF (2);
  return result;
}

void
map_call (int pars_number)
{
  ER_node_t context, vect;
  ER_node_t par1, par2;
  int map_dim, fun_result_offset;
  BC_node_t map_el_fun_block;

  if (pars_number != 2 && pars_number != 3)
    eval_error (parnumber_bc_decl,
		call_pos (), DERR_parameters_number, MAP_NAME);
  par1 = IVAL (ctop, -pars_number + 1);
  par2 = IVAL (ctop, -pars_number + 2);
  if (! function_p (par1) || ER_NODE_MODE (par2) != ER_NM_vect)
    eval_error (partype_bc_decl, call_pos (), DERR_parameter_type, MAP_NAME);
  fun_result_offset = (val_t *) fun_result - (val_t *) cvars;
  vect = ER_vect (par2);
  if (pars_number == 2)
    map_dim = 1;
  else
    {
      ER_node_t dim_par = IVAL (ctop, -pars_number + 3);
      implicit_int_conversion (dim_par, NULL);
      if (!ER_IS_OF_TYPE (dim_par, ER_NM_int))
	eval_error (partype_bc_decl,
		    call_pos (), DERR_parameter_type, MAP_NAME);
      map_dim = ER_i (dim_par);
    }
  if (map_dim <= 0)
    ;
  else
    {
      GO_THROUGH_REDIR (vect);
      try_full_pack (vect);
      context = ER_code_context (par1);
      map_el_fun_block = ID_TO_CODE (ER_code_id (par1));
      PUSH_TEMP_REF (context);
      vect = process_map_vect_op (vect, map_el_fun_block, map_dim, 1);
      POP_TEMP_REF (1);
    }
  /* Place the result. */
  fun_result = IVAL (cvars, fun_result_offset);
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, vect, 0);
}

/* Implement matrix transposition.  */
void
transpose_call (int pars_number)
{
  ER_node_t vect, row, col, res, res_el;
  size_t i, j, nrows, ncols, el_size, displ;
  const char *pack_els;
  char *res_pack_els;
  ER_node_mode_t res_eltp = ER_NM__error, eltp;

  if (pars_number != 1)
    eval_error (parnumber_bc_decl, call_pos (),
		DERR_parameters_number, TRANSPOSE_NAME);
  if (ER_NODE_MODE (ctop) != ER_NM_vect)
    eval_error (partype_bc_decl, call_pos (),
		DERR_parameter_type, TRANSPOSE_NAME);
  vect = ER_vect (ctop);
  GO_THROUGH_REDIR (vect);
  try_full_pack (vect);
  nrows = ER_els_number (vect);
  if (nrows == 0 || ER_NODE_MODE (vect) == ER_NM_heap_unpack_vect
      || ER_pack_vect_el_mode (vect) != ER_NM_vect)
    eval_error (matrixform_bc_decl, call_pos (),
		DERR_matrix_form_type, TRANSPOSE_NAME);
  pack_els = ER_pack_els (vect);
  for (i = 0; i < nrows; i++)
    {
      row = ((ER_node_t *) pack_els) [i];
      GO_THROUGH_REDIR (row);
      eltp = (ER_NODE_MODE (row) == ER_NM_heap_pack_vect
	      ? ER_pack_vect_el_mode (row) : ER_NM__error); // ???
      if (i == 0)
	{
	  ncols = ER_els_number (row);
	  res_eltp = eltp;
	}
      else if (ncols != ER_els_number (row))
	eval_error (matrixform_bc_decl, call_pos (),
		    DERR_matrix_form_type, TRANSPOSE_NAME);
      else if (eltp != res_eltp)
	res_eltp = ER_NM__error;
    }
  if (ncols == 0)
    eval_error (matrixform_bc_decl, call_pos (),
		DERR_matrix_form_type, TRANSPOSE_NAME);
  res = create_pack_vector (ncols, ER_NM_vect);
  res_pack_els = ER_pack_els (res);
  for (i = 0; i < ncols; i++)
    ((ER_node_t *) res_pack_els) [i]
      = (res_eltp != ER_NM__error
	 ? create_pack_vector (nrows, res_eltp)
	 : create_unpack_vector (nrows));
  for (i = 0; i < nrows; i++)
    {
      row = ((ER_node_t *) pack_els) [i];
      GO_THROUGH_REDIR (row);
      for (j = 0; j < ncols; j++)
	{
	  col = ((ER_node_t *) res_pack_els) [j];
	  if (res_eltp != ER_NM__error)
	    {
	      d_assert (ER_NODE_MODE (row) == ER_NM_heap_pack_vect
			&& ER_NODE_MODE (col) == ER_NM_heap_pack_vect);
	      el_size = type_size_table [res_eltp];
	      memcpy ((char *) ER_pack_els (col) + i * el_size,
		      (char *) ER_pack_els (row) + j * el_size, el_size);
	    }
	  else if (ER_NODE_MODE (row) == ER_NM_heap_pack_vect)
	    {
	      d_assert (ER_NODE_MODE (col) == ER_NM_heap_unpack_vect);
	      eltp = ER_pack_vect_el_mode (row);
	      el_size = type_size_table [eltp];
	      res_el = IVAL (ER_unpack_els (col), i);
	      ER_SET_MODE (res_el, eltp); // ????
	      displ = val_displ_table[eltp];
	      memcpy ((char *) res_el + displ,
		      (char *) ER_pack_els (row) + j * el_size, el_size);
	    }
	  else
	    {
	      d_assert (ER_NODE_MODE (col) == ER_NM_heap_unpack_vect);
	      ((val_t *) ER_unpack_els (col))[i]
		= ((val_t *) ER_unpack_els (row))[j];
	    }
	}
    }
  ER_SET_MODE (fun_result, ER_NM_vect);
  set_vect_dim (fun_result, res, 0);
}

/* This function is a trick to fullfil initiations after execution of
   stmts before __init__ call. */
void
init_call (int pars_number)
{
  ER_node_t var;

  d_assert (pars_number == 0);
  /* ------ Initiations after execution of stmts before __init__ ----- */
  /* Set stdin, stdout, stderr. */
  var = IVAL (ER_stack_vars (cstack), BC_var_num (stdin_bc_decl));
  place_file_instance (stdin, var);
  var = IVAL (ER_stack_vars (cstack), BC_var_num (stdout_bc_decl));
  place_file_instance (stdout, var);
  var = IVAL (ER_stack_vars (cstack), BC_var_num (stderr_bc_decl));
  place_file_instance (stderr, var);
  /* ----- End of the initiations ----- */
  /* Place the result instead of the function. */
  ER_SET_MODE (fun_result, ER_NM_undef);
}

static void
call_external_fun (int pars_number, BC_node_t fdecl)
{
  external_fun_t *fun;
  ER_node_t vect;
  ER_node_t val;
  ER_node_t v;
  ER_node_t tab;
  int curr_actual;

  fun = external_address (fdecl);
  vect = (ER_node_t) create_unpack_vector (pars_number);
  for (curr_actual = 0; curr_actual < pars_number; curr_actual++)
    {
      val = IVAL (ctop, curr_actual - pars_number + 1);
      if (ER_IS_OF_TYPE (val, ER_NM_vect))
	{
	  v = ER_vect (val);
	  GO_THROUGH_REDIR (v);
	  try_full_pack (v);
	  ER_set_vect (val, v);
	}
      else if (ER_IS_OF_TYPE (val, ER_NM_tab))
	{
	  tab = ER_tab (val);
	  GO_THROUGH_REDIR (tab);
	  ER_set_tab (val, tab);
	}
      *(val_t *) IVAL (ER_unpack_els (vect), curr_actual) = *(val_t *) val;
    }
  DECR_CTOP (pars_number);
  /* Put it here as it might call GC and GC might make a long jump.  */
  INCREMENT_PC();
  /* Pop all actual parameters. */
  *(val_t *) ctop
    = (*fun) (pars_number, (val_t *) IVAL (ER_unpack_els (vect), 0));
}

/* Set up formal parameters starting with VARS of BLOCK from
   actual parameters starting with ACTUAL_START.  Check correct number
   of actual parameters.  Initialize rest of VARS_NUMBER vars by
   NIL.  */
static void do_always_inline
setup_pars (BC_node_t block, int actuals_num,
	    ER_node_t vars, val_t *actual_start, int vars_number)
{
  int i, args_p, copies_num, formals_num, min_actuals_num;

  args_p = BC_args_p (block);
  formals_num = BC_pars_num (block) - (args_p ? 1 : 0);
  min_actuals_num = BC_min_pars_num (block);
  if (actuals_num < min_actuals_num)
    eval_error (parnumber_bc_decl, get_cpos (),
		DERR_too_few_actual_parameters, BC_ident (BC_fdecl (block)));
  else if (actuals_num > formals_num && ! args_p)
    eval_error (parnumber_bc_decl, get_cpos (),
		DERR_too_many_actual_parameters, BC_ident (BC_fdecl (block)));
  copies_num = (formals_num < actuals_num ? formals_num : actuals_num);
  /* Transfer actuals.  */
  for (i = 0; i < copies_num; i++)
    *(val_t *) IVAL (vars, i) = *actual_start++;
  /* Reset rest of variables.  */
  reset_vars ((ER_node_t) ((val_t *) vars + copies_num),
	      (ER_node_t) ((val_t *) vars + vars_number));
  if (args_p)
    {
      ER_node_t vect;
      int nargs = actuals_num - copies_num;
      
      if (nargs == 0)
	vect = create_empty_vector ();
      else
	vect = (ER_node_t) create_unpack_vector (nargs);
      /* Args number */
      ER_SET_MODE (IVAL (vars, formals_num), ER_NM_vect);
      set_vect_dim (IVAL (vars, formals_num), vect, 0);
      for (i = 0; i < nargs; i++)
	*(val_t *) IVAL (ER_unpack_els (vect), i) = *actual_start++;
    }
}

static ER_node_t do_always_inline
create_class_stack (BC_node_t class_block, ER_node_t context,
		    val_t *actuals_start, int actuals_num, int simple_p)
{
  ER_node_t stack;
  pc_t saved_cpc = cpc;

  heap_push (class_block, context, -1);
  setup_pars (class_block, actuals_num, ER_stack_vars (cstack),
	      actuals_start, BC_vars_num (class_block));
  stack = cstack;
  if (simple_p)
    {
      heap_pop ();
      cpc = saved_cpc;
    }
  return stack;
}

unsigned int generated_c_functions_num, generated_c_function_calls_num;

/* The following variable is PC of the last call of real DINO function
   (not external or implementation function).  It is used to
   diagnostic of earley parser functions. */
pc_t real_fun_call_pc;

/* Function processing tail (if TAIL_FLAG) call of function CODE with
   CONTEXT with ACTUALS_NUM params starting with PAR_START.  */
static void do_always_inline
process_fun_call (val_t *par_start, BC_node_t code, ER_node_t context,
		  int actuals_num, int tail_flag, int from_c_code_p)
{
  BC_node_t block = code;
  int vars_number = BC_vars_num (block);
  
  if (BC_thread_p (code) && sync_flag)
    /* We check it before creating a stack (see
       find_catch_pc).  */
    eval_error (syncthreadcall_bc_decl, get_cpos (),
		DERR_thread_call_in_sync_stmt);
  real_fun_call_pc = cpc;
  d_assert (BC_NODE_MODE (code) == BC_NM_fblock
	    && BC_fmode (code) != BC_builtin);
  if (tail_flag && ! BC_ext_life_p (ER_block_node (cstack))
      && context != cstack && cstack != uppest_stack
      /* We should not worry about extending stack.  Finally in the
	 chain of calls of different functions we have a function
	 block big enough to contain all subsequent tail calls.  */
      && (ER_all_block_vars_num (cstack) >= vars_number + BC_tvars_num (block)))
    {
      ER_set_context (cstack, context);
      ER_set_block_node (cstack, block);
      ctop = IVAL (cvars, vars_number - 1);
    }
  else
    heap_push (block, context, -1);
  setup_pars (block, actuals_num, cvars, par_start, vars_number);
  do_call (block, from_c_code_p);
  if (BC_thread_p (code))
    {
      ER_node_t process;
      
      process = create_process (cpc, code, context);
      cpc = BC_next (ER_call_pc (cstack));
      ER_set_ctop (cstack, (char *) ctop);
      cstack = ER_prev_stack (cstack);
      ER_set_saved_cstack (cprocess, cstack);
      cvars = ER_stack_vars (cstack);
      ctop = (ER_node_t) ER_ctop (cstack);
      TOP_UP;
      ER_SET_MODE (ctop, ER_NM_process);
      ER_set_process (ctop, process);
      TOP_DOWN;
    }
}

void do_always_inline
process_imm_ifun_call (BC_node_t code, int actuals_num, int from_c_code_p)
{
  implementation_fun_t ifunc;
  int saved_curr_from_c_code_p = curr_from_c_code_p;
  
  curr_from_c_code_p = from_c_code_p;
  d_assert (BC_NODE_MODE (code) == BC_NM_fblock);
  ifunc = BC_implementation_fun (code);
  d_assert (ifunc != NULL);
  fun_result = IVAL (ctop, 1);
  DECR_CTOP (-actuals_num);
  ifun_call_pc = cpc;
  INCREMENT_PC (); /*  Put it here as GC might make a long jump.  */
  (*ifunc) (actuals_num);
  DECR_CTOP (actuals_num);
  curr_from_c_code_p = saved_curr_from_c_code_p;
}


/* A general function processing tail (if TAIL_FLAG) fun/thread/class
   call of fun FDECL in CONTEXT with ACTUALS_NUM paramaters starting
   with ACTUALS_START and all call related data starting with
   CALL_START.  If CALL_START refers to the fun value if it is present
   on the stack.  The function does not setup */
void do_inline
process_fun_class_call (BC_node_t fdecl, ER_node_t context,
			ER_node_t call_start, ER_node_t actuals_start,
			int actuals_num, int tail_flag, int from_c_code_p)
{
  BC_node_t fblock;
  implementation_fun_t ifunc;
  ER_node_t instance;

  d_assert (call_start == actuals_start
	    || IVAL (call_start, 1) == actuals_start);
  if (BC_NODE_MODE (fdecl) == BC_NM_efdecl)
    {
      d_assert (! tail_flag);
      ctop = IVAL (actuals_start, actuals_num - 1);
      call_external_fun (actuals_num, fdecl);
      ctop = IVAL (call_start, -1);
      return;
    }
  if (BC_NODE_MODE (fdecl) != BC_NM_fdecl)
    eval_error (callop_bc_decl, get_cpos (),
		DERR_none_class_or_fun_before_left_bracket);
  fblock = BC_fblock (fdecl);
  if (BC_class_p (fblock))
    {
      ctop = IVAL (call_start, -1);
      instance = create_class_stack (fblock, context,
				     (val_t *) actuals_start, actuals_num,
				     BC_simple_p (fblock));
      if (BC_simple_p (fblock))
	{
	  TOP_UP;
	  ER_SET_MODE (ctop, ER_NM_stack);
	  ER_set_stack (ctop, instance);
	  TOP_DOWN;
	  INCREMENT_PC ();
	  return;
	}
      do_call (fblock, from_c_code_p);
    }
  else if (BC_fmode (fblock) == BC_builtin)
    {
      int saved_curr_from_c_code_p = curr_from_c_code_p;
      
      curr_from_c_code_p = from_c_code_p;
      ifunc = BC_implementation_fun (fblock);
      d_assert (ifunc != NULL);
      fun_result = call_start;
      ctop = IVAL (actuals_start, actuals_num - 1);
      ifun_call_pc = cpc;
      INCREMENT_PC (); /* Put it here as GC might make a long jump.  */
      (*ifunc) (actuals_num);
      ctop = IVAL (call_start, -1);
      curr_from_c_code_p = saved_curr_from_c_code_p;
    }
  else
    {
      /* Don't loose data below params in GC called during the
	 function execution.  We don't need to be accurate here
	 (e.g. exclude calculated function value on the stack) as
	 after the call ctop continues stay the same untill next call.
	 It is conservative but safe.  */
      ctop = IVAL (call_start, -1);
      process_fun_call ((val_t *) actuals_start, fblock, context,
			actuals_num, tail_flag, from_c_code_p);
    }
}

/* A general function processing tail (if TAIL_FLAG) fun/thread/class
   call of fun with ACTUALS_NUM paramaters starting with
   CALL_START.  */
void
process_call (ER_node_t call_start, int actuals_num,
	      int tail_flag, int from_c_code_p)
{
  BC_node_t fdecl = NULL;
  ER_node_t context = NULL;

  if (ER_NODE_MODE (call_start) == ER_NM_efun)
    fdecl = ER_efdecl (call_start);
  else if (ER_NODE_MODE (call_start) == ER_NM_code)
    {
      fdecl = BC_fdecl (ID_TO_CODE (ER_code_id (call_start)));
      context = ER_code_context (call_start);
    }
  process_fun_class_call (fdecl, context, call_start, IVAL (call_start, 1),
			  actuals_num, tail_flag, from_c_code_p);
}

void
initiate_funcs (void)
{
  if (trace_flag)
    VLO_CREATE (trace_stack, 0);
  initiate_io ();
  initiate_regex_tab ();
#ifdef RFLOAT_NAN
  rfloat_nan = RFLOAT_NAN;
  minus_rfloat_nan = -RFLOAT_NAN;
#endif
}

void
finish_funcs (void)
{
  finish_regex_tab ();
  finish_io ();
  if (trace_flag)
    VLO_DELETE (trace_stack);
}



#include "earley.h"

/* This page contains interface to earley parser.  See file
   `d_ir.sprut' for details in interface. */

/* The following function implements function set_grammar in class
   parser. */
void
int_earley_parse_grammar (int npars)
{
  struct grammar *g;
  int code;
  ER_node_t par1, par2, par3, v;
  const char *name = "set_grammar";

  par1 = IVAL (ctop, -2);
  implicit_int_conversion (below_ctop, NULL);
  par2 = below_ctop;
  par3 = IVAL (ctop, 0);
  d_assert (npars == 3 && ER_NODE_MODE (par1) == ER_NM_hide);
  if (ER_NODE_MODE (par3) == ER_NM_vect)
    {
      v = ER_vect (par3);
      GO_THROUGH_REDIR (v);
      try_full_pack (v);
      ER_set_vect (par3, v);
    }
  if (ER_NODE_MODE (par2) != ER_NM_int || ER_NODE_MODE (par3) != ER_NM_vect
      || ER_NODE_MODE (ER_vect (par3)) != ER_NM_heap_pack_vect
      || (ER_pack_vect_el_mode (ER_vect (par3)) != ER_NM_char
	  && ER_pack_vect_el_mode (ER_vect (par3)) != ER_NM_byte))
    eval_error (partype_bc_decl, get_pos (real_fun_call_pc),
		DERR_parameter_type, name);
  g = (struct grammar *) ER_hide (par1);
  code = earley_parse_grammar (g, ER_i (par2),
			       strvect_to_utf8 (ER_vect (par3), TRUE)); // ???
  if (code == EARLEY_NO_MEMORY)
    eval_error (pmemory_bc_decl, get_pos (real_fun_call_pc),
		"run time error (%s) -- no parser memory", name);
  else if (code != 0)
    eval_error (invgrammar_bc_decl, get_pos (real_fun_call_pc),
		"run time error (%s) -- %s", name, earley_error_message (g));
  /* Returned value should be ignored. */
  ER_SET_MODE (fun_result, ER_NM_undef);
}

/* The following function implements function set_debug in class
   parser. */
void
int_earley_set_debug_level (int npars)
{
  int i;
  ER_node_t par1, par2;
  const char *name = "set_debug";

  par1 = IVAL (ctop, -1);
  implicit_int_conversion (ctop, NULL);
  par2 = IVAL (ctop, 0);
  d_assert (npars == 2 && ER_NODE_MODE (par1) == ER_NM_hide);
  if (ER_NODE_MODE (par2) != ER_NM_int)
    eval_error (partype_bc_decl, get_pos (real_fun_call_pc),
		DERR_parameter_type, name);
  i = earley_set_debug_level ((struct grammar *) ER_hide (par1),
			      ER_i (par2));
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, i);
}

/* The following function implements function set_one_parse in class
   parser. */
void
int_earley_set_one_parse_flag (int npars)
{
  int i;
  ER_node_t par1, par2;
  const char *name = "set_one_parse";

  par1 = IVAL (ctop, -1);
  implicit_int_conversion (ctop, NULL);
  par2 = IVAL (ctop, 0);
  d_assert (npars == 2 && ER_NODE_MODE (par1) == ER_NM_hide);
  if (ER_NODE_MODE (par2) != ER_NM_int)
    eval_error (partype_bc_decl, get_pos (real_fun_call_pc),
		DERR_parameter_type, name);
  i = earley_set_one_parse_flag ((struct grammar *) ER_hide (par1),
				 ER_i (par2));
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, i);
}

/* The following function implements function set_lookahead in class
   parser. */
void
int_earley_set_lookahead_level (int npars)
{
  int i;
  ER_node_t par1, par2;
  const char *name = "set_lookahead";

  par1 = IVAL (ctop, -1);
  implicit_int_conversion (ctop, NULL);
  par2 = IVAL (ctop, 0);
  d_assert (npars == 2 && ER_NODE_MODE (par1) == ER_NM_hide);
  if (ER_NODE_MODE (par2) != ER_NM_int)
    eval_error (partype_bc_decl, get_pos (real_fun_call_pc),
		DERR_parameter_type, name);
  i = ER_i (par2);
  i = earley_set_lookahead_level ((struct grammar *) ER_hide (par1),
				  i ? 1 : 0);
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, i);
}

/* The following function implements function set_cost in class
   parser. */
void
int_earley_set_cost_flag (int npars)
{
  int i;
  ER_node_t par1, par2;
  const char *name = "set_cost";

  par1 = IVAL (ctop, -1);
  implicit_int_conversion (ctop, NULL);
  par2 = IVAL (ctop, 0);
  d_assert (npars == 2 && ER_NODE_MODE (par1) == ER_NM_hide);
  if (ER_NODE_MODE (par2) != ER_NM_int)
    eval_error (partype_bc_decl, get_pos (real_fun_call_pc),
		DERR_parameter_type, name);
  i = earley_set_cost_flag ((struct grammar *) ER_hide (par1), ER_i (par2));
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, i);
}

/* The following function implements function set_recovery in class
   parser. */
void
int_earley_set_error_recovery_flag (int npars)
{
  int i;
  ER_node_t par1, par2;
  const char *name = "set_recovery";

  par1 = IVAL (ctop, -1);
  implicit_int_conversion (ctop, NULL);
  par2 = IVAL (ctop, 0);
  d_assert (npars == 2 && ER_NODE_MODE (par1) == ER_NM_hide);
  if (ER_NODE_MODE (par2) != ER_NM_int)
    eval_error (partype_bc_decl, get_pos (real_fun_call_pc),
		DERR_parameter_type, name);
  i = earley_set_error_recovery_flag ((struct grammar *)
				      ER_hide (par1),
				      ER_i (par2));
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, i);
}

/* The following function implements function set_recovery_match in
   class parser. */
void
int_earley_set_recovery_match (int npars)
{
  int i;
  ER_node_t par1, par2;
  const char *name = "set_recovery_match";

  par1 = IVAL (ctop, -1);
  implicit_int_conversion (ctop, NULL);
  par2 = IVAL (ctop, 0);
  d_assert (npars == 2 && ER_NODE_MODE (par1) == ER_NM_hide);
  if (ER_NODE_MODE (par2) != ER_NM_int)
    eval_error (partype_bc_decl, get_pos (real_fun_call_pc),
		DERR_parameter_type, name);
  i = earley_set_recovery_match ((struct grammar *) ER_hide (par1),
				 ER_i (par2));
  ER_SET_MODE (fun_result, ER_NM_int);
  ER_set_i (fun_result, i);
}

/* The following contains parse tree nodes before they will be placed
   into the heap. */
static os_t tree_mem_os;

/* The following variables are vector of tokens and number of the
   current token to read. */
static ER_node_t tokens_vect;
static size_t curr_token;

/* The following function produces token to earley_parse. */
static int
init_read_token (void **attr)
{
  ER_node_t tok, code;
  BC_node_t decl;
  int n;
  const char *message;
  val_t t, t2;

  d_assert (ER_NODE_MODE (tokens_vect) == ER_NM_heap_pack_vect
	    && ER_pack_vect_el_mode (tokens_vect) == ER_NM_stack);
  if (curr_token >= ER_els_number (tokens_vect))
    return -1;
  tok = *attr = ((ER_node_t *) ER_pack_els (tokens_vect)) [curr_token];
  if (! ER_IS_OF_TYPE (tok, ER_NM_heap_stack))
    ER_SET_MODE ((ER_node_t) &t, ER_NM_nil);
  else
    {
      ER_SET_MODE ((ER_node_t) &t, ER_NM_stack);
      ER_set_stack ((ER_node_t) &t, tok);
    }
  ER_SET_MODE ((ER_node_t) &t2, ER_NM_code);
  ER_set_code_id ((ER_node_t) &t2, CODE_ID (token_bc_decl));
  if (! internal_isa_call (&message, (ER_node_t) &t2, (ER_node_t) &t))
    eval_error (invtoken_bc_decl, get_pos (real_fun_call_pc),
		"run time error (parse) -- invalid token #%lu",
		(unsigned long) curr_token);
  curr_token++;
  decl = get_another_block_decl (ER_block_node (tok), code_bc_decl);
  d_assert (BC_NODE_MODE (decl) == BC_NM_vdecl);
  n = BC_var_num (decl);
  code = IVAL (ER_stack_vars (tok), n);
  if (ER_NODE_MODE (code) != ER_NM_int)
    eval_error (invtoken_bc_decl, get_pos (real_fun_call_pc),
		"run time error (parse) -- invalid code of token #%d",
		curr_token - 1);
  return ER_i (code);
}

/* The following is DINO error function called by parser and its
   context. */
static BC_node_t error_fun_block;
static ER_node_t error_fun_block_context;

/* The following function is interface to DINO error function.  We
   need to provide at least 6 temporary variables (see trick for this
   in environment) for this function plus 4 vars for
   int_earley_parse. */
static void
init_syntax_token (int err_tok_num, void *err_tok_attr,
		   int start_ignored_tok_num, void *start_ignored_tok_attr,
		   int start_recovered_tok_num, void *start_recovered_tok_attr)
{
  TOP_UP;
  ER_SET_MODE (ctop, ER_NM_int);
  ER_set_i (ctop, err_tok_num);
  TOP_UP;
  if (err_tok_attr == NULL)
    ER_SET_MODE (ctop, ER_NM_nil);
  else
    {
      ER_SET_MODE (ctop, ER_NM_stack);
      ER_set_stack (ctop, err_tok_attr);
    }
  TOP_UP;
  ER_SET_MODE (ctop, ER_NM_int);
  ER_set_i (ctop, start_ignored_tok_num);
  TOP_UP;
  if (start_ignored_tok_attr == NULL)
    ER_SET_MODE (ctop, ER_NM_nil);
  else
    {
      ER_SET_MODE (ctop, ER_NM_stack);
      ER_set_stack (ctop, start_ignored_tok_attr);
    }
  TOP_UP;
  ER_SET_MODE (ctop, ER_NM_int);
  ER_set_i (ctop, start_recovered_tok_num);
  TOP_UP;
  if (start_recovered_tok_attr == NULL)
    ER_SET_MODE (ctop, ER_NM_nil);
  else
    {
      ER_SET_MODE (ctop, ER_NM_stack);
      ER_set_stack (ctop, start_recovered_tok_attr);
    }
  call_fun_class (error_fun_block, error_fun_block_context, 6,
		  curr_from_c_code_p);
}

struct tree_heap_node
{
  struct earley_tree_node *tree_node;
  ER_node_t heap_node;
};

static struct tree_heap_node temp_tree_heap_node;
static hash_table_t tree_heap_tab;

/* Hash of the node. */
static unsigned
tree_heap_node_hash (hash_table_entry_t n)
{
  struct tree_heap_node *node = ((struct tree_heap_node *) n);

  return (size_t) node->tree_node;
}

/* Equality of nodes. */
static int
tree_heap_node_eq (hash_table_entry_t n1, hash_table_entry_t n2)
{
  struct tree_heap_node *node1 = ((struct tree_heap_node *) n1);
  struct tree_heap_node *node2 = ((struct tree_heap_node *) n2);

  return node1->tree_node == node2->tree_node;
}

/* The following function places abstract tree into heap and returns
   the result. */
static ER_node_t
tree_to_heap (struct earley_tree_node *root)
{
  hash_table_entry_t *entry;
  ER_node_t var, res, vect, name_vect;
  struct earley_tree_node *node, *alt;
  struct tree_heap_node *tree_heap_node;
  int i;

  tree_heap_node = &temp_tree_heap_node;
  tree_heap_node->tree_node = root;
  entry = find_hash_table_entry (tree_heap_tab, tree_heap_node, TRUE);
  if (*entry != NULL)
    return ((struct tree_heap_node *) *entry)->heap_node;
  TOP_UP;
  ER_SET_MODE (ctop, ER_NM_vect);
  switch (root->type)
    {
    case EARLEY_NIL:
    case EARLEY_ERROR:
      var = IVAL (ER_stack_vars (uppest_stack),
		  BC_var_num (root->type == EARLEY_NIL
			      ? nil_anode_bc_decl : error_anode_bc_decl));
      d_assert (ER_NODE_MODE (var) == ER_NM_stack);
      res = ER_stack (var);
      DECR_CTOP (1);
      break;
    case EARLEY_TERM:
      name_vect = create_string ("$term");
      set_vect_dim (ctop, name_vect, 0);
      TOP_UP;
      ER_SET_MODE (ctop, ER_NM_stack);
      d_assert (ER_NODE_MODE ((ER_node_t) root->val.term.attr)
		== ER_NM_heap_stack);
      ER_set_stack (ctop, root->val.term.attr);
      DECR_CTOP (2);
      res = create_class_stack (anode_bc_decl, uppest_stack,
				(val_t *) IVAL (ctop, 1), 2, TRUE);
      break;
    case EARLEY_ANODE:
      name_vect = create_string (root->val.anode.name);
      set_vect_dim (ctop, name_vect, 0);
      for (i = 0; root->val.anode.children [i] != NULL; i++)
	;
      vect = create_empty_vector ();
      ER_set_pack_vect_el_mode (vect, ER_NM_stack);
      vect = expand_vector (vect, i);
      TOP_UP;
      ER_SET_MODE (ctop, ER_NM_vect);
      set_vect_dim (ctop, vect, 0);
      DECR_CTOP (2);
      res = create_class_stack (anode_bc_decl, uppest_stack,
				(val_t *) IVAL (ctop, 1), 2, TRUE);
      break;
    case EARLEY_ALT:
      name_vect = create_string ("$alt");
      set_vect_dim (ctop, name_vect, 0);
      for (i = 0, alt = root; alt != NULL; alt = alt->val.alt.next, i++)
	;
      vect = create_empty_vector ();
      ER_set_pack_vect_el_mode (vect, ER_NM_stack);
      vect = expand_vector (vect, i);
      TOP_UP;
      ER_SET_MODE (ctop, ER_NM_vect);
      set_vect_dim (ctop, vect, 0);
      DECR_CTOP (2);
      res = create_class_stack (anode_bc_decl, uppest_stack,
				(val_t *) IVAL (ctop, 1), 2, TRUE);
      break;
    default:
      d_unreachable ();
    }
  OS_TOP_EXPAND (tree_mem_os, sizeof (struct tree_heap_node));
  tree_heap_node = (struct tree_heap_node *) OS_TOP_BEGIN (tree_mem_os);
  *entry = tree_heap_node;
  OS_TOP_FINISH (tree_mem_os);
  tree_heap_node->tree_node = root;
  tree_heap_node->heap_node = res;
  if (root->type == EARLEY_ANODE)
    {
      for (i = 0; (node = root->val.anode.children [i]) != NULL; i++)
	((ER_node_t *) ER_pack_els (vect)) [i] = tree_to_heap (node);
      ER_set_els_number (vect, i);
    }
  else if (root->type == EARLEY_ALT)
    {
      for (i = 0, alt = root; alt != NULL; alt = alt->val.alt.next, i++)
	((ER_node_t *) ER_pack_els (vect)) [i]
	  = tree_to_heap (alt->val.alt.node);
      ER_set_els_number (vect, i);
    }
  return res;
}

/* The following function allocates memory for the parse tree. */
static void *
int_parse_alloc (int nmemb)
{
  void *res;

  OS_TOP_EXPAND (tree_mem_os, nmemb);
  res = OS_TOP_BEGIN (tree_mem_os);
  OS_TOP_FINISH (tree_mem_os);
  return res;
}

/* The following function implements function parse in class
   parser. */
void
int_earley_parse (int npars)
{
  struct grammar *g;
  int code, ambiguous_p;
  struct earley_tree_node *root;
  ER_node_t par1, par2, par3, v;
  ER_node_t instance, var;
  const char *name = "parse";
  int fun_result_offset;

  par1 = IVAL (ctop, -2);
  par2 = IVAL (ctop, -1);
  par3 = IVAL (ctop, 0);
  d_assert (npars == 3 && ER_NODE_MODE (par1) == ER_NM_hide);
  if (ER_NODE_MODE (par2) == ER_NM_vect)
    {
      v = ER_vect (par2);
      GO_THROUGH_REDIR (v);
      try_full_pack (v);
      ER_set_vect (par2, v);
    }
  if (ER_NODE_MODE (par2) != ER_NM_vect
      || (ER_NODE_MODE (ER_vect (par2))
	  != ER_NM_heap_pack_vect)
      || (ER_pack_vect_el_mode (ER_vect (par2)) != ER_NM_stack)
      || ! function_p (par3))
    eval_error (partype_bc_decl, get_pos (real_fun_call_pc),
		DERR_parameter_type, name);
  fun_result_offset = (val_t *) fun_result - (val_t *) cvars;
  /* We switch off GC because the parser may call error function
     several times and parser has references to tokens in the heap. */
  tokens_vect = ER_vect (par2);
  curr_token = 0;
  error_fun_block = ID_TO_CODE (ER_code_id (par3));
  error_fun_block_context = ER_code_context (par3);
  g = (struct grammar *) ER_hide (par1);
  OS_CREATE (tree_mem_os, 0);
  tree_heap_tab = create_hash_table (2 * ER_els_number (tokens_vect)
				     * sizeof (struct tree_heap_node *),
				     tree_heap_node_hash, tree_heap_node_eq);
  /* We need it because init_syntax_token may change it. */
  code = earley_parse (g, init_read_token, init_syntax_token,
		       int_parse_alloc, NULL, &root, &ambiguous_p);
  if (code == EARLEY_NO_MEMORY)
    {
      delete_hash_table (tree_heap_tab);
      OS_DELETE (tree_mem_os);
      eval_error (pmemory_bc_decl, get_pos (real_fun_call_pc),
		  "run time error (%s) -- no parser memory", name);
    }
  else if (code == EARLEY_UNDEFINED_OR_BAD_GRAMMAR)
    {
      delete_hash_table (tree_heap_tab);
      OS_DELETE (tree_mem_os);
      eval_error (invgrammar_bc_decl, get_pos (real_fun_call_pc),
		  "run time error (%s) -- %s", name, earley_error_message (g));
    }
  else if (code == EARLEY_INVALID_TOKEN_CODE)
    {
      delete_hash_table (tree_heap_tab);
      OS_DELETE (tree_mem_os);
      eval_error (invtoken_bc_decl, get_pos (real_fun_call_pc),
		  "run time error (%s) -- %s", name, earley_error_message (g));
    }
  else
    d_assert (code == 0);
  /* Set up ambiguous_p. */
  instance = ER_context (cstack);
  d_assert (instance != NULL && ER_NODE_MODE (instance) == ER_NM_heap_stack
	    && ER_stack_block (instance) == parser_bc_decl); /* ??? */
  var = IVAL (ER_stack_vars (instance), BC_var_num (ambiguous_p_bc_decl)); /* ??? */
  ER_SET_MODE (var, ER_NM_int);
  ER_set_i (var, ambiguous_p);
  fun_result = IVAL (cvars, fun_result_offset);
  if (root == NULL)
    ER_SET_MODE (fun_result, ER_NM_nil);
  else
    {
      /* Translation into heap: */
      instance = tree_to_heap (root);
      d_assert (ER_NODE_MODE (instance) == ER_NM_heap_stack);
      ER_SET_MODE (fun_result, ER_NM_stack);
      ER_set_stack (fun_result, instance);
    }
  delete_hash_table (tree_heap_tab);
  OS_DELETE (tree_mem_os);
}

/* The following function is used to initiate class parser. */
void
int_earley_create_grammar (int npars)
{
  struct grammar *g;

  d_assert (npars == 0);
  g = earley_create_grammar ();
  if (g == NULL)
    eval_error (pmemory_bc_decl, get_pos (real_fun_call_pc),
		"run time error (parser) -- no parser memory");
  ER_SET_MODE (fun_result, ER_NM_hide);
  ER_set_hide (fun_result, g);
}
