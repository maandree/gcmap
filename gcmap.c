/* See LICENSE file for copyright and license details. */
#include "common.h"

USAGE("[-f font-family] [-s [font-size][/[[min]-[max]]]] [-B | -S] [-bi]");


#define DEFAULT_LISTING_TYPE "script"
#define LIST_LISTINGS(X, D)\
	X(BY_SCRIPT, "By _script", DEFAULT_LISTING_TYPE, by_script_selected, populate_scripts, GDK_s) D\
	X(BY_BLOCK, "By Unicode _block", "block", by_block_selected, populate_blocks, GDK_b)

enum listing {
#define X(ENUM, TITLE, TYPE, SELFUN, POPFUN, ACCEL) ENUM
	LIST_LISTINGS(X, COMMA)
#undef X
};

#define NLISTINGS_X(...) (size_t)1
#define NLISTINGS (LIST_LISTINGS(NLISTINGS_X, +))


static const struct libcmap_block all_block = {.name = "All", .range = LIBCMAP_UNIVERSE_RANGE};

static unsigned int min_font_size = 4;
static unsigned int default_font_size = 22;
static unsigned int max_font_size = 500;
static unsigned int small_font_size_increment = 1;
static unsigned int big_font_size_increment = 8;

static GtkWidget *window;
static GtkWidget *notebook;
static GtkWidget *copytext;
static GtkWidget *statusbar;
static GtkWidget *grouplist;
static GtkWidget *listcombo;
static GtkWidget *fontcombo;
static GtkWidget *bold;
static GtkWidget *italic;
static GtkWidget *sizespinner;
static GtkWidget *nextgroup;
static GtkWidget *prevgroup;
static GtkWidget *bylisting[NLISTINGS];
static GtkWidget *vtabtext;
static GtkListStore *fontcombo_store;
static GtkAdjustment *fontsize_adjustment;
static GtkAccelGroup *accelgroup;

#ifdef HAVE_ITEM_TOOLTIPS
/* relying on initialisation to NULL and 0 */
static const char **group_tooltips[NLISTINGS];
static size_t ngroup_tooltips[NLISTINGS];
static const char **groups_tooltips;
static size_t ngroups_tooltips;
#endif


static void
populate_font_families(GtkWidget *widget, GtkListStore *store)
{
	const char **names;
	PangoFontFamily **families;
	int nfamilies_int;
	size_t i, nfamilies;
	GtkTreeIter iter;

	pango_context_list_families(gtk_widget_get_pango_context(GTK_WIDGET(widget)), &families, &nfamilies_int);
	nfamilies = (size_t)nfamilies_int;
	names = calloc(nfamilies, sizeof(*names));

	for (i = 0; i < nfamilies; i++)
		names[i] = pango_font_family_get_name(families[i]);
	libsimple_qsort_str(names, nfamilies);
	for (i = 0; i < nfamilies; i++) {
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, names[i], -1);
	}

	free(names);
	g_free(families);
}


#ifdef HAVE_ITEM_TOOLTIPS


static int
grouplist_query_tooltip(GtkWidget *widget, int x, int y, int keyboard_mode,
                        GtkTooltip *tooltip, void *user_data)
{
	GtkTreeView *treeview = GTK_TREE_VIEW(grouplist);
	GtkTreePath *path = NULL;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;
	int index;

	(void) widget;
	(void) user_data;

	if (keyboard_mode) {
		selection = gtk_tree_view_get_selection(treeview);
		if (gtk_tree_selection_get_selected(selection, &model, &iter) == 0)
			return 0;
		path = gtk_tree_model_get_path(model, &iter);
	} else {
		if (!gtk_tree_view_get_path_at_pos(treeview, x, y, &path, NULL, NULL, NULL))
			return 0;
	}

	index = gtk_tree_path_get_indices(path)[0];
	if (index < 0 || (size_t)index >= ngroups_tooltips) {
		gtk_tree_path_free(path);
		return 0;
	}

	gtk_tooltip_set_text(tooltip, groups_tooltips[index]);
	gtk_tree_view_set_tooltip_row(treeview, tooltip, path);
	gtk_tree_path_free(path);
	return 1;
}


