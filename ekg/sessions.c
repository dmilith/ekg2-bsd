/* $Id$ */

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

#include "ekg2-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "debug.h"
#include "dynstuff.h"
#include "sessions.h"
#include "stuff.h"
#include "themes.h"
#include "userlist.h"
#include "vars.h"
#include "windows.h"
#include "xmalloc.h"

#include "dynstuff_inline.h"
#include "objects.h"

#ifndef HAVE_STRLCPY
#  include "compat/strlcpy.h"
#endif

#include "queries.h"

session_t *sessions = NULL;

static LIST_ADD_COMPARE(session_compare, session_t *) { return xstrcasecmp(data1->uid, data2->uid); }

static __DYNSTUFF_LIST_ADD_SORTED(sessions, session_t, session_compare);	/* sessions_add() */
static __DYNSTUFF_LIST_COUNT(sessions, session_t);				/* sessions_count() */

static LIST_FREE_ITEM(session_param_free_item, session_param_t *) { xfree(data->key); xfree(data->value);  }

static __DYNSTUFF_ADD_BEGINNING(session_vars, session_param_t, NULL);			/* session_vars_add() */
static __DYNSTUFF_DESTROY(session_vars, session_param_t, session_param_free_item);	/* session_vars_destroy() */

session_t *session_current = NULL;

/**
 * session_find_ptr()
 *
 * it's search over sessions list and checks if param @a s is in that list.
 * it's useful for all watch handler, and if programmer was too lazy to destroy watches associated with that
 * session (in private watch data struct) before it gone.
 *
 * @note It's possible to find another session with the same address as old one.. it's rather not possible.. however.
 *	It's better if you use @a session_find() function.. Yeah, i know it's slower.
 *
 * @param s - session to look for.
 *
 * @return It returns @a s if session was found, otherwise NULL.
 */

session_t *session_find_ptr(session_t *s) {
	session_t *sl;

	for (sl = sessions; sl; sl = sl->next) {
		if (sl == s)
			return s;

	}
	return NULL;
}

/**
 * session_find()
 *
 * It's search over sessions list and checks if we have session with uid @a uid
 *
 * @param uid - uid of session you look for
 * @sa session_find_ptr() - If you are looking for smth faster ;) but less reliable.
 *
 * @return It returns pointer to session_t struct of found session, or NULL
 *
 */

session_t *session_find(const char *uid)
{
	session_t *s;

	if (!uid)
		return NULL;

	for (s = sessions; s; s = s->next) {
		if (!xstrcasecmp(s->uid, uid) || (s->alias && !xstrcasecmp(s->alias, uid)))
			return s;
	}

	return NULL;
}

/**
 * session_add()
 *
 * Add session with @a uid to session list.<br>
 * Check by plugin_find_uid() if any plugin can handle this type of session if not return NULL<br>
 * Allocate memory for variables, switch windows.. etc, etc..<br>
 * Emit global <i>SESSION_ADDED</i> plugin which handle this session can alloc memory for his private data<br>
 *
 * @todo See XXX's
 *
 * @param uid - full uid of new session
 *
 * @sa session_remove() - To remove session
 *
 * @return	NULL if none plugin can handle this session uid<br>
 *		allocated session_t struct.
 */

session_t *session_add(const char *uid) {
	plugin_t *pl;

	session_t *s;
	window_t *w;

	if (!uid)
		return NULL;

	if (!(pl = plugin_find_uid(uid))) {		/* search for plugin */
		debug_error("session_add() Invalid UID: %s\n", uid);
		return NULL;
	}
	
	s = xmalloc(sizeof(session_t));
	s->uid		= xstrdup(uid);
	s->status	= EKG_STATUS_AVAIL;	/* note: here we had EKG_STATUS_NA, but some protocol plugins doesn't like EKG_STATUS_NA at connect */
	s->plugin	= pl;
#ifdef HAVE_FLOCK
	s->lock_fd	= -1;
#endif

	sessions_add(s);

	/* XXX, wywalic sprawdzanie czy juz jest sesja? w koncu jak dodajemy sesje.. to moze chcemy sie od razu na nia przelaczyc? */
	if (!window_current->session && (window_current == window_debug || window_current == window_status))
		window_session_set(window_current, s);

	/* XXX, i still don't understand why session_current isn't macro to window_current->session... */
	if (!session_current)
		session_current = s;

	/* session_var_default() */
	if (pl->params) {
		int count, i;

		for (count=0; (pl->params[count].key /* && p->params[count].id != -1 */); count++);	/* count how many _global_ params should have this sessioni */
		s->values		= (char **) xcalloc(count+1, sizeof(char *));			/* alloc memory for it, +1 just in case. */
		s->global_vars_count	= count;							/* save it for future, little helper... */

		/* set variables */
		for (i=0; i < count; i++) {
			const char *key   = pl->params[i].key;
			const char *value = pl->params[i].value;

			s->values[i] = xstrdup(value);

				/* sorry, but to simplify plugin writing we've to assure handler
				 * is never called with nonconnected session */
			if (!xstrcasecmp(key, "statusdescr"))
				continue;
			/* notify plugin, like session_set() do */
			if (pl->params[i].notify) 
				pl->params[i].notify(s, key);
		}
	}

	query_emit_id(NULL, SESSION_ADDED, &(s->uid));		/* It's read-only query, XXX */

	for (w = windows; w; w = w->next) {
/* previous version was unacceptable. So we do now this trick:
 *	userlist (if plugin has one) have been already read by SESSION_ADDED emit. so now, 
 *	we check throught get_uid() if this plugin can handle it.. [userlist must be read, if we have nosession window 
 *	with w->target: "Aga". it's not uid. it's nickname.. so we must search for it in userlist.
 *	it's better idea than what was.. however it's slow and I still want to do it other way.
 */
		if (!w->session && !w->floating && get_uid(s, w->target))
			window_session_set(w, s);
	}

	return s;
}

static LIST_FREE_ITEM(session_free_item, session_t *) {
/* free _global_ session variables */
	array_free_count(data->values, data->global_vars_count);

/* free _local_ session variables */
	session_vars_destroy(&(data->local_vars));

	xfree(data->alias);
	xfree(data->uid);
	xfree(data->descr);
	xfree(data->password);
	xfree(data->last_descr);

	/* free memory like sessions_free() do */
	userlist_free(data);
}

