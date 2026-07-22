AC_INIT([1985], [0.4.7], [], [1985])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])

AC_PROG_CC
AX_CHECK_COMPILE_FLAG([-std=c11],
    [AM_CFLAGS="$AM_CFLAGS -std=c11"],
    [AC_MSG_ERROR([C11-capable compiler required])])
AC_SUBST([AM_CFLAGS])

case "$host_os" in
  haiku*)
    for d in /boot/system/develop/lib/x86/pkgconfig \
             /boot/system/develop/lib/pkgconfig; do
      if test -d "$d"; then
        PKG_CONFIG_PATH="$d${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
      fi
    done
    export PKG_CONFIG_PATH
    ;;
esac
PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES([SDL3], [sdl3], [], [AC_MSG_ERROR([SDL3 not found])])

# Cairo is optional: it backs the host-side PDF printer sink. Without
# it, the printer compiles to a no-op (port FCh/FDh still produce the
# guest-visible "printer present" status but nothing is captured to
# disk). Pass --without-cairo to skip detection (e.g. on platforms
# where shipping the full Cairo stack is impractical).
AC_ARG_WITH([cairo],
  [AS_HELP_STRING([--without-cairo],
                  [build without the PDF printer backend])],
  [], [with_cairo=yes])
if test "x$with_cairo" != "xno"; then
  PKG_CHECK_MODULES([CAIRO], [cairo], [],
    [AC_MSG_WARN([cairo not found — building without PDF printer])
     with_cairo=no])
fi
AM_CONDITIONAL([WITH_CAIRO], [test "x$with_cairo" = "xyes"])
AC_MSG_NOTICE([PDF printer backend: $with_cairo])

AC_CHECK_LIB([m], [sqrt], [], [AC_MSG_ERROR([libm not found])])

# ffmpeg is optional. The built-in GIF encoder remains the default and works
# without it; when found, users may enable a post-capture optimization pass.
AC_PATH_PROG([FFMPEG], [ffmpeg], [])
AS_IF([test -n "$FFMPEG"],
      [AC_DEFINE_UNQUOTED([FFMPEG_PATH], ["$FFMPEG"],
           [Path to ffmpeg binary for GIF optimization])
       AC_DEFINE([HAVE_FFMPEG], [1], [ffmpeg available for GIF optimization])
       AC_MSG_NOTICE([GIF optimization enabled via $FFMPEG])],
      [AC_MSG_NOTICE([GIF optimization disabled (no ffmpeg found)])])

case $host_os in
  mingw*|cygwin*|msys*)
    WIN_LIBS="-lws2_32"
    AC_CHECK_TOOL([WINDRES], [windres], [windres])
    WIN_RC_OBJ="icons/1985-rc.o"
    ;;
  *)
    WIN_LIBS=""
    WIN_RC_OBJ=""
    AC_SEARCH_LIBS([socket],     [network socket])
    AC_SEARCH_LIBS([getsockopt], [network socket])
    ;;
esac
AC_SUBST([WIN_LIBS])
AC_SUBST([WIN_RC_OBJ])
AC_SUBST([WINDRES])

PROG_GIT_COMMIT=`git -C "$srcdir" rev-parse --short HEAD 2>/dev/null || echo unknown`
AC_MSG_NOTICE([git commit: $PROG_GIT_COMMIT])
AC_SUBST([PROG_GIT_COMMIT])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
