/* See LICENSE file for copyright and license details. */
#include "common.h"

#define REFERENCE_CHARACTER "X"
#define CELL_WIDTH_FACTOR   4
#define CELL_HEIGHT_FACTOR  2
#define CELL_X_CLEARANCE    2
#define CELL_Y_CLEARANCE    2

struct cells {
	int r0, c0; /* inclusive */
	int r1, c1; /* exclusive */
	int r_off;
};

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
	buf[is_printable_character(ch) ? g_unichar_to_utf8(ch, buf) : 0] = '\0';
}


static void
apply_font_size(CharTable *this, int size, int *text_w_out, int *text_h_out)
{
	pango_font_description_set_size(this->font, size * PANGO_SCALE);
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


static void
get_exposed_cells(GtkWidget *widget, GdkEventExpose *event, struct cells *cells_out)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	GdkRectangle area = event->area;
	LogicalScrollPane *scrolls = LOGICAL_SCROLL_PANE(gtk_widget_get_parent(widget));
	GtkAdjustment *vadjustment = logical_scroll_pane_get_vadjustment(scrolls);
	double y;

	cells_out->r0 = area.y * this->nrows / (this->viewport_h + 1);
	cells_out->c0 = area.x * this->ncols / (this->viewport_w + 1);
	cells_out->r1 = (area.y + area.height - 1) * this->nrows / (this->viewport_h + 1) + 1;
	cells_out->c1 = (area.x + area.width  - 1) * this->ncols / (this->viewport_w + 1) + 1;

	cells_out->r0 = MAX(cells_out->r0, 0);
	cells_out->c0 = MAX(cells_out->c0, 0);
	cells_out->r1 = MIN(cells_out->r1, cells_out->r0 + this->nrows);
	cells_out->c1 = MIN(cells_out->c1, cells_out->c0 + this->ncols);

	y = round(vadjustment->value);
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
	char text[16];
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

			format_character((gunichar)cp, text);
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
update_content_size(CharTable *this)
{
	GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(this));
	int nrows;
	if (parent && LOGICAL_IS_SCROLL_PANE(parent)) {
		nrows = (int)(this->nchars / (uint32_t)this->ncols);
		nrows += this->nchars % (uint32_t)this->ncols > 0u;
		logical_scroll_pane_set_content_size(LOGICAL_SCROLL_PANE(parent), this->ncols, nrows);
		logical_scroll_pane_set_viewport_size(LOGICAL_SCROLL_PANE(parent), this->ncols, this->nrows);
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
select_clicked_character(GtkWidget *widget, int meta, unsigned click_count, double x, double y)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	LogicalScrollPane *scrolls = LOGICAL_SCROLL_PANE(gtk_widget_get_parent(widget));
	GtkAdjustment *vadjustment = logical_scroll_pane_get_vadjustment(scrolls);
	double yadj = round(vadjustment->value);
	int r0 = (int)yadj;
	int r = (int)y * this->nrows / (this->viewport_h + 1);
	int c = (int)x * this->ncols / (this->viewport_w + 1);
	uint32_t selected;
	r = MIN(MAX(0, r), this->nrows - 1);
	c = MIN(MAX(0, c), this->ncols - 1);
	selected = ((uint32_t)r + (uint32_t)r0) * (uint32_t)this->ncols + (uint32_t)c;
	if (selected >= this->nchars) {
		this->multiclick_count = 0u;
	} else if (selected != this->selected) {
		click_count = 1u;
	clicked:
		this->selected = selected;
		if (this->selection_callback)
			(*this->selection_callback)(selected, get_codepoint(this, selected), meta, click_count >= 2u);
		this->multiclick_count = click_count == 1u ? 1u : 0u;
		gtk_widget_queue_draw(GTK_WIDGET(this)); /* XXX just redraw the affected cells */
	} else if (click_count > this->multiclick_count) {
		goto clicked;
	} else {
		this->multiclick_count = 0u;
	}
}


static gboolean
gcmap_char_table_button_press(GtkWidget *widget, GdkEventButton *event, void *user_data)
{
	unsigned click_count;
	(void) user_data;
	gtk_widget_grab_focus(widget);
	if (event->button == 1 || event->button == 3) {
		switch ((unsigned)event->type) {
		default:
		case GDK_BUTTON_PRESS:   click_count = 1u;  break;
		case GDK_2BUTTON_PRESS:  click_count = 2u;  break;
		case GDK_3BUTTON_PRESS:  click_count = 3u;  break;
		}
		select_clicked_character(widget, !!(event->state & GDK_MOD1_MASK), click_count, event->x, event->y);
	} else {
		GCMAP_CHAR_TABLE(widget)->multiclick_count = 0u;
	}
	return TRUE;
}


static gboolean
gcmap_char_table_key_press(GtkWidget *widget, GdkEventKey *event, void *user_data)
{
	CharTable *this = GCMAP_CHAR_TABLE(widget);
	int meta;

	(void) user_data;

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
		if (this->selection_callback)
			(*this->selection_callback)(this->selected, get_codepoint(this, this->selected), meta, 1);
		break;
	default:
		return FALSE;
	}

	return TRUE;
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
	this->nchars = 0u;
	this->selected = 0u;
	this->ranges = NULL;
	this->nranges = 0u;
	this->viewport_w = 1;
	this->viewport_h = 1;
	this->snap_cols_to_2pow = 0;
	this->snap_rows_to_2pow = 0;
	this->multiclick_count = 0u;
	this->selection_callback = NULL;

	GTK_WIDGET_SET_FLAGS(widget, GTK_CAN_FOCUS);
	gtk_widget_set_sensitive(widget, TRUE);

	gtk_widget_add_events(widget,
	                      GDK_BUTTON_PRESS_MASK |
	                      GDK_KEY_PRESS_MASK |
	                      GDK_FOCUS_CHANGE_MASK);

	g_signal_connect(widget, "button-press-event", G_CALLBACK(&gcmap_char_table_button_press), NULL);
	g_signal_connect(widget, "key-press-event", G_CALLBACK(&gcmap_char_table_key_press), NULL);
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
	update_cell_size(GTK_WIDGET(this), 0);
	update_content_size(this);
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
	update_cell_size(widget, 0);
	update_content_size(this);
	gtk_widget_queue_draw(widget);
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
	if (this->ranges == ranges && this->nranges == nranges)
		return;
	this->ranges = ranges;
	this->nranges = nranges;
	this->selected = 0u;
	widget = GTK_WIDGET(this);
	update_cell_size(widget, 1);
	update_content_size(this);
	if (this->selection_callback)
		(*this->selection_callback)(this->selected, get_codepoint(this, this->selected), 0, 0);
	gtk_widget_queue_draw(widget);
}


