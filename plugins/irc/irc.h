/*
 *  (C) Copyright 2004-2005 Michal 'GiM' Spadlinski <gim at skrzynka dot pl>
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

#ifndef __EKG_PLUGINS_IRC_IRC_H
#define __EKG_PLUGINS_IRC_IRC_H

#define DOT(a,x,y,z,error) \
	print_info("__status", z, a, session_name(z), x, y->hostname, y->address, \
			itoa(y->port < 0 ? \
				session_int_get(z, "port") < 0 ? DEFPORT : session_int_get(z, "port") : y->port), \
			itoa(y->family), error ? strerror(error) : "")

#include <ekg/dynstuff.h>
#include <ekg/plugins.h>
#include <ekg/protocol.h>	/* XXX, protocol_uid() */
#include <ekg/sessions.h>
#include <ekg/windows.h>

#include "irc-ssl.h"

/* irc_private->sopt */
enum { USERMODES=0, CHANMODES, _005_PREFIX, _005_CHANTYPES,
	_005_CHANMODES, _005_MODES, _005_CHANLIMIT, _005_NICKLEN, _005_IDCHAN, SERVOPTS };

/* irc_private_t->casemapping values */
enum { IRC_CASEMAPPING_ASCII, IRC_CASEMAPPING_RFC1459, IRC_CASEMAPPING_RFC1459_STRICT, IRC_CASEMAPPING_COUNT };

typedef struct _irc_private_t {
	int fd;				/* connection's fd */
	int autoreconnecting;		/* are we in reconnecting mode now? */
	int resolving;			/* count of resolver threads. */
	list_t bindlist, bindtmplist;
	list_t connlist, conntmplist;

	watch_t *recv_watch;
	watch_t *send_watch;

	char *nick;			/* guess again ? ;> */
	char *host_ident;		/* ident+host */

#ifdef IRC_HAVE_SSL
	unsigned char using_ssl	: 2;	/**< 1 if we're using SSL, else 0 */
	SSL_SESSION ssl_session;	/**< SSL session */
	string_t ssl_buf;
#endif

	list_t people;			/* list of people_t */
	list_t channels;		/* list of people_chan_t */
	list_t hilights;

	char *sopt[SERVOPTS];		/* just a few options from
					 * www.irc.org/tech_docs/005.html
					 * server's response */
	int casemapping;

	list_t awaylog;

	list_t auto_guess_encoding;
	list_t out_recodes;
	list_t recoded_channels;

	void *conv_in;
	void *conv_out;
} irc_private_t;

/* data for private->auto_guess_encoding */
typedef struct {
	void *conv_in;
	void *conv_out;
} conv_in_out_t;

/* data for private->out_recodes */
typedef struct {
	char *name;	/* encoding name */
	void *conv_in;
	void *conv_out;
} out_recodes_t;

/* data for private->recoded_channels */
typedef struct {
	char *name;	/* channel or nick */
	out_recodes_t *recode;
} recoded_channels_t;

typedef struct _irc_awaylog_t {
	char *channame;	/* channel name, (null if priv) */
	char *uid;	/* nickname who wrote to us	*/
	char *msg;	/* msg				*/
	time_t t;	/* time_t when we recv message	*/
} irc_awaylog_t;

#define SOP(x) (j->sopt[x])

/* data for private->people */
typedef struct {
	char *nick;
	char *realname;
	char *host, *ident;
	list_t channels;
} people_t;

/* data for private->channels */
typedef struct {
	char		*name;
	int		syncmode;
	struct timeval	syncstart;
	int		mode;
	char		*topic, *topicby, *mode_str;
	window_t	*window;
	list_t		onchan;
	char		*nickpad_str;
	int		nickpad_len, nickpad_pos;
	int		longest_nick;
	list_t		banlist;
	/* needed ?
	list_t exclist;
	list_t invlist; */
	list_t		acclist;
} channel_t;

/* data for private->people->channels */
typedef struct {
	int mode; /* bitfield  */
	char sign[2];
	channel_t *chanp;
} people_chan_t;

/* structure needed by resolver */
typedef struct {
	session_t *session;
	char *hostname;
	char *address;
	int port;
	int family;
} connector_t;

typedef struct {
	char *session;
	list_t *plist;
	int isbind;
} irc_resolver_t;

#define irc_private(s) ((irc_private_t*) session_private_get(s))

/* DO NOT TOUCH THIS! */
#define IRC4 "irc:"
#define irc_uid(target) protocol_uid("irc", target)

extern plugin_t irc_plugin;

void irc_handle_disconnect(session_t *s, const char *reason, int type);

/* checks if name is in format irc:something
 * checkcon is one of:
 *   name is		   channel   |	nick 
 *   IRC_GC_CHAN	-  channame  |	NULL
 *   IRC_GC_NOT_CHAN	-  NULL      | nickname
 *   IRC_GC_ANY		-  name if it's in proper format [irc:something]
 */
enum { IRC_GC_CHAN=0, IRC_GC_NOT_CHAN, IRC_GC_ANY };

#define irc_write(s, args...) watch_write((s && s->priv) ? irc_private(s)->send_watch : NULL, args);

int irc_parse_line(session_t *s, char *buf, int fd);	/* misc.c */

extern int irc_config_experimental_chan_name_clean;

char *nickpad_string_create(channel_t *chan);
char *nickpad_string_apply(channel_t *chan, const char *str);
char *nickpad_string_restore(channel_t *chan);

char *clean_channel_names(session_t *session, char *channels);

#endif /* __EKG_PLUGINS_IRC_IRC_H */

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
