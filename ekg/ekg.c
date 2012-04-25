/* $Id$ */

/*
 *  (C) Copyright 2001-2003 Wojtek Kaniewski <wojtekka@irc.pl>
 *			    Robert J. Wo�ny <speedy@ziew.org>
 *			    Pawe� Maziarz <drg@infomex.pl>
 *			    Adam Osuchowski <adwol@polsl.gliwice.pl>
 *			    Dawid Jarosz <dawjar@poczta.onet.pl>
 *			    Wojciech Bojdo� <wojboj@htcon.pl>
 *			    Piotr Domagalski <szalik@szalik.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ekg2-config.h"
#include "win32.h"

#ifndef __FreeBSD__
#define _XOPEN_SOURCE 600
#define __EXTENSIONS__
#endif
#include <sys/types.h>

#ifndef NO_POSIX_SYSTEM
#include <sys/ioctl.h>
#endif

#include <sys/stat.h>
#define __USE_BSD
#include <sys/time.h>

#ifndef NO_POSIX_SYSTEM
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/resource.h>	/* rlimit */
#endif

#include <sys/select.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#else
#  include "compat/getopt.h"
#endif
#include <limits.h>
#include <locale.h>

#ifndef NO_POSIX_SYSTEM
#include <pwd.h>
#else
#include <lm.h>
#endif

#ifdef NO_POSIX_SYSTEM
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#endif

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "bindings.h"
#include "commands.h"
#include "debug.h"
#include "events.h"
#include "configfile.h"
#include "emoticons.h"
#include "log.h"
#include "metacontacts.h"
#include "msgqueue.h"
#include "plugins.h"
#include "protocol.h"
#ifndef HAVE_STRLCPY
#  include "compat/strlcpy.h"
#endif
#include "sessions.h"
#include "stuff.h"
#include "themes.h"
#include "userlist.h"
#include "scripts.h"
#include "vars.h"
#include "windows.h"
#include "xmalloc.h"

#include "queries.h"

char *config_dir;
int mesg_startup;
int ekg_watches_removed;
static char argv0[PATH_MAX];

pid_t speech_pid = 0;

static int stderr_backup = -1;

int no_mouse = 0;

/**
 * ekg_autoaway_timer()
 *
 *
 * less important things which don't need to be checked every main loop iteration
 * e.g. autoaways 
 *
 * executed each second.
 */
static TIMER(ekg_autoaway_timer) {
	session_t *sl;
	time_t t;

	if (type)
		return 0;

	t = time(NULL);

	/* sprawd� autoawaye r�nych sesji */
	for (sl = sessions; sl; sl = sl->next) {
		session_t *s = sl;
		int tmp;

		if (!s->connected || (s->status < EKG_STATUS_AWAY)) /* lowest autostatus is autoxa, so from xa and lower ones
								       we can't go further */
			continue;

		do {
			if ((s->status == EKG_STATUS_AWAY) || (tmp = session_int_get(s, "auto_away")) < 1 || !s->activity)
				break;

			if (t - s->activity > tmp)
				command_exec(NULL, s, ("/_autoaway"), 0);
		} while (0);

		do {
			if ((tmp = session_int_get(s, "auto_xa")) < 1 || !s->activity)
				break;

			if (t - s->activity > tmp)
				command_exec(NULL, s, ("/_autoxa"), 0);
		} while (0);
	}

	return 0;
}

/*
 * ekg_loop()
 *
 * g��wna p�tla ekg. obs�uguje przegl�danie deskryptor�w, timery i wszystko,
 * co ma si� dzia� w tle.
 */

