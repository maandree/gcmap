/* See LICENSE file for copyright and license details. */
#include "common.h"

#define REFERENCE_CHARACTER "X"
#define CELL_WIDTH_FACTOR   4
#define CELL_HEIGHT_FACTOR  2
#define CELL_X_CLEARANCE    2
#define CELL_Y_CLEARANCE    2

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
	if (!is_printable_character(ch)) {
		buf[0u] = '\0';
		return;
	}
	buf[g_unichar_to_utf8(ch, buf)] = '\0';
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

	/* Try desired size */
	apply_font_size(this, this->font_size, text_w_out, text_h_out);
	if (*text_w_out <= this->max_w && *text_h_out <= this->max_h)
		return;

	/* Too large? Then estimate largest smaller size that would fit */
	scale_x = (double)this->max_w / (double)*text_w_out;
	scale_y = (double)this->max_h / (double)*text_h_out;
	scale = MIN(scale_x, scale_y);
	scale = MIN(scale, 1) * this->font_size;
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
			if (ok_size == this->font_size)
				break;
			apply_font_size(this, ok_size + 1, text_w_out, text_h_out);
		} while (*text_w_out < this->max_w && *text_h_out < this->max_h);
	}

	/* Use the on that fit, 1 if none fit */
	if (ok_size != size)
		apply_font_size(this, ok_size, text_w_out, text_h_out);
}


static gboolean
gcmap_char_table_expose(GtkWidget *widget, GdkEventExpose *event)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	GdkRectangle area = event->area;
	int r0 = MAX(area.y * this->nrows / this->viewport_h, 0);
	int c0 = MAX(area.x * this->ncols / this->viewport_w, 0);
	int r1 = MIN((area.y + area.height - 1) * this->nrows / this->viewport_h + 1, r0 + this->nrows);
	int c1 = MIN((area.x + area.width  - 1) * this->ncols / this->viewport_w + 1, c0 + this->ncols);
	GtkStyle *style = gtk_widget_get_style(widget);
	cairo_t *g;
	char text[16];
	int r, c, x0, x1, y0, y1;
	int text_w, text_h;
	size_t skip, range, row_skip;
	uint32_t cp;

	g = gdk_cairo_create(widget->window);
	if (!g)
		eprintf("gdk_cairo_create:");

	/* Make sure it still looks good even if starting in the middle of row */
	r1 += 1;

	/* Configure drawing area */
	gdk_cairo_rectangle(g, &area);
	cairo_clip(g);

	/* Draw background */
	set_source_colour(g, &style->base[GTK_STATE_NORMAL]);
	cairo_paint(g);

	/* Draw grid lines */
	set_source_colour_blend(g, &style->base[GTK_STATE_NORMAL], 0.10, &style->text[GTK_STATE_NORMAL]);
	cairo_set_line_width(g, 1.0);
	/* horizontal */
	y0 = this->viewport_h * (r0 + 1) / this->nrows;
	x0 = this->viewport_w * c0 / this->ncols;
	x1 = this->viewport_w * c1 / this->ncols;
	for (r = r0 + (area.y > 0); r < r1; r++, y0 = y1) {
		y1 = this->viewport_h * (r + 1) / this->nrows;
		cairo_move_to(g, x0, y0 + 0.5);
		cairo_line_to(g, x1, y0 + 0.5);
		cairo_stroke(g);
	}
	/* vertical */
	x0 = this->viewport_w * (c0 + 1) / this->ncols;
	y0 = this->viewport_h * r0 / this->nrows;
	y1 = this->viewport_h * r1 / this->nrows;
	for (c = c0 + (area.x > 0); c < c1; c++, x0 = x1) {
		x1 = this->viewport_w * (c + 1) / this->ncols;
		cairo_move_to(g, x0 + 0.5, y0);
		cairo_line_to(g, x0 + 0.5, y1);
		cairo_stroke(g);
	}

	/* Draw gryphs */
	if (this->nranges == 0u)
		goto out;
	set_source_colour(g, &style->text[GTK_STATE_NORMAL]);
	y0 = this->viewport_h * r0 / this->nrows;
	range = 0u;
	cp = this->ranges[range].first;
	skip = (size_t)r0 * (size_t)this->ncols + (size_t)c0;
	row_skip = (size_t)this->ncols - (size_t)(c1 - c0);
	for (r = r0; r < r1; r++, y0 = y1, skip = row_skip) {
		while (skip) {
			size_t max_jump = (size_t)(this->ranges[range].last - cp + 1u);
			size_t jump = MIN(skip, max_jump);
			cp += (uint32_t)jump;
			skip -= jump;
			if (cp > this->ranges[range].last) {
				if (++range == this->nranges)
					goto out;
				cp = this->ranges[range].first;
			}
		}
		x0 = this->viewport_w * c0 / this->ncols;
		y1 = this->viewport_h * (r + 1) / this->nrows;
		for (c = c0; c < c1; c++, cp++, x0 = x1) {
			if (cp > this->ranges[range].last) {
				if (++range == this->nranges)
					goto out;
				cp = this->ranges[range].first;
			}
			x1 = this->viewport_w * (c + 1) / this->ncols;

			format_character((gunichar)cp, text);
			fit_text(this, text, &text_w, &text_h);
			cairo_move_to(g, (x0 + x1 - text_w) / 2, (y0 + y1 - text_h) / 2);
			pango_cairo_show_layout(g, this->layout);
		}
	}

