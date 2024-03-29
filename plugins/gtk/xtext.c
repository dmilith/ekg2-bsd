/* X-Chat
 * Copyright (C) 1998 Peter Zelezny.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 * =========================================================================
 *
 * xtext, the text widget used by X-Chat.
 * By Peter Zelezny <zed@xchat.org>.
 *
 * optimized, and rewriten to support fstring_t
 * By Jakub Zawadzki <darkjames@darkjames.ath.cx>.
 */

#define TINT_VALUE 195		/* 195/255 of the brightness. */
#define MOTION_MONITOR		/* URL hilights. */
#define SMOOTH_SCROLL		/* line-by-line or pixel scroll? */
#define SCROLL_HACK		/* use XCopyArea scroll, or full redraw? */
#undef COLOR_HILIGHT		/* Color instead of underline? */
/* Italic is buggy because it assumes drawing an italic string will have
   identical extents to the normal font. This is only true some of the
   time, so we can't use this hack yet. */
#undef ITALIC			/* support Italic? */
#define GDK_MULTIHEAD_SAFE
#define USE_DB			/* double buffer */

#define MARGIN 2		/* dont touch. */
#define REFRESH_TIMEOUT 20
#define WORDWRAP_LIMIT 24

#include <ekg2-config.h>
#define USE_XLIB


#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkselection.h>
#include <gtk/gtkclipboard.h>
#include <gtk/gtkversion.h>
#include <gtk/gtkwindow.h>

#ifdef USE_XLIB
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

#include <ekg/stuff.h>
#include <ekg/themes.h>
#include <ekg/xmalloc.h>

#include "xtext.h"

#define charlen(str) g_utf8_skip[*(guchar *)(str)]

/* is delimiter */
#define is_del(c)	(c == ' ' || c == '\n' || c == ')' || c == '(' || c == '>' || c == '<' || c == 0)
#warning "XXX, ATTR_* stuff"
/* is_del includes ATTR_RESET, ATTR_BOLD */

#ifdef SCROLL_HACK
/* force scrolling off */
#define dontscroll(buf) (buf)->last_pixel_pos = 0x7fffffff
#else
#define dontscroll(buf)
#endif

static GtkWidgetClass *parent_class = NULL;

struct textentry {
	struct textentry *next;
	struct textentry *prev;

	fstring_t *fstr;

	const unsigned char *str;
	gint16 str_width;
	gint16 str_len;
	gint16 mark_start;
	gint16 mark_end;
	gint16 indent;
	gint16 left_len;
	gint16 lines_taken;
#define RECORD_WRAPS 4
	guint16 wrap_offset[RECORD_WRAPS];
	guchar mb;		/* boolean: is multibyte? */
#if 0
	guchar tag;
	guchar pad1;
	guchar pad2;	/* 32-bit align : 44 bytes total */
#endif
};

enum {
	WORD_CLICK,
	LAST_SIGNAL
};

/* values for selection info */
enum {
	TARGET_UTF8_STRING,
	TARGET_STRING,
	TARGET_TEXT,
	TARGET_COMPOUND_TEXT
};

static guint xtext_signals[LAST_SIGNAL];

static void gtk_xtext_render_page(GtkXText * xtext);
static void gtk_xtext_calc_lines(xtext_buffer * buf, int);
#if defined(USE_XLIB) || defined(WIN32)
static void gtk_xtext_load_trans(GtkXText * xtext);
static void gtk_xtext_free_trans(GtkXText * xtext);
#endif
static char *gtk_xtext_selection_get_text(GtkXText * xtext, int *len_ret);
static textentry *gtk_xtext_nth(GtkXText * xtext, int line, int *subline);
static void gtk_xtext_adjustment_changed(GtkAdjustment * adj, GtkXText * xtext);
static int gtk_xtext_render_ents(GtkXText * xtext, textentry *, textentry *);
static void gtk_xtext_recalc_widths(xtext_buffer * buf, int);
static void gtk_xtext_fix_indent(xtext_buffer * buf);
static int gtk_xtext_find_subline(GtkXText * xtext, textentry * ent, int line);
static unsigned char *gtk_xtext_strip_color(const unsigned char *text, int len, unsigned char *outbuf,
					    int *mb_ret);

/* some utility functions first */

/* gives width of a 8bit string */

static int gtk_xtext_text_width_8bit(GtkXText * xtext, const unsigned char *str, int len)
{
	int width = 0;

	while (len) {
		width += xtext->fontwidth[*str];
		str++;
		len--;
	}

	return width;
}

#define xtext_draw_bg(xt,x,y,w,h) gdk_draw_rectangle(xt->draw_buf, xt->bgc,1,x,y,w,h);

/* ========================================= */
/* ========== XFT 1 and 2 BACKEND ========== */
/* ========================================= */

#ifdef USE_XFT

static void backend_font_close(GtkXText * xtext)
{
	XftFontClose(GDK_WINDOW_XDISPLAY(xtext->draw_buf), xtext->font);
#ifdef ITALIC
	XftFontClose(GDK_WINDOW_XDISPLAY(xtext->draw_buf), xtext->ifont);
#endif
}

static void backend_init(GtkXText * xtext)
{
	if (xtext->xftdraw == NULL) {
		xtext->xftdraw = XftDrawCreate(GDK_WINDOW_XDISPLAY(xtext->draw_buf),
					       GDK_WINDOW_XWINDOW(xtext->draw_buf),
					       GDK_VISUAL_XVISUAL(gdk_drawable_get_visual
								  (xtext->draw_buf)),
					       GDK_COLORMAP_XCOLORMAP(gdk_drawable_get_colormap
								      (xtext->draw_buf)));
		XftDrawSetSubwindowMode(xtext->xftdraw, IncludeInferiors);
	}
}

static void backend_deinit(GtkXText * xtext)
{
	if (xtext->xftdraw) {
		XftDrawDestroy(xtext->xftdraw);
		xtext->xftdraw = NULL;
	}
}

static XftFont *backend_font_open_real(Display * xdisplay, char *name, gboolean italics)
{
	XftFont *font = NULL;
	PangoFontDescription *fontd;
	int weight, slant, screen = DefaultScreen(xdisplay);

	fontd = pango_font_description_from_string(name);

	if (pango_font_description_get_size(fontd) != 0) {
		weight = pango_font_description_get_weight(fontd);
		/* from pangoft2-fontmap.c */
		if (weight < (PANGO_WEIGHT_NORMAL + PANGO_WEIGHT_LIGHT) / 2)
			weight = XFT_WEIGHT_LIGHT;
		else if (weight < (PANGO_WEIGHT_NORMAL + 600) / 2)
			weight = XFT_WEIGHT_MEDIUM;
		else if (weight < (600 + PANGO_WEIGHT_BOLD) / 2)
			weight = XFT_WEIGHT_DEMIBOLD;
		else if (weight < (PANGO_WEIGHT_BOLD + PANGO_WEIGHT_ULTRABOLD) / 2)
			weight = XFT_WEIGHT_BOLD;
		else
			weight = XFT_WEIGHT_BLACK;

		slant = pango_font_description_get_style(fontd);
		if (slant == PANGO_STYLE_ITALIC)
			slant = XFT_SLANT_ITALIC;
		else if (slant == PANGO_STYLE_OBLIQUE)
			slant = XFT_SLANT_OBLIQUE;
		else
			slant = XFT_SLANT_ROMAN;

		font = XftFontOpen(xdisplay, screen,
				   XFT_FAMILY, XftTypeString,
				   pango_font_description_get_family(fontd), XFT_CORE, XftTypeBool,
				   False, XFT_SIZE, XftTypeDouble,
				   (double)pango_font_description_get_size(fontd) / PANGO_SCALE,
				   XFT_WEIGHT, XftTypeInteger, weight, XFT_SLANT, XftTypeInteger,
				   italics ? XFT_SLANT_ITALIC : slant, NULL);
	}
	pango_font_description_free(fontd);

	if (font == NULL) {
		font = XftFontOpenName(xdisplay, screen, name);
		if (font == NULL)
			font = XftFontOpenName(xdisplay, screen, "sans-11");
	}

	return font;
}

static void backend_font_open(GtkXText * xtext, char *name)
{
	Display *dis = GDK_WINDOW_XDISPLAY(xtext->draw_buf);

	xtext->font = backend_font_open_real(dis, name, FALSE);
#ifdef ITALIC
	xtext->ifont = backend_font_open_real(dis, name, TRUE);
#endif
}

inline static int backend_get_char_width(GtkXText * xtext, const unsigned char *str, int *mbl_ret)
{
	XGlyphInfo ext;

	if (*str < 128) {
		*mbl_ret = 1;
		return xtext->fontwidth[*str];
	}

	*mbl_ret = charlen(str);
	XftTextExtentsUtf8(GDK_WINDOW_XDISPLAY(xtext->draw_buf), xtext->font, str, *mbl_ret, &ext);

	return ext.xOff;
}

static int backend_get_text_width(GtkXText * xtext, const guchar * str, int len, int is_mb)
{
	XGlyphInfo ext;

	if (!is_mb)
		return gtk_xtext_text_width_8bit(xtext, str, len);

	XftTextExtentsUtf8(GDK_WINDOW_XDISPLAY(xtext->draw_buf), xtext->font, str, len, &ext);
	return ext.xOff;
}

static void
backend_draw_text(GtkXText * xtext, int dofill, GdkGC * gc, int x, int y,
		  const char *str, int len, int str_width, int is_mb)
{
	/*Display *xdisplay = GDK_WINDOW_XDISPLAY (xtext->draw_buf); */
	void (*draw_func) (XftDraw *, XftColor *, XftFont *, int, int, XftChar8 *, int) =
		(void *)XftDrawString8;
	XftFont *font;

	/* if all ascii, use String8 to avoid the conversion penalty */
	if (is_mb)
		draw_func = (void *)XftDrawStringUtf8;

	if (dofill) {
/*		register GC xgc = GDK_GC_XGC (gc);
		XSetForeground (xdisplay, xgc, xtext->xft_bg->pixel);
		XFillRectangle (xdisplay, GDK_WINDOW_XWINDOW (xtext->draw_buf), xgc, x,
							 y - xtext->font->ascent, str_width, xtext->fontsize);*/
		XftDrawRect(xtext->xftdraw, xtext->xft_bg, x,
			    y - xtext->font->ascent, str_width, xtext->fontsize);
	}

	font = xtext->font;
#ifdef ITALIC
	if (xtext->italics)
		font = xtext->ifont;
#endif

	draw_func(xtext->xftdraw, xtext->xft_fg, font, x, y, str, len);

	if (xtext->overdraw)
		draw_func(xtext->xftdraw, xtext->xft_fg, font, x, y, str, len);

	if (xtext->bold)
		draw_func(xtext->xftdraw, xtext->xft_fg, font, x + 1, y, str, len);
}

/*static void
backend_set_clip (GtkXText *xtext, GdkRectangle *area)
{
	gdk_gc_set_clip_rectangle (xtext->fgc, area);
	gdk_gc_set_clip_rectangle (xtext->bgc, area);
}

static void
backend_clear_clip (GtkXText *xtext)
{
	gdk_gc_set_clip_rectangle (xtext->fgc, NULL);
	gdk_gc_set_clip_rectangle (xtext->bgc, NULL);
}*/

/*static void
backend_set_clip (GtkXText *xtext, GdkRectangle *area)
{
	Region reg;
	XRectangle rect;

	rect.x = area->x;
	rect.y = area->y;
	rect.width = area->width;
	rect.height = area->height;

	reg = XCreateRegion ();
	XUnionRectWithRegion (&rect, reg, reg);
	XftDrawSetClip (xtext->xftdraw, reg);
	XDestroyRegion (reg);

	gdk_gc_set_clip_rectangle (xtext->fgc, area);
}

static void
backend_clear_clip (GtkXText *xtext)
{
	XftDrawSetClip (xtext->xftdraw, NULL);
	gdk_gc_set_clip_rectangle (xtext->fgc, NULL);
}
*/
#else /* !USE_XFT */

/* ======================================= */
/* ============ PANGO BACKEND ============ */
/* ======================================= */

static void backend_font_close(GtkXText * xtext)
{
	pango_font_description_free(xtext->font->font);
#ifdef ITALIC
	pango_font_description_free(xtext->font->ifont);
#endif
}

static void backend_init(GtkXText * xtext)
{
	if (xtext->layout == NULL) {
		xtext->layout = gtk_widget_create_pango_layout(GTK_WIDGET(xtext), 0);
		if (xtext->font)
			pango_layout_set_font_description(xtext->layout, xtext->font->font);
	}
}

static void backend_deinit(GtkXText * xtext)
{
	if (xtext->layout) {
		g_object_unref(xtext->layout);
		xtext->layout = NULL;
	}
}

static PangoFontDescription *backend_font_open_real(char *name)
{
	PangoFontDescription *font;

	font = pango_font_description_from_string(name);
	if (font && pango_font_description_get_size(font) == 0) {
		pango_font_description_free(font);
		font = NULL;	/* we'll try again, with sans 11 */
	}
	if (!font)
		font = pango_font_description_from_string("sans 11");

	return font;
}

static void backend_font_open(GtkXText * xtext, char *name)
{
	PangoLanguage *lang;
	PangoContext *context;
	PangoFontMetrics *metrics;

	xtext->font = &xtext->pango_font;
	xtext->font->font = backend_font_open_real(name);
	if (!xtext->font->font) {
		xtext->font = NULL;
		return;
	}
#ifdef ITALIC
	xtext->font->ifont = backend_font_open_real(name);
	pango_font_description_set_style(xtext->font->ifont, PANGO_STYLE_ITALIC);
#endif

	backend_init(xtext);
	pango_layout_set_font_description(xtext->layout, xtext->font->font);

	/* vte does it this way */
	context = gtk_widget_get_pango_context(GTK_WIDGET(xtext));
	lang = pango_context_get_language(context);
	metrics = pango_context_get_metrics(context, xtext->font->font, lang);
	xtext->font->ascent = pango_font_metrics_get_ascent(metrics) / PANGO_SCALE;
	xtext->font->descent = pango_font_metrics_get_descent(metrics) / PANGO_SCALE;
	pango_font_metrics_unref(metrics);
}

static int backend_get_text_width(GtkXText * xtext, const guchar * str, int len, int is_mb)
{
	int width;

	if (!is_mb)
		return gtk_xtext_text_width_8bit(xtext, str, len);

	if (*str == 0)
		return 0;

	pango_layout_set_text(xtext->layout, str, len);
	pango_layout_get_pixel_size(xtext->layout, &width, NULL);

	return width;
}

inline static int backend_get_char_width(GtkXText * xtext, const unsigned char *str, int *mbl_ret)
{
	int width;

	if (*str < 128) {
		*mbl_ret = 1;
		return xtext->fontwidth[*str];
	}

	*mbl_ret = charlen(str);
	pango_layout_set_text(xtext->layout, str, *mbl_ret);
	pango_layout_get_pixel_size(xtext->layout, &width, NULL);

	return width;
}

/* simplified version of gdk_draw_layout_line_with_colors() */

static void
xtext_draw_layout_line(GdkDrawable * drawable, GdkGC * gc, gint x, gint y, PangoLayoutLine * line)
{
	GSList *tmp_list;
	PangoRectangle logical_rect;
	gint x_off = 0;

	for (tmp_list = line->runs; tmp_list; tmp_list = tmp_list->next) {
		PangoLayoutRun *run = tmp_list->data;

		pango_glyph_string_extents(run->glyphs, run->item->analysis.font,
					   NULL, &logical_rect);

		gdk_draw_glyphs(drawable, gc, run->item->analysis.font,
				x + x_off / PANGO_SCALE, y, run->glyphs);

		x_off += logical_rect.width;
	}
}

static void
backend_draw_text(GtkXText * xtext, int dofill, GdkGC * gc, int x, int y,
		  const char *str, int len, int str_width, int is_mb)
{
	GdkGCValues val;
	GdkColor col;
	PangoLayoutLine *line;

#ifdef ITALIC
	if (xtext->italics)
		pango_layout_set_font_description(xtext->layout, xtext->font->ifont);
#endif

	pango_layout_set_text(xtext->layout, str, len);

	if (dofill) {
		gdk_gc_get_values(gc, &val);
		col.pixel = val.background.pixel;
		gdk_gc_set_foreground(gc, &col);
		gdk_draw_rectangle(xtext->draw_buf, gc, 1, x, y -
				   xtext->font->ascent, str_width, xtext->fontsize);
		col.pixel = val.foreground.pixel;
		gdk_gc_set_foreground(gc, &col);
	}

	line = pango_layout_get_lines(xtext->layout)->data;

	xtext_draw_layout_line(xtext->draw_buf, gc, x, y, line);

	if (xtext->overdraw)
		xtext_draw_layout_line(xtext->draw_buf, gc, x, y, line);

	if (xtext->bold)
		xtext_draw_layout_line(xtext->draw_buf, gc, x + 1, y, line);

#ifdef ITALIC
	if (xtext->italics)
		pango_layout_set_font_description(xtext->layout, xtext->font->font);
#endif
}

