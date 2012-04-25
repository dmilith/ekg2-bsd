/*
 *  (C) Copyright 2004-2005 Michal 'GiM' Spadlinski <gim at skrzynka dot pl>
 *			Jakub 'darkjames' Zawadzki <darkjames@darkjames.ath.cx>
 *			Wies�aw Ochmi�ski <wiechu@wiechu.com>
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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef NO_POSIX_SYSTEM
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#endif

#include <sys/types.h>

#ifndef NO_POSIX_SYSTEM
#include <sys/socket.h>
#endif

#include <sys/stat.h>
#ifndef __USE_POSIX
#define __USE_POSIX
#endif
#ifndef NO_POSIX_SYSTEM
#include <netdb.h>
#endif

#include <sys/time.h>

#ifndef NO_POSIX_SYSTEM
#include <sys/utsname.h>
#include <pwd.h>
#endif

#ifdef __sun
/* Solaris, thanks to Beeth */
#include <sys/filio.h>
#endif

#include <ekg/commands.h>
#include <ekg/debug.h>
#include <ekg/dynstuff.h>
#include <ekg/log.h>
#include <ekg/net.h>
#include <ekg/protocol.h>
#include <ekg/recode.h>
#include <ekg/sessions.h>
#include <ekg/stuff.h>
#include <ekg/themes.h>
#include <ekg/userlist.h>
#include <ekg/vars.h>
#include <ekg/windows.h>
#include <ekg/xmalloc.h>

#include <ekg/queries.h>

#include "irc.h"
#include "people.h"
#include "input.h"
#include "autoacts.h"

#ifdef IRC_HAVE_OPENSSL
SSL_CTX *ircSslCtx;
#endif

#include "IRCVERSION.h"

#define DEFPORT 6667

#define DEFPARTMSG "EKG2 bejbi! http://ekg2.org/"
#define DEFQUITMSG "EKG2 - It's better than sex!"
#define DEFKICKMSG "EKG2 - Y0U 57iNK2 50 MUCH!"

#define SGPARTMSG(x) session_get(x, "PART_MSG")
#define SGQUITMSG(x) session_get(x, "QUIT_MSG")
#define SGKICKMSG(x) session_get(x, "KICK_MSG")

#define PARTMSG(x,r) (r?r: SGPARTMSG(x)?SGPARTMSG(x):DEFPARTMSG)
#define QUITMSG(x) (SGQUITMSG(x)?SGQUITMSG(x):DEFQUITMSG)
#define KICKMSG(x,r) (r?r: SGKICKMSG(x)?SGKICKMSG(x):DEFKICKMSG)

/* ************************ KNOWN BUGS ***********************
 *  OTHER LESS IMPORTANT BUGS
 *    -> somewhere with altnick sending
 *	G->dj: still not as I would like it to be
 *  !BUGS (?) TODO->check
 *    -> buggy auto_find. if smb type smth on the channel.
 *	  *  10:58:27 ::: Nieprawidowe parametry. Sprobuj help find *
 *************************************************************
 */
/* *************************** TODO **************************
 *
 * -> split mode.
 * -> disconnection detection
 *	 why not just sending simple PING to SERVER and deal with it's
 *	 pong reply...
 *		 PING konstantynopolitanczykiewikowna lublin.irc.pl
 *	 :prefix PONG lublin.irc.pl konstantynopolitanczykiewikowna
 * *************************************************************
 */

/* 
 * IDEAS: 
 *  -> maybe some params in /connect command ?
 *     for example if we have in /session server krakow.irc.pl,poznan.irc.pl
 *     and we want connect to poznan.irc.pl (150.254.64.64) nor krakow... we type
 *     /connect poznan.irc.pl
 *     /connect #2
 *     /connect 150.254.64.64
 *     next, if we want to connect instead of default 6667 port to i.e 6665 we type
 *     /connect :6665
 *     or we can use both.
 *     /connect poznan.irc.pl:6665 || /connect 150.254.64.64 6665.
 */
/*									 *
 * ======================================== STARTUP AND STANDARD FUNCS - *
 *									 */

static int irc_theme_init();
static WATCHER_LINE(irc_handle_resolver);
static COMMAND(irc_command_disconnect);
static int irc_really_connect(session_t *session);
static char *irc_getchan_int(session_t *s, const char *name, int checkchan);
static char *irc_getchan(session_t *s, const char **params, const char *name,
      char ***v, int pr, int checkchan);

static char *irc_config_default_access_groups;
int irc_config_experimental_chan_name_clean;

char fillchars_utf8[] = "\xC2\xA0";
char fillchars_norm[] = "\xA0";
char *fillchars = NULL; 
int fillchars_len = 0; 


PLUGIN_DEFINE(irc, PLUGIN_PROTOCOL, irc_theme_init);

#ifdef EKG2_WIN32_SHARED_LIB
	EKG2_WIN32_SHARED_LIB_HELPER
#endif

/**
 * irc_session_init()
 *
 * Handler for: <i>SESSION_ADDED</i><br>
 * Init private session struct irc_private_t if @a session is irc one.<br>
 * Read saved userlist with userlist_read()
 *
 * @param ap 1st param: <i>(char *) </i><b>session</b> - uid of session
 * @param data NULL
 *
 * @return	0 if @a session is irc one, and we init memory<br>
 *		1 if we don't found such session, or it wasn't irc session <b>[most probable]</b>, or we already init memory.
 */

static QUERY(irc_session_init) {
	char *session = *(va_arg(ap, char**));

	session_t *s = session_find(session);
	irc_private_t *j;

	if (!s || s->priv || (s->plugin != &irc_plugin))
		return 1;

	userlist_read(s);

	ekg_recode_utf8_inc();

	j = xmalloc(sizeof(irc_private_t));
	j->fd = -1;

	j->conv_in = (void *) -1;
	j->conv_out = (void *) -1;
	// xmalloc will do that, so it's not needed
	// j->ssl_buf = NULL;

	s->priv = j;
	return 0;
}

static LIST_FREE_ITEM(list_irc_awaylog_free, irc_awaylog_t *) {
	xfree(data->channame);
	xfree(data->uid);
	xfree(data->msg);
	xfree(data);
}

static LIST_FREE_ITEM(list_irc_resolver_free, connector_t *) {
	xfree(data->address);
	xfree(data->hostname);
	xfree(data);
}

/**
 * irc_session_deinit()
 *
 * Handler for: <i>SESSION_REMOVED</i><br>
 * Free memory allocated by irc_private_t if @a session is irc one.<br>
 * Save userlist with userlist_write()
 *
 * @param ap 1st param: <i>(char *) </i><b>session</b> - uid of session
 * @param data NULL
 *
 * @todo Check if userlist_write() here is good.
 * @todo 
 *
 * @return	0 if @a session is irc one, and memory allocated where xfree()'d.<br>
 *		1 if not such session, or it wasn't irc session <b>[most probable]</b>, or we already free memory.
 */

static QUERY(irc_session_deinit) {
	char *session = *(va_arg(ap, char**));

	session_t *s = session_find(session);
	irc_private_t *j;
	int i;

	if (!s || !(j = s->priv) || (s->plugin != &irc_plugin))
		return 1;

	userlist_write(s);

	s->priv = NULL;

	xfree(j->host_ident);
	xfree(j->nick);

	LIST_DESTROY(j->bindlist, list_irc_resolver_free);
	LIST_DESTROY(j->connlist, list_irc_resolver_free);

	if (j->conv_in != (void*) -1) {
		ekg_convert_string_destroy(j->conv_in);
		ekg_convert_string_destroy(j->conv_out);
	}

	/* XXX, hilights list_t */

	/* NOTE: j->awaylog shouldn't exists here */
	LIST_DESTROY(j->awaylog, list_irc_awaylog_free);

	irc_free_people(s, j);

	for (i = 0; i<SERVOPTS; i++) 
		xfree(j->sopt[i]);

	xfree(j);
	return 0;
}

static char *irc_make_banmask(session_t *session, const char *nick, const char *ident, const char *hostname) {
/* 
 *	  1 (Nick)   - nick!*@*
 *	  2 (User)   - *!*ident@*
 *	  4 (Host)   - *!*@host.*
 *	  4 (IP)     - *!*@*.168.11.11 - buggy, it bans @*.11 
 *	  8 (Domain) - *!*@*.domain.net
 *	  8 (IP)     - *!*@192.168.11.*
 */
	char		*host = xstrdup(hostname);
	const char	*tmp[4];
	char		*temp = NULL;

	int		family = 0; 
	char		ind = '.';
	int		bantype = session_int_get(session, "ban_type");
	
#ifdef HAVE_INET_PTON
	char		buf[33];
	
	if (xstrchr(host, ':')) {
		/* to protect againt iwil var in ircd.conf (ircd-hybrid)
		 *  dot_in_ip6_addr = yes;
		 */ 
		if (host[xstrlen(host)-1] == '.') 
			host[xstrlen(host)-1] = 0;
			
		if (inet_pton(AF_INET6, host, &buf) > 0) {
			family = AF_INET6;
			ind = ':';
		}
	}
	else if (inet_pton(AF_INET, host, &buf) > 0)
		family = AF_INET;
#else
/* TODO */
	print("generic_error", "It seems you don't have inet_pton(). The current version of irc_make_banmask won't work without this function. If you want to get it work faster, contact developers ;>");
#endif

	if (host && !family && (temp=xstrchr(host, ind)))
		*temp = '\0';
	if (host && family && (temp=xstrrchr(host, ind)))
		*temp = '\0';

	if (bantype > 15) bantype = 10;

	memset(tmp, 0, sizeof(tmp));
#define getit(x) tmp[x]?tmp[x]:"*"
	if (bantype & 1) tmp[0] = nick;
	if (bantype & 2 && (ident[0] != '~' || session_int_get(session, "dont_ban_user_on_noident") == 0 )) tmp[1] = ident;
	if (family) {
		if (bantype & 8) tmp[2] = host;
		if (bantype & 4) tmp[3] = hostname ? temp?temp+1:NULL : NULL;
	} else {
		if (bantype & 4) tmp[2] = host;
		if (bantype & 8) tmp[3] = hostname ? temp?temp+1:NULL : NULL;
	}


/*	temp = saprintf("%s!*%s@%s%c%s", getit(0), getit(1), getit(2), ind, getit(3)); */
	temp = saprintf("%s!%s@%s%c%s", getit(0), getit(1), getit(2), ind, getit(3));
	xfree(host);
	return temp;
#undef getit
}

/**
 * irc_print_version()
 *
 * handler for <i>PLUGIN_PRINT_VERSION</i> query requests<br>
 * print info about this plugin [Copyright note+version]
 *
 * @note what the heck this can be ? ;) (c GiM)
 *
 * @return 0
 */

static QUERY(irc_print_version) {
	print("generic", "IRC plugin by Michal 'GiM' Spadlinski, Jakub 'darkjames' Zawadzki v. "IRCVERSION);
	return 0;
}

static QUERY(irc_setvar_default) {
	xfree(irc_config_default_access_groups);
	irc_config_default_access_groups = xstrdup("__ison");
	return 0;
}

static int irc_resolver_sort(void *s1, void *s2) {
	connector_t *sort1 = s1/*, *sort2 = s2*/;
	int prefer_family = AF_INET;
/*	
	if (sort1->session != sort2->session)
		return 0;
*/	
	if (session_int_get(sort1->session, "prefer_family") == AF_INET6) 
		prefer_family = AF_INET6;

/*	debug("%d && %d -> %d\n", sort1->family, sort2->family, prefer_family); */

	if (prefer_family != sort1->family)
		return 1;
	return 0;
}

/**
 * irc_validate_uid()
 *
 * handler for <i>PROTOCOL_VALIDATE_UID</i><br>
 * checks, if @a uid is <i>proper for irc plugin</i>.
 *
 * @note <i>Proper for irc plugin</i> means if @a uid starts with "irc:" and uid len > 4
 *
 * @param ap 1st param: <i>(char *) </i><b>uid</b>  - of user/session/command/whatever
 * @param ap 2nd param: <i>(int) </i><b>valid</b> - place to put 1 if uid is valid for irc plugin.
 * @param data NULL
 *
 * @return	-1 if it's valid uid for irc plugin<br>
 *		 0 if not
 */

static QUERY(irc_validate_uid) {
	char	*uid	= *(va_arg(ap, char **));
	int	*valid	= va_arg(ap, int *);

	if (!uid)
		return 0;

	if (!xstrncasecmp(uid, IRC4, 4) && uid[4]) {
		(*valid)++;
		return -1; /* if it's correct uid for irc we don't need to send to others... */
	}

	return 0;
}

static void irc_changed_resolve(session_t *s, const char *var) {
	irc_private_t *j;
	list_t *rlist;
	int isbind;

	list_t oldlist, tmpoldlist;

	irc_resolver_t *irdata;

	if (!s || !(j = s->priv))
		return;

	isbind = !xstrcmp((char *) var, "hostname");

	if (isbind) {
		oldlist		= j->bindlist;
		tmpoldlist	= j->bindtmplist;

		j->bindtmplist = j->bindlist = NULL;

		rlist = &(j->bindlist);
	} else {
		oldlist		= j->connlist;
		tmpoldlist	= j->conntmplist;

		j->conntmplist = j->connlist = NULL;

		rlist = &(j->connlist);
	}

	j->resolving++;

	irdata = xmalloc(sizeof(irc_resolver_t));
	irdata->session = xstrdup(s->uid);
	irdata->plist	= rlist;
	irdata->isbind = isbind;

	if (!(ekg_resolver4(&irc_plugin, session_get(s, var), (watcher_handler_func_t*) irc_handle_resolver, irdata, 6667, 0, 0))) {
		print("generic_error", strerror(errno));

		j->resolving--;
		
		xfree(irdata->session);
		xfree(irdata);

		if (isbind) {
			j->bindlist = oldlist;
			j->bindtmplist = tmpoldlist;
		} else {
			j->connlist = oldlist;
			j->conntmplist = tmpoldlist;
		}

		return;
	}
	LIST_DESTROY(oldlist, list_irc_resolver_free);
}

