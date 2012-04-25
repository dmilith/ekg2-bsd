/* $Id$ */

/*
 *  (C) Copyright 2001-2003 Wojtek Kaniewski <wojtekka@irc.pl>
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "dynstuff.h"
#include "dynstuff_inline.h"
#include "xmalloc.h"

/*
 * list_add_sorted()
 *
 * dodaje do listy dany element. przy okazji mo�e te� skopiowa� zawarto��.
 * je�li poda si� jako ostatni parametr funkcj� por�wnuj�c� zawarto��
 * element�w, mo�e posortowa� od razu.
 *
 *  - list - wska�nik do listy,
 *  - data - wska�nik do elementu,
 *  - alloc_size - rozmiar elementu, je�li chcemy go skopiowa�.
 *
 * zwraca wska�nik zaalokowanego elementu lub NULL w przpadku b��du.
 */
void *list_add_sorted(list_t *list, void *data, int (*comparision)(void *, void *))
{
	list_t new_, tmp;

	if (!list) {
		errno = EFAULT;
		return NULL;
	}

	new_ = xmalloc(sizeof(struct list));

	new_->data = data;
	new_->next = NULL;
	/*new_->prev = NULL;*/

	if (!(tmp = *list)) {
		*list = new_;
	} else {
		if (!comparision) {
			while (tmp->next)
				tmp = tmp->next;
			tmp->next = new_;
		} else {
			list_t prev = NULL;
			
			while (comparision(new_->data, tmp->data) > 0) {
				prev = tmp;
				tmp = tmp->next;
				if (!tmp)
					break;
			}
			
			if (!prev) {
				new_->next = *list;
				*list = new_;
			} else {
				prev->next = new_;
				new_->next = tmp;
			}
		}
	}

	return new_->data;
}

/**
 * list_add_beginning()
 *
 * Add item @a data to the begining of the @a list<br>
 * (Once again), item will be added at begining of the list - as <b>first</b> item<br>
 *
 * @sa list_add()
 * @sa list_remove()
 */

void *list_add_beginning(list_t *list, void *data) {

	list_t new_;

	if (!list) {
		errno = EFAULT;
		return NULL;
	}

	new_ = xmalloc(sizeof(struct list));
	new_->next = *list;
	*list	  = new_;

	new_->data = data;

	return new_->data;

}

/**
 * list_add()
 *
 * Add item @a data to @a list without sorting<br>
 * Item will be added at end of list - as <b>last</b> item<br>
 * Wrapper to: <code>list_add_sorted(list, data, alloc_size, NULL)</code>
 *
 * @sa list_remove()
 * @sa list_add_beginning() - If you can have items of list in reverse sequence [third_added_item, second_added_item, first_added_item] and not sorted
 * @sa list_add_sorted()
 */

void *list_add(list_t *list, void *data)
{
	return list_add_sorted(list, data, NULL);
}

void *list_add_sorted3(list_t *list, list_t new_, int (*comparision)(void *, void *))
{
	list_t tmp;

	if (!list) {
		errno = EFAULT;
		return NULL;
	}

	new_->next = NULL;
	if (!(tmp = *list)) {
		*list = new_;
	} else {
		if (!comparision) {
			while (tmp->next)
				tmp = tmp->next;
			tmp->next = new_;
		} else {
			list_t prev = NULL;
			
			while (comparision(new_, tmp) > 0) {
				prev = tmp;
				tmp = tmp->next;
				if (!tmp)
					break;
			}
			
			if (!prev) {
				new_->next = *list;
				*list = new_;
			} else {
				prev->next = new_;
				new_->next = tmp;
			}
		}
	}

	return new_;
}

void *list_add_beginning3(list_t *list, list_t new_)
{
	if (!list) {
		errno = EFAULT;
		return NULL;
	}

	new_->next = *list;
	*list	  = new_;

	return new_;

}

void *list_add3(list_t *list, list_t new_)
{
	return list_add_sorted3(list, new_, NULL);
}

/**
 * list_remove_safe()
 *
 * Remove item @a data from list_t pointed by @a list.<br>
 * <b>Don't</b> free whole list_t item struct. only set item_list_t->data to NULL<br>
 *
 * @note XXX, add note here why we should do it.
 *
 * @param list - pointer to list_t
 * @param data - data to remove from @a list
 * @param free_data -if set and item was found it'll call xfree() on it.
 *
 * @sa list_cleanup() - to remove NULL items from list.
 */