static char *
script_tooltip(const struct libcmap_script *script)
{
	size_t i, len, off = 0;
	int r;
	char *ret;

	if (!script->nranges)
		return NULL;

	len = strlen(script->name) + 1U;
	for (i = 0; i < script->nranges; i++) {
		r = libcmap_sprint_range(NULL, &script->ranges[i], NULL);
		if (r < 0)
			return NULL;
		len += (size_t)r + 2U;
	}

	ret = malloc((size_t)len + 1U);
	if (!ret)
		return NULL;

	off = (size_t)(stpcpy(ret, script->name) - ret);
	ret[off++] = ' ';
	ret[off++] = '(';
	for (i = 0; i < script->nranges; i++) {
		if (i) {
			ret[off++] = ',';
			ret[off++] = ' ';
		}
		r = libcmap_sprint_range(&ret[off], &script->ranges[i], NULL);
		if (r < 0)
			return NULL;
		off += (size_t)r;
		if (off >= len)
			abort();
	}
	ret[off++] = ')';
	ret[off] = '\0';

	return ret;
}


static char *
block_tooltip(const struct libcmap_block *block)
{
	int len, r;
	char *ret, *p;
	size_t size;

	size = strlen(block->name) + sizeof(" ()");
	len = libcmap_sprint_range(NULL, &block->range, NULL);
	if (len < 0)
		return NULL;
	size += (size_t)len;

	ret = malloc(size);
	if (!ret)
		return NULL;

	p = stpcpy(stpcpy(ret, block->name), " (");
	r = libcmap_sprint_range(p, &block->range, NULL);
	if (r < 0 || r > len)
		abort();
	stpcpy(&p[r], ")");

	return ret;
}


#endif /* HAVE_ITEM_TOOLTIPS */


static void
populate_scripts(GtkListStore *store)
{
	const struct libcmap_script *list = libcmap_script_list;
	size_t i, n = libcmap_script_list_size;
	GtkTreeIter iter;

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, "All", -1);
	for (i = 0; i < n; i++) {
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, list[i].name, -1);
	}

#ifdef HAVE_ITEM_TOOLTIPS
	if (!group_tooltips[BY_SCRIPT]) {
		ngroup_tooltips[BY_SCRIPT] = n + 1U;
		group_tooltips[BY_SCRIPT] = calloc(ngroup_tooltips[BY_SCRIPT], sizeof(**group_tooltips));
		group_tooltips[BY_SCRIPT][0] = block_tooltip(&all_block);
		for (i = 0; i < n; i++) {
			if (!strcmp(list[i].name, "Unknown"))
				group_tooltips[BY_SCRIPT][i + 1U] = "Unknown (Codepoints not assigned any script)";
			else
				group_tooltips[BY_SCRIPT][i + 1U] = script_tooltip(&list[i]);
		}
	}
	groups_tooltips = group_tooltips[BY_SCRIPT];
	ngroups_tooltips = ngroup_tooltips[BY_SCRIPT];
#endif
}


static void
populate_blocks(GtkListStore *store)
{
	const struct libcmap_block *list = libcmap_block_list;
	size_t i, n = libcmap_block_list_size;
	GtkTreeIter iter;

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, "All", -1);
	for (i = 0; i < n; i++) {
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, list[i].name, -1);
	}
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, "No Block", -1);

#ifdef HAVE_ITEM_TOOLTIPS
	if (!group_tooltips[BY_BLOCK]) {
		ngroup_tooltips[BY_BLOCK] = n + 2U;
		group_tooltips[BY_BLOCK] = calloc(ngroup_tooltips[BY_BLOCK], sizeof(**group_tooltips));
		group_tooltips[BY_BLOCK][0] = block_tooltip(&all_block);
		group_tooltips[BY_BLOCK][n + 1U] = "No Block (Codepoints not assigned any block)";
		for (i = 0; i < n; i++)
			group_tooltips[BY_BLOCK][i + 1U] = block_tooltip(&list[i]);
	}
	groups_tooltips = group_tooltips[BY_BLOCK];
	ngroups_tooltips = ngroup_tooltips[BY_BLOCK];
#endif
}


static void
copytext_changed(GtkEditable *editable, GtkWidget *button)
{
	const char *text = gtk_entry_get_text(GTK_ENTRY(editable));
	gtk_widget_set_sensitive(button, !!text[0]);
}


