/* $Id$ */

/*
 *  (C) Copyright 2001-2004 Wojtek Kaniewski <wojtekka@irc.pl>
 *			    Robert J. Wo�ny <speedy@ziew.org>
 *			    Leszek Krupi�ski <leafnode@wafel.com>
 *			    Adam Mikuta <adammikuta@poczta.onet.pl>
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if HAVE_LANGINFO_CODESET
#include <langinfo.h>
#endif

#include "debug.h"
#include "dynstuff.h"
#include "recode.h"
#include "stuff.h"
#include "themes.h"
#include "vars.h"
#include "xmalloc.h"
#include "plugins.h"

#include "dynstuff_inline.h"
#include "queries.h"

void changed_session_locks(const char *varname); /* sessions.c */
char *console_charset;

static LIST_ADD_COMPARE(variable_add_compare, variable_t *) { return xstrcasecmp(data1->name, data2->name); }
static __DYNSTUFF_LIST_ADD_SORTED(variables, variable_t, variable_add_compare);	/* variables_add() */

variable_t *variables = NULL;

/*
 * dd_*()
 *
 * funkcje informuj�ce, czy dana grupa zmiennych ma zosta� wy�wietlona.
 * r�wnie dobrze mo�na by�o przekaza� wska�nik do zmiennej, kt�ra musi
 * by� r�na od zera, ale dzi�ki funkcjom nie trzeba b�dzie miesza� w 
 * przysz�o�ci.
 */
static int dd_sound(const char *name) { return (config_sound_app != NULL); }
static int dd_color(const char *name) { return (config_display_color); }
static int dd_beep(const char *name) { return (config_beep); }

/*
 * variable_init()
 *
 * inicjuje list� zmiennych.
 */