out_recodes_t *irc_find_out_recode(list_t rl, char *encname) {
	out_recodes_t *recode;

	if (!(encname && rl))
		return NULL;

	for ( ; rl; rl = rl->next) {
		recode = (out_recodes_t *)(rl->data);
		if ( recode->name && !xstrcasecmp(recode->name, encname) )
			return recode;
	}
	return NULL;
}

recoded_channels_t *irc_find_recode_channel(list_t rcl, char *channame) {
	recoded_channels_t *r_channel;

	if (!(channame && rcl))
		return NULL;

	for ( ; rcl; rcl = rcl->next) {
		r_channel = (recoded_channels_t *)(rcl->data);
		if ( r_channel->name && !xstrcasecmp(r_channel->name, channame) )
			return r_channel;
	}
	return NULL;
}

static char *irc_convert_out(irc_private_t *j, char *recipient, const char *line) {
	char *recoded;
	recoded_channels_t *r_channel;

	if ((j->recoded_channels)) {
		/* channel/nick recode */
		char *channame = (!xstrncasecmp(recipient, IRC4, 4)) ? recipient+4 : recipient;
		if ((r_channel = irc_find_recode_channel(j->recoded_channels, channame))) {
			if ((recoded = ekg_convert_string_p(line, r_channel->recode->conv_out)))
				return recoded;
		}
	}

	/* default recode */
	if (j->conv_out == (void *) -1)
		return NULL;

	if (!(recoded = ekg_convert_string_p(line, j->conv_out)))
		debug_error("[irc] ekg_convert_string_p() failed [%x] using not recoded text\n", j->conv_out);

	return recoded;
}

static void irc_changed_recode_list(session_t *s, const char *var) {
	const char *val;
	irc_private_t *j;
	char **list1, **list2, *nicks, *encoding;
	void *conv_in, *conv_out;
	int i,i2;
	list_t rcl, rl;
	out_recodes_t *recode;
	recoded_channels_t *r_channel;

	if (!s || !(j = s->priv))
		return;

	/* Clean old lists */
	for (rcl = j->recoded_channels; rcl; ) {
		r_channel = rcl->data;
		rcl = rcl->next;
		xfree(r_channel->name);
		list_remove(&rcl, r_channel, 1);
	}
	j->recoded_channels = NULL;

	for (rl = j->out_recodes; rl; ) {
		recode = rl->data;
		rl = rl->next;
		xfree(recode->name);
		ekg_convert_string_destroy(recode->conv_in);
		ekg_convert_string_destroy(recode->conv_out);
		list_remove(&rl, recode, 1);
	}
	j->out_recodes = NULL;

	if (!(val = session_get(s, var)) || !*val)
		return;

	/* Parse list */
	// Syntax: encoding1:nick1,nick2,#chan1,nick3;encoding2:nick4,#chan5,chan6
	list1 = array_make(val, ";", 0, 1, 0);
	for (i=0; list1[i]; i++) {
		if (!(nicks = xstrchr(list1[i], ':'))) {
			debug_error("[irc] recode_list parse error: no colon. Skipped. '%s'\n", list1[i]);
			continue;
		}
		*nicks++ = '\0';
		encoding = list1[i];
		if (!*nicks) {
			debug_error("[irc] recode_list parse error: no nick or channel. Skipped. '%s:'\n", encoding);
			continue;
		}
		if (!*encoding) {
			debug_error("[irc] recode_list parse error: no encoding name. Skipped. ':%s'\n", nicks);
			continue;
		}

		if (!(recode = irc_find_out_recode(j->out_recodes, encoding))) {
			if (!(conv_in = ekg_convert_string_init(encoding, NULL, &(conv_out)))) {
				debug_error("[irc] recode_list error: unknown encoding '%s'\n", encoding);
				continue;
			}
			recode = xmalloc(sizeof(out_recodes_t));
			recode->name = xstrdup(encoding);
			recode->conv_in  = conv_in;
			recode->conv_out = conv_out;
			list_add(&(j->out_recodes), recode);
		}

		list2 = array_make(nicks, ",", 0, 1, 0);
		for(i2=0; list2[i2]; i2++) {
			if ((r_channel = irc_find_recode_channel(j->recoded_channels, list2[i2]))) {
				debug_error("[irc] recode_list. Duplicated channel/nick '%s'. Skipped.'\n", list2[i2]);
				continue;
			}
			r_channel = xmalloc(sizeof(recoded_channels_t));
			r_channel->name = xstrdup(list2[i2]);
			r_channel->recode = recode;
			list_add(&(j->recoded_channels), r_channel);
		}
		array_free(list2);
	}
	array_free(list1);
}

static void irc_changed_recode(session_t *s, const char *var) {
	const char *val;
	irc_private_t *j;
	
	if (!s || !(j = s->priv))
		return;

	if (j->conv_in != (void*) -1) {
		ekg_convert_string_destroy(j->conv_in);
		ekg_convert_string_destroy(j->conv_out);
	}

	if (!(val = session_get(s, var)) || !*val) {
		j->conv_in = (void *) -1;
		j->conv_out = (void *) -1;
		return;
	}

	j->conv_in = ekg_convert_string_init(val, NULL, &(j->conv_out));
}

static void irc_changed_auto_guess_encoding(session_t *s, const char *var) {
	const char *val;
	irc_private_t *j;
	conv_in_out_t *e;
	list_t el;

	if (!s || !(j = s->priv))
		return;
	
	/* Clean old list */
	for (el=j->auto_guess_encoding; el;) {
		e = el->data;
		el = el->next;
		if (e->conv_in != (void*) -1) {
			ekg_convert_string_destroy(e->conv_in);
			ekg_convert_string_destroy(e->conv_out);
		}
		list_remove(&el, e, 1);
	}
	j->auto_guess_encoding = NULL;

	if (!(val = session_get(s, var)) || !*val)
		return;

	char **args;
	args = array_make(val, ",", 0, 1, 0);
	int i;
	for (i=0; args[i]; i++) {
		char *to = NULL;
		char *from = args[i];
		if (!xstrcasecmp(from, config_console_charset)) {
			/* ekg2 can't convert when from and to are the same 
			 * trick: lets convert iso-8859-2 -> iso8859-2; utf8 -> utf-8; etc.
			 */
			char *p = from, *q;
			to = xmalloc(xstrlen(from)+2);
			q = to;
			while ( (*p=tolower(*p)) && (*p>='a') && (*p<='z') )
			    *q++ = *p++;
			if (*p == '-')
			    p++;
			else
			    *q++ = '-';
			while ( (*q++ = *p++) )
			    ;
			*q = '\0';
		}
		e = xmalloc(sizeof(conv_in_out_t));
		e->conv_in = ekg_convert_string_init(from, to, &(e->conv_out));
		if (e->conv_in)
		    list_add(&(j->auto_guess_encoding), e);
		else
		    debug_error("auto_guess_encoding skips unknown '%s' value\n", from);
		xfree(to);
	}

	array_free(args);

}
/*									 *
 * ======================================== HANDLERS ------------------- *
 *									 */

void irc_handle_disconnect(session_t *s, const char *reason, int type)
{
/* 
 * EKG_DISCONNECT_NETWORK @ irc_handle_stream type == 1
 * EKG_DISCONNECT_FAILURE @ irc_handle_connect when we got timeouted or connection refused or other..
 * EKG_DISCONNECT_FAILURE @ irc_c_error(misc.c) when we recv ERROR message @ connecting
 * EKG_DISCONNECT_FAILURE @ irc_command_connect when smth goes wrong.
 * EKG_DISCONNECT_STOPPED @ irc_command_disconnect when we do /disconnect when we are not connected and we try to connect.
 * EKG_DISCONNECT_USER	  @ irc_command_disconnect when we do /disconnect when we are connected.
 */
	irc_private_t	*j = irc_private(s);
	char		*__reason;

	if (!j) {
		debug("[irc_ierror] @irc_handle_disconnect j == NULL");
		return;
	}

	// !!! 
	if (j->send_watch) {
		j->send_watch->type = WATCH_NONE;
		watch_free(j->send_watch);	
		j->send_watch = NULL;
	}
	if (j->recv_watch) {
		watch_free(j->recv_watch);
		j->recv_watch = NULL;
	}
	watch_remove(&irc_plugin, j->fd, WATCH_WRITE);

	close(j->fd);
	j->fd = -1;
	irc_free_people(s, j);

	switch (type) {
		case EKG_DISCONNECT_FAILURE:
			break;
		case EKG_DISCONNECT_STOPPED:
			/* this is just in case somebody would add some servers that have given
			 * port disabled so answer comes very fast and plug would think
			 * he is 'disconnected' while in fact he would try many times to
			 * reconnect to given host
			 */
			j->autoreconnecting = 0;
			/* if ,,reconnect'' timer exists we should stop doing */
			if (timer_remove_session(s, "reconnect") == 0)
				print("auto_reconnect_removed", session_name(s)); 
			break;
			/*
			 * default:
			 *	debug("[irc_handle_disconnect] unknow || !handled type = %d %s", type, reason);
			 */
	}
	__reason  = xstrdup(format_find(reason));
	
	if (!xstrcmp(__reason, "")) {
		xfree(__reason);
		__reason = xstrdup(reason);
	}
	protocol_disconnected_emit(s, __reason, type);
	xfree(__reason);
}

static WATCHER_LINE(irc_handle_resolver) {
	irc_resolver_t *resolv = (irc_resolver_t *) data;
	session_t *s = session_find(resolv->session);
	irc_private_t *j;
	char **p;

	if (!s || !(j = s->priv)) 
		return -1;

	if (type) {
		debug("[irc] handle_resolver for session %s type = 1 !! 0x%x resolving = %d connecting = %d\n", resolv->session, resolv->plist, j->resolving, s->connecting);
		xfree(resolv->session);
		xfree(resolv);
		if (j->resolving > 0)
			j->resolving--;

		if (j->resolving == 0 && s->connecting == 2) {
			debug("[irc] handle_resolver calling really_connect\n");
                        irc_really_connect(s);
		}
		return -1;
	}
/* 
 * %s %s %d %d hostname ip family port\n
 */
	if (!xstrcmp(watch, "EOR")) return -1;	/* koniec resolvowania */
	if ((p = array_make(watch, " ", 3, 1, 0)) && p[0] && p[1] && p[2]) {
		connector_t *listelem = xmalloc(sizeof(connector_t));

		listelem->session = s;
		listelem->hostname = xstrdup(p[0]);
		listelem->address  = xstrdup(p[1]);
		listelem->port	   = (resolv->isbind ? 0 : -1);
		listelem->family   = atoi(p[2]);
		list_add_sorted((resolv->plist), listelem, &irc_resolver_sort);

		debug("%s (%s %s) %x %x\n", p[0], p[1], p[2], resolv->plist, listelem); 
		array_free(p);
	} else { 
		debug("[irc] received some kind of junk from resolver thread: %s\n", watch);
		array_free(p);
		return -1;
	}
	return 0;
}

static WATCHER_SESSION_LINE(irc_handle_stream) {
	irc_private_t *j = NULL;

	if (!s || !(j = s->priv)) {
		debug_error("irc_handle_stream() s: 0x%x j: 0x%x\n", s, j);
		return -1;
	}

	/* ups, we get disconnected */
	if (type == 1) {
		j->recv_watch = NULL;
		/* this will cause  'Removed more than one watch...' */
		debug ("[irc] handle_stream(): ROZ��CZY�O %d %d\n", s->connected, s->connecting);
		
		/* avoid reconnecting when we do /disconnect */
		if (s->connected || s->connecting)
			irc_handle_disconnect(s, NULL, EKG_DISCONNECT_NETWORK);
		return 0;
	}

	/* this shouldn't be like that, it would be better to change
	 * query_connect, so the handler should get char not
	 * const char, so the queries could modify this param,
	 * I'm not sure if this is good idea, just thinking...
	 */
	irc_parse_line(s, (char *)watch, fd);

	return 0;
}


#ifdef IRC_HAVE_SSL
static WATCHER_SESSION(irc_handle_stream_ssl_input) {
#define IRC_SSL_BUFFER 4*1024
	irc_private_t *j = NULL;
	char buf[IRC_SSL_BUFFER], *tmp;
	int len;

	if (!s || !(j = s->priv)) { 
		debug_error("[irc] handle_write_ssl() j: 0x%p\n", j);
		return -1;
	}

	if (! j->using_ssl || ! j->ssl_session) {
		return -1;
	}

	debug("[irc] handle_stream_ssl_input() type: %d\n", type);

	// ATM we never enter here ;/
	if (type == 1) {
		debug ("ok need to do some deinitializaition stuff...\n");

		j->recv_watch = NULL;
		if (s->connected || s->connecting)
			irc_handle_disconnect(s, NULL, EKG_DISCONNECT_NETWORK);
		return 0;
	}

	//do {
		len = SSL_RECV(j->ssl_session, buf, IRC_SSL_BUFFER-1);
		debug("[irc] handle_stream_ssl_input() len: %d\n", len);

#ifdef IRC_HAVE_OPENSSL
		if ((len == 0 && SSL_get_error(j->ssl_session, len) == SSL_ERROR_ZERO_RETURN)) {
			debug("[irc] handle_stream_ssl_input HOORAY got EOF?\n");
			return -1;
		} else if (len < 0)  {
			len = SSL_get_error(j->ssl_session, len);
		}
		/* XXX, When an SSL_read() operation has to be repeated because of SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE, it must be repeated with the same arguments. */
#endif

		/* try reading again */
		if (SSL_E_AGAIN(len)) {
			ekg_yield_cpu();
			debug("[irc] handle_stream_ssl_input yield cpu\n");
			return 0;
		}

		if (len < 0) {
			irc_handle_disconnect(s, SSL_ERROR(len), EKG_DISCONNECT_NETWORK);
			debug("[irc] handle_stream_ssl_input disconnect?!\n");
			return -1;
		}

		buf[len] = 0;
		string_append(j->ssl_buf, buf);

		//debug ("stream: %s\n", buf);

	//} while (1);
	//
	while ((tmp = xstrchr(j->ssl_buf->str, '\n'))) {
		size_t strlen = tmp - j->ssl_buf->str;		/* get len of str from begining to \n char */
		char *line = xstrndup(j->ssl_buf->str, strlen);	/* strndup() str with len == strlen */

		/* we strndup() str with len == strlen, so we don't need to call xstrlen() */
		if (strlen > 1 && line[strlen - 1] == '\r')
			line[strlen - 1] = 0;


		irc_parse_line(s, line, fd);

		string_remove(j->ssl_buf, strlen + 1);
		xfree(line);
	}

	return 0;
}
#endif