/*static void
backend_set_clip (GtkXText *xtext, GdkRectangle *area)
{
	gdk_gc_set_clip_rectangle (xtext->fgc, area);
	gdk_gc_set_clip_rectangle (xtext->bgc, area);
}

static void
backend_clear_clip (GtkXText *xtext)
{
	gdk_gc_set_clip_rectangle (xtext->fgc, NULL);
	gdk_gc_set_clip_rectangle (xtext->bgc, NULL);
}*/

#endif /* !USE_PANGO */

static void xtext_set_fg(GtkXText * xtext, GdkGC * gc, int index)
{
	GdkColor col;

	col.pixel = xtext->palette[index];
	gdk_gc_set_foreground(gc, &col);

#ifdef USE_XFT
	if (gc == xtext->fgc)
		xtext->xft_fg = &xtext->color[index];
	else
		xtext->xft_bg = &xtext->color[index];
#endif
}

#ifdef USE_XFT

#define xtext_set_bg(xt,gc,index) xt->xft_bg = &xt->color[index]

#else

static void xtext_set_bg(GtkXText * xtext, GdkGC * gc, int index)
{
	GdkColor col;

	col.pixel = xtext->palette[index];
	gdk_gc_set_background(gc, &col);
}

#endif

static void gtk_xtext_init(GtkXText * xtext)
{
	xtext->pixmap = NULL;
	xtext->io_tag = 0;
	xtext->add_io_tag = 0;
	xtext->scroll_tag = 0;
	xtext->max_lines = 0;
	xtext->col_back = XTEXT_BG;
	xtext->col_fore = XTEXT_FG;
	xtext->pixel_offset = 0;
	xtext->bold = FALSE;
	xtext->underline = FALSE;
	xtext->italics = FALSE;
	xtext->font = NULL;
#ifdef USE_XFT
	xtext->xftdraw = NULL;
#else
	xtext->layout = NULL;
#endif
	xtext->jump_out_offset = 0;
	xtext->jump_in_offset = 0;
	xtext->ts_x = 0;
	xtext->ts_y = 0;
	xtext->clip_x = 0;
	xtext->clip_x2 = 1000000;
	xtext->clip_y = 0;
	xtext->clip_y2 = 1000000;
	xtext->error_function = NULL;
	xtext->urlcheck_function = NULL;
	xtext->skip_border_fills = FALSE;
	xtext->skip_stamp = FALSE;
	xtext->render_hilights_only = FALSE;
	xtext->un_hilight = FALSE;
	xtext->recycle = FALSE;
	xtext->dont_render = FALSE;
	xtext->dont_render2 = FALSE;
	xtext->overdraw = FALSE;
	xtext->tint_red = xtext->tint_green = xtext->tint_blue = TINT_VALUE;

	xtext->adj = (GtkAdjustment *) gtk_adjustment_new(0, 0, 1, 1, 1, 1);
	g_object_ref(G_OBJECT(xtext->adj));
	g_object_ref_sink(G_OBJECT(xtext->adj));
	g_object_unref(G_OBJECT(xtext->adj));

	xtext->vc_signal_tag = g_signal_connect(G_OBJECT(xtext->adj),
						"value_changed",
						G_CALLBACK(gtk_xtext_adjustment_changed), xtext);
	{
		static const GtkTargetEntry targets[] = {
			{"UTF8_STRING", 0, TARGET_UTF8_STRING},
			{"STRING", 0, TARGET_STRING},
			{"TEXT", 0, TARGET_TEXT},
			{"COMPOUND_TEXT", 0, TARGET_COMPOUND_TEXT}
		};
		static const gint n_targets = sizeof(targets) / sizeof(targets[0]);

		gtk_selection_add_targets(GTK_WIDGET(xtext), GDK_SELECTION_PRIMARY,
					  targets, n_targets);
	}

	if (getenv("XCHAT_OVERDRAW"))
		xtext->overdraw = TRUE;
}

static void gtk_xtext_adjustment_set(xtext_buffer * buf, int fire_signal)
{
	GtkAdjustment *adj = buf->xtext->adj;

	if (buf->xtext->buffer == buf) {
		adj->lower = 0;
		adj->upper = buf->num_lines;

		if (adj->upper == 0)
			adj->upper = 1;

		adj->page_size =
			(GTK_WIDGET(buf->xtext)->allocation.height -
			 buf->xtext->font->descent) / buf->xtext->fontsize;
		adj->page_increment = adj->page_size;

		if (adj->value > adj->upper - adj->page_size)
			adj->value = adj->upper - adj->page_size;

		if (adj->value < 0)
			adj->value = 0;

		if (fire_signal)
			gtk_adjustment_changed(adj);
	}
}

static gint gtk_xtext_adjustment_timeout(GtkXText * xtext)
{
	gtk_xtext_render_page(xtext);
	xtext->io_tag = 0;
	return 0;
}

static void gtk_xtext_adjustment_changed(GtkAdjustment * adj, GtkXText * xtext)
{
#ifdef SMOOTH_SCROLL
	if (xtext->buffer->old_value != xtext->adj->value)
#else
	if ((int)xtext->buffer->old_value != (int)xtext->adj->value)
#endif
	{
		if (xtext->adj->value >= xtext->adj->upper - xtext->adj->page_size)
			xtext->buffer->scrollbar_down = TRUE;
		else
			xtext->buffer->scrollbar_down = FALSE;

		if (xtext->adj->value + 1 == xtext->buffer->old_value || xtext->adj->value - 1 == xtext->buffer->old_value) {	/* clicked an arrow? */
			if (xtext->io_tag) {
				g_source_remove(xtext->io_tag);
				xtext->io_tag = 0;
			}
			gtk_xtext_render_page(xtext);
		} else {
			if (!xtext->io_tag)
				xtext->io_tag = g_timeout_add(REFRESH_TIMEOUT, (GSourceFunc)
							      gtk_xtext_adjustment_timeout, xtext);
		}
	}
	xtext->buffer->old_value = adj->value;
}

GtkWidget *gtk_xtext_new(GdkColor palette[], int separator) {
	GtkXText *xtext	= g_object_new(gtk_xtext_get_type(), NULL);
	xtext->separator	= separator;
	xtext->wordwrap		= TRUE;
	xtext->buffer		= gtk_xtext_buffer_new(xtext);
	xtext->orig_buffer	= xtext->buffer;

	gtk_widget_set_double_buffered(GTK_WIDGET(xtext), FALSE);
	gtk_xtext_set_palette(xtext, palette);

	return GTK_WIDGET(xtext);
}

static void gtk_xtext_destroy(GtkObject * object)
{
	GtkXText *xtext = GTK_XTEXT(object);

	if (xtext->add_io_tag) {
		g_source_remove(xtext->add_io_tag);
		xtext->add_io_tag = 0;
	}

	if (xtext->scroll_tag) {
		g_source_remove(xtext->scroll_tag);
		xtext->scroll_tag = 0;
	}

	if (xtext->io_tag) {
		g_source_remove(xtext->io_tag);
		xtext->io_tag = 0;
	}

	if (xtext->pixmap) {
#if defined(USE_XLIB) || defined(WIN32)
		if (xtext->transparent)
			gtk_xtext_free_trans(xtext);
		else
#endif
			g_object_unref(xtext->pixmap);
		xtext->pixmap = NULL;
	}

	if (xtext->font) {
		backend_font_close(xtext);
		xtext->font = NULL;
	}

	if (xtext->adj) {
		g_signal_handlers_disconnect_matched(G_OBJECT(xtext->adj),
						     G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, xtext);
		/*	gtk_signal_disconnect_by_data (GTK_OBJECT (xtext->adj), xtext); */
		g_object_unref(G_OBJECT(xtext->adj));
		xtext->adj = NULL;
	}

	if (xtext->bgc) {
		g_object_unref(xtext->bgc);
		xtext->bgc = NULL;
	}

	if (xtext->fgc) {
		g_object_unref(xtext->fgc);
		xtext->fgc = NULL;
	}

	if (xtext->light_gc) {
		g_object_unref(xtext->light_gc);
		xtext->light_gc = NULL;
	}

	if (xtext->dark_gc) {
		g_object_unref(xtext->dark_gc);
		xtext->dark_gc = NULL;
	}

	if (xtext->thin_gc) {
		g_object_unref(xtext->thin_gc);
		xtext->thin_gc = NULL;
	}

	if (xtext->marker_gc) {
		g_object_unref(xtext->marker_gc);
		xtext->marker_gc = NULL;
	}

	if (xtext->hand_cursor) {
		gdk_cursor_unref(xtext->hand_cursor);
		xtext->hand_cursor = NULL;
	}

	if (xtext->resize_cursor) {
		gdk_cursor_unref(xtext->resize_cursor);
		xtext->resize_cursor = NULL;
	}

	if (xtext->orig_buffer) {
		gtk_xtext_buffer_free(xtext->orig_buffer);
		xtext->orig_buffer = NULL;
	}

	if (GTK_OBJECT_CLASS(parent_class)->destroy)
		(*GTK_OBJECT_CLASS(parent_class)->destroy) (object);
}

static void gtk_xtext_unrealize(GtkWidget *widget)
{
	backend_deinit(GTK_XTEXT(widget));

	/* if there are still events in the queue, this'll avoid segfault */
	gdk_window_set_user_data(widget->window, NULL);

	if (parent_class->unrealize)
		(*GTK_WIDGET_CLASS(parent_class)->unrealize) (widget);
}

static void gtk_xtext_realize(GtkWidget *widget)
{
	GtkXText *xtext;
	GdkWindowAttr attributes;
	GdkGCValues val;
	GdkColor col;
	GdkColormap *cmap;

	GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);
	xtext = GTK_XTEXT(widget);

	attributes.x = widget->allocation.x;
	attributes.y = widget->allocation.y;
	attributes.width = widget->allocation.width;
	attributes.height = widget->allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.event_mask = gtk_widget_get_events(widget) |
		GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
#ifdef MOTION_MONITOR
		| GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK;
#else
		| GDK_POINTER_MOTION_MASK;
#endif

	cmap = gtk_widget_get_colormap(widget);
	attributes.colormap = cmap;
	attributes.visual = gtk_widget_get_visual(widget);

	widget->window = gdk_window_new(widget->parent->window, &attributes,
					GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP);

	gdk_window_set_user_data(widget->window, widget);

	xtext->depth = gdk_drawable_get_visual(widget->window)->depth;

	val.subwindow_mode = GDK_INCLUDE_INFERIORS;
	val.graphics_exposures = 0;

	xtext->bgc = gdk_gc_new_with_values(widget->window, &val,
					    GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);
	xtext->fgc = gdk_gc_new_with_values(widget->window, &val,
					    GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);
	xtext->light_gc = gdk_gc_new_with_values(widget->window, &val,
						 GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);
	xtext->dark_gc = gdk_gc_new_with_values(widget->window, &val,
						GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);
	xtext->thin_gc = gdk_gc_new_with_values(widget->window, &val,
						GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);
	xtext->marker_gc = gdk_gc_new_with_values(widget->window, &val,
						  GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);

	/* for the separator bar (light) */
	col.red = 0xffff;
	col.green = 0xffff;
	col.blue = 0xffff;
	gdk_colormap_alloc_color(cmap, &col, FALSE, TRUE);
	gdk_gc_set_foreground(xtext->light_gc, &col);

	/* for the separator bar (dark) */
	col.red = 0x1111;
	col.green = 0x1111;
	col.blue = 0x1111;
	gdk_colormap_alloc_color(cmap, &col, FALSE, TRUE);
	gdk_gc_set_foreground(xtext->dark_gc, &col);

	/* for the separator bar (thinline) */
	col.red = 0x8e38;
	col.green = 0x8e38;
	col.blue = 0x9f38;
	gdk_colormap_alloc_color(cmap, &col, FALSE, TRUE);
	gdk_gc_set_foreground(xtext->thin_gc, &col);

	/* for the marker bar (marker) */
	col.pixel = xtext->palette[XTEXT_MARKER];
	gdk_gc_set_foreground(xtext->marker_gc, &col);

	xtext_set_fg(xtext, xtext->fgc, XTEXT_FG);
	xtext_set_bg(xtext, xtext->fgc, XTEXT_BG);
	xtext_set_fg(xtext, xtext->bgc, XTEXT_BG);

	/* draw directly to window */
	xtext->draw_buf = widget->window;

#if defined(USE_XLIB) || defined(WIN32)
	if (xtext->transparent) {
		gtk_xtext_load_trans(xtext);
	} else
#endif
	if (xtext->pixmap) {
		gdk_gc_set_tile(xtext->bgc, xtext->pixmap);
		gdk_gc_set_ts_origin(xtext->bgc, 0, 0);
		xtext->ts_x = xtext->ts_y = 0;
		gdk_gc_set_fill(xtext->bgc, GDK_TILED);
	}
#if (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION == 0)
	xtext->hand_cursor = gdk_cursor_new(GDK_HAND1);
	xtext->resize_cursor = gdk_cursor_new(GDK_LEFT_SIDE);
#else
	xtext->hand_cursor =
		gdk_cursor_new_for_display(gdk_drawable_get_display(widget->window), GDK_HAND1);
	xtext->resize_cursor =
		gdk_cursor_new_for_display(gdk_drawable_get_display(widget->window),
					   GDK_LEFT_SIDE);
#endif

	gdk_window_set_back_pixmap(widget->window, NULL, FALSE);
	widget->style = gtk_style_attach(widget->style, widget->window);

	backend_init(xtext);
}

static void gtk_xtext_size_request(GtkWidget *widget, GtkRequisition * requisition)
{
	requisition->width = 200;
	requisition->height = 90;
}

static void gtk_xtext_size_allocate(GtkWidget *widget, GtkAllocation * allocation)
{
	GtkXText *xtext = GTK_XTEXT(widget);
	int height_only = FALSE;
	int do_trans = TRUE;

	if (allocation->width == xtext->buffer->window_width)
		height_only = TRUE;

	if (allocation->x == widget->allocation.x &&
	    allocation->y == widget->allocation.y && xtext->avoid_trans)
		do_trans = FALSE;

	xtext->avoid_trans = FALSE;

	widget->allocation = *allocation;
	if (GTK_WIDGET_REALIZED(widget)) {
		xtext->buffer->window_width = allocation->width;
		xtext->buffer->window_height = allocation->height;

		gdk_window_move_resize(widget->window, allocation->x, allocation->y,
				       allocation->width, allocation->height);
		dontscroll(xtext->buffer);	/* force scrolling off */
		if (!height_only)
			gtk_xtext_calc_lines(xtext->buffer, FALSE);
		else {
			xtext->buffer->pagetop_ent = NULL;
			gtk_xtext_adjustment_set(xtext->buffer, FALSE);
		}
#if defined(USE_XLIB) || defined(WIN32)
		if (do_trans && xtext->transparent && xtext->shaded) {
			gtk_xtext_free_trans(xtext);
			gtk_xtext_load_trans(xtext);
		}
#endif
		if (xtext->buffer->scrollbar_down)
			gtk_adjustment_set_value(xtext->adj, xtext->adj->upper -
						 xtext->adj->page_size);
	}
}

static int gtk_xtext_selection_clear(xtext_buffer * buf)
{
	textentry *ent;
	int ret = 0;

	for (ent = buf->last_ent_start; ent; ent = ent->next) {
		if (ent->mark_start != -1)
			ret = 1;
		ent->mark_start = -1;
		ent->mark_end = -1;
		if (ent == buf->last_ent_end)
			break;
	}

	return ret;
}

static int find_x(GtkXText * xtext, textentry * ent, const unsigned char *text, int x, int indent)
{
	int xx = indent;
	int i = 0;
	const unsigned char *orig = text;
	int mbl;
	int char_width;

	while (*text) {
		mbl = 1;

		char_width = backend_get_char_width(xtext, text, &mbl);
		xx += char_width;
		text += mbl;
		if (xx >= x)
			return i + (orig - ent->str);

		i += mbl;
		if (text - orig >= ent->str_len)
			return ent->str_len;
	}

	return ent->str_len;
}

static int
gtk_xtext_find_x(GtkXText * xtext, int x, textentry * ent, int subline,
		 int line, int *out_of_bounds)
{
	int indent;
	const unsigned char *str;

	if (subline < 1)
		indent = ent->indent;
	else
		indent = xtext->buffer->indent;

	if (line > xtext->adj->page_size || line < 0)
		return 0;

	if (xtext->buffer->grid_dirty || line > 255) {
		str = ent->str + gtk_xtext_find_subline(xtext, ent, subline);
		if (str >= ent->str + ent->str_len)
			return 0;
	} else {
		if (xtext->buffer->grid_offset[line] > ent->str_len)
			return 0;
		str = ent->str + xtext->buffer->grid_offset[line];
	}

	if (x < indent) {
		*out_of_bounds = 1;
		return (str - ent->str);
	}

	*out_of_bounds = 0;

	return find_x(xtext, ent, str, x, indent);
}

static textentry *gtk_xtext_find_char(GtkXText * xtext, int x, int y, int *off, int *out_of_bounds)
{
	textentry *ent;
	int line;
	int subline;

	line = (y + xtext->pixel_offset) / xtext->fontsize;
	if (!(ent = gtk_xtext_nth(xtext, line + (int)xtext->adj->value, &subline)))
		return NULL;

	if (off)
		*off = gtk_xtext_find_x(xtext, x, ent, subline, line, out_of_bounds);

	return ent;
}

