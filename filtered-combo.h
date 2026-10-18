#include <gtk/gtk.h>

#define FILTERED_TYPE_COMBO (filtered_combo_get_type())
#define FILTERED_COMBO(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), FILTERED_TYPE_COMBO, FilteredCombo))
#define FILTERED_IS_COMBO(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), FILTERED_TYPE_COMBO))

typedef struct _FilteredCombo FilteredCombo;
typedef struct _FilteredComboClass FilteredComboClass;

struct _FilteredCombo {
	GtkHBox parent_instance;
	GtkWidget *combo;
	GtkWidget *entry;
	GtkListStore *store;
	GtkEntryCompletion *completion;
	char *selected;
	GtkWidget *popup;
	unsigned int reposition_source;
	int popup_x;
	int popup_y;
	int popup_width;
};

struct _FilteredComboClass {
	GtkHBoxClass parent_class;
};

GType filtered_combo_get_type(void);
GtkWidget *filtered_combo_new(void);
void filtered_combo_append(FilteredCombo *this, const char *value);
void filtered_combo_set_active(FilteredCombo *this, int index);
PURE const char *filtered_combo_get_selected(FilteredCombo *this);
