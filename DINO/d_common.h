/*
   Copyright (C) 1997-2014 Vladimir Makarov.

   Written by Vladimir Makarov <vmakarov@users.sourceforge.net>

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

#include <stdarg.h>
#include "d_dino.h"

#include "allocate.h"
#include "vlobject.h"
#include "position.h"
#include "hashtab.h"
#include "ticker.h"
#include "bits.h"
#include "d_errors.h"
#include "d_types.h"

#define FALSE 0
#define TRUE 1

/* Determine positions for the tabs. */
#define TAB_STOP 8

/* The following value is used as a variable value which is not char.  */
#define NOT_A_CHAR (-2000)

/* Macros return value of digit CH.  Macros is undefined for non digit. */

#define VALUE_OF_DIGIT(ch) ((ch) - '0')
#define STANDARD_INPUT_FILE_SUFFIX ".d"

extern const char **include_path_directories;
extern const char **libraries;
extern const char *command_line_program;
extern FILE *input_dump_file;
extern int program_arguments_number;
extern char **program_arguments;
extern char **program_environment;
extern position_t source_position;
extern int bc_nodes_num;
extern unsigned int heap_chunk_size;
extern int repl_flag;
extern int statistics_flag;
extern int trace_flag;
extern int profile_flag;
extern int dump_flag;
extern double start_time;
extern int it_is_int_string (const char *str);
extern int_t a2i (const char *str);
extern floating_t a2f (const char *str);
extern const char *i2a (int_t number);
extern const char *f2a (floating_t number);
extern char *get_ch_repr (int ch);
extern int read_string_code (int input_char, int *correct_newln,
			     int d_getc (void), void d_ungetc (int));
extern void print_cont_prompt (void);
extern void print_stmt_cont_prompt (void);
extern void d_verror (int fatal_error_flag, position_t position,
		      const char *format, va_list ap);
extern void d_error (int fatal_error_flag, position_t position,
		     const char *format, ...);
extern void dino_finish (int code);

#define SET_SOURCE_POSITION(ref)     (source_position = IR_pos (ref))

#define ENVIRONMENT_PSEUDO_FILE_NAME "<environment>"

#ifndef HAVE_MEMCPY
extern void *memcpy (void *to, const void *from, size_t size);
#endif /* #ifndef HAVE_MEMCPY */

#ifndef HAVE_MEMSET
extern void *memset (void *to, int value, size_t size);
#endif /* #ifndef HAVE_MEMSET */

#ifndef HAVE_MEMCMP
extern int memcmp (const void *mem1, const void *mem2, size_t size);
#endif /* #ifndef HAVE_MEMCMP */

#ifndef HAVE_MEMMOVE
extern void *memmove (void *s1, const void *s2, size_t n);
#endif /* #ifndef HAVE_MEMMOVE */

#if !defined (NO_PROFILE) && !defined (HAVE_SETITIMER)
#define HAVE_SETITIMER 0
#endif

#ifndef INLINE
#ifdef __GNUC__
#define INLINE 1
#else
#define INLINE 0
#endif
#endif

#ifdef __GNUC__
#define __always__ __attribute__ ((__always_inline__))
#else
#define __always__
#endif

#if INLINE && !defined (SMALL_CODE)
#define do_inline inline __always__
#else
#define do_inline
#endif

#define do_always_inline inline __always__

#ifdef __GNUC__
#define ATTRIBUTE_UNUSED __attribute__ ((__unused__))
#else
#define ATTRIBUTE_UNUSED
#endif

