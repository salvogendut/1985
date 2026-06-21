AC_INIT([1985], [0.1.0], [], [1985])
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
AC_CHECK_LIB([m], [sqrt], [], [AC_MSG_ERROR([libm not found])])

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
