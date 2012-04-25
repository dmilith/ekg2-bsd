/* $Id: windows.h 4412 2008-08-17 12:28:15Z peres $ */

/*
 *  (C) Copyright 2003 Wojtek Kaniewski <wojtekka@irc.pl>
 *		  2004 Piotr Kupisiewicz <deli@rzepaknet.us>
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

#ifndef __EKG_WINDOWS_H
#define __EKG_WINDOWS_H

#include "ekg2-config.h"

#include <time.h>

#ifdef HAVE_REGEX_H
#include <sys/types.h>
#include <regex.h>
#endif

#include "sessions.h"
#include "themes.h"

typedef struct {
	void *w;			/* window, if NULL it means current */
	int casense		: 2;	/* 0 - ignore case; 1 - don't ignore case, -1 - use global variable */
	unsigned int lock	: 1;	/* if 0, don't update */
	unsigned int isregex	: 1;	/* 1 - in target regexp */
#ifdef HAVE_REGEX_H
	regex_t reg;			/* regexp compilated expression */
#endif
	char *expression;		/* expression */
} window_lastlog_t;

typedef struct window {
	struct window *next;

	unsigned short id;		/* ekg2-remote: OK */
	char *target;			/* ekg2-remote: OK */
	char *alias;			/* ekg2-remote: OK */
	session_t *session;		/* ekg2-remote: OK */

	unsigned short left, top;	/* pozycja (x, y) wzgl�dem pocz�tku ekranu */
	unsigned short width, height;	/* wymiary okna */

	unsigned int act	: 2;	/* activity: 1 - status/junk; 2 - msg ; 3 - msg to us */
	unsigned int in_typing	: 1;	/* user is composing a message to us */
	unsigned int in_active	: 1;	/* user has sent some kind of message,
					   so we can start sending composing to him/her */
	unsigned int out_active	: 1;	/* we 'started' sending messages to user (considered
					   ourselves active), so we shall say goodbye when done */
	unsigned int more	: 1;	/* pojawi�o si� co� poza ekranem */
	unsigned int floating	: 1;	/* czy p�ywaj�ce? */
	unsigned int doodle	: 1;	/* czy do gryzmolenia?		[we don't set it anywhere] */

	unsigned int frames	: 4;	/* informacje o ramkach */
	unsigned int edge	: 4;	/* okienko brzegowe */

	unsigned int nowrap	: 1;	/* nie zawijamy linii */
	unsigned int hide	: 1;	/* ukrywamy, bo jest zbyt du�e */

	time_t last_update;		/* czas ostatniego uaktualnienia */
	unsigned short lock;		/* ekg2-remote, always: 0, shoulnd't hurt (only CPU a little) */	/* sometimes... */

	struct userlist *userlist;	/* sometimes window may require separate userlist */

	window_lastlog_t *lastlog;	/* prywatne informacje lastloga */
	void *private;			/* prywatne informacje ui */

/* ekg2-remote ABI */
	char *irctopic;
	char *irctopicby;
	char *ircmode;
} window_t;

typedef enum {
	EKG_WINACT_NONE = 0,		/* No activity in window */
	EKG_WINACT_JUNK,		/* Junks: status change, irc join/part, etc. */
	EKG_WINACT_MSG,			/* Message, but not to us */
	EKG_WINACT_IMPORTANT		/* important message */
} winact_t;

extern window_t *windows;
extern window_t *window_debug;
extern window_t *window_status;
extern window_t *window_current;

extern window_lastlog_t *lastlog_current;

window_t *window_find(const char *target);
window_t *window_find_sa(session_t *session, const char *target, int session_null_means_no_session);

#define window_find_s(s, target) window_find_sa(s, target, 1)	/* XXX, need checking */
window_t *window_find_ptr(window_t *w);
window_t *window_new(const char *target, session_t *session, int new_id);
window_t *remote_window_new(int id, const char *target);
void window_kill(window_t *w);
void remote_window_kill(int id);
void window_switch(int id);
void remote_window_switch(int id);
window_t *window_exist(int id);
void print_window_w(window_t *w, int activity, const char *theme, ...);	/* themes.c */
void remote_print_window(int id, time_t ts, char *data);
char *window_target(window_t *window);

void window_session_set(window_t *w, session_t *new_session);
int window_session_cycle(window_t *w);

void windows_destroy(void);

void windows_lock_all();
void windows_unlock_all();

#endif /* __EKG_WINDOW_H */

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