static void
copybutton_clicked(GtkWidget *button, GtkEditable *editable)
{
	const char *text = gtk_entry_get_text(GTK_ENTRY(editable));
	(void) button;
	gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
	gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), text, -1);
	gtk_clipboard_store(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
}


static void
fontcombo_changed(GtkComboBox *combo, void *user_data) /* TODO */
{
	GtkTreeIter iter;
	char *family;
	(void) user_data;
	if (!gtk_combo_box_get_active_iter(combo, &iter))
                return;
	gtk_tree_model_get(GTK_TREE_MODEL(fontcombo_store), &iter, 0, &family, -1);
	if (!family)
                return;
}


static void
bold_toggled(GtkToggleButton *button, void *user_data) /* TODO */
{
	int selected = gtk_toggle_button_get_active(button);
	(void) selected;
	(void) user_data;
}


static void
italic_toggled(GtkToggleButton *button, void *user_data) /* TODO */
{
	int selected = gtk_toggle_button_get_active(button);
	(void) selected;
	(void) user_data;
}


static void
fontsize_changed(GtkAdjustment *adjustment, void *user_data)
{
	double size = gtk_adjustment_get_value(adjustment);
	(void) user_data;
	(void) size; /* TODO apply font size */
}


static void
listcombo_changed(GtkComboBox *combo, GtkListStore *store)
{
	static int recursion_guard = 0;
	GtkWidget *label;
	int listing;

	if (recursion_guard)
		return;
	recursion_guard++;

	listing = gtk_combo_box_get_active(combo);

	switch (listing) {
#define X(ENUM, TITLE, TYPE, SELFUN, POPFUN, ACCEL)\
	case ENUM:\
		label = gtk_bin_get_child(GTK_BIN(nextgroup));\
		gtk_label_set_text_with_mnemonic(GTK_LABEL(label), "N_ext "TYPE);\
		label = gtk_bin_get_child(GTK_BIN(prevgroup));\
		gtk_label_set_text_with_mnemonic(GTK_LABEL(label), "P_revious "TYPE);\
		gtk_list_store_clear(store);\
		POPFUN(store);\
		break
	LIST_LISTINGS(X, ;);
#undef X
	default:
		abort();
	}

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(bylisting[listing])))
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(bylisting[listing]), TRUE);

	/* TODO select appropriate list item to keep select character */

	recursion_guard--;
}


#define X(ENUM, TITLE, TYPE, SELFUN, POPFUN, ACCEL)\
	static void\
	SELFUN(GtkWidget *menu_item, void *user_data)\
	{\
		(void) menu_item;\
		(void) user_data;\
		if (gtk_combo_box_get_active(GTK_COMBO_BOX(listcombo)) != ENUM)\
			gtk_combo_box_set_active(GTK_COMBO_BOX(listcombo), ENUM);\
	}
LIST_LISTINGS(X,)
#undef X


static GtkWidget *
add_menu(GtkWidget *parent, const char *label)
{
	GtkWidget *menu = gtk_menu_new();
	GtkWidget *item = gtk_menu_item_new_with_mnemonic(label);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
	return menu;
}


static GtkWidget *
add_menu_item(GtkWidget *parent, const char *label, const char *icon, unsigned int key,
              GdkModifierType mods, void (*action)(GtkWidget *menu_item, void *user_data))
{
	GtkWidget *item, *image;
	if (icon) {
		item = gtk_image_menu_item_new_with_mnemonic(label);
		image = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
		gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item), TRUE);
	} else {
		item = gtk_menu_item_new_with_mnemonic(label);
	}
	gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
	if (key)
		gtk_widget_add_accelerator(item, "activate", accelgroup, key, mods, GTK_ACCEL_VISIBLE);
	if (action)
		g_signal_connect(item, "activate", G_CALLBACK(action), NULL);
	return item;
}


static GtkWidget *
add_check_menu_item(GtkWidget *parent, const char *label, int checked, unsigned int key,
                    GdkModifierType mods, void (*action)(GtkWidget *menu_item, void *user_data))
{
	GtkWidget *item;
	item = gtk_check_menu_item_new_with_mnemonic(label);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), checked);
	gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
	if (key)
		gtk_widget_add_accelerator(item, "activate", accelgroup, key, mods, GTK_ACCEL_VISIBLE);
	if (action)
		g_signal_connect(item, "activate", G_CALLBACK(action), NULL);
	return item;
}


