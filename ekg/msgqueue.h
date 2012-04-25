/* $Id$ */

/*
 *  (C) Copyright 2001-2003 Piotr Domagalski <szalik@szalik.net>
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

#ifndef __EKG_MSGQUEUE_H
#define __EKG_MSGQUEUE_H

#include <sys/types.h>
#include <time.h>

#include "dynstuff.h"
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct msg_queue {
	struct msg_queue	*next;

	char		*session;		/* do kt�rej sesji nale�y */
	char		*rcpts;			/* uidy odbiorc�w */
	char		*message;		/* tre�� */
	char		*seq;			/* numer sekwencyjny */
	time_t		time;			/* czas wys�ania */
	unsigned int	mark		: 1;	/* if added during cleanup */
	msgclass_t	mclass;
} msg_queue_t;

extern msg_queue_t *msgs_queue;

int msg_queue_add(const char *session, const char *rcpts, const char *message, const char *seq, msgclass_t mclass);
void msgs_queue_destroy();
int msg_queue_count_session(const char *uid);
int msg_queue_remove_uid(const char *uid);
int msg_queue_remove_seq(const char *seq);
int msg_queue_flush(const char *session);
int msg_queue_read();
int msg_queue_write();

#ifdef __cplusplus
}
#endif

#endif /* __EKG_MSGQUEUE_H */

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
