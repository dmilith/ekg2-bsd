/* $Id$ */

/*
 *  (C) Copyright 2001-2005 Wojtek Kaniewski <wojtekka@irc.pl>
 *			    Robert J. Wo�ny <speedy@ziew.org>
 *			    Pawe� Maziarz <drg@infomex.pl>
 *			    Wojciech Bojdo� <wojboj@htc.net.pl>
 *			    Piotr Wysocki <wysek@linux.bydg.org>
 *			    Dawid Jarosz <dawjar@poczta.onet.pl>
 *			    Piotr Domagalski <szalik@szalik.net>
 *			    Kuba Kowalski <qbq@kofeina.net>
 *			    Piotr Kupisiewicz <deli@rzepaknet.us>
 *			    Leszek Krupi�ski <leafnode@wafel.com>
 *			    Adam Mikuta <adamm@ekg2.org>
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

#define _BSD_SOURCE

#include <sys/types.h>

#ifndef NO_POSIX_SYSTEM
#include <sys/ioctl.h>
#include <sys/socket.h>
#endif

#include <sys/stat.h>
#include <sys/time.h>

#ifndef NO_POSIX_SYSTEM
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef NO_POSIX_SYSTEM
#include <sys/utsname.h>
#endif

#ifdef __sun
#include <procfs.h>
#endif

#ifdef __FreeBSD__
# include <kvm.h>		/* kvm_ funcs */
# include <limits.h>		/* _POSIX2_LINE_MAX */
# include <sys/param.h>
# include <sys/sysctl.h>	/* KERN_PROC_PID */
# include <sys/user.h>
#endif

#include "bindings.h"
#include "commands.h"
#include "debug.h"
#include "events.h"
#include "configfile.h"
#include "dynstuff.h"
#include "log.h"
#include "metacontacts.h"
#include "msgqueue.h"
#include "protocol.h"
#include "sessions.h"
#ifndef HAVE_STRLCAT
#  include "compat/strlcat.h"
#endif
#ifndef HAVE_STRLCPY
#  include "compat/strlcpy.h"
#endif
#include "stuff.h"
#include "themes.h"
#include "vars.h"
#include "userlist.h"
#include "scripts.h"
#include "windows.h"
#include "xmalloc.h"
#include "net.h"
#include "queries.h"
#include "dynstuff_inline.h"

char *send_nicks[SEND_NICKS_MAX] = { NULL };
int send_nicks_count = 0, send_nicks_index = 0;
static int quit_command = 0;

command_t *commands = NULL;

static LIST_ADD_COMPARE(command_add_compare, command_t *) { return xstrcasecmp(data1->name, data2->name); }
static LIST_FREE_ITEM(list_command_free, command_t *) { array_free(data->params); array_free(data->possibilities); }

DYNSTUFF_LIST_DECLARE2_SORTED(commands, command_t, command_add_compare, list_command_free,
	static __DYNSTUFF_LIST_ADD_SORTED,	/* commands_add() */
	__DYNSTUFF_LIST_REMOVE_SAFE,		/* commands_remove() */
	__DYNSTUFF_LIST_REMOVE_ITER,		/* commands_removei() */
	__DYNSTUFF_LIST_DESTROY)		/* commands_destroy() */

/*
 * match_arg()
 *
 * sprawdza, czy dany argument funkcji pasuje do podanego.
 */
int match_arg(const char *arg, char shortopt, const char *longopt, int longoptlen)
{
	if (!arg || *arg != '-')
		return 0;

	arg++;
	if (*arg == '-') {
		int len = xstrlen(++arg);

		if (longoptlen > len)
			len = longoptlen;

		return !xstrncmp(arg, longopt, len);
	}
	return (*arg == shortopt) && (*(arg + 1) == 0);
}

/*
 * tabnick_add()
 *
 * dodaje do listy nick�w dope�nianych automagicznie tabem.
 */
void tabnick_add(const char *nick)
{
	int i;

	for (i = 0; i < send_nicks_count; i++)
		if (send_nicks[i] && !xstrcmp(nick, send_nicks[i])) {
			tabnick_remove(nick);
			break;
		}

	if (send_nicks_count == SEND_NICKS_MAX) {
		xfree(send_nicks[SEND_NICKS_MAX - 1]);
		send_nicks_count--;
	}

	memmove(&send_nicks[1], &send_nicks[0], send_nicks_count * sizeof(send_nicks[0]) );

	send_nicks_count++;
	
	send_nicks[0] = xstrdup(nick);
}

/*
 * tabnick_remove()
 *
 * usuwa z listy dope�nianych automagicznie tabem.
 */
void tabnick_remove(const char *nick)
{
	int i, j;

	for (i = 0; i < send_nicks_count; i++) {
		if (send_nicks[i] && !xstrcmp(send_nicks[i], nick)) {
			xfree(send_nicks[i]);

			for (j = i + 1; j < send_nicks_count; j++)
				send_nicks[j - 1] = send_nicks[j];

			send_nicks_count--;
			send_nicks[send_nicks_count] = NULL;

			break;
		}
	}
}

/*
 * tabnick_flush()
 *
 * czy�ci list� nick�w dope�nianych tabem.
 */
static void tabnick_flush()
{
	int i;

	for (i = 0; i < send_nicks_count; i++) {
		xfree(send_nicks[i]);
		send_nicks[i] = NULL;
	}

	send_nicks_count = 0;
	send_nicks_index = 0;
}


static COMMAND(cmd_tabclear)
{
	int i;

	if (!params[0]) {
		tabnick_flush();
		return 0;
	}

	if (match_arg(params[0], 'o', ("offline"), 2)) {
		for (i = 0; i < send_nicks_count; i++) {
			userlist_t *u = NULL;

			if (send_nicks[i])
				u = userlist_find(session, send_nicks[i]);

			/* I think we should also remove errors and likes here
			 * if I'm wrong, change the macro to comparison */
			if (!u || !EKG_STATUS_IS_NA(u->status))
				continue;

			tabnick_remove(send_nicks[i]);
		}

		return 0;
	}

	printq("invalid_params", name);

	return -1;
}

/* XXX, rewritten, need checking */
COMMAND(cmd_add) {
	int params_free = 0;	/* zmienili�my params[] i trzeba zwolni� */
	int result = 0;
	userlist_t *u = NULL;
	
	/* XXX, just in case? */
	if (!target)
		target = window_current->target;

	/* XXX, check this damn session_current. */
	if (!session)
		session = session_current;

	if (!session)
		return -1;
	
	/* If we didn't have params[1] and we params[0] isn't option (for example not --find) and we have target 
	 *	get uid from current window, get nickname from params[0]
	 *
	 *	Code for getting more options from params[1] && params[2] were senseless, cause we have !params[1] ;(
	 */
	if (params[0][0] != '-' && !params[1] && target) {
		const char *name = params[0];

		params_free = 1;
		params = xmalloc(3 * sizeof(char *));
		params[0] = target;
		params[1] = name;
/*		params[2] = NULL */
	}

	/* if we have passed -f [lastfound] then get uid, nickname and other stuff from searches... */
	/* if params[1] passed than it should be used as nickname */

	/* XXX, we need to make it session-visible only.. or at least protocol-visible only.
	 *	cause we maybe implement it in jabber */
	if (match_arg(params[0], 'f', ("find"), 2)) {
		const char *nickname;

		if (!last_search_uid || (!last_search_nickname && !params[1])) {
			printq("search_no_last");
			return -1;
		}

		nickname = last_search_nickname ? strip_spaces(last_search_nickname) : params[1];

		if (nickname && nickname[0] == '\0') 
			nickname = params[1];

		if (!nickname) {
			printq("search_no_last_nickname");
			return -1;
		}

		params_free = 1;

		params = xmalloc(4 * sizeof(char *));
		params[0] = last_search_uid;
		params[1] = nickname;
		/* construct params[2] -f FIRST_NAME -l LAST_NAME */
		params[2] = saprintf("-f \"%s\" -l \"%s\"", 
				((last_search_first_name) ? last_search_first_name : ("")), 
				((last_search_last_name) ? last_search_last_name : ("")));
/*		params[3] = NULL; */
	}

	if (!params[1]) {
		printq("not_enough_params", name);
		result = -1;
		goto cleanup;
	}


	if (!valid_plugin_uid(session->plugin, params[0])) {
		printq("invalid_uid");
		result = -1;
		goto cleanup;
	}

	if (!valid_nick(params[1])) {
		printq("invalid_nick");
		result = -1;
		goto cleanup;
	}

	/* XXX, parse params[2] */

	if (((u = userlist_find(session, params[0])) && u->nickname) || ((u = userlist_find(session, params[1])) && u->nickname)) {
		if (!xstrcasecmp(params[1], u->nickname) && !xstrcasecmp(params[0], u->uid))
			printq("user_exists", params[1], session_name(session));
		else
			printq("user_exists_other", params[1], format_user(session, u->uid), session_name(session));

		result = -1;
		goto cleanup;
	}

	/* kto� by� tylko ignorowany/blokowany, nadajmy mu nazw� */
	if (u) {
		xfree(u->nickname);
		u->nickname = xstrdup(params[1]);
	}

	if (u || userlist_add(session, params[0], params[1])) {
		char *uid = xstrdup(params[0]);

		query_emit_id(NULL, USERLIST_ADDED, &uid, &params[1], &quiet);
		query_emit_id(NULL, ADD_NOTIFY, &session->uid, &uid);
		xfree(uid);

		printq("user_added", params[1], session_name(session));

		tabnick_remove(params[0]);
		config_changed = 1;

	}

cleanup:
	if (params_free) {
		xfree((char*) params[2]);
		xfree(params);
	}

	return result;
}

static COMMAND(cmd_alias) {
	int append = match_arg(params[0], 'A', ("append"), 2);

	if (append || match_arg(params[0], 'a', ("add"), 2)) {
		if (!params[1] || !xstrchr(params[1], ' ')) {
			printq("not_enough_params", name);
			return -1;
		}

		if (alias_add(params[1], quiet, append))
			return -1;

		config_changed = 1;
		return 0;
	}

	if (match_arg(params[0], 'd', ("del"), 2)) {
		int ret;

		if (!params[1]) {
			printq("not_enough_params", name);
			return -1;
		}

		if (!xstrcmp(params[1], "*"))
			ret = alias_remove(NULL, quiet);
		else
			ret = alias_remove(params[1], quiet);

		if (ret)
			return -1;

		config_changed = 1;
		return 0;
	}
	
	if (!params[0] || match_arg(params[0], 'l', ("list"), 2) || params[0][0] != '-') {
		const char *aname;
		int count = 0;
		alias_t *a;

		if (match_arg(params[0], 'l', ("list"), 2))
			aname = params[1];
		else
			aname = params[0];	/* it can be NULL */

		for (a = aliases; a; a = a->next) {
			list_t m;
			int first = 1;
			char *tmp;
			
			if (aname && xstrcasecmp(aname, a->name))
				continue;

			tmp = xmalloc(xstrlen(a->name) + 1);
			memset(tmp, ' ', xstrlen(a->name));

			for (m = a->commands; m; m = m->next) {
				printq((first) ? "aliases_list" : "aliases_list_next", a->name, (char *) m->data, tmp);
				first = 0;
				count++;
			}

			xfree(tmp);
		}

		if (!count) {
			if (aname) {
				printq("aliases_noexist", aname);
				return -1;
			}

			printq("aliases_list_empty");
		}
		return 0;
	}

	printq("invalid_params", name);
	return -1;
}

static COMMAND(cmd_status) {
	struct tm *t;
	time_t n;
	int now_days;

	char buf1[100] = { '\0' };
	const char *format;
	session_t *s = NULL;

	printq("show_status_header");

	s = params[0] ? session_find(params[0]) : session;

	if (params[0] && !s) {
		printq("invalid_uid", params[0]);
		return -1;
	}

	if (config_profile)
		printq("show_status_profile", config_profile);

	n = time(NULL);
	t = localtime(&n);
	now_days = t->tm_yday;

	if (s) {
		query_emit_id(s->plugin, STATUS_SHOW, &s->uid);

		/* when we connected [s->connected != 0] to server or when we lost last connection [s->connected == 0] [time from s->last_conn] */
		if (s->last_conn) { 
			char buf[100] = { '\0' };

			t = localtime(&s->last_conn);
			format = format_find((t->tm_yday == now_days) ? "show_status_last_conn_event_today" : "show_status_last_conn_event");
			if (format_ok(format) && !strftime(buf, sizeof(buf), format, t))
				xstrcpy(buf, "TOOLONG");

			printq((s->connected) ? "show_status_connected_since" : "show_status_disconnected_since", buf);
		}
	}

	t = localtime(&ekg_started);
	format = format_find((t->tm_yday == now_days) ? "show_status_ekg_started_today" : "show_status_ekg_started");
	if (format_ok(format) && !strftime(buf1, sizeof(buf1), format, t))
		xstrcpy(buf1, "TOOLONG");

	printq("show_status_ekg_started_since", buf1);
	printq("show_status_footer");

	return 0;
}

static COMMAND(cmd_del)
{
	userlist_t *u;
	char *tmp;
	int del_all = ((params[0] && !xstrcmp(params[0], "*")) ? 1 : 0);

	if (!session)
		return -1;
	
	if (!params[0]) {
		printq("not_enough_params", name);
		return -1;
	}

	if (del_all) {
		userlist_t *ul;
		for (ul = session->userlist; ul; ) {
			userlist_t *u = ul;
			userlist_t *next = ul->next;
			char *p0;
			char *tmp;
	
			p0 = xstrdup(u->nickname);
			tmp = xstrdup(u->uid);
			query_emit_id(NULL, USERLIST_REMOVED, &p0, &tmp);
			xfree(tmp);
			xfree(p0);

			userlist_remove(session, u);
			ul = next;
		}

		printq("user_cleared_list", session_name(session));
		tabnick_flush();
		config_changed = 1;

		return 0;
	}

	if (!(u = userlist_find(session, params[0])) || !u->nickname) {
		printq("user_not_found", params[0]);
		return -1;
	}

	tmp = xstrdup(u->uid);

	printq("user_deleted", params[0], session_name(session));

	tabnick_remove(u->uid);
	tabnick_remove(u->nickname);

	config_changed = 1;

	userlist_remove(session, u);

	query_emit_id(NULL, USERLIST_REMOVED, &params[0], &tmp);
	query_emit_id(NULL, REMOVE_NOTIFY, &session->uid, &tmp);

	xfree(tmp);
	
	return 0;
}

/**
 * cmd_exec_info_t is internal structure, containing information about processes started using /exec.
 */
typedef struct {
	char *target;		/**< Target UID, if redirecting output to someone */
	char *session;		/**< Session UID */
	int quiet;		/**< Whether to be quiet (i.e. don't print info on exit) */
	string_t buf;		/**< Buffer */
	char *cmdexec;		/**< Command to execute with result */
} cmd_exec_info_t;

static WATCHER_LINE(cmd_exec_watch_handler)	/* sta�y */
{
	cmd_exec_info_t *i = data;
	int quiet = (i) ? i->quiet : 0;

	if (!i)
		return -1;

	if (type == 1) {
		if (i->buf) {
			command_exec_format(i->target, session_find(i->session), quiet, ("/ %s"), i->buf->str);
			string_free(i->buf, 1);
		}
		xfree(i->cmdexec);
		xfree(i->target);
		xfree(i->session);
		xfree(i);
		return 0;
	}

	if (i->cmdexec) {
		if (*watch) /* XXX: maybe strip only last \n ? */
			command_exec_format(i->target, session_find(i->session), quiet, ("/%s %s"), i->cmdexec, watch);
	} else if (i->buf) {
		string_append(i->buf, watch);
		string_append(i->buf, ("\r\n"));
	} else if (i->target)
		command_exec_format(i->target, session_find(i->session), quiet, ("/ %s"), watch);
	else if (*watch)
		printq("exec", watch);

	return 0;
}

