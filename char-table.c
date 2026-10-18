/* See LICENSE file for copyright and license details. */
#include "common.h"

#define REFERENCE_CHARACTER  "X"
#define CELL_WIDTH_FACTOR    4
#define CELL_HEIGHT_FACTOR   2
#define CELL_X_CLEARANCE     2
#define CELL_Y_CLEARANCE     2
#define MAGNIFICATION_WIDTH  200
#define MAGNIFICATION_HEIGHT 200
#define DRAG_WIDTH           100
#define DRAG_HEIGHT          100
#define DRAG_X               -16
#define DRAG_Y               -16

#define TEXT_LIMIT 32u
#define IS_SHIFT_KEY(KEYVAL) ((KEYVAL) == GDK_KEY_Shift_L || (KEYVAL) == GDK_KEY_Shift_R)
#define SHIFT_KEY(KEYVAL) (1u << ((KEYVAL) == GDK_KEY_Shift_R))


struct cells {
	int r0, c0; /* inclusive */
	int r1, c1; /* exclusive */
	int r_off;
};


struct glyph_layout {
	double scale;
	double x;
	double y;
	double width;
	double height;

	/* XXX this should be calculated and displayed with lines */
	double baseline_y;
	double descender_y;
	double ascender_y;
	double x_height_y;
	double cap_height_y;
	double preadvance_x;
	double postadvance_x;
};


G_DEFINE_TYPE(CharTable, gcmap_char_table, GTK_TYPE_DRAWING_AREA)


static int
format_character(uint32_t cp, char *buf)
{
	int len = 0;

	switch (classify_codepoint(cp)) {
	case CP_COMBINING_MARK:
#if MARK_BASE
		len += g_unichar_to_utf8(MARK_BASE, &buf[len]);
#endif
		/* fall through */

	case CP_REGULAR:
	case CP_RESERVED:
		len += g_unichar_to_utf8((gunichar)cp, &buf[len]);
		break;

	case CP_CONTROL:
	case CP_SURROGATE:
	case CP_NONCHARACTER:
	case CP_OUT_OF_RANGE:
	default:
		break;
	}

	buf[len] = '\0';
	return len;
}


static void
apply_font_size(CharTable *this, double size, int *text_w_out, int *text_h_out)
{
	int size_int = (int)(size * PANGO_SCALE);
	pango_font_description_set_size(this->font, MAX(size_int, 1));
	pango_layout_set_font_description(this->layout, this->font);
	pango_layout_context_changed(this->layout);
	pango_layout_get_pixel_size(this->layout, text_w_out, text_h_out);
}


PURE static uint32_t
get_codepoint(CharTable *this, uint32_t index)
{
	size_t i = 0u;
	uint32_t count;
	while (index >= (count = (uint32_t)LIBCMAP_RANGE_SIZE(&this->ranges[i]))) {
		index -= count;
		if (++i == this->nranges)
			abort();
	}
	return this->ranges[i].first + index;
}


static int
find_codepoint_index(CharTable *this, uint32_t codepoint, uint32_t *index_out)
{
	size_t i = 0u;
	uint32_t index = 0u;
	while (i < this->nranges && codepoint > this->ranges[i].last) {
		index += (uint32_t)LIBCMAP_RANGE_SIZE(&this->ranges[i]);
		i += 1u;
	}
	if (i >= this->nranges || codepoint < this->ranges[i].first)
		return 0;
	*index_out = index + codepoint - this->ranges[i].first;
	return 1;
}


static void
execute_selection_callback(CharTable *this, uint32_t cell, int meta, int copy_to_text_field)
{
	uint32_t codepoint;
	if (!this->selection_callback)
		return;
	codepoint = get_codepoint(this, cell);
	if (copy_to_text_field)
		(*this->selection_callback)(cell, codepoint, COPY_TO_TEXTFIELD);
	else if (meta)
		(*this->selection_callback)(cell, codepoint, COPY_TO_SECONDARY);
	else
		(*this->selection_callback)(cell, codepoint, COPY_TO_PRIMARY);
}


static void
fit_text(CharTable *this, const char *text, int *text_w_out, int *text_h_out)
{
	double size, ok_size = 1.0;
	double xscale, yscale, scale;
	double next_size, steps[] = {1.0, 0.1, 0.01};
	size_t i;

	/* Set text to measure */
	pango_layout_set_text(this->layout, text, -1);

	/* Try desired size */
	apply_font_size(this, this->font_size, text_w_out, text_h_out);
	if (*text_w_out <= this->max_w && *text_h_out <= this->max_h)
		return;

	/* Too large? Then estimate largest smaller size that would fit */
	xscale = (double)this->max_w / (double)*text_w_out;
	yscale = (double)this->max_h / (double)*text_h_out;
	scale = MIN(xscale, yscale);
	scale = MIN(scale, 1) * this->font_size;
	size = MAX(scale, 1.0);

	/* Check and decrease by one until it fits */
	for (; size > 1.0; size -= 1.0) {
		apply_font_size(this, size, text_w_out, text_h_out);
		if (*text_w_out <= this->max_w && *text_h_out <= this->max_h) {
			ok_size = size;
			break;
		}
	}

	/* Maybe it was too small, then try larger */
	for (i = 0u; i < ELEMSOF(steps); i++) {
		while (*text_w_out < this->max_w && *text_h_out < this->max_h) {
			next_size = MIN(ok_size + steps[i], (double)this->font_size);
			if (next_size <= ok_size)
				break;
			apply_font_size(this, next_size, text_w_out, text_h_out);
			if (*text_w_out < this->max_w && *text_h_out < this->max_h)
				ok_size = next_size;
		}
	}

	/* Use the one that fits, 1 if none fit */
	apply_font_size(this, ok_size, text_w_out, text_h_out);
}


