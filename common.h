/* See LICENSE file for copyright and license details. */
#ifndef COMMON_H_
#define COMMON_H_

#if defined(__GNUC__)
# pragma GCC diagnostic ignored "-Wunsuffixed-float-constants"
#endif

#include <libsimple.h>
#include <libsimple-arg.h>
#include <libcmap.h>
#include <grapheme.h>
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

#if defined(__GNUC__)
# define PURE __attribute__((__pure__))
# define CONST __attribute__((__const__))
#else
# define PURE
# define CONST
#endif

#define COMMA ,

#include "char-table.h"
#include "filtered-combo.h"
#include "logical-scroll-pane.h"


#define MARK_BASE 0x20u /* 0x25CCu DOTTED CIRCLE || XXX make the a setting */

enum codepoint_class {
	CP_OUT_OF_RANGE,
	CP_CONTROL,
	CP_SURROGATE,
	CP_NONCHARACTER,
	CP_RESERVED, /* includes not yet assigned */
	CP_COMBINING_MARK,
	CP_REGULAR,
};


/* characters.c */
CONST int is_noncharacter(uint32_t cp);
CONST int is_surrogate(uint32_t cp);
CONST int is_valid_codepoint(uint32_t cp);
struct libcmap_codepoint_data *get_codepoint_data(uint32_t cp);
PURE enum codepoint_class classify_from_codepoint_data(const struct libcmap_codepoint_data *data);
enum codepoint_class classify_codepoint(uint32_t cp);

/* colour.c */
double srgb_encode_unsigned(double t);
double srgb_decode_unsigned(double t);
double srgb_blend(double at0, double t, double at1);
void set_source_colour_blend(cairo_t *g, const GdkColor *at0, double t, const GdkColor *at1);
void set_source_colour(cairo_t *g, const GdkColor *colour);

#endif
