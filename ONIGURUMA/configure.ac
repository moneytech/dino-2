dnl Process this file with autoconf to produce a configure script.
AC_INIT(onig, 6.0.0)

AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([-Wno-portability 1.14])
AC_CONFIG_HEADER(src/config.h)


dnl default value for STATISTICS
STATISTICS=""
AC_ARG_WITH(statistics,
	[  --with-statistics       take matching time statistical data], 
	[ STATISTICS=-DONIG_DEBUG_STATISTICS ])
AC_SUBST(STATISTICS)

dnl check for COMBINATION_EXPLOSION
AC_ARG_ENABLE(combination-explosion-check,
	[  --enable-combination-explosion-check   enable combination explosion check],
	[comb_expl_check=$enableval])
if test "${comb_expl_check}" = yes; then
  AC_DEFINE(USE_COMBINATION_EXPLOSION_CHECK,1,[Define if combination explosion check])
fi

dnl check for CRNL_AS_LINE_TERMINATOR
AC_ARG_ENABLE(crnl-as-line-terminator,
	[  --enable-crnl-as-line-terminator   enable CR+NL as line terminator],
	[crnl_as_line_terminator=$enableval])
if test "${crnl_as_line_terminator}" = yes; then
  AC_DEFINE(USE_CRNL_AS_LINE_TERMINATOR,1,[Define if enable CR+NL as line terminator])
fi


dnl Checks for programs.
AC_PROG_CC
AM_PROG_LIBTOOL
LTVERSION="3:0:0"
AC_SUBST(LTVERSION)

AC_PROG_INSTALL
AC_PROG_MAKE_SET

dnl Checks for libraries.

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(stdlib.h string.h strings.h sys/time.h unistd.h sys/times.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_SIZEOF(int, 4)
AC_CHECK_SIZEOF(short, 2)
AC_CHECK_SIZEOF(long, 4)
AC_C_CONST
AC_HEADER_TIME

dnl Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_MEMCMP

AC_CACHE_CHECK(for prototypes, _cv_have_prototypes,
  [AC_TRY_COMPILE([int foo(int x) { return 0; }], [return foo(10);],
	_cv_have_prototypes=yes,
	_cv_have_prototypes=no)])
if test "$_cv_have_prototypes" = yes; then
  AC_DEFINE(HAVE_PROTOTYPES,1,[Define if compilerr supports prototypes])
fi

AC_CACHE_CHECK(for variable length prototypes and stdarg.h, _cv_stdarg,
  [AC_TRY_COMPILE([
#include <stdarg.h>
int foo(int x, ...) {
	va_list va;
	va_start(va, x);
	va_arg(va, int);
	va_arg(va, char *);
	va_arg(va, double);
	return 0;
}
], [return foo(10, "", 3.14);],
	_cv_stdarg=yes,
	_cv_stdarg=no)])
if test "$_cv_stdarg" = yes; then
  AC_DEFINE(HAVE_STDARG_PROTOTYPES,1,[Define if compiler supports stdarg prototypes])
fi

if test "$GCC" = yes; then
  my_save_cflags="$CFLAGS"
  CFLAGS=-Ofast
  AC_MSG_CHECKING([whether CC supports -Ofast])
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes])]
      [fast=yes],
      [AC_MSG_RESULT([no])]
  )
  CFLAGS="$my_save_cflags"
  if test "$fast" = yes; then
    CFLAGS="`echo $CFLAGS|sed s/-O2//` -Ofast"
    CXXFLAGS="`echo $CXXFLAGS|sed s/-O2//` -Ofast"
  else
    CFLAGS="`echo $CFLAGS|sed s/-O2//` -O3"
    CXXFLAGS="`echo $CXXFLAGS|sed s/-O2//` -O3"
  fi
fi

AC_OUTPUT([Makefile src/Makefile test/Makefile sample/Makefile onig-config], [chmod +x onig-config])