static __DYNSTUFF_LIST_REMOVE_SAFE(sessions, session_t, session_free_item);	/* sessions_remove() */
static __DYNSTUFF_LIST_DESTROY(sessions, session_t, session_free_item);	/* sessions_destroy() */

/**
 * session_remove()
 *
 * Remove session with uid passed in @a uid<br>
 * This function free session params variable and internal<br>
 * session data like alias, current status, descr, password..<br>
 * If sesssion is connected, /disconnect command will be executed with session uid as reason<br>
 * Also it remove watches connected with this session (if watch->is_session && watch->data == s)br>
 * It'll do window->session swapping if needed... and changing current session also.
 * 
 * @bug Possible implementation/idea bug in window_session_cycle() I really don't know if we should change session on this windows...
 *	Maybe swaping is ok... but we really need think about protocol.. Now If we change session from jabber to gg one this window
 *	won't be very useful..
 *
 * @note If plugin allocated memory for session example in s->priv you should
 *	connect to <i>SESSION_REMOVED</i> query event, and free alloced memory
 *	(remember about checking if this is your session) timers and watches
 *	will be automagicly removed.
 *
 * @note Current ekg2 API have got session watches. Use watch_add_session()
 *
 * @note Current ekg2 API have got timer watches. Use timer_add_session()
 *
 * @param uid - uid of session to remove
 *
 * @return	 0 if session was found, and removed.<br>
 *		-1 if session wasn't founded.
 */

int session_remove(const char *uid)
{
	session_t *s;
	window_t *w;
	char *tmp;
	int count;
	list_t l;

	if (!(s = session_find(uid)))
		return -1;
	if (s == session_current)
		session_current = NULL;

	count = sessions_count();

	for (w = windows; w; w = w->next) {
		if (w->session == s) {
			w->session = NULL;
			if (count > 1)
				window_session_cycle(w);
		} 
	}
	
	if (s->connected)
		command_exec_format(NULL, s, 1, ("/disconnect %s"), s->uid);
#ifdef HAVE_FLOCK
	if (s->lock_fd != -1) { /* this shouldn't happen */
		flock(s->lock_fd, LOCK_UN);
		close(s->lock_fd);
			/* XXX: unlink then? */
	}
#endif

/* remove session watches */
	for (l = watches; l; l = l->next) {
		watch_t *w = l->data;

		if (w && w->is_session && w->data == s)
			watch_free(w);

	}

	{
		struct timer *t;

		for (t = timers; t; t = t->next) {
			if (t->is_session && t->data == s)
				t = timers_removei(t);
		}
	}

	tmp = xstrdup(uid);
	query_emit_id(NULL, SESSION_CHANGED);
	query_emit_id(NULL, SESSION_REMOVED, &tmp);
	xfree(tmp);

	sessions_remove(s);
	return 0;
}

PROPERTY_INT_GET(session, status, int)

int session_status_set(session_t *s, status_t status)
{
	int is_xa;

	if (!s)
		return -1;
	if (status == EKG_STATUS_UNKNOWN) /* we shouldn't set our status to unknown ( ; */
		status = EKG_STATUS_AVAIL;

	{
		char *__session = xstrdup(s->uid);
		int __status = status;

		query_emit_id(NULL, SESSION_STATUS, &__session, &__status);

		xfree(__session);
	}

/* if it's autoaway or autoxa */
	if ((is_xa = (status == EKG_STATUS_AUTOXA)) || (status == EKG_STATUS_AUTOAWAY)) {
		char *tmp = (char*) session_get(s, (is_xa ? "auto_xa_descr" : "auto_away_descr"));

	/* save current status/ descr && turn autoaway on */
		if (!s->autoaway) { /* don't overwrite laststatus, if already on aa */
			xfree(s->last_descr);		/* just in case */
			s->last_status	= s->status;
			s->last_descr	= xstrdup(s->descr);
			s->autoaway = 1;
		}
	/* new status */
		s->status = (is_xa ? EKG_STATUS_XA : EKG_STATUS_AWAY);

	/* new descr */
		if (tmp) {
			xfree(s->descr);

			if (xstrchr(tmp, '%')) { /* the New&Better-AutoAway-Markup^TM */
				const char *current_descr = (s->autoaway ? s->last_descr : s->descr);
				char *c, *xbuf, *xc;
				int xm = 0;

				/* following thing is used to count how large buffer do we need
				 * yep, I know that we can waste some space
				 * yep, I know that this would also count %%$, but I don't think we need to care that much
				 * if user does want to use shitty strings, it's his problem */
				for (c = tmp; (c = xstrstr(c, "%$")); c++, xm++);
				xbuf = xmalloc(xstrlen(tmp) + (xm * xstrlen(current_descr)) + 1);
				xc = xbuf;
				xm = 1; /* previously xm was used as %$ counter, now it says if we should copy or skip */

				for (c = tmp; *c; c++) {
					if (*c == '%') {
						switch (*(++c)) {
							case '?': /* write if descr is set */
								xm = !!(current_descr);
								break;
							case '!': /* write if descr isn't set */
								xm = !(current_descr);
								break;
							case '/': /* always write */
								xm = 1;
								break;
							/* do we need to employ some never-write (i.e. comments)? */
							case '$': /* insert current descr */
								if (current_descr) {
		/* Here I use memcpy(), 'cause I already need to get strlen(), and the function itself puts final \0 */
									const int xl = xstrlen(current_descr);
									memcpy(xc, current_descr, xl);
									xc += xl;
								}
								break;
							default: /* other chars, i.e. someone's forgotten to escape % */
								if (xm)
									*(xc++) = '%';
							case '%': /* above, or escaped % */
								if (*c == '\0') /* oops, escaped NULL? ( ; */
									c--; /* else for loop won't break */
								else if (xm)
									*(xc++) = *c;
						}
					} else if (xm) /* normal char */
						*(xc++) = *c;
				}

				*xc = '\0'; /* make sure we end with \0 */
				s->descr = xrealloc(xbuf, strlen(xbuf)+1); /* free unused bytes */
			} else /* no markup, just copy */
				s->descr = xstrdup(tmp);
		}
		return 0;
	}

/* if it's autoback */
	if (status == EKG_STATUS_AUTOBACK) {
	/* set status */
		s->status	= s->last_status ? s->last_status : EKG_STATUS_AVAIL;
	/* set descr */
		if (s->autoaway || s->last_descr) {
			xfree(s->descr);
			s->descr = s->last_descr;
		}

		s->last_status	= 0;
		s->last_descr	= NULL;
		s->autoaway	= 0;
		return 0;
	}

	s->status = status;

/* if it wasn't neither _autoback nor _autoaway|_autoxa, it should be one of valid status types... */
	if (s->autoaway) {	/* if we're @ away, set previous, set lastdescr status & free data */
		s->last_status	= 0;	/* EKG_STATUS_NULL */
		xfree(s->descr);	s->descr	= s->last_descr;	s->last_descr = NULL;
		s->autoaway	= 0;
	}
	return 0;
}