int list_remove_safe(list_t *list, void *data, int free_data) {
	list_t tmp;

	if (!list) {
		errno = EFAULT;
		return -1;
	}

	for (tmp = *list; tmp; tmp = tmp->next) {
		if (tmp->data == data) {
			if (free_data)
				xfree(tmp->data);
			tmp->data = NULL;
			return 0;
		}
	}

	errno = ENOENT;
	return -1;
}

/**
 * list_cleanup()
 *
 * Remove from list_t all items with l->data set to NULL.<br>
 * Use with list_remove_safe() after list is not in use.
 */

void list_cleanup(list_t *list) {
	list_t tmp;
	list_t last = NULL;

	if (!list)
		return;

	for (tmp = *list; tmp;) {
		if (tmp->data == NULL) {
			list_t cur = tmp;

			tmp = tmp->next;		/* move to next item */

			if (!last)
				*list = tmp;		/* repoint list to next item */	
			else
				last->next = tmp;	/* repoint last->next to next item */

			xfree(cur);			/* free current item struct */
		} else {
			last = tmp;
			tmp = tmp->next;
		}
	}
}

int list_remove2(list_t *list, void *data, void (*func)(void *data)) {
	list_t tmp, last = NULL;

	if (!list) {
		errno = EFAULT;
		return -1;
	}

	tmp = *list;
	if (tmp && tmp->data == data) {
		*list = tmp->next;
	} else {
		for (; tmp && tmp->data != data; tmp = tmp->next)
			last = tmp;
		if (!tmp) {
			errno = ENOENT;
			return -1;
		}
		last->next = tmp->next;
	}

	if (func && tmp->data)
		func(tmp->data);
	xfree(tmp);

	return 0;
}

void *list_remove3(list_t *list, list_t elem, void (*func)(list_t data)) {
	list_t tmp, last = NULL;
	void *ret = NULL;

	if (!list) {
		errno = EFAULT;
		return ret;
	}

	tmp = *list;
	if (tmp && tmp == elem) {
		*list = ret = tmp->next;
	} else {
		for (; tmp && tmp != elem; tmp = tmp->next)
			last = tmp;
		if (!tmp) {
			errno = ENOENT;
			return ret;
		}
		last->next = ret = tmp->next;
	}

	if (func)
		func(tmp);
	xfree(tmp);

	return ret;
}

void *list_remove3i(list_t *list, list_t elem, void (*func)(list_t data)) {
	list_t tmp, last = NULL;
	void *ret = NULL;

	if (!list) {
		errno = EFAULT;
		return ret;
	}

	tmp = *list;
	if (tmp && tmp == elem) {
		*list = tmp->next;
		ret = list;
	} else {
		for (; tmp && tmp != elem; tmp = tmp->next)
			last = tmp;
		if (!tmp) {
			errno = ENOENT;
			return ret;
		}
		last->next = tmp->next;
		ret = last;
	}

	if (func)
		func(tmp);
	xfree(tmp);

	return ret;
}

void *list_unlink3(list_t *list, list_t elem) {
	list_t tmp, last = NULL;
	void *ret = NULL;

	if (!list) {
		errno = EFAULT;
		return ret;
	}

	tmp = *list;
	if (tmp && tmp == elem) {
		*list = ret = tmp->next;
	} else {
		for (; tmp && tmp != elem; tmp = tmp->next)
			last = tmp;
		if (!tmp) {
			errno = ENOENT;
			return ret;
		}
		last->next = ret = tmp->next;
	}

	return ret;
}

/**
 * list_remove()
 *
 * Remove item @a data from list_t pointed by @a list
 *
 * @param list - pointer to list_t
 * @param data - data to remove from @a list
 * @param free_data - if set and item was found it'll call xfree() on it.
 * @sa list_destroy() - to remove whole list
 *
 * @return	 0 if item was founded, and was removed from list_t pointed by @a list<br>
 *		-1 and errno set to EFAULT, if @a list was NULL<br>
 *		-1 and errno set to ENOENT, if item was not found
 */

int list_remove(list_t *list, void *data, int free_data) {
	return list_remove2(list, data, free_data ? xfree : NULL);
}

/**
 * list_get_nth()
 *
 * Get n'th item from list_t
 *
 * @param list - list_t
 * @param id - n'th item [list items are numerated from 1]
 *
 * @return n'th item (list->data) if found, or NULL with errno set to ENOENT
 */