static void gtk_xtext_draw_sep(GtkXText * xtext, int y)
{
	int x, height;
	GdkGC *light, *dark;

	if (y == -1) {
		y = 0;
		height = GTK_WIDGET(xtext)->allocation.height;
	} else {
		height = xtext->fontsize;
	}

	/* draw the separator line */
	if (xtext->separator && xtext->buffer->indent) {
		light = xtext->light_gc;
		dark = xtext->dark_gc;

		x = xtext->buffer->indent - ((xtext->space_width + 1) / 2);
		if (x < 1)
			return;

		if (xtext->thinline) {
			if (xtext->moving_separator)
				gdk_draw_line(xtext->draw_buf, light, x, y, x, y + height);
			else
				gdk_draw_line(xtext->draw_buf, xtext->thin_gc, x, y, x,
					      y + height);
		} else {
			if (xtext->moving_separator) {
				gdk_draw_line(xtext->draw_buf, light, x - 1, y, x - 1, y + height);
				gdk_draw_line(xtext->draw_buf, dark, x, y, x, y + height);
			} else {
				gdk_draw_line(xtext->draw_buf, dark, x - 1, y, x - 1, y + height);
				gdk_draw_line(xtext->draw_buf, light, x, y, x, y + height);
			}
		}
	}
}

static void gtk_xtext_draw_marker(GtkXText * xtext, textentry * ent, int y)
{
	int x, width, render_y;

	if (!xtext->marker)
		return;

	if (xtext->buffer->marker_pos == ent) {
		render_y = y + xtext->font->descent;
	} else if (xtext->buffer->marker_pos == ent->next && ent->next != NULL) {
		render_y = y + xtext->font->descent + xtext->fontsize * ent->lines_taken;
	} else
		return;

	x = 0;
	width = GTK_WIDGET(xtext)->allocation.width;

	gdk_draw_line(xtext->draw_buf, xtext->marker_gc, x, render_y, x + width, render_y);

#if GTK_CHECK_VERSION(2,4,0)
	if (gtk_window_has_toplevel_focus(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(xtext)))))
#else
	if (GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(xtext)))->has_focus)
#endif
	{
		xtext->buffer->marker_seen = TRUE;
	}
}

#ifdef USE_SHM
static int have_shm_pixmaps(Display *dpy) {
	int major, minor;
	static int checked = 0;
	static int have = FALSE;

	if (!checked) {
		XShmQueryVersion (dpy, &major, &minor, &have);
		checked = 1;
	}

	return have;
}
#endif

static void gtk_xtext_paint(GtkWidget *widget, GdkRectangle * area)
{
	GtkXText *xtext = GTK_XTEXT(widget);
	textentry *ent_start, *ent_end;
	int x, y;

#if defined(USE_XLIB) || defined(WIN32)
	if (xtext->transparent) {
		gdk_window_get_origin(widget->window, &x, &y);
		/* update transparency only if it moved */
		if (xtext->last_win_x != x || xtext->last_win_y != y) {
			xtext->last_win_x = x;
			xtext->last_win_y = y;
#ifndef WIN32
#ifdef USE_SHM
			if (xtext->shaded && !have_shm_pixmaps(GDK_WINDOW_XDISPLAY(xtext->draw_buf))) 
#else
 			if (xtext->shaded)
#endif
			{
				xtext->recycle = TRUE;
				gtk_xtext_load_trans(xtext);
				xtext->recycle = FALSE;
			} else
#endif
			{
				gtk_xtext_free_trans(xtext);
				gtk_xtext_load_trans(xtext);
			}
		}
	}
#endif

	if (area->x == 0 && area->y == 0 &&
	    area->height == widget->allocation.height && area->width == widget->allocation.width) {
		dontscroll(xtext->buffer);	/* force scrolling off */
		gtk_xtext_render_page(xtext);
		return;
	}

	ent_start = gtk_xtext_find_char(xtext, area->x, area->y, NULL, NULL);
	if (!ent_start) {
		xtext_draw_bg(xtext, area->x, area->y, area->width, area->height);
		goto xit;
	}
	ent_end = gtk_xtext_find_char(xtext, area->x + area->width,
				      area->y + area->height, NULL, NULL);
	if (!ent_end)
		ent_end = xtext->buffer->text_last;

	/* can't set a clip here, because fgc/bgc are used to draw the DB too */
/*	backend_set_clip (xtext, area);*/
	xtext->clip_x = area->x;
	xtext->clip_x2 = area->x + area->width;
	xtext->clip_y = area->y;
	xtext->clip_y2 = area->y + area->height;

	/* y is the last pixel y location it rendered text at */
	y = gtk_xtext_render_ents(xtext, ent_start, ent_end);

	if (y && y < widget->allocation.height && !ent_end->next) {
		GdkRectangle rect;

		rect.x = 0;
		rect.y = y;
		rect.width = widget->allocation.width;
		rect.height = widget->allocation.height - y;

		/* fill any space below the last line that also intersects with
		   the exposure rectangle */
		if (gdk_rectangle_intersect(area, &rect, &rect)) {
			xtext_draw_bg(xtext, rect.x, rect.y, rect.width, rect.height);
		}
	}

	/*backend_clear_clip (xtext); */
	xtext->clip_x = 0;
	xtext->clip_x2 = 1000000;
	xtext->clip_y = 0;
	xtext->clip_y2 = 1000000;

xit:
	x = xtext->buffer->indent - ((xtext->space_width + 1) / 2);
	if (area->x <= x)
		gtk_xtext_draw_sep(xtext, -1);
}

static gboolean gtk_xtext_expose(GtkWidget *widget, GdkEventExpose * event)
{
	gtk_xtext_paint(widget, &event->area);
	return FALSE;
}

/* render a selection that has extended or contracted upward */

static void
gtk_xtext_selection_up(GtkXText * xtext, textentry * start, textentry * end, int start_offset)
{
	/* render all the complete lines */
	if (start->next == end)
		gtk_xtext_render_ents(xtext, end, NULL);
	else
		gtk_xtext_render_ents(xtext, start->next, end);

	/* now the incomplete upper line */
	if (start == xtext->buffer->last_ent_start)
		xtext->jump_in_offset = xtext->buffer->last_offset_start;
	else
		xtext->jump_in_offset = start_offset;
	gtk_xtext_render_ents(xtext, start, NULL);
	xtext->jump_in_offset = 0;
}

/* render a selection that has extended or contracted downward */

static void
gtk_xtext_selection_down(GtkXText * xtext, textentry * start, textentry * end, int end_offset)
{
	/* render all the complete lines */
	if (end->prev == start)
		gtk_xtext_render_ents(xtext, start, NULL);
	else
		gtk_xtext_render_ents(xtext, start, end->prev);

	/* now the incomplete bottom line */
	if (end == xtext->buffer->last_ent_end)
		xtext->jump_out_offset = xtext->buffer->last_offset_end;
	else
		xtext->jump_out_offset = end_offset;
	gtk_xtext_render_ents(xtext, end, NULL);
	xtext->jump_out_offset = 0;
}

static void
gtk_xtext_selection_render(GtkXText * xtext,
			   textentry * start_ent, int start_offset,
			   textentry * end_ent, int end_offset)
{
	textentry *ent;
	int start, end;

	xtext->skip_border_fills = TRUE;
	xtext->skip_stamp = TRUE;

	/* force an optimized render if there was no previous selection */
	if (xtext->buffer->last_ent_start == NULL && start_ent == end_ent) {
		xtext->buffer->last_offset_start = start_offset;
		xtext->buffer->last_offset_end = end_offset;
		goto lamejump;
	}

	/* mark changed within 1 ent only? */
	if (xtext->buffer->last_ent_start == start_ent && xtext->buffer->last_ent_end == end_ent) {
		/* when only 1 end of the selection is changed, we can really
		   save on rendering */
		if (xtext->buffer->last_offset_start == start_offset ||
		    xtext->buffer->last_offset_end == end_offset) {
		      lamejump:
			ent = end_ent;
			/* figure out where to start and end the rendering */
			if (end_offset > xtext->buffer->last_offset_end) {
				end = end_offset;
				start = xtext->buffer->last_offset_end;
			} else if (end_offset < xtext->buffer->last_offset_end) {
				end = xtext->buffer->last_offset_end;
				start = end_offset;
			} else if (start_offset < xtext->buffer->last_offset_start) {
				end = xtext->buffer->last_offset_start;
				start = start_offset;
				ent = start_ent;
			} else if (start_offset > xtext->buffer->last_offset_start) {
				end = start_offset;
				start = xtext->buffer->last_offset_start;
				ent = start_ent;
			} else {	/* WORD selects end up here */
				end = end_offset;
				start = start_offset;
			}
		} else {
			/* LINE selects end up here */
			/* so which ent actually changed? */
			ent = start_ent;
			if (xtext->buffer->last_offset_start == start_offset)
				ent = end_ent;

			end = MAX(xtext->buffer->last_offset_end, end_offset);
			start = MIN(xtext->buffer->last_offset_start, start_offset);
		}

		xtext->jump_out_offset = end;
		xtext->jump_in_offset = start;
		gtk_xtext_render_ents(xtext, ent, NULL);
		xtext->jump_out_offset = 0;
		xtext->jump_in_offset = 0;
	}
	/* marking downward? */
	else if (xtext->buffer->last_ent_start == start_ent &&
		 xtext->buffer->last_offset_start == start_offset) {
		/* find the range that covers both old and new selection */
		for (ent = start_ent; ent; ent = ent->next) {
			if (ent == xtext->buffer->last_ent_end) {
				gtk_xtext_selection_down(xtext, ent, end_ent, end_offset);
				/*gtk_xtext_render_ents (xtext, ent, end_ent); */
				break;
			}
			if (ent == end_ent) {
				gtk_xtext_selection_down(xtext, ent, xtext->buffer->last_ent_end,
							 end_offset);
				/*gtk_xtext_render_ents (xtext, ent, xtext->buffer->last_ent_end); */
				break;
			}
		}
	}
	/* marking upward? */
	else if (xtext->buffer->last_ent_end == end_ent &&
		 xtext->buffer->last_offset_end == end_offset) {
		for (ent = end_ent; ent; ent = ent->prev) {
			if (ent == start_ent) {
				gtk_xtext_selection_up(xtext, xtext->buffer->last_ent_start, ent,
						       start_offset);
				/*gtk_xtext_render_ents (xtext, xtext->buffer->last_ent_start, ent); */
				break;
			}
			if (ent == xtext->buffer->last_ent_start) {
				gtk_xtext_selection_up(xtext, start_ent, ent, start_offset);
				/*gtk_xtext_render_ents (xtext, start_ent, ent); */
				break;
			}
		}
	} else {		/* cross-over mark (stretched or shrunk at both ends) */

		/* unrender the old mark */
		gtk_xtext_render_ents(xtext, xtext->buffer->last_ent_start,
				      xtext->buffer->last_ent_end);
		/* now render the new mark, but skip overlaps */
		if (start_ent == xtext->buffer->last_ent_start) {
			/* if the new mark is a sub-set of the old, do nothing */
			if (start_ent != end_ent)
				gtk_xtext_render_ents(xtext, start_ent->next, end_ent);
		} else if (end_ent == xtext->buffer->last_ent_end) {
			/* if the new mark is a sub-set of the old, do nothing */
			if (start_ent != end_ent)
				gtk_xtext_render_ents(xtext, start_ent, end_ent->prev);
		} else
			gtk_xtext_render_ents(xtext, start_ent, end_ent);
	}

	xtext->buffer->last_ent_start = start_ent;
	xtext->buffer->last_ent_end = end_ent;
	xtext->buffer->last_offset_start = start_offset;
	xtext->buffer->last_offset_end = end_offset;

	xtext->skip_border_fills = FALSE;
	xtext->skip_stamp = FALSE;
}

static void gtk_xtext_selection_draw(GtkXText * xtext, GdkEventMotion * event, gboolean render)
{
	textentry *ent;
	textentry *ent_end;
	textentry *ent_start;
	int offset_start;
	int offset_end;
	int low_x;
	int low_y;
	int high_x;
	int high_y;
	int tmp;

	if (xtext->select_start_y > xtext->select_end_y) {
		low_x = xtext->select_end_x;
		low_y = xtext->select_end_y;
		high_x = xtext->select_start_x;
		high_y = xtext->select_start_y;
	} else {
		low_x = xtext->select_start_x;
		low_y = xtext->select_start_y;
		high_x = xtext->select_end_x;
		high_y = xtext->select_end_y;
	}

	ent_start = gtk_xtext_find_char(xtext, low_x, low_y, &offset_start, &tmp);
	if (!ent_start) {
		if (xtext->adj->value != xtext->buffer->old_value)
			gtk_xtext_render_page(xtext);
		return;
	}

	ent_end = gtk_xtext_find_char(xtext, high_x, high_y, &offset_end, &tmp);
	if (!ent_end) {
		ent_end = xtext->buffer->text_last;
		if (!ent_end) {
			if (xtext->adj->value != xtext->buffer->old_value)
				gtk_xtext_render_page(xtext);
			return;
		}
		offset_end = ent_end->str_len;
	}

	/* marking less than a complete line? */
	/* make sure "start" is smaller than "end" (swap them if need be) */
	if (ent_start == ent_end && offset_start > offset_end) {
		tmp = offset_start;
		offset_start = offset_end;
		offset_end = tmp;
	}

	/* has the selection changed? Dont render unless necessary */
	if (xtext->buffer->last_ent_start == ent_start &&
	    xtext->buffer->last_ent_end == ent_end &&
	    xtext->buffer->last_offset_start == offset_start &&
	    xtext->buffer->last_offset_end == offset_end)
		return;

	/* set all the old mark_ fields to -1 */
	gtk_xtext_selection_clear(xtext->buffer);

	ent_start->mark_start = offset_start;
	ent_start->mark_end = offset_end;

	if (ent_start != ent_end) {
		ent_start->mark_end = ent_start->str_len;
		if (offset_end != 0) {
			ent_end->mark_start = 0;
			ent_end->mark_end = offset_end;
		}

		/* set all the mark_ fields of the ents within the selection */
		for (ent = ent_start->next; (ent && ent != ent_end); ent = ent->next) {
			ent->mark_start = 0;
			ent->mark_end = ent->str_len;
		}
	}

	if (render)
		gtk_xtext_selection_render(xtext, ent_start, offset_start, ent_end, offset_end);
}

static gint gtk_xtext_scrolldown_timeout(GtkXText * xtext)
{
	int p_y, win_height;

	gdk_window_get_pointer(GTK_WIDGET(xtext)->window, 0, &p_y, 0);
	gdk_drawable_get_size(GTK_WIDGET(xtext)->window, 0, &win_height);

	if (p_y > win_height && xtext->adj->value < (xtext->adj->upper - xtext->adj->page_size)) {
		xtext->adj->value++;
		gtk_adjustment_changed(xtext->adj);
		gtk_xtext_render_page(xtext);
		return 1;
	}

	xtext->scroll_tag = 0;
	return 0;
}

static gint gtk_xtext_scrollup_timeout(GtkXText * xtext)
{
	int p_y;

	gdk_window_get_pointer(GTK_WIDGET(xtext)->window, 0, &p_y, 0);

	if (p_y < 0 && xtext->adj->value > 0.0) {
		xtext->adj->value--;
		gtk_adjustment_changed(xtext->adj);
		gtk_xtext_render_page(xtext);
		return 1;
	}

	xtext->scroll_tag = 0;
	return 0;
}

static void
gtk_xtext_selection_update(GtkXText * xtext, GdkEventMotion * event, int p_y, gboolean render)
{
	int win_height;
	int moved;

	gdk_drawable_get_size(GTK_WIDGET(xtext)->window, 0, &win_height);

	/* selecting past top of window, scroll up! */
	if (p_y < 0 && xtext->adj->value >= 0) {
		if (!xtext->scroll_tag)
			xtext->scroll_tag = g_timeout_add(100, (GSourceFunc)
							  gtk_xtext_scrollup_timeout, xtext);
		return;
	}

	/* selecting past bottom of window, scroll down! */
	if (p_y > win_height && xtext->adj->value < (xtext->adj->upper - xtext->adj->page_size)) {
		if (!xtext->scroll_tag)
			xtext->scroll_tag = g_timeout_add(100, (GSourceFunc)
							  gtk_xtext_scrolldown_timeout, xtext);
		return;
	}

	moved = (int)xtext->adj->value - xtext->select_start_adj;
	xtext->select_start_y -= (moved * xtext->fontsize);
	xtext->select_start_adj = xtext->adj->value;
	gtk_xtext_selection_draw(xtext, event, render);
}

static char *gtk_xtext_get_word(GtkXText * xtext, int x, int y, textentry ** ret_ent,
				int *ret_off, int *ret_len)
{
	textentry *ent;
	int offset;
	const unsigned char *str;
	const unsigned char *word;
	int len;
	int out_of_bounds = 0;

	ent = gtk_xtext_find_char(xtext, x, y, &offset, &out_of_bounds);
	if (!ent)
		return NULL;

	if (out_of_bounds)
		return NULL;

	if (offset == ent->str_len)
		return NULL;

	if (offset < 1)
		return NULL;

	/*offset--; *//* FIXME: not all chars are 1 byte */

	str = ent->str + offset;

	while (!is_del(*str) && str != ent->str)
		str--;
	word = str + 1;

	len = 0;
	str = word;
	while (!is_del(*str) && len != ent->str_len) {
		str++;
		len++;
	}

	if (len > 0 && word[len - 1] == '.') {
		len--;
		str--;
	}

	if (ret_ent)
		*ret_ent = ent;
	if (ret_off)
		*ret_off = word - ent->str;
	if (ret_len)
		*ret_len = str - word;

	return gtk_xtext_strip_color(word, len, xtext->scratch_buffer, NULL);
}