void variable_init() {
	variable_add(NULL, ("auto_save"), VAR_INT, 1, &config_auto_save, changed_auto_save, NULL, NULL);
	variable_add(NULL, ("auto_user_add"), VAR_BOOL, 1, &config_auto_user_add, NULL, NULL, NULL);
	variable_add(NULL, ("away_reason"), VAR_STR, 1, &config_away_reason, NULL, NULL, NULL);
	variable_add(NULL, ("back_reason"), VAR_STR, 1, &config_back_reason, NULL, NULL, NULL);
	variable_add(NULL, ("beep"), VAR_BOOL, 1, &config_beep, NULL, NULL, NULL);
	variable_add(NULL, ("beep_chat"), VAR_BOOL, 1, &config_beep_chat, NULL, NULL, dd_beep);
	variable_add(NULL, ("beep_msg"), VAR_BOOL, 1, &config_beep_msg, NULL, NULL, dd_beep);
	variable_add(NULL, ("beep_notify"), VAR_BOOL, 1, &config_beep_notify, NULL, NULL, dd_beep);
	variable_add(NULL, ("completion_char"), VAR_STR, 1, &config_completion_char, NULL, NULL, NULL);
	variable_add(NULL, ("completion_notify"), VAR_MAP, 1, &config_completion_notify, NULL, variable_map(4, 0, 0, "none", 1, 2, "add", 2, 1, "addremove", 4, 0, "away"), NULL);
		/* It's very, very special variable; shouldn't be used by user */
	variable_add(NULL, ("config_version"), VAR_INT, 2, &config_version, NULL, NULL, NULL);
		/* XXX, warn here. user should change only console_charset if it's really nesessary... we should make user know about his terminal
		 *	encoding... and give some tip how to correct this... it's just temporary
		 */
	variable_add(NULL, ("console_charset"), VAR_STR, 1, &config_console_charset, changed_console_charset, NULL, NULL);
	variable_add(NULL, ("dcc_dir"), VAR_STR, 1, &config_dcc_dir, NULL, NULL, NULL); 
	variable_add(NULL, ("debug"), VAR_BOOL, 1, &config_debug, NULL, NULL, NULL);
/*	variable_add(NULL, ("default_protocol"), VAR_STR, 1, &config_default_protocol, NULL, NULL, NULL); */
	variable_add(NULL, ("default_status_window"), VAR_BOOL, 1, &config_default_status_window, NULL, NULL, NULL);
	variable_add(NULL, ("display_ack"), VAR_MAP, 1, &config_display_ack, NULL, variable_map(6, 0, 0, "none", 1, 0, "delivered", 2, 0, "queued", 4, 0, "dropped", 8, 0, "tempfail", 16, 0, "unknown"), NULL);
	variable_add(NULL, ("display_blinking"), VAR_BOOL, 1, &config_display_blinking, changed_display_blinking, NULL, NULL);
	variable_add(NULL, ("display_color"), VAR_INT, 1, &config_display_color, NULL, NULL, NULL);
	variable_add(NULL, ("display_color_map"), VAR_STR, 1, &config_display_color_map, NULL, NULL, dd_color);
	variable_add(NULL, ("display_crap"),  VAR_BOOL, 1, &config_display_crap, NULL, NULL, NULL);
	variable_add(NULL, ("display_day_changed"), VAR_BOOL, 1, &config_display_day_changed, NULL, NULL , NULL);
	variable_add(NULL, ("display_notify"), VAR_MAP, 1, &config_display_notify, NULL, variable_map(4, 0, 0, "none", 1, 2, "all", 2, 1, "significant", 4, 0, "unknown_too"), NULL);
	variable_add(NULL, ("display_sent"), VAR_BOOL, 1, &config_display_sent, NULL, NULL, NULL);
	variable_add(NULL, ("display_welcome"), VAR_BOOL, 1, &config_display_welcome, NULL, NULL, NULL);
	variable_add(NULL, ("emoticons"), VAR_BOOL, 1, &config_emoticons, NULL, NULL, NULL);
	variable_add(NULL, ("events_delay"), VAR_INT, 1, &config_events_delay, NULL, NULL, NULL);
	variable_add(NULL, ("expert_mode"), VAR_INT, 1, &config_expert_mode, NULL, NULL, NULL);
	variable_add(NULL, ("exit_exec"), VAR_STR, 1, &config_exit_exec, NULL, NULL, NULL);
	variable_add(NULL, ("history_savedups"),  VAR_BOOL, 1, &config_history_savedups, NULL, NULL, NULL);
	variable_add(NULL, ("keep_reason"), VAR_INT, 1, &config_keep_reason, NULL, NULL, NULL);
	variable_add(NULL, ("last"), VAR_MAP, 1, &config_last, NULL, variable_map(4, 0, 0, "none", 1, 2, "all", 2, 1, "separate", 4, 0, "sent"), NULL);
	variable_add(NULL, ("last_size"), VAR_INT, 1, &config_last_size, NULL, NULL, NULL);
	variable_add(NULL, ("lastlog_display_all"), VAR_INT, 1, &config_lastlog_display_all, NULL, variable_map(3, 
			0, 0, "current window",
			1, 2, "current window + configured",
			2, 1, "all windows + configured"), NULL);
	variable_add(NULL, ("lastlog_matchcase"), VAR_BOOL, 1, &config_lastlog_case, NULL, NULL, NULL);
	variable_add(NULL, ("lastlog_noitems"), VAR_BOOL, 1, &config_lastlog_noitems, NULL, NULL, NULL);
	variable_add(NULL, ("nickname"), VAR_STR, 1, &config_nickname, NULL, NULL, NULL);
	variable_add(NULL, ("make_window"), VAR_MAP, 1, &config_make_window, changed_make_window, variable_map(4, 0, 0, "none", 1, 2, "usefree", 2, 1, "always", 4, 0, "chatonly"), NULL);
	variable_add(NULL, ("mesg"), VAR_INT, 1, &config_mesg, changed_mesg, variable_map(3, 0, 0, "no", 1, 2, "yes", 2, 1, "default"), NULL);
	variable_add(NULL, ("query_commands"), VAR_BOOL, 1, &config_query_commands, NULL, NULL, NULL);
	variable_add(NULL, ("quit_reason"), VAR_STR, 1, &config_quit_reason, NULL, NULL, NULL);
	variable_add(NULL, ("save_password"), VAR_BOOL, 1, &config_save_password, NULL, NULL, NULL);
	variable_add(NULL, ("save_quit"), VAR_INT, 1, &config_save_quit, NULL, NULL, NULL);
	variable_add(NULL, ("session_default"), VAR_STR, 1, &config_session_default, NULL, NULL, NULL);
	variable_add(NULL, ("send_white_lines"), VAR_BOOL, 1, &config_send_white_lines, NULL, NULL, NULL);
	variable_add(NULL, ("session_locks"), VAR_INT, 1, &config_session_locks, changed_session_locks, variable_map(3, 0, 0, "off", 1, 2, "flock", 2, 1, "file"), NULL);
	variable_add(NULL, ("sessions_save"), VAR_BOOL, 1, &config_sessions_save, NULL, NULL, NULL);
	variable_add(NULL, ("slash_messages"), VAR_INT, 1, &config_slash_messages, NULL, variable_map(3, 0, 0, "off", 1, 2, "moreslashes", 2, 1, "unknowncmd"), NULL);
	variable_add(NULL, ("sort_windows"), VAR_BOOL, 1, &config_sort_windows, NULL, NULL, NULL);
	variable_add(NULL, ("sound_app"), VAR_STR, 1, &config_sound_app, NULL, NULL, NULL);
	variable_add(NULL, ("sound_chat_file"), VAR_FILE, 1, &config_sound_chat_file, NULL, NULL, dd_sound);
	variable_add(NULL, ("sound_mail_file"), VAR_FILE, 1, &config_sound_mail_file, NULL, NULL, dd_sound);
	variable_add(NULL, ("sound_msg_file"), VAR_FILE, 1, &config_sound_msg_file, NULL, NULL, dd_sound);
	variable_add(NULL, ("sound_notify_file"), VAR_FILE, 1, &config_sound_notify_file, NULL, NULL, dd_sound);
	variable_add(NULL, ("sound_sysmsg_file"), VAR_FILE, 1, &config_sound_sysmsg_file, NULL, NULL, dd_sound);
	variable_add(NULL, ("speech_app"), VAR_STR, 1, &config_speech_app, NULL, NULL, NULL);
	variable_add(NULL, ("subject_prefix"), VAR_STR, 1, &config_subject_prefix, NULL, NULL, NULL);
	variable_add(NULL, ("subject_reply_prefix"), VAR_STR, 1, &config_subject_reply_prefix, NULL, NULL, NULL);
	variable_add(NULL, ("tab_command"), VAR_STR, 1, &config_tab_command, NULL, NULL, NULL);
	variable_add(NULL, ("theme"), VAR_THEME, 1, &config_theme, changed_theme, NULL, NULL);
	variable_add(NULL, ("time_deviation"), VAR_INT, 1, &config_time_deviation, NULL, NULL, NULL);
	variable_add(NULL, ("timestamp"), VAR_STR, 1, &config_timestamp, NULL, NULL, NULL);	/* ? */
	variable_add(NULL, ("timestamp_show"), VAR_BOOL, 1, &config_timestamp_show, NULL, NULL, NULL);
	variable_add(NULL, ("window_session_allow"), VAR_INT, 1, &config_window_session_allow, NULL, variable_map(4, 0, 0, "deny", 1, 6, "uid-capable", 2, 5, "any", 4, 3, "switch-to-status"), NULL);
	variable_add(NULL, ("windows_layout"), VAR_STR, 2, &config_windows_layout, NULL, NULL, NULL);
	variable_add(NULL, ("windows_save"), VAR_BOOL, 1, &config_windows_save, NULL, NULL, NULL);
}

