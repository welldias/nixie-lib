#include "theme.h"

#include <string.h>

#include "color.h"

/* Mix percentages ported verbatim from beautiful-mermaid/src/theme.ts's MIX
 * object (fg mixed into bg at N%). MIX.text (100) is not used as a mix -- the
 * primary text color is just fg directly. */
enum {
    MIX_TEXT_SEC     = 60,
    MIX_TEXT_MUTED   = 40,
    MIX_TEXT_FAINT   = 25,
    MIX_LINE         = 50,
    MIX_ARROW        = 85,
    MIX_NODE_FILL    = 3,
    MIX_NODE_STROKE  = 20,
    MIX_GROUP_HEADER = 5,
    MIX_INNER_STROKE = 12,
    MIX_KEY_BADGE    = 10
};

/* 15 built-in palettes, hex values ported verbatim from
 * beautiful-mermaid/src/theme.ts's THEMES object. */
const nixie_diagram_colors_t NIXIE_THEMES[NIXIE_THEME_COUNT] = {
    [NIXIE_THEME_ZINC_LIGHT] = {
        .bg = "#FFFFFF", .fg = "#27272A",
        .line = NULL, .accent = NULL, .muted = NULL, .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_ZINC_DARK] = {
        .bg = "#18181B", .fg = "#FAFAFA",
        .line = NULL, .accent = NULL, .muted = NULL, .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_TOKYO_NIGHT] = {
        .bg = "#1a1b26", .fg = "#a9b1d6",
        .line = "#3d59a1", .accent = "#7aa2f7", .muted = "#565f89", .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_TOKYO_NIGHT_STORM] = {
        .bg = "#24283b", .fg = "#a9b1d6",
        .line = "#3d59a1", .accent = "#7aa2f7", .muted = "#565f89", .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_TOKYO_NIGHT_LIGHT] = {
        .bg = "#d5d6db", .fg = "#343b58",
        .line = "#34548a", .accent = "#34548a", .muted = "#9699a3", .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_CATPPUCCIN_MOCHA] = {
        .bg = "#1e1e2e", .fg = "#cdd6f4",
        .line = "#585b70", .accent = "#cba6f7", .muted = "#6c7086", .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_CATPPUCCIN_LATTE] = {
        .bg = "#eff1f5", .fg = "#4c4f69",
        .line = "#9ca0b0", .accent = "#8839ef", .muted = "#9ca0b0", .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_NORD] = {
        .bg = "#2e3440", .fg = "#d8dee9",
        .line = "#4c566a", .accent = "#88c0d0", .muted = "#616e88", .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_NORD_LIGHT] = {
        .bg = "#eceff4", .fg = "#2e3440",
        .line = "#aab1c0", .accent = "#5e81ac", .muted = "#7b88a1", .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_DRACULA] = {
        .bg = "#282a36", .fg = "#f8f8f2",
        .line = "#6272a4", .accent = "#bd93f9", .muted = "#6272a4", .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_GITHUB_LIGHT] = {
        .bg = "#ffffff", .fg = "#1f2328",
        .line = "#d1d9e0", .accent = "#0969da", .muted = "#59636e", .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_GITHUB_DARK] = {
        .bg = "#0d1117", .fg = "#e6edf3",
        .line = "#3d444d", .accent = "#4493f8", .muted = "#9198a1", .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_SOLARIZED_LIGHT] = {
        .bg = "#fdf6e3", .fg = "#657b83",
        .line = "#93a1a1", .accent = "#268bd2", .muted = "#93a1a1", .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_SOLARIZED_DARK] = {
        .bg = "#002b36", .fg = "#839496",
        .line = "#586e75", .accent = "#268bd2", .muted = "#586e75", .surface = NULL, .border = NULL,
    },
    [NIXIE_THEME_ONE_DARK] = {
        .bg = "#282c34", .fg = "#abb2bf",
        .line = "#4b5263", .accent = "#c678dd", .muted = "#5c6370", .surface = NULL, .border = NULL,
    },
    [NINIE_THEM_COFFEE_BEAN] = {
        .bg = "#f3e9dc", .fg = "#5e3023",
        .line = "#644239", .accent = "#856258", .muted = "#5e3023", .surface = NULL, .border = NULL,
    },
};

static const char *NIXIE_THEME_NAMES[NIXIE_THEME_COUNT] = {
    "zinc-light",
    "zinc-dark",
    "tokyo-night",
    "tokyo-night-storm",
    "tokyo-night-light",
    "catppuccin-mocha",
    "catppuccin-latte",
    "nord",
    "nord-light",
    "dracula",
    "github-light",
    "github-dark",
    "solarized-light",
    "solarized-dark",
    "one-dark",
    "coffee-bean",
};

