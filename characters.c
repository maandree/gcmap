/* See LICENSE file for copyright and license details. */
#include "common.h"


int
is_noncharacter(uint32_t cp)
{
	return (0xFDD0u <= cp && cp <= 0xFDEFu) || (cp & 0xFFFEu) == 0xFFFEu;
}


int
is_surrogate(uint32_t cp)
{
	return 0xD800u <= cp && cp <= 0xDFFFu;
}


int
is_valid_codepoint(uint32_t cp)
{
	return cp <= LIBCMAP_ULTIMATE_CODEPOINT && !is_noncharacter(cp) && !is_surrogate(cp);
}


struct libcmap_codepoint_data *
get_codepoint_data(uint32_t cp)
{
	static char *databuf = NULL;
	static size_t databufsize = 0u;
	static struct libcmap_codepoint_data data;

	int r;

again:
	r = libcmap_get_codepoint_data(cp, &data, databuf, &databufsize);
	if (r <= 0) {
		if (!r || errno != ERANGE || !databufsize)
			return NULL;
		databuf = erealloc(databuf, databufsize);
		goto again;
	}

	return &data;
}


enum codepoint_class
classify_from_codepoint_data(const struct libcmap_codepoint_data *data)
{
	if (!data)
		return CP_OUT_OF_RANGE;

	switch ((int)(data->general_category & LIBCMAP_GENERAL_SUPERCATEGORY_MASK)) {
	case LIBCMAP_GENERAL_SUPERCATEGORY_MARK:
		return CP_COMBINING_MARK;
	default:
		break;
	}

	switch ((int)data->general_category) {
	case LIBCMAP_GENERAL_CATEGORY_OTHER_CONTROL:
		return CP_CONTROL;
	case LIBCMAP_GENERAL_CATEGORY_OTHER_SURROGATE:
		return CP_SURROGATE;
	case LIBCMAP_GENERAL_CATEGORY_OTHER_NOT_ASSIGNED:
		return data->noncharacter ? CP_NONCHARACTER : CP_RESERVED;
	default:
		break;
	}

	return CP_REGULAR;
}


enum codepoint_class
classify_codepoint(uint32_t cp)
{
	return classify_from_codepoint_data(get_codepoint_data(cp));
}


int
is_in_script(uint32_t cp, const struct libcmap_script *script)
{
	size_t i;
	if (!script->nranges)
		return 0;
	if (script->ranges[0].first > cp)
		return 0;
	if (cp > script->ranges[script->nranges - 1u].last)
		return 0;
	for (i = 0u; i < script->nranges; i++)
		if (script->ranges[i].first <= cp && cp <= script->ranges[i].last)
			return 1;
	return 0;
}
