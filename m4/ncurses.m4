dnl $Id$
dnl in unicode check ncursesw/ncurses.h

AC_DEFUN([AC_CHECK_NCURSES],
[
	AC_SUBST(NCURSES_LIBS)
	AC_SUBST(NCURSES_CPPFLAGS)

	AC_ARG_WITH(ncurses, 		AC_HELP_STRING([--with-ncurses[=dir]],	[Compile with ncurses/locate base dir]), [
		if test "x$withval" = "xno"; then
			without_ncurses=yes
		elif test "x$withval" != "xyes"; then
			NCURSES_CPPFLAGS="-I$withval/include"
			NCURSES_LIBS="-L$withval/lib"
		fi
	])

	if test "x$without_ncurses" != "xyes"; then
		save_CPPFLAGS="$CPPFLAGS"
		save_LIBS="$LIBS"
		CPPFLAGS="$CPPFLAGS $NCURSES_CPPFLAGS"
		LIBS="$LIBS $NCURSES_LIBS"
		NLIBRARY=""
		have_ncurses_h=""
		opt=ncurses.h
		if test "x$enable_unicode" != "xyes"; then
			AC_CHECK_HEADERS([ncurses.h],
			[
				have_ncurses_h="yes"
			], [
				AC_CHECK_HEADERS([ncurses/ncurses.h],
				[
					have_ncurses_h="yes"
				])
			])
		else
			AC_CHECK_HEADERS([ncursesw/ncurses.h],
			[
				have_ncurses_h="yes"
			])
		fi
		if test "x$have_ncurses_h" = "xyes"; then
			if test "x$enable_unicode" != "xyes"; then
				NLIBRARY="ncurses"
			else
				NLIBRARY="ncursesw"
			fi
			AC_CHECK_LIB([$NLIBRARY], [initscr],
			[
				AC_DEFINE([HAVE_NCURSES], 1, [define if you have ncurses])
				NCURSES_LIBS="$NCURSES_LIBS -l$NLIBRARY"
				have_ncurses="yes"

				AC_CHECK_LIB([$NLIBRARY], [use_legacy_coding],
				[
					AC_DEFINE([HAVE_NCURSES_ULC], 1, [define if your ncurses has use_legacy_coding()])
				])
			])
		fi

		$as_unset have_ncurses_h

		CPPFLAGS="$save_CPPFLAGS"
		LIBS="$save_LIBS"
	fi
])

