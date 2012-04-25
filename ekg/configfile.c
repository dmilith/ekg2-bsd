/* $Id$ */

/*
 *  (C) Copyright 2001-2005 Wojtek Kaniewski <wojtekka@irc.pl>
 *			    Robert J. Wo�ny <speedy@ziew.org>
 *			    Pawe� Maziarz <drg@o2.pl>
 *			    Dawid Jarosz <dawjar@poczta.onet.pl>
 *			    Piotr Domagalski <szalik@szalik.net>
 *			    Piotr Kupisiewicz <deletek@ekg2.org>
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
#include <sys/stat.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "bindings.h"
#include "commands.h"
#include "debug.h"
#include "dynstuff.h"
#include "events.h"
#include "metacontacts.h"
#include "stuff.h"
#include "vars.h"
#include "xmalloc.h"
#include "plugins.h"
#include "windows.h"

#include "queries.h"

/* function inside legacyconfig.c */
void config_upgrade();

#define check_file() if (!(f = fopen(filename, "r")))\
		return -1;\
\
	if (stat(filename, &st) || !S_ISREG(st.st_mode)) {\
		if (S_ISDIR(st.st_mode))\
			errno = EISDIR;\
		else\
			errno = EINVAL;\
		fclose(f);\
		return -1;\
	}


static char *strip_quotes(char *line) {
	size_t linelen;
	char *buf;

	if (!(linelen = xstrlen(line))) return line;

	for (buf = line; *buf == '\"'; buf++);

	while (linelen > 0 && line[linelen - 1] == '\"') {
		line[linelen - 1] = 0;
		linelen--;
	}

	return buf;
}

/* 
 * config_postread()
 *
 * initialized after config is read 
 */
void config_postread()
{
	if (config_windows_save && config_windows_layout) {
		char **targets = array_make(config_windows_layout, "|", 0, 0, 0);
		int i;

		for (i = 1; targets[i]; i++) {
			char *tmp;

			if (!xstrcmp(targets[i], "\"-\""))
				continue;

			if (xstrcmp(targets[i], "") && (tmp = xstrrchr(targets[i], '/'))) {
				char *session_name = xstrndup(targets[i], xstrlen(targets[i]) - xstrlen(tmp));
				session_t *s;

				if (!(s = session_find(session_name))) {
					xfree(session_name);
					continue;
				}

				tmp++;
				tmp = strip_spaces(tmp);
				tmp = strip_quotes(tmp);

				window_new(tmp, s, i + 1);	
	
				xfree(session_name);
			} else {
				window_new(NULL, NULL, i + 1);
			}
		}

		array_free(targets);
	}

	if (config_session_default) {
		session_t *s = session_find(config_session_default);

		if (s) {
			debug("setted default session to %s\n", s->uid);
			window_session_set(window_status, s);
		} else {
			debug_warn("default session not found\n");
		}
	}
	config_upgrade();
	query_emit_id(NULL, CONFIG_POSTINIT);
}

int config_read_plugins()
{
	char*buf, *foo;
	const char *filename;
	FILE *f;
	struct stat st;


	if (!(filename = prepare_path("plugins", 0)))
			return -1;
	
	check_file();

	while ((buf = read_file(f, 0))) {
		if (!(foo = xstrchr(buf, (' '))))
			continue;

		*foo++ = 0;

		if (!xstrcasecmp(buf, ("plugin"))) {
			char **p = array_make(foo, (" \t"), 3, 1, 0);

			if (array_count(p) == 2)
				plugin_load(p[0], atoi(p[1]), 1);

			array_free(p);
		}
	}
	fclose(f);

	return 0;
}

/*
 * config_read()
 *
 * czyta z pliku ~/.ekg2/config lub podanego konfiguracj�.
 *
 *  - filename,
 *
 * 0/-1
 */