void *list_get_nth(list_t list, int id) {
	while (list) {
		if ((--id) == 0) {
			/* errno = !ENOENT; */
			return list->data;
		}

		list = list->next;
	}

	errno = ENOENT;
	return NULL;
}

void *list_get_nth3(list_t list, int id) {
	while (list) {
		if ((--id) == 0)
			return list;

		list = list->next;
	}

	errno = ENOENT;
	return NULL;
}

void list_resort(list_t *list, int (*comparision)(void *, void *)) {
	list_t tmplist = NULL;
	list_t l = *list;

	while (l) {
		list_t cur = l;

		l = l->next;

		list_add_sorted(&tmplist, cur->data, comparision);

		xfree(cur);
	}

	*list = tmplist;
}

void list_resort3(list_t *list, int (*comparision)(void *, void *)) {
	list_t tmplist = NULL;
	list_t l = *list;

	while (l) {
		list_t cur = l;

		l = l->next;

		list_add_sorted3(&tmplist, cur, comparision);
	}

	*list = tmplist;
}

/**
 * list_count()
 *
 * @param list - list_t
 *
 * @return items count on list_t @a list.
 */

int list_count(list_t list)
{
	int count = 0;

	for (; list; list = list->next)
		count++;

	return count;
}

int list_destroy2(list_t list, void (*func)(void *)) {
	list_t tmp;
	
	while (list) {
		if (func && list->data)
			func(list->data);

		tmp = list->next;

		xfree(list);

		list = tmp;
	}

	return 0;
}

int list_destroy3(list_t list, void (*func)(void *)) {
	list_t tmp;
	
	while (list) {
		if (func)
			func(list);

		tmp = list->next;

		xfree(list);

		list = tmp;
	}

	return 0;
}

/**
 * list_destroy()
 *
 * Destroy all items from list_t @a list
 *
 * @note It doesn't take pointer to list_t, and it don't cleanup memory with \\0, so after list_destroy() you must remember that
 *	@a list is <b>unaccessible</b>. So if you have list as global variable, or if you keep pointer to it in some struct.. you must NULL pointer.
 *	so always do: <br>
 *	<code>
 *		list_destroy(my_list, free_data);
 *		my_list = NULL;
 *	</code>
 *
 * @param list - list_t
 * @param free_data - if set we will call xfree() on each item data
 * @sa list_remove() - to remove specified item.
 *
 * @return 0
 */

int list_destroy(list_t list, int free_data) {
	return list_destroy2(list, free_data ? xfree : NULL);
}

/*
 * string_realloc()
 *
 * upewnia si�, �e w stringu b�dzie wystarczaj�co du�o miejsca.
 *
 *  - s - ci�g znak�w,
 *  - count - wymagana ilo�� znak�w (bez ko�cowego '\0').
 */
static void string_realloc(string_t s, int count)
{
	char *tmp;
	
	if (s->str && (count + 1) <= s->size)
		return;
	
	tmp = xrealloc(s->str, count + 81);
	if (!s->str)
		*tmp = 0;
	tmp[count + 80] = 0;
	s->size = count + 81;
	s->str = tmp;
}

/**
 * string_append_c()
 *
 * Append to string_t @a s char @a c.
 *
 * @param s - string_t
 * @param c - char to append
 *
 * @return	 0 on success<br>
 *		-1 and errno set to EFAULT if input params were wrong (s == NULL || format == NULL)
 */

int string_append_c(string_t s, char c)
{
	if (!s) {
		errno = EFAULT;
		return -1;
	}
	
	string_realloc(s, s->len + 1);

	s->str[s->len + 1] = 0;
	s->str[s->len++] = c;

	return 0;
}

/**
 * string_append_n()
 *
 * Append to string_t @a s, first @a count chars, from @a str<br>
 *
 * @param s	- string_t
 * @param str	- buffer to append.
 * @param count - how many chars copy copy from @a str, or -1 to copy whole.
 *
 * @todo	We append here NUL terminated string, so maybe let's <b>always</b> do <code>count = xstrnlen(str, count);</code>?<br>
 *		Because now programmer can pass negative value, and it'll possible do SIGSEGV<br>
 *		Also we can allocate less memory for string, when for example str[count-3] was NUL char.<br>
 *
 * @return	 0 on success<br>
 *		-1 and errno set to EFAULT if input params were wrong (s == NULL || str == NULL)
 */

int string_append_n(string_t s, const char *str, int count)
{
	if (!s || !str) {
		errno = EFAULT;
		return -1;
	}

	if (count == -1)
		count = xstrlen(str);

	string_realloc(s, s->len + count);

	s->str[s->len + count] = 0;
	xstrncpy(s->str + s->len, str, count);

	s->len += count;

	return 0;
}