void ekg_loop() {
	struct timeval tv;
	struct timeval stv;
	fd_set rd, wd;
	int ret, maxfd, status;
	pid_t pid;

	gettimeofday(&tv, NULL);

	{
		{		/* przejrzyj timery u�ytkownika, ui, skrypt�w */
			struct timer *t;
			
			for (t = timers; t; t = t->next) {
				if (tv.tv_sec > t->ends.tv_sec || (tv.tv_sec == t->ends.tv_sec && tv.tv_usec >= t->ends.tv_usec)) {
					int ispersist = t->persist;
					
					if (ispersist) {
						memcpy(&t->ends, &tv, sizeof(tv));
						t->ends.tv_sec += (t->period / 1000);
						t->ends.tv_usec += ((t->period % 1000) * 1000);
						if (t->ends.tv_usec >= 1000000) {
							t->ends.tv_usec -= 1000000;
							t->ends.tv_sec++;
						}
					}

					if ((t->function(0, t->data) == -1) || !ispersist)
						t = timers_removei(t);
				}
			}
		}

		{		/* removed 'w->removed' watches, timeout checking moved below select() */
			list_t l;

			for (l = watches; l; l = l->next) {
				watch_t *w = l->data;

				if (w && w->removed == 1) {
					w->removed = 0;
					watch_free(w);
				}
			}
		}

		/* auto save */
		if (config_auto_save && config_changed && (tv.tv_sec - last_save) > config_auto_save) {
			debug("autosaving userlist and config after %d seconds\n", tv.tv_sec - last_save);
			last_save = tv.tv_sec;

			if (!config_write(NULL) && !session_write()) {
				config_changed = 0;
				ekg2_reason_changed = 0;
				print("autosaved");
			} else
				print("error_saving");
		}

		/* przegl�danie zdech�ych dzieciak�w */
#ifndef NO_POSIX_SYSTEM
		while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
			child_t *c;
			debug("child process %d exited with status %d\n", pid, WEXITSTATUS(status));

			for (c = children; c; c = c->next) {
				if (pid != c->pid)
					continue;

				if (pid == speech_pid) {
					speech_pid = 0;

					if (!config_speech_app)
						buffer_free(&buffer_speech);

					if (buffer_speech.count && !WEXITSTATUS(status)) {
						char *str = buffer_tail(&buffer_speech);
						say_it(str);
						xfree(str);
					}
				}

				if (c->handler)
					c->handler(c, c->pid, c->name, WEXITSTATUS(status), c->priv_data);

				c = children_removei(c);
			}
		}
#endif
		/* zerknij na wszystkie niezb�dne deskryptory */

		FD_ZERO(&rd);
		FD_ZERO(&wd);

		{
			list_t l;

			for (maxfd = 0, l = watches; l; l = l->next) {
				watch_t *w = l->data;
				if (!w)
					continue;

				if (w->fd > maxfd)
					maxfd = w->fd;
				if ((w->type & WATCH_READ))
					FD_SET(w->fd, &rd);
				if ((w->type & WATCH_WRITE)) {
					if (w->buf && !w->buf->len) continue; /* if we have WATCH_WRITE_LINE and there's nothink to send, ignore this */ 
					FD_SET(w->fd, &wd); 
				}
			}
		}

		stv.tv_sec = 1;
		stv.tv_usec = 0;
		{
			struct timer *t;

			for (t = timers; t; t = t->next) {
				int usec = 0;

				/* zeby uniknac przekrecenia licznika mikrosekund przy
				 * wiekszych czasach, pomijamy dlugie timery */
				if (t->ends.tv_sec - tv.tv_sec > 1)
					continue;

				/* zobacz, ile zostalo do wywolania timera */
				usec = (t->ends.tv_sec - tv.tv_sec) * 1000000 + (t->ends.tv_usec - tv.tv_usec);

				/* jesli wiecej niz sekunda, to nie ma znacznia */
				if (usec >= 1000000)
					continue;
				
				/* jesli mniej niz aktualny timeout, zmniejsz */
				if (stv.tv_sec * 1000000 + stv.tv_usec > usec) {
					stv.tv_sec = 0;
					stv.tv_usec = usec;
				}
			}
		}

		/* na wszelki wypadek sprawd� warto�ci */
		if (stv.tv_sec != 1)
			stv.tv_sec = 0;
		if (stv.tv_usec < 0)
			stv.tv_usec = 1;

		/* sprawd�, co si� dzieje */
		ret = select(maxfd + 1, &rd, &wd, NULL, &stv);

		/* je�li wyst�pi� b��d, daj zna� */
		if (ret == -1) {
			/* jaki� plugin doda� do watch�w z�y deskryptor. �eby
			 * ekg mog�o dzia�a� dalej, sprawd�my kt�ry to i go
			 * usu�my z listy. */
			if (errno == EBADF) {
				list_t l;

				for (l = watches; l; l = l->next) {
					watch_t *w = l->data;
					struct stat st;

					if (w && fstat(w->fd, &st)) {
						debug("select(): bad file descriptor: fd=%d, type=%d, plugin=%s\n", w->fd, w->type, (w->plugin) ? w->plugin->name : ("none"));
						watch_free(w);
					}
				}
			} else if (errno != EINTR)
				debug("select() failed: %s\n", strerror(errno));
			return;
		}

		{		/* przejrzyj deskryptory */
			list_t l;

			for (l = watches; l; l = l->next) {
				watch_t *w = l->data;

				if (!w)
					continue;

				if (!FD_ISSET(w->fd, &rd) && !FD_ISSET(w->fd, &wd)) { /* timeout checking */
					if (w->timeout < 1 || (tv.tv_sec - w->started) < w->timeout)
						continue;
					w->removed = -1;
					if (w->buf) {
						int (*handler)(int, int, char*, void*) = w->handler;
						if (handler(2, w->fd, NULL, w->data) == -1 || w->removed == 1) {
							w->removed = 0;
							watch_free(w);
							continue;
						}
					} else {
						int (*handler)(int, int, int, void*) = w->handler;
						if (handler(2, w->fd, w->type, w->data) == -1 || w->removed == 1) {
							w->removed = 0;
							watch_free(w);
							continue;
						}
					}
					w->removed = 0;

					continue;
				}

				if (w->fd == 0) {
					session_t *s;
					for (s = sessions; s; s = s->next) 
					{
						if (!s->connected || !s->autoaway)
							continue;

						if (session_int_get(s, "auto_back") != 2)
							continue;

						command_exec(NULL, s, ("/_autoback"), 2);
					}
				}
				if (!w->buf) {
					if (((w->type == WATCH_WRITE) && FD_ISSET(w->fd, &wd)) ||
							((w->type == WATCH_READ) && FD_ISSET(w->fd, &rd)))
						watch_handle(w);
				} else {
					if (FD_ISSET(w->fd, &rd) && w->type == WATCH_READ)		watch_handle_line(w);
					else if (FD_ISSET(w->fd, &wd) && w->type == WATCH_WRITE)	watch_handle_write(w);
				}
			}
		}

		if (ekg_watches_removed > 0) {
			debug("ekg_loop() Removed %d watches this loop, let's cleanup calling: list_cleanup() ...\n", ekg_watches_removed);
			list_cleanup(&watches);
			ekg_watches_removed = 0;
		}
	}
