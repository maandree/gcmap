/* See LICENSE file for copyright and license details. */
#include "common.h"


#define GCMAP_TYPE_CHAR_TABLE            (gcmap_char_table_get_type())
#define GCMAP_CHAR_TABLE(OBJ)            (G_TYPE_CHECK_INSTANCE_CAST((OBJ), GCMAP_TYPE_CHAR_TABLE, CharTable))
#define GCMAP_CHAR_TABLE_CLASS(CLASS)    (G_TYPE_CHECK_CLASS_CAST((CLASS), GCMAP_TYPE_CHAR_TABLE, CharTableClass))
#define IS_GCMAP_CHAR_TABLE(OBJ)         (G_TYPE_CHECK_INSTANCE_TYPE((OBJ), GCMAP_TYPE_CHAR_TABLE))
#define IS_GCMAP_CHAR_TABLE_CLASS(CLASS) (G_TYPE_CHECK_CLASS_TYPE((CLASS), GCMAP_TYPE_CHAR_TABLE))
#define GCMAP_CHAR_TABLE_GET_CLASS(OBJ)  (G_TYPE_INSTANCE_GET_CLASS((OBJ), GCMAP_TYPE_CHAR_TABLE, CharTableClass))


typedef struct _CharTable {
	GtkDrawingArea parent_instance;
	int nrows, ncols;
	int max_w, max_h;
	int cell_h;
	uint32_t nchars, selected;
	int viewport_w, viewport_h;
	PangoFontDescription *font;
	PangoLayout *layout;
	int font_size;
	int snap_cols_to_2pow;
	int snap_rows_to_2pow;
	unsigned multiclick_count;
	PangoWeight font_weight;
	PangoStyle font_style;
	const struct libcmap_range *ranges;
	size_t nranges;
	void (*selection_callback)(uint32_t index, uint32_t codepoint, int meta, int copy_to_text_field);
} CharTable;

typedef struct _CharTableClass {
	GtkDrawingAreaClass parent_class;
} CharTableClass;


GType gcmap_char_table_get_type(void);
GtkWidget *gcmap_char_table_new(const char *font_family, int font_size, PangoWeight font_weight, PangoStyle font_style);

void gcmap_char_table_set_font_family(CharTable *this, const char *family);
void gcmap_char_table_set_font_weight(CharTable *this, PangoWeight weight);
void gcmap_char_table_set_font_style(CharTable *this, PangoStyle style);
void gcmap_char_table_set_font_size(CharTable *this, int size);
void gcmap_char_table_set_snap_cols_to_2pow(CharTable *this, int active);
void gcmap_char_table_set_snap_rows_to_2pow(CharTable *this, int active);
void gcmap_char_table_set_ranges(CharTable *this, const struct libcmap_range *ranges, size_t nranges);
void gcmap_char_table_viewport_updated(CharTable *this, int viewport_w, int viewport_h);
void gcmap_char_table_select_cell(CharTable *this, int32_t selected, int clamp, int meta);
void gcmap_char_table_adjust_cell_selection(CharTable *this, int32_t adjustment, int clamp, int meta);
int gcmap_char_table_select_codepoint(CharTable *this, uint32_t codepoint);