/**
 * string_append_format()
 *
 * Append to string_t @a s, formatted output of @a format + params<br>
 * Equivalent to:<br>
 *	<code>
 *		char *tmp = saprintf(format, ...);<br>
 *		string_append(s, tmp);<br>
 *		xfree(tmp);<br>
 *	</code>
 *
 * @note For more details about string formating functions read man 3 vsnprintf
 *
 * @sa string_append()	- If you want/can use non-formating function..
 * @sa saprintf()	- If you want to format output but to normal char *, not to string_t
 *
 * @return	 0 on success<br>
 *		-1 and errno set to EFAULT if input params were wrong (s == NULL || format == NULL)
 */

int string_append_format(string_t s, const char *format, ...) {
	va_list ap;
	char *formatted;

	if (!s || !format) {
		errno = EFAULT;
		return -1;
	}

	va_start(ap, format);
	formatted = vsaprintf(format, ap);
	va_end(ap);
	
	if (!formatted) 
		return 0;
	
	string_append_n(s, formatted, -1);
	xfree(formatted);
	return 0;
}

/**
 * string_append_raw()
 *
 * Append to string_t @a s, @a count bytes from memory pointed by @a str<br>
 * 
 * @sa string_append_n() - If you want to append NUL terminated (C-like) String
 * @todo XXX Protect from negative count (and less than -1) ?
 */

int string_append_raw(string_t s, const char *str, int count) {
	if (!s || !str) {
		errno = EFAULT;
		return -1;
	}

	if (count == -1) return string_append_n(s, str, -1);

	string_realloc(s, s->len + count);

	s->str[s->len + count] = 0;
	memcpy(s->str + s->len, str, count);

	s->len += count;

	return 0;
}

/**
 * string_append()
 *
 * Append to string_t @a s, NUL terminated string pointed by str<br>
 * Wrapper to:<code>string_append_n(s, str, -1)</code>
 *
 * @sa string_append_n()
 */

int string_append(string_t s, const char *str)
{
	return string_append_n(s, str, -1);
}

/*
 * string_insert_n()
 *
 * wstawia tekst w podane miejsce bufora.
 *  
 *  - s - ci�g znak�w,
 *  - index - miejsce, gdzie mamy wpisa� (liczone od 0),
 *  - str - tekst do dopisania,
 *  - count - ilo�� znak�w do dopisania (-1 znaczy, �e wszystkie).
 */
void string_insert_n(string_t s, int index, const char *str, int count)
{
	if (!s || !str)
		return;

	if (count == -1)
		count = xstrlen(str);

	if (index > s->len)
		index = s->len;
	
	string_realloc(s, s->len + count);

	memmove(s->str + index + count, s->str + index, s->len + 1 - index);
	memmove(s->str + index, str, count);

	s->len += count;
}

/**
 * string_insert()
 *
 * Insert given text (@a str) to given string_t (@a s) at given pos (@a index)<br>
 * Wrapper to: <code>string_insert_t(s, index, str, -1)</code>
 *
 * @param s	- string_t
 * @param index - pos
 * @param str	- text 
 *
 * @sa string_insert_n()
 */

void string_insert(string_t s, int index, const char *str)
{
	string_insert_n(s, index, str, -1);
}

/**
 * string_init()
 *
 * init string_t struct, allocating memory for string passed by @a value, and setting internal string_t data.
 *
 *  @param value - if NULL char buffer will be inited with "", otherwise with given @a value.
 *  @sa string_free() - to free memory used by string_t
 *
 *  @return pointer to allocated string_t struct.
 */
string_t string_init(const char *value) {
	string_t tmp = xmalloc(sizeof(struct string));
	size_t valuelen;

	if (!value)
		value = "";

	valuelen = xstrlen(value);

	tmp->str = xstrdup(value);
	tmp->len = valuelen;
	tmp->size = valuelen + 1;

	return tmp;
}

/**
 * string_init_n()
 *
 * init string_t struct, allocating memory string of length n
 *
 *  @param n - number of bytes to allocate
 *  @sa string_free() - to free memory used by string_t
 *
 *  @return pointer to allocated string_t struct.
 */
string_t string_init_n(int n)
{
	string_t tmp;

	/* this is an error */
	if (n <= 0)
		return NULL;

	tmp = xmalloc(sizeof(struct string));
	tmp->str = (char *)xmalloc(n);
	tmp->len = 0;
	tmp->size = n;

	return tmp;
}

