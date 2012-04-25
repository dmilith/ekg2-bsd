/* $Id$ */

/*
 *  (C) Copyright 2001-2003 Piotr Domagalski <szalik@szalik.net>
 *			    Pawe� Maziarz <drg@infomex.pl>
 *			    Wojtek Kaniewski <wojtekka@irc.pl>
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
#include <ekg/win32.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#ifndef NO_POSIX_SYSTEM
#include <pwd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef HAVE_UTIMES
#  include <utime.h>
#endif

#ifdef HAVE_INOTIFY
#include <termios.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#endif /*HAVE_INOTIFY*/

#include <ekg/debug.h>
#include <ekg/dynstuff.h>
#include <ekg/stuff.h>
#include <ekg/themes.h>
#include <ekg/xmalloc.h>
#include <ekg/vars.h>

#include <ekg/queries.h>

struct mail_folder {
	int fhash;
	char *fname;
	time_t mtime;
	off_t size;
	int count;
	int check;

#ifdef HAVE_INOTIFY
	uint32_t watch;
#endif
};
int config_beep_mail = 1;

static list_t mail_folders = NULL;

static int config_check_mail = 0;
#ifndef HAVE_INOTIFY
static int config_check_mail_frequency = 15;
#endif
static char *config_check_mail_folders = NULL;

static int mail_count = 0;
static int last_mail_count = 0;

#ifndef HAVE_INOTIFY
static TIMER(check_mail);
#endif
static int check_mail_mbox();
static int check_mail_maildir();
static int check_mail_update(const char *, int);
static void check_mail_free();

static int mail_theme_init();

#ifdef HAVE_INOTIFY
static int inotify_fd;
static struct inotify_event *ev = NULL;
#endif

PLUGIN_DEFINE(mail, PLUGIN_GENERIC, mail_theme_init);

#ifdef EKG2_WIN32_SHARED_LIB
	EKG2_WIN32_SHARED_LIB_HELPER
#endif

#ifdef HAVE_INOTIFY
static WATCHER(mail_inotify) {
	int n;
	struct inotify_event *evp;

	if (type)
		return -1;

	ioctl(fd, FIONREAD, &n);
	if (n == 0)
		return 0;
	ev = xrealloc(ev, n);
	n = read(fd, ev, n);

	if (n < 0) {
		const int err = errno;
		debug_error("[mail] inotify read() failed, errno = %d\n", err);
		return -1;
	}

	for (evp = ev; n > 0; n -= (evp->len + sizeof(struct inotify_event)),
			evp = (void*) evp + (evp->len + sizeof(struct inotify_event))) {
		list_t l;
		struct mail_folder *m = NULL;

		for (l = mail_folders; l; l = l->next) {
			m = l->data;

			if (m->watch == evp->wd)
				break;
		}

		if (!l || !m || (evp->mask & (IN_IGNORED|IN_UNMOUNT)))
			return 0;
		/* XXX: handle IN_Q_OVERFLOW */

		/* XXX: check only correct mbox/mdir */
		if (config_check_mail & 1)
			check_mail_mbox();
		else
			if (config_check_mail & 2)
				check_mail_maildir();
		return 0; /* we already checked all of them, so just ignore rest */
	}
	return 0; /* XXX: revise above for */
}

#else /* HAVE_INOTIFY */

/*
 * check_mail()
 *
 * wywo�uje odpowiednie sprawdzanie poczty.
 */
static TIMER(check_mail)
{
	if (type)
		return 0;
	if (!config_check_mail)
		return -1;

	if (config_check_mail & 1)
		check_mail_mbox();
	else
		if (config_check_mail & 2)
			check_mail_maildir();
	return 0;
}
#endif

/*
 * check_mail_update()
 *
 * modyfikuje liczb� nowych emaili i daje o tym zna�.
 *
 * 0/-1 
 */