int session_password_set(session_t *s, const char *password)
{
	xfree(s->password);
	s->password = (password) ? base64_encode(password, xstrlen(password)+1) : NULL;	/* XXX: +1? */
	return 0;
}

/** 
 * session_password_get()
 *
 * It's decrypt ,,encrypted''(and return) using base64 from session struct (s->password) is @a s was passed, otherwise
 * it cleanup decrypted password from internal buffer.
 *
 * @note static buffer is a better idea - i think (del)
 * @sa base64_decode() - function to decode base64 strings.
 * @sa session_get() - session_get() can be used to get password also... but it's just a wrapper with several xstrcmp() to this function
 *		so it's slower. Instead of using session_get(session, "password") it's better If you use: session_password_get(session) 
 *
 * @param s - session of which we want get password from or NULL if we want to erase internal buf with password
 *
 * @return	,,decrypted'' password if succeed<br>
 *		otherwise ""<br>
 *		or even NULL if @a s was NULL
 */

const char *session_password_get(session_t *s)
{
	static char buf[100];
	char *tmp;

	if (!s) {
		memset(buf, 0, sizeof(buf));
		return NULL;
	}
	
	tmp = base64_decode(s->password);

	if (!tmp)
		return "";
	
	strlcpy(buf, tmp, sizeof(buf));
	xfree(tmp);
	
	return buf;
}


PROPERTY_STRING_GET(session, descr)

int session_descr_set(session_t *s, const char *descr)
{
	if (!s)
		return -1;
	
	if (s->autoaway) {
		xfree(s->last_descr);
		s->last_descr = xstrdup(descr);
	} else {
		xfree(s->descr);
		s->descr = xstrdup(descr);
	}
	
	return 0;
}

PROPERTY_STRING(session, alias)
PROPERTY_PRIVATE(session)
PROPERTY_INT_GET(session, connected, int)

int session_connected_set(session_t *s, int connected)
{
	if (!s)
		return -1;

	s->connected = connected;

	return 0;
}

PROPERTY_STRING_GET(session, uid)

/* Splits statusdescr into status and descr.
 * Returns status as status_t, and sets *statusdescr to descr.
 * Doesn't modify original *statusdescr */
static inline const status_t session_statusdescr_split(const char **statusdescr) {
	const char	*descr	= xstrchr(*statusdescr, ' ');
	char		*status	= NULL;
	status_t	nstatus;

	if (descr) {
		status	= xstrndup(*statusdescr, (descr - *statusdescr));
		descr++;
	}

	nstatus			= ekg_status_int(status ? status : *statusdescr);
	*statusdescr	= descr;

	if (descr)
		xfree(status); /* was duplicated */

	return nstatus;
}

static inline status_t session_status_nearest(session_t *s, status_t status) {
	plugin_t			*p	= s->plugin;
	struct protocol_plugin_priv	*pp	= (struct protocol_plugin_priv *)p->priv;
	const status_t			*ast;
	const int			dir	= (status < EKG_STATUS_AVAIL);

	if (p->pclass != PLUGIN_PROTOCOL) {
		debug_wtf("session_status_nearest(), session '%s' on non-protocol plugin '%s'!\n", session_uid_get(s), p->name);
		return EKG_STATUS_NULL;
	} else if (!p->priv) {
		debug_warn("session_status_nearest(), plugin '%s' doesn't declared supported statuses.\n", p->name);
		return status;
	}

	/* It's really hard to guess intentions
	 * so we currently are going 'towards' EKG_STATUS_AVAIL
	 * i.e. if status < EKG_STATUS_AVAIL, we're increasing it,
	 * else we're decreasing it */

	for (; dir ? (status <= EKG_STATUS_AVAIL) : (status >= EKG_STATUS_AVAIL); dir ? status++ : status--) {
		if (status <= EKG_STATUS_NA || status >= EKG_STATUS_LAST) continue;

		for (ast = pp->statuses; ast && (*ast != EKG_STATUS_NULL); ast++) {
			if (*ast == status)	/* is supported? */
				return status;
		}
	}

	return EKG_STATUS_NULL;
}

/* Just simple setter for both status&descr
 * The real magic should be done in plugin-defined notify handler */
static const int session_statusdescr_set(session_t *s, const char *statusdescr) {
	const char	*descr	= statusdescr;
	status_t	status	= session_statusdescr_split(&descr);
	const char	*label;

	debug_function("session_statusdescr_set(), status = %s [%d], descr = %s\n",
			ekg_status_string(status, 2), status, descr);

	if (status == EKG_STATUS_NULL) return -1;	/* if incorrect status, don't do anything! */
	status = session_status_nearest(s, status);	/* check whether plugin supports this status, if not find nearest */
	if (status == EKG_STATUS_NULL) {
		debug_function("session_statusdescr_set(), status setting on session '%s' not supported\n", session_uid_get(s));
		return -1;
	}

	session_status_set(s, status);
	session_descr_set(s, descr);
	session_unidle(s);
	ekg_update_status(s);

		/* ok, we suck even more. formats for statuschanges are command-based. */
	label = ekg_status_string(status, 1);
	debug_function("session_statusdescr_set(), -> status = %s [%d], label = %s\n",
			ekg_status_string(status, 2), status, label);

	if (descr) {
		char l[xstrlen(label)+7];
		sprintf(l, "%s_descr", label);
		print(l, descr, "", session_name(s));
	} else
		print(label, session_name(s));

	return 0;
}