/**
 * string_remove()
 *
 * Remove first @a count chars from string.
 *
 */

void string_remove(string_t s, int count) {
	if (!s || count <= 0)
		return;
	
	if (count >= s->len) {
		/* string_clear() */
		s->str[0]	= '\0';
		s->len		= 0;

	} else {
		memmove(s->str, s->str + count, s->len - count);
		s->len -= count;
		s->str[s->len] = '\0';
	}
}

/**
 * string_clear()
 *
 * Clear s->str (s->str[0] == '\\0')<br>
 * If memory allocated by string_t @a s was larger than 160, than decrease to 80
 *
 *  @param s - string_t
 *  @sa string_free() - To free memory allocated by string_t
 */

void string_clear(string_t s)
{
	if (!s)
		return;
	if (s->size > 160) {
		s->str = xrealloc(s->str, 80);
		s->size = 80;
	}

	s->str[0] = 0;
	s->len = 0;
}

/**
 * string_free()
 *
 * Cleanup memory after string_t @a s, and perhaps (if @a free_string set) cleanup memory after char buffer.
 *
 * @param s		- string_t which we want to free.
 * @param free_string	- do we want to free memory after char buffer?
 * @sa string_clear()	- if you just want to clear saved char buffer, and you don't want to free internal string_t struct.
 *
 * @return	if @a free_string != 0 always NULL<br>
 *		else returns saved char buffer, which need be free()'d after use by xfree()
 */

char *string_free(string_t s, int free_string)
{
	char *tmp = NULL;

	if (!s)
		return NULL;

	if (free_string)
		xfree(s->str);
	else
		tmp = s->str;

	xfree(s);

	return tmp;
}

/*
 * itoa()
 *
 * prosta funkcja, kt�ra zwraca tekstow� reprezentacj� liczby. w obr�bie
 * danego wywo�ania jakiej� funkcji lub wyra�enia mo�e by� wywo�ania 10
 * razy, poniewa� tyle mamy statycznych bufor�w. lepsze to ni� ci�g�e
 * tworzenie tymczasowych bufor�w na stosie i sprintf()owanie.
 *
 *  - i - liczba do zamiany.
 *
 * zwraca adres do bufora, kt�rego _NIE_NALE�Y_ zwalnia�.
 */
const char *itoa(long int i)
{
	static char bufs[10][16];
	static int index = 0;
	char *tmp = bufs[index++];

	if (index > 9)
		index = 0;
	
	snprintf(tmp, 16, "%ld", i);

	return tmp;
}

/*
 * array_make()
 *
 * tworzy tablic� tekst�w z jednego, rozdzielonego podanymi znakami.
 *
 *  - string - tekst wej�ciowy,
 *  - sep - lista element�w oddzielaj�cych,
 *  - max - maksymalna ilo�� element�w tablicy. je�li r�wne 0, nie ma
 *	    ogranicze� rozmiaru tablicy.
 *  - trim - czy wi�ksz� ilo�� element�w oddzielaj�cych traktowa� jako
 *	     jeden (na przyk�ad spacje, tabulacja itp.)
 *  - quotes - czy pola mog� by� zapisywane w cudzys�owiach lub
 *	       apostrofach z escapowanymi znakami.
 *
 * zaalokowan� tablic� z zaalokowanymi ci�gami znak�w, kt�r� nale�y
 * zwolni� funkcj� array_free()
 */
char **array_make(const char *string, const char *sep, int max, int trim, int quotes)
{
	const char *p, *q;
	char **result = NULL;
	int items = 0, last = 0;

	if (!string || !sep)
		goto failure;

	for (p = string; ; ) {
		int len = 0;
		char *token = NULL;

		if (max && items >= max - 1)
			last = 1;
		
		if (trim) {
			while (*p && xstrchr(sep, *p))
				p++;
			if (!*p)
				break;
		}

		if (quotes && (*p == '\'' || *p == '\"')) {
			char sep = *p;
			char *r;

			for (q = p + 1, len = 0; *q; q++, len++) {
				if (*q == '\\') {
					q++;
					if (!*q)
						break;
				} else if (*q == sep)
					break;
			}

			if (last && q[0] && q[1])
				goto way2;

			len++;

			r = token = xmalloc(len + 1);
			for (q = p + 1; *q; q++, r++) {
				if (*q == '\\') {
					q++;

					if (!*q)
						break;

					switch (*q) {
						case 'n':
							*r = '\n';
							break;
						case 'r':
							*r = '\r';
							break;
						case 't':
							*r = '\t';
							break;
						default:
							*r = *q;
					}
				} else if (*q == sep) {
					break;
				} else 
					*r = *q;
			}

			*r = 0;
			
			p = (*q) ? q + 1 : q;

		} else {
way2:
			for (q = p, len = 0; *q && (last || !xstrchr(sep, *q)); q++, len++);
			token = xstrndup(p, len);
			p = q;
		}
		
		result = xrealloc(result, (items + 2) * sizeof(char*));
		result[items] = token;
		result[++items] = NULL;

		if (!*p)
			break;

		p++;
	}

failure:
	if (!items)
		result = xcalloc(1, sizeof(char*));

	return result;
}

