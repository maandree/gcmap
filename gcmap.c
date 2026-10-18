/* See LICENSE file for copyright and license details. */
#include "common.h"

USAGE("[-f font-family] [-s [font-size][/[[min]-[max]]]] [-B | -S] [-bi]");


#define DEFAULT_FONT_FAMILY "Sans"


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

#define LEFT_TO_RIGHT_OVERRIDE     "\xe2\x80\xad"
#define POP_DIRECTIONAL_FORMATTING "\xe2\x80\xac"


static const struct libcmap_block all_block = {.name = "All", .range = LIBCMAP_UNIVERSE_RANGE};

static unsigned int min_font_size = 4;
static unsigned int default_font_size = 14;
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
static GtkWidget *snap2_cols;
static GtkWidget *snap2_rows;
static GtkWidget *chartable;
static GtkWidget *details;
static GtkAdjustment *fontsize_adjustment;
static GtkAccelGroup *accelgroup;
static GtkTextBuffer *details_buffer;
static GtkTextTag *tag_display;
static GtkTextTag *tag_name;
static GtkTextTag *tag_header;

static uint32_t selected_codepoint = 0u;
static int switching_groups = 0;

#ifdef HAVE_ITEM_TOOLTIPS
/* relying on initialisation to NULL and 0 */
static const char **group_tooltips[NLISTINGS];
static size_t ngroup_tooltips[NLISTINGS];
static const char **groups_tooltips;
static size_t ngroups_tooltips;
#endif


static size_t
format_statusbar_message(char *buffer, size_t size)
{
	struct libcmap_codepoint_data *data;
	int r;
	size_t len = 0u;

	r = snprintf(buffer, size, "U+%04"PRIX32, selected_codepoint);
	if (r < 0)
		eprintf("snprintf");
	len += (size_t)r;
	buffer = &buffer[MIN(size, (size_t)r)];
	size -= MIN(size, (size_t)r);

	data = get_codepoint_data(selected_codepoint);
	if (!data)
		return len;

	r = snprintf(buffer, size, " %s", data->name);
	if (r < 0)
		eprintf("snprintf");
	len += (size_t)r;
	buffer = &buffer[MIN(size, (size_t)r)];
	size -= MIN(size, (size_t)r);

	return len;
}


static void
update_statusbar(void)
{
	size_t len = format_statusbar_message(NULL, 0u);
	char *message = emalloc(++len);
	format_statusbar_message(message, len);
	gtk_statusbar_pop(GTK_STATUSBAR(statusbar), 0u);
        gtk_statusbar_push(GTK_STATUSBAR(statusbar), 0u, message);
	free(message);
}


static const char *
format_utf8_hex(char *buf, uint32_t cp, const char *prefix, const char *suffix)
{
	char utf8[8];
	int r, i, len = g_unichar_to_utf8((gunichar)cp, utf8);
	char *p = stpcpy(buf, prefix);
	for (i = 0; i < len; i++) {
		if (i)
			*p++ = ' ';
		r = sprintf(p, "%02X", (unsigned char)utf8[i] & 0xFFu);
		if (r < 0)
			eprintf("sprintf:");
		p += r;
	}
	stpcpy(p, suffix);
	return buf;
}


static const char *
format_utf8_hexesc(char *buf, uint32_t cp, const char *prefix, const char *suffix)
{
	char utf8[8];
	int r, i, len = g_unichar_to_utf8((gunichar)cp, utf8);
	char *p = stpcpy(buf, prefix);
	for (i = 0; i < len; i++) {
		r = sprintf(p, "\\x%02X", (unsigned char)utf8[i] & 0xFFu);
		if (r < 0)
			eprintf("sprintf:");
		p += r;
	}
	stpcpy(p, suffix);
	return buf;
}


static const char *
format_utf8_octesc(char *buf, uint32_t cp, const char *prefix, const char *suffix)
{
	char utf8[8];
	int r, i, len = g_unichar_to_utf8((gunichar)cp, utf8);
	char *p = stpcpy(buf, prefix);
	for (i = 0; i < len; i++) {
		r = sprintf(p, "\\%03o", (unsigned char)utf8[i] & 0xFFu);
		if (r < 0)
			eprintf("sprintf:");
		p += r;
	}
	stpcpy(p, suffix);
	return buf;
}


static const char *
format_utf16_hex(char *buf, uint32_t cp, const char *prefix, const char *suffix)
{
	char *p = stpcpy(buf, prefix);
	int r;
	if (cp <= UINT32_C(0xFFFF)) {
		r = sprintf(p, "%04"PRIX32, cp);
	} else {
		cp -= UINT32_C(0x10000);
		r = sprintf(p, "%04"PRIX32" %04"PRIX32,
		            (uint32_t)((cp >> 10) + 0xD800u),
		            (uint32_t)((cp & 0x3FFu) + 0xDC00u));
	}
	if (r < 0)
		eprintf("sprintf:");
	p += r;
	stpcpy(p, suffix);
	return buf;
}


