/* See LICENSE file for copyright and license details. */
#include "common.h"

#define FIRST_CHARACTER     0x0000u
#define LAST_CHARACTER      0xFFFFu
#define REFERENCE_CHARACTER "X"
#define CELL_WIDTH_FACTOR   4
#define CELL_HEIGHT_FACTOR  2
#define CELL_X_CLEARANCE    2
#define CELL_Y_CLEARANCE    2
#define BASE_FONT_FAMILY    "Sans"
#define BASE_FONT_SIZE      22

G_DEFINE_TYPE(CharTable, gcmap_char_table, GTK_TYPE_DRAWING_AREA)


static int
is_noncharacter(gunichar ch)
{
	return (0xFDD0u <= ch && ch <= 0xFDEFu) || (ch & 0xFFFEu) == 0xFFFEu;
}


static int
is_printable_character(gunichar ch)
{
	return g_unichar_validate(ch) &&
	       !g_unichar_iscntrl(ch) &&
	       !g_unichar_iszerowidth(ch) &&
	       !is_noncharacter(ch);
}


static void
format_character(gunichar ch, char *buf)
{
	if (is_printable_character(ch))
		buf[g_unichar_to_utf8(ch, buf)] = '\0';
#ifdef TODO /* this part is just for testing */
	else if (ch == 0x7Fu)
		stpcpy(buf, "DELETE");
	else
		stpcpy(buf, "X");
#else
	else
		buf[0u] = '\0';
#endif
}


static void
apply_font_size(CharTable *this, int size, int *text_w_out, int *text_h_out)
{
	pango_font_description_set_size(this->font, size * PANGO_SCALE);
	pango_layout_set_font_description(this->layout, this->font);
	pango_layout_context_changed(this->layout);
	pango_layout_get_pixel_size(this->layout, text_w_out, text_h_out);
}


static void
fit_text(CharTable *this, const char *text, int *text_w_out, int *text_h_out)
{
	int size, ok_size = 1;
	double scale_x, scale_y, scale;

	/* Set text to measure */
	pango_layout_set_text(this->layout, text, -1);

	/* Try default size */
	apply_font_size(this, BASE_FONT_SIZE, text_w_out, text_h_out);
	if (*text_w_out <= this->max_w && *text_h_out <= this->max_h)
		return;

	/* Too large? Then estimate largest smaller size that would fit */
	scale_x = (double)this->max_w / (double)*text_w_out;
	scale_y = (double)this->max_h / (double)*text_h_out;
	scale = MIN(scale_x, scale_y);
	scale = MIN(scale, 1) * BASE_FONT_SIZE;
	size = (int)scale;
	size = MAX(size, 1);

	/* Check and decrease by one until it fits */
	for (; size > 1; size--) {
		apply_font_size(this, size, text_w_out, text_h_out);
		if (*text_w_out <= this->max_w && *text_h_out <= this->max_h) {
			ok_size = size;
			break;
		}
	}

	/* Maybe it was too small, then try larger */
	if (*text_w_out < this->max_w && *text_h_out < this->max_h) {
		goto start;
		do {
			ok_size++;
		start:
			if (ok_size == BASE_FONT_SIZE)
				break;
			apply_font_size(this, ok_size + 1, text_w_out, text_h_out);
		} while (*text_w_out < this->max_w && *text_h_out < this->max_h);
	}

	/* Use the on that fit, 1 if none fit */
	if (ok_size != size)
		apply_font_size(this, ok_size, text_w_out, text_h_out);
}