/*
 * array_count()
 *
 * zwraca ilo�� element�w tablicy.
 */
int array_count(char **array)
{
	int result = 0;

	if (!array)
		return 0;

	while (*array) {
		result++;
		array++;
	}

	return result;
}

/* 
 * array_add()
 *
 * dodaje element do tablicy.
 */
int array_add(char ***array, char *string)
{
	int count = array_count(*array);

	*array = xrealloc(*array, (count + 2) * sizeof(char*));
	(*array)[count + 1] = NULL;
	(*array)[count] = string;

	return count + 1;
}

/*
 * array_add_check()
 * 
 * dodaje element do tablicy, uprzednio sprawdzaj�c
 * czy taki ju� w niej nie istnieje
 *
 *  - array - tablica,
 *  - string - szukany ci�g znak�w,
 *  - casesensitive - czy mamy zwraca� uwag� na wielko�� znak�w?
 *
 * zwraca zero w przypadku, je�li ci�g ju� jest na li�cie
 * lub aktualn� liczb� ci�g�w na li�cie, po dodaniu
 */ 
int array_add_check(char ***array, char *string, int casesensitive)
{
	if (!array_item_contains(*array, string, casesensitive))
		return array_add(array, string);
	else
		xfree(string);
	return 0;
}

/*
 * array_join()
 *
 * ��czy elementy tablicy w jeden string oddzielaj�c elementy odpowiednim
 * separatorem.
 *
 *  - array - wska�nik do tablicy,
 *  - sep - seperator.
 *
 * zwr�cony ci�g znak�w nale�y zwolni�.
 */
char *array_join(char **array, const char *sep)
{
	string_t s = string_init(NULL);
	int i;

	if (!array)
		return string_free(s, 0);

	for (i = 0; array[i]; i++) {
		if (i)
			string_append(s, sep);

		string_append(s, array[i]);
	}

	return string_free(s, 0);
}

char *array_join_count(char **array, const char *sep, int count) {
	string_t s = string_init(NULL);

	if (array) {
		int i;

		for (i = 0; i < count; i++) {
			if (array[i])
				string_append(s, array[i]);
			
			if (i != count-1)	
				string_append(s, sep);
		}
	}

	return string_free(s, 0);
}

/*
 * array_contains()
 *
 * stwierdza, czy tablica zawiera podany element.
 *
 *  - array - tablica,
 *  - string - szukany ci�g znak�w,
 *  - casesensitive - czy mamy zwraca� uwag� na wielko�� znak�w?
 *
 * 0/1
 */
int array_contains(char **array, const char *string, int casesensitive)
{
	int i;

	if (!array || !string)
		return 0;

	for (i = 0; array[i]; i++) {
		if (casesensitive && !xstrcmp(array[i], string))
			return 1;
		if (!casesensitive && !xstrcasecmp(array[i], string))
			return 1;
	}

	return 0;
}

/*
 * array_item_contains()
 *
 * stwierdza czy w tablicy znajduje si� element zawieraj�cy podany ci�g
 *
 *  - array - tablica,
 *  - string - szukany ci�g znak�w,
 *  - casesensitive - czy mamy zwraca� uwag� na wielko�� znak�w?
 *
 * 0/1
 */
int array_item_contains(char **array, const char *string, int casesensitive)
{
	int i;

	if (!array || !string)
		return 0;

	for (i = 0; array[i]; i++) {
		if (casesensitive && xstrstr(array[i], string))
			return 1;
		if (!casesensitive && xstrcasestr(array[i], string))
			return 1;
	}

	return 0;
}