/* this is only used for ssl connection */
#ifdef IRC_HAVE_SSL
static WATCHER_LINE(irc_handle_write_ssl) {
	irc_private_t *j = (irc_private_t*)data;
	int res;

	if (!j) { 
		debug_error("[irc] handle_write_ssl() j: 0x%p\n", j);
		return -1;
	}

	if (type == 1) {
		debug ("[irc] handle_write_ssl(): type %d (probably disconnect?)\n", type);
		return 0;
	}

	if (!j->using_ssl) {
		debug_error("[irc] handle_write_ssl() OH NOEZ impossible has become possible!\n");
		j->send_watch = NULL;
		return -1;
	}
	
	res = SSL_SEND(j->ssl_session, watch, xstrlen(watch));
#ifdef IRC_HAVE_OPENSSL		/* OpenSSL */
	if ((res == 0 && SSL_get_error(j->ssl_session, res) == SSL_ERROR_ZERO_RETURN)); /* connection shut down cleanly */
	else if (res < 0) 
		res = SSL_get_error(j->ssl_session, res);
	/* XXX, When an SSL_write() operation has to be repeated because of SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE, it must be repeated with the same arguments. */
#endif

	/* ok do try repeat */
	if (SSL_E_AGAIN(res)) {
		ekg_yield_cpu();
		return 0;
	}

	if (res < 0) {
		print("generic_error", SSL_ERROR(res));
	}

	return res;
}
#endif


static WATCHER(irc_handle_connect_real) {
	session_t *s = (session_t *) data;
	irc_private_t		*j = NULL;
	const char		*real = NULL, *localhostname = NULL;
	char			*pass = NULL;
	int			res = 0; 
	socklen_t		res_size = sizeof(res);

	if (type == 1) {
		debug ("[irc] handle_connect_real(): type %d\n", type);
		return 0;
	}

	if (!s || !(j = s->priv)) { 
		debug_error("[irc] handle_connect_real() s: 0x%x j: 0x%x\n", s, j);
		return -1;
	}

	debug ("[irc] handle_connect_real()\n");

        if (type || getsockopt(fd, SOL_SOCKET, SO_ERROR, &res, &res_size) || res) {
                if (type) debug("[irc] handle_connect_real(): SO_ERROR %s\n", strerror(res));

                /* try next server. */
                /* 'if' because someone can make: /session server blah and /reconnect
                 * during already began process of connecting
                 */
                if (j->conntmplist) {
                        if (!type) DOT("IRC_TEST_FAIL", "Connect", ((connector_t *) j->conntmplist->data), s, res); 
                        j->conntmplist = j->conntmplist->next;
                }
                irc_handle_disconnect(s, (type == 2) ? "Connection timed out" : strerror(res), EKG_DISCONNECT_FAILURE);
                return -1; /* ? */
        }

        timer_remove_session(s, "reconnect");
        DOT("IRC_CONN_ESTAB", NULL, ((connector_t *) j->conntmplist->data), s, 0);

#ifdef IRC_HAVE_SSL
	debug ("will have ssl: %d\n", j->using_ssl);
	if (j->using_ssl) {
	        j->send_watch = watch_add_line(&irc_plugin, fd, WATCH_WRITE_LINE, irc_handle_write_ssl, j);
		string_free(j->ssl_buf, 1);
		j->ssl_buf = string_init(NULL);
		j->recv_watch = watch_add_session(s, fd, WATCH_READ, irc_handle_stream_ssl_input);
	} else {
#endif
		j->send_watch = watch_add_line(&irc_plugin, fd, WATCH_WRITE_LINE, NULL, NULL);
		j->recv_watch = watch_add_session_line(s, fd, WATCH_READ_LINE, irc_handle_stream);
#ifdef IRC_HAVE_SSL
	}
#endif

        real = session_get(s, "realname");
        real = real ? xstrlen(real) ? real : j->nick : j->nick;
        if (j->bindtmplist)	
                localhostname = ((connector_t *) j->bindtmplist->data)->hostname;
        if (!xstrlen(localhostname))
                localhostname = NULL;
        pass = (char *)session_password_get(s);
        pass = xstrlen(strip_spaces(pass))?
                saprintf("PASS %s\r\n", strip_spaces(pass)) : xstrdup("");
        watch_write(j->send_watch, "%sUSER %s %s unused_field :%s\r\nNICK %s\r\n",
                        pass, j->nick, localhostname?localhostname:"12", real, j->nick);
        xfree(pass);

	return -1;
}

#ifdef IRC_HAVE_OPENSSL
static WATCHER(irc_handle_connect_ssl) {
	session_t *s = (session_t *) data;
	irc_private_t *j = NULL;
        int ret;

	if (!s || !(j = s->priv)) { 
		debug_error("[irc] handle_connect_ssl() s: 0x%x j: 0x%x\n", s, j);
		return -1;
	}
	
	debug_error("[irc] handle_connect_ssl() type: %d\n", type);

        if (type == -1) {
                if ((ret = SSL_INIT(j->ssl_session))) {
                        /* XXX, OpenSSL error value XXX */
						debug("SSL_INIT failed\n");
                        print("conn_failed_tls");
                        irc_handle_disconnect(s, SSL_ERROR(ret), EKG_DISCONNECT_FAILURE);
                        return -1;
                }

                if (SSL_SET_FD(j->ssl_session, fd) == 0) {	/* gnutls never fail */
						debug("SSL_SET_FD failed\n");
                        print("conn_failed_tls");
                        SSL_DEINIT(j->ssl_session);
                        j->ssl_session = NULL;
                        irc_handle_disconnect(s, SSL_ERROR(ret), EKG_DISCONNECT_FAILURE);
                        return -1;
                }

                watch_add(&irc_plugin, fd, WATCH_WRITE, irc_handle_connect_ssl, s);
        }

	if (type)
		return 0;

	ret = SSL_HELLO(j->ssl_session);
	if (ret == -1) {
		ret = SSL_get_error(j->ssl_session, ret);

		if (SSL_E_AGAIN(ret)) {
			int direc = SSL_WRITE_DIRECTION(j->ssl_session, ret) ? WATCH_WRITE : WATCH_READ;
			int newfd = SSL_GET_FD(j->ssl_session, fd);

			/* don't create && destroy watch if data is the same... */
			if (newfd == fd && direc == watch) {
				ekg_yield_cpu();
				return 0;
			}

			watch_add(&irc_plugin, fd, direc, irc_handle_connect_ssl, s);
			ekg_yield_cpu();
			return -1;
		} else {
			irc_handle_disconnect(s, SSL_ERROR(ret), EKG_DISCONNECT_FAILURE);
			return -1;
		}
	} /* else */

	debug ("don't be concerned, stick to your daily routine!\n");
	//if ((certret = irc_ssl_cert_verify(j->ssl_session))) {
	//	debug_error("[irc] irc_ssl_cert_verify() %s retcode = %s\n", s->uid, certret);
	//	print("generic2", certret);
	//}

        // handshake successful
        j->using_ssl = 1;
        watch_add(&irc_plugin, fd, WATCH_WRITE, irc_handle_connect_real, s);

        return -1;
}
#endif


static WATCHER(irc_handle_connect) {
	session_t *s = (session_t *) data;
	irc_private_t		*j = NULL;

	if (type == 1) {
		debug ("[irc] handle_connect(): type %d\n", type);
		return 0;
	}

	if (!s || !(j = s->priv)) { 
		debug_error("[irc] handle_connect() s: 0x%x j: 0x%x\n", s, j);
		return -1;
	}

#ifdef IRC_HAVE_OPENSSL
        if (session_int_get(s, "use_ssl")) {
                // irc_handle_connect_ssl() will call irc_handle_connect_real()
                irc_handle_connect_ssl(-1, fd, 0, s);
                return -1;
        }
#endif

        return irc_handle_connect_real(type, fd, watch, data);
}


/*									 *
 * ======================================== COMMANDS ------------------- *
 *									 */
static int irc_build_sin(session_t *s, connector_t *co, struct sockaddr **address) {
	struct sockaddr_in  *ipv4;
	struct sockaddr_in6 *ipv6;
	int len = 0;
	int defport = session_int_get(s, "port");
	int port;

	*address = NULL;

	if (!co) 
		return 0;
	port = co->port < 0 ? defport : co->port;
	if (port < 0) port = DEFPORT;

	if (co->family == AF_INET) {
		len = sizeof(struct sockaddr_in);

		ipv4 = xmalloc(len);

		ipv4->sin_family = AF_INET;
		ipv4->sin_port	 = htons(port);
#ifdef HAVE_INET_PTON
		inet_pton(AF_INET, co->address, &(ipv4->sin_addr));
#else
#warning "irc: You don't have inet_pton() connecting to ipv4 hosts may not work"
#ifdef HAVE_INET_ATON /* XXX */
		if (!inet_aton(co->address, &(ipv4->sin_addr))) {
			debug("inet_aton() failed on addr: %s.\n", co->address);
		}
#else
#warning "irc: You don't have inet_aton() connecting to ipv4 hosts may not work"
#endif
#warning "irc: Yeah, You have inet_addr() connecting to ipv4 hosts may work :)"
		if ((ipv4->sin_addr.s_addr = inet_addr(co->address)) == -1) {
			debug("inet_addr() failed or returns 255.255.255.255? on %s\n", co->address);
		}
#endif

		*address = (struct sockaddr *) ipv4;
	} else if (co->family == AF_INET6) {
		len = sizeof(struct sockaddr_in6);

		ipv6 = xmalloc(len);
		ipv6->sin6_family  = AF_INET6;
		ipv6->sin6_port    = htons(port);
#ifdef HAVE_INET_PTON
		inet_pton(AF_INET6, co->address, &(ipv6->sin6_addr));
#else
#warning "irc: You don't have inet_pton() connecting to ipv6 hosts may not work "
#endif

		*address = (struct sockaddr *) ipv6;
	}
	return len;
}

static int irc_really_connect(session_t *session) {
	irc_private_t		*j = irc_private(session);
	connector_t		*connco, *connvh = NULL;
	struct sockaddr		*sinco,  *sinvh  = NULL;
	int			one = 1, connret = -1, bindret = -1, err;
	int			sinlen, fd;
	int			timeout;
	watch_t			*w;

	if (!j->conntmplist) j->conntmplist = j->connlist;
	if (!j->bindtmplist) j->bindtmplist = j->bindlist;

	if (!j->conntmplist) {
		print("generic_error", "Ziomu� tw�j resolver co� nie tegesuje (!j->conntmplist)");
		return -1;
	}

	j->autoreconnecting = 1;
	connco = (connector_t *)(j->conntmplist->data);
	sinlen = irc_build_sin(session, connco, &sinco);
	if (!sinco) {
		print("generic_error", "Ziomu� tw�j resolver co� nie tegesuje (!sinco)"); 
		return -1;
	}

	if ((fd = socket(connco->family, SOCK_STREAM, 0)) == -1) {
		err = errno;
		debug("[irc] handle_resolver() socket() failed: %s\n",
				strerror(err));
		irc_handle_disconnect(session, strerror(err), EKG_DISCONNECT_FAILURE);
		goto irc_conn_error;
	}
	j->fd = fd;
	debug("[irc] socket() = %d\n", fd);

	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
	if (ioctl(fd, FIONBIO, &one) == -1) {
		err = errno;
		debug("[irc] handle_resolver() ioctl() failed: %s\n",
				strerror(err));
		irc_handle_disconnect(session, strerror(err), EKG_DISCONNECT_FAILURE);
		goto irc_conn_error;
	}

	/* loop, optimize... */
	if (j->bindtmplist) 
		connvh = j->bindtmplist->data;	
	irc_build_sin(session, connvh, &sinvh);
	while (bindret && connvh) {
		DOT("IRC_TEST", "Bind", connvh, session, 0);
		if (connvh->family == connco->family)  {
			bindret = bind(fd, sinvh, sinlen);
			if (bindret == -1)
				DOT("IRC_TEST_FAIL", "Bind", connvh, session, errno);
		}

		if (bindret && j->bindtmplist->next) {
			xfree(sinvh);
			j->bindtmplist = j->bindtmplist->next;
			connvh = j->bindtmplist->data;
			irc_build_sin(session, connvh, &sinvh);
			if (!sinvh)
				continue;
		} else
			break;
	}
	session->connecting = 1;
	DOT("IRC_TEST", "Connecting", connco, session, 0);
	connret = connect(fd, sinco, sinlen);
	debug("connecting: %s %s\n", connco->hostname, connco->address);

	xfree(sinco);
	xfree(sinvh);

	if (connret &&
#ifdef NO_POSIX_SYSTEM
		(WSAGetLastError() != WSAEWOULDBLOCK)
#else
		(errno != EINPROGRESS)
#endif
		) {
		debug("[irc] really_connect control point 1\n");
		err = errno;
		DOT("IRC_TEST_FAIL", "Connect", connco, session, err);
		j->conntmplist = j->connlist->next;
		irc_handle_disconnect(session, strerror(err), EKG_DISCONNECT_FAILURE);
		return -1;

	}
	if (session_status_get(session) == EKG_STATUS_NA)
		session_status_set(session, EKG_STATUS_AVAIL);

	w = watch_add(&irc_plugin, fd, WATCH_WRITE, irc_handle_connect, session);
	if ((timeout = session_int_get(session, "connect_timeout") > 0)) 
		watch_timeout_set(w, timeout);
	
	return 0;
irc_conn_error: 
	xfree(sinco);
	xfree(sinvh);
	return -1;

}

