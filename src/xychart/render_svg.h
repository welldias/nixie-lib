#ifndef NIXIE_XYCHART_RENDER_SVG_H
#define NIXIE_XYCHART_RENDER_SVG_H

#include "../theme.h"
#include "positioned.h"

/*
 * `raw_accent_hex` is the theme's RAW (pre-derivation) accent color --
 * chart series colors are shades/hue-drifts of this specific value (see
 * xychart/colors.h), not the "arrow" color derived elsewhere in
 * nixie_resolved_colors_t (which is mixed differently and serves a
 * different visual role). May be NULL, in which case
 * NIXIE_CHART_ACCENT_FALLBACK is used, matching beautiful-mermaid.
 *
 * Returns a malloc'd SVG string; free with nixie_free() (== free()).
 */
char *nixie_xy_render_svg(const nixie_positioned_xy_chart_t *pc, const nixie_resolved_colors_t *colors, const char *raw_accent_hex, int transparent);

#endif /* NIXIE_XYCHART_RENDER_SVG_H */