char *array_shift(char ***array)
{
	int count;
	char *out;

	if (!(count = array_count(*array)))
		return NULL;

	out = (*array)[0];
	memmove((*array), &(*array)[1], count * sizeof(char **));
	count--;

	if (count == 0) {
		xfree(*array);
		*array = NULL;
	}
	return out;
}

/*
 * array_free()
 *
 * zwalnia pamie� zajmowan� przez tablic�.
 */
void array_free(char **array)
{
	char **tmp;

	if (!array)
		return;

	for (tmp = array; *tmp; tmp++)
		xfree(*tmp);

	xfree(array);
}

void array_free_count(char **array, int count) {
	char **tmp;

	if (!array)
		return;

	for (tmp = array; count; tmp++, count--)
		xfree(*tmp);

	xfree(array);
}

/**
 * cssfind()
 *
 * Short for comma-separated string find, does check whether given string contains given element.
 * It's works like array_make()+array_contains(), but it's hell simpler and faster.
 *
 * @param haystack		- comma-separated string to search.
 * @param needle		- element to search for.
 * @param sep			- separator.
 * @param caseinsensitive	- take a wild guess.
 *
 * @return Pointer to found element on success, or NULL on failure.
 */
const char *cssfind(const char *haystack, const char *needle, const char sep, int caseinsensitive) {
	const char *comma = haystack-1;
	const int needlelen = xstrlen(needle);

	do {
		comma += xstrspn(comma+1, " \f\n\r\t\v")+1;
		if (!(caseinsensitive ? xstrncasecmp(comma, needle, needlelen) : xstrncmp(comma, needle, needlelen))) {
			const char *p, *q;

			p = comma + needlelen;
			if (!(q = xstrchr(p, sep)))
				q = p + xstrlen(p);
			if (q-p <= xstrspn(p, " \f\n\r\t\v")) /* '<' shouldn't happen */
				return comma;
		}
	} while (sep && (comma = xstrchr(comma, sep)));

	return NULL;
#if 0 /* old, exact-match code; uncomment when needed */
{
	const char *r = haystack-1;
	const int needlelen = xstrlen(needle);

	if (needlelen == 0) { /* workaround for searching '' */
		char c[3];
		c[0] = sep;	c[1] = sep;	c[2] = '\0';

		r = xstrstr(haystack, c);
		if (r) /* return pointer to 'free space' between seps */
			r++;
	} else {
		while ((r = (caseinsensitive ? xstrcasestr(r+1, needle) : xstrstr(r+1, needle))) &&
				(((r != haystack) && ((*(r-1) != sep)))
				|| ((*(r+needlelen) != '\0') && (*(r+needlelen) != sep)))) {};
	}

	return r;
}
#endif
}

/*
 * eskejpuje:
 *
 * - \ -> \\
 *
 * oraz wyst�pienia znak�w ze stringa esc:
 *
 * - 0x07 (\a) -> \a
 * - 0x08 (\b) -> \b
 * - 0x09 (\t) -> \t
 * - 0x0A (\n) -> \n
 * - 0x0B (\v) -> \v
 * - 0x0C (\f) -> \f
 * - 0x0D (\r) -> \r
 * - pozosta�e -> \xXX (szesnastkowa reprezentacja)
 *
 * je�eli kt�ry� z wymienionych wy�ej znak�w (np. \a) nie wyst�puje 
 * w stringu to zostanie przepisany tak jak jest, bez eskejpowania.
 *
 * zwraca nowego, zaalokowanego stringa.
 */
char *escape(const char *src) {
	static const char esc[] = "\r\n";
	string_t dst;

	if (!src)
		return NULL;

	dst = string_init(NULL);

	for (; *src; src++) {
		char ch = *src;
		static const char esctab[] = "abtnvfr";

		if (!(ch == '\\' || strchr(esc, ch))) {
			string_append_c(dst, ch);
			continue;
		}

		string_append_c(dst, '\\');

		if (ch >= 0x07 && ch <= 0x0D)
			string_append_c(dst, esctab[ch - 0x07]);
		else if (ch == '\\')
			string_append_c(dst, '\\');
		else {
			char s[5];
			snprintf(s, sizeof(s), "x%02X", (unsigned char) ch);
			string_append(dst, s);
		}
	}

	return string_free(dst, 0);
}

/*
 * dekoduje stringa wyeskejpowanego przy pomocy escape.
 *
 * - \\ -> \
 * - \xXX -> szesnastkowo zdekodowany znak
 * - \cokolwiekinnego -> \cokolwiekinnego
 *
 * zwraca nowego, zaalokowanego stringa.
 */