static const char *
format_xml_dec(char *buf, uint32_t cp, const char *prefix, const char *suffix)
{
	char *p = stpcpy(stpcpy(buf, prefix), "&#");
	int r = sprintf(p, "%"PRIu32, cp);
	if (r < 0)
		eprintf("sprintf:");
	p += r;
	stpcpy(stpcpy(p, ";"), suffix);
	return buf;
}


static void
update_details(void)
{
	enum libcmap_bidirectional_category bidirectional_category;
	struct libcmap_codepoint_data *data;
	const struct libcmap_block *block;
	const struct libcmap_script *script;
	size_t i, first_script2 = SIZE_MAX;
	enum codepoint_class class;
	GtkTextIter itr, itr_end, itr0;
	GtkTextMark *mark;
	char buf[128u];
	int len, printed_first_script2 = 0;

	/* XXX this should be defered to when the tab is open (being opened if it is not already opened) */

	gtk_text_buffer_get_start_iter(details_buffer, &itr);
	gtk_text_buffer_get_end_iter(details_buffer, &itr_end);

	gtk_text_buffer_delete(details_buffer, &itr, &itr_end);

	data = get_codepoint_data(selected_codepoint);
	if (!data)
		return;

	/* XXX when this text is copied, LEFT-TO-RIGHT OVERRIDE and POP DIRECTIONAL FORMATTING shall not be copied */
	len = 0;
	class = classify_from_codepoint_data(data);
	switch (class) {
	case CP_COMBINING_MARK:
	case CP_REGULAR:
	case CP_RESERVED:
		len = (int)(stpcpy(&buf[len], LEFT_TO_RIGHT_OVERRIDE) - buf);
#if MARK_BASE
		if (class == CP_COMBINING_MARK)
			len += g_unichar_to_utf8(MARK_BASE, &buf[len]);
#endif
		len += g_unichar_to_utf8((gunichar)selected_codepoint, &buf[len]);
		len = (int)(stpcpy(&buf[len], POP_DIRECTIONAL_FORMATTING) - buf);
		buf[len++] = '\n';
		buf[len] = '\0';
		gtk_text_buffer_insert_with_tags(details_buffer, &itr, buf, -1, tag_display, NULL);
		break;

	case CP_CONTROL:
	case CP_SURROGATE:
	case CP_NONCHARACTER:
	case CP_OUT_OF_RANGE:
	default:
		gtk_text_buffer_insert(details_buffer, &itr, "[non-printable character]\n", -1);
		break;
	}
	gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);

	mark = gtk_text_buffer_create_mark(details_buffer, NULL, &itr, TRUE);
	len = 0;
	buf[len++] = 'U';
	buf[len++] = '+';
	if (selected_codepoint >> 20)  buf[len++] = "0123456789ABCDEF"[(selected_codepoint >> 20) & 15u];
	if (selected_codepoint >> 16)  buf[len++] = "0123456789ABCDEF"[(selected_codepoint >> 16) & 15u];
	buf[len++] = "0123456789ABCDEF"[(selected_codepoint >> 12) & 15u];
	buf[len++] = "0123456789ABCDEF"[(selected_codepoint >>  8) & 15u];
	buf[len++] = "0123456789ABCDEF"[(selected_codepoint >>  4) & 15u];
	buf[len++] = "0123456789ABCDEF"[(selected_codepoint >>  0) & 15u];
	buf[len++] = ' ';
	gtk_text_buffer_insert(details_buffer, &itr, buf, len);
	gtk_text_buffer_insert(details_buffer, &itr, data->name, -1);
	gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);
	gtk_text_buffer_get_iter_at_mark(details_buffer, &itr0, mark);
	gtk_text_buffer_apply_tag(details_buffer, tag_name, &itr0, &itr);
	gtk_text_buffer_delete_mark(details_buffer, mark);

	gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);

	gtk_text_buffer_insert_with_tags_by_name(details_buffer, &itr, "General character properties\n", -1, "header", NULL);
	gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);
	gtk_text_buffer_insert(details_buffer, &itr, "General category: ", -1);
	gtk_text_buffer_insert(details_buffer, &itr, libcmap_general_category_string(data->general_category &
	                                                                             LIBCMAP_GENERAL_SUPERCATEGORY_MASK), -1);
	gtk_text_buffer_insert(details_buffer, &itr, ", ", -1);
	gtk_text_buffer_insert(details_buffer, &itr, libcmap_general_category_string(data->general_category), -1);
	if (data->general_category == LIBCMAP_GENERAL_CATEGORY_OTHER_NOT_ASSIGNED)
		gtk_text_buffer_insert(details_buffer, &itr, data->noncharacter ? ", Non-character" : ", Reserved", -1);
	gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);
	if (data->bidirectional_category) {
		gtk_text_buffer_insert(details_buffer, &itr, "Bidirectional category: ", -1);
		bidirectional_category = data->bidirectional_category;
		gtk_text_buffer_insert(details_buffer, &itr, libcmap_bidirectional_category_string(bidirectional_category), -1);
		gtk_text_buffer_insert(details_buffer, &itr, " (", -1);
		bidirectional_category &= LIBCMAP_BIDIRECTIONAL_SUPERCATEGORY_MASK;
		gtk_text_buffer_insert(details_buffer, &itr, libcmap_bidirectional_category_string(bidirectional_category), -1);
		gtk_text_buffer_insert(details_buffer, &itr, ")\n", -1);
	}
	/* XXX data->decomposition_mapping, data->decomposition_mapping_size, data->decomposition_type */
	/* XXX data->simple_uppercase_mapping */
	/* XXX data->simple_lowercase_mapping */
	/* XXX data->simple_titlecase_mapping */
	/* XXX data->canonical_combining_classes */
	/* XXX data->bidirectional_mirrored */
	gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);

	if (data->unicode_1_name) {
		gtk_text_buffer_insert(details_buffer, &itr, "Unicode 1.0 name: ", -1);
		gtk_text_buffer_insert(details_buffer, &itr, data->unicode_1_name, -1);
		gtk_text_buffer_insert(details_buffer, &itr, "\n\n", -1);
	}

	if (data->numeric_value) {
		gtk_text_buffer_insert(details_buffer, &itr, "Numeric value: ", -1);
		gtk_text_buffer_insert(details_buffer, &itr, data->numeric_value, -1);
		gtk_text_buffer_insert(details_buffer, &itr, "\nNumeric type: ", -1);
		gtk_text_buffer_insert(details_buffer, &itr, libcmap_numeric_type_string(data->numeric_type), -1);
		gtk_text_buffer_insert(details_buffer, &itr, "\n\n", -1);
	}

	block = libcmap_find_block(selected_codepoint, NULL);
	gtk_text_buffer_insert(details_buffer, &itr, "Unicode block: ", -1);
	gtk_text_buffer_insert(details_buffer, &itr, block ? block->name : "(none)", -1);
	gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);

	script = libcmap_find_script(selected_codepoint, NULL, NULL);
	for (i = 0u; i < libcmap_script_list_size; i++) {
		if (script && i == (size_t)(script - libcmap_script_list))
			continue;
		if (!is_in_script(selected_codepoint, &libcmap_script_list[i]))
			continue;
		if (first_script2 == SIZE_MAX) {
			first_script2 = i;
		} else if (printed_first_script2) {
			goto print_secondary_script;
		} else {
			printed_first_script2 = 1;
			gtk_text_buffer_insert(details_buffer, &itr, "Primary Unicode script: ", -1);
			gtk_text_buffer_insert(details_buffer, &itr, script ? script->name : "(none)", -1);
			gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);
			gtk_text_buffer_insert(details_buffer, &itr, "Secondary Unicode scripts:\n\t", -1);
			gtk_text_buffer_insert(details_buffer, &itr, libcmap_script_list[first_script2].name, -1);
			gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);
		print_secondary_script:
			gtk_text_buffer_insert(details_buffer, &itr, "\t", -1);
			gtk_text_buffer_insert(details_buffer, &itr, libcmap_script_list[i].name, -1);
			gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);
		}
	}
	if (!printed_first_script2) {
		if (first_script2 != SIZE_MAX)
			gtk_text_buffer_insert(details_buffer, &itr, "Primary Unicode script: ", -1);
		else
			gtk_text_buffer_insert(details_buffer, &itr, "Unicode script: ", -1);
		gtk_text_buffer_insert(details_buffer, &itr, script ? script->name : "(none)", -1);
		gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);
		if (first_script2 != SIZE_MAX) {
			gtk_text_buffer_insert(details_buffer, &itr, "Secondary Unicode scripts: ", -1);
			gtk_text_buffer_insert(details_buffer, &itr, libcmap_script_list[first_script2].name, -1);
			gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);
		}
	}

	if (is_surrogate(selected_codepoint))
		return;

	gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);
	gtk_text_buffer_insert_with_tags_by_name(details_buffer, &itr, "Various encodings\n", -1, "header", NULL);
	gtk_text_buffer_insert(details_buffer, &itr, "\n", -1);
	gtk_text_buffer_insert(details_buffer, &itr, "UTF-8:\n", -1);
	gtk_text_buffer_insert(details_buffer, &itr, format_utf8_hex(buf, selected_codepoint, "\t", "\n"), -1);
	gtk_text_buffer_insert(details_buffer, &itr, format_utf8_hexesc(buf, selected_codepoint, "\t", "\n"), -1);
	gtk_text_buffer_insert(details_buffer, &itr, format_utf8_octesc(buf, selected_codepoint, "\t", "\n"), -1);
	gtk_text_buffer_insert(details_buffer, &itr, "\nUTF-16:\n", -1);
	gtk_text_buffer_insert(details_buffer, &itr, format_utf16_hex(buf, selected_codepoint, "\t", "\n"), -1);
	gtk_text_buffer_insert(details_buffer, &itr, "\nXML:\n", -1);
	gtk_text_buffer_insert(details_buffer, &itr, format_xml_dec(buf, selected_codepoint, "\t", "\n"), -1);
}