/* 
 * session_localvar_find()
 * 
 * it looks for given _local_ var in given session
 */
session_param_t *session_localvar_find(session_t *s, const char *key) {
	session_param_t *pl;

	if (!s)
		return NULL;

	for (pl = s->local_vars; pl; pl = pl->next) {
		session_param_t *v = pl;
		if (!xstrcmp(v->key, key)) 
			return v;
	}

	return NULL;
}

static plugins_params_t *PLUGIN_VAR_FIND_BYID(plugin_t *plugin, int id) { return id ? &(((plugin_t *) plugin)->params[id-1]) : NULL; }

/*
 * session_get()
 *
 * pobiera parametr sesji.
 */
const char *session_get(session_t *s, const char *key) {
	session_param_t *sp;
	variable_t *v;
	int paid;		/* it's plugin_params id, not paid */

	if (!s)
		return NULL;
	
	if (!xstrcasecmp(key, "uid"))
		return session_uid_get(s);

	if (!xstrcasecmp(key, "alias"))
		return session_alias_get(s);

	if (!xstrcasecmp(key, "descr"))
		return session_descr_get(s);

	if (!xstrcasecmp(key, "status"))
		return ekg_status_string(session_status_get(s), 2);

	if (!xstrcasecmp(key, "statusdescr"))
		return NULL; /* XXX? */
	
	if (!xstrcasecmp(key, "password"))
		return session_password_get(s);

/* _global_ session variables */
	if ((paid = plugin_var_find(s->plugin, key))) {
/*		debug("session_get() CHECK [%s, %d] value: %s\n", PLUGIN_VAR_FIND_BYID(s->plugin, paid)->key, paid-1, s->values[paid-1]); */
		return s->values[paid-1];
	}

/* _local_ session variables */

	if ((sp = session_localvar_find(s, key)))
		return sp->value;

	if (!(v = variable_find(key)) || (v->type != VAR_INT && v->type != VAR_BOOL))
		return NULL;
	
	return itoa(*(int*)(v->ptr));
}

/*
 * session_int_get()
 *
 * pobiera parametr sesji jako liczb�.
 */
int session_int_get(session_t *s, const char *key)
{
	const char *tmp = session_get(s, key);

	if (!tmp)
		return -1;

	return strtol(tmp, NULL, 0);
}
/* 
 * session_is_var()
 *
 * checks if given variable is correct for given session
 */
int session_is_var(session_t *s, const char *key)
{
	const char *intvars[] = { "alias", "descr", "status", "password", "statusdescr", NULL };
	const char **intvar;

	if (!s)
		return -1;

		/* XXX: maybe move this above !s? */
	for (intvar = intvars; *intvar; intvar++)
		if (!xstrcasecmp(key, *intvar))
			return 1;

/* so maybe _global_ session variable? */
	return (plugin_var_find(s->plugin, key) != 0);
}

/*
 * session_set()
 *
 * ustawia parametr sesji.
 */
int session_set(session_t *s, const char *key, const char *value) {
	session_param_t *v;
	plugins_params_t *pa;
	int paid;
	
	int ret = 0;

	if (!s)
		return -1;

	paid = plugin_var_find(s->plugin, key);
	pa = PLUGIN_VAR_FIND_BYID(s->plugin, paid);

	if (!xstrcasecmp(key, "uid"))
		return -1;

	if (!xstrcasecmp(key, "alias")) {
		char *tmp;
		
		ret = session_alias_set(s, value);

		/* note:
		 * 	if we unset session alias, than value is NULL
		 * 	some code in metacontacts and remote plugin
		 * 	rely on if they can find session, session_find(NULL) will always return NULL :(
		 *
		 * 	I think it'll be better if we always pass s->uid
		 * 	but for now this is enough for me
		 */

		tmp = xstrdup((value) ? value : s->uid);
		query_emit_id(NULL, SESSION_RENAMED, &tmp);
		xfree(tmp);

		goto notify;
	}

	if (!xstrcasecmp(key, "descr")) {
		ret = session_descr_set(s, value);
		goto notify;
	}

	if (!xstrcasecmp(key, "status")) {
		ret = session_status_set(s, ekg_status_int(value));
		goto notify;
	}

	if (!xstrcasecmp(key, "statusdescr")) {
		ret = session_statusdescr_set(s, value);

		if (ret != 0 || !session_connected_get(s))		/* temporary workaround, see XXX @ notify: + don't notify when not connected */
			return ret;
		goto notify;
	}

	if (!xstrcasecmp(key, "password")) {
		if (s->connected && !session_get(s, "__new_password"))
			print("session_password_changed", session_name(s));
		ret = session_password_set(s, value);
		goto notify;
	}

	if (paid) {
/*		debug("session_set() CHECK [%s, %d] value: %s\n", pa->key, paid-1, value);  */

		xfree(s->values[paid-1]);	s->values[paid-1] = xstrdup(value);
		goto notify;
	}

	if ((v = session_localvar_find(s, key))) {
		xfree(v->value);
		v->value = xstrdup(value);
		return 0;
	}

	v = xmalloc(sizeof(session_param_t));
	v->key = xstrdup(key);
	v->value = xstrdup(value);

	session_vars_add(&s->local_vars, v);
	return 0;

notify:
	if (pa && pa->notify)		/* XXX: notify only when ret == 0 ? */
		pa->notify(s, key);

	return ret;
}

/*
 * session_int_set()
 *
 * ustawia parametr sesji jako liczb�.
 */
int session_int_set(session_t *s, const char *key, int value)
{
	return session_set(s, key, itoa(value));
}

/*
 * session_read()
 *
 * czyta informacje o sesjach z pliku.
 */