/*
 * variable_set_default()
 *
 * ustawia pewne standardowe warto�ci zmiennych
 * nieliczbowych.
 */
void variable_set_default() {
	xfree(config_timestamp);
	xfree(config_completion_char);
	xfree(config_display_color_map);
	xfree(config_subject_prefix);
	xfree(config_subject_reply_prefix);
	xfree(config_console_charset);
	xfree(config_dcc_dir);

	xfree(console_charset);

	config_slash_messages = 1;
	config_history_savedups = 1;		/* save lines matching the previous history entry */

	config_dcc_dir = NULL;

	config_timestamp = xstrdup("\\%H:\\%M:\\%S");
	config_completion_char = xstrdup(":");
	config_display_color_map = xstrdup("nTgGbBrR");
	config_subject_prefix = xstrdup("## ");
	config_subject_reply_prefix = xstrdup("Re: ");
#if HAVE_LANGINFO_CODESET
	console_charset = xstrdup(nl_langinfo(CODESET));
#endif

	if (console_charset) 
		config_console_charset = xstrdup(console_charset);
	else
		config_console_charset = xstrdup("ISO-8859-2"); /* Default: ISO-8859-2 */
#if USE_UNICODE
	if (!config_use_unicode && xstrcasecmp(console_charset, "UTF-8")) {
		debug("nl_langinfo(CODESET) == %s swapping config_use_unicode to 0\n", console_charset);
		config_use_unicode = 0;
	} else	config_use_unicode = 1;
#else
	config_use_unicode = 0;
	if (!xstrcasecmp(console_charset, "UTF-8")) {
		debug("Warning, nl_langinfo(CODESET) reports that you are using utf-8 encoding, but you didn't compile ekg2 with (experimental/untested) --enable-unicode\n");
		debug("\tPlease compile ekg2 with --enable-unicode or change your enviroment setting to use not utf-8 but iso-8859-1 maybe? (LC_ALL/LC_CTYPE)\n");
	}
#endif
	config_use_iso = !xstrncasecmp(console_charset, "ISO-8859-", 9);
}

