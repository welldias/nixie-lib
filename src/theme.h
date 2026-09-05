#ifndef NIXIE_THEME_H
#define NIXIE_THEME_H

#include <nixie/nixie.h>

/* nixie_theme_name_t and nixie_diagram_colors_t are public types, declared
 * in <nixie/nixie.h>: a diagram_colors' NULL fields mean "not set -> derive
 * from bg/fg". bg/fg are always set on every built-in theme; a
 * caller-provided override may leave them NULL too, in which case
 * nixie_resolve_colors() falls back to zinc-light's bg/fg. */

/* Every field always populated -- computed once per render call. Hex strings
 * are "#RRGGBB\0" (7 chars + NUL). */
typedef struct nixie_resolved_colors {
    char bg[8];
    char fg[8];
    char text[8];
    char text_sec[8];
    char text_muted[8];
    char text_faint[8];
    char line[8];
    char arrow[8];
    char node_fill[8];
    char node_stroke[8];
    char group_fill[8];
    char group_hdr[8];
    char inner_stroke[8];
    char key_badge[8];
} nixie_resolved_colors_t;

extern const nixie_diagram_colors_t NIXIE_THEMES[NIXIE_THEME_COUNT];

const nixie_diagram_colors_t *nixie_theme_lookup(nixie_theme_name_t name);

/* Looks up a theme by its beautiful-mermaid-style kebab-case name (e.g.
 * "tokyo-night"). Returns 0 and writes *out on success, -1 if unknown. */
int nixie_theme_lookup_by_name(const char *name, nixie_theme_name_t *out);

/* colors may be NULL, in which case the zinc-light defaults are used. Any
 * NULL field within *colors falls back to the mix-derived value. */
void nixie_resolve_colors(const nixie_diagram_colors_t *colors, nixie_resolved_colors_t *out);

#endif /* NIXIE_THEME_H */
