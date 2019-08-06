AC_INIT([[jafet2]], 0.0.0-pre0, [[marta.niedzwiedz@op.pl]], [[jafet2]])
AM_INIT_AUTOMAKE

AC_GNU_SOURCE

AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_CHECK_TOOL([OBJCOPY], [objcopy], [])
 
AC_ARG_ENABLE(debug, [  --enable-debug  add debug symbols], debug=yes)
if test "x$debug" = "xyes"; then
         CFLAGS="${CFLAGS} -g"
fi
 
AC_ARG_ENABLE(developer, [  --enable-developer  enable warnings], developer=yes)
if test "x$developer" = "xyes"; then
         CFLAGS="${CFLAGS} -gstabs -Wall -Wshadow -Wstrict-prototypes -Wpointer-arith -Wcast-align -Wwrite-strings"
         AC_DEFINE([DEVELOPER], [], [Developer])
fi

AM_CONFIG_HEADER(src/config.h)
AC_OUTPUT([
Makefile
src/Makefile
])