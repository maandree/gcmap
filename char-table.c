/* See LICENSE file for copyright and license details. */
#include "common.h"

G_DEFINE_TYPE(CharTable, gcmap_char_table, GTK_TYPE_DRAWING_AREA)


static gboolean
gcmap_char_table_expose(GtkWidget *widget, GdkEventExpose *event)
{
	cairo_t *cr = gdk_cairo_create(widget->window);

	(void) event;

	cairo_set_source_rgb(cr, 0.2f, 0.2f, 0.2f);
	cairo_paint(cr);

	cairo_set_source_rgb(cr, 1.0f, 1.0f, 1.0f);
	cairo_move_to(cr, 10, 20);
	cairo_show_text(cr, "Hello GTK+ 2");

	cairo_destroy(cr);
	return FALSE;
}

static void
gcmap_char_table_init(CharTable *self)
{
	gtk_widget_set_size_request(GTK_WIDGET(self), 200, 100);
}


static void
gcmap_char_table_class_init(CharTableClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->expose_event = gcmap_char_table_expose;
}


GtkWidget *
gcmap_char_table_new(void)
{
	return g_object_new(GCMAP_TYPE_CHAR_TABLE, NULL);
}