static void
get_exposed_cells(GtkWidget *widget, GdkEventExpose *event, struct cells *cells_out)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	GdkRectangle area = event->area;
	double y;

	cells_out->r0 = area.y * this->nrows / (this->viewport_h + 1);
	cells_out->c0 = area.x * this->ncols / (this->viewport_w + 1);
	cells_out->r1 = (area.y + area.height - 1) * this->nrows / (this->viewport_h + 1) + 1;
	cells_out->c1 = (area.x + area.width  - 1) * this->ncols / (this->viewport_w + 1) + 1;

	cells_out->r0 = MAX(cells_out->r0, 0);
	cells_out->c0 = MAX(cells_out->c0, 0);
	cells_out->r1 = MIN(cells_out->r1, cells_out->r0 + this->nrows);
	cells_out->c1 = MIN(cells_out->c1, cells_out->c0 + this->ncols);

	y = this->vadjustment ? round(this->vadjustment->value) : 0.0;
	cells_out->r_off = MAX((int)y, 0);
}


static gboolean
gcmap_char_table_expose(GtkWidget *widget, GdkEventExpose *event)
{
	struct cells cells;
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	GdkRectangle area = event->area;
	GtkStyle *style = gtk_widget_get_style(widget);
	cairo_t *g;
	char text[TEXT_LIMIT];
	int r, c, x0, x1, y0, y1, text_w, text_h;
	size_t skip, range, row_skip, max_jump, jump;
	uint32_t cp, index;
	GtkStateType selectedState = gtk_widget_has_focus(widget) ? GTK_STATE_SELECTED : GTK_STATE_ACTIVE;

	get_exposed_cells(widget, event, &cells);

	g = gdk_cairo_create(widget->window);
	if (!g)
		eprintf("gdk_cairo_create:");

	/* Configure drawing area */
	gdk_cairo_rectangle(g, &area);
	cairo_clip(g);

	/* Draw background */
	set_source_colour(g, &style->base[GTK_STATE_NORMAL]);
	cairo_paint(g);

	r = cells.r0;
	c = cells.c0;
	if (this->nranges == 0u)
		goto out;

	/* Draw grid lines */
	set_source_colour_blend(g, &style->base[GTK_STATE_NORMAL], 0.05, &style->text[GTK_STATE_NORMAL]);
	cairo_set_line_width(g, 1.0);
	/* horizontal */
	y0 = (this->viewport_h + 1) * (cells.r0 + 1) / this->nrows;
	x0 = (this->viewport_w + 1) * cells.c0 / this->ncols;
	x1 = (this->viewport_w + 1) * cells.c1 / this->ncols;
	for (r = cells.r0 + (area.y > 0); r < cells.r1; r++, y0 = y1) {
		y1 = (this->viewport_h + 1) * (r + 1) / this->nrows;
		cairo_move_to(g, x0, y0 - 0.5);
		cairo_line_to(g, x1, y0 - 0.5);
		cairo_stroke(g);
	}
	/* vertical */
	x0 = (this->viewport_w + 1) * (cells.c0 + 1) / this->ncols;
	y0 = (this->viewport_h + 1) * cells.r0 / this->nrows;
	y1 = (this->viewport_h + 1) * cells.r1 / this->nrows;
	for (c = cells.c0 + (area.x > 0); c < cells.c1; c++, x0 = x1) {
		x1 = (this->viewport_w + 1) * (c + 1) / this->ncols;
		cairo_move_to(g, x0 - 0.5, y0);
		cairo_line_to(g, x0 - 0.5, y1);
		cairo_stroke(g);
	}

	/* Draw gryphs */
	set_source_colour(g, &style->text[GTK_STATE_NORMAL]);
	y0 = (this->viewport_h + 1) * cells.r0 / this->nrows;
	range = 0u;
	cp = this->ranges[range].first;
	skip = (size_t)cells.r0 + (size_t)cells.r_off;
	skip = skip * (size_t)this->ncols + (size_t)cells.c0;
	index = (uint32_t)skip;
	row_skip = (size_t)this->ncols - (size_t)(cells.c1 - cells.c0);
	for (r = cells.r0; r < cells.r1; r++, y0 = y1, skip = row_skip, index += (uint32_t)row_skip) {
		while (skip) {
			max_jump = (size_t)(this->ranges[range].last - cp + 1u);
			jump = MIN(skip, max_jump);
			cp += (uint32_t)jump;
			skip -= jump;
			if (cp > this->ranges[range].last) {
				if (++range == this->nranges)
					goto out;
				cp = this->ranges[range].first;
			}
		}
		x0 = (this->viewport_w + 1) * cells.c0 / this->ncols;
		y1 = (this->viewport_h + 1) * (r + 1) / this->nrows;
		for (c = cells.c0; c < cells.c1; c++, cp++, index++, x0 = x1) {
			if (cp > this->ranges[range].last) {
				if (++range == this->nranges)
					goto out;
				cp = this->ranges[range].first;
			}
			x1 = (this->viewport_w + 1) * (c + 1) / this->ncols;

			if (index == this->selected) {
				set_source_colour(g, &style->base[selectedState]);
				cairo_rectangle(g, x0, y0, x1 - x0 - 1.0, y1 - y0 - 1.0);
				cairo_fill(g);
				set_source_colour(g, &style->text[selectedState]);
			}

			format_character(cp, text);
			fit_text(this, text, &text_w, &text_h);
			cairo_move_to(g, (x0 + x1 - text_w) / 2, (y0 + y1 - text_h) / 2);
			pango_cairo_show_layout(g, this->layout);

			if (index == this->selected)
				set_source_colour(g, &style->text[GTK_STATE_ACTIVE]);
		}
	}

out:
	/* Grey out unused area */
	set_source_colour_blend(g, &style->base[GTK_STATE_NORMAL], 0.05, &style->text[GTK_STATE_NORMAL]);
	y0 = (this->viewport_h + 1) * r / this->nrows;
	x0 = (this->viewport_w + 1) * c / this->ncols;
	y1 = (this->viewport_h + 1) * (r + 1) / this->nrows;
	cairo_rectangle(g, x0, y0, (double)this->viewport_w - x0, y1 - y0);
	cairo_fill(g);
	cairo_rectangle(g, 0.0, y1, (double)this->viewport_w, (double)this->viewport_h - y1);
	cairo_fill(g);

	cairo_destroy(g);
	return FALSE;
}