#ifdef MOTION_MONITOR

static void gtk_xtext_unrender_hilight(GtkXText * xtext)
{
	xtext->render_hilights_only = TRUE;
	xtext->skip_border_fills = TRUE;
	xtext->skip_stamp = TRUE;
	xtext->un_hilight = TRUE;

	gtk_xtext_render_ents(xtext, xtext->hilight_ent, NULL);

	xtext->render_hilights_only = FALSE;
	xtext->skip_border_fills = FALSE;
	xtext->skip_stamp = FALSE;
	xtext->un_hilight = FALSE;
}

static gboolean gtk_xtext_leave_notify(GtkWidget *widget, GdkEventCrossing * event)
{
	GtkXText *xtext = GTK_XTEXT(widget);

	if (xtext->cursor_hand) {
		gtk_xtext_unrender_hilight(xtext);
		xtext->hilight_start = -1;
		xtext->hilight_end = -1;
		xtext->cursor_hand = FALSE;
		gdk_window_set_cursor(widget->window, 0);
		xtext->hilight_ent = NULL;
	}

	if (xtext->cursor_resize) {
		gtk_xtext_unrender_hilight(xtext);
		xtext->hilight_start = -1;
		xtext->hilight_end = -1;
		xtext->cursor_resize = FALSE;
		gdk_window_set_cursor(widget->window, 0);
		xtext->hilight_ent = NULL;
	}

	return FALSE;
}

#endif

/* check if we should mark time stamps, and if a redraw is needed */

static gboolean gtk_xtext_check_mark_stamp(GtkXText * xtext, GdkModifierType mask)
{
	gboolean redraw = FALSE;

	if ((mask & GDK_SHIFT_MASK)) {
		if (!xtext->mark_stamp) {
			redraw = TRUE;	/* must redraw all */
			xtext->mark_stamp = TRUE;
		}
	} else {
		if (xtext->mark_stamp) {
			redraw = TRUE;	/* must redraw all */
			xtext->mark_stamp = FALSE;
		}
	}
	return redraw;
}

static gboolean gtk_xtext_motion_notify(GtkWidget *widget, GdkEventMotion * event)
{
	GtkXText *xtext = GTK_XTEXT(widget);
	GdkModifierType mask;
	int redraw, tmp, x, y, offset, len, line_x;
	char *word;
	textentry *word_ent;

	gdk_window_get_pointer(widget->window, &x, &y, &mask);

	if (xtext->moving_separator) {
		if (x < (3 * widget->allocation.width) / 5 && x > 15) {
			tmp = xtext->buffer->indent;
			xtext->buffer->indent = x;
			gtk_xtext_fix_indent(xtext->buffer);
			if (tmp != xtext->buffer->indent) {
				gtk_xtext_recalc_widths(xtext->buffer, FALSE);
				if (xtext->buffer->scrollbar_down)
					gtk_adjustment_set_value(xtext->adj, xtext->adj->upper -
								 xtext->adj->page_size);
				if (!xtext->io_tag)
					xtext->io_tag = g_timeout_add(REFRESH_TIMEOUT,
								      (GSourceFunc)
								      gtk_xtext_adjustment_timeout,
								      xtext);
			}
		}
		return FALSE;
	}

	if (xtext->button_down) {
		redraw = gtk_xtext_check_mark_stamp(xtext, mask);
		gtk_grab_add(widget);
		/*gdk_pointer_grab (widget->window, TRUE,
		   GDK_BUTTON_RELEASE_MASK |
		   GDK_BUTTON_MOTION_MASK, NULL, NULL, 0); */
		xtext->select_end_x = x;
		xtext->select_end_y = y;
		gtk_xtext_selection_update(xtext, event, y, !redraw);
		xtext->hilighting = TRUE;

		/* user has pressed or released SHIFT, must redraw entire selection */
		if (redraw) {
			xtext->force_stamp = TRUE;
			gtk_xtext_render_ents(xtext, xtext->buffer->last_ent_start,
					      xtext->buffer->last_ent_end);
			xtext->force_stamp = FALSE;
		}
		return FALSE;
	}
#ifdef MOTION_MONITOR

	if (xtext->separator && xtext->buffer->indent) {
		line_x = xtext->buffer->indent - ((xtext->space_width + 1) / 2);
		if (line_x == x || line_x == x + 1 || line_x == x - 1) {
			if (!xtext->cursor_resize) {
				gdk_window_set_cursor(GTK_WIDGET(xtext)->window,
						      xtext->resize_cursor);
				xtext->cursor_resize = TRUE;
			}
			return FALSE;
		}
	}

	if (xtext->urlcheck_function == NULL)
		return FALSE;

	word = gtk_xtext_get_word(xtext, x, y, &word_ent, &offset, &len);
	if (word) {
		if (xtext->urlcheck_function(GTK_WIDGET(xtext), word, len) > 0) {
			if (!xtext->cursor_hand ||
			    xtext->hilight_ent != word_ent ||
			    xtext->hilight_start != offset || xtext->hilight_end != offset + len) {
				if (!xtext->cursor_hand) {
					gdk_window_set_cursor(GTK_WIDGET(xtext)->window,
							      xtext->hand_cursor);
					xtext->cursor_hand = TRUE;
				}

				/* un-render the old hilight */
				if (xtext->hilight_ent)
					gtk_xtext_unrender_hilight(xtext);

				xtext->hilight_ent = word_ent;
				xtext->hilight_start = offset;
				xtext->hilight_end = offset + len;

				xtext->skip_border_fills = TRUE;
				xtext->render_hilights_only = TRUE;
				xtext->skip_stamp = TRUE;

				gtk_xtext_render_ents(xtext, word_ent, NULL);

				xtext->skip_border_fills = FALSE;
				xtext->render_hilights_only = FALSE;
				xtext->skip_stamp = FALSE;
			}
			return FALSE;
		}
	}

	gtk_xtext_leave_notify(widget, NULL);

#endif

	return FALSE;
}

static void gtk_xtext_set_clip_owner(GtkWidget *xtext, GdkEventButton * event)
{
	char *str;
	int len;

	if (GTK_XTEXT(xtext)->selection_buffer &&
	    GTK_XTEXT(xtext)->selection_buffer != GTK_XTEXT(xtext)->buffer)
		gtk_xtext_selection_clear(GTK_XTEXT(xtext)->selection_buffer);

	GTK_XTEXT(xtext)->selection_buffer = GTK_XTEXT(xtext)->buffer;

	if ((str = gtk_xtext_selection_get_text(GTK_XTEXT(xtext), &len))) {
#if (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION == 0)
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), str, len);
#else
		gtk_clipboard_set_text(gtk_widget_get_clipboard(xtext, GDK_SELECTION_CLIPBOARD),
				       str, len);
#endif
		free(str);
	}

	gtk_selection_owner_set(xtext, GDK_SELECTION_PRIMARY, event->time);
}

static void gtk_xtext_unselect(GtkXText * xtext)
{
	xtext_buffer *buf = xtext->buffer;

	xtext->skip_border_fills = TRUE;
	xtext->skip_stamp = TRUE;

	xtext->jump_in_offset = buf->last_ent_start->mark_start;
	/* just a single ent was marked? */
	if (buf->last_ent_start == buf->last_ent_end) {
		xtext->jump_out_offset = buf->last_ent_start->mark_end;
		buf->last_ent_end = NULL;
	}

	gtk_xtext_selection_clear(xtext->buffer);

	/* FIXME: use jump_out on multi-line selects too! */
	gtk_xtext_render_ents(xtext, buf->last_ent_start, buf->last_ent_end);

	xtext->jump_in_offset = 0;
	xtext->jump_out_offset = 0;

	xtext->skip_border_fills = FALSE;
	xtext->skip_stamp = FALSE;

	xtext->buffer->last_ent_start = NULL;
	xtext->buffer->last_ent_end = NULL;
}

static gboolean gtk_xtext_button_release(GtkWidget *widget, GdkEventButton * event)
{
	GtkXText *xtext = GTK_XTEXT(widget);
	int old;

	if (xtext->moving_separator) {
		xtext->moving_separator = FALSE;
		old = xtext->buffer->indent;
		if (event->x < (4 * widget->allocation.width) / 5 && event->x > 15)
			xtext->buffer->indent = event->x;
		gtk_xtext_fix_indent(xtext->buffer);
		if (xtext->buffer->indent != old) {
			gtk_xtext_recalc_widths(xtext->buffer, FALSE);
			gtk_xtext_adjustment_set(xtext->buffer, TRUE);
			gtk_xtext_render_page(xtext);
		} else
			gtk_xtext_draw_sep(xtext, -1);
		return FALSE;
	}

	if (xtext->word_or_line_select) {
		xtext->word_or_line_select = FALSE;
		xtext->button_down = FALSE;
		return FALSE;
	}

	if (event->button == 1) {
		xtext->button_down = FALSE;

		gtk_grab_remove(widget);
		/*gdk_pointer_ungrab (0); */

		/* got a new selection? */
		if (xtext->buffer->last_ent_start)
			gtk_xtext_set_clip_owner(GTK_WIDGET(xtext), event);

		if (xtext->select_start_x == event->x &&
		    xtext->select_start_y == event->y && xtext->buffer->last_ent_start) {
			gtk_xtext_unselect(xtext);
			xtext->mark_stamp = FALSE;
			return FALSE;
		}

		if (!xtext->hilighting) {
			char *word = gtk_xtext_get_word(xtext, event->x, event->y, 0, 0, 0);
			g_signal_emit(G_OBJECT(xtext), xtext_signals[WORD_CLICK], 0,
				      word ? word : NULL, event);
		} else {
			xtext->hilighting = FALSE;
		}
	}


	return FALSE;
}

static gboolean gtk_xtext_button_press(GtkWidget *widget, GdkEventButton * event)
{
	GtkXText *xtext = GTK_XTEXT(widget);
	GdkModifierType mask;
	textentry *ent;
	int line_x, x, y, offset, len;

	gdk_window_get_pointer(widget->window, &x, &y, &mask);

	if (event->button == 3 || event->button == 2) {	/* right/middle click */
		char *word = gtk_xtext_get_word(xtext, x, y, 0, 0, 0);
		if (word) {
			g_signal_emit(G_OBJECT(xtext), xtext_signals[WORD_CLICK], 0, word, event);
		} else
			g_signal_emit(G_OBJECT(xtext), xtext_signals[WORD_CLICK], 0, "", event);
		return FALSE;
	}

	if (event->button != 1)	/* we only want left button */
		return FALSE;

	if (event->type == GDK_2BUTTON_PRESS) {	/* WORD select */
		gtk_xtext_check_mark_stamp(xtext, mask);
		if (gtk_xtext_get_word(xtext, x, y, &ent, &offset, &len)) {
			if (len == 0)
				return FALSE;
			gtk_xtext_selection_clear(xtext->buffer);
			ent->mark_start = offset;
			ent->mark_end = offset + len;
			gtk_xtext_selection_render(xtext, ent, offset, ent, offset + len);
			xtext->word_or_line_select = TRUE;
			gtk_xtext_set_clip_owner(GTK_WIDGET(xtext), event);
		}

		return FALSE;
	}

	if (event->type == GDK_3BUTTON_PRESS) {	/* LINE select */
		gtk_xtext_check_mark_stamp(xtext, mask);
		if (gtk_xtext_get_word(xtext, x, y, &ent, 0, 0)) {
			gtk_xtext_selection_clear(xtext->buffer);
			ent->mark_start = 0;
			ent->mark_end = ent->str_len;
			gtk_xtext_selection_render(xtext, ent, 0, ent, ent->str_len);
			xtext->word_or_line_select = TRUE;
			gtk_xtext_set_clip_owner(GTK_WIDGET(xtext), event);
		}

		return FALSE;
	}

	/* check if it was a separator-bar click */
	if (xtext->separator && xtext->buffer->indent) {
		line_x = xtext->buffer->indent - ((xtext->space_width + 1) / 2);
		if (line_x == x || line_x == x + 1 || line_x == x - 1) {
			xtext->moving_separator = TRUE;
			/* draw the separator line */
			gtk_xtext_draw_sep(xtext, -1);
			return FALSE;
		}
	}

	xtext->button_down = TRUE;
	xtext->select_start_x = x;
	xtext->select_start_y = y;
	xtext->select_start_adj = xtext->adj->value;

	return FALSE;
}

/* another program has claimed the selection */

static gboolean gtk_xtext_selection_kill(GtkXText * xtext, GdkEventSelection * event)
{
	if (xtext->buffer->last_ent_start)
		gtk_xtext_unselect(xtext);
	return TRUE;
}

static char *gtk_xtext_selection_get_text(GtkXText * xtext, int *len_ret)
{
	textentry *ent;
	char *txt;
	char *pos;
	int len;
	int first = TRUE;
	xtext_buffer *buf;

	buf = xtext->selection_buffer;
	if (!buf)
		return NULL;

	/* first find out how much we need to malloc ... */
	len = 0;

	for (ent = buf->last_ent_start; ent; ent = ent->next) {
		if (ent->mark_start != -1) {
			/* include timestamp? */
			if (ent->mark_start == 0 && xtext->mark_stamp) {
				int stamp_size =
					xstrlen(timestamp_time("%H:%M:%S", ent->fstr->ts));
				len += stamp_size;
			}

			if (ent->mark_end - ent->mark_start > 0)
				len += (ent->mark_end - ent->mark_start) + 1;
			else
				len++;
		}
		if (ent == buf->last_ent_end)
			break;
	}

	if (len < 1)
		return NULL;

	/* now allocate mem and copy buffer */
	pos = txt = xmalloc(len);
	for (ent = buf->last_ent_start; ent; ent = ent->next) {
		if (ent->mark_start != -1) {
			if (!first) {
				*pos = '\n';
				pos++;
			}
			first = FALSE;
			if (ent->mark_end - ent->mark_start > 0) {
				/* include timestamp? */
				if (ent->mark_start == 0 && xtext->mark_stamp) {
					const char *time_str =
						timestamp_time("%H:%M:%S", ent->fstr->ts);
					int stamp_size = xstrlen(time_str);

					memcpy(pos, time_str, stamp_size);
					pos += stamp_size;
				}

				memcpy(pos, ent->str + ent->mark_start,
				       ent->mark_end - ent->mark_start);
				pos += ent->mark_end - ent->mark_start;
			}
		}
		if (ent == buf->last_ent_end)
			break;
	}
	*pos = 0;

	*len_ret = xstrlen(txt);
	return txt;
}

/* another program is asking for our selection */

static void
gtk_xtext_selection_get(GtkWidget *widget,
			GtkSelectionData * selection_data_ptr, guint info, guint time)
{
	GtkXText *xtext = GTK_XTEXT(widget);
	char *stripped;
	guchar *new_text;
	int len;
	gsize glen;

	if (!(stripped = gtk_xtext_selection_get_text(xtext, &len)))
		return;

	switch (info) {
	case TARGET_UTF8_STRING:
		/* it's already in utf8 */
		gtk_selection_data_set_text(selection_data_ptr, stripped, len);
		break;
	case TARGET_TEXT:
	case TARGET_COMPOUND_TEXT:
		{
			GdkAtom encoding;
			gint format;
			gint new_length;

#if (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION == 0)
			gdk_string_to_compound_text(
								stripped, &encoding, &format,
								&new_text, &new_length);
#else
			gdk_string_to_compound_text_for_display(gdk_drawable_get_display
								(widget->window),
								stripped, &encoding, &format,
								&new_text, &new_length);
#endif
			gtk_selection_data_set(selection_data_ptr, encoding, format, new_text,
					       new_length);
			gdk_free_compound_text(new_text);
		}
		break;
	/* case TARGET_STRING */
	default:
		new_text = g_locale_from_utf8(stripped, len, NULL, &glen, NULL);
		gtk_selection_data_set(selection_data_ptr, GDK_SELECTION_TYPE_STRING,
				       8, new_text, glen);
		g_free(new_text);
	}

	free(stripped);
}

static gboolean gtk_xtext_scroll(GtkWidget *widget, GdkEventScroll * event)
{
	GtkXText *xtext = GTK_XTEXT(widget);
	gfloat new_value;

	if (event->direction == GDK_SCROLL_UP) {	/* mouse wheel pageUp */
		new_value = xtext->adj->value - (xtext->adj->page_increment / 10);
		if (new_value < xtext->adj->lower)
			new_value = xtext->adj->lower;
		gtk_adjustment_set_value(xtext->adj, new_value);
	} else if (event->direction == GDK_SCROLL_DOWN) {	/* mouse wheel pageDn */
		new_value = xtext->adj->value + (xtext->adj->page_increment / 10);
		if (new_value > (xtext->adj->upper - xtext->adj->page_size))
			new_value = xtext->adj->upper - xtext->adj->page_size;
		gtk_adjustment_set_value(xtext->adj, new_value);
	}

	return FALSE;
}

