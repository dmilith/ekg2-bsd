/* $Id$ */

/*
 *  (C) Copyright 2004 Piotr Kupisiewicz <deletek@ekg2.org>
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

#ifdef HAVE_LIBGPM
#	include <gpm.h>
#endif

#include <stdlib.h>

#include "ecurses.h"

#include <ekg/bindings.h>
#include <ekg/debug.h>
#include <ekg/stuff.h>
#include <ekg/windows.h>
#include <ekg/xmalloc.h>

#include "old.h"
#include "bindings.h"
#include "contacts.h"
#include "mouse.h"

	/* imported bindings */
BINDING_FUNCTION(binding_previous_only_history);
BINDING_FUNCTION(binding_next_only_history);

int mouse_initialized = 0;

/* 
 * show_mouse_pointer()
 * 
 * should show mouse pointer 
 */
static void show_mouse_pointer(void) {
#ifdef HAVE_LIBGPM
	if (gpm_visiblepointer) {
		Gpm_Event event;

		Gpm_GetSnapshot(&event);
		Gpm_DrawPointer (event.x, event.y, gpm_consolefd);
	}
#endif
}

/*
 * ncurses_mouse_timer()
 * 
 * every second we should do something
 * it's done here
 */
static TIMER(ncurses_mouse_timer)
{
	if (type) return 0;
	show_mouse_pointer();
	return 0;
}

/*
 * ncurses_mouse_move_handler()
 * 
 * handler for move of mouse 
 */
static void ncurses_mouse_move_handler(int x, int y)
{
	/* debug("%d %d | %d\n", x, y); */

	/* add function that should be done when mouse move is done */
}

/*
 * ncurses_lastlog_mouse_handler()
 *
 * handler for mouse events in lastlog window
 */
void ncurses_lastlog_mouse_handler(int x, int y, int mouse_state) {
	window_t *w = window_find_sa(NULL, "__lastlog", 1);

	if (mouse_state == EKG_SCROLLED_UP) {
		binding_helper_scroll(w, -1);
	} else if (mouse_state == EKG_SCROLLED_DOWN) {
		binding_helper_scroll(w, +1);
	} else if (mouse_state == EKG_BUTTON3_DOUBLE_CLICKED) {
		window_kill(w);
		ncurses_resize();
		ncurses_commit();
	}
}

/*
 * ncurses_main_window_mouse_handler()
 *
 * handler for mouse events in main window
 */
void ncurses_main_window_mouse_handler(int x, int y, int mouse_state)
{
	if (mouse_state == EKG_SCROLLED_UP) {
		binding_helper_scroll(window_current, -5);
	} else if (mouse_state == EKG_SCROLLED_DOWN) {
		binding_helper_scroll(window_current, +5);
	}
}

/* 
 * ncurses_mouse_clicked_handler()
 * 
 * handler for clicked of mouse
 */
void ncurses_mouse_clicked_handler(int x, int y, int mouse_flag)
{
	window_t *w;
#if 0
	char *tmp;

	switch (mouse_flag) {
		case EKG_BUTTON1_CLICKED:
			tmp = "button1_clicked";
			break;
		case EKG_BUTTON2_CLICKED:
			tmp = "button2_clicked";
			break;
		case EKG_BUTTON3_CLICKED:
			tmp = "button3_clicked";
			break;
		case EKG_UNKNOWN_CLICKED:
			tmp = "unknown_clicked";
			break;
		case EKG_BUTTON1_DOUBLE_CLICKED:
			tmp = "button1_d_clicked";
			break;
		case EKG_BUTTON2_DOUBLE_CLICKED:
			tmp = "button2_d_clicked";
			break;
		case EKG_BUTTON3_DOUBLE_CLICKED:
			tmp = "button3_d_clicked";
			break;
		case EKG_UNKNOWN_DOUBLE_CLICKED:
			tmp = "unknown_d_clicked";
			break;
		case EKG_SCROLLED_UP:
			tmp = "scrolled_up";
			break;
		case EKG_SCROLLED_DOWN:
			tmp = "scrolled down";
			break;
		default:
			tmp = "nothing";
			break;
	}

	/* debug("stalo sie: %s x: %d y: %d\n", tmp, x, y); */
#endif
	for (w = windows; w; w = w->next) {
		if (x > w->left && x <= w->left + w->width && y > w->top && y <= w->top + w->height) {
			ncurses_window_t *n;
			if (w->id == 0) { /* if we are reporting status window it means that we clicked 
					 * on window_current and some other functions should be called */
				ncurses_main_window_mouse_handler(x - w->left, y - w->top, mouse_flag);
				break;
			}
			
			n = w->priv_data;
			/* debug("window id:%d y %d height %d\n", w->id, w->top, w->height); */
			if (n->handle_mouse)
				n->handle_mouse(x - w->left, y - w->top, mouse_flag);
			break;
		}
	}

	if (!w) { /* special screen sections */
			/* input */
		if (y > stdscr->_maxy - input_size + 1) {
			y -= (stdscr->_maxy - input_size + 2);
			x--;

			if (input_size == 1) {
				if (mouse_flag == EKG_SCROLLED_UP)
					binding_previous_only_history(NULL);
				else if (mouse_flag == EKG_SCROLLED_DOWN)
					binding_next_only_history(NULL);
				else if (mouse_flag == EKG_BUTTON1_CLICKED) {
						/* the plugin already calculates offset incorrectly,
						 * so we shall follow it */
					const int promptlen	= ncurses_current ? ncurses_current->prompt_real_len : 0;
					const int linelen	= xwcslen(ncurses_line);

					line_index = x - promptlen + line_start;

					if (line_index < 0)
						line_index = 0;
					else if (line_index > linelen)
						line_index = linelen;
				}
			} else {
				if (mouse_flag == EKG_SCROLLED_UP) {
					if (lines_start > 2)
						lines_start -= 2;
					else
						lines_start = 0;
				} else if (mouse_flag == EKG_SCROLLED_DOWN) {
					const int lines_count = array_count((char **) ncurses_lines);

					if (lines_start < lines_count - 2)
						lines_start += 2;
					else
						lines_start = lines_count - 1;
				} else if (mouse_flag == EKG_BUTTON1_CLICKED) {
					const int lines_count = array_count((char **) ncurses_lines);

					lines_index = lines_start + y;
					if (lines_index >= lines_count)
						lines_index = lines_count - 1;
					line_index = x + line_start;
					ncurses_lines_adjust();
				}
			}
		} else if (y > stdscr->_maxy - input_size - config_statusbar_size + 1) {
			if (mouse_flag == EKG_SCROLLED_UP)
				command_exec(window_current->target, window_current->session, "/window prev", 0);
			else if (mouse_flag == EKG_SCROLLED_DOWN)
				command_exec(window_current->target, window_current->session, "/window next", 0);
		}
	}
}