static int check_mail_update(const char *s, int more)
{
	int h = 0, c = 0, new_count = 0;
	list_t l;

	if (!s || !xstrchr(s, ','))
		return -1;

	h = atoi(s);
	c = atoi(xstrchr(s, ',') + 1);

	for (l = mail_folders; l; l = l->next) {
		struct mail_folder *m = l->data;

		if (m->fhash == h)
			m->count = c;

		new_count += m->count;
	}

	if (new_count == mail_count)
		return 0;

	if (!more) {
		last_mail_count = mail_count;
		mail_count = new_count;
	}

	if (!more && mail_count && mail_count > last_mail_count) {
		if (config_check_mail & 4) {
			if (mail_count == 1)
				print("new_mail_one");
			else {
				if (mail_count >= 2 && mail_count <= 4)
					print("new_mail_two_four", itoa(mail_count));
				else
					print("new_mail_more", itoa(mail_count));
			}
		}

		if (config_beep && config_beep_mail)
			query_emit_id(NULL, UI_BEEP, NULL);

		play_sound(config_sound_mail_file);

//		event_check(EVENT_NEWMAIL, 1, itoa(mail_count));
	}

	return 0;
}

static WATCHER_LINE(mail_handler)
{
	switch (type) {
		case 0:
			check_mail_update(watch, 1);
			break;
		case 1:
			check_mail_update("0,0", 0);	/* XXX paskuuudne! */
			close(fd);
			break;
	}
	return 0;
}

/*
 * check_mail_mbox()
 *
 * tworzy dzieciaka, kt�ry sprawdza wszystkie pliki typu
 * mbox i liczy, ile jest nowych wiadomo�ci, potem zwraca
 * wynik rurk�. sprawdza tylko te pliki, kt�re by�y
 * modyfikowane od czasu ostatniego sprawdzania.
 *
 * 0/-1
 */
static int check_mail_mbox()
{
#ifndef NO_POSIX_SYSTEM
	int fd[2], pid, to_check = 0;
	list_t l;

	for (l = mail_folders; l; l = l->next) {
		struct mail_folder *m = l->data;
		struct stat st;

		/* plik m�g� zosta� usuni�ty, uaktualnijmy */
		if (stat(m->fname, &st)) {
			if (m->count) {
				char *buf = saprintf("%d,%d", m->fhash, 0);
				check_mail_update(buf, 0);
				xfree(buf);
			}	

			m->mtime = 0;
			m->size = 0;  
			m->check = 0;  
			m->count = 0;

			continue;
		}

		if ((st.st_mtime != m->mtime) || (st.st_size != m->size)) {
			m->mtime = st.st_mtime;
			m->size = st.st_size;
			m->check = 1;
			to_check++;
		} else
			m->check = 0;
	}

	if (!to_check || pipe(fd))
		return -1;

	if ((pid = fork()) < 0) {
		close(fd[0]);
		close(fd[1]);
		return -1;
	}

	if (!pid) {	/* born to be wild */
		char *s = NULL, *line;
		int f_new = 0, new = 0, in_header = 0, i = 0;
		FILE *f;
		struct stat st;

		close(fd[0]);

		for (l = mail_folders; l; l = l->next) {
			struct mail_folder *m = l->data;

			if (!m->check)
				continue;

			i++;

			if (stat(m->fname, &st) || !(f = fopen(m->fname, "r")))
				continue;

			while ((line = read_file(f, 0))) {
				if (!strncmp(line, "From ", 5)) {
					in_header = 1;
					f_new++;
				}

				if (in_header && (!strncmp(line, "Status: RO", 10) || !strncmp(line, "Status: O", 9))) 
					f_new--;	

				line = strip_spaces(line);

				if (xstrlen(line) == 0)
					in_header = 0;
			}

			fclose(f);

#ifdef HAVE_UTIMES
			{
				struct timeval foo[2];

				foo[0].tv_sec = st.st_atime;
				foo[1].tv_sec = st.st_mtime;

				utimes(m->fname, foo);
			}

#else
			{
				struct utimbuf foo;

				foo.actime = st.st_atime;
				foo.modtime = st.st_mtime;

				utime(m->fname, &foo);
			}
#endif

			s = saprintf("%d,%d\n", m->fhash, f_new);

			{
				int sent = 0, left = xstrlen(s);
				char *ptr = s;

				while (left > 0) {
					sent = write(fd[1], ptr, sizeof(ptr));
	
					if (sent == -1)
						break;

					left -= sent;
					ptr += sent;
				}
			}

			xfree(s);

			new += f_new;
			f_new = 0;
		}

		close(fd[1]);
		exit(0);
	}

	close(fd[1]);
	fcntl(fd[0], F_SETFL, O_NONBLOCK);

	watch_add_line(&mail_plugin, fd[0], WATCH_READ_LINE, mail_handler, NULL);
	/* XXX czy tutaj potrzebny jest timeout? */
	return 0;
#else
	return -1;
#endif
}