static void cmd_exec_child_handler(child_t *c, int pid, const char *name, int status, void *priv)
{
	int quiet = (name && name[0] == '^');

	printq("process_exit", itoa(pid), name, itoa(status));
}

COMMAND(cmd_exec)
{
	pid_t pid;

	if (params[0]) {
		int fd[2] = { 0, 0 }, buf = 0, cmdx = 0, add_commandline = 0;
		const char *command = params[0], *__target = NULL, *cmdexec = NULL;
		char **args = NULL;
		cmd_exec_info_t *i;
		watch_t *w;

		if (params[0][0] == '-') {
			int big_match = 0;
			args = (char **) params;

			if (match_arg(args[0], 'M', ("MSG"), 2) || (buf = match_arg(args[0], 'B', ("BMSG"), 2)
						|| (cmdx = match_arg(args[0], 'C', ("CMD"), 2))))
				big_match = add_commandline = 1;

			if (big_match || match_arg(args[0], 'm', ("msg"), 2) || (buf = match_arg(args[0], 'b', ("bmsg"), 2))
						|| (cmdx = match_arg(args[0], 'c', ("cmd"), 2))) {
				const char *uid;

				if (!args[1] || !args[2]) {
					printq("not_enough_params", name);
					return -1;
				}

				if (cmdx) {
					cmdexec	= args[1];
					__target = target;
				} else if (!(uid = get_uid(session, args[1]))) {
					printq("user_not_found", args[1]);
					return -1;
				} else
					__target = uid;
				command = args[2];
			} else {
				printq("invalid_params", name);
				return -1;
			}
		} 

		if (pipe(fd) == -1) {
			printq("exec_error", strerror(errno));
			return -1;
		}

#ifndef NO_POSIX_SYSTEM
		if (!(pid = fork())) {
			dup2(open("/dev/null", O_RDONLY), 0);
			dup2(fd[1], 1);
			dup2(fd[1], 2);

			close(fd[0]);
			close(fd[1]);

			execl("/bin/sh", "sh", "-c", (command[0] == '^') ? command + 1 : command, (void *) NULL);

			exit(1);
		}
#else
/* XXX, fork and execute cmd /C $command
 * MG: I think using explicitly 'cmd /c' is very bad - in win9x we've got command.com instead;
 *     but AFAIR $COMSPEC should be always defined; if not, we should even use command.com
 *     (AFAIK newer versions of windows are backwards compatible with this) */
		pid = -1;
#endif

		if (pid < 0) {
			printq("exec_error", strerror(errno));
			close(fd[0]);
			close(fd[1]);
			return -1;
		}
	
		i = xmalloc(sizeof(cmd_exec_info_t));
		
		i->quiet = quiet;
		i->target = xstrdup(__target);
		i->cmdexec = xstrdup(cmdexec);
		i->session = xstrdup(session_uid_get(session));

		if (buf)
			i->buf = string_init(NULL);

		w = watch_add_line(NULL, fd[0], WATCH_READ_LINE, cmd_exec_watch_handler, i);

		if (add_commandline) {
			char *tmp = format_string(format_find("exec_prompt"), ((command[0] == '^') ? command + 1 : command));
			string_append(w->buf, tmp);
			xfree(tmp);
		}

		fcntl(fd[0], F_SETFL, O_NONBLOCK);

		close(fd[1]);
		child_add(NULL, pid, command, cmd_exec_child_handler, NULL);

	} else {
		child_t *c;

		for (c = children; c; c = c->next)
			printq("process", itoa(c->pid), ((c->name) ? (c->name) : ("?")));

		if (!children) {
			printq("no_processes");
			return -1;
		}
	}

	return 0;
}

/**
 * cmd_eval()
 *
 * Execute space seperated commands from @a params[0]<br>
 * If you want add params to command use " " sample: /eval "first_commamnd --first_param --second_param" second_command third_command
 *
 * Handler for: <i>/eval</i> command.
 *
 * @param params [0] - commands to execute
 *
 * @return 0
 */

static COMMAND(cmd_eval) {
	int i;
	char **argv;

	argv = array_make(params[0], (" "), 0, 1, 1);
	
	for (i = 0; argv[i]; i++)
		command_exec(NULL, session, argv[i], 0);

	array_free(argv);

	return 0;
}

static COMMAND(cmd_for)
{
	int for_all = 0;

	if (!xstrcmp(params[1], "*")) 
		for_all = 1; 

	if (match_arg(params[0], 's', ("sessions"), 2)) {
		char *param = (char *) params[2];
		int next_is_for = 0;

		if (param[0] == '/')
			param++;

		if (!xstrncasecmp(param, name, xstrlen(name)))
			next_is_for = 1;

		if (for_all) {
			session_t *s;
			
			if (!sessions) {
				printq("session_list_empty");
				return -2;
			}
				
			for (s = sessions; s; s = s->next) {
				char *for_command;
				
				if (!s || !s->uid)
					continue;
				
				if (!next_is_for)
					for_command = format_string(params[2], session_alias_uid(s), s->uid);
				else
					for_command = xstrdup(params[2]);

				command_exec(NULL, s, for_command, 0);
				xfree(for_command);
			}
		} else {
			char **tmp = array_make(params[1], ",", 0, 0, 0);
			int i;
			session_t **s;
	
			s = xmalloc(sizeof(session_t *) * array_count(tmp));
			
			/* first we are checking all of the parametrs */
			for (i = 0; tmp[i]; i++) {
				if (!(s[i] = session_find(tmp[i]))) {
					printq("session_doesnt_exist", tmp[i]);
					xfree(s);
					array_free(tmp);
					return -1;
				}
			}		
			
			for (i = 0; tmp[i]; i++) {
				char *for_command;
				
				if (!s[i] || !s[i]->uid)
					continue;
				
				if (!next_is_for)
					for_command = format_string(params[2], session_alias_uid(s[i]), s[i]->uid);
				else
					for_command = xstrdup(params[2]);

				command_exec(NULL, s[i], for_command, 0);
				xfree(for_command);
			}
			array_free(tmp);
			xfree(s);
		}
	} else if (match_arg(params[0], 'u', ("users"), 2)) {
		char *param = (char *) params[2];
		int next_is_for = 0;

		if (param[0] == '/')
			param++;

		if (!xstrncasecmp(param, name, xstrlen(name)))
			next_is_for = 1;
				
		if (!session) {
			return -1;
		}

		if (!session->userlist) {
			printq("list_empty");
			return -1;
		}

		if (for_all) {
			userlist_t *ul;

			for (ul = session->userlist; ul; ul = ul->next) {
				userlist_t *u = ul;
				char *for_command;

				if (!u || !u->uid)
					continue;
				
				if (!next_is_for)
					for_command = format_string(params[2], (u->nickname) ? u->nickname : u->uid, u->uid);
				else
					for_command = xstrdup(params[2]);

				command_exec(NULL, session, for_command, 0);
				xfree(for_command);
			}
				
		} else {
			char **tmp = array_make(params[1], ",", 0, 0, 0);
			int i;
			userlist_t **u;

			u = xmalloc(sizeof(userlist_t *) * array_count(tmp));

			/* first we are checking all of the parametrs */
			for (i = 0; tmp[i]; i++) {
				if (!(u[i] = userlist_find(session, tmp[i]))) {
					printq("user_not_found", tmp[i]);
					array_free(tmp);
					xfree(u);
					return -1;
				}
			}

			for (i = 0; tmp[i]; i++) {
				char *for_command;

				if (!u[i] || !u[i]->uid)
					continue;

				if (!next_is_for)
					for_command = format_string(params[2], (u[i]->nickname) ? u[i]->nickname : u[i]->uid, u[i]->uid);
				else
					for_command = xstrdup(params[2]);

				command_exec(NULL, session, for_command, 0);
				xfree(for_command);
			}
			array_free(tmp);
			xfree(u);
		}
	} else if (match_arg(params[0], 'w', ("windows"), 2)) {
		char *param = (char *) params[2];
		int next_is_for = 0;

		if (param[0] == '/')
			param++;

		if (!xstrncasecmp(param, name, xstrlen(name)))
			next_is_for = 1;

		if (!windows) {
			return -1;
		}

		if (for_all) {
			window_t *w, *next;

			for (w = windows; w; w = next) {
				char *for_command;
				next = w->next;		/* this shall protect us from window killing (current one, not next) */

				if (!w || !w->target || !w->session)
					continue;

				if (!next_is_for)		/* XXX, get_uid(), get_nickname() */
					for_command = format_string(params[2], get_nickname(w->session, w->target), get_uid(w->session, w->target));
				else
					for_command = xstrdup(params[2]);

				command_exec(NULL, w->session, for_command, 0);
				xfree(for_command);
			}
		 } else {
			char **tmp = array_make(params[1], ",", 0, 0, 0);
			int i;
			window_t **w;

			w = xmalloc(sizeof(window_t *) * array_count(tmp));

			/* first we are checking all of the parametrs */
			for (i = 0; tmp[i]; i++) {
				if (!(w[i] = window_exist(atoi(tmp[i])))) {
					printq("window_doesnt_exist", tmp[i]);
					array_free(tmp);
					xfree(w);
					return -1;
				}
			}

			for (i = 0; tmp[i]; i++) {
				char *for_command;

				if (!w[i] || !w[i]->target || !w[i]->session)
					continue;

				if (!next_is_for)		/* XXX, get_uid(), get_nickname() */
					for_command = format_string(params[2], get_nickname(w[i]->session, w[i]->target), get_uid(w[i]->session, w[i]->target));
				else
					for_command = xstrdup(params[2]);
				command_exec(NULL, w[i]->session, for_command, 0);
				xfree(for_command);
			}
			array_free(tmp);
			xfree(w);
		}
	} else {
		printq("invalid_params", name);
		return -1;
	}
	return 0;
}

static COMMAND(cmd_help)
{
	command_t *c;
	if (params[0]) {
		const char *p = (params[0][0] == '/' && xstrlen(params[0]) > 1) ? params[0] + 1 : params[0];
		int plen;

		if (!xstrcasecmp(p, ("set")) && params[1]) {
			if (!quiet)
				variable_help(params[1]);
			return 0;
		}

						/* vvv - allow /help sess sth */
		if (!xstrncasecmp(p, ("session"), xstrlen(p) > 3 ? xstrlen(p) : 3) && params[1]) {
			if (!quiet)
				session_help(session, params[1]);
			return 0;
		}

		if (session)
			plen = (int)(xstrchr(session->uid, ':') - session->uid) + 1;
		else
			plen = 0;
	
		for (c = commands; c; c = c->next) {
			if (!xstrcasecmp(c->name, p) && (c->flags & COMMAND_ISALIAS)) {
				printq("help_alias", p);
				return -1;
			}
			if (!xstrcasecmp(c->name, p) && (c->flags & COMMAND_ISSCRIPT)) {
				printq("help_script", p);
				return -1;
			}

			if (!(c->flags & COMMAND_ISALIAS) && 
					(!xstrcasecmp(c->name, p) || 
					(session && !xstrncmp(c->name, session->uid, plen) && !xstrcasecmp(c->name + plen, p))))
			{
				FILE *f; 
				char *line, *params_help = NULL, *params_help_s, *brief = NULL, *tmp = NULL;
				const char *seeking_name;
				string_t s;
				int found = 0;

				if (c->plugin && c->plugin->name) {
					char *tmp2;

					if (!(f = help_path("commands", c->plugin->name))) {
						print("help_command_file_not_found_plugin", c->plugin->name);
						return -1;
					}
					tmp2 = xstrchr(c->name, ':');
					if (!tmp2)
						seeking_name = c->name;
					else
						seeking_name = tmp2 + 1;
				} else {
					if (!(f = help_path("commands", NULL))) {
						print("help_command_file_not_found");
						return -1;
					}

					seeking_name = c->name;
				}

				while ((line = read_file_iso(f, 0))) {
					if (!xstrcasecmp(line, seeking_name)) {
						found = 1;
						break;
					}
				}

				if (!found) {
					fclose(f);
					print("help_command_not_found", c->name);
					return -1;
				}

				line = read_file_iso(f, 0);

				if ((tmp = xstrstr(line, (": "))))
					params_help = xstrdup(tmp + 2);
				else
					params_help = xstrdup((""));

				params_help_s = strip_spaces(params_help);

				line = read_file_iso(f, 0);

				if ((tmp = xstrstr(line, (": "))))
					brief = xstrdup(tmp + 2);
				else
					brief = xstrdup(("?"));

				tmp = NULL;

				if (xstrstr(brief, ("%")))
					tmp = format_string(brief);

				if (!xstrcmp(brief, (""))) {
					xfree(brief);
					brief = xstrdup(("?"));
				}

				if (xstrcmp(params_help_s, ("")))
					printq("help", (c->name) ? (c->name) : (""), params_help_s, tmp ? tmp : brief, "");
				else
					printq("help_no_params", (c->name) ? (c->name) : (""), tmp ? tmp : brief, (""));

				xfree(brief);
				xfree(params_help);
				xfree(tmp);

				s = string_init(NULL);
				while ((line = read_file_iso(f, 0))) {
					if (line[0] != ('\t'))
						break;
					
					if (!xstrncmp(line, ("\t- "), 3) && xstrcmp(s->str, (""))) {
						print("help_command_body", line);
						string_clear(s);
					}
					
					if (!xstrncmp(line, ("\t"), 1) && xstrlen(line) == 1) {
						string_append(s, "\n\r");
						continue;
					}
					
					string_append(s, line + 1);

					if (line[xstrlen(line) - 1] != ' ')
						string_append_c(s, ' ');
				}
				
				if (xstrcmp(s->str, (""))) {
					char *tmp = format_string(s->str);
					printq("help_command_body", tmp);
					xfree(tmp);
				}
				fclose(f);
				string_free(s, 1);
				return 0;
			}
		}
	}

	for (c = commands; c; c = c->next) {
		if (xisalnum(*c->name) && !(c->flags & COMMAND_ISALIAS)) {
			char *blah = NULL;
			FILE *f;
			char *line, *params_help, *params_help_s, *brief, *tmp = NULL;
			const char *seeking_name;
			int found = 0;

			if (c->plugin && c->plugin->name) {
				char *tmp2;

				if (!(f = help_path("commands", c->plugin->name))) continue;

				tmp2 = xstrchr(c->name, (':'));
				if (!tmp2)
					seeking_name = c->name;
				else 
					seeking_name = tmp2 + 1;
			} else {
				if (!(f = help_path("commands", NULL))) continue;

				seeking_name = c->name;
			}

			while ((line = read_file_iso(f, 0))) {
				if (!xstrcasecmp(line, seeking_name)) {
					found = 1;
					break;
				}
			}

			if (!found) {
				fclose(f);
				continue;
			}

			line = read_file_iso(f, 0);
		
			if ((tmp = xstrstr(line, (": "))))
			       params_help = xstrdup(tmp + 2);
			else
			       params_help = xstrdup((""));

			params_help_s = strip_spaces(params_help);	

			line = read_file_iso(f, 0);

			if ((tmp = xstrstr(line, (": "))))
			       brief = xstrdup(tmp + 2);
			else
			       brief = xstrdup(("?"));

			if (xstrstr(brief, ("%")))
				blah = format_string(brief);

			if (!xstrcmp(brief, (""))) {
				xfree(brief);
				brief = xstrdup(("?"));
			}

			if (xstrcmp(params_help_s, ("")))
				printq("help", c->name ? (c->name) : (""), params_help_s, blah ? blah : brief, (""));
			else
				printq("help_no_params", (c->name) ? (c->name) : (""), blah ? blah : brief, (""));
			xfree(blah);
			xfree(brief);
			xfree(params_help);

			fclose(f);
		}
	}

	printq("help_footer");
	printq("help_quick");
	return 0;
}