static void
get_selected_cell_screen_rectangle(CharTable *this, int *x_out, int *y_out, int *w_out, int *h_out, int *c_out, int *r_out)
{
	double yadj = this->vadjustment ? round(this->vadjustment->value) : 0.0;
	int yadj_int = (int)yadj;
	int row = (int)(this->selected / (uint32_t)this->ncols);
	int col = (int)(this->selected % (uint32_t)this->ncols);
	row -= MAX(yadj_int, 0);
	*x_out = (this->viewport_w + 1) * (col + 0) / this->ncols;
	*y_out = (this->viewport_h + 1) * (row + 0) / this->nrows;
	*w_out = (this->viewport_w + 1) * (col + 1) / this->ncols - *x_out - 1;
	*h_out = (this->viewport_h + 1) * (row + 1) / this->nrows - *y_out - 1;
	*r_out = row;
	*c_out = col;
}


static int
fit_magnification(CharTable *this, const char *text, int text_len, int width, int height, struct glyph_layout *layout_out)
{
	const int max_w = width  - CELL_X_CLEARANCE * 2;
	const int max_h = height - CELL_Y_CLEARANCE * 2;
	int font_size = this->font_size * PANGO_SCALE;
	PangoFontDescription *font;
	double xscale, yscale;
	PangoRectangle ink, logical;
	int x1i, x2i, y1i, y2i;
	int x1l, x2l, y1l, y2l;
	int x1, x2, y1, y2;

	font = pango_font_description_copy(this->font);
	if (!font) {
		weprintf("pango_font_description_copy:");
		return -1;
	}

	pango_font_description_set_size(font, MAX(font_size, 1));
	pango_layout_set_text(this->magnification_layout, text, text_len);
	pango_layout_set_font_description(this->magnification_layout, font);
	pango_layout_context_changed(this->magnification_layout);
	pango_font_description_free(font);
	pango_layout_get_pixel_extents(this->magnification_layout, &ink, &logical);

	x1i = MAX(ink.x, 0);
	y1i = MAX(ink.y, 0);
	x1l = MAX(logical.x, 0);
	y1l = MAX(logical.y, 0);
	x2i = x1i + ink.width;
	y2i = y1i + ink.height;
	x2l = x1l + logical.width;
	y2l = y1l + logical.height;
	x1 = MIN(x1i, x1l);
	y1 = MIN(y1i, y1l);
	x2 = MAX(x2i, x2l);
	y2 = MAX(y2i, y2l);

	layout_out->x = (double)MAX(x1 - ink.x, 0);
	layout_out->y = (double)MAX(y1 - ink.y, 0);
	layout_out->width  = (double)(x2 - x1);
	layout_out->height = (double)(y2 - y1);

	if (layout_out->width > 0.0 && layout_out->height > 0.0) {
		xscale = (double)max_w / (double)layout_out->width;
		yscale = (double)max_h / (double)layout_out->height;
		layout_out->scale = MIN(xscale, yscale);
	} else {
		layout_out->scale = 1.0;
	}

	layout_out->x *= layout_out->scale;
	layout_out->y *= layout_out->scale;
	layout_out->width  *= layout_out->scale;
	layout_out->height *= layout_out->scale;
	return 0;
}


static void
draw_magnification(CharTable *this, cairo_t *g, int width, int height)
{
	GtkStyle *style = gtk_widget_get_style(GTK_WIDGET(this));
	char text[TEXT_LIMIT];
	int text_len;
	struct glyph_layout layout;
	const double w = (double)width;
	const double h = (double)height;

	set_source_colour(g, &style->base[GTK_STATE_NORMAL]);
	cairo_paint(g);
	if (!this->nchars)
		return;

	text_len = format_character(get_codepoint(this, this->selected), text);
	if (fit_magnification(this, text, text_len, width, height, &layout) < 0)
		return;

	set_source_colour_blend(g, &style->base[GTK_STATE_NORMAL], 0.05, &style->text[GTK_STATE_NORMAL]);
	cairo_set_line_width(g, 1.0);
	cairo_move_to(g, 0.0, 0.5);
	cairo_line_to(g, w,   0.5);
	cairo_stroke(g);
	cairo_move_to(g, 0.0, h - 0.5);
	cairo_line_to(g, w,   h - 0.5);
	cairo_stroke(g);
	cairo_move_to(g, 0.5, 0.0);
	cairo_line_to(g, 0.5, h);
	cairo_stroke(g);
	cairo_move_to(g, w - 0.5, 0.0);
	cairo_line_to(g, w - 0.5, h);
	cairo_stroke(g);

	set_source_colour(g, &style->text[GTK_STATE_NORMAL]);
	cairo_save(g);
	cairo_translate(g, 0.5 * (w - layout.width)  + layout.x,
	                   0.5 * (h - layout.height) + layout.y);
	cairo_scale(g, layout.scale, layout.scale);
	pango_cairo_show_layout(g, this->magnification_layout);
	cairo_restore(g);
}


