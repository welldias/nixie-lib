#include <nixie/nixie.h>

#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "class/layout.h"
#include "class/parser.h"
#include "class/render_ascii.h"
#include "class/render_svg.h"
#include "diagram_type.h"
#include "er/layout.h"
#include "er/parser.h"
#include "er/render_ascii.h"
#include "er/render_svg.h"
#include "flowchart/layout.h"
#include "flowchart/parser.h"
#include "flowchart/render_ascii.h"
#include "flowchart/render_svg.h"
#include "sequence/layout.h"
#include "sequence/parser.h"
#include "sequence/render_ascii.h"
#include "sequence/render_svg.h"
#include "state/parser.h"
#include "svgraster/svg_to_png.h"
#include "theme.h"
#include "xychart/layout.h"
#include "xychart/parser.h"
#include "xychart/render_ascii.h"
#include "xychart/render_svg.h"

const char *nixie_version(void) {
    return "0.1.0";
}

nixie_render_options_t nixie_render_options_default(void) {
    nixie_render_options_t opts;
    opts.theme       = NIXIE_THEME_ZINC_LIGHT;
    opts.colors      = NULL;
    opts.transparent = 0;
    opts.use_unicode = 1;
    opts.font_path   = NULL;
    opts.scale       = 1.0;
    return opts;
}

void nixie_free(char *output) {
    free(output);
}

static nixie_result_t make_error_result(nixie_error_t err, const char *msg, int line) {
    nixie_result_t r;
    r.output     = NULL;
    r.error      = err;
    r.error_line = line;
    if (msg != NULL) {
        strncpy(r.error_message, msg, sizeof(r.error_message) - 1);
        r.error_message[sizeof(r.error_message) - 1] = '\0';
    } else {
        r.error_message[0] = '\0';
    }
    return r;
}

static nixie_result_t make_ok_result(char *output) {
    nixie_result_t r;
    r.output           = output;
    r.error            = NIXIE_OK;
    r.error_message[0] = '\0';
    r.error_line       = -1;
    return r;
}

typedef enum {
    RENDER_TARGET_SVG,
    RENDER_TARGET_ASCII
} render_target_t;

/*
 * Shared parse -> layout -> render pipeline for both public entry points.
 * Every allocation the pipeline makes (parse model, positioned model, ASCII
 * grid scratch space) comes from one arena, destroyed here in a single call
 * regardless of success or failure -- see the project plan's memory-
 * ownership rationale. The returned output string is independently
 * malloc'd (via nixie_strbuf_t), so it outlives the arena.
 */