static COMMAND(cmd_ignore)
{
	const char *uid;

	if (*name == 'i' || *name == 'I') {
		int flags, modified = 0;

		if (!params[0]) {
			userlist_t *ul;
			int i = 0;
			for (ul = session->userlist; ul; ul = ul->next) {
				userlist_t *u = ul;
				int level;

				if (!(level = ignored_check(session, u->uid)))
					continue;

				i = 1;

				printq("ignored_list", format_user(session, u->uid), ignore_format(level));
			}

			if (!i)
				printq("ignored_list_empty");

			return 0;
		}

		if (params[0][0] == '#') {
			return command_exec_format(NULL, NULL, quiet, ("/conference --ignore %s"), params[0]);
		}

		if (!(uid = get_uid(session, params[0]))) {
			printq("user_not_found", params[0]);
			return -1;
		}

		if ((flags = ignored_check(session, uid)))
			modified = 1;

		if (params[1]) {
			int __flags = ignore_flags(params[1]);

			if (!__flags) {
				printq("invalid_params", name);
				return -1;
			}

			flags |= __flags;
		} else
			flags = IGNORE_ALL;

		if (modified) {
			uid = xstrdup(uid);
			ignored_remove(session, uid);
		}

		if (!ignored_add(session, uid, flags)) {
			if (modified) {
				printq("ignored_modified", format_user(session, uid));
				/* We've just got this from xstrdup(), so safe to free here */
				xfree((void *) uid);
			} else
				printq("ignored_added", format_user(session, uid));
			config_changed = 1;
		}

	} else {
		int unignore_all = ((params[0] && !xstrcmp(params[0], "*")) ? 1 : 0);

		if (!params[0]) {
			printq("not_enough_params", name);
			return -1;
		}

		if (params[0][0] == '#') {
			return command_exec_format(NULL, NULL, quiet, ("/conference --unignore %s"), params[0]);
		}
		
		if (!unignore_all && !(uid = get_uid(session, params[0]))) {
			printq("user_not_found", params[0]);
			return -1;
		}

		if (unignore_all) {
			userlist_t *ul;
			int x = 0;
			
			for (ul = session->userlist; ul; ) {
				userlist_t *u = ul;
				userlist_t *next = ul->next;

				if (!ignored_remove(session, u->uid))
					x = 1;

				ul = next;
			}

			if (x) {
				printq("ignored_deleted_all");
				config_changed = 1;
			} else {
				printq("ignored_list_empty");
				return -1;
			}
			
			return 0;
		}

		if (!ignored_remove(session, uid)) {
			printq("ignored_deleted", format_user(session, params[0]));
			config_changed = 1;
		} else {
			printq("error_not_ignored", format_user(session, params[0]));
			return -1;
		}
	
	}
	return 0;
}

COMMAND(cmd_list)
{
	userlist_t *ul;
	int count = 0, show_all = 1, show_away = 0, show_active = 0, show_inactive = 0, show_invisible = 0, show_descr = 0, show_blocked = 0, show_offline = 0, show_online = 0, i;
	char *show_group = NULL;
	const char *tmp;
	metacontact_t *m = NULL;
	const char *params0 = params[0];
	const char *__first_name, *__last_name;
	int __ip, __port, __last_ip, __last_port;

	if (!params0 && window_current->target) { 
		params0 = window_current->target;
	}

	if (params0 && (*params0 != '-' || userlist_find(session, params0))) {
		char *status;
		const char *group = params0;
		userlist_t *u;
		ekg_resource_t *rl;
		int invert = 0;
		
		/* list !@grupa */
		if (group[0] == '!' && group[1] == '@') {
			group++;
			invert = 1;
		}

		/* list @grupa */
		if (group[0] == '@' && xstrlen(group) > 1) {
			string_t members = string_init(NULL);
			char *__group;
			int count = 0;
			
			for (ul = session->userlist; ul; ul = ul->next) {
				u = ul;

				if (u->groups || invert) {
					if ((!invert && ekg_group_member(u, group + 1)) || (invert && !ekg_group_member(u, group + 1))) {
						if (count++)
							string_append(members, ", ");
						string_append(members, u->nickname);
					}
				}
			}
			
			__group = saprintf("%s%s", ((invert) ? "!" : ""), group + 1);

			if (count)
				printq("group_members", __group, members->str);
			else
				printq("group_empty", __group);

			xfree(__group);

			string_free(members, 1);
			
			return 0;
		}

		if (params0 && (tmp = xstrrchr(params0, '/'))) {
			char *session_name = xstrndup(params0, xstrlen(params0) - xstrlen(tmp));
	
			if (!session_find(session_name))
				goto next;
	
			tmp++;
			session = session_find(session_name);
			if (!(u = userlist_find(session, tmp)) || !u->nickname) {
				printq("user_not_found", tmp);
				xfree(session_name);
				return -1;
			}
	
			xfree(session_name);
			goto list_user;
		}

next:
		/* list _metacontact */
		if (params0 && (m = metacontact_find(params0))) {
			metacontact_item_t *i;
	
			i = metacontact_find_prio(m);

			if (!i) {
				printq("metacontact_item_list_empty");
				return -1;
			} 
		
			u = userlist_find_n(i->s_uid, i->name);

			status = format_string(format_find(ekg_status_label(u->status, u->descr, "metacontact_info_")), get_user_name(u), u->descr);

			printq("metacontact_info_header", params0);
			printq("metacontact_info_status", status);
			printq("metacontact_info_footer", params0);

			xfree(status);
			return 0;
		}
	
		if (!(u = userlist_find(session, params0)) || !u->nickname) {
			printq("user_not_found", params0);
			return -1;
		}

list_user:
		status = format_string(format_find(ekg_status_label(u->status, u->descr, "user_info_")), 
				get_user_name(u), u->descr);

		printq("user_info_header", u->nickname, u->uid);
		if (u->nickname && xstrcmp(u->nickname, u->nickname)) 
			printq("user_info_nickname", u->nickname);

		printq("user_info_status", status);
		if (u->status_time && EKG_STATUS_IS_NA(u->status)) {
			struct tm *status_time;
			char buf[100];		

			status_time = localtime(&(u->status_time));
			if (!strftime(buf, sizeof(buf), format_find("user_info_status_time_format") ,status_time) && format_exists("user_info_status_time_format"))
				xstrcpy(buf, "TOOLONG");

			printq("user_info_status_time", buf);
		}

		if (u->last_status) {
			char *last_status = format_string(format_find(ekg_status_label(u->last_status, u->last_descr, "user_info_")), 
							get_user_name(u), u->last_descr);
			printq("user_info_last_status", last_status);
			xfree(last_status);
		}

		for (rl = u->resources; rl; rl = rl->next) {
			ekg_resource_t *r = rl;
			char *resstatus; 

			resstatus = format_string(format_find(ekg_status_label(r->status, r->descr, /* resource_info? senseless */ "user_info_")), 
					/* here r->name ? */
					 get_user_name(u), r->descr);
			printq("resource_info_status", r->name, resstatus, itoa(r->prio));
			xfree(resstatus);
		}

		if (ekg_group_member(u, "__blocked"))
			printq("user_info_block", get_user_name(u));
		if (ekg_group_member(u, "__offline"))
			printq("user_info_offline", get_user_name(u));
		if (ekg_group_member(u, "__online"))
			printq("user_info_online", get_user_name(u));

		/*
		 * (frequently) common private data
		 */
		__first_name = user_private_item_get(u, "first_name");
		__last_name = user_private_item_get(u, "last_name");
		if (xstrcmp(__first_name, "") && xstrcmp(__last_name, ""))
			printq("user_info_name", __first_name, __last_name);
		else if (xstrcmp(__first_name, ""))
			printq("user_info_name", __first_name, "");
		else if (xstrcmp(__last_name, ""))
			printq("user_info_name", __last_name, "");

		if ( (tmp = user_private_item_get(u, "mobile")) )
			printq("user_info_mobile", tmp);

		__ip = user_private_item_get_int(u, "ip");
		__port = user_private_item_get_int(u, "port");
		__last_ip = user_private_item_get_int(u, "last_ip");
		__last_port = user_private_item_get_int(u, "last_port");
		if (__ip) {
			char *ip_str;
			if (__port)
				ip_str = saprintf("%s:%d", inet_ntoa(*((struct in_addr*) &__ip)), __port);
			else
				ip_str = saprintf("%s", inet_ntoa(*((struct in_addr*) &__ip)));
			printq("user_info_ip", ip_str);
			xfree(ip_str);
		} else if (__last_ip) {
			char *last_ip_str;
				if (__last_port)
					last_ip_str = saprintf("%s:%d", inet_ntoa(*((struct in_addr*) &__last_ip)), __last_port);
				else
					last_ip_str = saprintf("%s", inet_ntoa(*((struct in_addr*) &__last_ip)));
			printq("user_info_last_ip", last_ip_str);
			xfree(last_ip_str);
		}


		query_emit_id(NULL, USERLIST_INFO, &u, &quiet);


		if (u->groups) {
			char *groups = group_to_string(u->groups, 0, 1);

			if (strcmp(groups, ""))
				printq("user_info_groups", groups);
			xfree(groups);
		}
		if (EKG_STATUS_IS_NA(u->status)) {
			char buf[100];
			struct tm *last_seen_time;
			
			if (u->last_seen) {
				last_seen_time = localtime(&(u->last_seen));
				if (!strftime(buf, sizeof(buf), format_find("user_info_last_seen_time"), last_seen_time) && format_exists("user_info_last_seen_time"))
					xstrcpy(buf, "TOOLONG");
				printq("user_info_last_seen", buf);
			} else
				printq("user_info_never_seen");
		}
			
		printq("user_info_footer", u->nickname, u->uid);
		
		xfree(status);
		return 0;
	}
	
	/* list --active | --away | --blocked | --description | --inactive | --invisible | --member | --notavail | --offline | --online */

	for (i = 0; params[i]; i++) {
		if (match_arg(params[i], 'a', ("active"), 2)) {
			show_all = 0;
			show_active = 1;
		} else if (match_arg(params[i], 'i', ("inactive"), 2) || match_arg(params[i], 'n', ("notavail"), 2)) {
			show_all = 0;
			show_inactive = 1;
		} else if (match_arg(params[i], 'A', ("away"), 2)) {
			show_all = 0;
			show_away = 1;
		} else if (match_arg(params[i], 'I', ("invisible"), 2)) {
			show_all = 0;
			show_invisible = 1;
		} else if (match_arg(params[i], 'B', ("blocked"), 2)) {
			show_all = 0;
			show_blocked = 1;
		} else if (match_arg(params[i], 'o', ("offline"), 2)) {
			show_all = 0;
			show_offline = 1;
		} else if (match_arg(params[i], 'O', ("online"), 2)) {
			show_all = 0;
			show_online = 1;
		} else if (match_arg(params[i], 'm', ("member"), 2)) {
			if (params[i+1]) {
				i++;
				int off = (params[i][0] == '@' && xstrlen(params[i]) > 1) ? 1 : 0;
				xfree(show_group);
				show_group = xstrdup(params[i] + off);
			} else
				printq("invalid_params", name);
		} else if (match_arg(params[i], 'd', ("description"), 2)) {
			show_descr = 1;
		} else
			printq("invalid_params", name);
	}

	for (ul = session->userlist; ul; ul = ul->next) {

		userlist_t *u = ul;
		int show;

		if (!u->nickname)
			continue;

		tmp = ekg_status_label(u->status, u->descr, "list_");

		show = show_all;
#define SHOW_IF_S(x,y) if (show_##x && (u->status == EKG_STATUS_##y)) show = 1;
		SHOW_IF_S(away, AWAY)
			SHOW_IF_S(active, AVAIL)
			SHOW_IF_S(inactive, NA)
			SHOW_IF_S(invisible, INVISIBLE)
			SHOW_IF_S(blocked, BLOCKED)
#undef SHOW_IF_S		
			/* XXX nie chcialo mi sie zmiennej robic */
			if (u->status == EKG_STATUS_ERROR)
				show = 1;

		if (show_descr && !u->descr)
			show = 0;

		if (show_group && !ekg_group_member(u, show_group))
			show = 0;

		if (show_offline && ekg_group_member(u, "__offline"))
			show = 1;

		if (show_online && ekg_group_member(u, "__online"))
			show = 1;

		if (show) {
			int __ip = user_private_item_get_int(u, "ip");

			printq(tmp, format_user(session, u->uid), get_user_name(u), inet_ntoa(*((struct in_addr*) &__ip)), itoa(user_private_item_get_int(u, "port")), u->descr);
			count++;
		}
	}

	if (!count && !(show_descr || show_group) && show_all)
		printq("list_empty");
	xfree(show_group);
	return 0;
}

static COMMAND(cmd_save) {
	int ret = 0;
	last_save = time(NULL);

/* Changes 14 wrze 2006 (dj) */
/* We try to save everything, but if smth not pass, try others */
/* makes it executable even if we don't have sessions. */

	/* set windows layout */
	windows_save();

	/* set default session */
	if (config_sessions_save && session_current) {
		xfree(config_session_default); config_session_default = xstrdup(session_current->uid);
	}

	if (session_write())		ret = -1;
	if (config_write(params[0]))	ret = -1;
	if (metacontact_write())	ret = -1;
	if (script_variables_write())	ret = -1;

	if (!ret) {
		printq("saved");
		config_changed = 0;
		ekg2_reason_changed = 0;
	} else {
		printq("error_saving");
	}

	return ret;
}

