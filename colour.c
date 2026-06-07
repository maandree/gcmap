/* See LICENSE file for copyright and license details. */
#include "common.h"


double
srgb_encode_unsigned(double t)
{
	return t <= 0.0031306684425217108
		? 12.92 * t
		: fma(1.055, pow(t, 1 / 2.4), -0.055);
}

double
srgb_decode_unsigned(double t)
{
	return t <= 0.0031306684425217108 * 12.92
		? t / 12.92
		: pow((t + 0.055) / 1.055, 2.4);
}


double
srgb_blend(double at0, double t, double at1)
{
	at0 = srgb_decode_unsigned(at0);
	at1 = srgb_decode_unsigned(at1);
	return srgb_encode_unsigned(fma((1 - t), at0, t * at1));
}


void
set_source_colour_blend(cairo_t *g, const GdkColor *at0, double t, const GdkColor *at1)
{
	double max   = (double)UINT16_MAX;
	double red   = srgb_blend(at0->red   / max, t, at1->red   / max);
	double green = srgb_blend(at0->green / max, t, at1->green / max);
	double blue  = srgb_blend(at0->blue  / max, t, at1->blue  / max);
	cairo_set_source_rgb(g, red, green, blue);
}


void
set_source_colour(cairo_t *g, const GdkColor *colour)
{
	double max = (double)UINT16_MAX;
	cairo_set_source_rgb(g, colour->red / max, colour->green / max, colour->blue / max);
}