static COMMAND(irc_command_connect) {
	irc_private_t		*j = irc_private(session);
	const char		*newnick;

	if (!session_get(session, "server")) {
		printq("generic_error", "gdzie lecimy ziom ?! [/session server]");
		return -1;
	}
	if (session->connecting) {
		printq("during_connect", session_name(session));
		return -1;
	}
	if (session_connected_get(session)) {
		printq("already_connected", session_name(session));
		return -1;
	}
	if ((newnick = session_get(session, "nickname"))) {
		xfree(j->nick);
		j->nick = xstrdup(newnick);
	} else {
		printq("generic_error", "gdzie lecimy ziom ?! [/session nickname]");
		return -1;
	}
	if (j->resolving) {
		printq("generic", "resolving in progress... you will be connected as soon as possible");
		session->connecting = 2;
		return -1;
	}
	return irc_really_connect(session);
}

static COMMAND(irc_command_disconnect) {
	irc_private_t	*j = irc_private(session);
	const char	*reason = params[0]?params[0]:QUITMSG(session);
	debug("[irc] comm_disconnect() !!!\n");

	if (!session->connecting && !session_connected_get(session) && !j->autoreconnecting) {
		printq("not_connected", session_name(session));
		return -1;
	}

	if (reason && session_connected_get(session))
		watch_write(j->send_watch, "QUIT :%s\r\n", reason);

	if (session->connecting || j->autoreconnecting)
		irc_handle_disconnect(session, reason, EKG_DISCONNECT_STOPPED);
	else
		irc_handle_disconnect(session, reason, EKG_DISCONNECT_USER);

	return 0;
}

static COMMAND(irc_command_reconnect) {
	if (session->connecting || session_connected_get(session)) 
		irc_command_disconnect(name, params, session, target, quiet);
	return irc_command_connect(name, params, session, target, quiet);
}

/*****************************************************************************/

static COMMAND(irc_command_msg) {
	irc_private_t	*j = irc_private(session);
	people_chan_t	*perchn = NULL;
	people_t	*person;
	window_t	*w;

	int		prv = 0; /* NOTICE | PRIVMSG */
	
	char		*mline[2];
	char		*tmpbuf;
	
	int		ischn;	/* IS CHANNEL ? */
	char		prefix[2] = {' ', '\0'};
	char		*head, *coloured;
	const char	*frname; /* formatname */

	int		secure = 0;
	
	char **rcpts;
	char *uid, *sid;

	if (!xstrncmp(target, IRC4, 4)) {
		uid = xstrdup(target);
	} else {
		uid = irc_uid(target);
	}

	w = window_find_s(session, uid);

	prv = xstrcmp(name, ("notice"));
	ischn = !!xstrchr(SOP(_005_CHANTYPES), uid[4]);
/* PREFIX */
	/* ok new irc-find-person checked */
	if ((ischn && (person = irc_find_person(j->people, j->nick)) && (perchn = irc_find_person_chan(person->channels, (char *)uid))))
		prefix[0] = *(perchn->sign);

	if (!ischn || (!session_int_get(session, "SHOW_NICKMODE_EMPTY") && *prefix==' '))
		*prefix='\0';

	frname = format_find(prv?
				ischn?"irc_msg_sent_chan":w?"irc_msg_sent_n":"irc_msg_sent":
				ischn?"irc_not_sent_chan":w?"irc_not_sent_n":"irc_not_sent");

	sid	 = xstrdup(session_uid_get(session));
	rcpts	 = xmalloc(sizeof(char *) * 2);
	rcpts[0] = xstrdup(!!w?w->target:uid);
	rcpts[1] = NULL;

	debug ("%s - %s\n", uid, rcpts[0]);

	tmpbuf	 = (mline[0] = xstrdup(params[1]));
	while ((mline[1] = split_line(&(mline[0])))) {
		char *__msg = NULL;

		char *__mtmp, *padding = NULL;
		int isour = 1;
		int xosd_to_us = 0;
		int xosd_is_priv = !ischn;
		
		if (perchn)
			padding = nickpad_string_apply(perchn->chanp, j->nick);
		head = format_string(frname, session_name(session),
			prefix,	j->nick, j->nick, uid+4, mline[1], padding);
		if (perchn)
			nickpad_string_restore(perchn->chanp);

/* XXX,
 *	Recoding should be done after emiting IRC_PROTOCOL_MESSAGE (?)
 *
 *	wo: Yes! We should use uncoded message for proper encoding in logs.
 */

		if (!(__msg = irc_convert_out(j, uid, mline[1])))
			__msg = xstrdup((const char *)mline[1]);

		coloured = irc_ircoldcolstr_to_ekgcolstr(session, head, 1);

		query_emit_id(NULL, IRC_PROTOCOL_MESSAGE, &(sid), &(j->nick), &mline[1], &isour, &xosd_to_us, &xosd_is_priv, &uid);

		query_emit_id(NULL, MESSAGE_ENCRYPT, &sid, &uid, &__msg, &secure);

		protocol_message_emit(session, session->uid, rcpts, coloured, NULL, time(NULL), (EKG_MSGCLASS_SENT | EKG_NO_THEMEBIT), NULL, EKG_NO_BEEP, secure);

		/* "Thus, there are 510 characters maximum allowed for the command and its parameters." [rfc2812]
		 * yes, I know it's a nasty variable reusing ;)
		 */
		__mtmp = __msg;
		debug ("%s ! %s\n", j->nick, j->host_ident);
		xosd_is_priv = xstrlen(__msg);
		isour = 510 - (prv?7:6) - 6 - xstrlen(uid+4) - xstrlen(j->host_ident) - xstrlen(j->nick);
		/* 6 = 3xspace + '!' + 2xsemicolon; -> [:nick!ident@hostident PRIVMSG dest :mesg] */
		while (xstrlen(__mtmp) > isour && __mtmp < __msg + xosd_is_priv)
		{
			xosd_to_us = __mtmp[isour];
			__mtmp[isour] = '\0';
			watch_write(j->send_watch, "%s %s :%s\r\n", (prv) ? "PRIVMSG" : "NOTICE", uid+4, __mtmp);
			__mtmp[isour] = xosd_to_us;
			__mtmp += isour;
		}
		watch_write(j->send_watch, "%s %s :%s\r\n", (prv) ? "PRIVMSG" : "NOTICE", uid+4, __mtmp);

		xfree(__msg);
		xfree(coloured);
		xfree(head);
	}

	xfree(rcpts[0]);
	xfree(rcpts);
	
	xfree(sid);
	xfree(uid);
	xfree(tmpbuf);

	if (!quiet)
		session_unidle(session);
	return 0;
}

static COMMAND(irc_command_inline_msg) {
	const char	*p[2] = { NULL, params[0] };
	if (!target || !params[0])
		return -1;
	return irc_command_msg(("msg"), p, session, target, quiet);
}

static COMMAND(irc_command_quote) {
	watch_write(irc_private(session)->send_watch, "%s\r\n", params[0]);
	return 0;
}

static COMMAND(irc_command_pipl) {
	irc_private_t	*j = irc_private(session);
	list_t		t1, t2;
	people_t	*per;
	people_chan_t	*chan;

	debug("[irc] this is a secret command ;-)\n");

	for (t1 = j->people; t1; t1=t1->next) {
		per = (people_t *)t1->data;
		debug("(%s)![%s]@{%s} ", per->nick, per->ident, per->host);
		for (t2 = per->channels; t2; t2=t2->next)
		{
			chan = (people_chan_t *)t2->data;
			debug ("%s:%d, ", chan->chanp->name, chan->mode);
		}
		debug("\n");
	}
	return 0;
}

static COMMAND(irc_command_alist) {
	irc_private_t	*j = irc_private(session);
	int isshow = 0;

#if 0
	for (l = j->people; l; l = l->next) {
		people_t	*per = l->data;

		printq("irc_access_known", session_name(session), per->nick+4, per->ident, per->host);
	}
#endif

	if (!params[0] || match_arg(params[0], 'l', "list", 2) || (isshow = match_arg(params[0], 's', "show", 2))) {	/* list, list, show */
#if 0
		list_t l;
		for (l = session->userlist; l; l = l->next) {
			userlist_t *u = l->data;
			/* u->resources; */
		}
#endif
		return 0;
	}

	if (match_arg(params[0], 'a', "add", 2)) {
	/* params[1] - nick!ident@hostname or nickname (in form irc:nick) 
	 * params[2] - options + channels like: +ison +autoop:#linux,#linux.pl +autounban:* +autovoice:* 
	 * ZZZ params[2] - channels: #chan1,#chan2 or '*' 
	 * ZZZ params[3] - options.
	 */

/* /irc:access -a irc:nickname #chan1,#chan2 +autoop +autounban +revenge +ison */

		char *mask = NULL;
		const char *channel;
		char *groupstr;

		userlist_t *u;

		if (!params[1] || !(channel = params[2])) {
			printq("not_enough_params", name);
			return -1;
		}

		if (!xstrncmp(params[1], "irc:", 4)) {	/* nickname */
			list_t l;
			for (l = j->people; l; l = l->next) {
				people_t *per = l->data;

				if (!xstrcmp(per->nick+4, params[1]+4)) {
					/* XXX, here generate mask */
					mask = saprintf("%s!%s@%s", per->nick+4, per->ident, per->host);
					break;
				}
			}
			
			if (!mask) {
				printq("user_not_found", params[1]);
				return -1;
			}
		} else {
			/* XXX verify if mask in params[1] is ok */
			mask = xstrdup(params[1]);
		}



		{
			char *tmp = saprintf("irc:%s:%s", mask, channel);
			u = userlist_add(session, tmp, params[1]);
			if (params[3]) {
				char **arr = array_make(params[3], " ", 0, 1, 1);
				int i;

				for (i=0; arr[i]; i++) {
					const char *value = arr[i];

					if (arr[i][0] == '+')
						value++;

					if (!xstrcmp(value, "autoop"))		ekg_group_add(u, "__autoop");		/* +o */
					else if (!xstrcmp(value, "autovoice"))	ekg_group_add(u, "__autovoice");	/* +v */
					else if (!xstrcmp(value, "autounban"))	ekg_group_add(u, "__autounban");	/* -b */

					else if (!xstrcmp(value, "autoban"))	ekg_group_add(u, "__autoban");		/* +b */
					else if (!xstrcmp(value, "autodevop"))	ekg_group_add(u, "__autodevop");	/* -o, -h, -v */
					else if (!xstrcmp(value, "revenge"))	ekg_group_add(u, "__revenge");		/* + */

					else if (!xstrcmp(value, "ison"))	ekg_group_add(u, "__ison");		/* + */
					else printq("irc_access_invalid_flag", value);
				}
				array_free(arr);
			} else u->groups = group_init(irc_config_default_access_groups);
			xfree(tmp);
		}

		groupstr = group_to_string(u->groups, 1, 1);
		printq("irc_access_added", session_name(session), "0" /* XXX # */, mask, channel, groupstr);
		xfree(groupstr);

		xfree(mask);
	
		/* XXX sync if wanted */
		return 0;
	}

	if (match_arg(params[0], 'd', "delete", 2)) {
		printq("generic_error", "stub function use /del");
		return -1;
	}

	if (match_arg(params[0], 'e', "edit", 2)) {
		printq("generic_error", "stub function");
		return -1;
	}

	if (match_arg(params[0], 'S', "sync", 2)) {
		printq("generic_error", "stub function");
		return -1;
	}

	printq("invalid_params", name);
	return -1;
}

static COMMAND(irc_command_add) {
	printq("generic_error", "/irc:add do nothing. if you want friendlists use /irc:access");
/* XXX, wrapper do /irc:access --add $UID +ison ? */
	return -1;
}

static void irc_display_awaylog(session_t *session) {
	irc_private_t	*j = irc_private(session);

	if (j->awaylog) {
		list_t l;
		const char *awaylog_timestampf = format_find("irc_awaylog_timestamp");

		print_status("irc_awaylog_begin", session_name(session));
		for (l = j->awaylog; l; l = l->next) {
			irc_awaylog_t *e = l->data;

			if (e->channame)
				print_status("irc_awaylog_msg_chan", session_name(session),
					timestamp_time(awaylog_timestampf, e->t), (e->channame)+4, (e->uid)+4, e->msg);
			else
				print_status("irc_awaylog_msg", session_name(session),
					timestamp_time(awaylog_timestampf, e->t), "", (e->uid)+4, e->msg);

			xfree(e->channame);
			xfree(e->uid);
			xfree(e->msg);
		}
		print_status("irc_awaylog_end", session_name(session));

		list_destroy(j->awaylog, 1);
		j->awaylog = NULL;
	}
}

static COMMAND(irc_command_away) {
	irc_private_t	*j = irc_private(session);
	int		isaway = 0;

	if (!xstrcmp(name, ("back"))) {
		session_descr_set(session, NULL);
		session_status_set(session, EKG_STATUS_AVAIL);
		session_unidle(session);
	} else if (!xstrcmp(name, ("away"))) {
		session_descr_set(session, params[0]);
		session_status_set(session, EKG_STATUS_AWAY);
		session_unidle(session);
		isaway = 1;
	} else if (!xstrcmp(name, ("_autoaway"))) {
		session_status_set(session, EKG_STATUS_AUTOAWAY);
		isaway = 1;
	} else if (!xstrcmp(name, ("_autoback"))) {
		session_status_set(session, EKG_STATUS_AUTOBACK);
		session_unidle(session);
	} else {
		printq("generic_error", "Ale o so chozi?");
		return -1;
	}
	if (isaway) {
		const char *status = ekg_status_string(session_status_get(session), 0);
		const char *descr  = session_descr_get(session);
		if (descr)
			watch_write(j->send_watch, "AWAY :%s\r\n", descr);
		else
			watch_write(j->send_watch, "AWAY :%s\r\n", status);
	} else {
		watch_write(j->send_watch, "AWAY :\r\n");

		/* @ back, display awaylog. */
		irc_display_awaylog(session);
	}
	return 0;
}

static void irc_statusdescr_handler(session_t *s, const char *varname) {
	irc_private_t	*j		= irc_private(s);
	const status_t	status	= session_status_get(s);

	if (status == EKG_STATUS_AWAY) {
		const char *descr  = session_descr_get(s);
		if (descr)
			watch_write(j->send_watch, "AWAY :%s\r\n", descr);
		else
			watch_write(j->send_watch, "AWAY :%s\r\n", ekg_status_string(status, 0));
	} else {
		watch_write(j->send_watch, "AWAY :\r\n");

		/* @ back, display awaylog. */
		irc_display_awaylog(s);
	}
}

