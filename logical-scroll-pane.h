#include <gtk/gtk.h>

#define LOGICAL_TYPE_SCROLL_PANE (logical_scroll_pane_get_type())
#define LOGICAL_SCROLL_PANE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), LOGICAL_TYPE_SCROLL_PANE, LogicalScrollPane))
#define LOGICAL_IS_SCROLL_PANE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), LOGICAL_TYPE_SCROLL_PANE))

typedef struct _LogicalScrollPane      LogicalScrollPane;
typedef struct _LogicalScrollPaneClass LogicalScrollPaneClass;

struct _LogicalScrollPane {
	GtkBin parent_instance;

	GtkWidget *child;
	GtkWidget *hscrollbar;
	GtkWidget *vscrollbar;

	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;

	int content_width;
	int content_height;
	int page_width;
	int page_height;
	int viewport_size_is_logical;
	int hscrollbar_visible;
	int vscrollbar_visible;
};

struct _LogicalScrollPaneClass {
	GtkBinClass parent_class;
};

PURE GType logical_scroll_pane_get_type(void);

GtkWidget *logical_scroll_pane_new(void);

void logical_scroll_pane_set_child(LogicalScrollPane *this, GtkWidget *child);
PURE GtkWidget *logical_scroll_pane_get_child(LogicalScrollPane *this);

void logical_scroll_pane_set_content_size(LogicalScrollPane *this, int width, int height);
void logical_scroll_pane_set_viewport_size(LogicalScrollPane *this, int width, int height);

void logical_scroll_pane_set_hscrollbar(LogicalScrollPane *this, GtkWidget *scrollbar);
void logical_scroll_pane_set_vscrollbar(LogicalScrollPane *this, GtkWidget *scrollbar);

PURE GtkWidget *logical_scroll_pane_get_hscrollbar(LogicalScrollPane *this);
PURE GtkWidget *logical_scroll_pane_get_vscrollbar(LogicalScrollPane *this);

void logical_scroll_pane_set_hscrollbar_visible(LogicalScrollPane *this, int visible);
void logical_scroll_pane_set_vscrollbar_visible(LogicalScrollPane *this, int visible);

PURE int logical_scroll_pane_get_hscrollbar_visible(LogicalScrollPane *this);
PURE int logical_scroll_pane_get_vscrollbar_visible(LogicalScrollPane *this);

void logical_scroll_pane_set_hadjustment(LogicalScrollPane *this, GtkAdjustment *adjustment);
void logical_scroll_pane_set_vadjustment(LogicalScrollPane *this, GtkAdjustment *adjustment);

PURE GtkAdjustment *logical_scroll_pane_get_hadjustment(LogicalScrollPane *this);
PURE GtkAdjustment *logical_scroll_pane_get_vadjustment(LogicalScrollPane *this);