int config_read(const char *filename)
{
	char *buf, *foo;
	FILE *f;
	int err_count = 0, first = (filename) ? 0 : 1, ret;
	struct stat st;

	if (!in_autoexec && !filename) {
		aliases_destroy();
		timer_remove_user(-1);
		event_free();
		variable_set_default();
		query_emit_id(NULL, SET_VARS_DEFAULT);
		query_emit_id(NULL, BINDING_DEFAULT);
		debug("  flushed previous config\n");
	} 

	/* then global and plugins variables */
	if (!filename && !(filename = prepare_path("config", 0)))
		return -1;

	check_file();

	while ((buf = read_file(f, 0))) {
		ret = 0;

		if (buf[0] == '#' || buf[0] == ';' || (buf[0] == '/' && buf[1] == '/'))
			continue;

		if (!(foo = xstrchr(buf, ' ')))
			continue;

		*foo++ = 0;
		if (!xstrcasecmp(buf, ("set"))) {
			char *bar;

			if (!(bar = xstrchr(foo, ' ')))
				ret = variable_set(foo, NULL) < 0;
			else {
				*bar++ = 0;
				ret = variable_set(foo, bar) < 0;
			}

			if (ret)
				debug_error("  unknown variable %s\n", foo);

		} else if (!xstrcasecmp(buf, ("plugin"))) {
			char **p = array_make(foo, (" \t"), 3, 1, 0);
			if (array_count(p) == 2) 
				plugin_load(p[0], atoi(p[1]), 1);
			array_free(p);
		} else if (!xstrcasecmp(buf, ("bind"))) {
			char **pms = array_make(foo, (" \t"), 2, 1, 0);

			if (array_count(pms) == 2) {
				ret = command_exec_format(NULL, NULL, 1, ("/bind --add %s %s"),  pms[0], pms[1]);
			}

			array_free(pms);
		} else if (!xstrcasecmp(buf, ("bind-set"))) {
			char **pms = array_make(foo, (" \t"), 2, 1, 0);

			if (array_count(pms) == 2) {
				query_emit_id(NULL, BINDING_SET, pms[0], pms[1], 1);
			}

			array_free(pms);
		} else if (!xstrcasecmp(buf, ("alias"))) {
			debug("  alias %s\n", foo);
			ret = alias_add(foo, 1, 1);
		} else if (!xstrcasecmp(buf, ("on"))) {
			char **pms = array_make(foo, (" \t"), 4, 1, 0);

			if (array_count(pms) == 4) {
				debug("  on %s %s %s\n", pms[0], pms[1], pms[2]);
				ret = event_add(pms[0], atoi(pms[1]), pms[2], pms[3], 1);
			}

			array_free(pms);

		} else if (!xstrcasecmp(buf, ("bind"))) {
			continue;
		} else if (!xstrcasecmp(buf, ("at"))) {
			char **p = array_make(foo, (" \t"), 2, 1, 0);

			if (array_count(p) == 2) {
				char *name = NULL;

				debug("  at %s %s\n", p[0], p[1]);

				if (xstrcmp(p[0], ("(null)")))
					name = p[0];

				ret = command_exec_format(NULL, NULL, 1, ("/at -a %s %s"), ((name) ? name : ("")), p[1]);
			}

			array_free(p);
		} else if (!xstrcasecmp(buf, ("timer"))) {
			char **p = array_make(foo, (" \t"), 3, 1, 0);
			char *period_str = NULL;
			char *name = NULL;
			time_t period;

			if (array_count(p) == 3) {
				debug("  timer %s %s %s\n", p[0], p[1], p[2]);

				if (xstrcmp(p[0], ("(null)")))
					name = p[0];

				if (!xstrncmp(p[1], ("*/"), 2)) {
					period = atoi(p[1] + 2);
					period_str = saprintf("*/%ld", (long) period);
				} else {
					period = atoi(p[1]) - time(NULL);
					period_str = saprintf("%ld", (long) period);
				}
		
				if (period > 0) {
					ret = command_exec_format(NULL, NULL, 1, 
						("/timer --add %s %s %s"), (name) ? name : "", period_str, p[2]);
				}

				xfree(period_str);
			}
			array_free(p);
		} else {
			ret = variable_set(buf, (xstrcmp(foo, (""))) ? foo : NULL) < 0;

			if (ret)
				debug_error("  unknown variable %s\n", buf);
		}

		if (ret && (err_count++ > 100))
			break;
	}
	
	fclose(f);

	if (first) {
		plugin_t *p;

		for (p = plugins; p; p = p->next) {
			const char *tmp;
			
			if ((tmp = prepare_pathf("config-%s", p->name)))
				config_read(tmp);
		}
	}
	
	return 0;
}

/*
 * config_write_variable()
 *
 * zapisuje jedn� zmienn� do pliku konfiguracyjnego.
 *
 *  - f - otwarty plik konfiguracji,
 *  - v - wpis zmiennej,
 */
static void config_write_variable(FILE *f, variable_t *v)
{
	if (!f || !v)
		return;

	switch (v->type) {
		case VAR_DIR:
		case VAR_THEME:
		case VAR_FILE:
		case VAR_STR:
			fprintf(f, "%s %s\n", v->name, (*(char**)(v->ptr)) ? *(char**)(v->ptr) : "");
			break;
		default:
			fprintf(f, "%s %d\n", v->name, *(int*)(v->ptr));
	}
}

/*
 * config_write_plugins()
 *
 * function saving plugins 
 *
 * - f - file, that we are saving to
 */
static void config_write_plugins(FILE *f)
{
	plugin_t *p;

	if (!f)
		return;

	for (p = plugins; p; p = p->next) {
		if (p->name) fprintf(f, "plugin %s %d\n", p->name, p->prio);
	}
}

