#ifndef NIXIE_H
#define NIXIE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * nixie -- a pure C99 library that parses Mermaid-style diagram text and
 * renders it to an SVG string or an ASCII/Unicode-art string.
 *
 * Usage is strictly "parse once, render once, discard": there is no
 * long-lived document handle to manage. Call nixie_render_svg() or
 * nixie_render_ascii() with the diagram text and (optionally) render
 * options, check result.error, use result.output, and free it with
 * nixie_free() when done.
 */

const char *nixie_version(void);

/* ==========================================================================
 * Errors
 * ========================================================================== */

typedef enum nixie_error {
    NIXIE_OK = 0,
    NIXIE_ERROR_EMPTY_INPUT,
    NIXIE_ERROR_UNKNOWN_HEADER,
    NIXIE_ERROR_PARSE,
    NIXIE_ERROR_UNSUPPORTED_DIAGRAM_TYPE,
    NIXIE_ERROR_INVALID_ARGUMENT,
    NIXIE_ERROR_OUT_OF_MEMORY
} nixie_error_t;

/* ==========================================================================
 * Theming
 * ========================================================================== */

typedef enum nixie_theme_name {
    NIXIE_THEME_ZINC_LIGHT = 0,
    NIXIE_THEME_ZINC_DARK,
    NIXIE_THEME_TOKYO_NIGHT,
    NIXIE_THEME_TOKYO_NIGHT_STORM,
    NIXIE_THEME_TOKYO_NIGHT_LIGHT,
    NIXIE_THEME_CATPPUCCIN_MOCHA,
    NIXIE_THEME_CATPPUCCIN_LATTE,
    NIXIE_THEME_NORD,
    NIXIE_THEME_NORD_LIGHT,
    NIXIE_THEME_DRACULA,
    NIXIE_THEME_GITHUB_LIGHT,
    NIXIE_THEME_GITHUB_DARK,
    NIXIE_THEME_SOLARIZED_LIGHT,
    NIXIE_THEME_SOLARIZED_DARK,
    NIXIE_THEME_ONE_DARK,
    NINIE_THEM_COFFEE_BEAN,
    NIXIE_THEME_COUNT
} nixie_theme_name_t;

/*
 * Diagram color palette. bg/fg are the only colors most callers need; the
 * rest (line/accent/muted/surface/border) are optional enrichment -- a NULL
 * field is derived from bg+fg at render time. This mirrors the 15 built-in
 * NIXIE_THEME_* palettes and lets a caller build a fully custom palette the
 * same way.
 */
typedef struct nixie_diagram_colors {
    const char *bg;      /* required (hex, e.g. "#1a1b26") */
    const char *fg;      /* required */
    const char *line;    /* optional: edge/connector color */
    const char *accent;  /* optional: arrow heads, highlights */
    const char *muted;   /* optional: secondary text, edge labels */
    const char *surface; /* optional: node/box fill tint */
    const char *border;  /* optional: node/group stroke color */
} nixie_diagram_colors_t;

/* ==========================================================================
 * Rendering
 * ========================================================================== */

typedef struct nixie_render_options {
    /* One of the 15 built-in themes; ignored if `colors` is non-NULL. */
    nixie_theme_name_t theme;

    /* Explicit color override. When non-NULL, takes priority over `theme`. */
    const nixie_diagram_colors_t *colors;

    /* SVG only: omit the background fill so the SVG composites over
     * whatever is behind it. */
    int transparent;

    /* ASCII only: use Unicode box-drawing glyphs (e.g. U+250C) instead of
     * plain ASCII ('+', '-', '|'). */
    int use_unicode;

    /* PNG only: path to a .ttf/.otf font file used to rasterize all text in
     * the image. Required (non-NULL) for nixie_render_png() -- NULL is a
     * NIXIE_ERROR_INVALID_ARGUMENT, never a silent fallback, since nixie
     * does not embed a font. Ignored by nixie_render_svg()/nixie_render_ascii(). */
    const char *font_path;

    /* PNG only: output-resolution multiplier (e.g. 2.0 for @2x output).
     * Values <= 0 are treated as 1.0. Ignored by SVG/ASCII renders. */
    double scale;
} nixie_render_options_t;

nixie_render_options_t nixie_render_options_default(void);

typedef struct nixie_result {
    char *output;             /* NULL on error; owned by caller, free with nixie_free() */
    nixie_error_t error;      /* NIXIE_OK on success */
    char error_message[256];  /* empty string on success */
    int error_line;           /* 1-based source line if applicable, else -1 */
} nixie_result_t;

/* opts may be NULL to use nixie_render_options_default(). */
nixie_result_t nixie_render_svg(const char *mermaid_text, const nixie_render_options_t *opts);
nixie_result_t nixie_render_ascii(const char *mermaid_text, const nixie_render_options_t *opts);

void nixie_free(char *output);

/* ==========================================================================
 * PNG rendering
 * ========================================================================== */

/*
 * Binary result of a PNG render: unlike nixie_result_t, `data` is NOT a
 * NUL-terminated string -- it is `size` raw bytes of PNG file content and
 * must be freed with nixie_free_png() (not nixie_free()).
 */
typedef struct nixie_png_result {
    unsigned char *data;      /* NULL on error; owned by caller */
    size_t size;               /* byte length of `data`; 0 on error */
    nixie_error_t error;      /* NIXIE_OK on success */
    char error_message[256];  /* empty string on success */
    int error_line;           /* 1-based source line if applicable, else -1 */
} nixie_png_result_t;

/*
 * opts must be non-NULL with opts->font_path set to a readable .ttf/.otf
 * file -- every diagram nixie renders includes text, and nixie does not
 * embed a fallback font, so a NULL opts or NULL opts->font_path is a
 * NIXIE_ERROR_INVALID_ARGUMENT. opts->scale (default 1.0) multiplies the
 * SVG's pixel dimensions to produce a higher-resolution raster (e.g. 2.0
 * for @2x output).
 *
 * Internally this calls nixie_render_svg() and feeds the resulting SVG
 * text through nixie_svg_to_png().
 */
nixie_png_result_t nixie_render_png(const char *mermaid_text, const nixie_render_options_t *opts);

/*
 * Standalone SVG-to-PNG utility: rasterizes an arbitrary, already-built SVG
 * document (not necessarily produced by nixie) into PNG bytes. Only a
 * bounded subset of SVG is supported -- the subset nixie's own renderers
 * emit (rect/circle/ellipse/line/polyline/polygon/path with M/L/Q/C/Z
 * commands/text/tspan/g/defs/marker, solid #RRGGBB or #RGB colors, no
 * gradients/clip-paths/images/arcs).
 *
 * font_path must point to a readable .ttf/.otf file if the SVG contains
 * any <text> elements; it may be NULL only when the SVG has none.
 * scale defaults to 1.0 when <= 0.
 */
nixie_png_result_t nixie_svg_to_png(const char *svg_text, const char *font_path, double scale);

void nixie_free_png(unsigned char *data);

#ifdef __cplusplus
}
#endif

#endif /* NIXIE_H */