char *unescape(const char *src) {
	int state = 0;
	string_t buf;
	unsigned char hex_msb = 0;

	if (!src)
		return NULL;

	buf = string_init(NULL);

	for (; *src; src++) {
		char ch = *src;

		if (state == 0) {		/* normalny tekst */
			/* sprawdzamy czy mamy cos po '\\', bo jezeli to ostatni 
			 * znak w stringu, to nie zostanie nigdy dodany. */
			if (ch == '\\' && *(src + 1)) {
				state = 1;
				continue;
			}
			string_append_c(buf, ch);
		} else if (state == 1) {	/* kod ucieczki */
			if (ch == 'a')
				ch = '\a';
			else if (ch == 'b')
				ch = '\b';
			else if (ch == 't')
				ch = '\t';
			else if (ch == 'n')
				ch = '\n';
			else if (ch == 'v')
				ch = '\v';
			else if (ch == 'f')
				ch = '\f';
			else if (ch == 'r')
				ch = '\r';
			else if (ch == 'x' && *(src + 1) && *(src + 2)) {
				state = 2;
				continue;
			} else if (ch != '\\')
				string_append_c(buf, '\\');	/* fallback - nieznany kod */
			string_append_c(buf, ch);
			state = 0;
		} else if (state == 2) {	/* pierwsza cyfra kodu szesnastkowego */
			hex_msb = ch;
			state = 3;
		} else if (state == 3) {	/* druga cyfra kodu szesnastkowego */
#define unhex(x) (unsigned char) ((x >= '0' && x <= '9') ? (x - '0') : \
	(x >= 'A' && x <= 'F') ? (x - 'A' + 10) : \
	(x >= 'a' && x <= 'f') ? (x - 'a' + 10) : 0)
			string_append_c(buf, unhex(ch) | (unhex(hex_msb) << 4));
#undef unhex
			state = 0;
		}
	}

	return string_free(buf, 0);
}

/*
 * handle private data
 */
static int private_data_cmp(private_data_t *item1, private_data_t *item2) {
	return xstrcmp(item1->name, item2->name);
}

static LIST_FREE_ITEM(private_data_free, private_data_t *) {
	xfree(data->name);
	xfree(data->value);
}

DYNSTUFF_LIST_DECLARE_SORTED(private_items, private_data_t, private_data_cmp, private_data_free,
	static __DYNSTUFF_ADD_SORTED,			/* private_items_add() */
	static __DYNSTUFF_REMOVE_SAFE,			/* private_items_remove() */
	__DYNSTUFF_DESTROY)				/* private_items_destroy() */

static private_data_t *private_item_find(private_data_t **data, const char *item_name) {
	private_data_t *item;
	int cmp;

	if (!item_name)
		return NULL;

	for (item = *data; item; item = item->next) {
		if ( !(cmp = xstrcmp(item->name, item_name)) )
			return item;
		if (cmp>0)
			return NULL;
	}

	return NULL;
}

int private_item_get_safe(private_data_t **data, const char *item_name, char **result) {
	private_data_t *item = private_item_find(data, item_name);

	if (item) {
		*result = item->value;
		return 1;
	}

	return 0;
}

const char *private_item_get(private_data_t **data, const char *item_name) {
	char *result = NULL;
	(void )private_item_get_safe(data, item_name, &result);

	return result;
}

int private_item_get_int_safe(private_data_t **data, const char *item_name, int *result) {
	char *tmp;
	if (!private_item_get_safe(data, item_name, &tmp))
		return 0;

	*result = atoi(tmp);
	return 1;
}

int private_item_get_int(private_data_t **data, const char *item_name) {
	int result = 0;
	(void) private_item_get_int_safe(data, item_name, &result);
	return result;
}

void private_item_set(private_data_t **data, const char *item_name, const char *value) {
	private_data_t *item = private_item_find(data, item_name);
	int unset = !(value && *value);

	if (item) {
		if (unset) {
			private_items_remove(data, item);
		} else if (xstrcmp(item->value, value)) {
			xfree(item->value);
			item->value = xstrdup(value);
		}
	} else if (!unset) {
		item = xmalloc(sizeof(private_data_t));
		item->name = xstrdup(item_name);
		item->value = xstrdup(value);
		private_items_add(data, item);
	}
}

void private_item_set_int(private_data_t **data, const char *item_name, int value) {
	private_item_set(data, item_name, value?itoa(value):NULL);
}

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