/*****************************************************************************/

/**
 * irc_window_kill()
 *
 * handler for <i>UI_WINDOW_KILL</i> query requests
 * It checks if window which will be destroyed is valid irc channel window, and we
 * are on it now. if yes (and of course if we are connected) it send PART message to irc
 * server, with reason got using @a PARTMSG macro
 *
 * @param ap 1st param: <i>(window_t *) </i><b>w</b> - window which will be destroyed 
 * @param data NULL
 *
 * @return 0
 */

static QUERY(irc_window_kill) {
	window_t	*w = *va_arg(ap, window_t **);
	irc_private_t	*j;
	char		*tmp;

	if (w && w->id && w->target && w->session && w->session->plugin == &irc_plugin &&
			(j = irc_private(w->session)) &&
			(tmp = SOP(_005_CHANTYPES)) &&
			xstrchr(tmp, (w->target)[4]) &&
			irc_find_channel((j->channels), (w->target)) &&
			session_connected_get(w->session)
			)
	{
		watch_write(j->send_watch, "PART %s :%s\r\n", (w->target)+4, PARTMSG(w->session, NULL));
	}
	return 0;
}

/** 
 * irc_topic_header() 
 *
 * handler for <i>IRC_TOPIC</i> query requests
 * @param ap 1st param: <i>(char *) </i><b>top</b> - place to put:<br>
 *	-> <i>topic of current irc channel</i> (if current window is valid irc channel)<br>
 *	-> <i>ident\@host of current irc user</i> (if current window is known user)
 * @param ap 2nd param: <i>(char *) </i><b>setby</b> - place to put:<br>
 *	-> <i>topic owner of current irc channel</i><br>
 *	-> <i>realname of current irc user</i>
 * @param ap 3rd param: <i>(char *) </i><b>modes</b> - place to put:<br>
 *	<i>modes of current irc channel</i> or <i>undefined if not channel</i>
 * @param data 0 
 *
 * @return	 1 if it's known irc channel.<br>
 *		 2 if it's known irc user.<br>
 *		 0 if it's neither known user, nor channel.<br>
 *		-3 if it's not valid irc window, or session is not connected
 */

static QUERY(irc_topic_header) {
	char **top   = va_arg(ap, char **);
	char **setby = va_arg(ap, char **);
	char **modes = va_arg(ap, char **);

	session_t *sess	= window_current->session;
	char *targ = window_current->target;

	if (targ && sess && sess->plugin == &irc_plugin && sess->priv && sess->connected)
	{ 
		irc_private_t *j = sess->priv;
		char *tmp;
		channel_t *chanp;
		people_t *per;

		/* channel */
		if ((tmp = SOP(_005_CHANTYPES)) && 
		     xstrchr(tmp, targ[4]) && 
		     (chanp = irc_find_channel((j->channels), targ))) 
		{
			*top   = irc_ircoldcolstr_to_ekgcolstr_nf(sess, chanp->topic, 1);
			*setby = xstrdup(chanp->topicby);
			*modes = xstrdup(chanp->mode_str);
			return 1;
		}
		
		/* person */
		/* ok new irc-find-person checked */
		if ((per = irc_find_person((j->people), targ+4))) { 
			*top   = saprintf("%s@%s", per->ident, per->host);
			*setby = xstrdup(per->realname);
			*modes = NULL;
			return 2;
		}

		*top = *setby = *modes = NULL;

		return 0;
	}
	return -3;
}

static char *irc_getchan_int(session_t *s, const char *name, int checkchan)
{
	char		*ret, *tmp;
	irc_private_t	*j = irc_private(s);

	if (!xstrlen(name))
		return NULL;

	if (!xstrncasecmp(name, IRC4, 4))
		ret = xstrdup(name);
	else
		ret = irc_uid(name);

	if (checkchan == 2) 
		return ret;

	tmp = SOP(_005_CHANTYPES);
	if (tmp && ((!!xstrchr(tmp, ret[4]))-checkchan) )
		return ret;
	else
		xfree(ret);
	return NULL;
}

/* pr - specifies priority of check
 *   0 - first param, than current window
 *   1 - reverse
 * as side effect it adjust table passed as v argument
 */
static char *irc_getchan(session_t *s, const char **params, const char *name,
		char ***v, int pr, int checkchan)
{
	char		*chan, *tmpname;
	const char	*tf, *ts, *tp; /* first, second */
	int		i = 0, parnum = 0, argnum = 0, hasq = 0;
	command_t	*c;

	if (params) tf = params[0];
	else tf = NULL;
	ts = window_current->target;

	if (pr) { tp=tf; tf=ts; ts=tp; }

	if (!(chan = irc_getchan_int(s, tf, checkchan))) {
		if (!(chan = irc_getchan_int(s, ts, checkchan)))
		{
			print("invalid_params", name);
			return 0;
		}
		pr = !!pr;
	} else {
		pr = !!!pr;
	}

	tmpname = irc_uid(name);
	for (c = commands; c; c = c->next) {
		if (&irc_plugin == c->plugin && !xstrcmp(tmpname, c->name))
		{
			while (c->params[parnum])
			{
				if (!hasq && !xstrcmp(c->params[parnum], ("?")))
					hasq = 1;
				parnum++;
			}
			break;
		}
	}
	xfree(tmpname);

	do {
		if (params)
			while (params[argnum])
				argnum++;

		(*v) = (char **)xcalloc(parnum+1, sizeof (char *));

		debug("argnum %d parnum %d pr %d hasq %d\n",
				argnum, parnum, pr, hasq);

		if (!pr) {
			if (!hasq)
			{
				for (i=0; i<parnum && i<argnum; i++) {
					(*v)[i] = xstrdup(params[i]);
					debug("  v[%d] - %s\n", i, (*v)[i]);
				}
			} else {
				for (i=0; i < parnum-2 && i<argnum; i++)
				{
					(*v)[i] = xstrdup(params[i]);
					debug("o v[%d] - %s\n", i, (*v)[i]);
				}
				if (params [i] && params[i+1]) {
					(*v)[i] = saprintf("%s %s",
						   params[i], params[i+1]);
					i++;
				} else if (params[i]) {
					(*v)[i] = xstrdup(params[i]);
					i++;
				}
			}
			(*v)[i] = NULL;
		} else {
			for (i=0; i<argnum; i++)
				(*v)[i] = xstrdup(params[i+1]);
		}
		/*
		i=0;
		while ((*v)[i])
			debug ("zzz> %s\n", (*v)[i++]);
		debug("\n");
		*/
	} while (0);

	return chan;
}
/*****************************************************************************/

static COMMAND(irc_command_names) {
	irc_private_t *j = irc_private(session);

			    /* 0 - op           1 - halfop       2 - voice      3 - owner             4 - admin       5 - rest */
	int sort_status[6] = {EKG_STATUS_AVAIL, EKG_STATUS_AWAY, EKG_STATUS_XA, EKG_STATUS_INVISIBLE, EKG_STATUS_FFC, EKG_STATUS_DND};
	int sum[6]   = {0, 0, 0, 0, 0, 0};

	int len;
	char **mp, *channame, *cchn, *pfx0, *pfx1;

	channel_t *chan;
	string_t buf;
	int lvl, count = 0, i;
	userlist_t *ul;

	if (!(channame = irc_getchan(session, params, name, &mp, 0, IRC_GC_CHAN))) 
		return -1;

	if (!(chan = irc_find_channel(j->channels, channame))) {
		printq("generic", "irc_command_names: wtf?");
		return -1;
	}

	if (chan->longest_nick > atoi(SOP(_005_NICKLEN)))
		debug_error("[irc, names] funny %d vs %s\n", chan->longest_nick, SOP(_005_NICKLEN));

	buf = string_init(NULL);

	len = xstrlen(SOP(_005_PREFIX))>>1;
	pfx0 = SOP(_005_PREFIX) + 1;		// point to "ov)@+"
	pfx1 = SOP(_005_PREFIX) + len + 1;	// point to "@+"
	for (i = 0; i < len; i++) {
		static char mode_str[2] = { '?', '\0' };
		const char *mode;

		/* set mode string passed to formatee to proper
		 * sign from modes, or if there are no modes left
		 * set it to formatee letter,
		 * The use of 160 fillerchar will cause that "[ nickname     ]"
		 * won't be splitted like: "[ nickname
		 *	]", and whole will be treated as long 'longest_nick+2' long string :)
		 */

		switch (pfx0[i]) {
			case 'o':	lvl = 0; break;
			case 'h':	lvl = 1; break;
			case 'v':	lvl = 2; break;
			case 'q':	lvl = 3; break;
			case 'a':	lvl = 4; break;
			default:	lvl = 5; break;
		}
		if (pfx1[i]) {
			mode = mode_str;
			mode_str[0] = pfx1[i];
		} else {
			mode = fillchars;
		}

		for (ul = chan->window->userlist; ul; ul = ul->next) {
			userlist_t *ulist = ul;
			char *tmp;

			if (ulist->status != sort_status[lvl])
				continue;

			nickpad_string_apply(chan, ulist->uid+4);
			string_append(buf, (tmp = format_string(format_find("IRC_NAMES"), mode, (ulist->uid + 4), chan->nickpad_str))); xfree(tmp);
			nickpad_string_restore(chan);

			++sum[lvl];
			++count;
		}
	}

	cchn = clean_channel_names(session, channame+4);
	print_info(channame, session, "IRC_NAMES_NAME", session_name(session), cchn);

	if (count)
		print_info(channame, session, "none", buf->str);

	print_info(channame, session, "none2", "");
#define plvl(x) itoa(sum[x])
	if (len > 3) /* has halfops */
		print_info(channame, session, "IRC_NAMES_TOTAL_H", session_name(session), cchn, itoa(count), plvl(0), plvl(1), plvl(2), plvl(5), plvl(3), plvl(4));
	else
		print_info(channame, session, "IRC_NAMES_TOTAL", session_name(session), cchn, itoa(count), plvl(0), plvl(2), plvl(5));
#undef plvl
	xfree(cchn);
	debug("[IRC_NAMES] levelcounts = %d %d %d %d %d %d\n", sum[0], sum[1], sum[2], sum[3], sum[4], sum[5]);

	array_free(mp);
	string_free (buf, 1);
	xfree(channame);
	return 0;
}

static COMMAND(irc_command_topic) {
	irc_private_t	*j = irc_private(session);
	char		**mp, *chan, *newtop, *recode;

	if (!(chan=irc_getchan(session, params, name, 
					&mp, 0, IRC_GC_CHAN))) 
		return -1;

	if (*mp)
		if (xstrlen(*mp)==1 && **mp==':')
			newtop = saprintf("TOPIC %s :\r\n", chan+4);
		else
			newtop = saprintf("TOPIC %s :%s\r\n", 
						chan+4, *mp);
	else
		newtop = saprintf("TOPIC %s\r\n", chan+4);

	if ((recode = irc_convert_out(j, chan+4, newtop))) {
		xfree(newtop);
		newtop = recode;
	}

	watch_write(j->send_watch, "%s", newtop);
	array_free(mp);
	xfree (newtop);
	xfree (chan);
	return 0;
}

static COMMAND(irc_command_who) {
	irc_private_t	*j = irc_private(session);
	char		**mp, *chan;

	if (!(chan=irc_getchan(session, params, name,
					&mp, 0, IRC_GC_CHAN)))
		return -1;

	watch_write(j->send_watch, "WHO %s\r\n", chan+4);

	array_free(mp);
	xfree(chan);
	return 0;
}

static COMMAND(irc_command_invite) {
	irc_private_t	*j = irc_private(session);
	char		**mp, *chan;

	if (!(chan=irc_getchan(session, params, name,
					&mp, 0, IRC_GC_CHAN)))
		return -1;

	if (!(*mp)) {
		printq("not_enough_params", name);
		xfree(chan);
		return -1;
	}
	watch_write(j->send_watch, "INVITE %s %s\r\n", *mp, chan+4);

	array_free(mp);
	xfree(chan);
	return 0;
}

static COMMAND(irc_command_kick) {
	irc_private_t	*j = irc_private(session);
	char		**mp, *chan;

	if (!(chan=irc_getchan(session, params, name,
					&mp, 0, IRC_GC_CHAN))) 
		return -1;

	if (!(*mp)) {
		printq("not_enough_params", name);
		xfree(chan);
		return -1;
	}
	watch_write(j->send_watch, "KICK %s %s :%s\r\n", chan+4, *mp, KICKMSG(session, mp[1]));

	array_free(mp);
	xfree(chan);
	return 0;
}

static COMMAND(irc_command_unban) {
	irc_private_t	*j = irc_private(session);
	char		*channame, **mp;
	channel_t	*chan = NULL;
	list_t		banlist;
	int		i, banid = 0;

	if (!(channame = irc_getchan(session, params, name,
					&mp, 0, IRC_GC_CHAN))) 
		return -1;

	debug("[irc]_command_unban(): chan: %s mp[0]:%s mp[1]:%s\n",
			channame, mp[0], mp[1]);

	if (!(*mp)) {
		printq("not_enough_params", name);
		array_free(mp);
		xfree(channame);
		return -1;
	} else {
		if ( (banid = atoi(*mp)) ) {
			chan = irc_find_channel(j->channels, channame+4);
			if (chan && (banlist = (chan->banlist)) ) {
				for (i=1; banlist && i<banid; banlist = banlist->next, ++i);
				if (banlist) /* fit or add  i<=banid) ? */
					watch_write(j->send_watch, "MODE %s -b %s\r\n", channame+4, banlist->data);
				else 
					debug("%d %d out of range or no such ban %08x\n", i, banid, banlist);
			}
			else
				debug("Chanell || chan->banlist not found -> channel not synced ?!Try /mode +b \n");
		}
		else { 
			watch_write(j->send_watch, "MODE %s -b %s\r\n", channame+4, *mp);
		}
	}
	array_free(mp);
	xfree(channame);
	return 0;

}

