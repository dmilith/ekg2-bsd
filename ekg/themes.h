/* $Id$ */

/*
 *  (C) Copyright 2001-2003 Wojtek Kaniewski <wojtekka@irc.pl>
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

#ifndef __EKG_THEMES_H
#define __EKG_THEMES_H

#include "strings.h"

#include "gettext.h" 
#define _(a) gettext(a)
#define N_(a) gettext_noop(a)

#include "dynstuff.h"
#include "sessions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	union {
		char	*b;			/* possibly multibyte string */
		CHAR_T	*w;			/* wide char string */
	} str;		/* A \0-terminated string of characters. Before the
	fstring_t is added to history, should be referred to using 'str->b'.
	Adding to history recodes it to CHAR_T, so afterwards it should be
	referred to by 'str->w'. */

	short		*attr;			/* atrybuty, ci�g o d�ugo�ci strlen(str) */
	time_t		ts;			/* timestamp */

	int		prompt_len;		/* d�ugo�� promptu, kt�ry b�dzie powtarzany przy
						   przej�ciu do kolejnej linii. */
	unsigned int	prompt_empty	: 1;	/* prompt przy przenoszeniu b�dzie pusty */
	int		margin_left;		/* where the margin is set (on what char) */
	void		*priv_data;		/* can be helpfull */
} fstring_t;

#define print(x...)		print_window_w(NULL, EKG_WINACT_JUNK, x) 
#define print_status(x...)	print_window_w(window_status, EKG_WINACT_JUNK, x)

#ifndef EKG2_WIN32_NOFUNCTION

void print_window(const char *target, session_t *session, int activity, int separate, const char *theme, ...);

void print_info(const char *target, session_t *session, const char *theme, ...);
void print_warning(const char *target, session_t *session, const char *theme, ...);

void format_add(const char *name, const char *value, int replace);
const char *format_find(const char *name);
#define format_ok(format_find_result)	(format_find_result[0])
#define format_exists(format)		(format_ok(format_find(format)))
char *format_string(const char *format, ...);

void theme_init();
void theme_plugins_init();
void theme_enumerate(int (*enumerator)(const char *theme, const char *value));
int theme_read(const char *filename, int replace);
int theme_write(const char *filename);
void theme_cache_reset();
void theme_free();

fstring_t *fstring_new(const char *str);
fstring_t *fstring_new_format(const char *format, ...);
void fstring_free(fstring_t *str);

#endif

/*
 * makro udaj�ce isalpha() z LC_CTYPE="pl_PL". niestety ncurses co� psuje
 * i �le wykrywa p�e�.
 */
#define isalpha_pl_PL(x) ((x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�')

typedef enum {
	FSTR_FOREA		= 1,
	FSTR_FOREB		= 2,
	FSTR_FOREC		= 4,
	FSTR_FOREMASK		= (FSTR_FOREA|FSTR_FOREB|FSTR_FOREC),
	FSTR_BACKA		= 8,
	FSTR_BACKB		= 16,
	FSTR_BACKC		= 32,
	FSTR_BACKMASK		= (FSTR_BACKA|FSTR_BACKB|FSTR_BACKC),
	FSTR_BOLD		= 64,
	FSTR_NORMAL		= 128,
	FSTR_BLINK		= 256,
	FSTR_UNDERLINE		= 512,
	FSTR_REVERSE		= 1024,
	FSTR_ALTCHARSET		= 2048
} fstr_t;

#ifdef __cplusplus
}
#endif

#endif /* __EKG_THEMES_H */

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