static void gtk_xtext_class_init(GtkXTextClass * class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GtkXTextClass *xtext_class;

	object_class = (GtkObjectClass *) class;
	widget_class = (GtkWidgetClass *) class;
	xtext_class = (GtkXTextClass *) class;

	parent_class = gtk_type_class(gtk_widget_get_type());

	xtext_signals[WORD_CLICK] =
		g_signal_new("word_click",
			     G_TYPE_FROM_CLASS(object_class),
			     G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
			     G_STRUCT_OFFSET(GtkXTextClass, word_click),
			     NULL, NULL,
			     gtk_marshal_VOID__POINTER_POINTER,
			     G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_POINTER);
	object_class->destroy = gtk_xtext_destroy;

	widget_class->realize = gtk_xtext_realize;
	widget_class->unrealize = gtk_xtext_unrealize;
	widget_class->size_request = gtk_xtext_size_request;
	widget_class->size_allocate = gtk_xtext_size_allocate;
	widget_class->button_press_event = gtk_xtext_button_press;
	widget_class->button_release_event = gtk_xtext_button_release;
	widget_class->motion_notify_event = gtk_xtext_motion_notify;
	widget_class->selection_clear_event = (void *)gtk_xtext_selection_kill;
	widget_class->selection_get = gtk_xtext_selection_get;
	widget_class->expose_event = gtk_xtext_expose;
	widget_class->scroll_event = gtk_xtext_scroll;
#ifdef MOTION_MONITOR
	widget_class->leave_notify_event = gtk_xtext_leave_notify;
#endif

	xtext_class->word_click = NULL;
}

GType gtk_xtext_get_type(void)
{
	static GType xtext_type = 0;

	if (!xtext_type) {
		static const GTypeInfo xtext_info = {
			sizeof(GtkXTextClass),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc) gtk_xtext_class_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			sizeof(GtkXText),
			0,	/* n_preallocs */
			(GInstanceInitFunc) gtk_xtext_init,
		};

		xtext_type = g_type_register_static(GTK_TYPE_WIDGET, "GtkXText", &xtext_info, 0);
	}

	return xtext_type;
}

/* copy text to outbuf, [len bytes], terminate with \0
 * set *mb_ret to TRUE, if there're chars with ANSI code >= 128 */

static unsigned char *gtk_xtext_strip_color(const unsigned char *text, int len, unsigned char *outbuf, int *mb_ret)
{
	int i = 0;
	int mb = FALSE;

	for (i = 0; i < len; i++) {
		if (text[i] >= 128)
			mb = TRUE;

		outbuf[i] = *text;
	}
	outbuf[len] = 0;

	if (mb_ret != NULL)
		*mb_ret = mb;

	return outbuf;
}

/* gives width of a string */

static int gtk_xtext_text_width(GtkXText * xtext, const unsigned char *text, int len, int *mb_ret)
{
	unsigned char *new_buf;
	int mb;

	new_buf = gtk_xtext_strip_color(text, len, xtext->scratch_buffer, &mb);

	if (mb_ret)
		*mb_ret = mb;

	return backend_get_text_width(xtext, new_buf, len, mb);
}

/* actually draw text to screen (one run with the same color/attribs) */

static int
gtk_xtext_render_flush(GtkXText * xtext, int x, int y, const unsigned char *str,
		       int len, GdkGC * gc, int is_mb)
{
	int str_width, dofill = TRUE;

#ifdef USE_DB
	GdkDrawable *pix;
#endif

	if (xtext->dont_render || len < 1)
		return 0;

	str_width = backend_get_text_width(xtext, str, len, is_mb);

	if (xtext->dont_render2)
		return str_width;

	/* roll-your-own clipping (avoiding XftDrawString is always good!) */
	if (x > xtext->clip_x2 || x + str_width < xtext->clip_x)
		return str_width;
	if (y - xtext->font->ascent > xtext->clip_y2
	    || (y - xtext->font->ascent) + xtext->fontsize < xtext->clip_y)
		return str_width;

	if (xtext->render_hilights_only) {
		if (!xtext->in_hilight)	/* is it a hilight prefix? */
			return str_width;
#ifndef COLOR_HILIGHT
		if (!xtext->un_hilight)	/* doing a hilight? no need to draw the text */
			goto dounder;
#endif
	}
#ifdef USE_DB
	pix = gdk_pixmap_new(xtext->draw_buf, str_width, xtext->fontsize, xtext->depth);
	if (pix) {
		GdkRectangle clip;
		GdkRectangle dest;
		int dest_x, dest_y;

#ifdef USE_XFT
		XftDrawChange(xtext->xftdraw, GDK_WINDOW_XWINDOW(pix));
#endif
		dest_x = x;
		dest_y = y - xtext->font->ascent;

		gdk_gc_set_ts_origin(xtext->bgc, xtext->ts_x - x, xtext->ts_y - dest_y);

		xtext->draw_buf = pix;

		/* backcolor is always handled by XDrawImageString */
		if (!xtext->backcolor && xtext->pixmap) {
			/* draw the background pixmap behind the text - CAUSES FLICKER HERE!! */
			xtext_draw_bg(xtext, 0, 0, str_width, xtext->fontsize);
			dofill = FALSE;	/* already drawn the background */
		}

		backend_draw_text(xtext, dofill, gc, 0, xtext->font->ascent, str, len, str_width, is_mb);

		gdk_gc_set_ts_origin(xtext->bgc, xtext->ts_x, xtext->ts_y);
		xtext->draw_buf = GTK_WIDGET(xtext)->window;
#ifdef USE_XFT
		XftDrawChange(xtext->xftdraw, GDK_WINDOW_XWINDOW(xtext->draw_buf));
#endif
		clip.x = xtext->clip_x;
		clip.y = xtext->clip_y;
		clip.width = xtext->clip_x2 - xtext->clip_x;
		clip.height = xtext->clip_y2 - xtext->clip_y;

		dest.x = dest_x;
		dest.y = dest_y;
		dest.width = str_width;
		dest.height = xtext->fontsize;

		if (gdk_rectangle_intersect(&clip, &dest, &dest))
			/* dump the DB to window, but only within the clip_x/x2/y/y2 */
			gdk_draw_drawable(xtext->draw_buf, xtext->bgc, pix,
					  dest.x - dest_x, dest.y - dest_y,
					  dest.x, dest.y, dest.width, dest.height);
		g_object_unref(pix);
	} else
#endif
	{
		/* backcolor is always handled by XDrawImageString */
		if (!xtext->backcolor && xtext->pixmap) {
			/* draw the background pixmap behind the text - CAUSES FLICKER HERE!! */
			xtext_draw_bg(xtext, x, y - xtext->font->ascent, str_width, xtext->fontsize);
			dofill = FALSE;	/* already drawn the background */
		}

		backend_draw_text(xtext, dofill, gc, x, y, str, len, str_width, is_mb);
	}

	if (xtext->underline) {
#ifdef USE_XFT
		GdkColor col;
#endif

#ifndef COLOR_HILIGHT
dounder:
#endif

#ifdef USE_XFT
		col.pixel = xtext->xft_fg->pixel;
		gdk_gc_set_foreground(gc, &col);
#endif
		/* draw directly to window, it's out of the range of our DB */
		gdk_draw_line(xtext->draw_buf, gc, x, y + 1, x + str_width - 1, y + 1);
	}

	return str_width;
}

static void gtk_xtext_reset(GtkXText * xtext, int mark, int attribs)
{
	if (attribs) {
		xtext->underline = FALSE;
		xtext->bold = FALSE;
		xtext->italics = FALSE;
	}
	if (!mark) {
		xtext->backcolor = FALSE;
		if (xtext->col_fore != XTEXT_FG)
			xtext_set_fg(xtext, xtext->fgc, XTEXT_FG);
		if (xtext->col_back != XTEXT_BG)
			xtext_set_bg(xtext, xtext->fgc, XTEXT_BG);
	}
	xtext->col_fore = XTEXT_FG;
	xtext->col_back = XTEXT_BG;
}

/* render a single line, which WONT wrap, and parse fstring_t attr */

static int
gtk_xtext_render_str(GtkXText * xtext, int y, textentry * ent,
		     const unsigned char *str, short *attr, int len, int win_width, int indent,
		     int line, int left_only, int *x_size_ret)
{
	GdkGC *gc;
	int i = 0, x = indent, j = 0;
	const unsigned char *pstr = str;
	int offset;
	int mark = FALSE;
	int ret = 1;

	short last_attr = FSTR_NORMAL;

	xtext->in_hilight = FALSE;

	offset = str - ent->str;

	if (line < 255 && line >= 0)
		xtext->buffer->grid_offset[line] = offset;

	gc = xtext->fgc;	/* our foreground GC */

	if (ent->mark_start != -1 && ent->mark_start <= i + offset && ent->mark_end > i + offset) {
		xtext_set_bg(xtext, gc, XTEXT_MARK_BG);
		xtext_set_fg(xtext, gc, XTEXT_MARK_FG);
		xtext->backcolor = TRUE;
		mark = TRUE;
	}
#ifdef MOTION_MONITOR
	if (xtext->hilight_ent == ent &&
	    xtext->hilight_start <= i + offset && xtext->hilight_end > i + offset) {
		if (!xtext->un_hilight) {
#ifdef COLOR_HILIGHT
			xtext_set_bg(xtext, gc, 2);
#else
			xtext->underline = TRUE;
#endif
		}
		xtext->in_hilight = TRUE;
	}
#endif

	if (!xtext->skip_border_fills && !xtext->dont_render) {
		/* draw background to the left of the text */
		if (str == ent->str && indent > MARGIN && xtext->buffer->time_stamp) {
			/* don't overwrite the timestamp */
			if (indent > xtext->stamp_width) {
				xtext_draw_bg(xtext, xtext->stamp_width, y - xtext->font->ascent,
					      indent - xtext->stamp_width, xtext->fontsize);
			}
		} else {
			/* fill the indent area with background gc */
			if (indent >= xtext->clip_x) {
				xtext_draw_bg(xtext, 0, y - xtext->font->ascent,
					      MIN(indent, xtext->clip_x2), xtext->fontsize);
			}
		}
	}

	if (xtext->jump_in_offset > 0 && offset < xtext->jump_in_offset)
		xtext->dont_render2 = TRUE;

	while (i < len) {

#ifdef MOTION_MONITOR
		if (xtext->hilight_ent == ent && xtext->hilight_start == (i + offset)) {
			x += gtk_xtext_render_flush(xtext, x, y, pstr, j, gc, ent->mb);
			pstr += j;
			j = 0;
			if (!xtext->un_hilight) {
#ifdef COLOR_HILIGHT
				xtext_set_bg(xtext, gc, 2);
#else
				xtext->underline = TRUE;
#endif
			}

			xtext->in_hilight = TRUE;
		}
#endif

		if (attr && last_attr != attr[i]) {
			int isbold;
			last_attr = attr[i];

			if (i) {
				x += gtk_xtext_render_flush(xtext, x, y, pstr, j, gc, ent->mb);
				pstr += j;
				j = 0;
			}

			gtk_xtext_reset(xtext, mark, !xtext->in_hilight);

			isbold = ((last_attr & FSTR_BOLD) != 0);

/*			xtext->bold = (isbold); */
			/* more follow */

			if (!(last_attr & FSTR_NORMAL)) {
				if (!mark) {
					xtext_set_fg(xtext, gc, ((last_attr & FSTR_FOREMASK) + 8*(isbold)));
/*					xtext_set_bg(xtext, gc, ((last_attr >> 3) & 7)); */
				}
				xtext->col_fore = ((last_attr & FSTR_FOREMASK) + 8*(isbold));
/*				xtext->col_back = ((last_attr >> 3) & 7); */
			} else {
				if (isbold) {
					if (!mark)
						xtext_set_fg(xtext, gc, 7+8);
					xtext->col_fore = 7+8;
				}
			}
		}


		if (str[i] != '\n') {
			int tmp = charlen(str + i);
			/* invalid utf8 safe guard */
			if (tmp + i > len)
				tmp = len - i;
			j += tmp;	/* move to the next utf8 char */
		}

		i += charlen(str + i);	/* move to the next utf8 char */
		/* invalid utf8 safe guard */
		if (i > len)
			i = len;

		/* Separate the left part, the space and the right part
		   into separate runs, and reset bidi state inbetween.
		   Perform this only on the first line of the message.
		 */
		if (offset == 0) {
			/* we've reached the end of the left part? */
			if ((pstr - str) + j == ent->left_len) {
				x += gtk_xtext_render_flush(xtext, x, y, pstr, j, gc, ent->mb);
				pstr += j;
				j = 0;
			} else if ((pstr - str) + j == ent->left_len + 1) {
				x += gtk_xtext_render_flush(xtext, x, y, pstr, j, gc, ent->mb);
				pstr += j;
				j = 0;
			}
		}

		/* have we been told to stop rendering at this point? */
		if (xtext->jump_out_offset > 0 && xtext->jump_out_offset <= (i + offset)) {
			gtk_xtext_render_flush(xtext, x, y, pstr, j, gc, ent->mb);
			ret = 0;	/* skip the rest of the lines, we're done. */
			j = 0;
			break;
		}

		if (xtext->jump_in_offset > 0 && xtext->jump_in_offset == (i + offset)) {
			x += gtk_xtext_render_flush(xtext, x, y, pstr, j, gc, ent->mb);
			pstr += j;
			j = 0;
			xtext->dont_render2 = FALSE;
		}
#ifdef MOTION_MONITOR
		if (xtext->hilight_ent == ent && xtext->hilight_end == (i + offset)) {
			x += gtk_xtext_render_flush(xtext, x, y, pstr, j, gc, ent->mb);
			pstr += j;
			j = 0;
#ifdef COLOR_HILIGHT
			if (mark) {
				xtext_set_bg(xtext, gc, XTEXT_MARK_BG);
				xtext->backcolor = TRUE;
			} else {
				xtext_set_bg(xtext, gc, xtext->col_back);
				if (xtext->col_back != XTEXT_BG)
					xtext->backcolor = TRUE;
				else
					xtext->backcolor = FALSE;
			}
#else
			xtext->underline = FALSE;
#endif
			xtext->in_hilight = FALSE;
			if (xtext->render_hilights_only) {
				/* stop drawing this ent */
				ret = 0;
				break;
			}
		}
#endif

		if (!mark && ent->mark_start == (i + offset)) {
			x += gtk_xtext_render_flush(xtext, x, y, pstr, j, gc, ent->mb);
			pstr += j;
			j = 0;
			xtext_set_bg(xtext, gc, XTEXT_MARK_BG);
			xtext_set_fg(xtext, gc, XTEXT_MARK_FG);
			xtext->backcolor = TRUE;
			mark = TRUE;
		}

		if (mark && ent->mark_end == (i + offset)) {
			x += gtk_xtext_render_flush(xtext, x, y, pstr, j, gc, ent->mb);
			pstr += j;
			j = 0;
			xtext_set_bg(xtext, gc, xtext->col_back);
			xtext_set_fg(xtext, gc, xtext->col_fore);
			if (xtext->col_back != XTEXT_BG)
				xtext->backcolor = TRUE;
			else
				xtext->backcolor = FALSE;
			mark = FALSE;
		}

	}

	if (j)
		x += gtk_xtext_render_flush(xtext, x, y, pstr, j, gc, ent->mb);

	if (mark) {
		xtext_set_bg(xtext, gc, xtext->col_back);
		xtext_set_fg(xtext, gc, xtext->col_fore);
		if (xtext->col_back != XTEXT_BG)
			xtext->backcolor = TRUE;
		else
			xtext->backcolor = FALSE;
	}

	/* draw background to the right of the text */
	if (!left_only && !xtext->dont_render) {
		/* draw separator now so it doesn't appear to flicker */
		gtk_xtext_draw_sep(xtext, y - xtext->font->ascent);
		if (!xtext->skip_border_fills && xtext->clip_x2 >= x) {
			int xx = MAX(x, xtext->clip_x);

			xtext_draw_bg(xtext, xx,	/* x */
				      y - xtext->font->ascent,	/* y */
				      MIN(xtext->clip_x2 - xx, (win_width + MARGIN) - xx),	/* width */
				      xtext->fontsize);	/* height */
		}
	}

	xtext->dont_render2 = FALSE;

	/* return how much we drew in the x direction */
	if (x_size_ret)
		*x_size_ret = x - indent;
	return ret;
}

#ifdef USE_XLIB

/* get the desktop/root window */

static Window desktop_window = None;

static Window get_desktop_window(Display * xdisplay, Window the_window)
{
	Atom prop, type;
	int format;
	unsigned long length, after;
	unsigned char *data;
	unsigned int nchildren;
	Window w, root, *children, parent;

	prop = XInternAtom(xdisplay, "_XROOTPMAP_ID", True);
	if (prop == None) {
		prop = XInternAtom(xdisplay, "_XROOTCOLOR_PIXEL", True);
		if (prop == None)
			return None;
	}

	for (w = the_window; w; w = parent) {
		if ((XQueryTree(xdisplay, w, &root, &parent, &children, &nchildren)) == False)
			return None;

		if (nchildren)
			XFree(children);

		XGetWindowProperty(xdisplay, w, prop, 0L, 1L, False,
				   AnyPropertyType, &type, &format, &length, &after, &data);
		if (data)
			XFree(data);

		if (type != None)
			return (desktop_window = w);
	}

	return (desktop_window = None);
}