#undef tv

	return;
}
#ifndef NO_POSIX_SYSTEM
static void handle_sigusr1()
{
	debug("sigusr1 received\n");
	query_emit_id(NULL, EKG_SIGUSR1);
	signal(SIGUSR1, handle_sigusr1);
}

static void handle_sigusr2()
{
	debug("sigusr2 received\n");
	query_emit_id(NULL, EKG_SIGUSR2);
	signal(SIGUSR2, handle_sigusr2);
}

static void handle_sighup()
{
	ekg_exit();
}

static void handle_sigsegv()
{
	plugin_t *p;

	signal(SIGSEGV, SIG_DFL);

	if (stderr_backup && stderr_backup != -1)
		dup2(stderr_backup, 2);

	/* wy��cz pluginy ui, �eby odda�y terminal
	 * destroy also log plugins to make sure that latest changes are written */
	for (p = plugins; p; p = p->next) {
		if (p->pclass != PLUGIN_UI && p->pclass != PLUGIN_LOG)
			continue;

		p->destroy();
	}

	fprintf(stderr,
"\r\n"
"\r\n"
"*** Segmentation violation detected ***\r\n"
"\r\n"
"The program will attempt to write its settings, but it is not\r\n"
"guaranteed to succeed. They will be saved as\r\n"
"%s/config.%d,  %s/config-<plugin>.%d\r\n"
"and %s/userlist.%d\r\n"
"\r\n"
"Last messages from the debugging window will be saved to a file called\r\n"
"%s/debug.%d.\r\n"
"\r\n"
"If a file called %s/core.%d will be created, try running the following\r\n"
"command:\r\n"
"\r\n"
"    gdb %s %s/core.%d\r\n"
"\n"
"note the last few lines, and then note the output from the ,,bt'' command.\r\n"
"This will help the program authors find the location of the problem\r\n"
"and most likely will help avoid such crashes in the future.\r\n"
"More details can be found in the documentation, in the file ,,gdb.txt''.\r\n"
"\r\n",
config_dir, (int) getpid(), config_dir, (int) getpid(), config_dir, (int) getpid(), config_dir, (int) getpid(), config_dir,(int) getpid(), argv0, config_dir, (int) getpid());

	config_write_crash();
	userlist_write_crash();
	debug_write_crash();

	raise(SIGSEGV);			/* niech zrzuci core */
}
#endif

