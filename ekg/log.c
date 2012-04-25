/* $Id$ */

/*
 *  (C) Copyright 2001-2003 Wojtek Kaniewski <wojtekka@irc.pl>
 *			    Robert J. Wo�ny <speedy@ziew.org>
 *			    Pawe� Maziarz <drg@o2.pl>
 *			    Dawid Jarosz <dawjar@poczta.onet.pl>
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

#include <sys/types.h>
#include <time.h>

#include "dynstuff.h"
#include "dynstuff_inline.h"
#include "log.h"
#include "xmalloc.h"

struct last *lasts = NULL;

int config_last_size = 10;
int config_last = 0;

static LIST_FREE_ITEM(list_last_free, struct last *) { xfree(data->uid); xfree(data->message); }

DYNSTUFF_LIST_DECLARE_WC(lasts, struct last, list_last_free,
	static __DYNSTUFF_LIST_ADD,			/* lasts_add() */
	static __DYNSTUFF_LIST_REMOVE_ITER,		/* lasts_removei() */
	__DYNSTUFF_LIST_DESTROY,			/* lasts_destroy() */
	static __DYNSTUFF_LIST_COUNT)			/* lasts_count() */

/*
 * last_add()
 *
 * dodaje wiadomo�� do listy ostatnio otrzymanych.
 * 
 *  - type - rodzaj wiadomo�ci,
 *  - uid - nadawca,
 *  - t - czas,
 *  - st - czas nadania,
 *  - msg - tre�� wiadomo�ci.
 */
void last_add(int type, const char *uid, time_t t, time_t st, const char *msg) {
	struct last *ll;
	int count = 0;

	/* nic nie zapisujemy, je�eli user sam nie wie czego chce. */
	if (config_last_size <= 0)
		return;
	
	if (config_last & 2) 
		count = last_count(uid);
	else
		count = lasts_count();
				
	/* usuwamy ostatni� wiadomo��, w razie potrzeby... */
	if (count >= config_last_size) {
		time_t tmp_time = 0;
		
		/* najpierw j� znajdziemy... */
		for (ll = lasts; ll; ll = ll->next) {
			if (config_last & 2 && xstrcasecmp(ll->uid, uid))
				continue;

			if (!tmp_time)
				tmp_time = ll->time;
			
			if (ll->time <= tmp_time)
				tmp_time = ll->time;
		}
		
		/* ...by teraz usun�� */
		for (ll = lasts; ll; ll = ll->next) {
			if (ll->time == tmp_time && !xstrcasecmp(ll->uid, uid)) {
				(void) lasts_removei(ll);
				break;
			}
		}

	}

	ll = xmalloc(sizeof(struct last));
	ll->type = type;
	ll->uid = xstrdup(uid);
	ll->time = t;
	ll->sent_time = st;
	ll->message = xstrdup(msg);
	
	lasts_add(ll);
}

/*
 * last_del()
 *
 * usuwa wiadomo�ci skojarzone z dan� osob�.
 *
 *  - uin - numerek osoby.
 */
void last_del(const char *uid) {
	struct last *ll;

	for (ll = lasts; ll; ll = ll->next) {
		if (!xstrcasecmp(uid, ll->uid))
			ll = lasts_removei(ll);
	}
}

/*
 * last_count()
 *
 * zwraca ilo�� wiadomo�ci w last dla danej osoby.
 *
 *  - uin.
 */
int last_count(const char *uid) {
	int count = 0;
	struct last *ll;

	for (ll = lasts; ll; ll = ll->next) {
		if (!xstrcasecmp(uid, ll->uid))
			count++;
	}

	return count;
}

/*
 * It's wrong, I think we could create one function which cover this two function.
 * But I don't have any idea howto
 *
 * However, that ent[] table, like someone already said, was waste of space.
 *
 * Code looks better. (So please don't change to somethink like: if (!ent) q++;) 
 *
 * I was thinking about some static pointer, 
 *	static char buf[2];
 *
 *	buf[0] = znak;
 *	return buf;
 * 
 * to remove if (ent) and always use strcpy() however it's still only idea.
 */

static int xml_escape_l(const char znak) {
	switch (znak) {
		case '"':	return (sizeof("&quot;")-1);
		case '&':	return (sizeof("&amp;")-1);
		case '\'':	return (sizeof("&apos;")-1);
		case '<':	return (sizeof("&lt;")-1);
		case '>':	return (sizeof("&gt;")-1);
	}

	return 1;
}

static const char *xml_escape_c(const char znak) {
	switch (znak) {
		case '"':	return "&quot;";
		case '&':	return "&amp;";
		case '\'':	return "&apos;";
		case '<':	return "&lt;";
		case '>':	return "&gt;";
	}

	return NULL;
}

/*
 * xml_escape()
 *
 *    escapes text to be xml-compliant
 *
 *  - text
 *
 * allocated buffer
 */
char *xml_escape(const char *text) {
	const char *p;
	char *res, *q;
	int len;

	if (!text)
		return NULL;

	for (p = text, len = 0; *p; p++)
		len += xml_escape_l(*p);

	q = res = xmalloc((len + 1)*sizeof(char));

	for (p = text; *p; p++) {
		const char *ent = xml_escape_c(*p);

		if (ent)
			xstrcpy(q, ent);
		else
			*q = *p;

		q += xml_escape_l(*p);

	}

	return res;
}

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