static void
character_cell_selected(uint32_t index, uint32_t codepoint, enum char_table_selection_type selection_type)
{
	GtkEditable *editable;
	char text[16];
	int len, pos;

	(void) index;

	selected_codepoint = codepoint;

	if (!is_valid_codepoint(codepoint)) {
		switch (selection_type) {
		case COPY_TO_TEXTFIELD:
		case COPY_TO_SECONDARY:
		case DRAG_ANNOUNCEMENT:
			gtk_statusbar_pop(GTK_STATUSBAR(statusbar), 0u);
			gtk_statusbar_push(GTK_STATUSBAR(statusbar), 0u, "Cannot copy non-character codepoint");
			return;
		default:
		case COPY_TO_PRIMARY:
			goto out;
		}
	}

	text[len = g_unichar_to_utf8(codepoint, text)] = '\0';

	switch (selection_type) {
	case COPY_TO_TEXTFIELD:
		editable = GTK_EDITABLE(copytext);
		pos = gtk_editable_get_position(editable);
		gtk_editable_insert_text(editable, text, len, &pos);
		gtk_editable_set_position(editable, pos);
		break;
	case COPY_TO_PRIMARY:
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), text, len);
		/* XXX add setting to disable */
		break;
	case COPY_TO_SECONDARY:
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_SECONDARY), text, len);
		/* XXX add setting to swap SECONDARY and PRIMARY */
		break;
	case DRAG_ANNOUNCEMENT:
		return;
	}