static GtkWidget *
add_radio_menu_item(GtkWidget *parent, const char *label, GSList **groupp, unsigned int key,
                    GdkModifierType mods, void (*action)(GtkWidget *menu_item, void *user_data))
{
	GtkWidget *item;
	item = gtk_radio_menu_item_new_with_mnemonic(*groupp, label);
	*groupp = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
	gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
	if (key)
		gtk_widget_add_accelerator(item, "activate", accelgroup, key, mods, GTK_ACCEL_VISIBLE);
	if (action)
		g_signal_connect(item, "activate", G_CALLBACK(action), NULL);
	return item;
}


static void
close_window(GtkWidget *menu_item, void *user_data)
{
	(void) menu_item;
	(void) user_data;
	gtk_main_quit();
}


static void
zoom_in(GtkWidget *menu_item, void *user_data)
{
	double size = gtk_adjustment_get_value(fontsize_adjustment);
	(void) menu_item;
	(void) user_data;
	size += 1;
	if (size > max_font_size)
		size = max_font_size;
	gtk_adjustment_set_value(fontsize_adjustment, size);
}


static void
zoom_out(GtkWidget *menu_item, void *user_data)
{
	double size = gtk_adjustment_get_value(fontsize_adjustment);
	(void) menu_item;
	(void) user_data;
	size -= 1;
	if (size < min_font_size)
		size = min_font_size;
	gtk_adjustment_set_value(fontsize_adjustment, size);
}


static void
zoom_original(GtkWidget *menu_item, void *user_data)
{
	(void) menu_item;
	(void) user_data;
	gtk_adjustment_set_value(fontsize_adjustment, default_font_size);
}


static int
zoom_original_accel(GtkAccelGroup *accel_group, GObject *acceleratable, unsigned int keyval,
                    GdkModifierType modifier, void *user_data)
{
	(void) accel_group;
	(void) acceleratable;
	(void) keyval;
	(void) modifier;
	zoom_original(NULL, user_data);
	return TRUE;
}

static void
go_next(GtkWidget *menu_item, void *user_data) /* TODO */
{
	(void) menu_item;
	(void) user_data;
}


static void
go_prev(GtkWidget *menu_item, void *user_data) /* TODO */
{
	(void) menu_item;
	(void) user_data;
}


static void
go_up_or_down(int adj)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(grouplist));
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	int selcount;
	int index;

	selcount = gtk_tree_selection_get_selected(selection, &model, &iter);
	if (selcount == 0) {
		index = -1;
	} else {
		path = gtk_tree_model_get_path(model, &iter);
		index = gtk_tree_path_get_indices(path)[0];
		gtk_tree_path_free(path);
	}

	index += adj;
	if (index < 0)
		return;
	if (index >= gtk_tree_model_iter_n_children(model, NULL))
		return;

	path = gtk_tree_path_new_from_indices(index, -1);
	gtk_tree_selection_unselect_all(selection);
	gtk_tree_selection_select_path(selection, path);
	gtk_tree_path_free(path);
}


static void
go_down(GtkWidget *menu_item, void *user_data)
{
	(void) menu_item;
	(void) user_data;
	go_up_or_down(+1);
}


static void
go_up(GtkWidget *menu_item, void *user_data)
{
	(void) menu_item;
	(void) user_data;
	go_up_or_down(-1);
}


static int
go_down_accel(GtkAccelGroup *accel_group, GObject *acceleratable, unsigned int keyval,
              GdkModifierType modifier, void *user_data)
{
	(void) accel_group;
	(void) acceleratable;
	(void) keyval;
	(void) modifier;
	go_down(nextgroup, user_data);
	return TRUE;
}


static int
go_up_accel(GtkAccelGroup *accel_group, GObject *acceleratable, unsigned int keyval,
            GdkModifierType modifier, void *user_data)
{
	(void) accel_group;
	(void) acceleratable;
	(void) keyval;
	(void) modifier;
	go_up(prevgroup, user_data);
	return TRUE;
}