static COMMAND(irc_command_ban) {
	irc_private_t	*j = irc_private(session);
	char		*chan, **mp, *temp = NULL;
	people_t	*person;

	if (!(chan=irc_getchan(session, params, name,
					&mp, 0, IRC_GC_CHAN))) 
		return -1;

	debug("[irc]_command_ban(): chan: %s mp[0]:%s mp[1]:%s\n",
			chan, mp[0], mp[1]);

	if (!(*mp))
		watch_write(j->send_watch, "MODE %s +b \r\n", chan+4);
	else {
		/* if parameter to /ban is prefixed with irc: like /ban irc:xxx
		 * we don't care, since this is what user requested ban user with
		 * nickname: 'irc:xxx' [this is quite senseless, since IRC servers
		 * doesn't allow ':' in nickname (an they probably never will)
		 *
		 * calling /ban irc:xxx will cause:
		 *   programmer's mistake in call to irc_find_person!: irc:xxx
		 * in debug window, but please do not report it, according to
		 * what is written above this is normal, DELETE THIS NOTE L8R
		 */
		/* ok new irc-find-person checked */
		person = irc_find_person(j->people, (char *) *mp);
		if (person) 
			temp = irc_make_banmask(session, person->nick+4, person->ident, person->host);
		if (temp) {
			watch_write(j->send_watch, "MODE %s +b %s\r\n", chan+4, temp);
			xfree(temp);
		} else
			watch_write(j->send_watch, "MODE %s +b %s\r\n", chan+4, *mp);
	}
	array_free(mp);
	xfree(chan);
	return 0;
}

static COMMAND(irc_command_kickban) {

	if (!xstrcmp(name, ("kickban")))
	{
		irc_command_kick(("kick"), params, session, target, quiet);
		irc_command_ban(("ban"), params, session, target, quiet);
	} else {
		irc_command_ban(("ban"), params, session, target, quiet);
		irc_command_kick(("kick"), params, session, target, quiet);
	}

	return 0;
}


static COMMAND(irc_command_devop) {
	irc_private_t	*j = irc_private(session);
	int		modes, i;
	char		**mp, *op, *nicks, *tmp, c, *chan, *p;
	string_t	zzz;

	if (!(chan = irc_getchan(session, params, name,
					&mp, 0, IRC_GC_CHAN))) 
		return -1;

	if (!(*mp)) {
		printq("not_enough_params", name);
		xfree(chan);
		return -1;
	}

	modes = atoi(j->sopt[_005_MODES]);
	op = xmalloc((modes+2) * sizeof(char));
		/* H alfop */	/* o P */	/* voice */
	c=xstrchr(name, 'h')?'h':xstrchr(name, 'p')?'o':'v';
	/* Yes, I know there is such a function as memset() ;> */
	for (i=0, tmp=op+1; i<modes; i++, tmp++) *tmp=c;
	op[0]=*name=='d'?'-':'+';

	zzz = string_init(*mp);
	for (i=1; mp[i]; i++)
		string_append_c(zzz, ' '), string_append(zzz, mp[i]);
	
	nicks = string_free(zzz, 0);
	p = nicks;

	i=0;
	chan+=4;

	tmp = p;
	while (1)
	{
		for (i=0; i<modes; i++)
			if (!(tmp = xstrchr(tmp, ' ')))
				break;
			else 
				tmp++;

		if (tmp) *(--tmp) = '\0';
		op[i+2]='\0';
		watch_write(j->send_watch, "MODE %s %s %s\r\n", chan, op, p);
		if (!tmp) break;
		*tmp = ' ';
		tmp++;
		p = tmp;
	}
	chan-=4;
	xfree(chan);
	xfree(nicks);
	xfree(op);
	array_free(mp);
	return 0;
}

static COMMAND(irc_command_ctcp) {
	int		i;
	char		*who;
	char		**mp;

	/* GiM: XXX */
	if (!(who=irc_getchan(session, params, name,
					&mp, 0, IRC_GC_ANY))) 
		return -1;

	if (*mp) {
		for (i=0; ctcps[i].name; i++)
			if (!xstrcasecmp(ctcps[i].name, *mp))
				break;
	} else i = CTCP_VERSION-1;

	/*if (!ctcps[i].name) {
		return -1;
	}*/

	watch_write(irc_private(session)->send_watch, "PRIVMSG %s :\01%s\01\r\n",
			who+4, ctcps[i].name?ctcps[i].name:(*mp));

	array_free(mp);
	xfree(who);
	return 0;
}

static COMMAND(irc_command_ping) {
	char		**mp, *who;
	struct timeval	tv;

	if (!(who=irc_getchan(session, params, name, &mp, 0, IRC_GC_ANY))) 
		return -1;

	gettimeofday(&tv, NULL);
	watch_write(irc_private(session)->send_watch, "PRIVMSG %s :\01PING %d %d\01\r\n",
			who+4 ,tv.tv_sec, tv.tv_usec);

	array_free(mp);
	xfree(who);
	return 0;
}

static COMMAND(irc_command_me) {
	irc_private_t	*j = irc_private(session);
	char		**mp, *chan, *chantypes = SOP(_005_CHANTYPES), *col;
	int		mw = session_int_get(session, "make_window"), ischn;

	char *str = NULL;

	if (!(chan=irc_getchan(session, params, name,
					&mp, 1, IRC_GC_ANY)))
		return -1;

	ischn = chantypes?!!xstrchr(chantypes, chan[4]):0;
	
	if (!(str = irc_convert_out(j, chan+4, *mp)))
		str = xstrdup(*mp);

	watch_write(irc_private(session)->send_watch, "PRIVMSG %s :\01ACTION %s\01\r\n",
			chan+4, str?str:"");

	col = irc_ircoldcolstr_to_ekgcolstr(session, *mp, 1);
	print_window(chan, session, EKG_WINACT_MSG, ischn?(mw&1):!!(mw&2),
			ischn?"irc_ctcp_action_y_pub":"irc_ctcp_action_y",
			session_name(session), j->nick, chan, col);

	array_free(mp);
	xfree(chan);
	xfree(col);
	xfree(str);
	return 0;
}

static COMMAND(irc_command_mode) {
	char	**mp, *chan;

	if (!(chan=irc_getchan(session, params, name,
					&mp, 0, IRC_GC_CHAN))) 
		return -1;
/* G->dj: I'm still leaving this 
	if (!(*mp)) {
		print("not_enough_params", name);
		array_free(mp);
		xfree(chan);
		return -1;
	}
*/
	debug("%s %s \n", chan, mp[0]);
	if (!(*mp))
		watch_write(irc_private(session)->send_watch, "MODE %s\r\n",
				chan+4);
	else
		watch_write(irc_private(session)->send_watch, "MODE %s %s\r\n",
				chan+4, *mp);

	array_free(mp);
	xfree(chan);
	return 0;
}

static COMMAND(irc_command_umode) {
	irc_private_t	*j = irc_private(session);

	if (!(*params)) {
		print("not_enough_params", name);
		return -1;
	}

	watch_write(j->send_watch, "MODE %s %s\r\n", j->nick, *params);

	return 0;
}

static COMMAND(irc_command_whois) {
	char	**mp, *person;

	if (!(person = irc_getchan(session, params, name,
					&mp, 0, IRC_GC_NOT_CHAN)))
		return -1;

	debug("irc_command_whois(): %s\n", name);
	if (!xstrcmp(name, ("whowas")))
		watch_write(irc_private(session)->send_watch, "WHOWAS %s\r\n", person+4);
	else if (!xstrcmp(name, ("wii")))
		watch_write(irc_private(session)->send_watch, "WHOIS %s %s\r\n", person+4, person+4);
	else	watch_write(irc_private(session)->send_watch, "WHOIS %s\r\n",  person+4);

	array_free(mp);
	xfree (person);
	return 0;
}

static QUERY(irc_status_show_handle) {
	char		**uid = va_arg(ap, char**);

	session_t	*s = session_find(*uid);
	irc_private_t	*j;

	const char	*p[1];

	if (!s)
		return -1;

	if ((j = s->priv)) {
		if (j->conv_in != (void *) -1) {
			debug("[%s] Uses recoding for: %s [%x, %x]\n", s->uid, session_get(s, "recode_out_default_charset"), j->conv_in, j->conv_out);
		}
	}

	p[0] = irc_private(s)->nick;
	p[1] = 0;

	return irc_command_whois(("wii"), p, s, NULL, 0);
}

static COMMAND(irc_command_query) {
	irc_private_t	*j = irc_private(session);
	window_t	*w;
	char		**mp, *tar, **p = xcalloc(3, sizeof(char*)), *tmp;
	int		i;

	for (i = 0; i<2 && params[i]; i++)
		p[i] = xstrdup(params[i]);

	p[i] = NULL;/* G->dj: I'm leaving this, I like things like 
		     * that to be clearly visible
		     */

	if (params[0] && (tmp = xstrrchr(params[0], '/'))) {
		tmp++;

		xfree(p[0]);
		p[0] = xstrdup(tmp);
	}

	if (!(tar = irc_getchan(session, (const char **) p, name,
					&mp, 0, IRC_GC_NOT_CHAN))) {
		array_free(p);
		return -1;
	}

	w = window_find_s(session, tar);

	if (!w) {
		w = window_new(tar, session, 0);
		if (session_int_get(session, "auto_lusers_sync") > 0)
			watch_write(j->send_watch, "USERHOST %s\r\n", tar+4);
	}

	window_switch(w->id);

	array_free(mp);
	array_free(p);
	xfree(tar);
	return 0;
}

static COMMAND(irc_command_jopacy) {
	irc_private_t	*j = irc_private(session);
	char		**mp, *tar = NULL, *pass = NULL, *str, *tmp;
	channel_t	*chan;

	if (!(tar = irc_getchan(session, params, name,
					&mp, 0, IRC_GC_CHAN)))
		return -1;

	if (!xstrcmp(name, ("cycle"))) {
		chan = irc_find_channel(j->channels, tar);
		if (chan && (pass = xstrchr(chan->mode_str, 'k')))
			pass+=2;
		debug("[IRC_CYCLE] %s\n", pass);
	}

	tmp = saprintf("JOIN %s%s\r\n", tar+4, pass ? pass : "");
	if (!xstrcmp(name, ("part")) || !xstrcmp(name, ("cycle"))) {
		str = saprintf("PART %s :%s\r\n%s", tar+4,
				PARTMSG(session,(*mp)),
				!xstrcmp(name, ("cycle"))?tmp:"");
	} else if (!xstrcmp(name, ("join"))) {
		str = tmp; tmp=NULL;
	} else
		return 0;

	watch_write(j->send_watch, str);

	array_free(mp);
	xfree(tar);
	xfree(str);
	xfree(tmp);

	return 0;
}

static COMMAND(irc_command_nick) {
	irc_private_t	*j = irc_private(session);

	/* GiM: XXX FIXME TODO think more about session->connecting... */
	if (session->connecting || session_connected_get(session)) {
		watch_write(j->send_watch, "NICK %s\r\n", params[0]);
		/* this is needed, couse, when connecting and server will
		 * respond, nickname is already in use, and user
		 * will type /nick somethin', server doesn't send respond
		 * about nickname.... */
		if (session->connecting) {
			xfree(j->nick);
			j->nick = xstrdup(params[0]);
		}
	}

	return 0;
}

static COMMAND(irc_command_test) {
	irc_private_t	*j = irc_private(session);
	list_t		tlist = j->connlist;

//#define DOT(a,x,y,z,error) print_info("__status", z, a, session_name(z), x, y->hostname, y->address, itoa(y->port), itoa(y->family), error ? strerror(error) : "")
	
	for (tlist = j->connlist; tlist; tlist = tlist->next)
		DOT("IRC_TEST", "Connect to:", ((connector_t *) tlist->data), session, 0);
	for (tlist = j->bindlist; tlist; tlist = tlist->next)
		DOT("IRC_TEST", "Bind to:", ((connector_t *) tlist->data), session, 0);

	if (j->conntmplist && j->conntmplist->data) {
		if (session->connecting)
			DOT("IRC_TEST", "Connecting:", ((connector_t *) j->conntmplist->data), session, 0);
		else if (session_connected_get(session))
			DOT("IRC_TEST", "Connected:", ((connector_t *) j->conntmplist->data), session, 0);
		else	DOT("IRC_TEST", "Will Connect to:", ((connector_t *) j->conntmplist->data), session, 0);
	}
	if (j->bindtmplist && j->bindtmplist->data)
		DOT("IRC_TEST", "((Will Bind to) || (Binded)) :", ((connector_t *) j->bindtmplist->data), session, 0);
	return 0;
}

/* nickpad, these funcs should go to misc.c */
char *nickpad_string_create(channel_t *chan)
{
	int i;
	chan->nickpad_len = (chan->longest_nick + 1) * fillchars_len;
	xfree (chan->nickpad_str);
	chan->nickpad_str = (char *)xmalloc(chan->nickpad_len);

	/* fill string */
	for (i = 0; i < chan->nickpad_len; i++)
		chan->nickpad_str[i] = fillchars[ i % fillchars_len ];

	debug("created NICKPAD with len: %d\n", chan->nickpad_len);
	return chan->nickpad_str;
}

char *nickpad_string_apply(channel_t *chan, const char *str)
{
	chan->nickpad_pos = chan->longest_nick - xstrlen(str);
	if (config_use_unicode)
		chan->nickpad_pos <<= 1;
	if (chan->nickpad_pos < chan->nickpad_len && chan->nickpad_pos >= 0)
	{
		chan->nickpad_str[chan->nickpad_pos] = '\0';
	} else {
		debug_error ("[irc, misc, nickpad], problem with padding %x against %x\n", chan->nickpad_pos, chan->nickpad_len);
	}
	return chan->nickpad_str;
}

char *nickpad_string_restore(channel_t *chan)
{
	if (chan->nickpad_pos < chan->nickpad_len && chan->nickpad_pos >= 0)
	{
		chan->nickpad_str[chan->nickpad_pos] = fillchars[ chan->nickpad_pos % fillchars_len ];
	} 
	return chan->nickpad_str;
}


/*									 *
 * ======================================== INIT/DESTROY --------------- *
 *									 */

#define params(x) x