int session_read(const char *filename) {
	char *line;
	FILE *f;
	session_t *s = NULL;
	int ret = 0;

	if (!filename) {
		plugin_t *p;

		if (!in_autoexec) {
			session_t *sf;

			for (sf = sessions; sf; sf = sf->next)
				command_exec(NULL, sf, ("/disconnect"), 1);
			sessions_free();
			debug("	 flushed sessions\n");
		}

		for (p = plugins; p; p = p->next) {
			const char *tmp;

			if (!p || p->pclass != PLUGIN_PROTOCOL)
				continue;

			if ((tmp = prepare_pathf("sessions-%s", p->name)))
				ret = session_read(tmp);
		}
		return ret;
	}

	if (!(f = fopen(filename, "r"))) {
		debug("Error opening file %s\n", filename);
		return -1;
	}

	while ((line = read_file(f, 0))) {
		char *tmp;

		if (line[0] == '[') {
			tmp = xstrchr(line, ']');

			if (!tmp)
				continue;

			*tmp = 0;
			s = session_add(line + 1);

			continue;
		}

		if ((tmp = xstrchr(line, '='))) {
			*tmp = 0;
			tmp++;
			if (!session_is_var(s, line)) {
				debug("\tSession variable \"%s\" is not correct\n", line);
				continue;
			}
			xstrtr(tmp, '\002', '\n');
			if(*tmp == '\001') { 
				char *decoded = base64_decode(tmp + 1);
				session_set(s, line, decoded);
				xfree(decoded);
			} else 
				session_set(s, line, tmp);
		}
	}

	fclose(f);
	return ret;
}

/*
 * session_write()
 *
 * writes information about sessions in files
 */
int session_write()
{
	plugin_t *p;
	FILE *f = NULL;
	int ret = 0;

	if (!prepare_path(NULL, 1))	/* try to create ~/.ekg2 */
		return -1;

	for (p = plugins; p; p = p->next) {
		session_t *s;
		const char *tmp;

		if (p->pclass != PLUGIN_PROTOCOL) continue; /* skip no protocol plugins */

		if (!(tmp = prepare_pathf("sessions-%s", p->name))) {
			ret = -1;
			continue;
		}
		
		if (!(f = fopen(tmp, "w"))) {
			debug("Error opening file %s\n", tmp);
			ret = -1;
			continue;
		}

		fchmod(fileno(f), 0600);
		fprintf(f, "# vim:fenc=%s\n", config_console_charset);
		for (s = sessions; s; s = s->next) {
			int i;

			if (s->plugin != p)
				continue;

			userlist_write(s);
			fprintf(f, "[%s]\n", s->uid);
			if (s->alias)
				fprintf(f, "alias=%s\n", s->alias);
			if (s->status && config_keep_reason != 2)
				fprintf(f, "status=%s\n", ekg_status_string(s->autoaway ? s->last_status : s->status, 0));
			if (s->descr && config_keep_reason) {
				char *myvar = (s->autoaway ? s->last_descr : s->descr);
				xstrtr(myvar, '\n', '\002');
				fprintf(f, "descr=%s\n", myvar);
				xstrtr(myvar, '\002', '\n');
			}
			if (s->password && config_save_password)
				fprintf(f, "password=\001%s\n", s->password);

			if (!p->params) 
				continue;
		
			for (i = 0; (p->params[i].key /* && p->params[i].id != -1 */); i++) {
				if (!s->values[i]) 
					continue;
				fprintf(f, "%s=%s\n", p->params[i].key, s->values[i]);
			}
			/* We don't save _local_ variables */
		}
		fclose(f);
	}
	return ret;
}

/*
 * session_format()
 *
 * formatuje �adnie nazw� sesji zgodnie z wybranym tematem.
 *
 *  - s - sesja.
 */
const char *session_format(session_t *s)
{
	static char buf[256];
	const char *uid;
	char *tmp;

	if (!s)
		return "";

	uid = s->uid;
/*
	if (xstrchr(uid, ':'))
		uid = xstrchr(uid, ':') + 1;
 */

	if (!s->alias)
		tmp = format_string(format_find("session_format"), uid, uid);
	else
		tmp = format_string(format_find("session_format_alias"), s->alias, uid);
	
	strlcpy(buf, tmp, sizeof(buf));
	
	xfree(tmp);

	return buf;
}

/*
 * session_check()
 *
 * sprawdza, czy dana sesja zawiera prywatne dane pluginu i jest danego
 * protoko�u.
 *
 * 0/1
 */
int session_check(session_t *s, int need_private, const char *protocol)
{
	if (!s)
		return 0;

	if (need_private && !s->priv)
		return 0;

	if (protocol) {
		int plen = xstrlen(protocol);

		if (xstrlen(s->uid) < plen + 1)
			return 0;

		if (strncmp(s->uid, protocol, plen) || s->uid[plen] != ':')
			return 0;
	}

	return 1;
}

/* 
 * session_name()
 * 
 * returns static buffer with formated session name
 */
const char *session_name(session_t *s)
{
	static char buf[150];
	char *tmp = format_string(format_find("session_name"), s ? (s->alias) ? s->alias : s->uid : "?");

	strlcpy(buf, tmp, sizeof(buf));

	xfree(tmp);
	return buf;
}

/*
 * session_unidle()
 *
 * funkcja wywo�ywana, gdy w danej sesji u�ytkownik podj�� jakie� dzia�ania.
 * je�li dla danej sesji zostanie wywo�ana cho� raz ta funkcja, b�dzie ona
 * brana pod uwag� przy autoawayu. po przekroczeniu okre�lonego czasu,
 * zostanie wywo�ana komenda /_autoaway dla tej sesji, a potem /_autoback.
 * nale�y je obs�ugiwa�, inaczej b�d� �mieci na ekranie.
 *
 * 0/-1
 */
int session_unidle(session_t *s)
{
	if (!s)
		return -1;

	s->activity = time(NULL);

	if (s->autoaway)
		command_exec(NULL, s, ("/_autoback"), 0);

	return 0;
}

/*
 * session_command()
 *
 * obs�uga komendy /session
 */