out:
	update_statusbar();
	update_details();
}


static void
select_group(int index)
{
	GtkTreeView *treeview = GTK_TREE_VIEW(grouplist);
	GtkTreePath *path = gtk_tree_path_new_from_indices(index, -1);
	GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
	gtk_tree_selection_select_path(selection, path);
	gtk_tree_view_set_cursor(treeview, path, NULL, FALSE);
	gtk_tree_path_free(path);
}


static void
select_group_for_codepoint(enum listing listing, uint32_t codepoint, int fallback_to_all)
{
	const struct libcmap_block *block;
	const struct libcmap_script *script;
	switch (listing) {
	case BY_BLOCK:
		block = libcmap_find_block(codepoint, NULL);
		select_group(block ? (int)(block - libcmap_block_list) + 1 :
		             fallback_to_all ? 0 : (int)libcmap_block_list_size + 1);
		break;
	case BY_SCRIPT:
		script = libcmap_find_script(codepoint, NULL, NULL);
		select_group(script ? (int)(script - libcmap_script_list) + 1 :
		             fallback_to_all ? 0 : (int)libcmap_script_list_size + 1);
		break;
	default:
		abort();
	}
}


static int
get_codepoint(const char **textp, uint32_t *codepoint_out)
{
	static const uint32_t limits[] = {
		(UINT32_C(1) <<  0) - 1u,
		(UINT32_C(1) <<  7) - 1u,
		(UINT32_C(1) << 11) - 1u,
		(UINT32_C(1) << 16) - 1u,
		UINT32_C(0x10FFFF)
	};

	const unsigned char *text = (const unsigned char *)*textp;
	uint32_t lower_limit, upper_limit;
	unsigned char c;
	size_t n;

	c = *text++;

	n = 0u;
	while (c & 0x80u) {
		c <<= 1;
		n += 1u;
	}

	if (n == 0u) {
		*codepoint_out = (uint32_t)c;
		goto out;
	}
	if (n == 1u || n >= ELEMSOF(limits))
		return 0;

	lower_limit = limits[n - 1u] + 1u;
	upper_limit = limits[n];

	*codepoint_out = (uint32_t)((c & 0xFFu) >> n);

	while (--n) {
		c = *text++;
		if ((c & 0xC0u) != 0x80u)
			return 0;
		*codepoint_out <<= 6;
		*codepoint_out |= (uint32_t)(c & 0x7Fu);
	}

	if (*codepoint_out < lower_limit || *codepoint_out > upper_limit)
		return 0;

out:
	*textp = (const char *)text;
	return 1;
}


static int
jump_to_character(const char *text)
{
#define TEXT_HEAD "Grapheme composition:"
#define CP_HEAD " U+"
#define ELLIPSIS " …"

	uint32_t codepoints[32];
	char *p, composition[sizeof(TEXT_HEAD ELLIPSIS) + ELEMSOF(codepoints) * (sizeof(CP_HEAD"10FFFF") - 1u)];
	size_t i, n = 0u;
	int r, listing, ellipsis;

	if (!text || !*text)
		return 0;

	if (text[grapheme_next_character_break_utf8(text, SIZE_MAX)])
		return 0;

	for (; *text && n < ELEMSOF(codepoints); n++)
		if (!get_codepoint(&text, &codepoints[n]))
			return 0;
	if (!n)
		return 0;
	ellipsis = !!*text;

	if (gcmap_char_table_select_codepoint(GCMAP_CHAR_TABLE(chartable), codepoints[0]))
		goto out;
	listing = gtk_combo_box_get_active(GTK_COMBO_BOX(listcombo));
	select_group_for_codepoint((enum listing)listing, codepoints[0], 0);
	gcmap_char_table_select_codepoint(GCMAP_CHAR_TABLE(chartable), codepoints[0]);

out:
	if (n > 1u) {
		p = stpcpy(composition, TEXT_HEAD);
		for (i = 0u; i < n; i++) {
			r = sprintf(p, "%s%04"PRIX32, CP_HEAD, codepoints[i]);
			if (r < 0) {
				weprintf("sprintf:");
				return 1;
			}
			p = &p[r];
		}
		if (ellipsis)
			stpcpy(composition, ELLIPSIS);
		gtk_statusbar_pop(GTK_STATUSBAR(statusbar), 0u);
		gtk_statusbar_push(GTK_STATUSBAR(statusbar), 0u, composition);
	}

	return 1;

#undef TEXT_HEAD
#undef CP_HEAD
#undef ELLIPSIS
}


