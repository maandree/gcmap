/* See LICENSE file for copyright and license details. */
#include "common.h"


#define GCMAP_TYPE_CHAR_TABLE            (gcmap_char_table_get_type())
#define GCMAP_TYPE_CHAR(OBJ)             (G_TYPE_CHECK_INSTANCE_CAST((OBJ), GCMAP_TYPE_CHAR_TABLE, CharTable))
#define GCMAP_CHAR_TABLE_CLASS(CLASS)    (G_TYPE_CHECK_CLASS_CAST((CLASS), GCMAP_TYPE_CHAR_TABLE, CharTableClass))
#define IS_GCMAP_CHAR_TABLE(OBJ)         (G_TYPE_CHECK_INSTANCE_TYPE((OBJ), GCMAP_TYPE_CHAR_TABLE))
#define IS_GCMAP_CHAR_TABLE_CLASS(CLASS) (G_TYPE_CHECK_CLASS_TYPE((CLASS), GCMAP_TYPE_CHAR_TABLE))
#define GCMAP_CHAR_TABLE_GET_CLASS(OBJ)  (G_TYPE_INSTANCE_GET_CLASS((OBJ), GCMAP_TYPE_CHAR_TABLE, CharTableClass))


typedef struct _CharTable {
	GtkDrawingArea parent_instance;
	int nrows, ncols, max_w, max_h;
	PangoFontDescription *font;
	PangoLayout *layout;
} CharTable;

typedef struct _CharTableClass {
	GtkDrawingAreaClass parent_class;
} CharTableClass;


GType gcmap_char_table_get_type(void);
GtkWidget *gcmap_char_table_new(void);