/*
 * prepare_batch_line()
 *
 * funkcja bierze podane w linii polece� argumenty i robi z nich pojedy�cz�
 * lini� polece�.
 *
 * - argc - wiadomo co ;)
 * - argv - wiadomo co ;)
 * - n - numer argumentu od kt�rego zaczyna si� polecenie.
 *
 * zwraca stworzon� linie w zaalokowanym buforze lub NULL przy b��dzie.
 */
static char *prepare_batch_line(int argc, char *argv[], int n)
{
	size_t len = 0;
	char *buf;
	int i;

	for (i = n; i < argc; i++)
		len += xstrlen(argv[i]) + 1;

	buf = xmalloc(len);

	for (i = n; i < argc; i++) {
		xstrcat(buf, argv[i]);
		if (i < argc - 1)
			xstrcat(buf, " ");
	}

	return buf;
}

/*
 * handle_stderr()
 *
 * wy�wietla to, co uzbiera si� na stderr.
 */
static WATCHER_LINE(handle_stderr)	/* sta�y */
{
/* XXX */
/*	print("stderr", watch); */
	return 0;
}

/**
 * ekg_debug_handler()
 *
 * debug message [if config_debug set] coming direct from libgadu (by libgadu_debug_handler())
 * or by debug() or by debug_ext()<br>
 * XXX, doc more. But function now is ok.
 * 
 * @sa debug_ext()
 *
 * @bug It can happen than internal string_t @a line will be not freed.
 *
 * @param level 
 * @param format
 * @param ap
 *
 */

void ekg_debug_handler(int level, const char *format, va_list ap) {
	static string_t line = NULL;
	char *tmp;

	int is_UI = 0;
	char *theme_format;

	if (!config_debug)
		return;

	if (!(tmp = vsaprintf(format, ap)))
		return;

	if (line) {
		string_append(line, tmp);
		xfree(tmp);

		if (line->len == 0 || line->str[line->len - 1] != '\n')
			return;

		line->str[line->len - 1] = '\0';	/* remove '\n' */
		tmp = string_free(line, 0);
		line = NULL;
	} else {
		const size_t tmplen = xstrlen(tmp);
		if (tmplen == 0 || tmp[tmplen - 1] != '\n') {
			line = string_init(tmp);
			xfree(tmp);
			return;
		}
		tmp[tmplen - 1] = 0;			/* remove '\n' */
	}

	switch(level) {
		case 0:				theme_format = "debug";		break;
		case DEBUG_IO:			theme_format = "iodebug";	break;
		case DEBUG_IORECV:		theme_format = "iorecvdebug";	break;
		case DEBUG_FUNCTION:		theme_format = "fdebug";	break;
		case DEBUG_ERROR:		theme_format = "edebug";	break;
		case DEBUG_WHITE:		theme_format = "wdebug";	break;
		case DEBUG_WARN:		theme_format = "warndebug";	break;
		case DEBUG_OK:			theme_format = "okdebug";	break;
		case DEBUG_WTF:		theme_format = "wtfdebug";	break;
		default:			theme_format = "debug";		break;
	}

	buffer_add(&buffer_debug, theme_format, tmp);

	query_emit_id(NULL, UI_IS_INITIALIZED, &is_UI);

	if (is_UI && window_debug) {
		print_window_w(window_debug, EKG_WINACT_NONE, theme_format, tmp);

		if (level == DEBUG_WTF) /* if real failure, warn also in current window (XXX: maybe always __status?) */
			print("ekg_failure", tmp);
	}
#ifdef STDERR_DEBUG	/* STDERR debug */
	else
		fprintf(stderr, "%s\n", tmp);
#endif

	xfree(tmp);
}

struct option ekg_options[] = {
	{ "user", required_argument, 0, 'u' },
	{ "theme", required_argument, 0, 't' },
	{ "no-auto", no_argument, 0, 'n' },
	{ "no-mouse", no_argument, 0, 'm' },
	{ "no-global-config", no_argument, 0, 'N' },
	{ "frontend", required_argument, 0, 'F' },

	{ "away", optional_argument, 0, 'a' },
	{ "back", optional_argument, 0, 'b' },
	{ "invisible", optional_argument, 0, 'i' },
	{ "dnd", optional_argument, 0, 'd' },
	{ "free-for-chat", optional_argument, 0, 'f' },
	{ "xa", optional_argument, 0, 'x' },

