/* $Id$ */

/*
 *  (C) Copyright 2003 Wojtek Kaniewski <wojtekka@irc.pl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License Version
 *  2.1 as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __SIMLITE_H
#define __SIMLITE_H

#ifndef __AC_STDINT_H
#include <stdint.h>
#endif 

extern char *sim_key_path;
extern int sim_errno;

typedef enum {
	SIM_ERROR_SUCCESS,	/* uda�o si� */
	SIM_ERROR_PUBLIC,	/* b��d klucza publicznego */
	SIM_ERROR_PRIVATE,	/* b��d klucza prywatnego */
	SIM_ERROR_RSA,		/* nie uda�o si� odszyfrowa� RSA */
	SIM_ERROR_BF,		/* nie uda�o si� odszyfrowa� BF */
	SIM_ERROR_RAND,		/* entropia posz�a na piwo */
	SIM_ERROR_MEMORY,	/* brak pami�ci */
	SIM_ERROR_INVALID,	/* niew�a�ciwa wiadomo�� (za kr�tka) */
	SIM_ERROR_MAGIC		/* niew�a�ciwy magic */
} sim_errno_t;

#define SIM_MAGIC_V1 0x2391
#define SIM_MAGIC_V1_BE 0x9123

typedef struct {
	unsigned char init[8];
	uint16_t magic;
	uint8_t flags;
}
#ifdef __GNUC__
__attribute__ ((packed))
#endif
sim_message_header;

char *sim_message_decrypt(const unsigned char *message, const char *uid);
char *sim_message_encrypt(const unsigned char *message, const char *uid);
int sim_key_generate(const char *uid);
char *sim_key_fingerprint(const char *uid);

const char *sim_strerror(int error);

#endif /* __SIMLITE_H */

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