static COMMAND(cmd_set)
{
	const char *arg = NULL, *val = NULL;
	int unset = 0, show_all = 0, res = 0, be_quiet = 0;
	char *value = NULL;

	show_all = match_arg(params[0], 'a', ("all"), 1);
	be_quiet = match_arg(params[0], 'q', ("quiet"), 1);
	if (show_all || be_quiet) {
		arg = params[1];
		if (arg)
			val = params[2];
	} else {
		arg = params[0];
		if (arg)
			val = params[1];
	}
	
	if (arg && arg[0] == '-') {
		unset = 1;
		arg++;
	}

	if (arg && val) {
		char **tmp = array_make(val, (""), 0, 0, 1);

		value = xstrdup(tmp[0]);
		array_free(tmp);
	}

	if ((!arg || !val) && !unset) {
		variable_t *v;
		int displayed = 0;

		for (v = variables; v; v = v->next) {
			if ((!arg || !xstrcasecmp(arg, v->name)) && (v->display != 2 || xstrcmp(name, ("set")))) {
				int value;

				if (!show_all && !arg && v->dyndisplay && !((v->dyndisplay)(v->name)))
					continue;

				if (!v->display) {
					printq("variable", v->name, ("(...)"));
					displayed = 1;
					continue;
				}

				if (v->type == VAR_STR || v->type == VAR_FILE || v->type == VAR_DIR || v->type == VAR_THEME) {
					char *string = *(char**)(v->ptr);
					char *tmp = (string) ? saprintf(("\"%s\""), string) : ("(none)");

					printq("variable", v->name, tmp);
					
					if (string)
						xfree(tmp);
				}

				/* We delay variable initialization until the
				 * type is known to be such that is properly
				 * aligned for reading an int.
				 */
				value = *(int*)(v->ptr);

				if (v->type == VAR_BOOL)
					printq("variable", v->name, (value) ? ("1 (on)") : ("0 (off)"));
				
				if ((v->type == VAR_INT || v->type == VAR_MAP) && !v->map)
					printq("variable", v->name, itoa(value));

				if (v->type == VAR_INT && v->map) {
					char *tmp = NULL;
					int i;

					for (i = 0; v->map[i].label; i++)
						if (v->map[i].value == value) {
							tmp = saprintf(("%d (%s)"), value, v->map[i].label);
							break;
						}

					if (!tmp)
						tmp = saprintf(("%d"), value);

					printq("variable", v->name, tmp);

					xfree(tmp);
				}

				if (v->type == VAR_MAP && v->map) {
					string_t s = string_init(itoa(value));
					int i, first = 1;

					for (i = 0; v->map[i].label; i++) {
						if ((value & v->map[i].value) || (!value && !v->map[i].value)) {
							string_append(s, (first) ? (" (") : (","));
							first = 0;
							string_append(s, v->map[i].label);
						}
					}

					if (!first)
						string_append_c(s, (')'));

					printq("variable", v->name, s->str);

					string_free(s, 1);
				}

				displayed = 1;
			}
		}

		if (!displayed && params[0]) {
			printq("variable_not_found", params[0]);
			return -1;
		}
	} else {
		variable_t *v = variable_find(arg);
		theme_cache_reset();

		if (!unset && !xstrcasecmp(value, ("t"))) {
			if (v && v->type == VAR_BOOL) {
				int t_value = *(int*)(v->ptr);
			
				xfree(value);
				value = (t_value) ? xstrdup(("0")) : xstrdup(("1"));
			}
		} 

		switch (variable_set(arg, (unset) ? NULL : value)) {
			case 0:
				if (!be_quiet) {
					config_changed = 1;
					last_save = time(NULL);
				}
			case 1:
			{
				if (be_quiet)
					break;

				const char *my_params[2] = { (!unset) ? params[0] : params[0] + 1, NULL };
				cmd_set(("set-show"), (const char **) my_params, NULL, NULL, quiet);
				break;
			}
			case -1:
				printq("variable_not_found", arg);
				res = -1;
				break;
			case -2:
				printq("variable_invalid", arg);
				res = -1;
				break;
		}
	}
	
	xfree(value);

	return res;
}

static COMMAND(cmd_quit)
{
	char *reason;
	session_t *s;
	
	reason = xstrdup(params[0]);
	query_emit_id(NULL, QUITTING, &reason);
	xfree(reason);

	for (s = sessions; s; s = s->next) {
		if (params[0])
			command_exec_format(NULL, s, 3, ("/disconnect %s"), params[0]);
		else
			command_exec(NULL, s, "/disconnect", 3);
	}

	/* nie wychodzimy tutaj, �eby command_exec() mia�o szans� zwolni�
	 * u�ywan� przez siebie pami��. */
	quit_command = 1;

	return 0;
}

/**
 * cmd_version()
 *
 * printq() ekg2 VERSION + compile_time() and emit <i>PLUGIN_PRINT_VERSION</i><br>
 * Handler for: <i>/version</i> command.
 *
 * @return 0
 */

static COMMAND(cmd_version) {
	printq("ekg_version", VERSION, compile_time());
#ifdef SCONS_NOTES
	printq("generic2", SCONS_NOTES);
#endif
#ifdef VER_DISTNOTES
	printq("generic2", VER_DISTNOTES);
#endif
	query_emit_id(NULL, PLUGIN_PRINT_VERSION);

	return 0;
}

/**
 * cmd_test_segv()
 *
 * It try do Segmentation fault [By writting one byte to @@ 0x41414141]<br>
 * Sad command :(<br>
 * Handler for: <i>/_segv</i> command.
 *
 * @sa handle_sigsegv()	- ekg2's handler for SIGSEGV signal
 *
 * @return Shouldn't return, SIGSEGV should be raised before. If SIGSEGV not raised return 0
 */

static COMMAND(cmd_test_segv) {
	char *foo = (char*) 0x41414141;

	*foo = 0x41;

	return 0;
}

static COMMAND(cmd_test_send)
{
	const char *sender, *rcpts[2] = { NULL, NULL };

	if (!params[0] || !params[1] || !window_current || !window_current->session)
		return -1;

	sender = params[0];

	if (sender[0] == '>') {
		rcpts[0] = sender + 1;
		sender = window_current->session->uid;
	}

	xfree(message_print((session) ? session->uid : NULL, sender, (rcpts[0]) ? rcpts : NULL, params[1], NULL, time(NULL), EKG_MSGCLASS_CHAT, "1234", EKG_TRY_BEEP, 0));

	return 0;
}

static COMMAND(cmd_test_addtab)
{
	tabnick_add(params[0]);
	return 0;
}

static COMMAND(cmd_test_deltab)
{
	tabnick_remove(params[0]);
	return 0;
}

static COMMAND(cmd_test_debug)
{
	debug("%s\n", params[0]);
	return 0;
}

static COMMAND(cmd_test_debug_dump)
{
	char *tmp = saprintf(("Zapisalem debug do pliku debug.%d"), (int) getpid());

	debug_write_crash();
	printq("generic", tmp);
	xfree(tmp);

	return 0;
}

static COMMAND(cmd_test_debug_theme)
{
	const char *fname = params[0];

	if (!fname)
		fname = "ekg2-dump.theme";

	/* XXX, wyswietlic komunikat */

	return theme_write(fname);
}

static char *timer_next_call(struct timer *t) {
	long usec, sec, minutes = 0, hours = 0, days = 0;
	struct timeval tv;

	gettimeofday(&tv, NULL);

	if (t->ends.tv_usec < tv.tv_usec) {
		sec = t->ends.tv_sec - tv.tv_sec - 1;
		usec = (t->ends.tv_usec - tv.tv_usec + 1000000) / 1000;
	} else {
		sec = t->ends.tv_sec - tv.tv_sec;
		usec = (t->ends.tv_usec - tv.tv_usec) / 1000;
	}

	if (sec > 86400) {
		days = sec / 86400;
		sec -= days * 86400;
	}

	if (sec > 3600) {
		hours = sec / 3600;
		sec -= hours * 3600;
	}

	if (sec > 60) {
		minutes = sec / 60;
		sec -= minutes * 60;
	}

	if (days)
		return saprintf("%ldd %ldh %ldm %ld.%.3ld", days, hours, minutes, sec, usec);

	if (hours)
		return saprintf("%ldh %ldm %ld.%.3ld", hours, minutes, sec, usec);

	if (minutes)
		return saprintf("%ldm %ld.%.3ld", minutes, sec, usec);

	return saprintf("%ld.%.3ld", sec, usec);
}

static COMMAND(cmd_debug_plugins) {
	char buf[256];
	const plugin_t *p;

	for (p = plugins; p; p = p->next) {
		const char *class;

		switch (p->pclass) {
			case PLUGIN_GENERIC:	class = "generic";		break;
			case PLUGIN_PROTOCOL:	class = "protocol";		break;
			case PLUGIN_UI:			class = "ui";			break;
			case PLUGIN_LOG:		class = "log";			break;
			case PLUGIN_SCRIPTING:	class = "scripting";	break;
			case PLUGIN_AUDIO:		class = "audio";		break;
			case PLUGIN_CODEC:		class = "codec";		break;
			case PLUGIN_CRYPT:		class = "crypt";		break;
			default:				class = "-";
		}

		snprintf(buf, sizeof(buf), "%-15s %-10s %-3d", p->name, class, p->prio);
		printq("generic", buf);

		if (p->pclass == PLUGIN_PROTOCOL && p->priv) {
			struct protocol_plugin_priv *pp = (struct protocol_plugin_priv *)p->priv;

			char *pr = array_join((char**) pp->protocols, ", ");
			char *st;
			char **_sts = NULL;
			const status_t *_st;

			for (_st = pp->statuses; *_st != EKG_STATUS_NULL; _st++) {
				array_add(&_sts, (char*) ekg_status_string(*_st, 2));
			}
			st = array_join(_sts, ", ");

			snprintf(buf, sizeof(buf), "    protocols:  %s", pr);
			printq("generic2", buf);
			snprintf(buf, sizeof(buf), "    statuses:   %s", st);
			printq("generic2", buf);

			xfree(pr);
			xfree(_sts);
		}
	}
	return 0;
}

static COMMAND(cmd_debug_timers) {
/* XXX, */
	char buf[256];
	struct timer *t;
	
	printq("generic_bold", ("name               pers peri    handler  next"));
	
	for (t = timers; t; t = t->next) {
		char *plugin;
		char *tmp;

		if (t->plugin)
			plugin = t->plugin->name;
		else
			plugin = "-";

		tmp = timer_next_call(t);

		/* XXX: pointer truncated */
		snprintf(buf, sizeof(buf), "%-20s %-2d %-8d %.8x %-20s", t->name, t->persist, (int) t->period, (int) (long) t->function, tmp);
		printq("generic", buf);
		xfree(tmp);
	}

	return 0;
}

static COMMAND(cmd_debug_watches)
{
	char buf[256];
	list_t l;
	
	printq("generic_bold", ("fd	wa   plugin  pers tout	started     rm"));
	
	for (l = watches; l; l = l->next) {
		char *plugin;
		char wa[4];
		watch_t *w = l->data;

		if (!w)
			continue;

		xstrcpy(wa, "");

		if ((w->type & WATCH_READ))
			xstrcat(wa, "R");
		if ((w->type & WATCH_WRITE))
			xstrcat(wa, "W");
		if (w->buf)
			xstrcat(wa, "L");
		if (w->plugin)
			plugin = w->plugin->name;
		else
			plugin = ("-");
		snprintf(buf, sizeof(buf), "%-5d  %-3s	%-8s  %-2d  %-4ld  %-10ld  %-2d", w->fd, wa, plugin, 1, w->timeout, w->started, w->removed);
		printq("generic", buf);
	}

	return 0;
}

static COMMAND(cmd_debug_queries)
{
	query_t **ll, *q;
	
	printq("generic", ("name			     | plugin	   | count"));
	printq("generic", ("---------------------------------|-------------|------"));
	
	for (ll = queries; ll <= &queries[QUERY_EXTERNAL]; ll++) {
		if (ll == &queries[QUERY_EXTERNAL] && *ll)
			printq("generic", ("------EXTERNAL-QUERIES-----------|-------------|-----"));
		for (q = *ll; q; q = q->next) {
			char buf[256];
			const char *plugin = (q->plugin) ? q->plugin->name : ("-");

			snprintf(buf, sizeof(buf), "%-32s | %-11s | %d", __(query_name(q->id)), plugin, q->count);
			printq("generic", buf);
		}
	}

	return 0;
}

static COMMAND(cmd_debug_query)
{
	char *p[10];
	int i;

	memset(p, 0, sizeof(p));

	for (i = 0; params[i] && i < 10; i++)
		p[i] = xstrdup(params[i]);

	query_emit(NULL, p[0], &p[1], &p[2], &p[3], &p[4], &p[5], &p[6], &p[7], &p[8], &p[9]);

	for (i = 0; i < 10; i++)
		xfree(p[i]);

	return 0;
}

static WATCHER(cmd_test_dns2_watch) {
	struct in_addr a;
	int len;

	if (type) {
		xfree(data);
		close(fd);
		return -1;
	}

	len = read(fd, &a, sizeof(a));

	if (len != sizeof(a)) {
		print("generic_error", "ekg2-resolver-internal-error");
	} else {
		print("dns2_resolved", (char *) data, inet_ntoa(a));
	}

	return -1;
}

static COMMAND(cmd_test_dns2) {
	watch_t *w;

	/* hacked just to get it compile, fix it l8r */
	if (!(w = ekg_resolver4(NULL, params[0], cmd_test_dns2_watch, NULL, 0, 0, 0))) {
		printq("generic_error", strerror(errno));
		return -1;
	}
	w->data = xstrdup(params[0]);
	return 0;
}

/**
 * cmd_test_mem()
 *
 * Check and printq() how much memory ekg2 use.<br>
 * Handler for: <i>/_mem</i> command
 *
 * @note OS currently supported: Linux, Solaris, FreeBSD. If your OS isn't supported please give us info.
 * @bug Need cleanup/rewritting
 *
 * @return Memory used by ekg2
 */

static COMMAND(cmd_test_mem) {
#ifndef NO_POSIX_SYSTEM
	char *temp, *p = NULL;
	char *txt;
	FILE *file = NULL;
	int rd = 0, rozmiar = 0, unmres;
	struct utsname sys;

	unmres = uname(&sys);

	temp = saprintf("/proc/%d/status", getpid());

	if ( (unmres != -1 && !xstrcmp(sys.sysname, "FreeBSD")) || (file  = fopen(temp,"rb")) ) {
		xfree(temp);
		{
#ifdef __linux__
			char buf[1024];

			rd = fread(buf, 1, 1024, file);
			fclose(file);
			if (rd == 0)
			{
				printq("generic_error", ("Internal error, GiM's fault"));
				return -1;
			} 
			p = xstrstr(buf, "VmSize");
			if (p) {
				sscanf(p, "VmSize:     %d kB", &rozmiar);
			} else {
				printq("generic_error", ("VmSize line not found!"));
				return -1;
			}
#elif __sun
			pstatus_t proc_stat;
			rd = fread(&proc_stat, sizeof(proc_stat), 1, file);
			fclose(file);
			if (rd == 0)
			{
				printq("generic_error", "Internal error, GiM's fault");
				return -1;
			}
			rozmiar = proc_stat.pr_brksize + proc_stat.pr_stksize;
#elif __FreeBSD__ /* link with -lkvm */
			char errbuf[_POSIX2_LINE_MAX];
			int nentries = -1;
			struct kinfo_proc *kp;
			static kvm_t	  *kd;

			if (!(kd = kvm_openfiles(NULL /* "/dev/null" */, "/dev/null", NULL, /* O_RDONLY */0, errbuf))) {
				printq("generic_error", ("Internal error! (kvm_openfiles)"));
				return -1;
			}
			kp = kvm_getprocs(kd, KERN_PROC_PID, getpid(), &nentries);
			if (!kp || nentries != 1) {
				printq("generic_error", ("Internal error! (kvm_getprocs)"));
				return -1; 
			}
#ifdef HAVE_STRUCT_KINFO_PROC_KI_SIZE
			rozmiar = (u_long) kp->ki_size/1024; /* freebsd5 */
#else
			rozmiar = kp->kp_eproc.e_vm.vm_map.size/1024; /* freebsd4 */
#endif /* HAVE_STRUCT_KINFO_PROC_KI_SIZE */
#else
			if (unmres != -1)
				p = saprintf(" You seem to have /proc mounted, but I don't know how to deal with it. Authors would be very thankful, if you'd contac wit them, quoting this error message. [%s %s %s %s]", sys.sysname, sys.release, sys.version, sys.machine);
			else
				p = saprintf(" You seem to have /proc mounted, but I don't know how to deal with it. Authors would be very thankful, if you'd contac wit them, quoting this error message. uname() failed!");

			printq("generic_error", p);
			xfree(p);
			return -1;
#endif
		}
		txt = saprintf(("Memory used by ekg2: %d KiB"), rozmiar);
		printq("generic", txt);
		xfree(txt);
	} else {
		printq("generic_error", ("/proc not mounted, no permissions, or no proc filesystem support"));
		xfree(temp);
		return -1;
	}
	return rozmiar;
#else
	return -1;
#endif
}