/*
 * variable_find()
 *
 * znajduje struktur� variable_t opisuj�c� zmienn� o podanej nazwie.
 *
 * - name.
 */
variable_t *variable_find(const char *name) {
	variable_t *v;
	int hash;

	if (!name)
		return NULL;

	hash = variable_hash(name);

	for (v = variables; v; v = v->next) {
		if (v->name_hash == hash && !xstrcasecmp(v->name, name))
			return v;
	}

	return NULL;
}

/*
 * variable_map()
 *
 * tworzy now� map� warto�ci. je�li kt�ra� z warto�ci powinna wy��czy� inne
 * (na przyk�ad w ,,log'' 1 wy��cza 2, 2 wy��cza 1, ale nie maj� wp�ywu na 4)
 * nale�y doda� do ,,konflikt''.
 *
 *  - count - ilo��,
 *  - ... - warto��, konflikt, opis.
 *
 * zaalokowana tablica.
 */
variable_map_t *variable_map(int count, ...) {
	variable_map_t *res;
	va_list ap;
	int i;

	res = xcalloc(count + 1, sizeof(variable_map_t));
	
	va_start(ap, count);

	for (i = 0; i < count; i++) {
		res[i].value = va_arg(ap, int);
		res[i].conflicts = va_arg(ap, int);
		res[i].label = xstrdup(va_arg(ap, char*));
	}
	
	va_end(ap);

	return res;
}

/*
 * variable_add()
 *
 * dodaje zmienn� do listy zmiennych.
 *
 *  - plugin - opis wtyczki, kt�ra obs�uguje zmienn�,
 *  - name - nazwa,
 *  - type - typ zmiennej,
 *  - display - czy i jak ma wy�wietla�,
 *  - ptr - wska�nik do zmiennej,
 *  - notify - funkcja powiadomienia,
 *  - map - mapa warto�ci,
 *  - dyndisplay - funkcja sprawdzaj�ca czy wy�wietli� zmienn�.
 *
 * zwraca 0 je�li si� nie uda�o, w przeciwnym wypadku adres do strutury.
 */
variable_t *variable_add(plugin_t *plugin, const char *name, int type, int display, void *ptr, variable_notify_func_t *notify, variable_map_t *map, variable_display_func_t *dyndisplay) {
	variable_t *v;
	char *__name;

	if (!name)
		return NULL;

	if (plugin && !xstrchr(name, ':'))
		__name = saprintf("%s:%s", plugin->name, name);
	else
		__name = xstrdup(name);

	v = xmalloc(sizeof(variable_t));
	v->name		= __name;
	v->name_hash	= variable_hash(__name);
	v->type		= type;
	v->display	= display;
	v->ptr		= ptr;
	v->notify	= notify;
	v->map		= map;
	v->dyndisplay	= dyndisplay;
	v->plugin	= plugin;

	variables_add(v);
	return v;
}

/*
 * variable_remove()
 *
 * usuwa zmienn�.
 */