static int
gcmap_char_table_expose(GtkWidget *widget, GdkEventExpose *event)
{
	CharTable *this = (CharTable *)widget;
	GdkRectangle area = event->area;
	int r0 = MAX(area.y * this->nrows / widget->allocation.height, 0);
	int c0 = MAX(area.x * this->ncols / widget->allocation.width,  0);
	int r1 = MIN((area.y + area.height - 1) * this->nrows / widget->allocation.height + 1, this->nrows);
	int c1 = MIN((area.x + area.width  - 1) * this->ncols / widget->allocation.width  + 1, this->ncols);
	GtkStyle *style = gtk_widget_get_style(widget);
	cairo_t *g;
	char text[16];
	int r, c, x0, x1, y0, y1;
	int text_w, text_h;
	size_t index, index1;
	gunichar ch;

	g = gdk_cairo_create(widget->window);
	if (!g)
		eprintf("gdk_cairo_create:");

	/* Configure drawing area */
	gdk_cairo_rectangle(g, &area);
	cairo_clip(g);

	/* Draw background */
	set_source_colour(g, &style->base[GTK_STATE_NORMAL]);
	cairo_paint(g);

	/* Draw grid lines */
	set_source_colour_blend(g, &style->text[GTK_STATE_NORMAL], 0.5, &style->base[GTK_STATE_NORMAL]);
	cairo_set_line_width(g, 1.0);
	/* horizontal */
	y0 = widget->allocation.height * (r0 + 1) / this->nrows;
	x0 = widget->allocation.width * c0 / this->ncols;
	x1 = widget->allocation.width * c1 / this->ncols;
	for (r = r0 + 1; r < r1; r++, y0 = y1) {
		y1 = widget->allocation.height * (r + 1) / this->nrows;
		cairo_move_to(g, x0, y0 + 0.5);
		cairo_line_to(g, x1, y0 + 0.5);
		cairo_stroke(g);
	}
	/* vertical */
	x0 = widget->allocation.width * (c0 + 1) / this->ncols;
	y0 = widget->allocation.height * r0 / this->nrows;
	y1 = widget->allocation.height * r1 / this->nrows;
	for (c = c0 + 1; c < c1; c++, x0 = x1) {
		x1 = widget->allocation.width * (c + 1) / this->ncols;
		cairo_move_to(g, x0 + 0.5, y0);
		cairo_line_to(g, x0 + 0.5, y1);
		cairo_stroke(g);
	}

	/* Draw gryphs */
	set_source_colour(g, &style->text[GTK_STATE_NORMAL]);
	index = (size_t)r0 * (size_t)this->ncols + (size_t)c0;
	y0 = widget->allocation.height * r0 / this->nrows;
	for (r = r0; r < r1; r++, y0 = y1, index = index1) {
		index1 = index + (size_t)this->ncols;
		x0 = widget->allocation.width * c0 / this->ncols;
		y1 = widget->allocation.height * (r + 1) / this->nrows;
		for (c = c0; c < c1; c++, index++, x0 = x1) {
			ch = FIRST_CHARACTER + (gunichar)index;
			if (ch > LAST_CHARACTER)
				goto out;
			x1 = widget->allocation.width * (c + 1) / this->ncols;

			format_character(ch, text);
			fit_text(this, text, &text_w, &text_h);
			cairo_move_to(g, (x0 + x1 - text_w) / 2, (y0 + y1 - text_h) / 2);
			pango_cairo_show_layout(g, this->layout);
		}
	}

out:
	cairo_destroy(g);
	return 0;
}


static void
gcmap_char_table_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	CharTable *this = (CharTable *)widget;
	int text_w, text_h;
	int cell_w, cell_h;

	GTK_WIDGET_CLASS(gcmap_char_table_parent_class)->size_allocate(widget, allocation);

	pango_layout_set_text(this->layout, REFERENCE_CHARACTER, -1);
	apply_font_size(this, BASE_FONT_SIZE, &text_w, &text_h);
	cell_w = MAX(text_w, 1) * CELL_WIDTH_FACTOR;
	cell_h = MAX(text_h, 1) * CELL_HEIGHT_FACTOR;
	this->ncols = MAX(widget->allocation.width  / cell_w, 1);
	this->nrows = MAX(widget->allocation.height / cell_h, 1);
	cell_w = widget->allocation.width  / this->ncols - 1;
	cell_h = widget->allocation.height / this->nrows - 1;
	this->max_w = cell_w - CELL_X_CLEARANCE * 2;
	this->max_h = cell_h - CELL_Y_CLEARANCE * 2;
	this->max_w = this->max_w < 1 ? cell_w : this->max_w;
	this->max_h = this->max_h < 1 ? cell_h : this->max_h;
}


static void
gcmap_char_table_init(CharTable *this)
{
	this->nrows = 1; /* will be set by gcmap_char_table_size_allocate */
	this->ncols = 1; /* will be set by gcmap_char_table_size_allocate */

	this->font = pango_font_description_new();
	if (!this->font)
		eprintf("pango_font_description_new:");
	pango_font_description_set_family(this->font, BASE_FONT_FAMILY);
	pango_font_description_set_size(this->font, BASE_FONT_SIZE * PANGO_SCALE);
	pango_font_description_set_weight(this->font, PANGO_WEIGHT_NORMAL);

	this->layout = gtk_widget_create_pango_layout((GtkWidget *)this, "");
	if (!this->layout)
		eprintf("gtk_widget_create_pango_layout:");

	/* TODO on tear down: pango_font_description_free(this->font); */
	/* TODO on tear down: g_object_unref(this->layout); */
}


static void
gcmap_char_table_class_init(CharTableClass *class)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(class);
	widget_class->expose_event = &gcmap_char_table_expose;
	widget_class->size_allocate = &gcmap_char_table_size_allocate;
}


GtkWidget *
gcmap_char_table_new(void)
{
	return g_object_new(GCMAP_TYPE_CHAR_TABLE, NULL);
}