static COMMAND(cmd_test_fds)
{
#ifndef NO_POSIX_SYSTEM
	struct stat st;
	char buf[PATH_MAX+1000];
	int i;
	
	for (i = 0; i < 2048; i++) {
		if (fstat(i, &st) == -1)
			continue;

		sprintf(buf, "%d: ", i);

		if (S_ISREG(st.st_mode)) {
#ifdef __linux__
			char *mypath = saprintf("/proc/%d/fd/%d", getpid(), i);
			char newpath[PATH_MAX];
			int r = readlink(mypath, newpath, sizeof(newpath)-1);

			xfree(mypath);
			if (r <= 0)
				sprintf(buf + xstrlen(buf), "file, inode %lu, size %lu", (long)st.st_ino, (long)st.st_size);
			else {
				newpath[r] = 0;
				sprintf(buf + xstrlen(buf), "file, inode %lu, size %lu, path %s", (long)st.st_ino, (long)st.st_size, newpath);
			}
#else
			sprintf(buf + xstrlen(buf), "file, inode %lu, size %lu", st.st_ino, st.st_size);
#endif
		}

		if (S_ISSOCK(st.st_mode)) {
			/* GiM: ten sock_n, bo sockaddr_in6 > sockaddr_in */
			char sock_n[256], bufek[256];
			struct sockaddr *sa = (struct sockaddr*)sock_n;
/*			struct sockaddr_un *sun = (struct sockaddr_un*) &sa;		*/
			struct sockaddr_in *sin = (struct sockaddr_in*) sa;
#ifdef HAVE_GETADDRINFO
			struct sockaddr_in6 *sin6 = (struct sockaddr_in6*) sa;
#endif
			socklen_t sa_len = 256;

			if (getpeername(i, sa, &sa_len) == -1) {
				getsockname(i, sa, &sa_len);

				/* GiM->dj: I'm not changing this stuff
				 * couse this was just beautifying stuff
				 * and by '*' I don't mean 0.0.0.0
				 */
				if (sa->sa_family == AF_INET) {
					xstrcat(buf, "socket, inet, *:");
					xstrcat(buf, itoa(ntohs(sin->sin_port)));
				} else if (sa->sa_family == AF_INET6) {
					xstrcat(buf, "socket, inet6, *:");
					xstrcat(buf, itoa(ntohs(sin->sin_port)));
				} else
					xstrcat(buf, "socket");
			} else {
				switch (sa->sa_family) {
					case AF_UNIX:
						xstrcat(buf, "socket, unix");
						break;
					case AF_INET:
						xstrcat(buf, "socket, inet, ");
						xstrcat(buf, inet_ntoa(sin->sin_addr));
						xstrcat(buf, ":");
						xstrcat(buf, itoa(ntohs(sin->sin_port)));
						break;
#ifdef HAVE_GETADDRINFO
					case AF_INET6:
						xstrcat(buf, "socket, inet6, ");
#ifdef HAVE_INET_NTOP
						inet_ntop(sa->sa_family, &(sin6->sin6_addr), bufek, sizeof(bufek));
						xstrcat(buf, bufek);
#else
						xstrcat(buf, "strange?");
#endif /* HAVE_INET_NTOP */
						xstrcat(buf, ":");
						xstrcat(buf, itoa(ntohs(sin6->sin6_port)));
						break;
#endif /* HAVE_GETADDRINFO */
					default:
						xstrcat(buf, "socket, ");
						xstrcat(buf, itoa(sa->sa_family));
				}
			}
		}
		
		if (S_ISDIR(st.st_mode))
			xstrcat(buf, "directory");
		
		if (S_ISCHR(st.st_mode))
			xstrcat(buf, "char");

		if (S_ISBLK(st.st_mode))
			xstrcat(buf, "block");

		if (S_ISFIFO(st.st_mode))
			xstrcat(buf, "fifo");

		if (S_ISLNK(st.st_mode))
			xstrcat(buf, "symlink");

		printq("generic", buf);
	}

	return 0;
#else
	return -1;
#endif
}

static COMMAND(cmd_test_iconv) {
	return ekg_converters_display(quiet);
}

/**
 * cmd_beep()
 *
 * Beep by emiting <i>UI_BEEP</i> event
 *
 * @note UI-PLUGINS should connect to this event, and when they succesfully beep, returns -1.
 *	 Cause we may have more than 1 UI-PLUGIN, and I really don't like idea, when after /beep 
 *	 I hear 2 or more beeps.
 *
 * @return 0
 */

static COMMAND(cmd_beep) {
	query_emit_id(NULL, UI_BEEP, NULL);
	return 0;
}

static COMMAND(cmd_play)
{
	if (!config_sound_app) {
		printq("var_not_set", name, "sound_app");
		return -1;
	}

	return play_sound(params[0]);
}

static COMMAND(cmd_say)
{
	if (!config_speech_app) {
		printq("var_not_set", name, "speech_app");
		return -1;
	}

	if (match_arg(params[0], 'c', ("clear"), 2)) {
		buffer_free(&buffer_speech);
		return 0;
	}

	return say_it(params[0]);
}

static COMMAND(cmd_reload)
{
	int res;

	if ((res = config_read_plugins())) printq("error_reading_config", strerror(errno));
	if (res == -1) return -1;

	if ((res = config_read(NULL))) printq("error_reading_config", strerror(errno));
	if (res == -1) return -1;

	metacontacts_destroy();

	if ((res = session_read(NULL))) printq("error_reading_config", strerror(errno));
	if (res == -1) return -1;

	if ((res = metacontact_read())) printq("error_reading_config", strerror(errno));
	if (res == -1) return -1;

	printq("config_read_success");
	config_changed = 0;

	return res;
}

static COMMAND(cmd_query) {
	char *par0 = NULL;
	metacontact_t *m;
	window_t *w;
	char *tmp;

	if (params[0][0] == '@' || xstrchr(params[0], ',')) {	/* you want to talk with lot people? let's create conference ! */
		struct conference *c = conference_create(session, params[0]);
		
		if (!c)
			return -1;

		par0 = c->name;
	}

	if (!par0 && params[0][0] == '#') {			/* query conference ? ok... */
		struct conference *c = conference_find(params[0]);

		if (!c) {
			printq("conferences_noexist", params[0]);
			return -1;
		}

		par0 = c->name;
	}

	if (!par0 && (tmp = xstrrchr(params[0], '/'))) {	/* query user in passed session? wow XXX, we need to fix it cause jabber can pass '/' as resource. XXX */
		char *session_name = xstrndup(params[0], xstrlen(params[0]) - xstrlen(tmp));
		session_t *sess_tmp;

		if (!(sess_tmp = session_find(session_name))) {
			xfree(session_name);
			goto next;
		}
		xfree(session_name);

		session = sess_tmp;

		tmp++;
	
		if (!get_uid(session, tmp)) {
			printq("invalid_session");
			return -1;
		}

		par0 = tmp;
	}

next:
	if (!par0 && (m = metacontact_find(params[0]))) {	/* some metacontact's name ? */
		metacontact_item_t *i = metacontact_find_prio(m);

		if (!i) {
			printq("metacontact_item_list_empty");
			return -1;
		}
		
		par0 = i->name;
		session = session_find(i->s_uid);
	}

	if (!par0) {						/* everything else if it's valid uid/ (nickname if we have user in roster) */
		const char *uid = get_uid(session, params[0]);

		if (!uid) {
			printq("user_not_found", params[0]);
			return -1;
		}
		par0 = (char *) params[0];
	}

	if (!(w = window_find_s(session, par0))) {		/* if we don't have window, we need to create it, in way specified by (config_make_window) */
		if (config_make_window & 1) {
			window_t *v;

			for (v = windows; v; v = v->next) {
				if (v->id < 2 || v->floating || v->target)
					continue;

				w = v;
				break;
			}

			if (w) {
				w->target = xstrdup(par0);				/* new target */
				w->session = session;					/* change session */
				query_emit_id(NULL, UI_WINDOW_TARGET_CHANGED, &w);	/* notify ui-plugin */
			}
		} else if (!(config_make_window & 2) && window_current /* && window_current->id >1 && !window_current->floating */) {
			w = window_current;
			xfree(w->target);	w->target = xstrdup(par0);		/* change target */
			w->session = session;						/* change session */
			query_emit_id(NULL, UI_WINDOW_TARGET_CHANGED, &w);		/* notify ui-plugin */
		}

		if (!w) w = window_new(par0, session, 0);	/* jesli jest config_make_window => 2 lub nie mielismy wolnego okienka przy config_make_window == 1, stworzmy je */

		if (!quiet) {		/* display some beauty info */
			print_info(par0, session, "query_started", par0, session_name(session));
			print_info(par0, session, "query_started_window", par0, session_name(session));
		}
	}
	if (w != window_current) window_switch(w->id);	/* switch to that window */

	if (params[1])	/* if we've got message in params[1] send it */
		command_exec_format(par0, session, quiet, ("/ %s"), params[1]);
	return 0;
}

/**
 * cmd_echo()
 *
 * printq() params[0] if not NULL, or just ""<br>
 * Handler for: <i>/echo</i> command
 *
 * @return 0
 */

static COMMAND(cmd_echo) {
	printq("generic", params[0] ? params[0] : "");
	return 0;
}

/*
 * command_exec()
 *
 * executes a command stored in a string.
 *
 *  - target - which window was the command invoked in (or NULL if not in one)
 *  - session - session on behalf of which we're running
 *  - xline - the string containing the line of text
 *  - quiet - hide output if == 1, hide inexistence of command if == 2
 *
 * 0/-1.
 */
int command_exec(const char *target, session_t *session, const char *xline, int quiet)
{
	char short_cmd[2] = (".");
	char *p = NULL;
	char *line_save = NULL, *line = NULL;
	char *cmd = NULL, *tmp;
	size_t cmdlen;

	/* TODO: what does the "last" prefix stand for? */
	command_t *last_command = NULL;
	command_t *last_command_plugin = NULL; /* unneeded, but someone wrote it as if it would be necessary one day, so we leave it here */
	int abbrs = 0;	/* 1 if the post-prefix part of command was spelled out fully, e.g. user entered "disconnect" while the command is "gg:disconnect" */
	int abbrs_plugins = 0;

	int exact = 0;

	command_t *c;

	if (!xline)
		return 0;

	/* in a chat window, and there is no leading slash */
	if (target && *xline != '/') {
		int correct_command = 0;
	
		/* detection of commands entered by mistake */
		if (config_query_commands) {
			for (c = commands; c; c = c->next) {
				size_t l = xstrlen(c->name);

				if (l < 3 || xstrncasecmp(xline, c->name, l))
					continue;
				
				if (!xline[l] || xisspace(xline[l])) {
					correct_command = 1;
					break;
				}
			}		
		}

		if (!correct_command)
			return command_exec_format(target, session, quiet, ("/ %s"), xline);
	}
	if (target && *xline == '/' && config_slash_messages == 1) {
		/* send the message if the first word contains at least two slashes,
		 * e.g. "/bin/sh" or "/dev/hda1", as it is not likely to be an ekg2 command
		 * code stolen from ekg1, idea stolen from irssi. */
		char *p = strchr(xline + 1, '/');
		char *s = strchr(xline + 1, ' ');

		if (p && (!s || p < s)) 
			return command_exec_format(target, session, quiet, ("/ %s"), xline);
	}

	
	send_nicks_index = 0;
	line = line_save = xstrdup(xline);

	if (*line == '/')
		line++;

	if (*line == '^') {
		quiet = 1;
		line++;
	}

	/* Check if this is a special one-character command. These are special
	 * because they do not require whitespace to separate them from their arguments. */
	for (c = commands; c; c = c->next) {
		if (!isalpha_pl_PL(c->name[0]) && xstrlen(c->name) == 1 && line[0] == c->name[0]) {
			short_cmd[0] = c->name[0];
			cmd = short_cmd;
			p = line + 1;
		}
	}
	/* Separate command from arguments if not. */
	if (!cmd) {
		tmp = cmd = line;
		while (*tmp && !xisspace(*tmp))
			tmp++;
		p = (*tmp) ? tmp + 1 : tmp;
		*tmp = 0;
	}
	/* Whatever it was, at this point:
	 * 'cmd' points at a string containing the command verb
	 * 'p' points at the (possibly empty) string containing parameters
	 */
	cmdlen = xstrlen(cmd);

	/* First, look for a command specific to the given session. */
	if (!session && session_current)
		session = session_current;
	if (session && session->uid) {
		int prefix_len = (int)(xstrchr(session->uid, ':') - session->uid) + 1;
		
		for (c = commands; c; c = c->next) {
			/* Consider commands prefixed with current session's prefix. */
			if (xstrncasecmp(c->name, session->uid, prefix_len))
				continue;
			/* Look for fully spelled out command. */
			if (!xstrcasecmp(c->name + prefix_len, cmd)) {
				last_command = c;
				abbrs = 1;
				exact = 1;
				break;
			}
			/* Fall back to the first matching prefix. */
			if (!xstrncasecmp(c->name + prefix_len, cmd, cmdlen)) {
				last_command_plugin = c;
				abbrs_plugins++;
			} else {
				/* TODO: document what this does */
				if (last_command_plugin && abbrs_plugins == 1)
					break;
			} 
		}
	}
	/* If needed, fall back to non-session-specific commands. */
	if (!exact) {
		for (c = commands; c; c = c->next) {
			if (!xstrcasecmp(c->name, cmd)) {
				last_command = c;
				abbrs = 1;
				exact = 1;
				/* if this is exact_match we should zero those below, they won't be used */
				abbrs_plugins = 0; 
				last_command_plugin = NULL;
				break;
			}
			if (!xstrncasecmp(c->name, cmd, cmdlen)) {
				last_command = c;
				abbrs++;
			} else {
				if (last_command && abbrs == 1)
					break;
			}
		} 
	}
/*	debug("%x %x\n", last_command, last_command_plugin);	*/

	/* TODO: what does the following condition ACTUALLY MEAN? */
	if ((last_command && abbrs == 1 && !abbrs_plugins) || ( (last_command = last_command_plugin) && abbrs_plugins == 1 && !abbrs)) {
		/* At this point last_command points at the command_t structure we need to invoke. */
		session_t *s = session ? session : window_current->session;
		const char *last_name	 = last_command->name;
		char *tmp;
		int last_alias	   = 0;
		int res		   = 0;

		if (exact) 
			last_alias = (last_command->flags & COMMAND_ISALIAS || last_command->flags & COMMAND_ISSCRIPT) ? 1 : 0;
		
		if (!last_alias && (tmp = xstrchr(last_name, ':')))
			last_name = tmp + 1;
		
		/* sprawdzamy czy session istnieje - je�li nie to nie mamy po co robi� czego� dalej ... */
		/* nie obsolete ? nie robi tego samego co `session_t *s = session ? session : window_current->session;` (?) */
		if (last_command->flags & SESSION_MUSTHAS) {
			if (!s && !(s = session_current))
				res = -1;
		}

		if (!res && last_command->flags & SESSION_MUSTHASPRIVATE) {
			if (!s || !(s->priv)) {
				printq("invalid_session");
				res = -1;
/*				debug("[command_exec] res = -1; SESSION_MUSTHASPRIVATE"); */
			}
		}
		if (!res && (last_command->flags & SESSION_MUSTBECONNECTED)) {
			if (!session_connected_get(s)) {
				printq("not_connected", session_name(s));	
				res = -1;
			}
		}
		if (!res && (last_command->flags & SESSION_MUSTBELONG)) {
			/* if in future of ekg2, there'll be need of this, please feel free to change thoese if's.
			 * it's correct.. but for now, i don't see much sense of doing it cause. we don't have 
			 * two plugins which handle for example irc: or gg:
			 * if we'll have/ or maybe you have then write to use.
			 * thx.
			 */
/*			if (valid_plugin_uid(last_command->plugin, session_uid_get(s)) != 1) */

			if (last_command->plugin != s->plugin) {
				printq("invalid_session");
				res = -1;
/*				debug("[command_exec] res = -1; SESSION_MUSTBELONG\n"); */
			}
		}

		if (!res) {
			char **parameter_types = (last_command->flags & COMMAND_ISALIAS) ? array_make(("?"), (" "), 0, 1, 1) : last_command->params;
			int parameter_types_count = array_count(parameter_types);
			char **parsed_params = NULL; /* The array of parameter values which is going to be passed to the command handler. */

			/* Perform some parameter verification and mangling.
			 * See flag definitions in commands.h for the exact
			 * meaning of each of the following cases. */

			if (last_command->flags & COMMAND_PASS_UNCHANGED)
				array_add(&parsed_params, xstrdup(p));
			else
				parsed_params = array_make(strip_spaces(p), (" \t"), parameter_types_count, 1, 1);

			if ((last_command->flags & COMMAND_PARAMASTARGET) && parsed_params[0]) {
/*				debug("[command_exec] oldtarget = %s newtarget = %s\n", target, parsed_params[0]); */
				target = parsed_params[0];
			}

			if (/* !res && */ last_command->flags & COMMAND_ENABLEREQPARAMS) {
				int i;
				for (i=0; i < parameter_types_count; i++) {
					char *p;
					if (!(p = parameter_types[i])) break; /* rather impossible */
					if (p[0] == '!' && !parsed_params[i]) {
						if (i == 0 && (last_command->flags & COMMAND_PARAMASTARGET) && target) /* if params[0] already in target */
							continue;	/* skip it */
						debug("[command_exec,%s] res = -1; req params[%d] = NIL\n", last_name, i);
						printq("not_enough_params", last_name);
						res = -1;
						break;
					} else if (p[0] != '!') break;
				}
			}

			if (!res && last_command->flags & COMMAND_TARGET_VALID_UID) {
				const char *tmp = target;
				if (!(target = get_uid(session, target))) {
					/* user_not_found vs invalid_uid */
					printq("user_not_found", tmp);
					res = -1;
				}
			}

			if (!res) {
				window_t *w;
				char *uid;
				
				uid = xstrdup(target);
				w = window_find_sa(s, uid, 0);

				window_lock_inc(w);
				res = (last_command->function)(last_name, (const char **) parsed_params, s, uid, (quiet & 1));
				if (window_find_ptr(w) || (w == window_find_sa(s, uid, 0)))
					window_lock_dec(w);
				else { 
					window_t *w;
					debug("[WINDOW LOCKING] INTERNAL ERROR SETTING ALL WINDOW LOCKS TO 0 [wtarget=%s command=%s]\n", __(uid), __(last_name));
					/* may be faultly */
					for (w=windows; w; w = w->next) {
						if (!(w->lock)) continue;
						w->lock = 0;
					}
				}
				query_emit_id(NULL, UI_WINDOW_REFRESH);
				xfree(uid);
			}
			if (last_command->flags & COMMAND_ISALIAS) array_free(parameter_types);
			array_free(parsed_params);
		}
		xfree(line_save);

		if (quit_command)
			ekg_exit();

		return res;
	}

	if (cmdlen) {	/* if not empty command typed: ("") and we didn't found handler for it... [was: xstrcmp(cmd, "")] */
		if (config_slash_messages == 2 && target && *xline == '/') {
			xfree(line_save);
			return command_exec_format(target, session, quiet, ("/ %s"), xline);
		}

		quiet = quiet & 2;
		printq("unknown_command", cmd);	/* display warning, if !(quiet & 2) */
	}

	xfree(line_save);

	return -1;
}