static gboolean
gcmap_char_table_magnification_expose(GtkWidget *widget, GdkEventExpose *event, void *user_data)
{
	CharTable *this = user_data;
	cairo_t *g = gdk_cairo_create(widget->window);
	if (!g) {
		weprintf("gdk_cairo_create:");
		return FALSE;
	}
	gdk_cairo_rectangle(g, &event->area);
	cairo_clip(g);
	draw_magnification(this, g, MAGNIFICATION_WIDTH, MAGNIFICATION_HEIGHT);
	cairo_destroy(g);
	return FALSE;
}


static void
gcmap_char_table_update_magnification(CharTable *this)
{
	GtkWidget *widget = GTK_WIDGET(this);
	GdkRectangle monitor;
	GdkScreen *screen;
	int monitor_num, x, y, cw, ch;
	int cx1, cx2, cy1, cy2, col, row;
	int mx1, mx2, my1, my2;

	if (!this->magnification_window ||
	    !(this->shift_keys || this->right_button_down) ||
	    !this->nchars || !GTK_WIDGET_REALIZED(widget)) {
		if (this->magnification_window)
			gtk_widget_hide(this->magnification_window);
		return;
	}

	get_selected_cell_screen_rectangle(this, &cx1, &cy1, &cw, &ch, &col, &row);
	gdk_window_get_origin(widget->window, &x, &y);
	cx1 += x;
	cy1 += y;
	cx2 = cx1 + cw;
	cy2 = cy1 + ch;

	x = cx1 + cw / 2;
	y = cy1 + ch / 2;
	screen = gtk_widget_get_screen(widget);
	gtk_window_set_screen(GTK_WINDOW(this->magnification_window), screen);
	monitor_num = gdk_screen_get_monitor_at_point(screen, x, y);
	gdk_screen_get_monitor_geometry(screen, monitor_num, &monitor);
	mx1 = monitor.x;
	my1 = monitor.y;
	mx2 = mx1 + monitor.width;
	my2 = my1 + monitor.height;

	mx2 -= MAGNIFICATION_WIDTH;
	my2 -= MAGNIFICATION_HEIGHT;

	cx1 -= 1;
	x = cx1 > mx2 ? cx2 - MAGNIFICATION_WIDTH  : cx1;
	y = cy2 > my2 ? cy1 - MAGNIFICATION_HEIGHT : cy2;
	x = MIN(x, mx2);
	y = MIN(y, my2);
	x = MAX(x, mx1);
	y = MAX(y, my1);

	gtk_window_move(GTK_WINDOW(this->magnification_window), x, y);
	gtk_widget_show_all(this->magnification_window);
	gtk_widget_queue_draw(this->magnification);
}


static void
update_cell_size(GtkWidget *widget, int ranges_changed)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	int text_w, text_h, cell_w, cell_h;
	uint32_t nchars;
	size_t i;

	pango_layout_set_text(this->layout, REFERENCE_CHARACTER, -1);

	pango_font_description_set_weight(this->font, PANGO_WEIGHT_NORMAL);
	pango_font_description_set_style(this->font, PANGO_STYLE_NORMAL);
	apply_font_size(this, this->font_size, &text_w, &text_h);
	pango_font_description_set_weight(this->font, this->font_weight);
	pango_font_description_set_style(this->font, this->font_style);

	cell_w = MAX(text_w, 1) * CELL_WIDTH_FACTOR;
	cell_h = MAX(text_h, 1) * CELL_HEIGHT_FACTOR;
	this->ncols = (this->viewport_w + 1) / (cell_w + 1);
	this->nrows = (this->viewport_h + 1) / (cell_h + 1);
	this->ncols = MAX(this->ncols, 1);
	this->nrows = MAX(this->nrows, 1);

	if (this->snap_cols_to_2pow)
		while (this->ncols & (this->ncols - 1))
			this->ncols &= this->ncols - 1;
	if (this->snap_rows_to_2pow)
		while (this->nrows & (this->nrows - 1))
			this->nrows &= this->nrows - 1;

	cell_w = (this->viewport_w + 1) / this->ncols - 1;
	cell_h = (this->viewport_h + 1) / this->nrows - 1;
	cell_w = MAX(cell_w, 1);
	cell_h = MAX(cell_h, 1);
	this->max_w = cell_w - CELL_X_CLEARANCE * 2;
	this->max_h = cell_h - CELL_Y_CLEARANCE * 2;
	this->max_w = this->max_w < 1 ? cell_w : this->max_w;
	this->max_h = this->max_h < 1 ? cell_h : this->max_h;
	this->cell_h = cell_h;

	if (ranges_changed) {
		nchars = 0u;
		for (i = 0u; i < this->nranges; i++)
			nchars += (uint32_t)LIBCMAP_RANGE_SIZE(&this->ranges[i]);
		this->nchars = nchars;
	}
}


static void
gcmap_char_table_scroll_selected_cell_into_view(CharTable *this)
{
	int selected_row, viewport_row, yadj_int;
	double yadj;

	if (!this->vadjustment || !this->nchars)
		return;

	yadj = round(this->vadjustment->value);
	yadj_int = (int)yadj;

	selected_row = (int)(this->selected / (uint32_t)this->ncols);
	viewport_row = MAX(yadj_int, 0);

	if (selected_row < viewport_row)
		gtk_adjustment_set_value(this->vadjustment, (double)selected_row);
	else if (selected_row >= viewport_row + this->nrows)
		gtk_adjustment_set_value(this->vadjustment, (double)(selected_row - this->nrows + 1));
}