static plugins_params_t irc_plugin_vars[] = {
	/* lower case: names of variables that reffer to client itself */
	PLUGIN_VAR_ADD("alt_nick",		VAR_STR, NULL, 0, NULL),
	PLUGIN_VAR_ADD("alias",			VAR_STR, NULL, 0, NULL),
	PLUGIN_VAR_ADD("auto_away",		VAR_INT, "0", 0, NULL),
	PLUGIN_VAR_ADD("auto_back",		VAR_INT, "0", 0, NULL),
	PLUGIN_VAR_ADD("auto_connect",		VAR_BOOL, "0", 0, NULL),
	PLUGIN_VAR_ADD("auto_find",		VAR_BOOL, "0", 0, NULL),		/* it's really auto_whois */
	PLUGIN_VAR_ADD("auto_guess_encoding",	VAR_STR, NULL, 0, irc_changed_auto_guess_encoding),
	PLUGIN_VAR_ADD("auto_reconnect",	VAR_INT, "10", 0, NULL), 
	PLUGIN_VAR_ADD("auto_channel_sync",	VAR_BOOL, "1", 0, NULL),		/* like channel_sync in irssi; better DO NOT turn it off! */
	PLUGIN_VAR_ADD("auto_lusers_sync",	VAR_BOOL, "0", 0, NULL),		/* sync lusers, stupid ;(,  G->dj: well why ? */
	PLUGIN_VAR_ADD("away_log",		VAR_BOOL, "1", 0, NULL),
	PLUGIN_VAR_ADD("ban_type",		VAR_INT, "10", 0, NULL),
	PLUGIN_VAR_ADD("connect_timeout",	VAR_INT, "0", 0, NULL),
	PLUGIN_VAR_ADD("close_windows",		VAR_BOOL, "0", 0, NULL),
	PLUGIN_VAR_ADD("dcc_port",		VAR_INT, "0", 0, NULL),
	PLUGIN_VAR_ADD("display_notify",	VAR_INT, "0", 0, NULL),
	PLUGIN_VAR_ADD("hostname",		VAR_STR, 0, 0, irc_changed_resolve),
	PLUGIN_VAR_ADD("identify",		VAR_STR, 0, 0, NULL),
	PLUGIN_VAR_ADD("log_formats",		VAR_STR, "irssi", 0, NULL),
	PLUGIN_VAR_ADD("make_window",		VAR_INT, "2", 0, NULL),
#define IRC_PLUGIN_VAR_NICKNAME 20
	PLUGIN_VAR_ADD("nickname",		VAR_STR, NULL, 0, NULL),		/* value will be inited @ irc_plugin_init() [pwd_entry->pw_name] */
	PLUGIN_VAR_ADD("password",		VAR_STR, 0, 1, NULL),
	PLUGIN_VAR_ADD("port",			VAR_INT, "6667", 0, NULL),
	PLUGIN_VAR_ADD("prefer_family",		VAR_INT, "0", 0, NULL),
#define IRC_PLUGIN_VAR_REALNAME 24
	PLUGIN_VAR_ADD("realname",		VAR_STR, NULL, 0, NULL),		/* value will be inited @ irc_plugin_init() [pwd_entry->pw_gecos] */
	PLUGIN_VAR_ADD("recode_list", VAR_STR, NULL, 0, irc_changed_recode_list),
	PLUGIN_VAR_ADD("recode_out_default_charset", VAR_STR, NULL, 0, irc_changed_recode),		/* irssi-like-variable */
	PLUGIN_VAR_ADD("server",                VAR_STR, 0, 0, irc_changed_resolve),
	PLUGIN_VAR_ADD("statusdescr",           VAR_STR, 0, 0, irc_statusdescr_handler),
#ifdef IRC_HAVE_OPENSSL
	PLUGIN_VAR_ADD("use_ssl",               VAR_BOOL, "0", 0, NULL),
#endif

	/* upper case: names of variables, that reffer to protocol stuff */
	PLUGIN_VAR_ADD("AUTO_JOIN",			VAR_STR, 0, 0, NULL),
	PLUGIN_VAR_ADD("AUTO_JOIN_CHANS_ON_INVITE",	VAR_BOOL, "0", 0, NULL),
	PLUGIN_VAR_ADD("DEFAULT_COLOR",			VAR_INT, "0", 0, NULL),			/* TODO :> */
	PLUGIN_VAR_ADD("DISPLAY_PONG",			VAR_BOOL, "1", 0, NULL),		/* GiM, do we really want/need '1' here as default? */
	PLUGIN_VAR_ADD("DISPLAY_AWAY_NOTIFICATION",	VAR_INT, "1", 0, NULL),
	PLUGIN_VAR_ADD("DISPLAY_IN_CURRENT",		VAR_INT, "2", 0, NULL),
	PLUGIN_VAR_ADD("DISPLAY_NICKCHANGE",		VAR_INT, "0", 0, NULL),
	PLUGIN_VAR_ADD("DISPLAY_QUIT",			VAR_INT, "0", 0, NULL),
	/* plugin_var_add(&irc_plugin, "HIGHLIGHTS", VAR_STR, 0, 0, NULL); */
	PLUGIN_VAR_ADD("KICK_MSG",			VAR_STR, DEFKICKMSG, 0, NULL),
	PLUGIN_VAR_ADD("PART_MSG",			VAR_STR, DEFPARTMSG, 0, NULL),
	PLUGIN_VAR_ADD("QUIT_MSG",			VAR_STR, DEFQUITMSG, 0, NULL),
	PLUGIN_VAR_ADD("REJOIN",			VAR_INT, "0", 0, NULL),
	PLUGIN_VAR_ADD("REJOIN_TIME",			VAR_INT, "2", 0, NULL),
	PLUGIN_VAR_ADD("SHOW_NICKMODE_EMPTY",		VAR_INT, "1", 0, NULL),
	PLUGIN_VAR_ADD("SHOW_MOTD",			VAR_BOOL, "1", 0, NULL),
	PLUGIN_VAR_ADD("STRIPMIRCCOL",			VAR_BOOL, "0", 0, NULL),
	PLUGIN_VAR_ADD("VERSION_NAME",			VAR_STR, 0, 0, NULL),
	PLUGIN_VAR_ADD("VERSION_NO",			VAR_STR, 0, 0, NULL),
	PLUGIN_VAR_ADD("VERSION_SYS",			VAR_STR, 0, 0, NULL),

	PLUGIN_VAR_END()
};

static const char *irc_protocols[] = { "irc:", NULL };
static const status_t irc_statuses[] = {
	EKG_STATUS_NA, EKG_STATUS_AWAY, EKG_STATUS_AVAIL /* XXX */, EKG_STATUS_NULL
};

static const struct protocol_plugin_priv irc_priv = {
	.protocols	= irc_protocols,
	.statuses	= irc_statuses
};

EXPORT int irc_plugin_init(int prio)
{
#ifndef NO_POSIX_SYSTEM
	struct passwd	*pwd_entry = getpwuid(getuid());

/* yeah, i know it's static. */
	static char pwd_name[2000]	= { '\0'};
	static char pwd_realname[2000]	= { '\0'};

	PLUGIN_CHECK_VER("irc");

	if (pwd_entry) {
		xstrncpy(pwd_name, pwd_entry->pw_name, sizeof(pwd_name));
		xstrncpy(pwd_realname, pwd_entry->pw_gecos, sizeof(pwd_realname));

		pwd_name[sizeof(pwd_name)-1] = '\0';
		pwd_realname[sizeof(pwd_realname)-1] = '\0';
		/* XXX, we need to free buffer allocated by getpwuid()? */
	}

#else
	const char *pwd_name = NULL;
	const char *pwd_realname = NULL;
#endif

#ifdef IRC_HAVE_OPENSSL
	SSL_load_error_strings();
	SSL_library_init();
	if (! (ircSslCtx = SSL_CTX_new(SSLv23_method()))) {
		debug("couldn't init ssl ctx: %s!\n", ERR_error_string(ERR_get_error(), NULL));
		return -1;
	}
#endif

/* TODO: hardcoding offsets like this is fragile */
	irc_plugin_vars[IRC_PLUGIN_VAR_NICKNAME].value = pwd_name;
	irc_plugin_vars[IRC_PLUGIN_VAR_REALNAME].value = pwd_realname;

	irc_plugin.params	= irc_plugin_vars;
	irc_plugin.priv		= &irc_priv;

	plugin_register(&irc_plugin, prio);

	fillchars = (config_use_unicode ? fillchars_utf8 : fillchars_norm);
	fillchars_len = (config_use_unicode ? 2 : 1);

	query_connect_id(&irc_plugin, PROTOCOL_VALIDATE_UID,	irc_validate_uid, NULL);
	query_connect_id(&irc_plugin, PLUGIN_PRINT_VERSION,	irc_print_version, NULL);
	query_connect_id(&irc_plugin, UI_WINDOW_KILL,		irc_window_kill, NULL);
	query_connect_id(&irc_plugin, SESSION_ADDED,		irc_session_init, NULL);
	query_connect_id(&irc_plugin, SESSION_REMOVED,		irc_session_deinit, NULL);
	query_connect_id(&irc_plugin, IRC_TOPIC,		irc_topic_header, (void*) 0);
	query_connect_id(&irc_plugin, STATUS_SHOW,		irc_status_show_handle, NULL);
	query_connect_id(&irc_plugin, IRC_KICK,			irc_onkick_handler, 0);
	query_connect_id(&irc_plugin, SET_VARS_DEFAULT,		irc_setvar_default, NULL);

#define IRC_ONLY		SESSION_MUSTBELONG | SESSION_MUSTHASPRIVATE
#define IRC_FLAGS		IRC_ONLY | SESSION_MUSTBECONNECTED
#define IRC_FLAGS_TARGET	IRC_FLAGS | COMMAND_ENABLEREQPARAMS | COMMAND_PARAMASTARGET
	command_add(&irc_plugin, ("irc:"), "?",		irc_command_inline_msg, IRC_FLAGS | COMMAND_PASS_UNCHANGED, NULL);
	command_add(&irc_plugin, ("irc:_autoaway"), NULL,	irc_command_away,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:_autoback"), NULL,	irc_command_away,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:_conntest"), "?",	irc_command_test,	IRC_ONLY, NULL);
	command_add(&irc_plugin, ("irc:access"), "p uUw ? ?",	irc_command_alist, 0, "-a --add -d --delete -e --edit -s --show -l --list -S --sync");
	command_add(&irc_plugin, ("irc:add"), NULL,	irc_command_add,	IRC_ONLY | COMMAND_PARAMASTARGET, NULL);
	command_add(&irc_plugin, ("irc:away"), "?",	irc_command_away,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:back"), NULL,	irc_command_away,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:ban"),  "uUw uU",	irc_command_ban,	IRC_FLAGS, NULL); 
	command_add(&irc_plugin, ("irc:bankick"), "uUw uU ?", irc_command_kickban,IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:connect"), NULL,	irc_command_connect,	IRC_ONLY, NULL);
	command_add(&irc_plugin, ("irc:ctcp"), "uUw ?",	irc_command_ctcp,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:cycle"), "w ?",	irc_command_jopacy,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:dehalfop"), "uUw uU uU uU uU uU uU ?",irc_command_devop, IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:deop"), "uUw uU uU uU uU uU uU ?",	irc_command_devop, IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:devoice"), "uUw uU uU uU uU uU uU ?",irc_command_devop, IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:disconnect"), "r",irc_command_disconnect,IRC_ONLY, NULL);
	command_add(&irc_plugin, ("irc:find"), "uU",	irc_command_whois,	IRC_FLAGS, NULL); /* for auto_find */
	command_add(&irc_plugin, ("irc:halfop"), "uUw uU uU uU uU uU uU ?",irc_command_devop, IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:invite"), "uUw uUw",irc_command_invite,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:join"), "w",	irc_command_jopacy,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:kick"), "uUw uU ?",irc_command_kick,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:kickban"), "uUw uU ?", irc_command_kickban,IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:me"), "uUw ?",	irc_command_me,		IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:mode"), "w ?",	irc_command_mode,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:msg"), "!uUw !",	irc_command_msg,	IRC_FLAGS_TARGET, NULL);
	command_add(&irc_plugin, ("irc:names"), "w?",	irc_command_names,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:nick"), "!",	irc_command_nick,	IRC_ONLY | COMMAND_ENABLEREQPARAMS, NULL);
	command_add(&irc_plugin, ("irc:notice"), "!uUw !",irc_command_msg,	IRC_FLAGS_TARGET, NULL);
	command_add(&irc_plugin, ("irc:op"), "uUw uU uU uU uU uU uU ?",	irc_command_devop, IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:part"), "w ?",	irc_command_jopacy,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:people"), NULL,	irc_command_pipl,	IRC_ONLY, NULL);
	command_add(&irc_plugin, ("irc:ping"), "uUw ?",	irc_command_ping,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:query"), "uUw",	irc_command_query,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:quote"), "!",	irc_command_quote,	IRC_FLAGS | COMMAND_ENABLEREQPARAMS, NULL);
	command_add(&irc_plugin, ("irc:reconnect"), "r ?",irc_command_reconnect,	IRC_ONLY, NULL);
	command_add(&irc_plugin, ("irc:topic"), "w ?",	irc_command_topic,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:umode"), "?",	irc_command_umode,	IRC_ONLY /* _FLAGS ? */, NULL);
	command_add(&irc_plugin, ("irc:unban"),  "uUw uU",irc_command_unban,	IRC_FLAGS, NULL); 
	command_add(&irc_plugin, ("irc:voice"), "uUw uU uU uU uU uU uU ?",irc_command_devop, IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:who"), "uUw",	irc_command_who,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:whois"), "uU",	irc_command_whois,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:whowas"), "uU",	irc_command_whois,	IRC_FLAGS, NULL);
	command_add(&irc_plugin, ("irc:wii"), "uU",	irc_command_whois,	IRC_FLAGS, NULL);
/*
	command_add(&irc_plugin, ("irc:admin"), "",	  NULL, 0, NULL);   q admin
	command_add(&irc_plugin, ("irc:map"),  "",	  NULL, 0, NULL);   q map
	command_add(&irc_plugin, ("irc:links"),  "",	  NULL, 0, NULL); V q links
	command_add(&irc_plugin, ("irc:oper"), "",	NULL, 0, NULL);   q oper %nick %pass
	command_add(&irc_plugin, ("irc:trace"), "",	NULL, 0, NULL);   q trace %...
	command_add(&irc_plugin, ("irc:stats"), "\"STATS\" ?",irc_command_quote, 0, NULL); V q stats
	command:add(&irc_plugin, ("irc:list"), .....)			V q list 
*/
	variable_add(&irc_plugin, "access_groups", VAR_STR, 1, &irc_config_default_access_groups, NULL, NULL, NULL);
	variable_add(&irc_plugin, "experimental_chan_name_clean", VAR_BOOL, 1, &irc_config_experimental_chan_name_clean, NULL, NULL, NULL);

	return 0;
}