out:
	cairo_destroy(g);
	return FALSE;
}


static int
update_cell_size(GtkWidget *widget, int ranges_changed)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	int text_w, text_h;
	int cell_w, cell_h;
	uint32_t nchars;
	int table_h;
	size_t i;

	int old_ncols = this->ncols;
	int old_cell_h = this->cell_h;

	pango_layout_set_text(this->layout, REFERENCE_CHARACTER, -1);

	pango_font_description_set_weight(this->font, PANGO_WEIGHT_NORMAL);
	pango_font_description_set_style(this->font, PANGO_STYLE_NORMAL);
	apply_font_size(this, this->font_size, &text_w, &text_h);
	pango_font_description_set_weight(this->font, this->font_weight);
	pango_font_description_set_style(this->font, this->font_style);

	cell_w = MAX(text_w, 1) * CELL_WIDTH_FACTOR;
	cell_h = MAX(text_h, 1) * CELL_HEIGHT_FACTOR;
	this->ncols = MAX(this->viewport_w / cell_w, 1);
	this->nrows = MAX(this->viewport_h / cell_h, 1);
	cell_w = MAX(this->viewport_w / this->ncols - 1, 1);
	cell_h = MAX(this->viewport_h / this->nrows - 1, 1);
	this->max_w = cell_w - CELL_X_CLEARANCE * 2;
	this->max_h = cell_h - CELL_Y_CLEARANCE * 2;
	this->max_w = this->max_w < 1 ? cell_w : this->max_w;
	this->max_h = this->max_h < 1 ? cell_h : this->max_h;
	this->cell_h = cell_h;

	if (old_ncols == this->ncols && old_cell_h == this->cell_h && !ranges_changed)
		return 0;

	nchars = (uint32_t)this->nranges;
	for (i = 0u; i < this->nranges; i++)
		nchars += this->ranges[i].last - this->ranges[i].first;
	this->nchars = nchars;
	table_h = (int)(nchars / (uint32_t)this->ncols);
	if (nchars % (uint32_t)this->ncols)
		table_h += 1;
	table_h = table_h > INT_MAX / this->cell_h ? INT_MAX : table_h * this->cell_h;
	if (this->table_h == table_h)
		return 0;
	this->table_h = table_h;
	return 1;
}


static void
gcmap_char_table_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	requisition->width = 1;
	requisition->height = this->table_h;
}

static gboolean
gcmap_char_table_button_press(GtkWidget *widget, GdkEventButton *event)
{
	(void) event;
	gtk_widget_grab_focus(widget);
	return FALSE;
}

static void
gcmap_char_table_init(CharTable *this)
{
	GtkWidget *widget = GTK_WIDGET(this);

	this->nrows = 1;
	this->ncols = 1;
	this->max_w = 1;
	this->max_h = 1;
	this->cell_h = 1;
	this->table_h = 1;
	this->nchars = 0u;
	this->viewport_w = 1;
	this->viewport_h = 1;

	GTK_WIDGET_SET_FLAGS(widget, GTK_CAN_FOCUS);
	gtk_widget_set_sensitive(widget, TRUE);

	gtk_widget_add_events(widget,
	                      GDK_BUTTON_PRESS_MASK |
	                      GDK_KEY_PRESS_MASK |
	                      GDK_FOCUS_CHANGE_MASK);

	g_signal_connect(widget, "button-press-event",
	                 G_CALLBACK(&gcmap_char_table_button_press), NULL);

	/* TODO scrolling with control held should change font size (attempt to keep hover glyph in place) */
}


