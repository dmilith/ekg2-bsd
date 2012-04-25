/* $Id$ */

/*
 *  (C) Copyright 2004 Piotr Kupisiewicz <deli@rzepaknet.us>
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

#ifndef __EKG_METACONTACTS_H
#define __EKG_METACONTACTS_H
#include "dynstuff.h" 

#ifdef __cplusplus
extern "C" {
#endif

typedef struct metacontact_item {
	struct metacontact_item	*next;

	char			*name;	/* uid or name */
	unsigned int		prio;	/* prio */
	char			*s_uid;	/* session uid */
} metacontact_item_t;

typedef struct metacontact {
	struct metacontact	*next;

	char			*name; /* name of metacontact */
	metacontact_item_t	*metacontact_items;
} metacontact_t;

#ifndef EKG2_WIN32_NOFUNCTION 
extern metacontact_t *metacontacts;

metacontact_t *metacontact_add(const char *name);
metacontact_t *metacontact_find(const char *name);
metacontact_item_t *metacontact_find_prio(metacontact_t *m);

void metacontact_init();
void metacontacts_destroy();

int metacontact_write();
int metacontact_read();

#endif

#ifdef __cplusplus
}
#endif

#endif /* __EKG_METACONTACTS_H */


/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