/* find the root window (backdrop) Pixmap */

static Pixmap get_pixmap_prop(Display * xdisplay, Window the_window)
{
	Atom type;
	int format;
	unsigned long length, after;
	unsigned char *data;
	Pixmap pix = None;
	static Atom prop = None;

	if (desktop_window == None)
		desktop_window = get_desktop_window(xdisplay, the_window);
	if (desktop_window == None)
		desktop_window = DefaultRootWindow(xdisplay);

	if (prop == None)
		prop = XInternAtom(xdisplay, "_XROOTPMAP_ID", True);
	if (prop == None)
		return None;

	XGetWindowProperty(xdisplay, desktop_window, prop, 0L, 1L, False,
			   AnyPropertyType, &type, &format, &length, &after, &data);
	if (data) {
		if (type == XA_PIXMAP)
			pix = *((Pixmap *) data);

		XFree(data);
	}

	return pix;
}

/* slow generic routine, for the depths/bpp we don't know about */

static void
shade_ximage_generic(GdkVisual * visual, XImage * ximg, int bpl, int w, int h, int rm, int gm,
		     int bm, int bg)
{
	int x, y;
	int bgr = (256 - rm) * (bg & visual->red_mask);
	int bgg = (256 - gm) * (bg & visual->green_mask);
	int bgb = (256 - bm) * (bg & visual->blue_mask);

	for (x = 0; x < w; x++) {
		for (y = 0; y < h; y++) {
			unsigned long pixel = XGetPixel(ximg, x, y);
			int r, g, b;

			r = rm * (pixel & visual->red_mask) + bgr;
			g = gm * (pixel & visual->green_mask) + bgg;
			b = bm * (pixel & visual->blue_mask) + bgb;

			XPutPixel(ximg, x, y,
				  ((r >> 8) & visual->red_mask) |
				  ((g >> 8) & visual->green_mask) |
				  ((b >> 8) & visual->blue_mask));
		}
	}
}

#endif

/* Fast shading routine. Based on code by Willem Monsuwe <willem@stack.nl> */

#define SHADE_IMAGE(bytes, type, rmask, gmask, bmask) \
	unsigned char *ptr; \
	int x, y; \
	int bgr = (256 - rm) * (bg & rmask); \
	int bgg = (256 - gm) * (bg & gmask); \
	int bgb = (256 - bm) * (bg & bmask); \
	ptr = (unsigned char *) data + (w * bytes); \
	for (y = h; --y >= 0;) \
	{ \
		for (x = -w; x < 0; x++) \
		{ \
			int r, g, b; \
			b = ((type *) ptr)[x]; \
			r = rm * (b & rmask) + bgr; \
			g = gm * (b & gmask) + bgg; \
			b = bm * (b & bmask) + bgb; \
			((type *) ptr)[x] = ((r >> 8) & rmask) \
										| ((g >> 8) & gmask) \
										| ((b >> 8) & bmask); \
		} \
		ptr += bpl; \
    }

/* RGB 15 */
static void shade_ximage_15(void *data, int bpl, int w, int h, int rm, int gm, int bm, int bg)
{
	SHADE_IMAGE(2, guint16, 0x7c00, 0x3e0, 0x1f);
}

/* RGB 16 */
static void shade_ximage_16(void *data, int bpl, int w, int h, int rm, int gm, int bm, int bg)
{
	SHADE_IMAGE(2, guint16, 0xf800, 0x7e0, 0x1f);
}

/* RGB 24 */
static void shade_ximage_24(void *data, int bpl, int w, int h, int rm, int gm, int bm, int bg)
{
	/* 24 has to be a special case, there's no guint24, or 24bit MOV :) */
	unsigned char *ptr;
	int x, y;
	int bgr = (256 - rm) * ((bg & 0xff0000) >> 16);
	int bgg = (256 - gm) * ((bg & 0xff00) >> 8);
	int bgb = (256 - bm) * (bg & 0xff);

	ptr = (unsigned char *)data + (w * 3);
	for (y = h; --y >= 0;) {
		for (x = -(w * 3); x < 0; x += 3) {
			int r, g, b;

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
			r = (ptr[x + 0] * rm + bgr) >> 8;
			g = (ptr[x + 1] * gm + bgg) >> 8;
			b = (ptr[x + 2] * bm + bgb) >> 8;
			ptr[x + 0] = r;
			ptr[x + 1] = g;
			ptr[x + 2] = b;
#else
			r = (ptr[x + 2] * rm + bgr) >> 8;
			g = (ptr[x + 1] * gm + bgg) >> 8;
			b = (ptr[x + 0] * bm + bgb) >> 8;
			ptr[x + 2] = r;
			ptr[x + 1] = g;
			ptr[x + 0] = b;
#endif
		}
		ptr += bpl;
	}
}

/* RGB 32 */
static void shade_ximage_32(void *data, int bpl, int w, int h, int rm, int gm, int bm, int bg)
{
	SHADE_IMAGE(4, guint32, 0xff0000, 0xff00, 0xff);
}

static void
shade_image(GdkVisual * visual, void *data, int bpl, int bpp, int w, int h,
	    int rm, int gm, int bm, int bg, int depth)
{
	int bg_r, bg_g, bg_b;

	bg_r = bg & visual->red_mask;
	bg_g = bg & visual->green_mask;
	bg_b = bg & visual->blue_mask;

#ifdef USE_MMX
	/* the MMX routines are about 50% faster at 16-bit. */
	/* only use MMX routines with a pure black background */
	if (bg_r == 0 && bg_g == 0 && bg_b == 0 && have_mmx()) {	/* do a runtime check too! */
		switch (depth) {
		case 15:
			shade_ximage_15_mmx(data, bpl, w, h, rm, gm, bm);
			break;
		case 16:
			shade_ximage_16_mmx(data, bpl, w, h, rm, gm, bm);
			break;
		case 24:
			if (bpp != 32)
				goto generic;
		case 32:
			shade_ximage_32_mmx(data, bpl, w, h, rm, gm, bm);
			break;
		default:
			goto generic;
		}
	} else {
	      generic:
#endif
		switch (depth) {
		case 15:
			shade_ximage_15(data, bpl, w, h, rm, gm, bm, bg);
			break;
		case 16:
			shade_ximage_16(data, bpl, w, h, rm, gm, bm, bg);
			break;
		case 24:
			if (bpp != 32) {
				shade_ximage_24(data, bpl, w, h, rm, gm, bm, bg);
				break;
			}
		case 32:
			shade_ximage_32(data, bpl, w, h, rm, gm, bm, bg);
		}
#ifdef USE_MMX
	}
#endif
}

#ifdef USE_XLIB

#ifdef USE_SHM

static XImage *get_shm_image(Display * xdisplay, XShmSegmentInfo * shminfo, int x, int y,
			     int w, int h, int depth, Pixmap pix)
{
	XImage *ximg;

	shminfo->shmid = -1;
	shminfo->shmaddr = (char *)-1;
	ximg = XShmCreateImage(xdisplay, 0, depth, ZPixmap, 0, shminfo, w, h);
	if (!ximg)
		return NULL;

	shminfo->shmid = shmget(IPC_PRIVATE, ximg->bytes_per_line * ximg->height,
				IPC_CREAT | 0600);
	if (shminfo->shmid == -1) {
		XDestroyImage(ximg);
		return NULL;
	}

	shminfo->readOnly = False;
	ximg->data = shminfo->shmaddr = (char *)shmat(shminfo->shmid, 0, 0);
	if (shminfo->shmaddr == ((char *)-1)) {
		shmctl(shminfo->shmid, IPC_RMID, 0);
		XDestroyImage(ximg);
		return NULL;
	}

	XShmAttach(xdisplay, shminfo);
	XSync(xdisplay, False);
	shmctl(shminfo->shmid, IPC_RMID, 0);
	XShmGetImage(xdisplay, pix, ximg, x, y, AllPlanes);

	return ximg;
}

static XImage *get_image(GtkXText * xtext, Display * xdisplay, XShmSegmentInfo * shminfo,
			 int x, int y, int w, int h, int depth, Pixmap pix)
{
	XImage *ximg;

	xtext->shm = 1;
	ximg = get_shm_image(xdisplay, shminfo, x, y, w, h, depth, pix);
	if (!ximg) {
		xtext->shm = 0;
		ximg = XGetImage(xdisplay, pix, x, y, w, h, -1, ZPixmap);
	}

	return ximg;
}

#endif

static GdkPixmap *shade_pixmap(GtkXText * xtext, Pixmap p, int x, int y, int w, int h)
{
	unsigned int dummy, width, height, depth;
	int dummy_;
	GdkPixmap *shaded_pix;
	Window root;
	Pixmap tmp;
	XImage *ximg;
	XGCValues gcv;
	GC tgc;
	Display *xdisplay = GDK_WINDOW_XDISPLAY(xtext->draw_buf);

#ifdef USE_SHM
	int shm_pixmaps = have_shm_pixmaps(xdisplay);
#endif

	XGetGeometry(xdisplay, p, &root, &dummy_, &dummy_, &width, &height, &dummy, &depth);

	if (width < x + w || height < y + h || x < 0 || y < 0) {
		gcv.subwindow_mode = IncludeInferiors;
		gcv.graphics_exposures = False;
		tgc = XCreateGC(xdisplay, p, GCGraphicsExposures | GCSubwindowMode, &gcv);
		tmp = XCreatePixmap(xdisplay, p, w, h, depth);
		XSetTile(xdisplay, tgc, p);
		XSetFillStyle(xdisplay, tgc, FillTiled);
		XSetTSOrigin(xdisplay, tgc, -x, -y);
		XFillRectangle(xdisplay, tmp, tgc, 0, 0, w, h);
		XFreeGC(xdisplay, tgc);

#ifdef USE_SHM
		if (shm_pixmaps)
			ximg = get_image(xtext, xdisplay, &xtext->shminfo, 0, 0, w, h, depth, tmp);
		else
#endif
			ximg = XGetImage(xdisplay, tmp, 0, 0, w, h, -1, ZPixmap);
		XFreePixmap(xdisplay, tmp);
	} else {
#ifdef USE_SHM
		if (shm_pixmaps)
			ximg = get_image(xtext, xdisplay, &xtext->shminfo, x, y, w, h, depth, p);
		else
#endif
			ximg = XGetImage(xdisplay, p, x, y, w, h, -1, ZPixmap);
	}

	if (!ximg)
		return NULL;

	if (depth <= 14) {
		shade_ximage_generic(gdk_drawable_get_visual(GTK_WIDGET(xtext)->window),
				     ximg, ximg->bytes_per_line, w, h, xtext->tint_red,
				     xtext->tint_green, xtext->tint_blue,
				     xtext->palette[XTEXT_BG]);
	} else {
		shade_image(gdk_drawable_get_visual(GTK_WIDGET(xtext)->window),
			    ximg->data, ximg->bytes_per_line, ximg->bits_per_pixel,
			    w, h, xtext->tint_red, xtext->tint_green, xtext->tint_blue,
			    xtext->palette[XTEXT_BG], depth);
	}

	if (xtext->recycle)
		shaded_pix = xtext->pixmap;
	else {
#ifdef USE_SHM
		if (xtext->shm && shm_pixmaps) {
#if (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION == 0)
			shaded_pix =
				gdk_pixmap_foreign_new(XShmCreatePixmap
						       (xdisplay, p, ximg->data, &xtext->shminfo,
							w, h, depth));
#else
			shaded_pix =
				gdk_pixmap_foreign_new_for_display(gdk_drawable_get_display
								   (xtext->draw_buf),
								   XShmCreatePixmap(xdisplay, p,
										    ximg->data,
										    &xtext->
										    shminfo, w, h,
										    depth));
#endif
		} else
#endif
		{
			shaded_pix = gdk_pixmap_new(GTK_WIDGET(xtext)->window, w, h, depth);
		}
	}

#ifdef USE_SHM
	if (!xtext->shm || !shm_pixmaps)
#endif
		XPutImage(xdisplay, GDK_WINDOW_XWINDOW(shaded_pix),
			  GDK_GC_XGC(xtext->fgc), ximg, 0, 0, 0, 0, w, h);
	XDestroyImage(ximg);

	return shaded_pix;
}

#endif /* !USE_XLIB */

/* free transparency xtext->pixmap */
#if defined(USE_XLIB) || defined(WIN32)

static void gtk_xtext_free_trans(GtkXText * xtext)
{
	if (xtext->pixmap) {
#ifdef USE_SHM
		if (xtext->shm && have_shm_pixmaps(GDK_WINDOW_XDISPLAY(xtext->draw_buf))) {
			XFreePixmap(GDK_WINDOW_XDISPLAY(xtext->pixmap),
				    GDK_WINDOW_XWINDOW(xtext->pixmap));
			XShmDetach(GDK_WINDOW_XDISPLAY(xtext->draw_buf), &xtext->shminfo);
			shmdt(xtext->shminfo.shmaddr);
		}
#endif
		g_object_unref(xtext->pixmap);
		xtext->pixmap = NULL;
		xtext->shm = 0;
	}
}

#endif

/* grab pixmap from root window and set xtext->pixmap */
#if defined(USE_XLIB) || defined(WIN32)

static void gtk_xtext_load_trans(GtkXText * xtext)
{
	Pixmap rootpix;
	GtkWidget *widget = GTK_WIDGET(xtext);
	int x, y;

	rootpix =
		get_pixmap_prop(GDK_WINDOW_XDISPLAY(widget->window),
				GDK_WINDOW_XWINDOW(widget->window));
	if (rootpix == None) {
		if (xtext->error_function)
			xtext->error_function(0);
		xtext->transparent = FALSE;
		return;
	}

	gdk_window_get_origin(widget->window, &x, &y);

	if (xtext->shaded) {
		int width, height;
		gdk_drawable_get_size(GTK_WIDGET(xtext)->window, &width, &height);
		xtext->pixmap = shade_pixmap(xtext, rootpix, x, y, width + 105, height);
		if (xtext->pixmap == NULL) {
			xtext->shaded = 0;
			goto noshade;
		}
		gdk_gc_set_tile(xtext->bgc, xtext->pixmap);
		gdk_gc_set_ts_origin(xtext->bgc, 0, 0);
		xtext->ts_x = xtext->ts_y = 0;
	} else {
	      noshade:
#if (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION == 0)
		xtext->pixmap = gdk_pixmap_foreign_new(rootpix);
#else
		xtext->pixmap =
			gdk_pixmap_foreign_new_for_display(gdk_drawable_get_display
							   (GTK_WIDGET(xtext)->window), rootpix);
#endif
		gdk_gc_set_tile(xtext->bgc, xtext->pixmap);
		gdk_gc_set_ts_origin(xtext->bgc, -x, -y);
		xtext->ts_x = -x;
		xtext->ts_y = -y;
	}
	gdk_gc_set_fill(xtext->bgc, GDK_TILED);
}

#endif /* ! XLIB || WIN32 */

/* walk through str until this line doesn't fit anymore */

static int find_next_wrap(GtkXText * xtext, textentry * ent, const unsigned char *str, int win_width,
			  int indent)
{
	const unsigned char *last_space = str;
	const unsigned char *orig_str = str;
	int str_width = indent;
	int mbl;
	int char_width;
	int ret;

	/* single liners */
	if (win_width >= ent->str_width + ent->indent)
		return ent->str_len;

	/* it does happen! */
	if (win_width < 1) {
		ret = ent->str_len - (str - ent->str);
		goto done;
	}

	while (1) {
		char_width = backend_get_char_width(xtext, str, &mbl);
		str_width += char_width;
		if (str_width > win_width) {
			if (xtext->wordwrap) {
				if (str - last_space > WORDWRAP_LIMIT)
					ret = str - orig_str;	/* fall back to character wrap */
				else {
					if (*last_space == ' ')
						last_space++;
					ret = last_space - orig_str;
					if (ret == 0)	/* fall back to character wrap */
						ret = str - orig_str;
				}
				goto done;
			}
			ret = str - orig_str;
			goto done;
		}

		/* keep a record of the last space, for wordwrapping */
		if (is_del(*str))
			last_space = str;

		/* progress to the next char */
		str += mbl;

		if (str >= ent->str + ent->str_len) {
			ret = str - orig_str;
			goto done;
		}
	}
done:
	/* must make progress */
	if (ret < 1)
		ret = 1;

	return ret;
}

/* find the offset, in bytes, that wrap number 'line' starts at */

