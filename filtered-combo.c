#include "common.h"

G_DEFINE_TYPE(FilteredCombo, filtered_combo, GTK_TYPE_HBOX)


enum {
	FILTERED_COMBO_CHANGED,
	LAST_SIGNAL
};


static unsigned int signals[LAST_SIGNAL];


static int
filtered_combo_matches(const char *value, const char *query)
{
	return !!libsimple_strcasestr(value, query);
}


static void
filtered_combo_set_selected(FilteredCombo *this, const char *value)
{
	free(this->selected);
	this->selected = estrdup(value);
}


static void
filtered_combo_notify_changed(FilteredCombo *this)
{
	if (this->selected)
		g_signal_emit(this, signals[FILTERED_COMBO_CHANGED], 0, this->selected);
}


static gboolean
filtered_combo_completion_match(GtkEntryCompletion *completion, const char *query, GtkTreeIter *iter, void *user_data)
{
	GtkTreeModel *model = gtk_entry_completion_get_model(completion);
	char *value;
	int matches;
	(void) user_data;
	gtk_tree_model_get(model, iter, 0, &value, -1);
	if (!value)
		return FALSE;
	matches = filtered_combo_matches(value, query);
	g_free(value);
	return matches;
}


static int
filtered_combo_select_first(FilteredCombo *this, const char *query)
{
	GtkTreeIter iter;
	char *value;
	if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(this->store), &iter))
		return 0;
	do {
		gtk_tree_model_get(GTK_TREE_MODEL(this->store), &iter, 0, &value, -1);
		if (value && filtered_combo_matches(value, query)) {
			filtered_combo_set_selected(this, value);
			g_free(value);
			return 1;
		}
		g_free(value);
	} while (gtk_tree_model_iter_next(GTK_TREE_MODEL(this->store), &iter));
	free(this->selected);
	this->selected = NULL;
	return 0;
}


static void
filtered_combo_changed(GtkComboBox *combo, FilteredCombo *this)
{
	GtkTreeIter iter;
	char *value;
	if (!gtk_combo_box_get_active_iter(combo, &iter))
		return;
	gtk_tree_model_get(GTK_TREE_MODEL(this->store), &iter, 0, &value, -1);
	if (!value)
		return;
	filtered_combo_set_selected(this, value);
	filtered_combo_notify_changed(this);
	g_free(value);
}


static void
filtered_combo_entry_changed(GtkEditable *editable, FilteredCombo *this)
{
	const char *query = gtk_entry_get_text(GTK_ENTRY(editable));
	int has_match = filtered_combo_select_first(this, query);
	gtk_entry_completion_set_popup_completion(this->completion, has_match);
	gtk_entry_completion_complete(this->completion);
}


static gboolean
filtered_combo_entry_key_press(GtkWidget *widget, GdkEventKey *event, FilteredCombo *this)
{
	(void) widget;
	if (event->keyval == GDK_Up || event->keyval == GDK_Down) {
		gtk_entry_completion_complete(this->completion);
		return FALSE;
	}
	return FALSE;
}


static gboolean
filtered_combo_completion_cursor_on_match(GtkEntryCompletion *completion, GtkTreeModel *model,
                                          GtkTreeIter *iter, FilteredCombo *this)
{
	char *value;
	(void) completion;
	gtk_tree_model_get(model, iter, 0, &value, -1);
	if (value) {
		filtered_combo_set_selected(this, value);
		g_free(value);
	}
	return FALSE;
}


static gboolean
filtered_combo_completion_match_selected(GtkEntryCompletion *completion, GtkTreeModel *model,
                                         GtkTreeIter *iter, FilteredCombo *this)
{
	char *value;
	(void) completion;
	gtk_tree_model_get(model, iter, 0, &value, -1);
	if (value) {
		filtered_combo_set_selected(this, value);
		filtered_combo_notify_changed(this);
		g_free(value);
	}
	return FALSE;
}


static void
filtered_combo_entry_accept(GtkEntry *entry, FilteredCombo *this)
{
	(void) entry;
	filtered_combo_notify_changed(this);
}


static gboolean
filtered_combo_entry_focus_out(GtkWidget *widget, GdkEventFocus *event, FilteredCombo *this)
{
	(void) event;
	filtered_combo_entry_accept(GTK_ENTRY(widget), this);
	return FALSE;
}