static void
update_content_size(CharTable *this)
{
	GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(this));
	int nrows;
	if (parent && LOGICAL_IS_SCROLL_PANE(parent)) {
		nrows = (int)(this->nchars / (uint32_t)this->ncols);
		nrows += this->nchars % (uint32_t)this->ncols > 0u;

		if (this->vadjustment_value_changed_id)
			g_signal_handler_block(this->vadjustment, this->vadjustment_value_changed_id);

		logical_scroll_pane_set_content_size(LOGICAL_SCROLL_PANE(parent), this->ncols, nrows);
		logical_scroll_pane_set_viewport_size(LOGICAL_SCROLL_PANE(parent), this->ncols, this->nrows);

		if (this->vadjustment_value_changed_id)
			g_signal_handler_unblock(this->vadjustment, this->vadjustment_value_changed_id);
		gcmap_char_table_scroll_selected_cell_into_view(this);
		gcmap_char_table_update_magnification(this);
	}
}


static void
gcmap_char_table_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
	(void) widget;
	requisition->width = 1;
	requisition->height = 1;
}


static void
gcmap_char_table_drag_begin(GtkWidget *widget, GdkDragContext *context, void *user_data)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	GdkPixmap *pixmap;
	cairo_t *g;
	uint32_t codepoint;

	(void) user_data;

	if (!this->nchars)
		return;

	codepoint = get_codepoint(this, this->selected);
	if (!is_valid_codepoint(codepoint))
		return;

	pixmap = gdk_pixmap_new(widget->window, DRAG_WIDTH, DRAG_HEIGHT, -1);
	if (!pixmap)
		return;

	g = gdk_cairo_create(pixmap);
	if (!g) {
		g_object_unref(pixmap);
		return;
	}
	draw_magnification(this, g, DRAG_WIDTH, DRAG_HEIGHT);
	cairo_destroy(g);

	gtk_drag_set_icon_pixmap(context, gtk_widget_get_colormap(widget), pixmap, NULL, DRAG_X, DRAG_Y);
	g_object_unref(pixmap);

	this->shift_keys = 0u;
	this->right_button_down = 0;
	gcmap_char_table_update_magnification(this);
}


static int
select_clicked_character(GtkWidget *widget, int meta, unsigned click_count, double x, double y)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	double yadj = this->vadjustment ? round(this->vadjustment->value) : 0.0;
	int r0 = (int)yadj;
	int r = (int)y * this->nrows / (this->viewport_h + 1);
	int c = (int)x * this->ncols / (this->viewport_w + 1);
	uint32_t selected;
	r = MIN(MAX(0, r), this->nrows - 1);
	c = MIN(MAX(0, c), this->ncols - 1);
	selected = ((uint32_t)r + (uint32_t)r0) * (uint32_t)this->ncols + (uint32_t)c;
	if (selected >= this->nchars) {
		this->multiclick_count = 0u;
		return 0;
	} else if (selected != this->selected) {
		click_count = 1u;
	clicked:
		this->selected = selected;
		execute_selection_callback(this, selected, meta, click_count >= 2u);
		this->multiclick_count = click_count == 1u ? 1u : 0u;
		gcmap_char_table_update_magnification(this);
		gtk_widget_queue_draw(GTK_WIDGET(this)); /* XXX just redraw the affected cells */
	} else if (click_count > this->multiclick_count) {
		goto clicked;
	} else {
		this->multiclick_count = 0u;
	}
	return 1;
}


static gboolean
gcmap_char_table_button_press(GtkWidget *widget, GdkEventButton *event, void *user_data)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	unsigned click_count;
	int meta, is_a_cell;
	(void) user_data;
	switch (event->button) {
	case 3:
		this->right_button_down = 1;
		/* fall through */
	case 1:
		gtk_widget_grab_focus(widget);
		switch ((unsigned)event->type) {
		default:
		case GDK_BUTTON_PRESS:   click_count = 1u;  break;
		case GDK_2BUTTON_PRESS:  click_count = 2u;  break;
		case GDK_3BUTTON_PRESS:  click_count = 3u;  break;
		}
		meta = !!(event->state & GDK_MOD1_MASK);
		is_a_cell = select_clicked_character(widget, meta, click_count, event->x, event->y);
		gcmap_char_table_update_magnification(this);
		if (is_a_cell && event->button == 1 && event->type == GDK_BUTTON_PRESS) {
			this->drag_x = (int)event->x;
			this->drag_y = (int)event->y;
			this->drag_stage = 1;
		}
		return TRUE;
	default:
		this->multiclick_count = 0u;
		return event->button != 2;
	}
}


static gboolean
gcmap_char_table_button_release(GtkWidget *widget, GdkEventButton *event, void *user_data)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	(void) user_data;
	if (event->button == 1)
		this->drag_stage = 0;
	if (event->button != 3)
		return FALSE;
	this->right_button_down = 0;
	gcmap_char_table_update_magnification(this);
	return TRUE;
}


static gboolean
gcmap_char_table_motion_notify(GtkWidget *widget, GdkEventMotion *event, void *user_data)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	uint32_t codepoint;
	GdkDragContext *drag_ctx;

	this->pointer_cell = UINT32_MAX;

	if (this->drag_stage == 1 && (event->state & GDK_BUTTON1_MASK)) {
		if (!gtk_drag_check_threshold(widget, this->drag_x, this->drag_y, (int)event->x, (int)event->y))
			goto not_starting_drag;
		this->drag_stage = 2;

		if (!this->nchars)
			goto cannot_drag;

		codepoint = get_codepoint(this, this->selected);
		if (this->selection_callback)
			(*this->selection_callback)(this->selected, codepoint, DRAG_ANNOUNCEMENT);
		if (!is_valid_codepoint(codepoint))
			goto cannot_drag;

		drag_ctx = gtk_drag_begin(widget, this->drag_targets, GDK_ACTION_COPY, 1, (GdkEvent *)event);
		if (!drag_ctx)
			goto cannot_drag;

		gcmap_char_table_drag_begin(widget, drag_ctx, user_data);
		return TRUE;

	cannot_drag:
		this->drag_stage = 0;
	}