/*
 * config_write_main()
 *
 * w�a�ciwa funkcja zapisuj�ca konfiguracj� do podanego pliku.
 *
 *  - f - plik, do kt�rego piszemy
 */
static void config_write_main(FILE *f)
{
	if (!f)
		return;

	{
		variable_t *v;

		for (v = variables; v; v = v->next) {
			if (!v->plugin)
				config_write_variable(f, v);
		}
	}

	{
		alias_t *a;

		for (a = aliases; a; a = a->next) {
			list_t m;

			for (m = a->commands; m; m = m->next)
				fprintf(f, "alias %s %s\n", a->name, (char *) m->data);
		}
	}

	{
		event_t *e;

		for (e = events; e; e = e->next) {
			fprintf(f, "on %s %d %s %s\n", e->name, e->prio, e->target, e->action);
		}
	}

	{
		struct binding *b;

		for (b = bindings; b; b = b->next) {
			if (b->internal)
				continue;

			fprintf(f, "bind %s %s\n", b->key, b->action);
		}
	}

	{
		binding_added_t *d;

		for (d = bindings_added; d; d = d->next) {
			fprintf(f, "bind-set %s %s\n", d->binding->key, d->sequence);
		}
	}

	{
		struct timer *t;

		for (t = timers; t; t = t->next) {
			const char *name = NULL;

			if (t->function != timer_handle_command)
				continue;

			/* nie ma sensu zapisywa� */
			if (!t->persist && t->ends.tv_sec - time(NULL) < 5)
				continue;

			/* posortuje, je�li nie ma nazwy */
			if (t->name && !xisdigit(t->name[0]))
				name = t->name;
			else
				name = "(null)";

			if (t->at) {
				char buf[100];
				time_t foo = (time_t) t->ends.tv_sec;
				struct tm *tt = localtime(&foo);

				strftime(buf, sizeof(buf), "%G%m%d%H%M.%S", tt);

				if (t->persist)
					fprintf(f, "at %s %s/%s %s\n", name, buf, itoa(t->period / 1000), (char*)(t->data));
				else
					fprintf(f, "at %s %s %s\n", name, buf, (char*)(t->data));
			} else {
				char *foo;

				if (t->persist)
					foo = saprintf("*/%s", itoa(t->period / 1000));
				else
					foo = saprintf("%s", itoa(t->ends.tv_sec));

				fprintf(f, "timer %s %s %s\n", name, foo, (char*)(t->data));

				xfree(foo);
			}
		}
	}
}

/*
 * config_write()
 *
 * zapisuje aktualn� konfiguracj� do pliku ~/.ekg2/config lub podanego.
 *
 * 0/-1
 */
int config_write()
{
	FILE *f;
	plugin_t *p;

	if (!prepare_path(NULL, 1))	/* try to create ~/.ekg2 dir */
		return -1;

	/* first of all we are saving plugins */
	if (!(f = fopen(prepare_path("plugins", 0), "w")))
		return -1;
	
	fchmod(fileno(f), 0600);
	fprintf(f, "# vim:fenc=%s\n", config_console_charset);

	config_write_plugins(f);
	fclose(f);

	/* now we are saving global variables and settings
	 * timers, bindings etc. */

	if (!(f = fopen(prepare_path("config", 0), "w")))
		return -1;

	fchmod(fileno(f), 0600);
	fprintf(f, "# vim:fenc=%s\n", config_console_charset);

	config_write_main(f);
	fclose(f);

	/* now plugins variables */
	for (p = plugins; p; p = p->next) {
		const char *tmp;
		variable_t *v;

		if (!(tmp = prepare_pathf("config-%s", p->name)))
			return -1;

		if (!(f = fopen(tmp, "w")))
			return -1;

		fchmod(fileno(f), 0600);
		fprintf(f, "# vim:fenc=%s\n", config_console_charset);

		for (v = variables; v; v = v->next) {
			if (p == v->plugin) {
				config_write_variable(f, v);
			}
		}	

		fclose(f);
	}

	return 0;
}

/*
 * config_write_partly()
 *
 * zapisuje podane zmienne, nie zmieniaj�c reszty konfiguracji.
 *  
 *  - plugin - zmienne w vars, maja byc z tego pluginu, lub NULL gdy to sa zmienne z core.
 *  - vars - tablica z nazwami zmiennych do zapisania.
 * 
 * 0/-1
 */