static int gtk_xtext_find_subline(GtkXText * xtext, textentry * ent, int line)
{
	int win_width;
	const unsigned char *str;
	int indent, str_pos, line_pos, len;

	if (ent->lines_taken < 2 || line < 1)
		return 0;

	/* we record the first 4 lines' wraps, so take a shortcut */
	if (line <= RECORD_WRAPS)
		return ent->wrap_offset[line - 1];

	gdk_drawable_get_size(GTK_WIDGET(xtext)->window, &win_width, 0);
	win_width -= MARGIN;

/*	indent = ent->indent;
	str = ent->str;
	line_pos = str_pos = 0;*/

	/* start from the last recorded wrap, and move forward */
	indent = xtext->buffer->indent;
	str_pos = ent->wrap_offset[RECORD_WRAPS - 1];
	str = str_pos + ent->str;
	line_pos = RECORD_WRAPS;

	do {
		len = find_next_wrap(xtext, ent, str, win_width, indent);
		indent = xtext->buffer->indent;
		str += len;
		str_pos += len;
		line_pos++;
		if (line_pos >= line)
			return str_pos;
	} while (str < ent->str + ent->str_len);

	return 0;
}

/* horrible hack for drawing time stamps */

static void gtk_xtext_render_stamp(GtkXText * xtext, textentry * ent, const unsigned char *text, int len, int line, int win_width)
{
	textentry tmp_ent;
	int jo, ji, hs;
	int xsize, y;

	/* trashing ent here, so make a backup first */
	memcpy(&tmp_ent, ent, sizeof(tmp_ent));
	ent->mb = TRUE;		/* make non-english days of the week work */
	jo = xtext->jump_out_offset;	/* back these up */
	ji = xtext->jump_in_offset;
	hs = xtext->hilight_start;
	xtext->jump_out_offset = 0;
	xtext->jump_in_offset = 0;
	xtext->hilight_start = 0xffff;	/* temp disable */

	if (xtext->mark_stamp) {
		/* if this line is marked, mark this stamp too */
		if (ent->mark_start == 0) {
			ent->mark_start = 0;
			ent->mark_end = len;
		} else {
			ent->mark_start = -1;
			ent->mark_end = -1;
		}
		ent->str = text;
	}

	y = (xtext->fontsize * line) + xtext->font->ascent - xtext->pixel_offset;
	gtk_xtext_render_str(xtext, y, ent, text, NULL, len, win_width, 2, line, TRUE, &xsize);

	/* restore everything back to how it was */
	memcpy(ent, &tmp_ent, sizeof(tmp_ent));
	xtext->jump_out_offset = jo;
	xtext->jump_in_offset = ji;
	xtext->hilight_start = hs;

	/* with a non-fixed-width font, sometimes we don't draw enough
	   background i.e. when this stamp is shorter than xtext->stamp_width */
	xsize += MARGIN;
	if (xsize < xtext->stamp_width) {
		y -= xtext->font->ascent;
		xtext_draw_bg(xtext, xsize,	/* x */
			      y,	/* y */
			      xtext->stamp_width - xsize,	/* width */
			      xtext->fontsize /* height */ );
	}
}

/* render a single line, which may wrap to more lines */

static int
gtk_xtext_render_line(GtkXText * xtext, textentry * ent, int line,
		      int lines_max, int subline, int win_width)
{
	const unsigned char *str;
	short *attr;
	int indent, taken, entline, len, y, start_subline;

	entline = taken = 0;
	str = ent->str;
	attr = ent->fstr->attr;
	indent = ent->indent;
	start_subline = subline;

#if 1
	/* draw the timestamp */
	if (xtext->auto_indent && xtext->buffer->time_stamp && (!xtext->skip_stamp || xtext->mark_stamp || xtext->force_stamp)) {
		const char *ts = timestamp_time("%H:%M:%S", ent->fstr->ts);
		int len = xstrlen(ts);

		gtk_xtext_render_stamp(xtext, ent, ts, len, line, win_width);
	}
#endif

	/* draw each line one by one */
	do {
		/* if it's one of the first 4 wraps, we don't need to calculate it, it's
		   recorded in ->wrap_offset. This saves us a loop. */
		if (entline < RECORD_WRAPS) {
			if (ent->lines_taken < 2)
				len = ent->str_len;
			else {
				if (entline > 0)
					len = ent->wrap_offset[entline] -
						ent->wrap_offset[entline - 1];
				else
					len = ent->wrap_offset[0];
			}
		} else
			len = find_next_wrap(xtext, ent, str, win_width, indent);

		entline++;

		y = (xtext->fontsize * line) + xtext->font->ascent - xtext->pixel_offset;
		if (!subline) {
			if (!gtk_xtext_render_str(xtext, y, ent, str, attr, len, win_width,
						  indent, line, FALSE, NULL)) {
				/* small optimization */
				gtk_xtext_draw_marker(xtext, ent,
						      y - xtext->fontsize * (taken +
									     start_subline + 1));
				return ent->lines_taken - subline;
			}
		} else {
			xtext->dont_render = TRUE;
			gtk_xtext_render_str(xtext, y, ent, str, attr, len, win_width,
					     indent, line, FALSE, NULL);
			xtext->dont_render = FALSE;
			subline--;
			line--;
			taken--;
		}

		indent = xtext->buffer->indent;
		line++;
		taken++;
		str += len;
		attr += len;

		if (line >= lines_max)
			break;

	} while (str < ent->str + ent->str_len);

	gtk_xtext_draw_marker(xtext, ent, y - xtext->fontsize * (taken + start_subline));

	return taken;
}

void gtk_xtext_set_palette(GtkXText * xtext, GdkColor palette[])
{
	int i;
	GdkColor col;

	for (i = (XTEXT_COLS - 1); i >= 0; i--) {
#ifdef USE_XFT
		xtext->color[i].color.red = palette[i].red;
		xtext->color[i].color.green = palette[i].green;
		xtext->color[i].color.blue = palette[i].blue;
		xtext->color[i].color.alpha = 0xffff;
		xtext->color[i].pixel = palette[i].pixel;
#endif
		xtext->palette[i] = palette[i].pixel;
	}

	if (GTK_WIDGET_REALIZED(xtext)) {
		xtext_set_fg(xtext, xtext->fgc, XTEXT_FG);
		xtext_set_bg(xtext, xtext->fgc, XTEXT_BG);
		xtext_set_fg(xtext, xtext->bgc, XTEXT_BG);

		col.pixel = xtext->palette[XTEXT_MARKER];
		gdk_gc_set_foreground(xtext->marker_gc, &col);
	}
	xtext->col_fore = XTEXT_FG;
	xtext->col_back = XTEXT_BG;
}

static void gtk_xtext_fix_indent(xtext_buffer * buf)
{
	int j;

	/* make indent a multiple of the space width */
	if (buf->indent && buf->xtext->space_width) {
		j = 0;
		while (j < buf->indent) {
			j += buf->xtext->space_width;
		}
		buf->indent = j;
	}

	dontscroll(buf);	/* force scrolling off */
}

static void gtk_xtext_recalc_widths(xtext_buffer * buf, int do_str_width)
{
	textentry *ent;

	/* since we have a new font, we have to recalc the text widths */
	for (ent = buf->text_first; ent; ent = ent->next) {
		if (do_str_width) {
			ent->str_width = gtk_xtext_text_width(buf->xtext, ent->str,
							      ent->str_len, NULL);
		}
		if (ent->left_len != -1) {
			ent->indent =
				(buf->indent -
				 gtk_xtext_text_width(buf->xtext, ent->str,
						      ent->left_len,
						      NULL)) - buf->xtext->space_width;
			if (ent->indent < MARGIN)
				ent->indent = MARGIN;
		}
	}

	gtk_xtext_calc_lines(buf, FALSE);
}

int gtk_xtext_set_font(GtkXText * xtext, char *name)
{
	int i;
	unsigned char c;

	if (xtext->font)
		backend_font_close(xtext);

	/* realize now, so that font_open has a XDisplay */
	gtk_widget_realize(GTK_WIDGET(xtext));

	backend_font_open(xtext, name);
	if (xtext->font == NULL)
		return FALSE;

	/* measure the width of every char;  only the ASCII ones for XFT */
	for (i = 0; i < sizeof(xtext->fontwidth) / sizeof(xtext->fontwidth[0]); i++) {
		c = i;
		xtext->fontwidth[i] = backend_get_text_width(xtext, &c, 1, TRUE);
	}
	xtext->space_width = xtext->fontwidth[' '];
	xtext->fontsize = xtext->font->ascent + xtext->font->descent;

#if 1
	{
		const char *time_str = timestamp("%H:%M:%S");
		int stamp_size = xstrlen(time_str);
		xtext->stamp_width =
			gtk_xtext_text_width(xtext, time_str, stamp_size, NULL) + MARGIN;
	}
#endif

	gtk_xtext_fix_indent(xtext->buffer);

	if (GTK_WIDGET_REALIZED(xtext))
		gtk_xtext_recalc_widths(xtext->buffer, TRUE);

	return TRUE;
}

void gtk_xtext_set_background(GtkXText * xtext, GdkPixmap * pixmap, gboolean trans)
{
	GdkGCValues val;
	gboolean shaded = FALSE;

	if (trans
	    && (xtext->tint_red != 255 || xtext->tint_green != 255 || xtext->tint_blue != 255))
		shaded = TRUE;

#if !defined(USE_XLIB) && !defined(WIN32)
	shaded = FALSE;
	trans = FALSE;
#endif

	if (xtext->pixmap) {
#if defined(USE_XLIB) || defined(WIN32)
		if (xtext->transparent)
			gtk_xtext_free_trans(xtext);
		else
#endif
			g_object_unref(xtext->pixmap);
		xtext->pixmap = NULL;
	}

	xtext->transparent = trans;

#if defined(USE_XLIB) || defined(WIN32)
	if (trans) {
		xtext->shaded = shaded;
		if (GTK_WIDGET_REALIZED(xtext))
			gtk_xtext_load_trans(xtext);
		return;
	}
#endif

	dontscroll(xtext->buffer);
	xtext->pixmap = pixmap;

	if (pixmap != 0) {
		g_object_ref(pixmap);
		if (GTK_WIDGET_REALIZED(xtext)) {
			gdk_gc_set_tile(xtext->bgc, pixmap);
			gdk_gc_set_ts_origin(xtext->bgc, 0, 0);
			xtext->ts_x = xtext->ts_y = 0;
			gdk_gc_set_fill(xtext->bgc, GDK_TILED);
		}
	} else if (GTK_WIDGET_REALIZED(xtext)) {
		g_object_unref(xtext->bgc);
		val.subwindow_mode = GDK_INCLUDE_INFERIORS;
		val.graphics_exposures = 0;
		xtext->bgc = gdk_gc_new_with_values(GTK_WIDGET(xtext)->window,
						    &val, GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);
		xtext_set_fg(xtext, xtext->bgc, XTEXT_BG);
	}
}

/* count how many lines 'ent' will take (with wraps) */

static int gtk_xtext_lines_taken(xtext_buffer * buf, textentry * ent)
{
	const unsigned char *str;
	int indent, taken, len;
	int win_width;

	win_width = buf->window_width - MARGIN;

	if (ent->str_width + ent->indent < win_width)
		return 1;

	indent = ent->indent;
	str = ent->str;
	taken = 0;

	do {
		len = find_next_wrap(buf->xtext, ent, str, win_width, indent);
		if (taken < RECORD_WRAPS)
			ent->wrap_offset[taken] = (str + len) - ent->str;
		indent = buf->indent;
		taken++;
		str += len;
	} while (str < ent->str + ent->str_len);

	return taken;
}

/* Calculate number of actual lines (with wraps), to set adj->lower. *
 * This should only be called when the window resizes.		     */

static void gtk_xtext_calc_lines(xtext_buffer * buf, int fire_signal)
{
	textentry *ent;
	int width;
	int height;
	int lines;

	gdk_drawable_get_size(GTK_WIDGET(buf->xtext)->window, &width, &height);
	width -= MARGIN;

	if (width < 30 || height < buf->xtext->fontsize || width < buf->indent + 30)
		return;

	lines = 0;

	for (ent = buf->text_first; ent; ent = ent->next) {
		ent->lines_taken = gtk_xtext_lines_taken(buf, ent);
		lines += ent->lines_taken;
	}

	buf->pagetop_ent = NULL;
	buf->num_lines = lines;
	gtk_xtext_adjustment_set(buf, fire_signal);
}

/* find the n-th line in the linked list, this includes wrap calculations */

static textentry *gtk_xtext_nth(GtkXText * xtext, int line, int *subline)
{
	int lines = 0;
	textentry *ent;

	ent = xtext->buffer->text_first;

	/* -- optimization -- try to make a short-cut using the pagetop ent */
	if (xtext->buffer->pagetop_ent) {
		if (line == xtext->buffer->pagetop_line) {
			*subline = xtext->buffer->pagetop_subline;
			return xtext->buffer->pagetop_ent;
		}
		if (line > xtext->buffer->pagetop_line) {
			/* lets start from the pagetop instead of the absolute beginning */
			ent = xtext->buffer->pagetop_ent;
			lines = xtext->buffer->pagetop_line - xtext->buffer->pagetop_subline;
		} else if (line > xtext->buffer->pagetop_line - line) {
			/* move backwards from pagetop */
			ent = xtext->buffer->pagetop_ent;
			lines = xtext->buffer->pagetop_line - xtext->buffer->pagetop_subline;
			while (1) {
				if (lines <= line) {
					*subline = line - lines;
					return ent;
				}
				ent = ent->prev;
				if (!ent)
					break;
				lines -= ent->lines_taken;
			}
			return NULL;
		}
	}
	/* -- end of optimization -- */

	while (ent) {
		lines += ent->lines_taken;
		if (lines > line) {
			*subline = ent->lines_taken - (lines - line);
			return ent;
		}
		ent = ent->next;
	}
	return NULL;
}

/* render enta (or an inclusive range enta->entb) */

static int gtk_xtext_render_ents(GtkXText * xtext, textentry * enta, textentry * entb)
{
	textentry *ent, *orig_ent, *tmp_ent;
	int line;
	int lines_max;
	int width;
	int height;
	int subline;
	int drawing = FALSE;

	if (xtext->buffer->indent < MARGIN)
		xtext->buffer->indent = MARGIN;	/* 2 pixels is our left margin */

	gdk_drawable_get_size(GTK_WIDGET(xtext)->window, &width, &height);
	width -= MARGIN;

	if (width < 32 || height < xtext->fontsize || width < xtext->buffer->indent + 30)
		return 0;

	lines_max = ((height + xtext->pixel_offset) / xtext->fontsize) + 1;
	line = 0;
	orig_ent = xtext->buffer->pagetop_ent;
	subline = xtext->buffer->pagetop_subline;

	/* used before a complete page is in buffer */
	if (orig_ent == NULL)
		orig_ent = xtext->buffer->text_first;

	/* check if enta is before the start of this page */
	if (entb) {
		for (tmp_ent = orig_ent; tmp_ent; tmp_ent = tmp_ent->next) {
			if (tmp_ent == enta)
				break;
			if (tmp_ent == entb) {
				drawing = TRUE;
				break;
			}
		}
	}

	for (ent = orig_ent; ent; ent = ent->next) {
		if (entb && ent == enta)
			drawing = TRUE;

		if (drawing || ent == entb || ent == enta) {
			gtk_xtext_reset(xtext, FALSE, TRUE);
			line += gtk_xtext_render_line(xtext, ent, line, lines_max, subline, width);
			subline = 0;
			xtext->jump_in_offset = 0;	/* jump_in_offset only for the 1st */
		} else {
			if (ent == orig_ent) {
				line -= subline;
				subline = 0;
			}
			line += ent->lines_taken;
		}

		if (ent == entb)
			break;

		if (line >= lines_max)
			break;
	}

	/* space below last line */
	return (xtext->fontsize * line) - xtext->pixel_offset;
}

/* render a whole page/window, starting from 'startline' */