not_starting_drag:
	if (!this->right_button_down)
		return FALSE;
	if (!(event->state & GDK_BUTTON3_MASK)) {
		this->right_button_down = 0;
		gcmap_char_table_update_magnification(this);
		return FALSE;
	}
	select_clicked_character(widget, !!(event->state & GDK_MOD1_MASK), 1u, event->x, event->y);
	return TRUE;
}


static gboolean
gcmap_char_table_key_press(GtkWidget *widget, GdkEventKey *event, void *user_data)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	int meta;

	(void) user_data;

	if (IS_SHIFT_KEY(event->keyval)) {
		this->shift_keys |= SHIFT_KEY(event->keyval);
		gcmap_char_table_update_magnification(this);
		return TRUE;
	}

	if (event->state & GDK_CONTROL_MASK)
		return FALSE;

	meta = !!(event->state & GDK_MOD1_MASK);

	switch (event->keyval) {
	case GDK_KEY_Left:   case GDK_KEY_KP_Left:   gcmap_char_table_adjust_cell_selection(this, -1, 0, meta);            break;
	case GDK_KEY_Right:  case GDK_KEY_KP_Right:  gcmap_char_table_adjust_cell_selection(this, +1, 0, meta);            break;
	case GDK_KEY_Up:     case GDK_KEY_KP_Up:     gcmap_char_table_adjust_cell_selection(this, -this->ncols, 0, meta);  break;
	case GDK_KEY_Down:   case GDK_KEY_KP_Down:   gcmap_char_table_adjust_cell_selection(this, +this->ncols, 0, meta);  break;
	case GDK_KEY_Home:   case GDK_KEY_KP_Home:   gcmap_char_table_select_cell(this, 0, 0, meta);                       break;
	case GDK_KEY_End:    case GDK_KEY_KP_End:    gcmap_char_table_select_cell(this, INT32_MAX, 1, meta);               break;
	case GDK_KEY_Return: case GDK_KEY_KP_Enter:
	case GDK_KEY_Begin:  case GDK_KEY_KP_Begin:
	case GDK_KEY_space:
		execute_selection_callback(this, this->selected, meta, 1);
		break;
	default:
		return FALSE;
	}

	return TRUE;
}


static gboolean
gcmap_char_table_key_release(GtkWidget *widget, GdkEventKey *event, void *user_data)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	(void) user_data;
	if (IS_SHIFT_KEY(event->keyval)) {
		this->shift_keys &= ~SHIFT_KEY(event->keyval);
		gcmap_char_table_update_magnification(this);
		return TRUE;
	}
	return FALSE;
}


static gboolean
gcmap_char_table_focus_out(GtkWidget *widget, GdkEventFocus *event, void *user_data)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	(void) event;
	(void) user_data;
	this->shift_keys = 0u;
	this->right_button_down = 0;
	gcmap_char_table_update_magnification(this);
	return FALSE;
}


static gboolean
gcmap_char_table_focus_in(GtkWidget *widget, GdkEventFocus *event, void *user_data)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	GdkModifierType modifiers;
	(void) event;
	(void) user_data;
	gdk_window_get_pointer(widget->window, NULL, NULL, &modifiers);
	this->shift_keys = modifiers & GDK_SHIFT_MASK ? 1u : 0u;
	gcmap_char_table_update_magnification(this);
	return FALSE;
}


static void
gcmap_char_table_vadjustment_value_changed(GtkAdjustment *adjustment, CharTable *this)
{
	uint32_t first, last, column, cell;
	double yadj;
	int row, yadj_int;

	if (!this->nchars)
		return;

	yadj = round(adjustment->value);
	yadj_int = (int)yadj;

	row = MAX(yadj_int, 0);
	first = (uint32_t)row * (uint32_t)this->ncols;
	last = first + (uint32_t)this->nrows * (uint32_t)this->ncols - 1u;
	last = MIN(last, this->nchars - 1u);
	column = this->selected % (uint32_t)this->ncols;

	if (this->selected < first) {
		cell = first + column;
	} else if (this->selected > last) {
		cell = last - (last % (uint32_t)this->ncols);
		cell += column;
	} else {
		gcmap_char_table_update_magnification(this);
		gtk_widget_queue_draw(GTK_WIDGET(this));
		return;
	}

	cell = MIN(cell, last);
	gcmap_char_table_select_cell(this, (int32_t)cell, 1, 0);
}


static void
gcmap_char_table_parent_set(GtkWidget *widget, GtkWidget *old_parent, void *user_data)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	GtkWidget *parent = gtk_widget_get_parent(widget);

	(void) old_parent;
	(void) user_data;

	if (this->vadjustment_value_changed_id) {
		g_signal_handler_disconnect(this->vadjustment, this->vadjustment_value_changed_id);
		this->vadjustment_value_changed_id = 0u;
	}
	this->vadjustment = NULL;

	if (!parent || !LOGICAL_IS_SCROLL_PANE(parent))
		return;

	this->vadjustment = logical_scroll_pane_get_vadjustment(LOGICAL_SCROLL_PANE(parent));
	this->vadjustment_value_changed_id = g_signal_connect(this->vadjustment, "value-changed",
	                                                      G_CALLBACK(&gcmap_char_table_vadjustment_value_changed), this);
}


static void
gcmap_char_table_drag_data_get(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data,
                               unsigned int info, unsigned int time, void *user_data)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	char text[TEXT_LIMIT];
	uint32_t codepoint;
	int text_len;
	(void) context;
	(void) info;
	(void) time;
	(void) user_data;
	if (!this->nchars)
		return;
	codepoint = get_codepoint(this, this->selected);
	if (!is_valid_codepoint(codepoint))
		return;
	text_len = g_unichar_to_utf8((gunichar)codepoint, text);
	gtk_selection_data_set_text(selection_data, text, text_len);
}