static void
chartable_drag_data_received(GtkWidget *widget, GdkDragContext *context, int x, int y,
                             GtkSelectionData *selection_data, unsigned int info, unsigned int time, void *user_data)
{
	unsigned char *text = gtk_selection_data_get_text(selection_data);
	int success = text != NULL;
	(void) widget;
	(void) x;
	(void) y;
	(void) info;
	(void) user_data;
	if (text) {
		success = jump_to_character((char *)text);
		g_free(text);
	}
	gtk_drag_finish(context, success, FALSE, time);
}


static void
chartable_clipboard_text_received(GtkClipboard *clipboard, const char *text, void *user_data)
{
	(void) clipboard;
	(void) user_data;
	if (text)
		jump_to_character(text);
}


static gboolean
chartable_button_press(GtkWidget *widget, GdkEventButton *event, void *user_data)
{
	int meta = !!(event->state & GDK_MOD1_MASK);
	GdkAtom selection = meta ? GDK_SELECTION_SECONDARY : GDK_SELECTION_PRIMARY;
	(void) user_data;
	if (event->button == 2 && event->type == GDK_BUTTON_PRESS) {
		gtk_widget_grab_focus(widget);
		gtk_clipboard_request_text(gtk_clipboard_get(selection), &chartable_clipboard_text_received, widget);
		return TRUE;
	}
	return FALSE;
}


static int
populate_font_families(FilteredCombo *combo, const char **selectedp)
{
	const char **names;
	PangoFontFamily **families;
	int nfamilies_int;
	size_t i, nfamilies;
	size_t selected_index = SIZE_MAX;
	size_t default_index = SIZE_MAX;

	pango_context_list_families(gtk_widget_get_pango_context(GTK_WIDGET(combo)), &families, &nfamilies_int);
	nfamilies = (size_t)nfamilies_int;
	if (nfamilies == 0u)
		eprintf("no fonts available");
	names = ecalloc(nfamilies, sizeof(*names));

	for (i = 0u; i < nfamilies; i++)
		names[i] = pango_font_family_get_name(families[i]);
	libsimple_qsort_str(names, nfamilies);
	for (i = 0u; i < nfamilies; i++) {
		filtered_combo_append(combo, names[i]);
		if (selected_index == SIZE_MAX && !strcasecmp(names[i], *selectedp))
			selected_index = i;
		if (default_index == SIZE_MAX && !strcasecmp(names[i], DEFAULT_FONT_FAMILY))
			default_index = i;
	}

	if (selected_index == SIZE_MAX)
		selected_index = default_index;
	if (selected_index == SIZE_MAX)
		selected_index = 0u;
	*selectedp = names[selected_index];

	free(names);
	g_free(families);

	return (int)selected_index;
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

	index = gtk_tree_path_get_indices(path)[0u];
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
	size_t i, len, off = 0u;
	int r;
	char *ret;

	if (!script->nranges)
		return NULL;

	len = strlen(script->name) + 1u;
	for (i = 0u; i < script->nranges; i++) {
		r = libcmap_sprint_range(NULL, &script->ranges[i], NULL);
		if (r < 0)
			return NULL;
		len += (size_t)r + 2u;
	}

	ret = malloc((size_t)len + 1u);
	if (!ret)
		return NULL;

	off = (size_t)(stpcpy(ret, script->name) - ret);
	ret[off++] = ' ';
	ret[off++] = '(';
	for (i = 0u; i < script->nranges; i++) {
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
grouplist_selection_changed(GtkTreeSelection *selection, void *user_data)
{
	const struct libcmap_range *ranges = &all_block.range;
	size_t nranges = 1u;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	int index;

	(void) user_data;

	if (switching_groups)
		return;

	if (!gtk_tree_selection_get_selected(selection, &model, &iter))
		return;

	path = gtk_tree_model_get_path(model, &iter);
	index = gtk_tree_path_get_indices(path)[0u];
	gtk_tree_path_free(path);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(listcombo))) {
	case BY_SCRIPT:
		if (index-- > 0 && (size_t)index < libcmap_script_list_size) {
			ranges = libcmap_script_list[index].ranges;
			nranges = libcmap_script_list[index].nranges;
		}
		break;

	case BY_BLOCK:
		if (index-- > 0 && (size_t)index < libcmap_block_list_size) {
			ranges = &libcmap_block_list[index].range;
		} else if ((size_t)index == libcmap_block_list_size) {
			ranges = libcmap_no_block.ranges;
			nranges = libcmap_no_block.nranges;
		}
		break;

	default:
		abort();
	}

	gcmap_char_table_set_ranges(GCMAP_CHAR_TABLE(chartable), ranges, nranges);
}