static int
select_page_accel(GtkAccelGroup *accel_group, GObject *acceleratable, unsigned int keyval,
                  GdkModifierType modifier, void *user_data)
{
	(void) accel_group;
	(void) acceleratable;
	(void) modifier;
	(void) user_data;
	gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), (int)(keyval - GDK_1));
	return TRUE;
}


static void
set_tab_angle(float degrees)
{
	int i, n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
	GtkWidget *page, *tab;
	for (i = 0; i < n; i++) {
		page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), i);
		tab = gtk_notebook_get_tab_label(GTK_NOTEBOOK(notebook), page);
		if (!GTK_IS_LABEL(tab))
			continue;
		gtk_label_set_angle(GTK_LABEL(tab), degrees);
	}
}


static void
tabs_on_top(GtkWidget *menu_item, void *user_data)
{
	(void) menu_item;
	(void) user_data;
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
	set_tab_angle(0);
	gtk_widget_set_sensitive(vtabtext, FALSE);
}


static void
tabs_on_left(GtkWidget *menu_item, void *user_data)
{
	(void) menu_item;
	(void) user_data;
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_LEFT);
	set_tab_angle(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(vtabtext)) ? 90 : 0);
	gtk_widget_set_sensitive(vtabtext, TRUE);
}


static void
tabs_on_bottom(GtkWidget *menu_item, void *user_data)
{
	(void) menu_item;
	(void) user_data;
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_BOTTOM);
	set_tab_angle(0);
	gtk_widget_set_sensitive(vtabtext, FALSE);
}


static void
tabs_on_right(GtkWidget *menu_item, void *user_data)
{
	(void) menu_item;
	(void) user_data;
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_RIGHT);
	set_tab_angle(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(vtabtext)) ? 90 : 0);
	gtk_widget_set_sensitive(vtabtext, TRUE);
}


static void
vtabtext_changed(GtkWidget *menu_item, void *user_data)
{
	(void) menu_item;
	(void) user_data;
	if (!gtk_widget_get_sensitive(vtabtext))
		return;
	set_tab_angle(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(vtabtext)) ? 90 : 0);
}


static void
create_window(void)
{
	GtkWidget *vbox, *hbox, *left, *scrolled;
	GtkWidget *label, *copybutton, *align, *menubar, *menu;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkObject *obj;
	AtkObject *accessible;
	GClosure *closure;
	GSList *group;
	int i, n;

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(window, "destroy", G_CALLBACK(&gtk_main_quit), NULL);
	gtk_window_set_title(GTK_WINDOW(window), "gcmap");

	accelgroup = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(window), accelgroup);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	menubar = gtk_menu_bar_new();
	gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);

	menu = add_menu(menubar, "_File");
	add_menu_item(menu, "_Close", "window-close", GDK_w, GDK_CONTROL_MASK, &close_window);

	menu = add_menu(menubar, "_Layout");
	group = NULL;
	add_radio_menu_item(menu, "Tabs on _top", &group, 0, 0, &tabs_on_top);
	add_radio_menu_item(menu, "Tabs on _left", &group, 0, 0, &tabs_on_left);
	add_radio_menu_item(menu, "Tabs on _bottom", &group, 0, 0, &tabs_on_bottom);
	add_radio_menu_item(menu, "Tabs on _right", &group, 0, 0, &tabs_on_right);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	vtabtext = add_check_menu_item(menu, "_Vertical tab text", 1, 0, 0, &vtabtext_changed);
	gtk_widget_set_sensitive(vtabtext, FALSE);
	/* TODO add options to put some tabs in side-pane on the right */
	/* TODO add option for shorter tab texts */

	menu = add_menu(menubar, "_View");
	group = NULL;
#define X(ENUM, TITLE, TYPE, SELFUN, POPFUN, ACCEL)\
	bylisting[ENUM] = add_radio_menu_item(menu, TITLE, &group, ACCEL, GDK_CONTROL_MASK, &SELFUN)
	LIST_LISTINGS(X, ;);
