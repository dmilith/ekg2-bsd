/* $Id$ */

/*
 *  (C) Copyright 2001-2002 Wojtek Kaniewski <wojtekka@irc.pl>
 *			    Dawid Jarosz <dawjar@poczta.onet.pl>
 *                          Adam Wysocki <gophi@ekg.chmurka.net>
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

#ifndef __EKG_DYNSTUFF_H
#define __EKG_DYNSTUFF_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * typedef list_t
 *
 * list_t jest prostym typem listy u�ywanej w praktycznie wszystkich
 * dynamicznych strukturach ekg. obecnie jest to lista jednokierunkowa
 * (pole `prev' jest r�wne NULL), ale zostawiono mo�liwo�� rozbudowy
 * do dwukierunkowej bez zmiany ABI. dane s� przechowywane w polu `data',
 * kolejny element w `next'. przyk�adowy kod iteracji:
 *
 *     list_t l;
 *
 *     for (l = lista; l; l = l->next) {
 *	   struct cokolwiek *c = l->data;
 *	   printf("%s\n", c->cokolwiek);
 *     }
 *
 * wi�kszo�� list wyst�puj�cych w ekg mo�na iterowa� bez obawy o zmiany
 * ABI. pierwsze pole, b�d�ce jednoznacznym identyfikatorem elementu listy
 * jest dost�pne bezpo�rednio, reszta przez odpowiednie funkcje.
 */

/*
 * New *3() lists
 *
 * Instead of allocing additional list of dual-pointer structs, we add
 * 'next' field to the beginning of real struct. C allows us to point
 * to that first field independently of which struct is it, so we can
 * use some type-indepdendent functions for that. The main target is
 * to totally remove old functions, but leave 'list_t'.
 *
 * Then, for example, session_t would point to first entry of userlist
 * (as userlist_t*), and that entry would point to second one, second
 * one to third, etc. But if we want to group few userlist entries
 * independently of their original structure, we could just catch them
 * in standard list_t and use its' 'next' field.
 */

struct list {
		/* it is important that we keep 'next' first field,
		 * because C allows us to call first field of any structure
		 * without knowing its' type
		 *
		 * so *3() would work peacefully w/ both list_t and not-list_t lists */
	struct list *next;

	void *data;
};

typedef struct list *list_t;

#ifndef EKG2_WIN32_NOFUNCTION
#define LIST_ADD_COMPARE(x, type)			int x(const type data1, const type data2)
#define LIST_ADD_SORTED(list, data, comp)		list_add_sorted(list, data, (void *) comp)
#define LIST_ADD_SORTED2(list, data, comp)		list_add_sorted3((list_t *) (void *) list, (list_t) data, (void *) comp)
#define LIST_ADD_BEGINNING2(list, data)			list_add_beginning3((list_t *) (void *) list, (list_t) data)
#define LIST_ADD2(list, data)				list_add3((list_t *) (void *) list, (list_t) data)

#define LIST_COUNT2(list)				list_count((list_t) list)
#define LIST_GET_NTH2(list, id)				list_get_nth3((list_t) list, id)
#define LIST_RESORT(list, comp)				list_resort(list, (void *) comp)
#define LIST_RESORT2(list, comp)			list_resort3((list_t *) (void *) list, (void *) comp)

#define LIST_REMOVE(list, data, func)			list_remove2(list, data, (void *) func)
#define LIST_REMOVE2(list, elem, func)			list_remove3((list_t *) (void *) list, (list_t) elem, (void *) func)
#define LIST_UNLINK2(list, elem)			list_unlink3((list_t *) (void *) list, (list_t) elem)
#define LIST_FREE_ITEM(x, type)				void x(type data)

#define LIST_DESTROY(list, func)			list_destroy2(list, (void *) func)
#define LIST_DESTROY2(list, func)			list_destroy3((list_t) list, (void *) func)

