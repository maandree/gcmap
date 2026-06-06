/* See LICENSE file for copyright and license details. */
#ifndef COMMON_H_
#define COMMON_H_

#include <libsimple.h>
#include <libsimple-arg.h>
#include <libcmap.h>
#if defined(__GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wdeprecated-declarations"
# pragma GCC diagnostic ignored "-Wstrict-prototypes"
#endif
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#if defined(__GNUC__)
# pragma GCC diagnostic pop
#endif

#if GTK_CHECK_VERSION(2, 12, 0)
# define HAVE_ITEM_TOOLTIPS
#endif

#define COMMA ,

#include "char-table.h"

#endif
