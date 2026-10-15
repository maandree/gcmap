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
	uint32_t nchars;
	int viewport_w, viewport_h;
	PangoFontDescription *font;
	PangoLayout *layout;
	int font_size;
	PangoWeight font_weight;
	PangoStyle font_style;
	const struct libcmap_range *ranges;
	size_t nranges;
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
void gcmap_char_table_set_ranges(CharTable *this, const struct libcmap_range *ranges, size_t nranges);
void gcmap_char_table_viewport_updated(CharTable *this, int viewport_w, int viewport_h);