static void
gcmap_char_table_init(CharTable *this)
{
	static char utf8_string[] = "UTF8_STRING";
	GtkWidget *widget = GTK_WIDGET(this);
	GtkTargetEntry drag_target = {utf8_string, 0, 0};

	this->nrows = 1;
	this->ncols = 1;
	this->max_w = 1;
	this->max_h = 1;
	this->cell_h = 1;
	this->nchars = 0u;
	this->selected = 0u;
	this->ranges = NULL;
	this->nranges = 0u;
	this->magnification_layout = NULL;
	this->magnification_window = NULL;
	this->magnification = NULL;
	this->vadjustment = NULL;
	this->vadjustment_value_changed_id = 0u;
	this->shift_keys = 0u;
	this->right_button_down = 0;
	this->pointer_cell = UINT32_MAX;
	this->viewport_w = 1;
	this->viewport_h = 1;
	this->snap_cols_to_2pow = 0;
	this->snap_rows_to_2pow = 0;
	this->drag_x = 0;
	this->drag_y = 0;
	this->drag_stage = 0;
	this->multiclick_count = 0u;
	this->selection_callback = NULL;

	GTK_WIDGET_SET_FLAGS(widget, GTK_CAN_FOCUS);
	gtk_widget_set_sensitive(widget, TRUE);

	gtk_widget_add_events(widget,
	                      GDK_BUTTON_PRESS_MASK |
	                      GDK_BUTTON_RELEASE_MASK |
	                      GDK_POINTER_MOTION_MASK |
	                      GDK_BUTTON3_MOTION_MASK |
	                      GDK_KEY_PRESS_MASK |
	                      GDK_KEY_RELEASE_MASK |
	                      GDK_FOCUS_CHANGE_MASK);

	this->drag_targets = gtk_target_list_new(&drag_target, 1u);
	if (!this->drag_targets)
		eprintf("gtk_target_list_new:");

	g_signal_connect(widget, "drag-data-get", G_CALLBACK(&gcmap_char_table_drag_data_get), NULL);
	g_signal_connect(widget, "button-press-event", G_CALLBACK(&gcmap_char_table_button_press), NULL);
	g_signal_connect(widget, "button-release-event", G_CALLBACK(&gcmap_char_table_button_release), NULL);
	g_signal_connect(widget, "motion-notify-event", G_CALLBACK(&gcmap_char_table_motion_notify), NULL);
	g_signal_connect(widget, "key-press-event", G_CALLBACK(&gcmap_char_table_key_press), NULL);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(&gcmap_char_table_key_release), NULL);
	g_signal_connect(widget, "focus-in-event", G_CALLBACK(&gcmap_char_table_focus_in), NULL);
	g_signal_connect(widget, "focus-out-event", G_CALLBACK(&gcmap_char_table_focus_out), NULL);
	g_signal_connect(widget, "parent-set", G_CALLBACK(&gcmap_char_table_parent_set), NULL);
}


static void
gcmap_char_table_dispose(GObject *object)
{
	CharTable *this = GCMAP_CHAR_TABLE(object);
	if (this->magnification_window) {
		gtk_widget_destroy(this->magnification_window);
		this->magnification_window = NULL;
		this->magnification = NULL;
	}
	g_clear_object(&this->magnification_layout);
	g_clear_object(&this->layout);
	gtk_target_list_unref(this->drag_targets);
	this->drag_targets = NULL;
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
	update_cell_size(GTK_WIDGET(this), 0);
	update_content_size(this);
	gcmap_char_table_update_magnification(this);
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
	gcmap_char_table_update_magnification(this);
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
	gcmap_char_table_update_magnification(this);
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
	update_cell_size(widget, 0);
	update_content_size(this);
	gcmap_char_table_update_magnification(this);
	gtk_widget_queue_draw(widget);
}


void
gcmap_char_table_set_font_size_at_pointer(CharTable *this, int size, double x, double y)
{
	int row, col, row0, px, py, prow, crow;
	double value, upper, yadj;
	uint32_t cell;

	if (!this->nchars || !this->vadjustment || this->font_size == MAX(size, 1)) {
		gcmap_char_table_set_font_size(this, size);
		return;
	}

	px = (int)x;
	py = (int)y;

	if (this->pointer_cell < this->nchars) {
		cell = this->pointer_cell;
	} else {
		yadj = round(MAX(this->vadjustment->value, 0.0));
		row0 = (int)yadj;

		row = py * this->nrows / (this->viewport_h + 1);
		col = px * this->ncols / (this->viewport_w + 1);
		row = MAX(row, 0);
		col = MAX(col, 0);
		row = MIN(row, this->nrows - 1);
		col = MIN(col, this->ncols - 1);

		cell = ((uint32_t)row + (uint32_t)row0) * (uint32_t)this->ncols + (uint32_t)col;
		cell = MIN(cell, this->nchars - 1u);
		this->pointer_cell = cell;
	}

	gcmap_char_table_set_font_size(this, size);

	yadj = round(MAX(this->vadjustment->value, 0.0));
	row0 = (int)yadj;
	crow = (int)(cell / (uint32_t)this->ncols) - row0;
	crow = MAX(crow, 0);
	prow = py * this->nrows / (this->viewport_h + 1);
	prow = MAX(prow, 0);
	prow = MIN(prow, this->nrows - 1);
	row0 = crow - prow + row0;

	row = (int)(this->selected / (uint32_t)this->ncols);
	if (row < row0) {
		this->selected = (uint32_t)row0 * (uint32_t)this->ncols;
	} else if (row >= row0 + this->nrows) {
		this->selected = (uint32_t)row0 + (uint32_t)this->nrows;
		this->selected *= (uint32_t)this->ncols;
		if (this->selected)
			this->selected -= 1u;
	}
	this->selected = MIN(this->selected, this->nchars);

	value = (double)row0;
	upper = this->vadjustment->upper - this->vadjustment->page_size;
	value = MAX(value, this->vadjustment->lower);
	value = MIN(value, upper);
	gtk_adjustment_set_value(this->vadjustment, value);
}


