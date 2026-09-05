#ifndef NIXIE_XYCHART_COLORS_H
#define NIXIE_XYCHART_COLORS_H

#include "../color.h"

/* Default accent for charts when the theme doesn't provide one (blue-500). */
#define NIXIE_CHART_ACCENT_FALLBACK "#3B82F6"

/*
 * Hex color for a chart series index, ported from
 * beautiful-mermaid/src/xychart/colors.ts's getSeriesColor(): index 0 is the
 * accent color as-is; index 1+ alternate darker/lighter shades of the same
 * hue with subtle hue drift, adapting shade direction to whether `bg` is a
 * dark background (so shades stay visible on both light and dark themes).
 * `accent_hex` and `bg_hex` are "#RRGGBB" strings; bg_hex may be NULL.
 */
void nixie_xy_series_color(int index, const char *accent_hex, const char *bg_hex, char out[8]);

#endif /* NIXIE_XYCHART_COLORS_H */