/*
 * check_mail_maildir()
 *
 * tworzy dzieciaka, kt�ry sprawdza wszystkie 
 * katalogi typu Maildir i liczy, ile jest w nich
 * nowych wiadomo�ci. zwraca wynik rurk�.
 *
 * 0/-1
 */
static int check_mail_maildir()
{
#ifndef NO_POSIX_SYSTEM
	int fd[2], pid;

	if (pipe(fd))
		return -1;

	if ((pid = fork()) < 0) {
		close(fd[0]);
		close(fd[1]);
		return -1;
	}

	if (!pid) {	/* born to be wild */
		int d_new = 0, new = 0;
		char *s = NULL;
		struct dirent *d;
		DIR *dir;
		list_t l;

		close(fd[0]);

		for (l = mail_folders; l; l = l->next) {
			struct mail_folder *m = l->data;
			char *tmp = saprintf("%s/new", m->fname);

			if (!(dir = opendir(tmp))) {
				xfree(tmp);
				continue;
			}

			while ((d = readdir(dir))) {
				char *fname = saprintf("%s/%s", tmp, d->d_name);
				struct stat st;

				if (d->d_name[0] != '.' && !stat(fname, &st) && S_ISREG(st.st_mode))
					d_new++;

				xfree(fname);
			}
	
			xfree(tmp);
			closedir(dir);

			if (!l->next)
				s = saprintf("%d,%d", m->fhash, d_new);
			else
				s = saprintf("%d,%d\n", m->fhash, d_new);

			{
				int sent = 0, left = xstrlen(s);
				char *ptr = s;

				while (left > 0) {
					sent = write(fd[1], ptr, sizeof(ptr));

					if (sent == -1)
						break;

					left -= sent;
					ptr += sent;
				}
			}

			xfree(s);

			new += d_new;
			d_new = 0;
		}

		close(fd[1]);
		exit(0);
	}

	close(fd[1]);
	fcntl(fd[0], F_SETFL, O_NONBLOCK);

	watch_add_line(&mail_plugin, fd[0], WATCH_READ_LINE, mail_handler, NULL);
	/* XXX timeout */

	return 0;
#else
	return -1;
#endif
}

/*
 * changed_check_mail_folders()
 *
 * wywo�ywane przy zmianie ,,check_mail_folders''.
 */
static void changed_check_mail_folders(const char *var)
{
	struct mail_folder foo;

	check_mail_free();
	memset(&foo, 0, sizeof(foo));

	if (config_check_mail_folders) {
		char **f = NULL;
		int i;
		
		f = array_make(config_check_mail_folders, ", ", 0, 1, 1);

		for (i = 0; f[i]; i++) {
			if (f[i][0] != '/') {
				char *buf = saprintf("%s/%s", home_dir, f[i]);
				xfree(f[i]);
				f[i] = buf;
			}

			foo.fhash = ekg_hash(f[i]);
			foo.fname = f[i];
			foo.check = 1;
#ifdef HAVE_INOTIFY
			if (((foo.watch = inotify_add_watch(inotify_fd, foo.fname, IN_CLOSE_WRITE))) == -1) {
				debug_error("[mail] unable to set inotify watch for %s\n", foo.fname);
				xfree(foo.fname);
			} else
#endif

			list_add(&mail_folders, xmemdup(&foo, sizeof(foo)));
		}

		xfree(f);
	}

#ifndef NO_POSIX_SYSTEM
	if (config_check_mail & 1) {
		char *inbox = xstrdup(getenv("MAIL"));
		if (!inbox) {
			struct passwd *pw = getpwuid(getuid());

			if (!pw)
				return;

			inbox = saprintf("/var/mail/%s", pw->pw_name);
		}

		foo.fhash = ekg_hash(inbox);
		foo.fname = inbox;
		foo.check = 1;

#ifdef HAVE_INOTIFY
		if (((foo.watch = inotify_add_watch(inotify_fd, foo.fname, IN_CLOSE_WRITE))) == -1) {
			debug_error("[mail] unable to set inotify watch for %s\n", foo.fname);
			xfree(foo.fname);
		} else
#endif

		list_add(&mail_folders, xmemdup(&foo, sizeof(foo)));
	} else {
		if (config_check_mail & 2) {
			char *inbox = saprintf("%s/Maildir", home_dir);
			
			foo.fhash = ekg_hash(inbox);
			foo.fname = inbox;
			foo.check = 1;

#ifdef HAVE_INOTIFY
			if (((foo.watch = inotify_add_watch(inotify_fd, foo.fname, IN_CLOSE_WRITE))) == -1) {
				debug_error("[mail] unable to set inotify watch for %s\n", foo.fname);
				xfree(foo.fname);
			} else
#endif

			list_add(&mail_folders, xmemdup(&foo, sizeof(foo)));
		}
	}
#endif
}