COMMAND(session_command)
{
	session_t *s;

	if (!params[0] || match_arg(params[0], 'l', ("list"), 2)) {
		for (s = sessions; s; s = s->next) {
			const char *descr = (s->connected) ? s->descr : NULL;
			const int status = (s->connected) ? s->status : EKG_STATUS_NA;
			char *tmp;
												/* wtf?  vvvvvv */
			tmp = format_string(format_find(ekg_status_label(status, descr, "user_info_")), "foobar", descr);

			if (!s->alias)
				printq("session_list", s->uid, s->uid, tmp);
			else
				printq("session_list_alias", s->uid, s->alias, tmp);

			xfree(tmp);
		}

		if (!sessions)
			printq("session_list_empty");

		return 0;
	}
	
	if (!xstrcasecmp(params[0], "--dump")) {
		for (s = sessions; s; s = s->next) {
			plugin_t *p = s->plugin;
			session_param_t *pl;
			int i;

			debug("[%s]\n", s->uid);
			if (s->alias)
				debug("alias=%s\n", s->alias);
			if (s->status)
				debug("status=%s\n", ekg_status_string(s->autoaway ? s->last_status : s->status, 0));
			if (s->descr)
				debug("descr=%s\n", (s->autoaway ? s->last_descr : s->descr));

			/*  _global_ vars: */
			if (p) {
				for (i = 0; (p->params[i].key /* && p->params[i].id != -1 */); i++)
					debug("%s=%s\n", p->params[i].key, s->values[i]);
			} else	debug_error("FATAL: [%s] plugin somewhere disappear :(\n", s->uid);

			/* _local_ vars: */
			for (pl = s->local_vars; pl; pl = pl->next) {
				session_param_t *v = pl;

				if (v->value)
					debug("%s=%s\n", v->key, v->value);
			}
		}
		return 0;
	}

	if (match_arg(params[0], 'a', ("add"), 2)) {
		session_t *s;

		if (session_find(params[1])) {
			printq("session_exists", params[1]);
			return -1;
		}

	/* add session, session_add() only fails if there is no plugin handling this uid... so it's ok */
		if (!(s = session_add(params[1]))) {
			printq("invalid_uid", params[1]);
			return -1;
		}

		config_changed = 1;
		
		printq("session_added", s->uid);
		return 0;
	}

	if (match_arg(params[0], 'd', ("del"), 2)) {
	/* remove session, session_remove() only fails (ret -1) if session wasn't found.. so it's ok */
		if (session_remove(params[1])) {
			printq("session_doesnt_exist", params[1]);
			return -1;
		}

		config_changed = 1;
		printq("session_removed", params[1]);

		return 0;
	}

	if (match_arg(params[0], 'w', ("sw"), 2)) {
		session_t *s;
		
		if (!params[1]) {
			printq("invalid_params", name);
			return -1;		
		}
		if (!(s = session_find(params[1]))) {
			printq("session_doesnt_exist", params[1]);
			return -1;
		}
		if (window_current->session == s)
			return 0; /* we don't need to switch to the same session */
		if (window_current->target && (window_current->id != 0))
			command_exec(NULL, NULL, "/window switch 1", 2);

		window_session_set(window_current, s);

		return 0;
	}

	if (match_arg(params[0], 'g', ("get"), 2)) {	/* /session --get [session uid] <variable name> */
		const char *key;	/* variable name */
		const char *var;	/* variable value */
		int paid;		/* `plugin params id`, if it's _global_ session variable */

		char *tmp = NULL;

		if (!params[1]) {
			printq("not_enough_params", name);
			return -1;
		}

		if ((s = session_find(params[1]))) {
			key = params[2];
		} else {
			if (!(s = session)) {
				printq("invalid_session");
				return -1;
			}
			key = params[1];
		}

		if (!key) {
			printq("not_enough_params", name);
			return -1;
		}

		/* emulate session_get() */
		if (!xstrcasecmp(key, "uid"))		var = session_uid_get(s);
		else if (!xstrcasecmp(key, "alias"))	var = session_alias_get(s);
		else if (!xstrcasecmp(key, "descr"))	var = session_descr_get(s);
		else if (!xstrcasecmp(key, "status"))	var = ekg_status_string(session_status_get(s), 2);
		else if (!xstrcasecmp(key, "statusdescr"))	return 0; /* workaround to disable display, XXX? */
		else if (!xstrcasecmp(key, "password")) { var = s->password ? "(...)" : NULL; }
		else if ((paid = plugin_var_find(s->plugin, key))) {
			plugins_params_t *pa = PLUGIN_VAR_FIND_BYID(s->plugin, paid);

			var = s->values[paid-1];
			if (pa->secret)
				var = var ? "(...)" : NULL;
		} else {
		/* XXX, idea, here we can do: session_localvar_find() to check if this is _local_ variable, and perhaps print other info.. 
		 *	The same at --set ? 
		 */
			printq("session_variable_doesnt_exist", session_name(s), key);
			return -1;
		}

		printq("session_variable", session_name(s), key, (var) ? var : (tmp = format_string(format_find("value_none"))));
		xfree(tmp);
		return 0;
	}

	if (match_arg(params[0], 's', ("set"), 2)) {
		
		if (!params[1]) {
			printq("invalid_params", name);
			return -1;
		}	
		
		if (!(s = session_find(params[1]))) {
			if (params[1][0] == '-') {
				if (session) {
					if (!session_is_var(session, params[1] + 1)) {
						printq("session_variable_doesnt_exist", session_name(session), params[1] + 1);
						return -1;
					}
					session_set(window_current->session, params[1] + 1, NULL);
					config_changed = 1;
					printq("session_variable_removed", session_name(session), params[1] + 1);
					return 0;
				} else {
					printq("invalid_session");
					return -1;
				}
			}
		
			if(params[2] && !params[3]) {
				if (session) {
					if (!session_is_var(session, params[1])) {
						printq("session_variable_doesnt_exist", session_name(session), params[1]);
						return -1;
					}
					session_set(session, params[1], params[2]);
					config_changed = 1;
					command_exec_format(NULL, s, 0, ("%s --get %s %s"), name, session->uid, params[1]);
					return 0;
				} else {
					printq("invalid_session");
					return -1;
				}
			}
			
			if(params[2] && params[3]) {
				printq("session_doesnt_exist", params[1]);
				return -1;
			}

			if (!xstrcmp(params[1], "password")) {
				char *prompt	= format_string("session_password_input", session_name(session));
				char *pass	= password_input(prompt, NULL, 1);
				xfree(prompt);

				if (pass) {
					session_set(session, params[1], pass);
					config_changed = 1;
					command_exec_format(NULL, s, 0, ("%s --get %s %s"), name, session->uid, params[1]);
					xfree(pass);
				}
				
				return 0;
			}
			
			printq("invalid_params", name);
			return -1;
		}
		
		if (params[2] && params[2][0] == '-') {
			if (!session_is_var(s, params[2] + 1)) {
				printq("session_variable_doesnt_exist", session_name(session), params[2] + 1);
				return -1;
			}

			session_set(s, params[2] + 1, NULL);
			config_changed = 1;
			printq("session_variable_removed", session_name(s), params[2] + 1);
			return 0;
		}
		
		if(params[2] && params[3]) {
			if (!session_is_var(s, params[2])) {
				printq("session_variable_doesnt_exist", session_name(session), params[2]);
				return -1;
			}

			session_set(s, params[2], params[3]);
			config_changed = 1;
			command_exec_format(NULL, s, 0, ("%s --get %s %s"), name, s->uid, params[2]);
			return 0;
		}
		
		if (!xstrcmp(params[2], "password")) {
			char *prompt	= format_string(format_find("session_password_input"), session->uid);
			char *pass	= password_input(prompt, NULL, 1);
			xfree(prompt);

			if (pass) {
				session_set(session, params[2], pass);
				config_changed = 1;
				command_exec_format(NULL, s, 0, ("%s --get %s %s"), name, session->uid, params[2]);
				xfree(pass);
			}
			
			return 0;
		}

		printq("invalid_params", name);
		return -1;
	}

	if (match_arg(params[0], 'L', "lock", 2)) {
		int fd;
		const char *path;
		session_t *s;

		if (params[1]) {
			if (!(s = session_find(params[1]))) {
				printq("session_doesnt_exist", params[1]);
				return -1;
			}
		} else s = session;

		if (!s) {
			printq("invalid_session");
			return -1;
		}

		if (!config_session_locks) {
/*			printq("var_not_set", name, "session_locks"); */
			return 0;
		}

#ifdef HAVE_FLOCK
		if (config_session_locks == 1 && s->lock_fd != -1) {
			printq("session_locked", session_name(s));
			return -1;
		}
#endif

		if ((path = prepare_pathf("%s-lock", session_uid_get(s))))
			fd = open(path,
					O_CREAT|O_WRONLY
					| (config_session_locks != 1
						? O_EXCL		/* if we don't use flock(), we just take care of file's existence */
						: O_TRUNC
							| O_NONBLOCK	/* if someone's set up shitpipe for us */
					), S_IWUSR);
		else
			return 0;

		if (fd == -1) {
			if (errno == EEXIST) {
				printq("session_locked", session_name(s));
				return -1;
			} else if (errno != ENXIO) {
					/* XXX, be more loud? */
				debug_error("session_command(), lock's open() failed with errno=%d\n", errno);
				return 0;
			}
		}

#ifdef HAVE_FLOCK
		if (config_session_locks == 1) {
			if (flock(fd, LOCK_EX|LOCK_NB) == -1) {
				int flock_errno = errno;

				close(fd);
	
				if (flock_errno == EWOULDBLOCK) {
					printq("session_locked", session_name(s));
					return -1;
				} else {
					debug_error("session_command(), lock's flock() failed with errno=%d\n", flock_errno);
					return 0;
				}
			}
			s->lock_fd = fd;
		} else
#endif
			close(fd);
		/* XXX, info about lock */
		return 0;
	}

	if (match_arg(params[0], 'u', "unlock", 3)) {
#ifdef HAVE_FLOCK
		int fd;
#endif
		const char *path;

		if (!session) {
			printq("invalid_session");
			return -1;
		}

		if (!config_session_locks) {
/*			printq("var_not_set", name, "session_locks"); */
			return 0;
		}

#ifdef HAVE_FLOCK
		if (config_session_locks == 1 && ((fd = session->lock_fd) != -1)) {
			flock(fd, LOCK_UN);
			close(fd);
			session->lock_fd = -1;
		}
#endif

		if ((path = prepare_pathf("%s-lock", session_uid_get(session))))
			unlink(path);
		/* XXX, info about unlock */
		return 0;
	}

	if ((s = session_find(params[0]))) {
		int status;
		char *tmp;
		int i;
		plugin_t *p = s->plugin;

		if (params[1] && params[1][0] == '-') { 
			config_changed = 1;
			command_exec_format(NULL, s, 0, ("%s --set %s %s"), name, params[0], params[1]);
			return 0;
		}

		if(params[1] && params[2]) {
			command_exec_format(NULL, s, 0, ("%s --set %s %s %s"), name, params[0], params[1], params[2]);
			config_changed = 1;
			return 0;
		}
		
		if(params[1]) {
			if (!xstrcmp(params[1], "password"))
				command_exec_format(NULL, s, 0, ("%s --set %s %s"), name, params[0], params[1]);
			else
				command_exec_format(NULL, s, 0, ("%s --get %s %s"), name, params[0], params[1]);
			config_changed = 1;
			return 0;		
		}
		
		status = (!s->connected) ? EKG_STATUS_NA : s->status;
	
		tmp = format_string(format_find(ekg_status_label(status, s->descr, "user_info_")), (s->alias) ? s->alias : "x", s->descr);

		if (!s->alias)
			printq("session_info_header", s->uid, s->uid, tmp);
		else
			printq("session_info_header_alias", s->uid, s->alias, tmp);

		xfree(tmp);

		if (p) {
			tmp = format_string(format_find("value_none"));

			for (i = 0; (p->params[i].key /* && p->params[i].id != -1*/); i++) {
				plugins_params_t *sp = &(p->params[i]);

				if (!xstrcmp(sp->key, "alias"));
				else if (!xstrcmp(sp->key, "password"))
					printq("session_info_param", sp->key, s->password ? "(...)" : tmp);
				else {
					if (sp->secret)
						printq("session_info_param", sp->key, s->values[i] ? "(...)" : tmp);
					else
						printq("session_info_param", sp->key, s->values[i] ? s->values[i] : tmp);
				}
			}
			xfree(tmp);
		} else printq("generic_error", "Internal fatal error, plugin somewhere disappear. Report this bug");

		printq("session_info_footer", s->uid);
		
		return 0;	
	}
	
	if (params[0] && params[0][0] != '-' && params[1] && session && session->uid) {
		command_exec_format(NULL, s, 0, ("%s --set %s %s %s"), name, session_alias_uid(session), params[0], params[1]);
		return 0;
	}
	
	if (params[0] && params[0][0] != '-' && session && session->uid) {
		command_exec_format(NULL, s, 0, ("%s --%cet %s %s"), name,
				!xstrcmp(params[0], "password") ? 's' : 'g', session_alias_uid(session), params[0]);
		return 0;
	}

	if (params[0] && params[0][0] == '-' && session && session->uid) {
		command_exec_format(NULL, s, 0, ("%s --set %s %s"), name, session_alias_uid(session), params[0]);
		return 0;
	}

	printq("invalid_params", name);
	
	return -1;
}