int command_exec_params(const char *target, session_t *session, int quiet, const char *command, ...) {
	string_t command_with_params;
	va_list ap;
	char *tmp;
	int res;

	if (!command) 
		return 0;

	/* XXX, it will work like command_exec_format(). we need to:
	 *	- command_exec() -> command_exec_params() */

	command_with_params = string_init(command);

/* XXX, later: array_add() */
	
	va_start(ap, command);
	while ((tmp = va_arg(ap, char *))) {
		char ch = '"';

		string_append_c(command_with_params, ' ');	/* separate from command or from previous param */

/*
		if (tmp[0] == '"' && tmp[xstrlen(tmp)-1] == '"')
			ch = '\'';
 */

		string_append_c(command_with_params, ch);
		string_append(command_with_params, tmp);
		string_append_c(command_with_params, ch);
	}

	va_end(ap);
	
	res = command_exec(target, session, command_with_params->str, quiet);
	string_free(command_with_params, 1);
	return res;
}

/**
 * command_exec_format()
 *
 * Format string in @a format and execute formated command
 * Equivalent to:<br>
 *	<code>
 *		char *tmp = saprintf(format, ...);<br>
 *		command_exec(target, session, tmp, quiet);<br>
 *		xfree(tmp);<br>
 *	</code>
 *
 * @note For more details about string formating functions read man 3 vsnprintf
 *
 * @sa command_exec()	- If you want/can use non-formating function.. Watch for swaped params! (@a quiet with @a format)
 *
 * @return	 0 - If @a format was NULL<br>
 *		-1 - If command was not found	[It's result of command_exec()]<br>
 *		else it returns result of command handler.
 */

int command_exec_format(const char *target, session_t *session, int quiet, const char *format, ...) {
	char *command;
	va_list ap;
	int res;
	
	va_start(ap, format);
	command = vsaprintf(format, ap);
	va_end(ap);
	
	if (!command) 
		return 0;
/*	debug("[command_exec_format] %s\n", command); */
	res = command_exec(target, session, command, quiet); /* segvuje na tym ? wtf ?! */
	xfree(command);
	return res;
}

COMMAND(cmd_alias_exec)
{	
	list_t tmp = NULL, m = NULL;
	alias_t *a;
	int need_args = 0;

	for (a = aliases; a; a = a->next) {
		if (!xstrcasecmp(name, a->name)) {
			tmp = a->commands;
			break;
		}
	}

	for (; tmp; tmp = tmp->next) {
		char *p;
		int __need = 0;

		for (p = tmp->data; *p; p++) {

			if (*p == '\\' && p[1] == ('%')) {
				p += 2;
				continue;
			}

			if (*p != '%')
				continue;

			p++;

			if (!*p)
				break;

			if (*p >= '1' && *p <= '9' && (*p - '0') > __need)
				__need = *p - '0';

			if (need_args < __need)
				need_args = __need;
		}

		list_add(&m, xstrdup(tmp->data));
	}
	
	for (tmp = m; tmp; tmp = tmp->next) {
		string_t str;

		if (*((char *) tmp->data) == '/')
			str = string_init(NULL);
		else
			str = string_init(("/"));

		if (need_args) {
			char *args[9], **arr, *s; 
			int i;

			if (!params[0]) {
				printq("aliases_not_enough_params", name);
				string_free(str, 1);
				list_destroy(m, 1);
				return -1;
			}

			arr = array_make(params[0], ("\t "), need_args, 1, 1);

			if (array_count(arr) < need_args) {
				printq("aliases_not_enough_params", name);
				string_free(str, 1);
				array_free(arr);
				list_destroy(m, 1);
				return -1;
			}

			for (i = 0; i < 9; i++) {
				if (i < need_args)
					args[i] = arr[i];
				else
					args[i] = NULL;
			}

			s = format_string((char *) tmp->data, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]);
			string_append(str, s);
			xfree(s);

			array_free(arr);

		} else {
			string_append(str, tmp->data);
			
			if (params[0]) {
				string_append(str, (" "));
				string_append(str, params[0]);
			}
		}

		command_exec(target, session, str->str, quiet);
		string_free(str, 1);
	}
	
	list_destroy(m, 1);
		
	return 0;
}

static COMMAND(cmd_at)
{
	struct timer *t;

	if (match_arg(params[0], 'a', ("add"), 2)) {
		const char *p, *a_name = NULL;
		char *a_command;
		time_t period = 0, freq = 0;
		struct timer *t;

		if (!params[1] || !params[2]) {
			printq("not_enough_params", name);
			return -1;
		}

		if (!strncmp(params[2], "*/", 2) || xisdigit(params[2][0])) {
			a_name = params[1];

			if (!xstrcmp(a_name, "(null)")) {
				printq("invalid_params", name);
				return -1;
			}

			for (t = timers; t; t = t->next) {
				if (!xstrcasecmp(t->name, a_name)) {
					printq("at_exist", a_name);
					return -1;
				}
			}

			p = params[2];
		} else
			p = params[1];

		{
			struct tm *lt;
			time_t now = time(NULL);
			char *tmp, *freq_str = NULL, *foo = xstrdup(p);
			int wrong = 0;

			lt = localtime(&now);
			lt->tm_isdst = -1;

			/* cz�stotliwo�� */
			if ((tmp = xstrchr(foo, '/'))) {
				*tmp = 0;
				freq_str = ++tmp;
			}

			/* wyci�gamy sekundy, je�li s� i obcinamy */
			if ((tmp = xstrchr(foo, '.')) && !(wrong = (xstrlen(tmp) != 3))) {
				sscanf(tmp + 1, "%2d", &lt->tm_sec);
				tmp[0] = 0;
			} else
				lt->tm_sec = 0;

			/* pozb�d�my si� dwukropka */
			if ((tmp = xstrchr(foo, ':')) && !(wrong = (xstrlen(tmp) != 3))) {
				tmp[0] = tmp[1];
				tmp[1] = tmp[2];
				tmp[2] = 0;
			}

			/* jedziemy ... */
			if (!wrong) {
				switch (xstrlen(foo)) {
					int ret;

					case 12:
						ret = sscanf(foo, "%4d%2d%2d%2d%2d", &lt->tm_year, &lt->tm_mon, &lt->tm_mday, &lt->tm_hour, &lt->tm_min);
						if (ret != 5)
							wrong = 1;
						lt->tm_year -= 1900;
						lt->tm_mon -= 1;
						break;
					case 10:
						ret = sscanf(foo, "%2d%2d%2d%2d%2d", &lt->tm_year, &lt->tm_mon, &lt->tm_mday, &lt->tm_hour, &lt->tm_min);
						if (ret != 5)
							wrong = 1;
						lt->tm_year += 100;
						lt->tm_mon -= 1;
						break;
					case 8:
						ret = sscanf(foo, "%2d%2d%2d%2d", &lt->tm_mon, &lt->tm_mday, &lt->tm_hour, &lt->tm_min);
						if (ret != 4)
							wrong = 1;
						lt->tm_mon -= 1;
						break;
					case 6:
						ret = sscanf(foo, "%2d%2d%2d", &lt->tm_mday, &lt->tm_hour, &lt->tm_min);
						if (ret != 3)
							wrong = 1;
						break;	
					case 4:
						ret = sscanf(foo, "%2d%2d", &lt->tm_hour, &lt->tm_min);
						if (ret != 2)
							wrong = 1;
						break;
					default:
						wrong = 1;
				}
			}

			/* nie ma b��d�w ? */
			if (wrong || lt->tm_hour > 23 || lt->tm_min > 59 || lt->tm_sec > 59 || lt->tm_mday > 31 || !lt->tm_mday || lt->tm_mon > 11) {
				printq("invalid_params", name);
				xfree(foo);
				return -1;
			}

			if (freq_str) {
				for (;;) {
					time_t _period = 0;

					if (xisdigit(*freq_str))
						_period = atoi(freq_str);
					else {
						printq("invalid_params", name);
						xfree(foo);
						return -1;
					}

					freq_str += xstrlen(itoa(_period));

					if (xstrlen(freq_str)) {
						switch (xtolower(*freq_str++)) {
							case 'd':
								_period *= 86400;
								break;
							case 'h':
								_period *= 3600;
								break;
							case 'm':
								_period *= 60;
								break;
							case 's':
								break;
							default:
								printq("invalid_params", name);
								xfree(foo);
								return -1;
						}
					}

					freq += _period;
					
					if (!*freq_str)
						break;
				}
			}

			xfree(foo);

			/* plany na przesz�o��? */
			if ((period = mktime(lt) - now) <= 0) {
				if (freq) {
					while (period <= 0)
						period += freq;
				} else {
					printq("at_back_to_past");
					return -1;
				}
			}
		}

		if (a_name)
			a_command = xstrdup(params[3]);
		else
			a_command = array_join((char **) params + 2, " ");

		if (!xstrcmp(strip_spaces(a_command), "")) {
			printq("not_enough_params", name);
			xfree(a_command);
			return -1;
		}

		if ((t = timer_add(NULL, a_name, period, ((freq) ? 1 : 0), timer_handle_command, xstrdup(a_command)))) {
			t->at = 1;
			printq("at_added", t->name);
			if (freq)
				t->period = freq * 1000;
			if (!in_autoexec)
				config_changed = 1;
		}

		xfree(a_command);
		return 0;
	}

	if (match_arg(params[0], 'd', ("del"), 2)) {
		int del_all = 0;
		int ret = 1;

		if (!params[1]) {
			printq("not_enough_params", name);
			return -1;
		}

		if (!xstrcmp(params[1], "*")) {
			del_all = 1;
			ret = timer_remove_user(1);
		} else
			ret = timer_remove(NULL, params[1]);
		
		if (!ret) {
			if (del_all)
				printq("at_deleted_all");
			else
				printq("at_deleted", params[1]);
			
			config_changed = 1;
		} else {
			if (del_all)
				printq("at_empty");
			else {
				printq("at_noexist", params[1]);
				return -1;
			}
		}

		return 0;
	}

	if (!params[0] || match_arg(params[0], 'l', ("list"), 2) || params[0][0] != '-') {
		const char *a_name = NULL;
		int count = 0;

		if (params[0] && match_arg(params[0], 'l', ("list"), 2))
			a_name = params[1];
		else if (params[0])
			a_name = params[0];

		for (t = timers; t; t = t->next) {
			struct timeval tv;
			struct tm *at_time;
			char tmp[100], tmp2[150];
			time_t sec, minutes = 0, hours = 0, days = 0;

			if (!t->at || (a_name && xstrcasecmp(t->name, a_name)))
				continue;

			if (t->function != timer_handle_command)
				continue;

			count++;

			gettimeofday(&tv, NULL);

			at_time = localtime((time_t *) &t->ends.tv_sec);
			if (!strftime(tmp, sizeof(tmp), format_find("at_timestamp"), at_time) && format_exists("at_timestamp"))
				xstrcpy(tmp, "TOOLONG");

			if (t->persist) {
				sec = t->period / 1000;

				if (sec > 86400) {
					days = sec / 86400;
					sec -= days * 86400;
				}

				if (sec > 3600) {
					hours = sec / 3600;
					sec -= hours * 3600;
				}
			
				if (sec > 60) {
					minutes = sec / 60;
					sec -= minutes * 60;
				}

				strlcpy(tmp2, "every ", sizeof(tmp2));

				if (days) {
					strlcat(tmp2, itoa(days), sizeof(tmp2));
					strlcat(tmp2, "d ", sizeof(tmp2));
				}

				if (hours) {
					strlcat(tmp2, itoa(hours), sizeof(tmp2));
					strlcat(tmp2, "h ", sizeof(tmp2));
				}

				if (minutes) {
					strlcat(tmp2, itoa(minutes), sizeof(tmp2));
					strlcat(tmp2, "m ", sizeof(tmp2));
				}

				if (sec) {
					strlcat(tmp2, itoa(sec), sizeof(tmp2));
					strlcat(tmp2, "s", sizeof(tmp2));
				}
			}

			printq("at_list", t->name, tmp, (char*)(t->data), "", ((t->persist) ? tmp2 : ""));
		}

		if (!count) {
			if (a_name) {
				printq("at_noexist", a_name);
				return -1;
			} else
				printq("at_empty");
		}

		return 0;
	}

	printq("invalid_params", name);

	return -1;
}