/*
 * changed_check_mail()
 *
 * wywo�ywane przy zmianie zmiennej ,,check_mail''.
 */
static void changed_check_mail(const char *var)
{
#ifndef HAVE_INOTIFY
	if (config_check_mail) {
		struct timer *t;

		/* konieczne, je�li by�a zmiana typu skrzynek */
		changed_check_mail_folders(("check_mail_folders"));

		timer_remove(&mail_plugin, "mail-check");

		if (config_check_mail_frequency)
			timer_add(&mail_plugin, "mail-check", config_check_mail_frequency, 1, check_mail, NULL);

		return;
	}

	timer_remove(&mail_plugin, "mail-check");
#else
	changed_check_mail_folders(("check_mail_folders"));
#endif
}

static int dd_beep(const char *name)
{
	return (config_beep);
}

static int dd_check_mail(const char *name)
{
	return (config_check_mail);
}

static QUERY(mail_count_query)
{
	int *__count = va_arg(ap, int*);

	*__count = mail_count;

	return 0;
}

EXPORT int mail_plugin_init(int prio)
{
	PLUGIN_CHECK_VER("mail");

#ifdef HAVE_INOTIFY
	if ((inotify_fd = inotify_init()) == -1) {
		print("generic_error", "inotify init failed.");
		return -1;
	}
#endif
	plugin_register(&mail_plugin, prio);

	query_connect_id(&mail_plugin, MAIL_COUNT, mail_count_query, NULL);

	variable_add(&mail_plugin, ("beep_mail"), VAR_BOOL, 1, &config_beep_mail, NULL, NULL, dd_beep);
	variable_add(&mail_plugin, ("check_mail"), VAR_MAP, 1, &config_check_mail, changed_check_mail, variable_map(4, 0, 0, "no", 1, 2, "mbox", 2, 1, "maildir", 4, 0, "notify"), NULL);
#ifndef HAVE_INOTIFY
	variable_add(&mail_plugin, ("check_mail_frequency"), VAR_INT, 1, &config_check_mail_frequency, changed_check_mail, NULL, dd_check_mail);
#endif
	variable_add(&mail_plugin, ("check_mail_folders"), VAR_STR, 1, &config_check_mail_folders, changed_check_mail_folders, NULL, dd_check_mail);

#ifdef HAVE_INOTIFY
	watch_add(&mail_plugin, inotify_fd, WATCH_READ, mail_inotify, NULL);
#endif

	return 0;
}

static int mail_theme_init() {
#ifndef NO_DEFAULT_THEME
	format_add("new_mail_one", _("%) You got one email\n"), 1);
	format_add("new_mail_two_four", _("%) You got %1 new emails\n"), 1);
	format_add("new_mail_more", _("%) You got %1 new emails\n"), 1);
#endif
	return 0;
}

/*
 * check_mail_free()
 *
 * zwalnia pami�� po li�cie folder�w z poczt�.
 */
static void check_mail_free()
{
	list_t l;

	if (!mail_folders)
		return;

	for (l = mail_folders; l; l = l->next) {
		struct mail_folder *m = l->data;

		xfree(m->fname);
#ifdef HAVE_INOTIFY
		inotify_rm_watch(inotify_fd, m->watch);
#endif
	}

	list_destroy(mail_folders, 1);
	mail_folders = NULL;
}

static int mail_plugin_destroy()
{
	check_mail_free();

#ifdef HAVE_INOTIFY
	close(inotify_fd);
#endif

	plugin_unregister(&mail_plugin);

	return 0;
}

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