int variable_remove(plugin_t *plugin, const char *name) {
	int hash;
	variable_t *v;

	if (!name)
		return -1;

	hash = ekg_hash(name);

	for (v = variables; v; v = v->next) {
		if (!v->name)
			continue;
		
		if (hash == v->name_hash && plugin == v->plugin && !xstrcasecmp(name, v->name)) {
			(void) variables_removei(v);
			return 0;
		}
	}
	return -1;
}

/**
 * on_off()
 *
 * @return	 1 - If @a value is one of: <i>on</i>, <i>true</i>, <i>yes</i>, <i>tak</i>, <i>1</i>	[case-insensitive]<br>
 *		 0 - If @a value is one of: <i>off</i>, <i>false</i>, <i>no</i>, <i>nie</i>, <i>0</i>	[case-insensitive]<br>
 *		else -1
 */

static int on_off(const char *value)
{
	if (!value)
		return -1;

	if (!xstrcasecmp(value, "on") || !xstrcasecmp(value, "true") || !xstrcasecmp(value, "yes") || !xstrcasecmp(value, "tak") || !xstrcmp(value, "1"))
		return 1;

	if (!xstrcasecmp(value, "off") || !xstrcasecmp(value, "false") || !xstrcasecmp(value, "no") || !xstrcasecmp(value, "nie") || !xstrcmp(value, "0"))
		return 0;

	return -1;
}

/*
 * variable_set()
 *
 * ustawia warto�� podanej zmiennej. je�li to zmienna liczbowa lub boolowska,
 * zmienia ci�g na liczb�. w przypadku boolowskich, rozumie zwroty typu `on',
 * `off', `yes', `no' itp. je�li dana zmienna jest bitmap�, akceptuje warto��
 * w postaci listy flag oraz konstrukcje `+flaga' i `-flaga'.
 *
 *  - name - nazwa zmiennej,
 *  - value - nowa warto��,
 */
int variable_set(const char *name, const char *value) {
	variable_t *v = variable_find(name);
	char *tmpname;
	int changed = 0;

	if (!v)
		return -1;

	switch (v->type) {
		case VAR_INT:
		case VAR_MAP:
		{
			const char *p = value;
			int hex, tmp;

			if (!value)
				return -2;

			if (v->map && v->type == VAR_INT && !xisdigit(*p)) {
				int i;

				for (i = 0; v->map[i].label; i++)
					if (!xstrcasecmp(v->map[i].label, value))
						value = itoa(v->map[i].value);
			}

			if (v->map && v->type == VAR_MAP && !xisdigit(*p)) {
				int i, k = *(int*)(v->ptr);
				int mode = 0; /* 0 set, 1 add, 2 remove */
				char **args;

				if (*p == '+') {
					mode = 1;
					p++;
				} else if (*p == '-') {
					mode = 2;
					p++;
				}

				if (!mode)
					k = 0;

				args = array_make(p, ",", 0, 1, 0);

				for (i = 0; args[i]; i++) {
					int j, found = 0;

					for (j = 0; v->map[j].label; j++) {
						if (!xstrcasecmp(args[i], v->map[j].label)) {
							found = 1;

							if (mode == 2)
								k &= ~(v->map[j].value);
							if (mode == 1)
								k &= ~(v->map[j].conflicts);
							if (mode == 1 || !mode)
								k |= v->map[j].value;
						}
					}

					if (!found) {
						array_free(args);
						return -2;
					}
				}

				array_free(args);

				value = itoa(k);
			}

			p = value;
				
			if ((hex = !xstrncasecmp(p, "0x", 2)))
				p += 2;

			while (*p && *p != ' ') {
				if (hex && !xisxdigit(*p))
					return -2;
				
				if (!hex && !xisdigit(*p))
					return -2;
				p++;
			}

			tmp = strtol(value, NULL, 0);

			if (v->map) {
				int i;

				for (i = 0; v->map[i].label; i++) {
					if ((tmp & v->map[i].value) && (tmp & v->map[i].conflicts))
						return -2;
				}
			}

			changed = (*(int*)(v->ptr) != tmp);
			*(int*)(v->ptr) = tmp;

			break;
		}

		case VAR_BOOL:
		{
			int tmp;
		
			if (!value)
				return -2;
		
			if ((tmp = on_off(value)) == -1)
				return -2;

			changed = (*(int*)(v->ptr) != tmp);
			*(int*)(v->ptr) = tmp;

			break;
		}
		case VAR_THEME:
		case VAR_FILE:
		case VAR_DIR:
		case VAR_STR:
		{
			char **tmp = (char**)(v->ptr);

			char *oldval = *tmp;

			if (value) {
				if (*value == 1)
					*tmp = base64_decode(value + 1);
				else
					*tmp = xstrdup(value);
			} else
				*tmp = NULL;
	
			changed = xstrcmp(oldval, *tmp);
			xfree(oldval);
			break;
		}
		default:
			return -1;
	}

	if (!changed)
		return 1;

	if (v->notify)
		(v->notify)(v->name);

	tmpname = xstrdup(v->name);
	query_emit_id(NULL, VARIABLE_CHANGED, &tmpname);
	xfree(tmpname);
			
	return 0;
}