static nixie_result_t render_dispatch(const char *text, const nixie_render_options_t *opts, render_target_t target) {
    if (text == NULL) {
        return make_error_result(NIXIE_ERROR_INVALID_ARGUMENT, "mermaid_text must not be NULL", -1);
    }

    nixie_render_options_t default_opts = nixie_render_options_default();
    if (opts == NULL) {
        opts = &default_opts;
    }

    nixie_diagram_type_t type = nixie_detect_diagram_type(text);
    if (type != NIXIE_DIAGRAM_FLOWCHART && type != NIXIE_DIAGRAM_STATE && type != NIXIE_DIAGRAM_CLASS && type != NIXIE_DIAGRAM_XYCHART && type != NIXIE_DIAGRAM_ER && type != NIXIE_DIAGRAM_SEQUENCE) {
        const char *msg = (type == NIXIE_DIAGRAM_UNKNOWN) ? "Could not detect a supported diagram type from the input header" : "This diagram type is not implemented yet in this version of nixie";
        return make_error_result(NIXIE_ERROR_UNSUPPORTED_DIAGRAM_TYPE, msg, -1);
    }

    nixie_arena_t *arena = nixie_arena_create(0);
    if (arena == NULL) {
        return make_error_result(NIXIE_ERROR_OUT_OF_MEMORY, "failed to allocate pipeline arena", -1);
    }

    char *output = NULL;

    if (type == NIXIE_DIAGRAM_XYCHART) {
        nixie_xy_parse_result_t parsed = nixie_xy_parse(arena, text);
        if (parsed.error != NIXIE_OK) {
            nixie_result_t r = make_error_result(parsed.error, parsed.error_message, parsed.error_line);
            nixie_arena_destroy(arena);
            return r;
        }

        if (target == RENDER_TARGET_SVG) {
            nixie_positioned_xy_chart_t *pc = nixie_xy_layout(arena, parsed.chart);
            nixie_resolved_colors_t colors;
            const nixie_diagram_colors_t *palette = opts->colors != NULL ? opts->colors : nixie_theme_lookup(opts->theme);
            nixie_resolve_colors(palette, &colors);
            output = nixie_xy_render_svg(pc, &colors, palette->accent, opts->transparent);
        } else {
            nixie_xy_ascii_options_t ascii_opts;
            ascii_opts.use_unicode = opts->use_unicode;
            output                 = nixie_xy_render_ascii(arena, parsed.chart, &ascii_opts);
        }
    } else if (type == NIXIE_DIAGRAM_ER) {
        nixie_er_parse_result_t parsed = nixie_er_parse(arena, text);
        if (parsed.error != NIXIE_OK) {
            nixie_result_t r = make_error_result(parsed.error, parsed.error_message, parsed.error_line);
            nixie_arena_destroy(arena);
            return r;
        }

        nixie_positioned_er_diagram_t *ped = nixie_er_layout(arena, parsed.diagram);

        if (target == RENDER_TARGET_SVG) {
            nixie_resolved_colors_t colors;
            const nixie_diagram_colors_t *palette = opts->colors != NULL ? opts->colors : nixie_theme_lookup(opts->theme);
            nixie_resolve_colors(palette, &colors);
            output = nixie_er_render_svg(ped, &colors, opts->transparent);
        } else {
            nixie_er_ascii_options_t ascii_opts;
            ascii_opts.use_unicode = opts->use_unicode;
            output                 = nixie_er_render_ascii(arena, ped, &ascii_opts);
        }
    } else if (type == NIXIE_DIAGRAM_SEQUENCE) {
        nixie_sequence_parse_result_t parsed = nixie_sequence_parse(arena, text);
        if (parsed.error != NIXIE_OK) {
            nixie_result_t r = make_error_result(parsed.error, parsed.error_message, parsed.error_line);
            nixie_arena_destroy(arena);
            return r;
        }

        if (target == RENDER_TARGET_SVG) {
            nixie_positioned_sequence_diagram_t *psd = nixie_sequence_layout(arena, parsed.diagram);
            nixie_resolved_colors_t colors;
            const nixie_diagram_colors_t *palette = opts->colors != NULL ? opts->colors : nixie_theme_lookup(opts->theme);
            nixie_resolve_colors(palette, &colors);
            output = nixie_sequence_render_svg(psd, &colors, opts->transparent);
        } else {
            nixie_sequence_ascii_options_t ascii_opts;
            ascii_opts.use_unicode = opts->use_unicode;
            output                 = nixie_sequence_render_ascii(arena, parsed.diagram, &ascii_opts);
        }
    } else if (type == NIXIE_DIAGRAM_CLASS) {
        nixie_class_parse_result_t parsed = nixie_class_parse(arena, text);
        if (parsed.error != NIXIE_OK) {
            nixie_result_t r = make_error_result(parsed.error, parsed.error_message, parsed.error_line);
            nixie_arena_destroy(arena);
            return r;
        }

        nixie_positioned_class_diagram_t *pcd = nixie_class_layout(arena, parsed.diagram);

        if (target == RENDER_TARGET_SVG) {
            nixie_resolved_colors_t colors;
            const nixie_diagram_colors_t *palette = opts->colors != NULL ? opts->colors : nixie_theme_lookup(opts->theme);
            nixie_resolve_colors(palette, &colors);
            output = nixie_class_render_svg(pcd, &colors, opts->transparent);
        } else {
            nixie_class_ascii_options_t ascii_opts;
            ascii_opts.use_unicode = opts->use_unicode;
            output                 = nixie_class_render_ascii(arena, pcd, &ascii_opts);
        }
    } else {
        /* Both flowchart and state diagrams produce the same
         * nixie_mm_graph_t, so the rest of the pipeline below (layout,
         * SVG/ASCII rendering) is shared unchanged regardless of which
         * parser ran. */
        nixie_parse_result_t parsed = (type == NIXIE_DIAGRAM_FLOWCHART) ? nixie_flowchart_parse(arena, text) : nixie_state_parse(arena, text);
        if (parsed.error != NIXIE_OK) {
            nixie_result_t r = make_error_result(parsed.error, parsed.error_message, parsed.error_line);
            nixie_arena_destroy(arena);
            return r;
        }

        nixie_positioned_flowchart_t *pf = nixie_flowchart_layout(arena, parsed.graph, NULL);

        if (target == RENDER_TARGET_SVG) {
            nixie_resolved_colors_t colors;
            const nixie_diagram_colors_t *palette = opts->colors != NULL ? opts->colors : nixie_theme_lookup(opts->theme);
            nixie_resolve_colors(palette, &colors);
            output = nixie_flowchart_render_svg(pf, &colors, opts->transparent);
        } else {
            nixie_ascii_options_t ascii_opts;
            ascii_opts.use_unicode = opts->use_unicode;
            output                 = nixie_flowchart_render_ascii(arena, pf, &ascii_opts);
        }
    }

    nixie_arena_destroy(arena);

    if (output == NULL) {
        return make_error_result(NIXIE_ERROR_OUT_OF_MEMORY, "failed to allocate output buffer", -1);
    }

    return make_ok_result(output);
}