static int irc_plugin_destroy() {
	plugin_unregister(&irc_plugin);

	return 0;
}

static int irc_theme_init()
{
	debug("I love you honey bunny\n");

	/* %1 should be _always_ session name, if it's not so,
	 * you should report this to me (GiM)
	 */
#ifndef NO_DEFAULT_THEME
	/* %2 - prefix, %3 - nick, %4 - nick+ident+host, %5 - chan, %6 - msg*/
	format_add("irc_msg_sent",	"%P<%n%3/%5%P>%n %6", 1);
	format_add("irc_msg_sent_n",	"%P<%n%3%P>%n %6", 1);
	format_add("irc_msg_sent_chan",	"%P%7<%w%{2*!@%+yrgcp}X%2%3%P>%n %6", 1);
	format_add("irc_msg_sent_chanh","%P%7<%W%{2*!@%+YRGCP}X%2%3%P>%n %6", 1);
	
	format_add("irc_not_sent",	"%P(%n%3/%5%P)%n %6", 1);
	format_add("irc_not_sent_n",	"%P(%n%3%P)%n %6", 1);
	format_add("irc_not_sent_chan",	"%P%7(%w%{2*!@%+yrgcp}X%2%3%P)%n %6", 1);
	format_add("irc_not_sent_chanh","%P%7(%W%{2*!@%+YRGCP}X%2%3%P)%n %6", 1);

//	format_add("irc_msg_f_chan",	"%B<%w%{2@%+gcp}X%2%3/%5%B>%n %6", 1); /* NOT USED */
//	format_add("irc_msg_f_chanh",	"%B<%W%{2@%+GCP}X%2%3/%5%B>%n %6", 1); /* NOT USED */
	format_add("irc_msg_f_chan_n",	"%B%7<%w%{2*!@%+yrgcp}X%2%3%B>%n %6", 1);
	format_add("irc_msg_f_chan_nh",	"%B%7<%W%{2*!@%+YRGCP}X%2%3%B>%n %6", 1);
	format_add("irc_msg_f_some",	"%b<%n%3%b>%n %6", 1);

//	format_add("irc_not_f_chan",	"%B(%w%{2@%+gcp}X%2%3/%5%B)%n %6", 1); /* NOT USED */
//	format_add("irc_not_f_chanh",	"%B(%W%{2@%+GCP}X%2%3/%5%B)%n %6", 1); /* NOT USED */
	format_add("irc_not_f_chan_n",	"%B(%w%{2*!@%+yrgcp}X%2%3%B)%n %6", 1);
	format_add("irc_not_f_chan_nh",	"%B(%W%{2*!@%+YRGCP}X%2%3%B)%n %6", 1);
	format_add("irc_not_f_some",	"%b(%n%3%b)%n %6", 1);
	format_add("irc_not_f_server",	"%g!%3%n %6", 1);

	format_add("IRC_NAMES_NAME",	_("[%gUsers %G%2%n]"), 1);
	format_add("IRC_NAMES",		"%K[%W%1%w%2%3%K]%n ", 1);
	format_add("IRC_NAMES_TOTAL_H",	_("%> %WEKG2: %2%n: Total of %W%3%n nicks [%W%8%n owners, %W%9%n admins, %W%4%n ops, %W%5%n halfops, %W%6%n voices, %W%7%n normal]\n"), 1);
	format_add("IRC_NAMES_TOTAL",	"%> %WEKG2: %2%n: Total of %W%3%n nicks [%W%4%n ops, %W%5%n voices, %W%6%n normal]\n", 1);

	format_add("irc_joined",	_("%> %Y%2%n has joined %4\n"), 1);
	format_add("irc_joined_you",	_("%> %RYou%n have joined %4\n"), 1);
	format_add("irc_left",		_("%> %g%2%n has left %4 (%5)\n"), 1);
	format_add("irc_left_you",	_("%> %RYou%n have left %4 (%5)\n"), 1);
	format_add("irc_kicked",	_("%> %Y%2%n has been kicked out by %R%3%n from %5 (%6)\n"), 1);
	format_add("irc_kicked_you",	_("%> You have been kicked out by %R%3%n from %5 (%6)\n"), 1);
	format_add("irc_quit",		_("%> %Y%2%n has quit irc (%4)\n"), 1);
	format_add("irc_split",		"%> ", 1);
	format_add("irc_unknown_ctcp",	_("%> %Y%2%n sent unknown CTCP %3: (%4)\n"), 1);
	format_add("irc_ctcp_action_y_pub",	"%y* %2%n  %4\n", 1);
	format_add("irc_ctcp_action_y",		"%Y* %2%n  %4\n", 1);
	format_add("irc_ctcp_action_pub",	"%y* %2%n  %5\n", 1);
	format_add("irc_ctcp_action",		"%Y* %2%n  %5\n", 1);
	format_add("irc_ctcp_request_pub",	_("%> %Y%2%n requested ctcp %5 from %4\n"), 1);
	format_add("irc_ctcp_request",		_("%> %Y%2%n requested ctcp %5\n"), 1);
	format_add("irc_ctcp_reply",		_("%> %Y%2%n CTCP reply from %3: %5\n"), 1);


	format_add("IRC_ERR_CANNOTSENDTOCHAN",	"%! %2: %1\n", 1);
	
	format_add("IRC_RPL_FIRSTSECOND",	"%> (%1) %2 %3\n", 1);
	format_add("IRC_RPL_SECONDFIRST",	"%> (%1) %3 %2\n", 1);
	format_add("IRC_RPL_JUSTONE",		"%> (%1) %2\n", 1);
	format_add("IRC_RPL_NEWONE",		"%> (%1,%2) 1:%3 2:%4 3:%5 4:%6\n", 1);

	format_add("IRC_ERR_FIRSTSECOND",	"%! (%1) %2 %3\n", 1);
	format_add("IRC_ERR_SECONDFIRST",	"%! (%1) %3 %2\n", 1);
	format_add("IRC_ERR_JUSTONE",		"%! (%1) %2\n", 1);
	format_add("IRC_ERR_NEWONE",		"%! (%1,%2) 1:%3 2:%4 3:%5 4:%6\n", 1);
	
	format_add("IRC_RPL_CANTSEND",	_("%> Cannot send to channel %T%2%n\n"), 1);
	format_add("RPL_MOTDSTART",	"%g,+=%G-----\n", 1);
	format_add("RPL_MOTD",		"%g|| %n%2\n", 1);
	format_add("RPL_ENDOFMOTD",	"%g`+=%G-----\n", 1);

	
	format_add("RPL_INVITE",	_("%> Inviting %W%2%n to %W%3%n\n"), 1);
	/* Used in: /mode +b|e|I %2 - chan %3 - data from server */
	/* THIS IS TEMPORARY AND WILL BE DONE OTHER WAY, DO NOT USE THIS STYLES
	 */
	format_add("RPL_LISTSTART",	"%g,+=%G-----\n", 1);
	format_add("RPL_EXCEPTLIST",	_("%g|| %n %5 - %W%2%n: except %c%3\n"), 1);
	format_add("RPL_BANLIST",	_("%g|| %n %5 - %W%2%n: ban %c%3\n"), 1);
	format_add("RPL_INVITELIST",	_("%g|| %n %5 - %W%2%n: invite %c%3\n"), 1);;
	format_add("RPL_EMPTYLIST" ,	_("%g|| %n Empty list \n"), 1);
	format_add("RPL_LINKS",		"%g|| %n %5 - %2  %3  %4\n", 1);
	format_add("RPL_ENDOFLIST",	"%g`+=%G----- %2%n\n", 1);

	/* %2 - number; 3 - type of stats (I, O, K, etc..) ....*/
	format_add("RPL_STATS",		"%g|| %3 %n %4 %5 %6 %7 %8\n", 1);
	format_add("RPL_STATS_EXT",	"%g|| %3 %n %2 %4 %5 %6 %7 %8\n", 1);
	format_add("RPL_STATSEND",	"%g`+=%G--%3--- %2\n", 1);
	/*
	format_add("RPL_CHLISTSTART",  "%g,+=%G lp %2\t%3\t%4\n", 1);
	format_add("RPL_CHLIST",       "%g|| %n %5 %2\t%3\t%4\n", 1);
	*/
	format_add("RPL_CHLISTSTART",	"%g,+=%G lp %2\t%3\t%4\n", 1);
	format_add("RPL_LIST",		"%g|| %n %5 %2\t%3\t%4\n", 1);

	/* 2 - number; 3 - chan; 4 - ident; 5 - host; 6 - server ; 7 - nick; 8 - mode; 9 -> realname
	 * format_add("RPL_WHOREPLY",	"%g|| %c%3 %W%7 %n%8 %6 %4@%5 %W%9\n", 1);
	 */
	format_add("RPL_WHOREPLY",	"%g|| %c%3 %W%7 %n%8 %6 %4@%5 %W%9\n", 1);
	/* delete those irssi-like styles */

	format_add("RPL_AWAY",		_("%G||%n away	   : %2 - %3\n"), 1);
	/* in whois %2 is always nick */
	format_add("RPL_WHOISUSER",	_("%G.+===%g-----\n%G||%n (%T%2%n) (%3@%4)\n"
				"%G||%n realname : %6\n"), 1);

	format_add("RPL_WHOWASUSER",	_("%G.+===%g-----\n%G||%n (%T%2%n) (%3@%4)\n"
				"%G||%n realname : %6\n"), 1);

/* %2 - nick %3 - there is/ was no such nickname / channel, and so on... */
	/*
	format_add("IRC_WHOERROR", _("%G.+===%g-----\n%G||%n %3 (%2)\n"), 1);
	format_add("IRC_ERR_NOSUCHNICK", _("%n %3 (%2)\n"), 1);
	*/

	format_add("RPL_WHOISCHANNELS",	_("%G||%n %|channels : %3\n"), 1);
	format_add("RPL_WHOISSERVER",	_("%G||%n %|server   : %3 (%4)\n"), 1);
	format_add("RPL_WHOISOPERATOR",	_("%G||%n %|ircOp    : %3\n"), 1);
	format_add("RPL_WHOISIDLE",	_("%G||%n %|idle     : %3 (signon: %4)\n"), 1);
	format_add("RPL_WHOISMISC", _("%G||%n %|         * %3\n"), 1);
	format_add("RPL_ENDOFWHOIS",	_("%G`+===%g-----\n"), 1);
	format_add("RPL_ENDOFWHOWAS",	_("%G`+===%g-----\n"), 1);

	format_add("RPL_TOPIC",		_("%> Topic %2: %3\n"), 1);
	/* \n not needed if you're including date [%4] */
	format_add("IRC_RPL_TOPICBY",	_("%> set by %2 on %4"), 1);
	format_add("IRC_TOPIC_CHANGE",	_("%> %T%2%n changed topic on %T%4%n: %5\n"), 1);
	format_add("IRC_TOPIC_UNSET",	_("%> %T%2%n unset topic on %T%4%n\n"), 1);
	format_add("IRC_MODE_CHAN_NEW",	_("%> %2/%4 sets mode [%5]\n"), 1);
	format_add("IRC_MODE_CHAN",	_("%> %2 mode is [%3]\n"), 1);
	format_add("IRC_MODE",		_("%> (%1) %2 set mode %3 on You\n"), 1);
	
	format_add("IRC_INVITE",	_("%> %W%2%n invites you to %W%5%n\n"), 1);
	format_add("IRC_PINGPONG",	_("%) (%1) ping/pong %c%2%n\n"), 1);
	format_add("IRC_YOUNEWNICK",	_("%> You are now known as %G%3%n\n"), 1);
	format_add("IRC_NEWNICK",	_("%> %g%2%n is now known as %G%4%n\n"), 1);
	format_add("IRC_TRYNICK",	_("%> Will try to use %G%2%n instead\n"), 1);
	format_add("IRC_CHANNEL_SYNCED", "%> Join to %W%2%n was synced in %W%3.%4%n secs", 1);
	/* %1 - sesja ; %2 - Connect, BIND, whatever, %3 - hostname %4 - adres %5 - port 
	 * %6 - family (debug pursuit only) */
	format_add("IRC_TEST",		"%> (%1) %2 to %W%3%n [%4] port %W%5%n (%6)", 1);
	format_add("IRC_CONN_ESTAB",	"%> (%1) Connection to %W%3%n estabilished", 1);
	/* j/w %7 - error */
	format_add("IRC_TEST_FAIL",	"%! (%1) Error: %2 to %W%3%n [%4] port %W%5%n (%7)", 1);
	
	format_add("irc_channel_secure",	"%) (%1) Echelon can kiss our ass on %2 *g*", 1); 
	format_add("irc_channel_unsecure",	"%! (%1) warning no plugin protects us on %2 :( install sim plugin now or at least rot13..", 1); 

	format_add("irc_access_added",	_("%> (%1) %3 [#%2] was added to accesslist chan: %4 (flags: %5)"), 1);
	format_add("irc_access_known", "a-> %2!%3@%4", 1);	/* %2 is nickname, not uid ! */


	/* away log */
	format_add("irc_awaylog_begin",		_("%G.+===%g----- Awaylog for: (%n%1%g)%n\n"), 1);
	format_add("irc_awaylog_msg",		_("%G|| %n[%Y%2%n] <%W%4%n> %5\n"), 1);
	format_add("irc_awaylog_msg_chan",	_("%G|| %n[%Y%2%n] [%G%3%n] <%W%4%n> %5\n"), 1);
	format_add("irc_awaylog_end",		_("%G`+===%g-----\n"), 1);
	format_add("irc_awaylog_timestamp", "%d-%m-%Y %H:%M:%S", 1);


#endif	/* !NO_DEFAULT_THEME */
	return 0;
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