LIST_FREE_ITEM(variable_list_freeone, variable_t *) {
	xfree(data->name);

	switch (data->type) {
		case VAR_STR:
		case VAR_FILE:
		case VAR_THEME:
		case VAR_DIR:
			xfree(*((char**) data->ptr));
			*((char**) data->ptr) = NULL;
			break;

		default:
			break;
	}

	if (data->map) {
		int i;

		for (i = 0; data->map[i].label; i++)
			xfree(data->map[i].label);

		xfree(data->map);
	}
}

__DYNSTUFF_LIST_REMOVE_ITER(variables, variable_t, variable_list_freeone);	/* variables_removei() */
__DYNSTUFF_LIST_DESTROY(variables, variable_t, variable_list_freeone);	/* variables_destroy() */

/*
 * variable_help()
 *
 * it shows help about variable from file ${datadir}/ekg/vars.txt
 * or ${datadir}/ekg/plugins/{plugin_name}/vars.txt
 *
 * name - name of the variable
 */
void variable_help(const char *name) {
	FILE *f; 
	char *line, *type = NULL, *def = NULL, *tmp;
	const char *seeking_name;
	string_t s;
	int found = 0;
	variable_t *v = variable_find(name);

	if (!v) {
		print("variable_not_found", name);
		return;
	}

	if (v->plugin && v->plugin->name) {
		char *tmp2;

		if (!(f = help_path("vars", v->plugin->name))) {
			print("help_set_file_not_found_plugin", v->plugin->name);
			return;
		}

		tmp2 = xstrchr(name, ':');
		if (tmp2)
			seeking_name = tmp2+1;
		else
			seeking_name = name;
	} else {
		if (!(f = help_path("vars", NULL))) {
			print("help_set_file_not_found");
			return;
		}
		
		seeking_name = name;
	}

	while ((line = read_file_iso(f, 0))) {
		if (!xstrcasecmp(line, seeking_name)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		fclose(f);
		print("help_set_var_not_found", name);
		return;
	}

	line = read_file_iso(f, 0);
	
	if ((tmp = xstrstr(line, (": "))))
		type = xstrdup(tmp + 2);
	else
		type = xstrdup(("?"));
	
	line = read_file_iso(f, 0);
	if ((tmp = xstrstr(line, (": "))))
		def = xstrdup(tmp + 2);
	else
		def = xstrdup(("?"));

	print("help_set_header", name, type, def);

	xfree(type);
	xfree(def);

	if (tmp)		/* je�li nie jest to ukryta zmienna... */
		read_file_iso(f, 0);	/* ... pomijamy lini� */
	s = string_init(NULL);
	while ((line = read_file_iso(f, 0))) {
		if (line[0] != '\t')
			break;

		if (!xstrncmp(line, ("\t- "), 3) && xstrcmp(s->str, (""))) {
			print("help_set_body", s->str);
			string_clear(s);
		}

		if (!xstrncmp(line, ("\t"), 1) && xstrlen(line) == 1) {
			string_append(s, ("\n\r"));
			continue;
		}
	
		string_append(s, line + 1);

		if (line[xstrlen(line) - 1] != ' ')
			string_append_c(s, ' ');
	}

	if (xstrcmp(s->str, ("")))
		print("help_set_body", s->str);

	string_free(s, 1);
	
	if (format_exists("help_set_footer"))
		print("help_set_footer", name);

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