void
gcmap_char_table_set_snap_cols_to_2pow(CharTable *this, int active)
{
	GtkWidget *widget = GTK_WIDGET(this);
	this->snap_cols_to_2pow = active;
	update_cell_size(widget, 0);
	update_content_size(this);
	gtk_widget_queue_draw(widget);
}


void
gcmap_char_table_set_snap_rows_to_2pow(CharTable *this, int active)
{
	GtkWidget *widget = GTK_WIDGET(this);
	this->snap_rows_to_2pow = active;
	update_cell_size(widget, 0);
	update_content_size(this);
	gtk_widget_queue_draw(widget);
}


void
gcmap_char_table_set_ranges(CharTable *this, const struct libcmap_range *ranges, size_t nranges)
{
	GtkWidget *widget;
	uint32_t codepoint, selected;

	if (this->ranges == ranges && this->nranges == nranges)
		return;

	codepoint = this->nchars ? get_codepoint(this, this->selected) : 0u;
	this->ranges = ranges;
	this->nranges = nranges;
	this->selected = 0u;
	if (this->nchars && find_codepoint_index(this, codepoint, &selected))
		this->selected = selected;

	widget = GTK_WIDGET(this);
	update_cell_size(widget, 1);
	update_content_size(this);
	execute_selection_callback(this, this->selected, 0, 0);
	gcmap_char_table_update_magnification(this);
	gtk_widget_queue_draw(widget);
}


void
gcmap_char_table_viewport_updated(CharTable *this, int viewport_w, int viewport_h)
{
	this->viewport_w = viewport_w;
	this->viewport_h = viewport_h;
	update_cell_size(GTK_WIDGET(this), 0);
	update_content_size(this);
	gcmap_char_table_update_magnification(this);
}


GtkWidget *
gcmap_char_table_new(const char *font_family, int font_size, PangoWeight font_weight, PangoStyle font_style)
{
	CharTable *this = g_object_new(GCMAP_TYPE_CHAR_TABLE, NULL);
	GtkWidget *widget = GTK_WIDGET(this);

	this->font_size = font_size;
	this->font_weight = font_weight;
	this->font_style = font_style;

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
	this->magnification_layout = gtk_widget_create_pango_layout(widget, "");
	if (!this->magnification_layout)
		eprintf("gtk_widget_create_pango_layout:");

	this->magnification_window = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_window_set_wmclass(GTK_WINDOW(this->magnification_window), "gcmap", "gcmap");
	gtk_window_set_role(GTK_WINDOW(this->magnification_window), "magnification");
	gtk_window_set_decorated(GTK_WINDOW(this->magnification_window), FALSE);
	gtk_window_set_resizable(GTK_WINDOW(this->magnification_window), FALSE);
	gtk_window_set_type_hint(GTK_WINDOW(this->magnification_window), GDK_WINDOW_TYPE_HINT_TOOLTIP);
	this->magnification = gtk_drawing_area_new();
	gtk_widget_set_size_request(this->magnification, MAGNIFICATION_WIDTH, MAGNIFICATION_HEIGHT);
	gtk_container_add(GTK_CONTAINER(this->magnification_window), this->magnification);
	g_signal_connect(this->magnification, "expose-event", G_CALLBACK(&gcmap_char_table_magnification_expose), this);

	return widget;
}


void
gcmap_char_table_select_cell(CharTable *this, int32_t selected, int clamp, int meta)
{
	GtkWidget *widget = GTK_WIDGET(this);
	double yadj = this->vadjustment ? round(this->vadjustment->value) : 0.0;
	int r0 = MAX((int)yadj, 0);
	int r = r0;
	int32_t first, last;

	if (selected < 0) {
		if (!clamp || !this->nchars)
			return;
		selected = 0;
	} else if ((uint32_t)selected >= this->nchars) {
		if (!clamp)
			return;
		selected = (int32_t)(this->nchars - 1u);
	}

	if ((uint32_t)selected == this->selected)
		return;

	if (selected == 0) {
		r = 0;
	} else if (selected == (int32_t)this->nchars - 1) {
		goto scroll_down;
	} else if ((uint32_t)selected < this->selected) {
		first = (int32_t)r0 * (int32_t)this->ncols;
		if (selected < first)
			r = (int)((uint32_t)selected / (uint32_t)this->ncols);
	} else {
		last = ((int32_t)r0 + (int32_t)this->nrows) * (int32_t)this->ncols - 1;
		if (selected > last) {
		scroll_down:
			r = (int)((uint32_t)selected / (uint32_t)this->ncols);
			r -= this->nrows - 1;
			r = MAX(r, 0);
		}
	}

	this->selected = (uint32_t)selected;
	execute_selection_callback(this, this->selected, meta, 0);
	gcmap_char_table_update_magnification(this);

	if (r == r0) {
		gtk_widget_queue_draw(widget); /* XXX just redraw the affected cells */
	} else if (this->vadjustment) {
		yadj = (double)r;
		gtk_adjustment_set_value(this->vadjustment, yadj);
	}
}


void
gcmap_char_table_adjust_cell_selection(CharTable *this, int32_t adjustment, int clamp, int meta)
{
	int32_t selected = (int32_t)this->selected;
	gcmap_char_table_select_cell(this, selected + adjustment, clamp, meta);
}


int
gcmap_char_table_select_codepoint(CharTable *this, uint32_t codepoint)
{
	uint32_t index;
	if (find_codepoint_index(this, codepoint, &index)) {
		gcmap_char_table_select_cell(this, (int32_t)index, 0, 0);
		return 1;
	}
	return 0;
}
