/* See LICENSE file for copyright and license details. */
#ifndef COMMON_H_
#define COMMON_H_

#if defined(__GNUC__)
# pragma GCC diagnostic ignored "-Wunsuffixed-float-constants"
#endif

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
#include <math.h>

#if GTK_CHECK_VERSION(2, 12, 0)
# define HAVE_ITEM_TOOLTIPS
#endif

#define COMMA ,

#include "char-table.h"

/* colour.c */
double srgb_encode_unsigned(double t);
double srgb_decode_unsigned(double t);
double srgb_blend(double at0, double t, double at1);
void set_source_colour_blend(cairo_t *g, const GdkColor *at0, double t, const GdkColor *at1);
void set_source_colour(cairo_t *g, const GdkColor *colour);

#endif