/* sessions_free()
 *
 * zwalnia wszystkie dost�pne sesje
 */
void sessions_free() {
	session_t *s;

	struct timer *t;
	window_t *wl;
	list_t l;

	if (!sessions)
		return;

/* remove _ALL_ session watches */
	for (l = watches; l; l = l->next) {
		watch_t *w = l->data;

		if (w && w->is_session)
			watch_free(w);
	}

	for (t = timers; t; t = t->next) {
		if (t->is_session)
			t = timers_removei(t);
	}

/* it's sessions, not 'l' because we emit SESSION_REMOVED, which might want to search over sessions list...
 * This bug was really time-wasting ;(
 */
/* mg: I modified it so it'll first emit all the events, and then start to free everything
 * That shouldn't be a problem, should it? */
	for (s = sessions; s; s = s->next) {
		query_emit_id(s->plugin, SESSION_REMOVED, &(s->uid));	/* it notify only that plugin here, to free internal data. 
									 * ui-plugin already removed.. other plugins @ quit.
									 * shouldn't be aware about it. too...
									 * XXX, think about it?
									 */
	}

	for (wl = windows; wl; wl = wl->next)
		wl->session = NULL;

	sessions_destroy();
	session_current = NULL;
	window_current->session = NULL;
}

/*
 * session_help()
 *
 * it shows help about variable from ${datadir}/ekg/plugins/{plugin_name}/
 * session.txt
 *
 * s - session
 * name - name of the variable
 */
