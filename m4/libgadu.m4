AC_DEFUN([AC_CHECK_LIBGADU],
[
  AC_SUBST([LIBGADU_LIBS])
  AC_SUBST([LIBGADU_CPPFLAGS])

  AC_ARG_WITH([libgadu],		AC_HELP_STRING([--with-libgadu],	[use libgadu]),
      if test "x$withval" = "xno" ; then
        without_libgadu=yes
      elif test "x$withval" != "xyes" ; then
        with_arg="-I$withval/include:-L$withval/lib"
      fi)

  if test "x$without_libgadu" != "xyes" -a "x$with_arg" = "x"; then
    ## for now, we'll check for pkg-config idenpendently - it's cached anyway
    if test "x$PKGCONFIG" != "xno"; then
      AC_MSG_CHECKING([for libgadu])
      if $PKGCONFIG --exists libgadu; then
        LIBGADU_LIBS=$($PKGCONFIG --libs openssl)
        LIBGADU_CPPFLAGS=$($PKGCONFIG --cflags openssl)
        AC_DEFINE(HAVE_OPENSSL, 1, [define if you have OpenSSL])
        AC_MSG_RESULT([yes (found via pkg-config)])
      else
        ## pkg-config nie znalazl... wi�c sprawdzamy standardowo metod�
        AC_MSG_WARN([unknown to pkg-config. use --with-libgadu=DIR if libgadu is installed in nonstandard location])
      fi
    fi
  elif test "x$without_libgadu" != "xyes" -a "x$with_arg" != "x"; then
    ## chca od nas libgadu i na dodatek podali gdzie ich chca
    ## wiec bierzemy je z parametru...

    ## jesli podali skad chca, to musimy zgadnac dodatkowe flagi laczenia

    AC_MSG_NOTICE("libgadu directory specified by user: $with_arg")
    ## is sed really necessary?
    incl=`echo "$with_arg" | sed 's/:.*//'`
    lib=`echo "$with_arg" | sed 's/.*://'`
    LIBGADU_LIBS="$lib -lgadu -lcrypto -lssl $PTHREAD_LIBS"
    LIBGADU_CPPFLAGS="$incl"
  fi

  if test "x$without_libgadu" != "xyes"; then
    ocf="$CPPFLAGS"
    olf="$LIBS"
    CPPFLAGS="$CPPFLAGS $LIBGADU_CPPFLAGS"
    LIBS="$LIBS $LIBGADU_LIBS"
    AC_CHECK_HEADERS([libgadu.h],
    [
      AC_CHECK_LIB([gadu], [gg_logoff],
      [
	AC_CHECK_LIB([gadu], [gg_remind_passwd3], [ AC_DEFINE(HAVE_GG_REMIND_PASSWD3, 1, [define if libgadu have gg_remind_passwd3 since LIBGADU ~20050217 ])])
	AC_CHECK_LIB([gadu], [gg_change_passwd4], [ AC_DEFINE(HAVE_GG_CHANGE_PASSWD4, 1, [define if libgadu have gg_change_passwd4 since LIBGADU ~20030930 ])])

        AC_DEFINE([HAVE_LIBGADU], 1, [define if you have libgadu])
        LIBGADU_LIBS="$LIBGADU_LIBS -lgadu -lcrypto -lssl $PTHREAD_LIBS"
        have_libgadu="yes"
      ])
    ])
    CPPFLAGS="$ocf"
    LIBS="$olf"
  fi
])