static void
populate_scripts(GtkListStore *store)
{
	const struct libcmap_script *list = libcmap_script_list;
	size_t i, n = libcmap_script_list_size;
	GtkTreeIter iter;

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, "All", -1);
	for (i = 0u; i < n; i++) {
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, list[i].name, -1);
	}

#ifdef HAVE_ITEM_TOOLTIPS
	if (!group_tooltips[BY_SCRIPT]) {
		ngroup_tooltips[BY_SCRIPT] = n + 1u;
		group_tooltips[BY_SCRIPT] = ecalloc(ngroup_tooltips[BY_SCRIPT], sizeof(**group_tooltips));
		group_tooltips[BY_SCRIPT][0u] = block_tooltip(&all_block);
		for (i = 0u; i < n; i++) {
			if (!strcmp(list[i].name, "Unknown"))
				group_tooltips[BY_SCRIPT][i + 1u] = "Unknown (Codepoints not assigned any script)";
			else
				group_tooltips[BY_SCRIPT][i + 1u] = script_tooltip(&list[i]);
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
	for (i = 0u; i < n; i++) {
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, list[i].name, -1);
	}
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, "No Block", -1);

#ifdef HAVE_ITEM_TOOLTIPS
	if (!group_tooltips[BY_BLOCK]) {
		ngroup_tooltips[BY_BLOCK] = n + 2u;
		group_tooltips[BY_BLOCK] = ecalloc(ngroup_tooltips[BY_BLOCK], sizeof(**group_tooltips));
		group_tooltips[BY_BLOCK][0u] = block_tooltip(&all_block);
		group_tooltips[BY_BLOCK][n + 1u] = "No Block (Codepoints not assigned any block)";
		for (i = 0u; i < n; i++)
			group_tooltips[BY_BLOCK][i + 1u] = block_tooltip(&list[i]);
	}
	groups_tooltips = group_tooltips[BY_BLOCK];
	ngroups_tooltips = ngroup_tooltips[BY_BLOCK];
#endif
}


static void
copytext_changed(GtkEditable *editable, GtkWidget *button)
{
	const char *text = gtk_entry_get_text(GTK_ENTRY(editable));
	gtk_widget_set_sensitive(button, !!text[0u]);
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
fontcombo_changed(FilteredCombo *combo, const char *family, void *user_data)
{
	(void) combo;
	(void) user_data;
	gcmap_char_table_set_font_family(GCMAP_CHAR_TABLE(chartable), family);
}


static void
bold_toggle(GtkToggleButton *button, void *user_data)
{
	int selected = gtk_toggle_button_get_active(button);
	(void) user_data;
	gcmap_char_table_set_font_weight(GCMAP_CHAR_TABLE(chartable),
	                                 selected ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
}


static void
italic_toggle(GtkToggleButton *button, void *user_data)
{
	int selected = gtk_toggle_button_get_active(button);
	(void) user_data;
	gcmap_char_table_set_font_style(GCMAP_CHAR_TABLE(chartable),
	                                selected ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
}


static void
fontsize_changed(GtkAdjustment *adjustment, void *user_data)
{
	double size = gtk_adjustment_get_value(adjustment);
	(void) user_data;
	gcmap_char_table_set_font_size(GCMAP_CHAR_TABLE(chartable), (int)size);
}


static gboolean
scroll_grab_focus(GtkWidget *widget, GdkEventScroll *event, void *user_data)
{
	(void) event;
	(void) user_data;
	gtk_widget_grab_focus(widget);
	return FALSE;
}


static gboolean
chartable_scroll(GtkWidget *widget, GdkEventScroll *event, void *user_data)
{
	double size;

	(void) user_data;

	if (!(event->state & GDK_CONTROL_MASK))
		return FALSE;

	if (event->state & (GDK_SHIFT_MASK | GDK_MOD1_MASK | GDK_BUTTON3_MASK))
		return FALSE;

	size = gtk_adjustment_get_value(fontsize_adjustment);
	switch (event->direction) {
	case GDK_SCROLL_UP:    size += fontsize_adjustment->step_increment;  break;
	case GDK_SCROLL_DOWN:  size -= fontsize_adjustment->step_increment;  break;
	case GDK_SCROLL_LEFT:
	case GDK_SCROLL_RIGHT:
	default:
		return FALSE;
	}

	if (size < fontsize_adjustment->lower)  return TRUE;
	if (size > fontsize_adjustment->upper)  return TRUE;

	gcmap_char_table_set_font_size_at_pointer(GCMAP_CHAR_TABLE(widget), (int)size, event->x, event->y);
	gtk_adjustment_set_value(fontsize_adjustment, size);
	return TRUE;
}


static void
listcombo_changed(GtkComboBox *combo, GtkListStore *store)
{
	static int recursion_guard = 0;

	GtkWidget *label;
	int listing;
	int all_selected = 1;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreePath *path;
	GtkTreeIter iter;
	uint32_t codepoint = selected_codepoint;

	if (recursion_guard)
		return;
	recursion_guard++;

	listing = gtk_combo_box_get_active(combo);

	switching_groups++;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(grouplist));
	if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
		path = gtk_tree_model_get_path(model, &iter);
		all_selected = !gtk_tree_path_get_indices(path)[0u];
		gtk_tree_path_free(path);
	}

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

	switching_groups--;

	if (all_selected)
		select_group(0);
	else
		select_group_for_codepoint((enum listing)listing, codepoint, 0);

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


static void
snap2_cols_changed(GtkWidget *menu_item, void *user_data)
{
	(void) user_data;
	gcmap_char_table_set_snap_cols_to_2pow(GCMAP_CHAR_TABLE(chartable),
	                                       gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menu_item)));
}