#undef X
	/* TODO add option to use character list (show glyphs with names in rows) instead of character grid */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	add_menu_item(menu, "_Larger glyphs", "zoom-in", GDK_plus, GDK_CONTROL_MASK, &zoom_in);
	add_menu_item(menu, "S_maller glyphs", "zoom-out", GDK_minus, GDK_CONTROL_MASK, &zoom_out);
	add_menu_item(menu, "_Normal size", "zoom-original", GDK_equal, GDK_CONTROL_MASK, &zoom_original);
	gtk_accel_group_connect(accelgroup, GDK_0, GDK_CONTROL_MASK, 0,
	                        g_cclosure_new(G_CALLBACK(&zoom_original_accel), NULL, NULL));
	gtk_accel_group_connect(accelgroup, GDK_1, GDK_CONTROL_MASK, 0,
	                        g_cclosure_new(G_CALLBACK(&zoom_original_accel), NULL, NULL));

	menu = add_menu(menubar, "_Go");
	add_menu_item(menu, "_Next character", "go-next", GDK_n, GDK_CONTROL_MASK, &go_next);
	add_menu_item(menu, "_Previous character", "go-previous", GDK_p, GDK_CONTROL_MASK, &go_prev);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	nextgroup = add_menu_item(menu, "N_ext "DEFAULT_LISTING_TYPE, "go-down", GDK_n, GDK_CONTROL_MASK | GDK_SHIFT_MASK, &go_down);
	prevgroup = add_menu_item(menu, "P_revious "DEFAULT_LISTING_TYPE, "go-up", GDK_p, GDK_CONTROL_MASK | GDK_SHIFT_MASK, &go_up);
	gtk_accel_group_connect(accelgroup, GDK_Down, GDK_CONTROL_MASK, 0,
	                        g_cclosure_new(G_CALLBACK(&go_down_accel), NULL, NULL));
	gtk_accel_group_connect(accelgroup, GDK_Up, GDK_CONTROL_MASK, 0,
	                        g_cclosure_new(G_CALLBACK(&go_up_accel), NULL, NULL));

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 8);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	accessible = gtk_widget_get_accessible(GTK_WIDGET(hbox));
	atk_object_set_name(accessible, "Font");

	fontcombo = gtk_combo_box_new();
	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(fontcombo), renderer, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(fontcombo), renderer, "text", 0, NULL);
	fontcombo_store = gtk_list_store_new(1, G_TYPE_STRING);
	gtk_combo_box_set_model(GTK_COMBO_BOX(fontcombo), GTK_TREE_MODEL(fontcombo_store));
	populate_font_families(fontcombo, fontcombo_store);
	gtk_combo_box_set_focus_on_click(GTK_COMBO_BOX(fontcombo), FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), fontcombo, FALSE, FALSE, 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(fontcombo), -1);
	g_signal_connect(fontcombo, "changed", G_CALLBACK(&fontcombo_changed), NULL);
	accessible = gtk_widget_get_accessible(fontcombo);
	atk_object_set_name(accessible, "Font family");

	bold = gtk_toggle_button_new_with_mnemonic(GTK_STOCK_BOLD);
	gtk_button_set_use_stock(GTK_BUTTON(bold), TRUE);
	gtk_button_set_focus_on_click(GTK_BUTTON(bold), FALSE);
	g_signal_connect(bold, "toggled", G_CALLBACK(&bold_toggled), NULL);
	gtk_box_pack_start(GTK_BOX(hbox), bold, FALSE, FALSE, 0);

	italic = gtk_toggle_button_new_with_mnemonic(GTK_STOCK_ITALIC);
	gtk_button_set_use_stock(GTK_BUTTON(italic), TRUE);
	gtk_button_set_focus_on_click(GTK_BUTTON(italic), FALSE);
	g_signal_connect(italic, "toggled", G_CALLBACK(&italic_toggled), NULL);
	gtk_box_pack_start(GTK_BOX(hbox), italic, FALSE, FALSE, 0);

	obj = gtk_adjustment_new(default_font_size, min_font_size, max_font_size,
	                         small_font_size_increment, big_font_size_increment, 0);
	fontsize_adjustment = GTK_ADJUSTMENT(obj);
	sizespinner = gtk_spin_button_new(fontsize_adjustment, 0, 0);
	g_signal_connect(fontsize_adjustment, "value-changed", G_CALLBACK(fontsize_changed), NULL);
	gtk_box_pack_start(GTK_BOX(hbox), sizespinner, FALSE, FALSE, 0);
	accessible = gtk_widget_get_accessible(sizespinner);
	atk_object_set_name(accessible, "Font size");

	left = gtk_vbox_new(FALSE, 0);

	listcombo = gtk_combo_box_new_text();
	gtk_combo_box_append_text(GTK_COMBO_BOX(listcombo), "Scripts");
	gtk_combo_box_append_text(GTK_COMBO_BOX(listcombo), "Unicode blocks");
	gtk_combo_box_set_focus_on_click(GTK_COMBO_BOX(listcombo), FALSE);
	gtk_box_pack_start(GTK_BOX(left), listcombo, FALSE, FALSE, 0);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_ETCHED_IN);
	store = gtk_list_store_new(1, G_TYPE_STRING);
	populate_scripts(store);
	grouplist = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(NULL, renderer, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(grouplist), column);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(grouplist), FALSE);
	g_signal_connect(grouplist, "query-tooltip", G_CALLBACK(&grouplist_query_tooltip), NULL);
	gtk_widget_set_has_tooltip(GTK_WIDGET(grouplist), TRUE);
	gtk_container_add(GTK_CONTAINER(scrolled), grouplist);
	gtk_box_pack_start(GTK_BOX(left), scrolled, TRUE, TRUE, 0);

	g_signal_connect(G_OBJECT(listcombo), "changed", G_CALLBACK(&listcombo_changed), store);

	notebook = gtk_notebook_new();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);

	/* TODO character map tab pane (should have focus: gtk_widget_grab_focus(GTK_WIDGET(...)) */
	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(scrolled), gcmap_char_table_new());
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new_with_mnemonic("Character _table"));