#ifdef HAVE_LIBGPM
/*
 * ncurses_gpm_watch_handler()
 * 
 * handler for gpm events etc
 */
static WATCHER(ncurses_gpm_watch_handler)
{
	Gpm_Event event;

	if (type)
		return 0;

	Gpm_GetEvent(&event);

	/* przy double click nie powinno by� wywo�ywane single click */

	if (gpm_visiblepointer) GPM_DRAWPOINTER(&event);

	switch (event.type) {
		case GPM_MOVE:
			ncurses_mouse_move_handler(event.x, event.y);
			break;
		case GPM_DOUBLE + GPM_UP:
			{
				int mouse_state = EKG_UNKNOWN_DOUBLE_CLICKED;
				switch (event.buttons) {
					case GPM_B_LEFT:
						mouse_state = EKG_BUTTON1_DOUBLE_CLICKED;
						break;
					case GPM_B_RIGHT:
						mouse_state = EKG_BUTTON3_DOUBLE_CLICKED;
						break;
					case GPM_B_MIDDLE:
						mouse_state = EKG_BUTTON2_DOUBLE_CLICKED;
						break;
				}
				ncurses_mouse_clicked_handler(event.x, event.y, mouse_state);
				break;
			}
		case GPM_SINGLE + GPM_UP:
			{
				int mouse_state = EKG_UNKNOWN_CLICKED;
				switch (event.buttons) {
					case GPM_B_LEFT:
						mouse_state = EKG_BUTTON1_CLICKED;
						break;
					case GPM_B_RIGHT:
						mouse_state = EKG_BUTTON3_CLICKED;
						break;
					case GPM_B_MIDDLE:
						mouse_state = EKG_BUTTON2_CLICKED;
						break;
				}
				ncurses_mouse_clicked_handler(event.x, event.y, mouse_state);
				break;
			}
			break;
		default:
	 debug("Event Type : %d at x=%d y=%d buttons=%d\n", event.type, event.x, event.y, event.buttons); 
			break;
	}
	return 0;
}
#endif

static int ncurses_has_mouse_support(const char *term) {
	const char *km = tigetstr("kmous");

	if (km == (void*) -1 || (km && !*km))
		km = NULL;
	if (km)
		return 1;

#ifdef HAVE_LIBGPM
	if (gpm_fd == -2)
		return 2;
#endif
#ifndef HAVE_USABLE_TERMINFO
	if (!xstrncmp(term, "xterm", 5) || !xstrncmp(term, "rxvt", 4) || !xstrncmp(term, "screen", 6))
		return 2;
#endif

	return 0;
}

/*
 * ncurses_enable_mouse()
 * 
 * it should enable mouse support
 * checks if we are in console mode or in xterm
 */
void ncurses_enable_mouse(const char *env) {
#ifdef HAVE_LIBGPM
	Gpm_Connect conn;

	conn.eventMask		= ~0;
	conn.defaultMask	= 0;   
	conn.minMod		= 0;
	conn.maxMod		= 0;

	Gpm_Open(&conn, 0);

	if (gpm_fd >= 0) {
		debug("Gpm at fd no %d\n", gpm_fd);
		
		watch_add(&ncurses_plugin, gpm_fd, WATCH_READ, ncurses_gpm_watch_handler, NULL);
		gpm_visiblepointer = 1;
		mouse_initialized = 1;
	} else {
		if (gpm_fd == -1)
			debug_error("[ncurses] Cannot connect to gpm mouse server\n");
	}
#endif

	if (!mouse_initialized) {
		if ((mouse_initialized = ncurses_has_mouse_support(env))) {
			printf("\033[?1001s\033[?1000h");
			fflush(stdout);
		} else
			debug_error("[ncurses] Mouse in %s terminal is not supported\n", env);
	}

	if (mouse_initialized)
		timer_add(&ncurses_plugin, "ncurses:mouse", 1, 1, ncurses_mouse_timer, NULL);
}

/*
 * ncurses_disable_mouse()
 * 
 * it should disable mouse and destroy everything
 * connected with it's support
 */
void ncurses_disable_mouse(void)
{
	if (!mouse_initialized)
		return;

	timer_remove(&ncurses_plugin, "ncurses:mouse");
#ifdef HAVE_LIBGPM
	if (gpm_fd >= 0)
		watch_remove(&ncurses_plugin, gpm_fd, WATCH_READ);
	else {		/* if GPM is not used, but we have mouse on, then it's xterm */
#endif
		printf("\033[?1000l\033[?1001r"); /* we would like to disable it if restoring flag won't work */
		fflush(stdout);
#ifdef HAVE_LIBGPM
	}

	Gpm_Close();
#endif
}


/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