static gboolean
filtered_combo_entry_focus_in(GtkWidget *widget, GdkEventFocus *event, FilteredCombo *this)
{
	(void) event;
	(void) this;
	gtk_editable_set_position(GTK_EDITABLE(widget), -1);
	return FALSE;
}


static void
filtered_combo_destroy(GtkObject *this_)
{
	FilteredCombo *this = FILTERED_COMBO(this_);
	if (this->reposition_source)
		g_source_remove(this->reposition_source);
	this->reposition_source = 0;
	free(this->selected);
	this->selected = NULL;
	GTK_OBJECT_CLASS(filtered_combo_parent_class)->destroy(this_);
}


static void
filtered_combo_class_init(FilteredComboClass *class)
{
	GtkObjectClass *object_class = GTK_OBJECT_CLASS(class);
	object_class->destroy = filtered_combo_destroy;
	signals[FILTERED_COMBO_CHANGED] = g_signal_new("changed", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST,
	                                               0, NULL, NULL, g_cclosure_marshal_VOID__STRING,
	                                               G_TYPE_NONE, 1, G_TYPE_STRING);
}


static void
filtered_combo_init(FilteredCombo *this)
{
	GtkCellRenderer *width_renderer;

	this->store = gtk_list_store_new(1, G_TYPE_STRING);
	this->combo = gtk_combo_box_entry_new_with_model(GTK_TREE_MODEL(this->store), 0);
	this->entry = gtk_bin_get_child(GTK_BIN(this->combo));
	this->completion = gtk_entry_completion_new();

	gtk_entry_completion_set_model(this->completion, GTK_TREE_MODEL(this->store));
	gtk_entry_completion_set_match_func(this->completion, &filtered_combo_completion_match, this, NULL);
	gtk_entry_completion_set_minimum_key_length(this->completion, 0);
	gtk_entry_completion_set_text_column(this->completion, 0);
	width_renderer = gtk_cell_renderer_text_new();
	gtk_cell_renderer_set_padding(width_renderer, 0, 0);
	gtk_cell_renderer_set_fixed_size(width_renderer, 48, -1);
	gtk_cell_layout_pack_end(GTK_CELL_LAYOUT(this->completion), width_renderer, FALSE);
	gtk_entry_completion_set_inline_completion(this->completion, FALSE);
	gtk_entry_completion_set_inline_selection(this->completion, FALSE);
	gtk_entry_completion_set_popup_completion(this->completion, TRUE);
	gtk_entry_completion_set_popup_set_width(this->completion, FALSE);
	gtk_entry_completion_set_popup_single_match(this->completion, TRUE);

	gtk_entry_set_completion(GTK_ENTRY(this->entry), this->completion);
	gtk_combo_box_set_focus_on_click(GTK_COMBO_BOX(this->combo), TRUE);
	gtk_box_pack_start(GTK_BOX(this), this->combo, FALSE, FALSE, 0);

	g_signal_connect(this->combo, "changed", G_CALLBACK(&filtered_combo_changed), this);
	g_signal_connect(this->entry, "changed", G_CALLBACK(&filtered_combo_entry_changed), this);
	g_signal_connect(this->entry, "key-press-event", G_CALLBACK(&filtered_combo_entry_key_press), this);
	g_signal_connect(this->entry, "activate", G_CALLBACK(&filtered_combo_entry_accept), this);
	g_signal_connect(this->entry, "focus-out-event", G_CALLBACK(&filtered_combo_entry_focus_out), this);
	g_signal_connect_after(this->entry, "focus-in-event", G_CALLBACK(&filtered_combo_entry_focus_in), this);
	g_signal_connect(this->completion, "cursor-on-match", G_CALLBACK(&filtered_combo_completion_cursor_on_match), this);
	g_signal_connect(this->completion, "match-selected", G_CALLBACK(&filtered_combo_completion_match_selected), this);
}


GtkWidget *
filtered_combo_new(void)
{
	return g_object_new(FILTERED_TYPE_COMBO, NULL);
}


void
filtered_combo_append(FilteredCombo *this, const char *value)
{
	GtkTreeIter iter;
	gtk_list_store_append(this->store, &iter);
	gtk_list_store_set(this->store, &iter, 0, value, -1);
}


void
filtered_combo_set_active(FilteredCombo *this, int index)
{
	gtk_combo_box_set_active(GTK_COMBO_BOX(this->combo), index);
	gtk_editable_set_position(GTK_EDITABLE(this->entry), -1);
}


const char *
filtered_combo_get_selected(FilteredCombo *this)
{
	return this->selected;
}