#ifdef TODO
	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(scrolled), gtk_label_new("TODO"));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new_with_mnemonic("Character _details"));
#endif

#ifdef TODO
	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(scrolled), gtk_label_new("TODO"));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new_with_mnemonic("Glyph v_ariants"));
#endif

#ifdef TODO
	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(scrolled), gtk_label_new("TODO"));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new_with_mnemonic("Font c_omparison"));
#endif

	n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
	for (i = 1; i <= n && i <= 9; i++) {
		closure = g_cclosure_new(G_CALLBACK(&select_page_accel), NULL, NULL);
		gtk_accel_group_connect(accelgroup, GDK_0 + (unsigned)i, GDK_MOD1_MASK, GTK_ACCEL_LOCKED, closure);
	}

	hbox = gtk_hpaned_new();
	gtk_paned_pack1(GTK_PANED(hbox), left, FALSE, TRUE);
	gtk_paned_pack2(GTK_PANED(hbox), notebook, TRUE, TRUE);
	align = gtk_alignment_new(0.0f, 0.0f, 1.0f, 1.0f);
	gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0, 0, 8, 8);
	gtk_container_add(GTK_CONTAINER(align), hbox);
	gtk_box_pack_start(GTK_BOX(vbox), align, TRUE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 8);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 8);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	label = gtk_label_new_with_mnemonic("Te_xt to copy:");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	copytext = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), copytext, TRUE, TRUE, 0);
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), copytext);

	copybutton = gtk_button_new_from_stock(GTK_STOCK_COPY);
	gtk_widget_set_tooltip_text(copybutton, "Copy to the clipboard");
	gtk_widget_set_sensitive(copybutton, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), copybutton, FALSE, FALSE, 0);

	g_signal_connect(G_OBJECT(copybutton), "clicked", G_CALLBACK(&copybutton_clicked), copytext);
	g_signal_connect(G_OBJECT(copytext), "changed", G_CALLBACK(&copytext_changed), copybutton);

	statusbar = gtk_statusbar_new();
	gtk_statusbar_set_has_resize_grip(GTK_STATUSBAR(statusbar), TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), statusbar, FALSE, FALSE, 0);

	gtk_widget_show_all(vbox);
}


static const char *
maybe_sat_parse_int_as_uint(const char *s, unsigned int *out, int *setp)
{
	unsigned int digit;
	if (!isdigit(*s))
		return s;
	*out = 0;
	*setp = 1;
	for (; isdigit(*s); s++) {
		digit = (unsigned int)(*s & 15);
		if (*out > ((unsigned int)INT_MAX - digit) / 10U)
			*out = (unsigned int)INT_MAX;
		else
			*out = *out * 10U + digit;
	}
	return s;
}