	{ "unicode", no_argument, 0, 'U' }, 

	{ "help", no_argument, 0, 'h' },
	{ "version", no_argument, 0, 'v' },
	{ 0, 0, 0, 0 }
};

#define EKG_USAGE N_( \
"Usage: %s [OPTIONS] [COMMANDS]\n" \
"  -u, --user=NAME	       uses profile NAME\n" \
"  -t, --theme=FILE	       loads theme from FILE\n"\
"  -n, --no-auto	       does not connect to server automatically\n" \
"  -m, --no-mouse	       does not load mouse support\n" \
"  -N, --no-global-config      ignores global configuration file\n" \
"  -F, --frontend=NAME	       uses NAME frontend (default is ncurses)\n" \
\
"  -a, --away[=DESCRIPTION]    changes status to ``away''\n" \
"  -b, --back[=DESCRIPTION]    changes status to ``available''\n" \
"  -i, --invisible[=DESCR]     changes status to ``invisible''\n" \
"  -d, --dnd[=DESCRIPTION]     changes status to ``do not disturb''\n" \
"  -f, --free-for-chat[=DESCR] changes status to ``free for chat''\n" \
"  -x, --xa[=DESCRIPTION]      changes status to ``very busy''\n" \
\
"  -h, --help		       displays this help message\n" \
"  -v, --version	       displays program version and exits\n" \
"\n" \
"Options concerned with status depend on the protocol of particular session --\n" \
"some sessions may not support ``do not disturb'' status, etc.\n" \
"\n" )


int main(int argc, char **argv)
{
	int auto_connect = 1, c = 0, no_global_config = 0, no_config = 0, new_status = 0;
	char *tmp = NULL, *new_descr = NULL;
	char *load_theme = NULL, *new_profile = NULL, *frontend = NULL;
#ifndef NO_POSIX_SYSTEM
	struct rlimit rlim;
#else
	WSADATA wsaData;
#endif

#ifndef NO_POSIX_SYSTEM
	/* zostaw po sobie core */
	rlim.rlim_cur = RLIM_INFINITY;
	rlim.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_CORE, &rlim);
#endif
#ifdef NO_POSIX_SYSTEM
	if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
		fprintf(stderr, "WSAStartup() failed? wtf?!.. Oh, I see windows ;>");
	}
#endif

	ekg_started = time(NULL);

	ekg2_dlinit();
	setlocale(LC_ALL, "");
	tzset();
#ifdef ENABLE_NLS
	bindtextdomain("ekg2",LOCALEDIR);
	textdomain("ekg2");
#endif
	srand(time(NULL));

	strlcpy(argv0, argv[0], sizeof(argv0));

	home_dir = xstrdup(getenv("HOME"));

#ifndef NO_POSIX_SYSTEM
	if (!home_dir) {
		struct passwd *pw;

		if ((pw = getpwuid(getuid())))
			home_dir = xstrdup(pw->pw_dir);
	}
#else
	if (!home_dir)
		home_dir = xstrdup(getenv("USERPROFILE"));

	if (!home_dir)
		home_dir = xstrdup("c:\\");
#endif

	if (!home_dir) {
		fprintf(stderr, _("Can't find user's home directory. Ask administration to fix it.\n"));
		return 1;
	}

	command_init();
