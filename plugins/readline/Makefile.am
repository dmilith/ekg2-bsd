EXTRA_DIST = vars-pl.txt vars-en.txt

lib_LTLIBRARIES = readline.la
readline_la_SOURCES = main.c ui-readline.c ui-readline.h readline-completion.c
readline_la_LDFLAGS = -module -avoid-version
readline_la_LIBADD = $(READLINE_LIBS)
readline_la_CPPFLAGS = $(READLINE_CPPFLAGS) $(AM_CPPFLAGS)
readline_la_CFLAGS = $(C_FVISIBILITY) $(READLINE_CFLAGS) $(AM_CFLAGS)

INCLUDES = -DDATADIR=\"$(datadir)/ekg\" -I$(top_srcdir)
override libdir = $(plugindir)

install-data-am:
	mkdir -p $(DESTDIR)$(pkgdatadir)/plugins/readline/
	$(INSTALL) -m 644 $(EXTRA_DIST) $(DESTDIR)$(pkgdatadir)/plugins/readline/