static COMMAND(cmd_timer)
{
	struct timer *t;

	if (match_arg(params[0], 'a', ("add"), 2)) {
		const char *t_name = NULL, *p;
		char *t_command;
		time_t period = 0;
		struct timer *t;
		int persistent = 0;

		if (!params[1] || !params[2]) {
			printq("not_enough_params", name);
			return -1;
		}

		if (xisdigit(params[2][0]) || !strncmp(params[2], "*/", 2)) {
			t_name = params[1];

			if (!xstrcmp(t_name, "(null)")) {
				printq("invalid_params", name);
				return -1;
			}

			for (t = timers; t; t = t->next) {
				if (!t->at && !xstrcasecmp(t->name, t_name)) {
					printq("timer_exist", t_name);
					return -1;
				}
			}

			p = params[2];
			t_command = xstrdup(params[3]);
		} else {
			p = params[1];
			t_command = array_join((char **) params + 2, " ");
		}

		if ((persistent = !strncmp(p, "*/", 2)))
			p += 2;

		for (;;) {
			time_t _period = 0;

			if (xisdigit(*p))
				_period = atoi(p);
			else {
				printq("invalid_params", name);
				xfree(t_command);
				return -1;
			}

			p += xstrlen(itoa(_period));

			if (xstrlen(p)) {
				switch (xtolower(*p++)) {
					case 'd':
						_period *= 86400;
						break;
					case 'h':
						_period *= 3600;
						break;
					case 'm':
						_period *= 60;
						break;
					case 's':
						break;
					default:
						printq("invalid_params", name);
						xfree(t_command);
						return -1;
				}
			}

			period += _period;
			
			if (!*p)
				break;
		}

		if (!xstrcmp(strip_spaces(t_command), "")) {
			printq("not_enough_params", name);
			xfree(t_command);
			return -1;
		}

		if ((t = timer_add(NULL, t_name, period, persistent, timer_handle_command, xstrdup(t_command)))) {
			printq("timer_added", t->name);
			if (!in_autoexec)
				config_changed = 1;
		}

		xfree(t_command);
		return 0;
	}

	if (match_arg(params[0], 'd', ("del"), 2)) {
		int del_all = 0, ret;

		if (!params[1]) {
			printq("not_enough_params", name);
			return -1;
		}

		if (!xstrcmp(params[1], "*")) {
			del_all = 1;
			ret = timer_remove_user(0);
		} else
			ret = timer_remove(NULL, params[1]);
		
		if (!ret) {
			if (del_all)
				printq("timer_deleted_all");
			else
				printq("timer_deleted", params[1]);

			config_changed = 1;
		} else {
			if (del_all)
				printq("timer_empty");
			else {
				printq("timer_noexist", params[1]);
				return -1;	
			}
		}

		return 0;
	}

	if (!params[0] || match_arg(params[0], 'l', ("list"), 2) || params[0][0] != '-') {
		const char *t_name = NULL;
		int count = 0;

		if (params[0] && match_arg(params[0], 'l', ("list"), 2))
			t_name = params[1];
		else if (params[0])
			t_name = params[0];

		for (t = timers; t; t = t->next) {
			char *tmp;

			if (t->function != timer_handle_command)
				continue;

			if (t->at || (t_name && xstrcasecmp(t->name, t_name)))
				continue;

			count++;

			tmp = timer_next_call(t);
			printq("timer_list", t->name, tmp, (char*)(t->data), "", (t->persist) ? "*" : "");
			xfree(tmp);
		}

		if (!count) {
			if (t_name) {
				printq("timer_noexist", t_name);
				return -1;
			} else
				printq("timer_empty");
		}

		return 0;
	}	

	printq("invalid_params", name);

	return -1;
}

static COMMAND(cmd_conference) 
{
	if (!params[0] || match_arg(params[0], 'l', ("list"), 2) || params[0][0] == '#') {
		list_t r;
		int count = 0;
		const char *cname = NULL;
	
		if (params[0] && match_arg(params[0], 'l', ("list"), 2))
			cname = params[1];
		else if (params[0])
			cname = params[0];

		{
			newconference_t *c;

			for (c = newconferences; c; c = c->next) {
/* XXX, 
 *	::: konferencje:
 *		-- konferencja1/sesja
 *			USER1:		[query_emit(NULL/pl, "conference-member-info", &c, &u)] 
 *			USER2:		[....]
 *			query_emit(NULL/pl, "conference-info", &c); 
 *	albo jak /names w ircu, albo jak?
 *	XXX
 */
				print("conferences_list", c->name, "");
				count++;
			}
		}

		{
			struct conference *c;

			for (c = conferences; c; c = c->next) {
				string_t recipients;
				const char *recipient;
				int first = 0;

				recipients = string_init(NULL);

				if (cname && xstrcasecmp(cname, c->name))
					continue;
				
				for (r = c->recipients; r; r = r->next) {
					char *uid = r->data;
					userlist_t *u = userlist_find(session, uid);

					if (u && u->nickname)
						recipient = u->nickname;
					else
						recipient = uid;

					if (first++)
						string_append_c(recipients, ',');
					
					string_append(recipients, recipient);

					count++;
				}

				print(c->ignore ? "conferences_list_ignored" : "conferences_list", c->name, recipients->str);

				string_free(recipients, 1);
			}
		}

		if (!count) {
			if (params[0] && params[0][0] == '#') {
				printq("conferences_noexist", params[0]);
				return -1;
			} else
				printq("conferences_list_empty");
		}

		return 0;
	}

	if (match_arg(params[0], 'j', ("join"), 2)) {
		struct conference *c;
		const char *uid;

		if (!params[1] || !params[2]) {
			printq("not_enough_params", name);
			return -1;
		}

		if (params[1][0] != '#') {
			printq("conferences_name_error");
			return -1;
		}

		if (!(c = conference_find(params[1]))) {
			printq("conferences_noexist", params[1]);
			return -1;
		}

		if (!(uid = get_uid(session, params[2]))) {
			printq("unknown_user", params[2]);
			return -1;
		}

		if (conference_participant(c, uid)) {
			printq("conferences_already_joined", format_user(session, uid), params[1]);
			return -1;
		}

		list_add(&c->recipients, xstrdup(uid));

		printq("conferences_joined", format_user(session, uid), params[1]);

		return 0;
	}

	if (match_arg(params[0], 'a', ("add"), 2)) {
		if (!params[1]) {
			printq("not_enough_params", name);
			return -1;
		}

		if (params[2]) {
			if (params[1][0] != '#') {
				printq("conferences_name_error");
				return -1;
			} else
				conference_add(session, params[1], params[2], quiet);
		} else
			conference_create(session, params[1]);

		return 0;
	}

	if (match_arg(params[0], 'd', ("del"), 2)) {
		if (!params[1]) {
			printq("not_enough_params", name);
			return -1;
		}

		if (!xstrcmp(params[1], "*"))
			conference_remove(NULL, quiet);
		else {
			if (params[1][0] != '#') {
				printq("conferences_name_error");
				return -1;
			}

			conference_remove(params[1], quiet);
		}

		return 0;
	}

	if (match_arg(params[0], 'r', ("rename"), 2)) {
		if (!params[1] || !params[2]) {
			printq("not_enough_params", name);
			return -1;
		}

		if (params[1][0] != '#' || params[2][0] != '#') {
			printq("conferences_name_error");
			return -1;
		}

		conference_rename(params[1], params[2], quiet);

		return 0;
	}
	
	if (match_arg(params[0], 'i', ("ignore"), 2)) {
		if (!params[1]) {
			printq("not_enough_params", name);
			return -1;
		}

		if (params[1][0] != '#') {
			printq("conferences_name_error");
			return -1;
		}

		conference_set_ignore(params[1], 1, quiet);

		return 0;
	}

	if (match_arg(params[0], 'u', ("unignore"), 2)) {
		if (!params[1]) {
			printq("not_enough_params", name);
			return -1;
		}

		if (params[1][0] != '#') {
			printq("conferences_name_error");
			return -1;
		}

		conference_set_ignore(params[1], 0, quiet);

		return 0;
	}

	if (match_arg(params[0], 'f', ("find"), 2)) {
		struct conference *c;
		list_t l;

		if (!params[1]) {
			printq("not_enough_params", name);
			return -1;
		}

		if (params[1][0] != '#') {
			printq("conferences_name_error");
			return -1;
		}

		c = conference_find(params[1]);

		if (c) {
			for (l = c->recipients; l; l = l->next) {
				command_exec_format(target, session, quiet, ("/find %s\n"), (char*) (l->data));
			}
		} else {
			printq("conferences_noexist", params[1]);
			return -1;
		}


		return 0;
	}

	printq("invalid_params", name);

	return -1;
}

static COMMAND(cmd_last)
{
	struct last *ll;
	const char *uid = NULL;
	int show_sent = 0, last_n = 0, count = 0, i = 0, show_all = 0;
	char **arr = NULL;
	const char *nick = NULL;
	time_t n;
	struct tm *now;

	if (match_arg(params[0], 'c', ("clear"), 2)) {
				/* XXX, get_uid() */
		if (params[1] && !(uid = get_uid(session, params[1]))) {
			printq("user_not_found", params[1]);
			return -1;
		}

		if ((uid && !last_count(uid)) || !LIST_COUNT2(lasts)) {
			if (uid)
				printq("last_list_empty_nick", format_user(session, uid));
			else
				printq("last_list_empty");

			return -1;
		}

		if (uid) {
			last_del(uid);
			printq("last_clear_uin", format_user(session, uid));
		} else {
			lasts_destroy();
			printq("last_clear");
		}

		return 0;
	}		

	if (params[0]) {
		show_sent = match_arg(params[0], 's', ("stime"), 2);

		if (!show_sent)
			nick = params[0];

		if (params[1]) {
			arr = array_make(params[1], " \t", 0, 1, 0);

			nick = arr[0];
			
			if (match_arg(params[0], 'n', ("number"), 2)) {
				last_n = strtol(arr[0], NULL, 0);
				nick = arr[1];
				
				if (arr[1] && (show_sent = match_arg(arr[1], 's', ("stime"), 2)))
					nick = arr[2];
			}

			if (arr[1] && show_sent && match_arg(arr[0], 'n', ("number"), 2)) {
				last_n = atoi(arr[1]);
				nick = arr[2];
			}

		}
	}

	show_all = (nick && !xstrcasecmp(nick, "*")) ? 1 : 0;
			/* XXX, get_uid() */
	if (!show_all && nick && !(uid = get_uid(session, nick))) {
		printq("user_not_found", nick);
		array_free(arr);
		return -1;
	}

	array_free(arr);
		
	if (!((uid) ? (count = last_count(uid)) : (count = LIST_COUNT2(lasts)))) {
		if (uid) {
			printq("last_list_empty_nick", format_user(session, uid));
			return -1;
		}

		printq("last_list_empty");
		return 0;
	}

	n = time(NULL);
	now = localtime(&n);

	printq("last_begin");

	for (ll = lasts; ll; ll = ll->next) {
		struct tm *tm, *st;
		char buf[100], buf2[100], *time_str = NULL;
		const char *form;

		if (show_all || !uid || !xstrcasecmp(uid, ll->uid)) {

			if (last_n && i++ < (count - last_n))
				continue;

			tm = localtime(&ll->time);
			if (!strftime(buf, sizeof(buf), format_find("last_list_timestamp"), tm) && format_exists("last_list_timestamp"))
				xstrcpy(buf, "TOOLONG");

			if (show_sent && !ll->type && !(ll->sent_time - config_time_deviation <= ll->time && ll->time <= ll->sent_time + config_time_deviation)) {
				st = localtime(&ll->sent_time);
				form = format_find((tm->tm_yday == now->tm_yday) ? "last_list_timestamp_today" : "last_list_timestamp");
				if (!strftime(buf2, sizeof(buf2), form, st) && xstrlen(form)>0)
					xstrcpy(buf2, "TOOLONG");
				time_str = saprintf("%s/%s", buf, buf2);
			} else
				time_str = xstrdup(buf);

			printq(config_last & 4 && ll->type ? "last_list_out" : "last_list_in", time_str, format_user(session, ll->uid), ll->message);
			xfree(time_str);
		}
	}

	printq("last_end");

	return 0;
}

/**
 * cmd_queue()
 *
 * Manage ekg2 message queue list.<br>
 * Handler for: <i>/queue</i> command.
 *
 * @return 0
 */

static COMMAND(cmd_queue) {
	msg_queue_t *m;
	int isempty = 1;
	const char *queue_list_timestamp_f;	/* cached result of format_find("queue_list_timestamp") */
	
	if (match_arg(params[0], 'c', ("clear"), 2)) {
		if (!msgs_queue) {
			printq("queue_empty");
			return 0;
		}

		if (params[1]) {
			if (!msg_queue_remove_uid(params[1]))	/* msg_queue_remove_uid() returns 0 on success */
				printq("queue_clear_uid", format_user(session, params[1]));
			else	printq("queue_empty_uid", format_user(session, params[1]));	/* queue for user empty */
		} else {
			msgs_queue_destroy();
			printq("queue_clear");
		}

		return 0;
	}

	queue_list_timestamp_f = format_find("queue_list_timestamp");

	for (m = msgs_queue; m; m = m->next) {
		struct tm *tm;
		char buf[100] = { '\0' };	/* we need to init it to '\0' cause queue_list_timestamp_f can be empty. and if buf was not NUL terminated string, than printq() can do SIGSEGV */

		if (!params[0] || !xstrcasecmp(m->rcpts, params[0])) {
			tm = localtime(&m->time);

			/* [if queue_list_timestamp_f != "", (format_find() returns "" if format not found]] && if strftime() fails */
			if (format_ok(queue_list_timestamp_f) && !strftime(buf, sizeof(buf), queue_list_timestamp_f, tm))
				xstrcpy(buf, "TOOLONG");	/* use 'TOOLONG' */

			printq("queue_list_message", buf, m->rcpts, m->message);
			isempty = 0;
		}
	}

	if (isempty) {
		if (params[0])
			printq("queue_empty_uid", format_user(session, params[0]));
		else
			printq("queue_empty");
	}

	return 0;
}

