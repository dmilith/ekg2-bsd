EXTRA_DIST = commands-pl.txt session-pl.txt 

lib_LTLIBRARIES = irc.la
irc_la_SOURCES = irc.c misc.c people.c input.c misc.h irc.h people.h \
		input.h IRCVERSION.h autoacts.c autoacts.h irc-ssl.h
irc_la_LDFLAGS = -module -avoid-version $(IRC_SSL_LIBS)
irc_la_CPPFLAGS = $(IRC_SSL_CPPFLAGS) $(C_FVISIBILITY)

INCLUDES = -I$(top_srcdir)
override libdir = $(plugindir)

install-data-am:
	mkdir -p $(DESTDIR)$(pkgdatadir)/plugins/irc/
	$(INSTALL) -m 644 $(EXTRA_DIST) $(DESTDIR)$(pkgdatadir)/plugins/irc/