static void gtk_xtext_render_page(GtkXText * xtext)
{
	textentry *ent;
	int line;
	int lines_max;
	int width;
	int height;
	int subline;
	int startline = xtext->adj->value;

	if (!GTK_WIDGET_REALIZED(xtext))
		return;

	if (xtext->buffer->indent < MARGIN)
		xtext->buffer->indent = MARGIN;	/* 2 pixels is our left margin */

	gdk_drawable_get_size(GTK_WIDGET(xtext)->window, &width, &height);

	if (width < 34 || height < xtext->fontsize || width < xtext->buffer->indent + 32)
		return;

#ifdef SMOOTH_SCROLL
	xtext->pixel_offset = (xtext->adj->value - startline) * xtext->fontsize;
#else
	xtext->pixel_offset = 0;
#endif

	subline = line = 0;
	ent = xtext->buffer->text_first;

	if (startline > 0)
		ent = gtk_xtext_nth(xtext, startline, &subline);

	xtext->buffer->pagetop_ent = ent;
	xtext->buffer->pagetop_subline = subline;
	xtext->buffer->pagetop_line = startline;

#ifdef SCROLL_HACK
	{
		int pos, overlap;
		GdkRectangle area;

		if (xtext->buffer->num_lines <= xtext->adj->page_size)
			dontscroll(xtext->buffer);

#ifdef SMOOTH_SCROLL
		pos = xtext->adj->value * xtext->fontsize;
#else
		pos = startline * xtext->fontsize;
#endif
		overlap = xtext->buffer->last_pixel_pos - pos;
		xtext->buffer->last_pixel_pos = pos;

#ifdef USE_DB
		if (!xtext->pixmap && abs(overlap) < height)
#else
		/* dont scroll PageUp/Down without a DB, it looks ugly */
		if (!xtext->pixmap && abs(overlap) < height - (3 * xtext->fontsize))
#endif
		{
			/* so the obscured regions are exposed */
			gdk_gc_set_exposures(xtext->fgc, TRUE);
			if (overlap < 1) {	/* DOWN */
				int remainder;

				gdk_draw_drawable(xtext->draw_buf, xtext->fgc, xtext->draw_buf,
						  0, -overlap, 0, 0, width, height + overlap);
				remainder = ((height - xtext->font->descent) % xtext->fontsize) +
					xtext->font->descent;
				area.y = (height + overlap) - remainder;
				area.height = remainder - overlap;
			} else {
				gdk_draw_drawable(xtext->draw_buf, xtext->fgc, xtext->draw_buf,
						  0, 0, 0, overlap, width, height - overlap);
				area.y = 0;
				area.height = overlap;
			}
			gdk_gc_set_exposures(xtext->fgc, FALSE);

			if (area.height > 0) {
				area.x = 0;
				area.width = width;
				gtk_xtext_paint(GTK_WIDGET(xtext), &area);
			}
			xtext->buffer->grid_dirty = TRUE;

			return;
		}
	}
#endif

	xtext->buffer->grid_dirty = FALSE;
	width -= MARGIN;
	lines_max = ((height + xtext->pixel_offset) / xtext->fontsize) + 1;

	while (ent) {
		gtk_xtext_reset(xtext, FALSE, TRUE);
		line += gtk_xtext_render_line(xtext, ent, line, lines_max, subline, width);
		subline = 0;

		if (line >= lines_max)
			break;

		ent = ent->next;
	}

	line = (xtext->fontsize * line) - xtext->pixel_offset;
	/* fill any space below the last line with our background GC */
	xtext_draw_bg(xtext, 0, line, width + MARGIN, height - line);

	/* draw the separator line */
	gtk_xtext_draw_sep(xtext, -1);
}

void gtk_xtext_refresh(GtkXText * xtext, int do_trans)
{
	if (GTK_WIDGET_REALIZED(GTK_WIDGET(xtext))) {
#if defined(USE_XLIB) || defined(WIN32)
		if (xtext->transparent && do_trans) {
			gtk_xtext_free_trans(xtext);
			gtk_xtext_load_trans(xtext);
		}
#endif
		gtk_xtext_render_page(xtext);
	}
}


static gboolean gtk_xtext_check_ent_visibility(GtkXText * xtext, textentry * find_ent, int add);
static int gtk_xtext_render_page_timeout(GtkXText * xtext);

/* remove the topline from the list */
static void gtk_xtext_remove_top(xtext_buffer * buffer)
{
	int visible;
	textentry *ent;

	ent = buffer->text_first;
	if (!ent)
		return;
	buffer->num_lines -= ent->lines_taken;
	buffer->pagetop_line -= ent->lines_taken;
	buffer->last_pixel_pos -= (ent->lines_taken * buffer->xtext->fontsize);
	buffer->text_first = ent->next;
	buffer->text_first->prev = NULL;

	buffer->old_value -= ent->lines_taken;
	if (buffer->xtext->buffer == buffer) {	/* is it the current buffer? */
		buffer->xtext->adj->value -= ent->lines_taken;
		buffer->xtext->select_start_adj -= ent->lines_taken;
	}

	visible = buffer->xtext->buffer == buffer && ent == buffer->pagetop_ent;

	if (ent == buffer->pagetop_ent)
		buffer->pagetop_ent = NULL;

	if (ent == buffer->last_ent_start)
		buffer->last_ent_start = ent->next;

	if (ent == buffer->last_ent_end) {
		buffer->last_ent_start = NULL;
		buffer->last_ent_end = NULL;
	}

	if (buffer->marker_pos == ent)
		buffer->marker_pos = NULL;

	free(ent);

	if (visible) {
		if (!buffer->xtext->add_io_tag) {
			/* remove scrolling events */
			if (buffer->xtext->io_tag) {
				g_source_remove(buffer->xtext->io_tag);
				buffer->xtext->io_tag = 0;
			}
			buffer->xtext->force_render = TRUE;
			buffer->xtext->add_io_tag = g_timeout_add(REFRESH_TIMEOUT * 2, (GSourceFunc)
							       gtk_xtext_render_page_timeout,
							       buffer->xtext);
		}
	}
}

void gtk_xtext_clear(xtext_buffer * buf)
{
	textentry *next;

	if (buf->xtext->auto_indent)
		buf->indent = MARGIN;
	buf->scrollbar_down = TRUE;
	buf->last_ent_start = NULL;
	buf->last_ent_end = NULL;
	buf->marker_pos = NULL;
	dontscroll(buf);

	while (buf->text_first) {
		next = buf->text_first->next;
		free(buf->text_first);
		buf->text_first = next;
	}
	buf->text_last = NULL;

	if (buf->xtext->buffer == buf) {
		gtk_xtext_calc_lines(buf, TRUE);
		gtk_xtext_refresh(buf->xtext, 0);
	} else {
		gtk_xtext_calc_lines(buf, FALSE);
	}
}

static gboolean gtk_xtext_check_ent_visibility(GtkXText * xtext, textentry * find_ent, int add)
{
	textentry *ent;
	int lines_max;
	int line = 0;
	int width;
	int height;

	gdk_drawable_get_size(GTK_WIDGET(xtext)->window, &width, &height);

	lines_max = ((height + xtext->pixel_offset) / xtext->fontsize) + add;
	ent = xtext->buffer->pagetop_ent;

	while (ent && line < lines_max) {
		if (find_ent == ent)
			return TRUE;
		line += ent->lines_taken;
		ent = ent->next;
	}

	return FALSE;
}

void gtk_xtext_check_marker_visibility(GtkXText * xtext)
{
	if (gtk_xtext_check_ent_visibility(xtext, xtext->buffer->marker_pos, 1))
		xtext->buffer->marker_seen = TRUE;
}

static int gtk_xtext_render_page_timeout(GtkXText * xtext)
{
	GtkAdjustment *adj = xtext->adj;

	xtext->add_io_tag = 0;

	/* less than a complete page? */
	if (xtext->buffer->num_lines <= adj->page_size) {
		xtext->buffer->old_value = 0;
		adj->value = 0;
		gtk_xtext_render_page(xtext);
	} else if (xtext->buffer->scrollbar_down) {
		g_signal_handler_block(xtext->adj, xtext->vc_signal_tag);
		gtk_xtext_adjustment_set(xtext->buffer, FALSE);
		gtk_adjustment_set_value(adj, adj->upper - adj->page_size);
		g_signal_handler_unblock(xtext->adj, xtext->vc_signal_tag);
		xtext->buffer->old_value = adj->value;
		gtk_xtext_render_page(xtext);
	} else {
		gtk_xtext_adjustment_set(xtext->buffer, TRUE);
		if (xtext->force_render) {
			xtext->force_render = FALSE;
			gtk_xtext_render_page(xtext);
		}
	}

	return 0;
}

/* append a textentry to our linked list */

static void gtk_xtext_append_entry(xtext_buffer * buf, textentry * ent)
{
	int mb;

/* xchat->ekg2, note: i removed here strtr(ent->str, '\t', ' ') 
 *		coz we should pass here only fstring_t, where fstring_t can't have \t */

	ent->str_width = gtk_xtext_text_width(buf->xtext, ent->str, ent->str_len, &mb);
	ent->mb = FALSE;
	if (mb)
		ent->mb = TRUE;
	ent->mark_start = -1;
	ent->mark_end = -1;
	ent->next = NULL;

	if (ent->indent < MARGIN)
		ent->indent = MARGIN;	/* 2 pixels is the left margin */

	/* append to our linked list */
	if (buf->text_last)
		buf->text_last->next = ent;
	else
		buf->text_first = ent;
	ent->prev = buf->text_last;
	buf->text_last = ent;

	ent->lines_taken = gtk_xtext_lines_taken(buf, ent);
	buf->num_lines += ent->lines_taken;

	if (buf->reset_marker_pos ||
	    ((buf->marker_pos == NULL || buf->marker_seen) && (buf->xtext->buffer != buf ||
#if GTK_CHECK_VERSION(2,4,0)
							       !gtk_window_has_toplevel_focus
							       (GTK_WINDOW
								(gtk_widget_get_toplevel
								 (GTK_WIDGET(buf->xtext))))
#else
							       !(GTK_WINDOW
								 (gtk_widget_get_toplevel
								  (GTK_WIDGET(buf->xtext))))->
							       has_focus
#endif
	     ))) {
		buf->marker_pos = ent;
		dontscroll(buf);	/* force scrolling off */
		buf->marker_seen = FALSE;
		buf->reset_marker_pos = FALSE;
	}

	if (buf->xtext->max_lines > 2 && buf->xtext->max_lines < buf->num_lines) {
		gtk_xtext_remove_top(buf);
	}

	if (buf->xtext->buffer == buf) {
#ifdef SCROLL_HACK
		/* this could be improved */
		if ((buf->num_lines - 1) <= buf->xtext->adj->page_size)
			dontscroll(buf);
#endif

		if (!buf->xtext->add_io_tag) {
			/* remove scrolling events */
			if (buf->xtext->io_tag) {
				g_source_remove(buf->xtext->io_tag);
				buf->xtext->io_tag = 0;
			}
			buf->xtext->add_io_tag = g_timeout_add(REFRESH_TIMEOUT * 2, (GSourceFunc)
							       gtk_xtext_render_page_timeout,
							       buf->xtext);
		}
	} else if (buf->scrollbar_down) {
		buf->old_value = buf->num_lines - buf->xtext->adj->page_size;
		if (buf->old_value < 0)
			buf->old_value = 0;
	}
}

/* XXX: gtk_xtext_append(), gtk_xtext_append_indent() */

void gtk_xtext_append_fstring(xtext_buffer *buf, fstring_t *fstr)
{
	textentry *ent;
	int space;
	int tempindent;

	size_t len = xstrlen(fstr->str.b);

	if (len >= sizeof(buf->xtext->scratch_buffer))
		len = sizeof(buf->xtext->scratch_buffer) - 1;

	ent = xmalloc(sizeof(textentry));
	ent->fstr = fstr;

	/* NOTE, xchat create new string with str[0] = ' ' str[1...] = str[0...] 
	 *	i don't know why, but without it, ui looks ugly.
	 *
	 *	slowdown, hack.
	 */

	ent->fstr->str.b = xrealloc(ent->fstr->str.b, sizeof(char) * (len+2));
		memmove(ent->fstr->str.b+1, ent->fstr->str.b, len);
		ent->fstr->str.b[0] = ' ';
		ent->fstr->str.b[len+1] = '\0';
	ent->fstr->attr  = xrealloc(ent->fstr->attr, sizeof(short) * (len+1));
		memmove(ent->fstr->attr+1, ent->fstr->attr, len*sizeof(short));
		ent->fstr->attr[0] = FSTR_NORMAL;

	ent->left_len = 0;
	ent->str = fstr->str.b;
	ent->str_len = len+1;
	ent->indent = (buf->indent) - buf->xtext->space_width;

	if (buf->time_stamp)
		space = buf->xtext->stamp_width;
	else
		space = 0;

	/* do we need to auto adjust the separator position? */
	if (buf->xtext->auto_indent && ent->indent < MARGIN + space) {
		tempindent = MARGIN + space + buf->xtext->space_width;

		if (tempindent > buf->indent)
			buf->indent = tempindent;

		if (buf->indent > buf->xtext->max_auto_indent)
			buf->indent = buf->xtext->max_auto_indent;

		gtk_xtext_fix_indent(buf);
		gtk_xtext_recalc_widths(buf, FALSE);

		ent->indent = (buf->indent) - buf->xtext->space_width;
		buf->xtext->force_render = TRUE;
	}

	gtk_xtext_append_entry(buf, ent);

}

void gtk_xtext_set_error_function(GtkXText * xtext, void (*error_function) (int))
{
	xtext->error_function = error_function;
}

void gtk_xtext_set_indent(GtkXText * xtext, gboolean indent)
{
	xtext->auto_indent = indent;
}

void gtk_xtext_set_max_indent(GtkXText * xtext, int max_auto_indent)
{
	xtext->max_auto_indent = max_auto_indent;
}

void gtk_xtext_set_max_lines(GtkXText * xtext, int max_lines)
{
	xtext->max_lines = max_lines;
}

void gtk_xtext_set_show_marker(GtkXText * xtext, gboolean show_marker)
{
	xtext->marker = show_marker;
}

void gtk_xtext_set_show_separator(GtkXText * xtext, gboolean show_separator)
{
	xtext->separator = show_separator;
}

void gtk_xtext_set_thin_separator(GtkXText * xtext, gboolean thin_separator)
{
	xtext->thinline = thin_separator;
}

void gtk_xtext_set_time_stamp(xtext_buffer * buf, gboolean time_stamp)
{
	buf->time_stamp = time_stamp;
}

void gtk_xtext_set_tint(GtkXText * xtext, int tint_red, int tint_green, int tint_blue)
{
	xtext->tint_red = tint_red;
	xtext->tint_green = tint_green;
	xtext->tint_blue = tint_blue;

	/*if (xtext->tint_red != 255 || xtext->tint_green != 255 || xtext->tint_blue != 255)
	   shaded = TRUE; */
}

void
gtk_xtext_set_urlcheck_function(GtkXText * xtext,
				int (*urlcheck_function) (GtkWidget *, char *, int))
{
	xtext->urlcheck_function = urlcheck_function;
}

void gtk_xtext_set_wordwrap(GtkXText * xtext, gboolean wordwrap)
{
	xtext->wordwrap = wordwrap;
}

void gtk_xtext_reset_marker_pos(GtkXText * xtext)
{
	xtext->buffer->marker_pos = NULL;
	dontscroll(xtext->buffer);	/* force scrolling off */
	gtk_xtext_render_page(xtext);
	xtext->buffer->reset_marker_pos = TRUE;
}

void gtk_xtext_buffer_show(GtkXText * xtext, xtext_buffer * buf, int render)
{
	int w, h;

	buf->xtext = xtext;

	if (xtext->buffer == buf)
		return;

/*printf("text_buffer_show: xtext=%p buffer=%p\n", xtext, buf);*/

	if (xtext->add_io_tag) {
		g_source_remove(xtext->add_io_tag);
		xtext->add_io_tag = 0;
	}

	if (xtext->io_tag) {
		g_source_remove(xtext->io_tag);
		xtext->io_tag = 0;
	}

	if (!GTK_WIDGET_REALIZED(GTK_WIDGET(xtext)))
		gtk_widget_realize(GTK_WIDGET(xtext));

	gdk_drawable_get_size(GTK_WIDGET(xtext)->window, &w, &h);

	/* after a font change */
	if (buf->needs_recalc) {
		buf->needs_recalc = FALSE;
		gtk_xtext_recalc_widths(buf, TRUE);
	}

	/* now change to the new buffer */
	xtext->buffer = buf;
	dontscroll(buf);	/* force scrolling off */
	xtext->adj->value = buf->old_value;
	xtext->adj->upper = buf->num_lines;
	if (xtext->adj->upper == 0) {
		xtext->adj->upper = 1;
	/* sanity check */
	} else if (xtext->adj->value > xtext->adj->upper - xtext->adj->page_size) {
		/*buf->pagetop_ent = NULL; */
		xtext->adj->value = xtext->adj->upper - xtext->adj->page_size;
		if (xtext->adj->value < 0)
			xtext->adj->value = 0;
	}

	if (render) {
		/* did the window change size since this buffer was last shown? */
		if (buf->window_width != w) {
			buf->window_width = w;
			gtk_xtext_calc_lines(buf, FALSE);
			if (buf->scrollbar_down)
				gtk_adjustment_set_value(xtext->adj, xtext->adj->upper -
							 xtext->adj->page_size);
		} else if (buf->window_height != h) {
			buf->window_height = h;
			buf->pagetop_ent = NULL;
			gtk_xtext_adjustment_set(buf, FALSE);
		}

		gtk_xtext_render_page(xtext);
		gtk_adjustment_changed(xtext->adj);
	} else {
		/* avoid redoing the transparency */
		xtext->avoid_trans = TRUE;
	}
}

xtext_buffer *gtk_xtext_buffer_new(GtkXText * xtext) {
	xtext_buffer *buf = xmalloc(sizeof(xtext_buffer));
	buf->old_value		= -1;
	buf->xtext		= xtext;
	buf->scrollbar_down	= TRUE;
	buf->indent		= xtext->space_width * 2;
	dontscroll(buf);

	return buf;
}

void gtk_xtext_buffer_free(xtext_buffer * buf) {
	textentry *ent;

	if (buf->xtext->buffer == buf)
		buf->xtext->buffer = buf->xtext->orig_buffer;

	if (buf->xtext->selection_buffer == buf)
		buf->xtext->selection_buffer = NULL;

	for (ent = buf->text_first; ent;) {
		textentry *next = ent->next;
		free(ent);
		/* XXX, fstring_t */
		ent = next;
	}

	free(buf);
}

/* gtk_xtext_search() XXX, lastlog */
/* gtk_xtext_save() XXX? */
/* gtk_xtext_lastlog() XXX */
/* gtk_xtext_foreach() */
/* gtk_xtext_is_empty() */
