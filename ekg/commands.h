/* $Id$ */

/*
 *  (C) Copyright 2001-2002 Wojtek Kaniewski <wojtekka@irc.pl>
 *			    Dawid Jarosz <dawjar@poczta.onet.pl>
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

#ifndef __EKG_COMMANDS_H
#define __EKG_COMMANDS_H

#include "dynstuff.h"
#include "plugins.h"
#include "themes.h"
#include "sessions.h"

#ifdef __cplusplus
extern "C" {
#endif

#define printq(x...) do { if (!quiet) { print(x); } } while(0)

#define COMMAND(x) int x(const char *name, const char **params, session_t *session, const char *target, int quiet)

typedef enum {
/* INFORMATIONAL FLAGS */
	COMMAND_ISALIAS			= 0x01,		/* command is binded by alias management */
	COMMAND_ISSCRIPT		= 0x02,		/* command is binded by script management */
	COMMAND_WITH_RESOURCE		= 0x04,		/* [XXX] command uses resource, and resource should be passed */
	COMMAND_PASS_UNCHANGED		= 0x08,		/* WYSIWYG, pass unchanged line, as first argument */
/* .... */

/* CONDITIONAL FLAGS */
	COMMAND_ENABLEREQPARAMS		= 0x10,		/* '!' in params means that arg must exist in params[..] (?) */
	COMMAND_PARAMASTARGET		= 0x20,		/* when params[0] != NULL, than target = params[0] and then params list moves up
							   (params++ ; params[0] == params[1] and so on */
	SESSION_MUSTBECONNECTED		= 0x40,		/* session must be connected to execute that command */
	SESSION_MUSTBELONG		= 0x80,		/* command must come from the same plugin as session (?) */
	SESSION_MUSTHAS			= 0x100,	/* if session == NULL, we try session_current, if still NULL. we return -1...
							   mh, i really don't know if this flag is obsolete... but we do simillar thing
							   in many places in code, so implemented. */
	SESSION_MUSTHASPRIVATE		= 0x200,	/* session must exist and has private struct */
	COMMAND_TARGET_VALID_UID	= 0x400		/* before executing handler, check if target (or params[0] if COMMAND_PARAMASTARGET
							   set) is valid uid for current session, or we've got smb with this nickname
							   on userlist... (read: we check if get_uid(session, target) return smth,
							   if not we print message) */
} command_flags_t;

typedef COMMAND(command_func_t);

typedef struct command {
	struct command	*next;

/* public: */
	const char	*name;
	plugin_t	*plugin;

/* private: */
	char		**params;
	command_func_t	*function;
	command_flags_t	flags;
	char		**possibilities;
} command_t;

#ifndef EKG2_WIN32_NOFUNCTION
extern command_t *commands;

command_t *command_add(plugin_t *plugin, const char *name, char *params, command_func_t function, command_flags_t flags, char *possibilities);
int command_remove(plugin_t *plugin, const char *name);
command_t *command_find (const char *name);
void command_init();
void commands_remove(command_t *c);
command_t *commands_removei(command_t *c);
void commands_destroy();
int command_exec(const char *target, session_t *session, const char *line, int quiet);
int command_exec_params(const char *target, session_t *session, int quiet, const char *command, ...);
int command_exec_format(const char *target, session_t *session, int quiet, const char *format, ...);

COMMAND(cmd_add);
COMMAND(cmd_alias_exec);
COMMAND(cmd_exec);
COMMAND(cmd_list);
COMMAND(cmd_dcc);
COMMAND(cmd_bind);		/* bindings.c */
COMMAND(session_command);	/* sessions.c */
COMMAND(cmd_on);		/* events.c */
COMMAND(cmd_metacontact);	/* metacontacts.c */
COMMAND(cmd_streams);		/* audio.c */
COMMAND(cmd_script);		/* script.c */
#endif
/*
 * jaka� malutka lista tych, do kt�rych by�y wysy�ane wiadomo�ci.
 */
#define SEND_NICKS_MAX 100

extern char *send_nicks[SEND_NICKS_MAX];
extern int send_nicks_count, send_nicks_index;

#ifndef EKG2_WIN32_NOFUNCTION
void tabnick_add(const char *nick);
void tabnick_remove(const char *nick);


int match_arg(const char *arg, char shortopt, const char *longopt, int longoptlen);

/* wyniki ostatniego szukania */
extern char *last_search_first_name;
extern char *last_search_last_name;
extern char *last_search_nickname;
extern char *last_search_uid;
#endif

#ifdef __cplusplus
}
#endif

#endif /* __EKG_COMMANDS_H */

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