const nixie_diagram_colors_t *nixie_theme_lookup(nixie_theme_name_t name) {
    if (name < 0 || name >= NIXIE_THEME_COUNT) {
        return &NIXIE_THEMES[NIXIE_THEME_ZINC_LIGHT];
    }
    return &NIXIE_THEMES[name];
}

int nixie_theme_lookup_by_name(const char *name, nixie_theme_name_t *out) {
    if (name == NULL) {
        return -1;
    }
    for (int i = 0; i < NIXIE_THEME_COUNT; i++) {
        if (strcmp(name, NIXIE_THEME_NAMES[i]) == 0) {
            if (out != NULL) {
                *out = (nixie_theme_name_t)i;
            }
            return 0;
        }
    }
    return -1;
}

static void resolve_hex(const char *value, nixie_rgb_t fallback, char out[8]) {
    nixie_rgb_t rgb;
    if (value != NULL && nixie_parse_hex(value, &rgb) == 0) {
        nixie_format_hex(rgb, out);
        return;
    }
    nixie_format_hex(fallback, out);
}

void nixie_resolve_colors(const nixie_diagram_colors_t *colors, nixie_resolved_colors_t *out) {
    const nixie_diagram_colors_t *defaults = &NIXIE_THEMES[NIXIE_THEME_ZINC_LIGHT];
    const char *bg_str                     = (colors != NULL && colors->bg != NULL) ? colors->bg : defaults->bg;
    const char *fg_str                     = (colors != NULL && colors->fg != NULL) ? colors->fg : defaults->fg;
    const char *line_str                   = (colors != NULL) ? colors->line : NULL;
    const char *accent_str                 = (colors != NULL) ? colors->accent : NULL;
    const char *muted_str                  = (colors != NULL) ? colors->muted : NULL;
    const char *surface_str                = (colors != NULL) ? colors->surface : NULL;
    const char *border_str                 = (colors != NULL) ? colors->border : NULL;

    nixie_rgb_t bg = { 255, 255, 255 };
    nixie_rgb_t fg = { 0x27, 0x27, 0x2a };
    nixie_parse_hex(bg_str, &bg);
    nixie_parse_hex(fg_str, &fg);

    nixie_format_hex(bg, out->bg);
    nixie_format_hex(fg, out->fg);

    /* --_text: always fg directly. */
    nixie_format_hex(fg, out->text);

    /* --_text-sec / --_text-muted: muted override wins outright if set,
     * otherwise mix at their respective percentages. */
    if (muted_str != NULL) {
        resolve_hex(muted_str, fg, out->text_sec);
        resolve_hex(muted_str, fg, out->text_muted);
    } else {
        nixie_format_hex(nixie_mix_rgb(fg, bg, MIX_TEXT_SEC), out->text_sec);
        nixie_format_hex(nixie_mix_rgb(fg, bg, MIX_TEXT_MUTED), out->text_muted);
    }

    nixie_format_hex(nixie_mix_rgb(fg, bg, MIX_TEXT_FAINT), out->text_faint);

    if (line_str != NULL) {
        resolve_hex(line_str, fg, out->line);
    } else {
        nixie_format_hex(nixie_mix_rgb(fg, bg, MIX_LINE), out->line);
    }

    if (accent_str != NULL) {
        resolve_hex(accent_str, fg, out->arrow);
    } else {
        nixie_format_hex(nixie_mix_rgb(fg, bg, MIX_ARROW), out->arrow);
    }

    if (surface_str != NULL) {
        resolve_hex(surface_str, fg, out->node_fill);
    } else {
        nixie_format_hex(nixie_mix_rgb(fg, bg, MIX_NODE_FILL), out->node_fill);
    }

    if (border_str != NULL) {
        resolve_hex(border_str, fg, out->node_stroke);
    } else {
        nixie_format_hex(nixie_mix_rgb(fg, bg, MIX_NODE_STROKE), out->node_stroke);
    }

    /* --_group-fill: always bg directly. */
    nixie_format_hex(bg, out->group_fill);

    nixie_format_hex(nixie_mix_rgb(fg, bg, MIX_GROUP_HEADER), out->group_hdr);
    nixie_format_hex(nixie_mix_rgb(fg, bg, MIX_INNER_STROKE), out->inner_stroke);
    nixie_format_hex(nixie_mix_rgb(fg, bg, MIX_KEY_BADGE), out->key_badge);
}