static void
gcmap_char_table_dispose(GObject *object)
{
	CharTable *this = GCMAP_CHAR_TABLE(object);
	g_clear_object(&this->layout);
	G_OBJECT_CLASS(gcmap_char_table_parent_class)->dispose(object);
}


static void
gcmap_char_table_finalize(GObject *object)
{
	CharTable *this = GCMAP_CHAR_TABLE(object);
	pango_font_description_free(this->font);
	G_OBJECT_CLASS(gcmap_char_table_parent_class)->finalize(object);
}


static void
gcmap_char_table_class_init(CharTableClass *class)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(class);
	GObjectClass *object_class = G_OBJECT_CLASS(class);
	object_class->dispose = &gcmap_char_table_dispose;
	object_class->finalize = &gcmap_char_table_finalize;
	widget_class->expose_event = &gcmap_char_table_expose;
	widget_class->size_request = &gcmap_char_table_size_request;
}


void
gcmap_char_table_set_font_family(CharTable *this, const char *family)
{
	pango_font_description_set_family(this->font, family);
	pango_layout_set_font_description(this->layout, this->font);
	pango_layout_context_changed(this->layout);
	gtk_widget_queue_resize(GTK_WIDGET(this));
	gtk_widget_queue_draw(GTK_WIDGET(this));
}


void
gcmap_char_table_set_font_weight(CharTable *this, PangoWeight weight)
{
	if (this->font_weight == weight)
		return;
	this->font_weight = weight;
	pango_font_description_set_weight(this->font, this->font_weight);
	pango_layout_set_font_description(this->layout, this->font);
	pango_layout_context_changed(this->layout);
	gtk_widget_queue_draw(GTK_WIDGET(this));
}


void
gcmap_char_table_set_font_style(CharTable *this, PangoStyle style)
{
	if (this->font_style == style)
		return;
	this->font_style = style;
	pango_font_description_set_style(this->font, this->font_style);
	pango_layout_set_font_description(this->layout, this->font);
	pango_layout_context_changed(this->layout);
	gtk_widget_queue_draw(GTK_WIDGET(this));
}


void
gcmap_char_table_set_font_size(CharTable *this, int size)
{
	GtkWidget *widget;
	if (this->font_size == MAX(size, 1))
		return;
	this->font_size = MAX(size, 1);
	pango_font_description_set_size(this->font, this->font_size * PANGO_SCALE);
	pango_layout_set_font_description(this->layout, this->font);
	pango_layout_context_changed(this->layout);
	widget = GTK_WIDGET(this);
	gtk_widget_queue_resize(widget);
	gtk_widget_queue_draw(widget);
}


void
gcmap_char_table_set_ranges(CharTable *this, const struct libcmap_range *ranges, size_t nranges)
{
	GtkWidget *widget;
	if (this->ranges == ranges && this->nranges == nranges)
		return;
	this->ranges = ranges;
	this->nranges = nranges;
	widget = GTK_WIDGET(this);
	if (update_cell_size(widget, 1))
		gtk_widget_queue_resize(widget);
	gtk_widget_queue_draw(widget);
}


void
gcmap_char_table_viewport_updated(CharTable *this, int viewport_w, int viewport_h)
{
	this->viewport_w = viewport_w;
	this->viewport_h = viewport_h;
	update_cell_size(GTK_WIDGET(this), 0);
}


GtkWidget *
gcmap_char_table_new(const char *font_family, int font_size, PangoWeight font_weight, PangoStyle font_style)
{
	CharTable *this = g_object_new(GCMAP_TYPE_CHAR_TABLE, NULL);
	GtkWidget *widget = GTK_WIDGET(this);

	this->font_size = font_size;
	this->font_weight = font_weight;
	this->font_style = font_style;
	this->ranges = NULL;
	this->nranges = 0u;

	this->font = pango_font_description_new();
	if (!this->font)
		eprintf("pango_font_description_new:");
	pango_font_description_set_family(this->font, font_family);
	pango_font_description_set_size(this->font, this->font_size * PANGO_SCALE);
	pango_font_description_set_weight(this->font, this->font_weight);
	pango_font_description_set_style(this->font, this->font_style);

	this->layout = gtk_widget_create_pango_layout(widget, "");
	if (!this->layout)
		eprintf("gtk_widget_create_pango_layout:");

	return widget;
}