/* BIG BUGNOTE:
 *	Ta funkcja jest zle zportowana z ekg1, zle napisana, wolna, etc..
 *	Powinnismy robic tak:
 *		- dla kazdej zmiennej w vars[] znalezc variable_t * jej odpowiadajace i do tablicy vars_ptr[]
 *		- dla kazdej zmiennej w vars[] policzyc dlugosc i do vars_len[]
 *	- nastepnie otworzyc "config-%s", vars_ptr[0]->plugin->name (lub "config" gdy nie plugin)
 *		- zrobic to co tutaj robimy, czyli poszukac tej zmiennej.. oraz nastepnie wszystkie inne ktore maja taki
 *			sam vars_ptr[]->plugin jak vars_ptr[0]->plugin, powtarzac dopoki sie skoncza takie.
 *	- nastepnie wziasc zmienna ktora ma inny plugin.. i j/w
 */
int config_write_partly(plugin_t *plugin, const char **vars)
{
	const char *filename;
	char *newfn;
	char *line;
	FILE *fi, *fo;
	int *wrote, i;

	if (!vars)
		return -1;

	if (plugin)
		filename = prepare_pathf("config-%s", plugin->name);
	else	filename = prepare_pathf("config");

	if (!filename)
		return -1;
	
	if (!(fi = fopen(filename, "r")))
		return -1;

	newfn = saprintf("%s.%d.%ld", filename, (int) getpid(), (long) time(NULL));

	if (!(fo = fopen(newfn, "w"))) {
		xfree(newfn);
		fclose(fi);
		return -1;
	}
	
	wrote = xcalloc(array_count((char **) vars) + 1, sizeof(int));
	
	fchmod(fileno(fo), 0600);

	while ((line = read_file(fi, 0))) {
		char *tmp;

		if (line[0] == '#' || line[0] == ';' || (line[0] == '/' && line[1] == '/'))
			goto pass;

		if (!xstrchr(line, ' '))
			goto pass;

		if (!xstrncasecmp(line, ("alias "), 6))
			goto pass;

		if (!xstrncasecmp(line, ("on "), 3))
			goto pass;

		if (!xstrncasecmp(line, ("bind "), 5))
			goto pass;

		tmp = line;

		if (!xstrncasecmp(tmp, ("set "), 4))
			tmp += 4;
		
		for (i = 0; vars[i]; i++) {
			int len;

			if (wrote[i])
				continue;
			
			len = xstrlen(vars[i]);

			if (xstrlen(tmp) < len + 1)
				continue;

			if (xstrncasecmp(tmp, vars[i], len) || tmp[len] != ' ')
				continue;
			
			config_write_variable(fo, variable_find(vars[i]));

			wrote[i] = 1;
			
			line = NULL;
			break;
		}

pass:
		if (line)
			fprintf(fo, "%s\n", line);
	}

	for (i = 0; vars[i]; i++) {
		if (wrote[i])
			continue;

		config_write_variable(fo, variable_find(vars[i]));
	}

	xfree(wrote);
	
	fclose(fi);
	fclose(fo);
	
	rename(newfn, filename);

	xfree(newfn);
	return 0;
}

/*
 * config_write_crash()
 *
 * funkcja zapisuj�ca awaryjnie konfiguracj�. nie powinna alokowa� �adnej
 * pami�ci.
 */
void config_write_crash()
{
	char name[32];
	FILE *f;
	plugin_t *p;

	chdir(config_dir);

	/* first of all we are saving plugins */
	snprintf(name, sizeof(name), "plugins.%d", (int) getpid());

	if (!(f = fopen(name, "w")))
		return;

	fchmod(fileno(f), 0400);

	config_write_plugins(f);

	fflush(f);
	fclose(f);

	/* then main part of config */
	snprintf(name, sizeof(name), "config.%d", (int) getpid());
	if (!(f = fopen(name, "w")))
		return;

	chmod(name, 0400);
	
	config_write_main(f);

	fflush(f);
	fclose(f);

	/* now plugins variables */
	for (p = plugins; p; p = p->next) {
		variable_t *v;

		snprintf(name, sizeof(name), "config-%s.%d", p->name, (int) getpid());

		if (!(f = fopen(name, "w")))
			continue;	
	
		chmod(name, 0400);

		for (v = variables; v; v = v->next) {
			if (p == v->plugin) {
				config_write_variable(f, v);
			}
		}

		fflush(f);
		fclose(f);
	}
}

/*
 * debug_write_crash()
 *
 * zapisuje ostatnie linie z debug.
 */
void debug_write_crash()
{
	char name[32];
	FILE *f;
	struct buffer *b;

	chdir(config_dir);

	snprintf(name, sizeof(name), "debug.%d", (int) getpid());
	if (!(f = fopen(name, "w")))
		return;

	chmod(name, 0400);
	
	for (b = buffer_debug.data; b; b = b->next)
		fprintf(f, "%s\n", b->line);
	
	fclose(f);
}

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