void session_help(session_t *s, const char *name)
{
	FILE *f;
	char *line, *type = NULL, *def = NULL, *tmp;
	char *plugin_name;

	string_t str;
	int found = 0;
	int sessfilnf = 0;

	if (!s)
		return;

	if (!session_is_var(s, name)) {
		/* XXX, check using session_localvar_find() if this is _local_ variable */ 
		print("session_variable_doesnt_exist", session_name(s), name);
		return;
	}

	plugin_name = ((plugin_t *) s->plugin)->name;

	do {
		/* first try to find the variable in plugins' session file */
		if (!(f = help_path("session", plugin_name))) {
			sessfilnf = 1;
			break;
		}

		while ((line = read_file_iso(f, 0))) {
			if (!xstrcasecmp(line, name)) {
				found = 1;
				break;
			}
		}
	} while(0);

	if (!found) {
		do {
			/* then look for them inside global session file */
			if (!sessfilnf)
				fclose(f);
			
			if (!(f = help_path("session", NULL)))
				break;

			while ((line = read_file_iso(f, 0))) {
				if (!xstrcasecmp(line, name)) {
					found = 1;
					break;
				}
			}
		} while (0);
	}


	if (!found) {
		if (f)
			fclose(f);
		if (sessfilnf)
			print("help_session_file_not_found", plugin_name);
		else
			print("help_session_var_not_found", name);
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

	print("help_session_header", session_name(s), name, type, def);

	xfree(type);
	xfree(def);

	if (tmp)		/* je�li nie jest to ukryta zmienna... */
		read_file_iso(f, 0);	/* ... pomijamy lini� */

	str = string_init(NULL);
	while ((line = read_file_iso(f, 0))) {
		if (line[0] != '\t')
			break;

		if (!xstrncmp(line, ("\t- "), 3) && str->len != 0) {
			print("help_session_body", str->str);
			string_clear(str);
		}

		if (line[0] == '\t' && line[1] == '\0') {	/* if it's \t\0, than add new line */
			string_append(str, ("\n\r"));
			continue;
		}

		string_append(str, line + 1);

		if (line[xstrlen(line) - 1] != ' ')
			string_append_c(str, ' ');
	}

	if (str->len != 0)	/* if we have smth in str */
		print("help_session_body", str->str);		/* display it */

	string_free(str, 1);

	if (xstrcmp(format_find("help_session_footer"), ""))
		print("help_session_footer", name);

	fclose(f);
}

/**
 * changed_session_locks() is called whenever 'session_locks' variable changes it's value.
 * 
 * It should cleanup old locks and reinit new, if needed.
 */
void changed_session_locks(const char *varname) {
	session_t *s;

#ifdef HAVE_FLOCK
	if (config_session_locks != 1) {
			/* unlock all files, close fds */
		for (s = sessions; s; s = s->next) {
			if (s->lock_fd != -1) {
				flock(s->lock_fd, LOCK_UN);
				close(s->lock_fd);
				s->lock_fd = -1;
			}
		}
	}
#endif

	if (!config_session_locks) {
			/* unlink all lockfiles */
		for (s = sessions; s; s = s->next) {
			if (s->connected) { /* don't break locks of other copy of ekg2 */
				const char *path = prepare_pathf("%s-lock", session_uid_get(s));
				if (path)
					unlink(path);
			}
		}
	} else {
			/* lock all connected sessions */
		for (s = sessions; s; s = s->next) {
			if (s->connected
#ifdef HAVE_FLOCK
					&& ((config_session_locks != 1) || (s->lock_fd == -1))
#endif
					)
				command_exec(NULL, s, "/session --lock", 1);
		}
	}

}

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