nixie_result_t nixie_render_svg(const char *mermaid_text, const nixie_render_options_t *opts) {
    return render_dispatch(mermaid_text, opts, RENDER_TARGET_SVG);
}

nixie_result_t nixie_render_ascii(const char *mermaid_text, const nixie_render_options_t *opts) {
    return render_dispatch(mermaid_text, opts, RENDER_TARGET_ASCII);
}

static nixie_png_result_t make_png_error_result(nixie_error_t err, const char *msg) {
    nixie_png_result_t r;
    r.data       = NULL;
    r.size       = 0;
    r.error      = err;
    r.error_line = -1;
    if (msg != NULL) {
        strncpy(r.error_message, msg, sizeof(r.error_message) - 1);
        r.error_message[sizeof(r.error_message) - 1] = '\0';
    } else {
        r.error_message[0] = '\0';
    }
    return r;
}

nixie_png_result_t nixie_render_png(const char *mermaid_text, const nixie_render_options_t *opts) {
    if (opts == NULL || opts->font_path == NULL) {
        return make_png_error_result(NIXIE_ERROR_INVALID_ARGUMENT, "nixie_render_png requires opts->font_path to point to a .ttf/.otf file");
    }

    nixie_result_t svg = nixie_render_svg(mermaid_text, opts);
    if (svg.error != NIXIE_OK) {
        nixie_png_result_t r = make_png_error_result(svg.error, svg.error_message);
        r.error_line         = svg.error_line;
        return r;
    }

    nixie_png_result_t png = nixie_svg_to_png(svg.output, opts->font_path, opts->scale);
    nixie_free(svg.output);
    return png;
}

nixie_png_result_t nixie_svg_to_png(const char *svg_text, const char *font_path, double scale) {
    if (svg_text == NULL) {
        return make_png_error_result(NIXIE_ERROR_INVALID_ARGUMENT, "svg_text must not be NULL");
    }
    return nixie_svg_to_png_impl(svg_text, font_path, scale);
}

void nixie_free_png(unsigned char *data) {
    free(data);
}