#ifndef NO_POSIX_SYSTEM
	signal(SIGSEGV, handle_sigsegv);
	signal(SIGHUP, handle_sighup);
	signal(SIGTERM, handle_sighup);
	signal(SIGUSR1, handle_sigusr1);
	signal(SIGUSR2, handle_sigusr2);
	signal(SIGALRM, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
#endif
	while ((c = getopt_long(argc, argv, "b::a::i::d::f::x::u:F:t:nmNhvU", ekg_options, NULL)) != -1) 
	{
		switch (c) {
			case 'a':
				if (!optarg && argv[optind] && argv[optind][0] != '-')
					optarg = argv[optind++];

				new_status = EKG_STATUS_AWAY;
				xfree(new_descr);
				new_descr = xstrdup(optarg);
				break;

			case 'b':
				if (!optarg && argv[optind] && argv[optind][0] != '-')
					optarg = argv[optind++];

				new_status = EKG_STATUS_AVAIL;
				xfree(new_descr);
				new_descr = xstrdup(optarg);
				break;

			case 'i':
				if (!optarg && argv[optind] && argv[optind][0] != '-')
					optarg = argv[optind++];

				new_status = EKG_STATUS_INVISIBLE;
				xfree(new_descr);
				new_descr = xstrdup(optarg);
				break;

			case 'd':
				if (!optarg && argv[optind] && argv[optind][0] != '-')
					optarg = argv[optind++];

				new_status = EKG_STATUS_DND;
				xfree(new_descr);
				new_descr = xstrdup(optarg);
				break;

			case 'f':
				if (!optarg && argv[optind] && argv[optind][0] != '-')
					optarg = argv[optind++];

				new_status = EKG_STATUS_FFC;
				xfree(new_descr);
				new_descr = xstrdup(optarg);
				break;

			case 'x':
				if (!optarg && argv[optind] && argv[optind][0] != '-')
					optarg = argv[optind++];

				new_status = EKG_STATUS_XA;
				xfree(new_descr);
				new_descr = xstrdup(optarg);
				break;


			case 'u':
				new_profile = optarg;
				break;
			case 'F':
				frontend = optarg;
				break;
			case 't':
				load_theme = optarg;
				break;

			case 'n':
				auto_connect = 0;
				break;

			case 'm':
				no_mouse = 1;
				break;

			case 'N':
				no_global_config = 1;
				break;


			case 'h':
				printf(_(EKG_USAGE), argv[0]);
				return 0;

			case 'U':
#ifdef USE_UNICODE
				config_use_unicode = 1;
#else
				fprintf(stderr, _("EKG2 compiled without unicode support. This just can't work!\n"));
				return 1;
#endif
				break;

			case 'v':
				printf("ekg2-%s (compiled on %s)\n", VERSION, compile_time());
				return 0;

			case '?':
				/* supported by getopt */
				fprintf(stdout, _("To get more information, start program with --help.\n"));
				return 1;

			default:
				break;
		}
	}

	in_autoexec = 1;

	if (optind < argc) {
		batch_line = prepare_batch_line(argc, argv, optind);
		batch_mode = 1;
	}

	if ((config_profile = new_profile))
		tmp = saprintf("/%s", config_profile);
	else
		tmp = xstrdup("");

	if (getenv("HOME_ETC"))
		config_dir = saprintf("%s/ekg2%s", getenv("HOME_ETC"), tmp);
	else
		config_dir = saprintf("%s/.ekg2%s", home_dir, tmp);

	xfree(tmp);
	tmp = NULL;

	variable_init();
	variable_set_default();

	mesg_startup = mesg_set(MESG_CHECK);
#ifdef DEFAULT_THEME 
	if (theme_read(DEFAULT_THEME, 1) == -1) 
#endif
		theme_init();

	window_debug	= window_new(NULL, NULL, -1);			/* debugowanie */
	window_status	= window_new(NULL, NULL, 1);			/* okno stanu */
	window_current	= window_status;

	if (!no_global_config)
		config_read(SYSCONFDIR "/ekg2.conf");

	if (frontend) {
		plugin_load(frontend, -254, 1);
		config_changed = 1;
	}

	config_read_plugins();
	if (!no_global_config)
		config_read(SYSCONFDIR "/ekg2-override.conf");

/*	  userlist_read(); */
	emoticon_read();
	msg_queue_read();

#ifdef HAVE_NCURSES
	if (!have_plugin_of_class(PLUGIN_UI)) plugin_load(("ncurses"), -254, 1);
#endif
#ifdef HAVE_GTK
	if (!have_plugin_of_class(PLUGIN_UI)) plugin_load(("gtk"), -254, 1);
#endif
#ifdef HAVE_READLINE
	if (!have_plugin_of_class(PLUGIN_UI)) plugin_load(("readline"), -254, 1);
#endif
	if (!have_plugin_of_class(PLUGIN_UI)) {
		struct buffer *b;
		for (b = buffer_debug.data; b; b = b->next)
			fprintf(stderr, "%s\n", b->line);
		fprintf(stderr, "\n\nNo UI-PLUGIN!\n");
		return 1;
	} else {
		struct buffer *b;
		for (b = buffer_debug.data; b; b = b->next)
			print_window_w(window_debug, EKG_WINACT_NONE, b->target, b->line);
	}

	if (!have_plugin_of_class(PLUGIN_PROTOCOL)) {
#ifdef HAVE_EXPAT
		plugin_load(("jabber"), -254, 1);
#endif
#ifdef HAVE_LIBGADU
		plugin_load(("gg"), -254, 1);
#endif
		plugin_load(("irc"), -254, 1);
	}
	theme_plugins_init();

	scripts_init();
	/* If user does not have a config, don't bug about config upgrades. */
	if (config_read(NULL) == -1)
		config_version = -1;

	/* je�li ma by� theme, niech b�dzie theme */
	if (load_theme)		theme_read(load_theme, 1);
	else if (config_theme)	theme_read(config_theme, 1);

	in_autoexec = 0;

	/* XXX, unidle() was here */

	/* wypada�oby obserwowa� stderr */
	if (!batch_mode) {
#ifndef NO_POSIX_SYSTEM
		int fd[2];

		if (!pipe(fd)) {
			fcntl(fd[0], F_SETFL, O_NONBLOCK);
			fcntl(fd[1], F_SETFL, O_NONBLOCK);
			watch_add_line(NULL, fd[0], WATCH_READ_LINE, handle_stderr, NULL);
			stderr_backup = fcntl(2, F_DUPFD, 0);
			dup2(fd[1], 2);
		}
#endif
	}

	if (!batch_mode && config_display_welcome)
		print("welcome", VERSION);

	protocol_init();
	events_init();
	metacontact_init();
	audio_initialize();
/*	scripts_init();		*/

	/* it has to be done after plugins are loaded, either we wouldn't know if we are
	 * supporting some protocol in current build */
	if (session_read(NULL) == -1)
		no_config = 1;

	config_postread();

	/* status window takes first session if not set before*/
	if (!window_status->session && sessions)
		window_session_set(window_status, sessions);

	metacontact_read(); /* read the metacontacts info */

	{
		session_t *s;

		/* wylosuj opisy i zmie� stany klient�w */
		for (s = sessions; s; s = s->next) {
			const char *cmd = NULL;

			if (new_status)
				session_status_set(s, new_status);

			if (new_descr)
				session_descr_set(s, new_descr);

			cmd = ekg_status_string(s->status, 1);

			command_exec_format(NULL, s, 2, ("/%s %s"), cmd, (new_descr) ? new_descr : "");
		}

		/* po zainicjowaniu protoko��w, po��cz si� automagicznie ze
		 * wszystkim, co chce si� automagicznie ��czy�. */
		for (s = sessions; s; s = s->next) {
			if (auto_connect && session_int_get(s, "auto_connect") == 1)
				command_exec(NULL, s, ("/connect"), 0);
		}
	}

	if (config_auto_save)
		last_save = time(NULL);

	if (no_config) {
#ifdef HAVE_LIBGADU
		if (plugin_find("gg"))
			print("no_config");
		else
			print("no_config_gg_not_loaded");
#else
		print("no_config_no_libgadu");
#endif
	}

	timer_add(NULL, "autoaway", 1, 1, ekg_autoaway_timer, NULL);

	ekg2_reason_changed = 0;
	/* jesli jest emit: ui-loop (plugin-side) to dajemy mu kontrole, jesli nie 
	 * to wywolujemy normalnie sami ekg_loop() w petelce */
	if (query_emit_id(NULL, UI_LOOP) != -1) {
		/* kr�� imprez� */
		while (1) {
			ekg_loop();
		}
	}

	ekg_exit();

	return 0;
}