static void
snap2_rows_changed(GtkWidget *menu_item, void *user_data)
{
	(void) user_data;
	gcmap_char_table_set_snap_rows_to_2pow(GCMAP_CHAR_TABLE(chartable),
	                                       gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menu_item)));
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
go_next(GtkWidget *menu_item, void *user_data)
{
	(void) menu_item;
	(void) user_data;
	gcmap_char_table_adjust_cell_selection(GCMAP_CHAR_TABLE(chartable), +1, 0, 0);
}


static void
go_prev(GtkWidget *menu_item, void *user_data)
{
	(void) menu_item;
	(void) user_data;
	gcmap_char_table_adjust_cell_selection(GCMAP_CHAR_TABLE(chartable), -1, 0, 0);
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
chartable_size_allocate(GtkWidget *widget, GtkAllocation *allocation, void *user_data)
{
	(void) user_data;
	gcmap_char_table_viewport_updated(GCMAP_CHAR_TABLE(widget), allocation->width, allocation->height);
}


static void
create_window(const char *default_font, PangoWeight default_weight, PangoStyle default_style)
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
	int i, n, default_font_index;

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
	/* XXX add options to put some tabs in a side-pane on the right */
	/* XXX add option for shorter tab texts */

	menu = add_menu(menubar, "_View");
	group = NULL;
#define X(ENUM, TITLE, TYPE, SELFUN, POPFUN, ACCEL)\
	bylisting[ENUM] = add_radio_menu_item(menu, TITLE, &group, ACCEL, GDK_CONTROL_MASK, &SELFUN)
	LIST_LISTINGS(X, ;);
#undef X
	/* XXX add option to use character list (show glyphs with names in rows) instead of character grid */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	add_menu_item(menu, "_Larger glyphs", "zoom-in", GDK_plus, GDK_CONTROL_MASK, &zoom_in);
	add_menu_item(menu, "S_maller glyphs", "zoom-out", GDK_minus, GDK_CONTROL_MASK, &zoom_out);
	add_menu_item(menu, "_Normal size", "zoom-original", GDK_equal, GDK_CONTROL_MASK, &zoom_original);
	gtk_accel_group_connect(accelgroup, GDK_0, GDK_CONTROL_MASK, 0,
	                        g_cclosure_new(G_CALLBACK(&zoom_original_accel), NULL, NULL));
	gtk_accel_group_connect(accelgroup, GDK_1, GDK_CONTROL_MASK, 0,
	                        g_cclosure_new(G_CALLBACK(&zoom_original_accel), NULL, NULL));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	snap2_cols = add_check_menu_item(menu, "Snap _columns to power of 2", 0, GDK_KEY_2, GDK_CONTROL_MASK, &snap2_cols_changed);
	snap2_rows = add_check_menu_item(menu, "Snap _rows to power of 2", 0, GDK_KEY_3, GDK_CONTROL_MASK, &snap2_rows_changed);

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

	fontcombo = filtered_combo_new();
	default_font_index = populate_font_families(FILTERED_COMBO(fontcombo), &default_font);
	filtered_combo_set_active(FILTERED_COMBO(fontcombo), default_font_index);
	default_font = filtered_combo_get_selected(FILTERED_COMBO(fontcombo));
	gtk_box_pack_start(GTK_BOX(hbox), fontcombo, FALSE, FALSE, 0);
	accessible = gtk_widget_get_accessible(fontcombo);
	atk_object_set_name(accessible, "Font family");

	bold = gtk_toggle_button_new_with_mnemonic(GTK_STOCK_BOLD);
	gtk_button_set_use_stock(GTK_BUTTON(bold), TRUE);
	gtk_button_set_focus_on_click(GTK_BUTTON(bold), FALSE);
	g_signal_connect(bold, "toggled", G_CALLBACK(&bold_toggle), NULL);
	gtk_box_pack_start(GTK_BOX(hbox), bold, FALSE, FALSE, 0);

	italic = gtk_toggle_button_new_with_mnemonic(GTK_STOCK_ITALIC);
	gtk_button_set_use_stock(GTK_BUTTON(italic), TRUE);
	gtk_button_set_focus_on_click(GTK_BUTTON(italic), FALSE);
	g_signal_connect(italic, "toggled", G_CALLBACK(&italic_toggle), NULL);
	gtk_box_pack_start(GTK_BOX(hbox), italic, FALSE, FALSE, 0);

	obj = gtk_adjustment_new(default_font_size, min_font_size, max_font_size,
	                         small_font_size_increment, big_font_size_increment, 0);
	fontsize_adjustment = GTK_ADJUSTMENT(obj);
	sizespinner = gtk_spin_button_new(fontsize_adjustment, 0, 0);
	g_signal_connect(fontsize_adjustment, "value-changed", G_CALLBACK(&fontsize_changed), NULL);
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
	g_signal_connect(grouplist, "scroll-event", G_CALLBACK(&scroll_grab_focus), NULL);
	g_signal_connect(grouplist, "query-tooltip", G_CALLBACK(&grouplist_query_tooltip), NULL);
	gtk_widget_set_has_tooltip(GTK_WIDGET(grouplist), TRUE);
	g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(grouplist)), "changed",
	                 G_CALLBACK(&grouplist_selection_changed), NULL);
	gtk_container_add(GTK_CONTAINER(scrolled), grouplist);
	gtk_box_pack_start(GTK_BOX(left), scrolled, TRUE, TRUE, 0);

	g_signal_connect(G_OBJECT(listcombo), "changed", G_CALLBACK(&listcombo_changed), store);

	notebook = gtk_notebook_new();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);

	scrolled = logical_scroll_pane_new();
	logical_scroll_pane_set_hscrollbar_visible(LOGICAL_SCROLL_PANE(scrolled), FALSE);
	chartable = gcmap_char_table_new(default_font, (int)default_font_size, default_weight, default_style);
	gtk_drag_dest_set(chartable, GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
	gtk_drag_dest_add_text_targets(chartable);
	g_signal_connect(chartable, "drag-data-received", G_CALLBACK(&chartable_drag_data_received), NULL);
	g_signal_connect(chartable, "button-press-event", G_CALLBACK(&chartable_button_press), NULL);
	g_signal_connect(fontcombo, "changed", G_CALLBACK(&fontcombo_changed), NULL);
	g_signal_connect(chartable, "scroll-event", G_CALLBACK(&scroll_grab_focus), NULL);
	logical_scroll_pane_set_child(LOGICAL_SCROLL_PANE(scrolled), chartable);
	g_signal_connect(chartable, "scroll-event", G_CALLBACK(&chartable_scroll), NULL);
	g_signal_connect(chartable, "size-allocate", G_CALLBACK(&chartable_size_allocate), NULL);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new_with_mnemonic("Character _table"));

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_NONE);
	details = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(details), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(details), FALSE);
	details_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(details));
	tag_display = gtk_text_buffer_create_tag(details_buffer, "display", "scale", 8.0, NULL);
	tag_name = gtk_text_buffer_create_tag(details_buffer, "name", "scale", 1.5, "weight", PANGO_WEIGHT_BOLD, NULL);
	tag_header = gtk_text_buffer_create_tag(details_buffer, "header", "weight", PANGO_WEIGHT_BOLD, NULL);
	gtk_container_add(GTK_CONTAINER(scrolled), details);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new_with_mnemonic("Character _details"));