static void
parse_fontsize(const char *s, int *default_font_size_setp, int *min_font_size_setp, int *max_font_size_setp)
{
	s = maybe_sat_parse_int_as_uint(s, &default_font_size, default_font_size_setp);

	if (!*s)
		return;
	if (*s++ != '/')
		usage();
	if (!*s)
		return;

	s = maybe_sat_parse_int_as_uint(s, &min_font_size, min_font_size_setp);

	if (*s++ != '-')
		usage();
	if (!*s)
		return;

	s = maybe_sat_parse_int_as_uint(s, &max_font_size, max_font_size_setp);

	if (*s)
		usage();

	if (!default_font_size)
		eprintf("default font size cannot be zero");
	if (!min_font_size)
		eprintf("minimum font size cannot be zero");
	if (!max_font_size)
		eprintf("maximum font size cannot be zero");
}


int
main(int argc, char *argv[])
{
	GtkIconTheme *theme;
	const char *icon;
	int xargc = 1;
        char *xargva[] = {argv[0], NULL};
	char **xargv = xargva;
	enum listing listing = (enum listing)0;
	int use_bold = 0;
	int use_italic = 0;
	int default_font_size_set = 0;
	int min_font_size_set = 0;
	int max_font_size_set = 0;
	const char *font_family = "Sans";

	ARGBEGIN {
	case 'B':
		listing = 1;
		break;
	case 'S':
		listing = 0;
		break;
	case 'b':
		use_bold = 1;
		break;
	case 'i':
		use_italic = 1;
		break;
	case 'f':
		font_family = ARG();
		break;
	case 's':
		parse_fontsize(ARG(), &default_font_size_set, &min_font_size_set, &max_font_size_set);
		break;
	default:
		usage();
	} ARGEND;

	if (argc)
		usage();

	if (min_font_size_set && max_font_size_set) {
		if (min_font_size > max_font_size)
			eprintf("minimum font size cannot be greater than maximum font size");
	} else if (min_font_size_set) {
		if (max_font_size < min_font_size)
			max_font_size = min_font_size;
	} else if (max_font_size_set) {
		if (min_font_size > max_font_size)
			min_font_size = max_font_size;
	}
	if (default_font_size_set) {
		if (default_font_size < min_font_size) {
			if (min_font_size_set)
				eprintf("default font size cannot be less than the minimum font size");
			min_font_size = default_font_size;
		}
		if (default_font_size > max_font_size) {
			if (max_font_size_set)
				eprintf("default font size cannot be greater than the minimum font size");
			max_font_size = default_font_size;
		}
	} else {
		if (default_font_size < min_font_size)
			default_font_size = min_font_size;
		else if (default_font_size > max_font_size)
			default_font_size = max_font_size;
	}

	g_set_prgname(argv0);
	g_set_application_name("gcmap");

	gtk_init(&xargc, &xargv);
	theme = gtk_icon_theme_get_default();
	if (!gtk_icon_theme_has_icon(theme, icon = "apps/gcmap") &&
	    !gtk_icon_theme_has_icon(theme, icon = "gcmap") &&
	    !gtk_icon_theme_has_icon(theme, icon = "apps/accessories-character-map") &&
	    !gtk_icon_theme_has_icon(theme, icon = "accessories-character-map"))
		icon = "gcmap";
	gtk_window_set_default_icon_name(icon);

	create_window();
	gtk_window_set_icon_name(GTK_WINDOW(window), icon);
	gtk_window_set_wmclass(GTK_WINDOW(window), "gcmap", "gcmap");
	gtk_window_set_role(GTK_WINDOW(window), "gcmap");

	if (gtk_combo_box_get_active(GTK_COMBO_BOX(listcombo)) != (int)listing)
		gtk_combo_box_set_active(GTK_COMBO_BOX(listcombo), (int)listing);
	if (use_bold)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bold), TRUE);
	if (use_italic)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(italic), TRUE);
	groups_tooltips = group_tooltips[listing];
	ngroups_tooltips = ngroup_tooltips[listing];

	(void) font_family; /* TODO select font family */
	/* TODO select character (U+0000) and group (locate the on containing it, go to All if not found) */

	gtk_window_present(GTK_WINDOW(window));
        gtk_main();
}