/*
 * ekg_exit()
 *
 * wychodzi z klienta sprz�taj�c przy okazji wszystkie sesje, zwalniaj�c
 * pami�� i czyszcz�c pok�j.
 */
void ekg_exit()
{
	char *exit_exec = config_exit_exec;
	extern int ekg2_dlclose(void *plugin);
	int i;

	msg_queue_write();

	xfree(last_search_first_name);
	xfree(last_search_last_name);
	xfree(last_search_nickname);
	xfree(last_search_uid);

	windows_save();

	/* setting windows layout */
	if (config_windows_save) {
		const char *vars[] = { "windows_layout", NULL };
		config_write_partly(NULL, vars);
	}

	/* setting default session */
	if (config_sessions_save && session_current) {
		const char *vars[] = { "session_default", NULL };
		xfree(config_session_default); config_session_default = xstrdup(session_current->uid);

		config_write_partly(NULL, vars);
	}

	for (i = 0; i < SEND_NICKS_MAX; i++) {
		xfree(send_nicks[i]);
		send_nicks[i] = NULL;
	}
	send_nicks_count = 0;

	{
		child_t *c;

		for (c = children; c; c = c->next) {
#ifndef NO_POSIX_SYSTEM
			kill(c->pid, SIGTERM);
#else
			/* TerminateProcess / TerminateThread */
#endif
		}
		children_destroy();
	}

	{
		list_t l;

		for (l = watches; l; l = l->next) {
			watch_t *w = l->data;

			watch_free(w);
		}
	}

	{
		plugin_t *p, *next;

		for (p = plugins; p; p = next) {
			next = p->next;

			if (p->pclass != PLUGIN_UI)
				continue;

			p->destroy();

//			if (p->dl) ekg2_dlclose(p->dl);
		}
	}
	list_destroy(watches, 0); watches = NULL;

	if (config_changed && !config_speech_app && config_save_quit == 1) {
		char line[80];

		printf("%s", format_find("config_changed"));
		fflush(stdout);
		if (fgets(line, sizeof(line), stdin)) {
			if (line[xstrlen(line) - 1] == '\n')
				line[xstrlen(line) - 1] = 0;
			if (!xstrcasecmp(line, "tak") || !xstrcasecmp(line, "yes") || !xstrcasecmp(line, "t") || !xstrcasecmp(line, "y")) {
				if (config_write(NULL) || session_write() || metacontact_write() || script_variables_write())
					printf(_("Error while saving.\n"));
			}
		} else
			printf("\n");
	} else if (config_save_quit == 2) {
		if (config_write(NULL) || session_write() || metacontact_write() || script_variables_write())
			printf(_("Error while saving.\n"));

	} else if (config_keep_reason && ekg2_reason_changed && config_save_quit == 1) {
		char line[80];

		printf("%s", format_find("quit_keep_reason"));
		fflush(stdout);
		if (fgets(line, sizeof(line), stdin)) {
			if (line[xstrlen(line) - 1] == '\n')
				line[xstrlen(line) - 1] = 0;
			if (!xstrcasecmp(line, "tak") || !xstrcasecmp(line, "yes") || !xstrcasecmp(line, "t") || !xstrcasecmp(line, "y")) {
				if (session_write())
					printf(_("Error while saving.\n"));
			}
		} else
			printf("\n");

	} else if (config_keep_reason && ekg2_reason_changed && config_save_quit == 2) {
		if (session_write())
			printf(_("Error while saving.\n"));
	}
	config_exit_exec = NULL; /* avoid freeing it */

/* XXX, think about sequence of unloading. */

	msgs_queue_destroy();
	conferences_destroy();
	newconferences_destroy();
	metacontacts_destroy();
	sessions_free();

	{
		plugin_t *p;

		for (p = plugins; p; ) {
			plugin_t *next = p->next;

			p->destroy();

//			if (p->dl) ekg2_dlclose(p->dl);

			p = next;
		}
	}

	audio_deinitialize();
	aliases_destroy();
	theme_free();
	variables_destroy();
	script_variables_free(1);
	emoticons_destroy();
	commands_destroy();
	timers_destroy();
	binding_free();
	lasts_destroy();

	buffer_free(&buffer_debug);	buffer_free(&buffer_speech);
	event_free();

	/* free internal read_file() buffer */
	read_file(NULL, -1);
	read_file_iso(NULL, -1);

/* windows: */
	windows_destroy();
	window_status = NULL; window_debug = NULL; window_current = NULL;	/* just in case */

/* queries: */
	{
		query_t **ll;

		for (ll = queries; ll <= &queries[QUERY_EXTERNAL]; ll++) {
			query_t *q;

			for (q = *ll; q; ) {	/* free other queries... connected by protocol_init() for example */
				query_t *next = q->next;

				query_free(q);

				q = next;
			}

			LIST_DESTROY2(*ll, NULL); /* XXX: really needed? */
		}
	}
	query_external_free();

	xfree(home_dir);

	xfree(config_dir);
	xfree(console_charset);

	mesg_set(mesg_startup);
#ifdef NO_POSIX_SYSTEM
	WSACleanup();
#endif
	close(stderr_backup);

	if (exit_exec) {
#ifndef NO_POSIX_SYSTEM
		execl("/bin/sh", "sh", "-c", exit_exec, NULL);
#else
		/* XXX, like in cmd_exec() */
#endif
		/* should we return some error code if exec() failed?
		 * AKA this line shouldn't be ever reached */
	}

	exit(0);
}

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