COMMAND(cmd_dcc)
{
	dcc_t *d;

	if (!params[0] || !xstrncasecmp(params[0], "li", 2)) {	/* list */
		int empty = 1, passed = 0;

		for (d = dccs; d; d = d->next) {
			if (d->active)
				continue;
			
			empty = 0;
			
			if (!passed++)
				printq("dcc_show_pending_header");

			switch (d->type) {
				case DCC_SEND:
					printq("dcc_show_pending_send", itoa(d->id), format_user(session, d->uid), d->filename);
					break;
				case DCC_GET:
					printq("dcc_show_pending_get", itoa(d->id), format_user(session, d->uid), d->filename);
					break;
				case DCC_VOICE:
					printq("dcc_show_pending_voice", itoa(d->id), format_user(session, d->uid));
					break;
				default:
					break;
			}
		}

		passed = 0;

		for (d = dccs; d; d = d->next) {
			if (!d->active)
				continue;

			empty = 0;

			if (!passed++)
				printq("dcc_show_active_header");

			switch (d->type) {
				case DCC_SEND:
					printq("dcc_show_active_send", itoa(d->id), format_user(session, d->uid), d->filename, itoa(d->offset), itoa(d->size), 
							(d->size) ? itoa(d->offset * 100 / d->size) : "?");
					break;
				case DCC_GET:
					printq("dcc_show_active_get", itoa(d->id), format_user(session, d->uid), d->filename, itoa(d->offset), itoa(d->size), 
							(d->size) ? itoa(d->offset * 100 / d->size) : "?");
					break;
				case DCC_VOICE:
					printq("dcc_show_active_voice", itoa(d->id), format_user(session, d->uid));
					break;
				default:
					break;
			}
		}

		if (empty)
			printq("dcc_show_empty");
		
		return 0;
	}

	if (!xstrncasecmp(params[0], "c", 1)) {		/* close */
		dcc_t *d = NULL;
		dcc_t *D;
		const char *uid;

		if (!params[1]) {
			printq("not_enough_params", name);
			return -1;
		}
			/* XXX, get_uid() */
		uid = get_uid(session, params[1]);		

		for (D = dccs; D; D = D->next) {
			if (params[1][0] == '#' && atoi(params[1] + 1) == D->id) {
				d = D;
				uid = dcc_uid_get(d);
				break;
			}

			if (uid && !xstrcasecmp(D->uid, uid)) {
				d = D;
				break;
			}
		}

		if (!d) {
			printq("dcc_not_found", params[1]);
			return -1;
		}

		printq("dcc_close", format_user(session, uid));
		dcc_close(d);

		return 0;
	}

	printq("invalid_params", name);

	return -1;
}

/**
 * cmd_plugin()
 *
 * Manage plugins in ekg2 load/unload/list/change plugin prios<br>
 * Handler for: <i>/plugin</i> command
 *
 * @todo see XXX's
 *
 */

static COMMAND(cmd_plugin) {
	int ret;
	plugin_t *pl;

	if (!params[0]) {
		plugin_t *p;

		for (p = plugins; p; p = p->next)
			printq("plugin_list", p->name ? p->name : ("?"), itoa(p->prio));

		if (!plugins) {
			/* XXX, display info: no plugins. */
		}

		return 0;
	}

	if (match_arg(params[0], 'd', ("default"), 2)) {
		plugin_t *p;

		for (p = plugins; p;) {
			plugin_t *next;

			next = LIST_UNLINK2(&plugins, p);
			plugin_register(p, -254);

			p = next;
		}

		queries_reconnect();
		config_changed = 1;
		printq("plugin_default");
	}

	if (params[0][0] == '+') {
		const int prio = (params[1] ? atoi(params[1]) : -254);
		if ((ret = plugin_load(params[0] + 1, prio, 0))) {
			/* if plugin cannot be founded || loaded don't reload theme. */
			return ret;
		}

		queries_reconnect();
		changed_theme(NULL); 
		return 0;
	}

	if (params[0][0] == '-') {
		pl = plugin_find(params[0] + 1);

		if (!pl) {
			/* XXX, display info */
			return -1;
		}
		return plugin_unload(pl);
	}

	if (params[1] && (pl = plugin_find(params[0]))) {
		plugins_unlink(pl);
		plugin_register(pl, atoi(params[1])); 

		queries_reconnect();
		config_changed = 1;
		printq("plugin_prio_set", pl->name, params[1]);

		return 0;
	}

	printq("invalid_params", name);

	return -1;
}

/**
 * cmd_desc()
 *
 * Changes description (@a params[0]) without changing status<br>
 * Handler for: <i>/_desc</i> command
 *
 * @todo Think about it. think, think, think. Maybe let's use queries for it?
 *
 * @todo Check if session_unidle() is needed.
 *
 * @param params [0] New description, if NULL than "" will be used.
 */

static COMMAND(cmd_desc) {
	const char *cmd;
	
	session_unidle(session);
	cmd = ekg_status_string(session->status, 1);

	return command_exec_format(NULL, session, quiet, ("/%s %s"), cmd, (params[0] ? params[0] : ""));
}

/* this command allows user to type /me in Jabber, GG and other not-overriding it protocols
 * without need to prefix with space (to keep syntax the same as with overriding ones) */

static COMMAND(cmd_me)
{
	if (!target) {
		printq("invalid_params", name);
		return -1;
	}

	return command_exec_format(target, session, 0, "/ /me %s", params[0]?params[0]:"");	
}

/**
 * command_add()
 *
 * Add command, and make it known for ekg2.
 *
 * @note About params XXX
 *
 * @note @a flag param, there're two types of it.
 *		Informational like:
 *			- <i>COMMAND_ISALIAS</i> - When it's alias command.<br>
 *			- <i>COMMAND_ISSCRIPT</i> - When it's script command.<br>
 *		and <br>
 *		Conditionals, checked at executing command @@ command_exec() like: [XXX, dorobic]
 *			- <i>COMMAND_ENABLEREQPARAMS</i> -	<br>
 *			- <i>COMMAND_PARAMASTARGET</i>		<br>
 *			- <i>SESSION_MUSTBECONNECTED</i> -	<br>
 *			- <i>SESSION_MUSTBELONG</i>		<br>
 *			- <i>SESSION_MUSTHAS</i>		<br>
 *			- <i>SESSION_MUSTHASPRIVATE</i>		<br>
 *
 * @param plugin	- plugin which handle this command
 * @param name		- name of command
 * @param params	- space seperated paramlist (read note for more details!)
 * @param function	- function handler
 * @param flags		- bitmask from commands.h (read note for more details!)
 * @param possibilities	- optional space separated list of possible params.. completion useful
 *
 * @return Pointer to added command_t *, or NULL if name was NULL.
 *
 */

command_t *command_add(plugin_t *plugin, const char *name, char *params, command_func_t function, command_flags_t flags, char *possibilities) {
	command_t *c;

	if (!name) 
		return NULL;

	c = xmalloc(sizeof(command_t));

	c->name = name;
	c->params = params ? array_make(params, (" "), 0, 1, 1) : NULL;
	c->function = function;
	c->flags = flags;
	c->plugin = plugin;
	c->possibilities = possibilities ? array_make(possibilities, " ", 0, 1, 1) : NULL;

	commands_add(c);
	return c;
}

/*
 * command_remove()
 *
 * usuwa komend� z listy.
 *
 *  - plugin - plugin obs�uguj�cy,
 *  - name - nazwa komendy.
 */

int command_remove(plugin_t *plugin, const char *name)
{
	command_t *c;

	for (c = commands; c; c = c->next) {
		if (!xstrcasecmp(name, c->name) && plugin == c->plugin) {
			(void) commands_removei(c);
			return 0;
		}
	}

	return -1;
}

/*
 * rodzaje parametr�w komend:
 *
 * '?' - it means nothing 
 * 'U' - uid typed by hand, sender of message
 * 'u' - name or uid from the contact list
 * 'C' - name of conference
 * 'c' - name of command
 * 'i' - nicks of ignored
 * 'b' - nicks of blocked
 * 'v' - variable name
 * 'p' - params typed in possibilities
 * 'P' - plugin's name
 * 'f' - file
 * 'e' - event name
 * 'I' - ignored level
 * 's' - session name
 * 'S' - session variable name
 * 'r' - session description 
 * 'o' - directory
 * 'm' - metacontact
 * 'w' - window name
 * 
 * je�eli parametr == 'p' to 6 argument funkcji command_add() przyjmuje jako argument
 * tablic� z mo�liwymi uzupe�nieniami 
 * 
 * parametry te� s� tablic�, dlatego u�ywamy makr possibilities() i params() - najlepiej
 * przeanalizowa� przyk�ady 
 */

/*
 * command_init()
 *
 * inicjuje list� domy�lnych komend.
 */
void command_init()
{
	command_add(NULL, ("!"), "?", cmd_exec, 0, NULL);

	command_add(NULL, ("?"), "c vS", cmd_help, 0, NULL);

	command_add(NULL, ("_addtab"), "!", cmd_test_addtab, COMMAND_ENABLEREQPARAMS, NULL);

	command_add(NULL, ("_debug"), "!", cmd_test_debug, COMMAND_ENABLEREQPARAMS, NULL);
 
	command_add(NULL, ("_debug_dump"), NULL, cmd_test_debug_dump, 0, NULL);

	command_add(NULL, ("_deltab"), "!", cmd_test_deltab, COMMAND_ENABLEREQPARAMS, NULL);

	command_add(NULL, ("_desc"), "r", cmd_desc, SESSION_MUSTHAS, NULL);

	command_add(NULL, ("_dns2"), "!", cmd_test_dns2, COMMAND_ENABLEREQPARAMS, NULL);

	command_add(NULL, ("_fds"), NULL, cmd_test_fds, 0, NULL);

	command_add(NULL, ("_iconv"), NULL, cmd_test_iconv, 0, NULL);

	command_add(NULL, ("_mem"), NULL, cmd_test_mem, 0, NULL);

	command_add(NULL, ("_msg"), "uUC ?", cmd_test_send, 0, NULL);

	command_add(NULL, ("_plugins"), NULL, cmd_debug_plugins, 0, NULL);

	command_add(NULL, ("_queries"), NULL, cmd_debug_queries, 0, NULL);

	command_add(NULL, ("_query"), "? ? ? ? ? ? ? ? ? ?", cmd_debug_query, 0,NULL); 

	command_add(NULL, ("_segv"), NULL, cmd_test_segv, 0, NULL);

	command_add(NULL, ("_streams"), "p ? ? ? ?", cmd_streams, 0, "-c --create -l --list");

	command_add(NULL, ("_theme_dump"), "?", cmd_test_debug_theme, 0, NULL);

	command_add(NULL, ("_timers"), NULL, cmd_debug_timers, 0, NULL);

	command_add(NULL, ("_watches"), NULL, cmd_debug_watches, 0, NULL);

	command_add(NULL, ("add"), "!U ? p", cmd_add, COMMAND_ENABLEREQPARAMS, "-f --find");

	command_add(NULL, ("alias"), "p ?", cmd_alias, 0,
	 "-a --add -A --append -d --del -l --list");

	command_add(NULL, ("at"), "p ? ? c", cmd_at, 0, 
	 "-a --add -d --del -l --list");

	command_add(NULL, ("beep"), NULL, cmd_beep, 0, NULL);

	command_add(NULL, ("bind"), "p ? ?", cmd_bind, 0,
	 "-a --add -d --del -l --list -L --list-default");

	command_add(NULL, ("clear"), NULL, cmd_window, 0,	NULL);
 
	command_add(NULL, ("conference"), "p C uU", cmd_conference, SESSION_MUSTHAS,
	  "-a --add -j --join -d --del -i --ignore -u --unignore -r --rename -f --find -l --list");
 
	command_add(NULL, ("dcc"), "p u f ?", cmd_dcc, 0,
	  "send rsend get resumce rvoice voice close list");

	command_add(NULL, ("del"), "u ?", cmd_del, 0, NULL);

	command_add(NULL, ("echo"), "?", cmd_echo, 0, NULL);
	  
	command_add(NULL, ("eval"), "!", cmd_eval, COMMAND_ENABLEREQPARAMS, NULL);

	command_add(NULL, ("exec"), "p UuC ?", cmd_exec, 0,
	  "-m --msg -b --bmsg");
 
	command_add(NULL, ("for"), "!p !? !c", cmd_for, COMMAND_ENABLEREQPARAMS,
	  "-s --sessions -u --users -w --windows");
 
	command_add(NULL, ("help"), "c vS", cmd_help, 0, NULL);
	  
	command_add(NULL, ("ignore"), "uUC I", cmd_ignore, 0,
	  "status descr notify msg dcc events *");
	  
	command_add(NULL, ("last"), "CpuU CuU", cmd_last, SESSION_MUSTHAS,
	  "-c --clear -s --stime -n --number");

	command_add(NULL, ("list"), "CpuUsm p p p p p p p p", cmd_list, SESSION_MUSTHAS,
	  "-a --active -A --away -B --blocked -d --description -i --inactive -I --invisible -m --member -n --notavail -o --offline -O --online");

	command_add(NULL, ("me"), "?", cmd_me, SESSION_MUSTBECONNECTED, NULL);

	command_add(NULL, ("metacontact"), "mp m s uU ?", cmd_metacontact, 0,
	  "-a --add -d --del -i --add-item -r --del-item -l --list");
	  
	command_add(NULL, ("on"), "p e ? UuC c", cmd_on, SESSION_MUSTHAS,
	  "-a --add -d --del -l --list" );
	
	command_add(NULL, ("play"), "!f", cmd_play, COMMAND_ENABLEREQPARAMS, NULL);

	command_add(NULL, ("plugin"), "P ?", cmd_plugin, 0, NULL);

	command_add(NULL, ("query"), "!uUCms ?", cmd_query, SESSION_MUSTHAS | COMMAND_ENABLEREQPARAMS,
	  "-c --clear");

	command_add(NULL, ("queue"), "puUC uUC", cmd_queue, 0, 
	  "-c --clear");

	command_add(NULL, ("quit"), "r", cmd_quit, 0, NULL);
	  
	command_add(NULL, ("reload"), NULL, cmd_reload, 0, NULL);
	  
	command_add(NULL, ("save"), NULL, cmd_save, 0, NULL);

	command_add(NULL, ("say"), "!", cmd_say, COMMAND_ENABLEREQPARAMS,
	  "-c --clear");

	command_add(NULL, ("script")	    , ("p ?"),	cmd_script, 0, 
	  "--list --load --unload --varlist --reset"); /* todo	?!!? */

	command_add(NULL, ("script:autorun"), ("?"),	cmd_script, 0, "");
	  
	command_add(NULL, ("script:list")   , ("?"),	cmd_script, 0, "");

	command_add(NULL, ("script:load")   , ("f"),	cmd_script, 0, "");

	command_add(NULL, ("script:reset")  , ("?"),	cmd_script, 0, "");

	command_add(NULL, ("script:unload") , ("?"),	cmd_script, 0, "");

	command_add(NULL, ("script:varlist"), ("?"),	cmd_script, 0, "");

	command_add(NULL, ("session"), "psS psS sS ?", session_command, 0,
	  "-a --add -d --del -l --list -g --get -s --set -w --sw");

	command_add(NULL, ("set"), "pv v ?", cmd_set, 0, "-a --all -q --quiet");

	command_add(NULL, ("status"), "s", cmd_status, 0, NULL);

	command_add(NULL, ("tabclear"), "p", cmd_tabclear, 0,
	  "-o --offline");

	command_add(NULL, ("timer"), "p ? ? c", cmd_timer, 0,
	  "-a --add -d --del -l --list");

	command_add(NULL, ("unignore"), "i ?", cmd_ignore, 0, NULL);
	  
	command_add(NULL, ("version"), NULL, cmd_version, 0, NULL);
	  
	command_add(NULL, ("window"), "p ? p", cmd_window, 0,
	  "active clear kill last lastlog list move new next prev switch refresh left right");
}

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 * vim: noet
 */