void
gcmap_char_table_viewport_updated(CharTable *this, int viewport_w, int viewport_h)
{
	this->viewport_w = viewport_w;
	this->viewport_h = viewport_h;
	update_cell_size(GTK_WIDGET(this), 0);
	update_content_size(this);
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

	return widget;
}


void
gcmap_char_table_select_cell(CharTable *this, int32_t selected, int clamp, int meta)
{
	GtkWidget *widget = GTK_WIDGET(this);
	LogicalScrollPane *scrolls = LOGICAL_SCROLL_PANE(gtk_widget_get_parent(widget));
	GtkAdjustment *vadjustment = logical_scroll_pane_get_vadjustment(scrolls);
	double yadj = round(vadjustment->value);
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
	if (this->selection_callback)
		(*this->selection_callback)(this->selected, get_codepoint(this, this->selected), meta, 0);

	if (r == r0) {
		gtk_widget_queue_draw(widget); /* XXX just redraw the affected cells */
	} else {
		yadj = (double)r;
		gtk_adjustment_set_value(vadjustment, yadj);
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
	size_t i = 0u;
	uint32_t index = 0u;

	while (i < this->nranges && codepoint > this->ranges[i].last) {
		index += (uint32_t)LIBCMAP_RANGE_SIZE(&this->ranges[i]);
		i += 1u;
	}

	if (i >= this->nranges || codepoint < this->ranges[i].first)
		return 0;

	index += codepoint - this->ranges[i].first;

	gcmap_char_table_select_cell(this, (int32_t)index, 0, 0);
	return 1;
}