void *list_add(list_t *list, void *data);
void *list_add_beginning(list_t *list, void *data);
void *list_add_sorted(list_t *list, void *data, int (*comparision)(void *, void *));

void *list_add3(list_t *list, list_t new_);
void *list_add_beginning3(list_t *list, list_t new_);
void *list_add_sorted3(list_t *list, list_t new_, int (*comparision)(void *, void *));


int list_count(list_t list);
void *list_get_nth(list_t list, int id);
void *list_get_nth3(list_t list, int id);
void list_resort(list_t *list, int (*comparision)(void *, void *));
void list_resort3(list_t *list, int (*comparision)(void *, void *));

int list_remove(list_t *list, void *data, int free_data);
int list_remove2(list_t *list, void *data, void (*func)(void *));
void *list_remove3(list_t *list, list_t elem, void (*func)(list_t));
void *list_remove3i(list_t *list, list_t elem, void (*func)(list_t data));
void *list_unlink3(list_t *list, list_t elem);

int list_destroy(list_t list, int free_data);
int list_destroy2(list_t list, void (*func)(void *));
int list_destroy3(list_t list, void (*func)(void *));

void list_cleanup(list_t *list);
int list_remove_safe(list_t *list, void *data, int free_data);
#endif

/*
 * typedef string_t
 *
 * prosty typ tekstowy pozwalaj�cy tworzy� ci�gi tekstowe o dowolnej
 * d�ugo�ci bez obawy o rozmiar bufora. ci�g tekstowy jest dost�pny
 * przez pole `str'. nie nale�y go zmienia� bezpo�rednio. przyk�adowy
 * kod:
 *
 *     string_t s;
 *
 *     s = string_init("ala");
 *     string_append_c(s, ' ');
 *     string_append(s, "ma kota");
 *     printf("%s\n", s->str);
 *     string_free(s, 1);
 */

struct string {
	char *str;
	int len, size;
};

typedef struct string *string_t;

#ifndef EKG2_WIN32_NOFUNCTION

string_t string_init(const char *str);
int string_append(string_t s, const char *str);
int string_append_n(string_t s, const char *str, int count);
int string_append_c(string_t s, char ch);
int string_append_raw(string_t s, const char *str, int count);
int string_append_format(string_t s, const char *format, ...);
void string_insert(string_t s, int index, const char *str);
void string_insert_n(string_t s, int index, const char *str, int count);
void string_remove(string_t s, int count);
void string_clear(string_t s);
char *string_free(string_t s, int free_string);

/* tablice stringow */
char **array_make(const char *string, const char *sep, int max, int trim, int quotes);
char *array_join(char **array, const char *sep);
char *array_join_count(char **array, const char *sep, int count);

int array_add(char ***array, char *string);
int array_add_check(char ***array, char *string, int casesensitive);
int array_count(char **array);
int array_contains(char **array, const char *string, int casesensitive);
int array_item_contains(char **array, const char *string, int casesensitive);
char *array_shift(char ***array);
void array_free(char **array);
void array_free_count(char **array, int count);

/* rozszerzenia libc�w */

const char *itoa(long int i);
const char *cssfind(const char *haystack, const char *needle, const char sep, int caseinsensitive);

#endif

char *escape(const char *src);
char *unescape(const char *src);

/*
 * handle private data
 */
typedef struct private_data_s {
	struct private_data_s *next;

	char *name;
	char *value;
} private_data_t;

int private_item_get_safe(private_data_t **data, const char *item_name, char **result);
const char *private_item_get(private_data_t **data, const char *item_name);

int private_item_get_int_safe(private_data_t **data, const char *item_name, int *result);
int private_item_get_int(private_data_t **data, const char *item_name);

void private_item_set(private_data_t **data, const char *item_name, const char *value);
void private_item_set_int(private_data_t **data, const char *item_name, int value);

void private_items_destroy(private_data_t **data);

#ifdef __cplusplus
}
#endif

#endif /* __EKG_DYNSTUFF_H */

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