#ifdef TODO
	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(scrolled), gtk_label_new("todo"));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new_with_mnemonic("Glyph v_ariants"));
#endif

#ifdef TODO
	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(scrolled), gtk_label_new("todo"));
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
		if (*out > ((unsigned int)INT_MAX - digit) / 10u)
			*out = (unsigned int)INT_MAX;
		else
			*out = *out * 10u + digit;
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
	enum listing listing = (enum listing)0;
	int use_bold = 0;
	int use_italic = 0;
	int default_font_size_set = 0;
	int min_font_size_set = 0;
	int max_font_size_set = 0;
	const char *font_family = DEFAULT_FONT_FAMILY;
	char *message;

	ARGBEGIN {
	case 'B':
		listing = BY_BLOCK;
		break;
	case 'S':
		listing = BY_SCRIPT;
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

	gtk_init(NULL, NULL);
	theme = gtk_icon_theme_get_default();
	if (!gtk_icon_theme_has_icon(theme, icon = "apps/gcmap") &&
	    !gtk_icon_theme_has_icon(theme, icon = "gcmap") &&
	    !gtk_icon_theme_has_icon(theme, icon = "apps/accessories-character-map") &&
	    !gtk_icon_theme_has_icon(theme, icon = "accessories-character-map"))
		icon = "gcmap";
	gtk_window_set_default_icon_name(icon);

	create_window(font_family,
	              use_bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
	              use_italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
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

	select_group_for_codepoint(listing, selected_codepoint, 1);
	GCMAP_CHAR_TABLE(chartable)->selection_callback = &character_cell_selected;
	gcmap_char_table_select_codepoint(GCMAP_CHAR_TABLE(chartable), selected_codepoint);

	if (libsimple_asprintf(&message, "Using Unicode %s", libcmap_unicode_version) >= 0 && message) {
		gtk_statusbar_pop(GTK_STATUSBAR(statusbar), 0u);
		gtk_statusbar_push(GTK_STATUSBAR(statusbar), 0u, message);
		free(message);
	} else {
		update_statusbar();
	}
	update_details();

	gtk_window_present(GTK_WINDOW(window));
	gtk_widget_grab_focus(GTK_WIDGET(chartable));
	gtk_main();
}
