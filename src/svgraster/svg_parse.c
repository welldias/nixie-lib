#include "svg_parse.h"

#include <stdlib.h>
#include <string.h>

#include "../color.h"
#include "svg_path.h"

/* ==========================================================================
 * Small affine helper (translate + uniform-ish scale). SVG's
 * transform="translate(tx,ty) scale(s)" always applies scale first (inner)
 * then translate (outer) -- which is exactly what {tx,ty,sx,sy} represents
 * as a single operation: out = in*s + t. rotate() cannot be folded into
 * this representation, so it is tracked separately and only ever baked
 * into TEXT shapes (see NIXIE_SVG_SHAPE_TEXT handling below) since that's
 * the only element nixie's renderers ever rotate.
 * ========================================================================== */

typedef struct nixie_affine {
    double tx, ty, sx, sy;
} nixie_affine_t;

static nixie_affine_t affine_identity(void) {
    nixie_affine_t a = { 0.0, 0.0, 1.0, 1.0 };
    return a;
}

/* Returns the affine equivalent to applying `inner` then `outer`. */
static nixie_affine_t affine_compose(nixie_affine_t outer, nixie_affine_t inner) {
    nixie_affine_t r;
    r.sx = inner.sx * outer.sx;
    r.sy = inner.sy * outer.sy;
    r.tx = inner.tx * outer.sx + outer.tx;
    r.ty = inner.ty * outer.sy + outer.ty;
    return r;
}

static nixie_point_t affine_apply(nixie_affine_t a, nixie_point_t p) {
    nixie_point_t r;
    r.x = p.x * a.sx + a.tx;
    r.y = p.y * a.sy + a.ty;
    return r;
}

/* ==========================================================================
 * Minimal tag tokenizer. nixie's own SVG output always XML-escapes text
 * content and attribute values (&amp; &lt; &gt; &quot;), so a literal '<'
 * or '>' never appears except as a structural tag delimiter -- meaning a
 * plain strchr-based scan is safe and correct for this bounded generator
 * (not a general XML parser).
 * ========================================================================== */

typedef struct tag_info {
    int is_end;
    int self_closing;
    const char *name;
    size_t name_len;
    const char *attrs;
    size_t attrs_len;
    const char *tag_start; /* position of '<' */
    const char *after;     /* position right after this tag's '>' */
} tag_info_t;

static int is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int next_tag(const char *p, tag_info_t *out) {
    const char *lt = strchr(p, '<');
    if (lt == NULL)
        return 0;
    const char *gt = strchr(lt, '>');
    if (gt == NULL)
        return 0;

    const char *q = lt + 1;
    out->is_end   = 0;
    if (*q == '/') {
        out->is_end = 1;
        q++;
    }
    const char *name_start = q;
    while (q < gt && !is_ws(*q) && *q != '/' && *q != '>')
        q++;
    out->name     = name_start;
    out->name_len = (size_t)(q - name_start);
    while (q < gt && is_ws(*q))
        q++;
    out->attrs = q;

    const char *r     = gt - 1;
    out->self_closing = (r > lt && *r == '/');
    out->attrs_len    = (size_t)((out->self_closing ? r : gt) - out->attrs);
    out->tag_start    = lt;
    out->after        = gt + 1;
    return 1;
}

static int tag_name_is(const tag_info_t *t, const char *name) {
    size_t nlen = strlen(name);
    return t->name_len == nlen && strncmp(t->name, name, nlen) == 0;
}

/* Scans forward from `p` (positioned right after an open tag's own '>')
 * for the matching close tag, tracking nesting depth of the same tag name.
 * Returns a pointer to the '<' of the matching close tag, or NULL. */
static const char *skip_to_close_tag(const char *p, const char *name, size_t name_len) {
    int depth = 1;
    tag_info_t t;
    while (next_tag(p, &t)) {
        if (t.name_len == name_len && strncmp(t.name, name, name_len) == 0) {
            if (t.is_end) {
                depth--;
                if (depth == 0)
                    return t.tag_start;
            } else if (!t.self_closing) {
                depth++;
            }
        }
        p = t.after;
    }
    return NULL;
}

static const char *find_substr_bounded(const char *hay, size_t hay_len, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || needle_len > hay_len)
        return NULL;
    for (size_t i = 0; i + needle_len <= hay_len; i++) {
        if (strncmp(hay + i, needle, needle_len) == 0)
            return hay + i;
    }
    return NULL;
}

/* ==========================================================================
 * Attribute helpers
 * ========================================================================== */

static const char *find_attr(const char *attrs, size_t len, const char *key, size_t *out_val_len) {
    size_t key_len  = strlen(key);
    const char *p   = attrs;
    const char *end = attrs + len;
    while (p < end) {
        while (p < end && is_ws(*p))
            p++;
        const char *name_start = p;
        while (p < end && *p != '=' && !is_ws(*p))
            p++;
        size_t name_len = (size_t)(p - name_start);
        while (p < end && is_ws(*p))
            p++;
        if (p < end && *p == '=') {
            p++;
            while (p < end && is_ws(*p))
                p++;
            if (p < end && (*p == '"' || *p == '\'')) {
                char quote = *p;
                p++;
                const char *val_start = p;
                while (p < end && *p != quote)
                    p++;
                size_t val_len = (size_t)(p - val_start);
                if (name_len == key_len && strncmp(name_start, key, key_len) == 0) {
                    *out_val_len = val_len;
                    return val_start;
                }
                if (p < end)
                    p++;
            }
        } else {
            if (p < end)
                p++;
        }
    }
    return NULL;
}

static double attr_double(const char *attrs, size_t len, const char *key, double def) {
    size_t vlen;
    const char *v = find_attr(attrs, len, key, &vlen);
    if (v == NULL)
        return def;
    char *end;
    double val = strtod(v, &end);
    if (end == v)
        return def;
    return val;
}

static int attr_equals(const char *attrs, size_t len, const char *key, const char *expected) {
    size_t vlen;
    const char *v = find_attr(attrs, len, key, &vlen);
    if (v == NULL)
        return 0;
    size_t elen = strlen(expected);
    return vlen == elen && strncmp(v, expected, elen) == 0;
}

static int parse_color_attr(const char *attrs, size_t len, const char *key, nixie_rgb_t *out) {
    size_t vlen;
    const char *v = find_attr(attrs, len, key, &vlen);
    if (v == NULL)
        return 0;
    if (vlen == 4 && strncmp(v, "none", 4) == 0)
        return 0;
    char buf[16];
    size_t n = vlen < sizeof(buf) - 1 ? vlen : sizeof(buf) - 1;
    memcpy(buf, v, n);
    buf[n] = '\0';
    nixie_rgb_t rgb;
    if (nixie_parse_hex(buf, &rgb) != 0)
        return 0;
    *out = rgb;
    return 1;
}

static void attr_dasharray(const char *attrs, size_t len, double *on, double *off) {
    *on  = 0.0;
    *off = 0.0;
    size_t vlen;
    const char *v = find_attr(attrs, len, "stroke-dasharray", &vlen);
    if (v == NULL)
        return;
    const char *end_bound = v + vlen;
    char *e;
    double a = strtod(v, &e);
    if (e == v)
        return;
    *on           = a;
    const char *p = e;
    while (p < end_bound && (*p == ',' || is_ws(*p)))
        p++;
    double b = strtod(p, &e);
    *off     = (e == p) ? a : b;
}

static nixie_svg_paint_t parse_paint(const char *attrs, size_t len, double scale_factor) {
    nixie_svg_paint_t paint;
    memset(&paint, 0, sizeof(paint));
    paint.has_fill     = parse_color_attr(attrs, len, "fill", &paint.fill);
    paint.has_stroke   = parse_color_attr(attrs, len, "stroke", &paint.stroke);
    paint.stroke_width = attr_double(attrs, len, "stroke-width", 1.0) * scale_factor;
    paint.opacity      = attr_double(attrs, len, "opacity", 1.0);
    attr_dasharray(attrs, len, &paint.dash_on, &paint.dash_off);
    paint.dash_on *= scale_factor;
    paint.dash_off *= scale_factor;
    return paint;
}

static nixie_point_list_t parse_points_attr(nixie_arena_t *arena, const char *s, size_t len) {
    nixie_point_list_t out = { 0 };
    const char *p          = s;
    const char *end_bound  = s + len;
    while (p < end_bound) {
        while (p < end_bound && (is_ws(*p) || *p == ','))
            p++;
        if (p >= end_bound)
            break;
        char *e;
        double x = strtod(p, &e);
        if (e == p)
            break;
        p = e;
        while (p < end_bound && (is_ws(*p) || *p == ','))
            p++;
        double y = strtod(p, &e);
        if (e == p)
            break;
        p                = e;
        nixie_point_t pt = { x, y };
        nixie_point_list_push(arena, &out, pt);
    }
    return out;
}

static int parse_numbers_bounded(const char *p, const char *end_bound, double *out, int max_count) {
    const char *paren = memchr(p, '(', (size_t)(end_bound - p));
    if (paren == NULL)
        return 0;
    p     = paren + 1;
    int n = 0;
    while (n < max_count && p < end_bound) {
        char *e;
        double v = strtod(p, &e);
        if (e == p)
            break;
        out[n++] = v;
        p        = e;
        while (p < end_bound && (*p == ',' || is_ws(*p)))
            p++;
        if (p < end_bound && *p == ')')
            break;
    }
    return n;
}

static void parse_transform_attr(const char *val, size_t val_len, nixie_affine_t *affine, int *has_rotate, double *rotate_deg, double *rotate_cx, double *rotate_cy) {
    *affine     = affine_identity();
    *has_rotate = 0;
    *rotate_deg = 0.0;
    *rotate_cx  = 0.0;
    *rotate_cy  = 0.0;
    if (val == NULL)
        return;
    const char *end_bound = val + val_len;

    const char *t = find_substr_bounded(val, val_len, "translate(");
    if (t != NULL) {
        double nums[2] = { 0.0, 0.0 };
        int c          = parse_numbers_bounded(t, end_bound, nums, 2);
        affine->tx     = nums[0];
        affine->ty     = (c >= 2) ? nums[1] : 0.0;
    }
    const char *sc = find_substr_bounded(val, val_len, "scale(");
    if (sc != NULL) {
        double nums[2] = { 1.0, 1.0 };
        int c          = parse_numbers_bounded(sc, end_bound, nums, 2);
        affine->sx     = nums[0];
        affine->sy     = (c >= 2) ? nums[1] : nums[0];
    }
    const char *ro = find_substr_bounded(val, val_len, "rotate(");
    if (ro != NULL) {
        double nums[3] = { 0.0, 0.0, 0.0 };
        int c          = parse_numbers_bounded(ro, end_bound, nums, 3);
        *has_rotate    = 1;
        *rotate_deg    = nums[0];
        *rotate_cx     = (c >= 3) ? nums[1] : 0.0;
        *rotate_cy     = (c >= 3) ? nums[2] : 0.0;
    }
}

static void extract_marker_id(const char *v, size_t vlen, nixie_svg_marker_ref_t *ref) {
    memset(ref, 0, sizeof(*ref));
    const char *hash = find_substr_bounded(v, vlen, "#");
    if (hash == NULL)
        return;
    const char *end_bound = v + vlen;
    const char *p         = hash + 1;
    size_t n              = 0;
    while (p < end_bound && *p != ')' && n < sizeof(ref->id) - 1) {
        ref->id[n++] = *p;
        p++;
    }
    ref->id[n]   = '\0';
    ref->present = (n > 0);
}

static void parse_marker_refs(const char *attrs, size_t len, nixie_svg_marker_ref_t *start, nixie_svg_marker_ref_t *end) {
    memset(start, 0, sizeof(*start));
    memset(end, 0, sizeof(*end));
    size_t vlen;
    const char *v = find_attr(attrs, len, "marker-start", &vlen);
    if (v != NULL)
        extract_marker_id(v, vlen, start);
    v = find_attr(attrs, len, "marker-end", &vlen);
    if (v != NULL)
        extract_marker_id(v, vlen, end);
}

/* Unescapes the 4 XML entities nixie's own renderers emit (&amp; &lt; &gt;
 * &quot;) -- text content is the only thing that ever needs this, since
 * every attribute nixie's generator emits that we actually consume is a
 * plain number or hex color. */
static char *unescape_xml_arena(nixie_arena_t *arena, const char *s, size_t len) {
    char *out = (char *)nixie_arena_alloc(arena, len + 1);
    size_t oi = 0, i = 0;
    while (i < len) {
        if (s[i] == '&') {
            if (len - i >= 5 && strncmp(s + i, "&amp;", 5) == 0) {
                out[oi++] = '&';
                i += 5;
                continue;
            }
            if (len - i >= 4 && strncmp(s + i, "&lt;", 4) == 0) {
                out[oi++] = '<';
                i += 4;
                continue;
            }
            if (len - i >= 4 && strncmp(s + i, "&gt;", 4) == 0) {
                out[oi++] = '>';
                i += 4;
                continue;
            }
            if (len - i >= 6 && strncmp(s + i, "&quot;", 6) == 0) {
                out[oi++] = '"';
                i += 6;
                continue;
            }
        }
        out[oi++] = s[i++];
    }
    out[oi] = '\0';
    return out;
}

/* ==========================================================================
 * Path `d` attribute parsing -- only M/L/Q/C/Z ever appear in nixie's own
 * output (confirmed: no arcs, no gradients). Lowercase (relative) commands
 * are treated identically to uppercase (absolute); nixie never emits
 * relative path commands, so this is a safe simplification, not a general
 * SVG path parser.
 * ========================================================================== */

static nixie_path_op_t *parse_path_d(nixie_arena_t *arena, const char *d, size_t len, int *out_count) {
    nixie_path_op_t *ops = NULL;
    int count = 0, cap = 0;
    const char *p         = d;
    const char *end_bound = d + len;
    char cmd              = 0;

    while (p < end_bound) {
        while (p < end_bound && (is_ws(*p) || *p == ','))
            p++;
        if (p >= end_bound)
            break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            cmd = *p;
            p++;
            while (p < end_bound && is_ws(*p))
                p++;
        }

        nixie_path_op_t op;
        memset(&op, 0, sizeof(op));
        char *e;

        if (cmd == 'M' || cmd == 'm') {
            double x = strtod(p, &e);
            if (e == p)
                break;
            p = e;
            while (p < end_bound && (is_ws(*p) || *p == ','))
                p++;
            double y = strtod(p, &e);
            if (e == p)
                break;
            p       = e;
            op.kind = NIXIE_PATHOP_MOVE;
            op.p.x  = x;
            op.p.y  = y;
            cmd     = 'L'; /* subsequent bare coordinate pairs after M are implicit lineto */
        } else if (cmd == 'L' || cmd == 'l') {
            double x = strtod(p, &e);
            if (e == p)
                break;
            p = e;
            while (p < end_bound && (is_ws(*p) || *p == ','))
                p++;
            double y = strtod(p, &e);
            if (e == p)
                break;
            p       = e;
            op.kind = NIXIE_PATHOP_LINE;
            op.p.x  = x;
            op.p.y  = y;
        } else if (cmd == 'Q' || cmd == 'q') {
            double cx = strtod(p, &e);
            if (e == p)
                break;
            p = e;
            while (p < end_bound && (is_ws(*p) || *p == ','))
                p++;
            double cy = strtod(p, &e);
            if (e == p)
                break;
            p = e;
            while (p < end_bound && (is_ws(*p) || *p == ','))
                p++;
            double x = strtod(p, &e);
            if (e == p)
                break;
            p = e;
            while (p < end_bound && (is_ws(*p) || *p == ','))
                p++;
            double y = strtod(p, &e);
            if (e == p)
                break;
            p       = e;
            op.kind = NIXIE_PATHOP_QUAD;
            op.c1.x = cx;
            op.c1.y = cy;
            op.p.x  = x;
            op.p.y  = y;
        } else if (cmd == 'C' || cmd == 'c') {
            double c1x = strtod(p, &e);
            if (e == p)
                break;
            p = e;
            while (p < end_bound && (is_ws(*p) || *p == ','))
                p++;
            double c1y = strtod(p, &e);
            if (e == p)
                break;
            p = e;
            while (p < end_bound && (is_ws(*p) || *p == ','))
                p++;
            double c2x = strtod(p, &e);
            if (e == p)
                break;
            p = e;
            while (p < end_bound && (is_ws(*p) || *p == ','))
                p++;
            double c2y = strtod(p, &e);
            if (e == p)
                break;
            p = e;
            while (p < end_bound && (is_ws(*p) || *p == ','))
                p++;
            double x = strtod(p, &e);
            if (e == p)
                break;
            p = e;
            while (p < end_bound && (is_ws(*p) || *p == ','))
                p++;
            double y = strtod(p, &e);
            if (e == p)
                break;
            p       = e;
            op.kind = NIXIE_PATHOP_CUBIC;
            op.c1.x = c1x;
            op.c1.y = c1y;
            op.c2.x = c2x;
            op.c2.y = c2y;
            op.p.x  = x;
            op.p.y  = y;
        } else if (cmd == 'Z' || cmd == 'z') {
            op.kind = NIXIE_PATHOP_CLOSE;
        } else {
            break;
        }

        if (count == cap) {
            int new_cap              = (cap == 0) ? 16 : cap * 2;
            nixie_path_op_t *new_ops = (nixie_path_op_t *)nixie_arena_alloc(arena, sizeof(nixie_path_op_t) * (size_t)new_cap);
            if (count > 0)
                memcpy(new_ops, ops, sizeof(nixie_path_op_t) * (size_t)count);
            ops = new_ops;
            cap = new_cap;
        }
        ops[count++] = op;
    }

    *out_count = count;
    return ops;
}

static void transform_path_ops_inplace(nixie_path_op_t *ops, int count, nixie_affine_t group, nixie_affine_t own) {
    for (int i = 0; i < count; i++) {
        nixie_path_op_t *op = &ops[i];
        if (op->kind == NIXIE_PATHOP_CLOSE)
            continue;
        op->p = affine_apply(group, affine_apply(own, op->p));
        if (op->kind == NIXIE_PATHOP_QUAD || op->kind == NIXIE_PATHOP_CUBIC) {
            op->c1 = affine_apply(group, affine_apply(own, op->c1));
        }
        if (op->kind == NIXIE_PATHOP_CUBIC) {
            op->c2 = affine_apply(group, affine_apply(own, op->c2));
        }
    }
}

/* ==========================================================================
 * Root <svg> attributes
 * ========================================================================== */

static int parse_style_background(const char *attrs, size_t len, nixie_rgb_t *out) {
    size_t vlen;
    const char *v = find_attr(attrs, len, "style", &vlen);
    if (v == NULL)
        return 0;
    const char *bg = find_substr_bounded(v, vlen, "background:");
    if (bg == NULL)
        return 0;
    const char *p         = bg + strlen("background:");
    const char *end_bound = v + vlen;
    char buf[16];
    size_t n = 0;
    while (p < end_bound && n < sizeof(buf) - 1 && *p != ';' && *p != '"') {
        buf[n++] = *p;
        p++;
    }
    buf[n] = '\0';
    return nixie_parse_hex(buf, out) == 0;
}

static void parse_svg_root(const char *attrs, size_t len, nixie_svg_document_t *doc, double scale) {
    size_t vblen;
    const char *vb = find_attr(attrs, len, "viewBox", &vblen);
    double w = 0.0, h = 0.0;
    if (vb != NULL) {
        char *e;
        const char *p = vb;
        strtod(p, &e);
        p = e;
        while (is_ws(*p))
            p++;
        strtod(p, &e);
        p = e;
        while (is_ws(*p))
            p++;
        double vw = strtod(p, &e);
        p         = e;
        while (is_ws(*p))
            p++;
        double vh = strtod(p, &e);
        w         = vw;
        h         = vh;
    } else {
        w = attr_double(attrs, len, "width", 0.0);
        h = attr_double(attrs, len, "height", 0.0);
    }
    doc->width  = w * scale;
    doc->height = h * scale;

    nixie_rgb_t bg;
    doc->has_background = parse_style_background(attrs, len, &bg);
    if (doc->has_background)
        doc->background = bg;
}

/* ==========================================================================
 * <defs><marker>...</marker></defs> extraction (pass 1)
 * ========================================================================== */

static void push_marker(nixie_arena_t *arena, nixie_svg_marker_def_t **markers, int *count, int *cap, nixie_svg_marker_def_t m) {
    if (*count == *cap) {
        int new_cap                = (*cap == 0) ? 4 : (*cap) * 2;
        nixie_svg_marker_def_t *nm = (nixie_svg_marker_def_t *)nixie_arena_alloc(arena, sizeof(nixie_svg_marker_def_t) * (size_t)new_cap);
        if (*count > 0)
            memcpy(nm, *markers, sizeof(nixie_svg_marker_def_t) * (size_t)(*count));
        *markers = nm;
        *cap     = new_cap;
    }
    (*markers)[(*count)++] = m;
}

static void parse_defs_and_markers(nixie_arena_t *arena, const char *svg_text, nixie_svg_document_t *doc, double scale) {
    nixie_svg_marker_def_t *markers = NULL;
    int count = 0, cap = 0;
    size_t doc_len      = strlen(svg_text);
    const char *doc_end = svg_text + doc_len;

    tag_info_t t;
    const char *p = svg_text;
    while (next_tag(p, &t)) {
        if (t.is_end || !tag_name_is(&t, "defs") || t.self_closing) {
            p = t.after;
            continue;
        }

        const char *defs_close = skip_to_close_tag(t.after, "defs", 4);
        const char *defs_end   = defs_close ? defs_close : doc_end;

        tag_info_t mt;
        const char *mp = t.after;
        while (mp < defs_end && next_tag(mp, &mt) && mt.tag_start < defs_end) {
            if (!mt.is_end && tag_name_is(&mt, "marker")) {
                nixie_svg_marker_def_t md;
                memset(&md, 0, sizeof(md));

                size_t idlen;
                const char *idv = find_attr(mt.attrs, mt.attrs_len, "id", &idlen);
                if (idv != NULL) {
                    size_t n = idlen < sizeof(md.id) - 1 ? idlen : sizeof(md.id) - 1;
                    memcpy(md.id, idv, n);
                    md.id[n] = '\0';
                }
                md.ref_x = attr_double(mt.attrs, mt.attrs_len, "refX", 0.0) * scale;
                md.ref_y = attr_double(mt.attrs, mt.attrs_len, "refY", 0.0) * scale;

                const char *marker_end = mt.self_closing ? mt.after : skip_to_close_tag(mt.after, "marker", 6);
                const char *inner_end  = marker_end ? marker_end : defs_end;

                if (!mt.self_closing) {
                    tag_info_t pt;
                    if (next_tag(mt.after, &pt) && pt.tag_start < inner_end && !pt.is_end && tag_name_is(&pt, "polygon")) {
                        size_t ptslen;
                        const char *ptsv = find_attr(pt.attrs, pt.attrs_len, "points", &ptslen);
                        if (ptsv != NULL) {
                            nixie_point_list_t pts = parse_points_attr(arena, ptsv, ptslen);
                            for (int i = 0; i < pts.count; i++) {
                                pts.points[i].x *= scale;
                                pts.points[i].y *= scale;
                            }
                            md.points      = pts.points;
                            md.point_count = pts.count;
                        }
                        md.has_fill = parse_color_attr(pt.attrs, pt.attrs_len, "fill", &md.fill);
                    }
                }

                push_marker(arena, &markers, &count, &cap, md);
                mp = marker_end ? marker_end : inner_end;
                continue;
            }
            mp = mt.after;
        }

        if (defs_close != NULL) {
            tag_info_t close_t;
            next_tag(defs_close, &close_t);
            p = close_t.after;
        } else {
            p = doc_end;
        }
    }

    doc->markers      = markers;
    doc->marker_count = count;
}

/* ==========================================================================
 * Body shape walk (pass 2)
 * ========================================================================== */

#define NIXIE_MAX_GROUP_DEPTH 16

static void push_shape(nixie_arena_t *arena, nixie_svg_shape_t **shapes, int *count, int *cap, nixie_svg_shape_t s) {
    if (*count == *cap) {
        int new_cap           = (*cap == 0) ? 32 : (*cap) * 2;
        nixie_svg_shape_t *ns = (nixie_svg_shape_t *)nixie_arena_alloc(arena, sizeof(nixie_svg_shape_t) * (size_t)new_cap);
        if (*count > 0)
            memcpy(ns, *shapes, sizeof(nixie_svg_shape_t) * (size_t)(*count));
        *shapes = ns;
        *cap    = new_cap;
    }
    (*shapes)[(*count)++] = s;
}

/* Parses one <text>...</text> block (which may contain <tspan> children for
 * multiline or same-line multi-color runs -- see the design note above
 * parse_text_block's body) and appends the resulting TEXT shape(s). Returns
 * a pointer just past the </text> close tag. */
static const char *parse_text_block(nixie_arena_t *arena, tag_info_t text_tag, nixie_affine_t group, nixie_svg_shape_t **shapes, int *count, int *cap) {
    double base_scale = group.sx;

    nixie_affine_t own;
    int hr;
    double rd, rcx, rcy;
    size_t tvlen;
    const char *tv = find_attr(text_tag.attrs, text_tag.attrs_len, "transform", &tvlen);
    parse_transform_attr(tv, tv ? tvlen : 0, &own, &hr, &rd, &rcx, &rcy);

    double base_x                  = attr_double(text_tag.attrs, text_tag.attrs_len, "x", 0.0);
    double base_y                  = attr_double(text_tag.attrs, text_tag.attrs_len, "y", 0.0);
    double base_dy                 = attr_double(text_tag.attrs, text_tag.attrs_len, "dy", 0.0);
    double font_size               = attr_double(text_tag.attrs, text_tag.attrs_len, "font-size", 14.0) * own.sx * base_scale;
    int font_weight                = (int)attr_double(text_tag.attrs, text_tag.attrs_len, "font-weight", 400.0);
    int italic                     = attr_equals(text_tag.attrs, text_tag.attrs_len, "font-style", "italic");
    int underline                  = attr_equals(text_tag.attrs, text_tag.attrs_len, "text-decoration", "underline");
    nixie_svg_text_anchor_t anchor = NIXIE_SVG_ANCHOR_START;
    if (attr_equals(text_tag.attrs, text_tag.attrs_len, "text-anchor", "middle")) {
        anchor = NIXIE_SVG_ANCHOR_MIDDLE;
    } else if (attr_equals(text_tag.attrs, text_tag.attrs_len, "text-anchor", "end")) {
        anchor = NIXIE_SVG_ANCHOR_END;
    }
    nixie_rgb_t fill;
    int has_fill   = parse_color_attr(text_tag.attrs, text_tag.attrs_len, "fill", &fill);
    double opacity = attr_double(text_tag.attrs, text_tag.attrs_len, "opacity", 1.0);

    double current_y = base_y + base_dy;

    if (text_tag.self_closing) {
        return text_tag.after;
    }

    const char *close       = skip_to_close_tag(text_tag.after, "text", 4);
    const char *content_end = close ? close : (text_tag.after + strlen(text_tag.after));

    tag_info_t first_child;
    int has_tspan = next_tag(text_tag.after, &first_child) && first_child.tag_start < content_end && !first_child.is_end && tag_name_is(&first_child, "tspan");

    if (!has_tspan) {
        char *text_content = unescape_xml_arena(arena, text_tag.after, (size_t)(content_end - text_tag.after));
        if (text_content[0] != '\0') {
            nixie_svg_shape_t s;
            memset(&s, 0, sizeof(s));
            s.kind                  = NIXIE_SVG_SHAPE_TEXT;
            nixie_point_t anchor_pt = affine_apply(group, affine_apply(own, (nixie_point_t){ base_x, current_y }));
            s.text_x                = anchor_pt.x;
            s.text_y                = anchor_pt.y;
            s.text                  = text_content;
            s.font_size             = font_size;
            s.font_weight           = font_weight;
            s.italic                = italic;
            s.underline             = underline;
            s.text_anchor           = anchor;
            s.paint.has_fill        = has_fill;
            s.paint.fill            = fill;
            s.paint.opacity         = opacity;
            if (hr) {
                s.has_rotation  = 1;
                s.rotate_deg    = rd;
                s.rotate_center = affine_apply(group, affine_apply(own, (nixie_point_t){ rcx, rcy }));
            }
            push_shape(arena, shapes, count, cap, s);
        }
    } else {
        int current_line_idx = -1;
        const char *scan     = text_tag.after;
        tag_info_t ct;
        while (scan < content_end && next_tag(scan, &ct) && ct.tag_start < content_end) {
            if (!ct.is_end && tag_name_is(&ct, "tspan")) {
                size_t xlen;
                const char *xv    = find_attr(ct.attrs, ct.attrs_len, "x", &xlen);
                double tspan_dy   = attr_double(ct.attrs, ct.attrs_len, "dy", 0.0);
                nixie_rgb_t tfill = fill;
                int thas_fill     = has_fill;
                if (parse_color_attr(ct.attrs, ct.attrs_len, "fill", &tfill))
                    thas_fill = 1;

                const char *tspan_close       = ct.self_closing ? NULL : skip_to_close_tag(ct.after, "tspan", 5);
                const char *tspan_content_end = tspan_close ? tspan_close : ct.after;
                char *piece                   = ct.self_closing ? nixie_arena_strdup(arena, "") : unescape_xml_arena(arena, ct.after, (size_t)(tspan_content_end - ct.after));

                if (xv != NULL) {
                    double tx = strtod(xv, NULL);
                    current_y += tspan_dy;
                    nixie_svg_shape_t s;
                    memset(&s, 0, sizeof(s));
                    s.kind                  = NIXIE_SVG_SHAPE_TEXT;
                    nixie_point_t anchor_pt = affine_apply(group, affine_apply(own, (nixie_point_t){ tx, current_y }));
                    s.text_x                = anchor_pt.x;
                    s.text_y                = anchor_pt.y;
                    s.text                  = piece;
                    s.font_size             = font_size;
                    s.font_weight           = font_weight;
                    s.italic                = italic;
                    s.underline             = underline;
                    s.text_anchor           = anchor;
                    s.paint.has_fill        = thas_fill;
                    s.paint.fill            = tfill;
                    s.paint.opacity         = opacity;
                    if (hr) {
                        s.has_rotation  = 1;
                        s.rotate_deg    = rd;
                        s.rotate_center = affine_apply(group, affine_apply(own, (nixie_point_t){ rcx, rcy }));
                    }
                    push_shape(arena, shapes, count, cap, s);
                    current_line_idx = *count - 1;
                } else if (current_line_idx >= 0) {
                    nixie_svg_shape_t *cur = &(*shapes)[current_line_idx];
                    size_t old_len         = strlen(cur->text);
                    size_t add_len         = strlen(piece);
                    char *merged           = (char *)nixie_arena_alloc(arena, old_len + add_len + 1);
                    memcpy(merged, cur->text, old_len);
                    memcpy(merged + old_len, piece, add_len);
                    merged[old_len + add_len] = '\0';
                    cur->text                 = merged;
                } else {
                    nixie_svg_shape_t s;
                    memset(&s, 0, sizeof(s));
                    s.kind                  = NIXIE_SVG_SHAPE_TEXT;
                    nixie_point_t anchor_pt = affine_apply(group, affine_apply(own, (nixie_point_t){ base_x, current_y }));
                    s.text_x                = anchor_pt.x;
                    s.text_y                = anchor_pt.y;
                    s.text                  = piece;
                    s.font_size             = font_size;
                    s.font_weight           = font_weight;
                    s.italic                = italic;
                    s.underline             = underline;
                    s.text_anchor           = anchor;
                    s.paint.has_fill        = thas_fill;
                    s.paint.fill            = tfill;
                    s.paint.opacity         = opacity;
                    push_shape(arena, shapes, count, cap, s);
                    current_line_idx = *count - 1;
                }
            }
            scan = ct.after;
        }
    }

    if (close == NULL) {
        return content_end;
    }
    tag_info_t close_t;
    next_tag(close, &close_t);
    return close_t.after;
}

static void parse_body(nixie_arena_t *arena, const char *svg_text, nixie_svg_document_t *doc, double scale) {
    nixie_svg_shape_t *shapes = NULL;
    int count = 0, cap = 0;

    nixie_affine_t group_stack[NIXIE_MAX_GROUP_DEPTH];
    group_stack[0].tx = 0.0;
    group_stack[0].ty = 0.0;
    group_stack[0].sx = scale;
    group_stack[0].sy = scale;
    int group_top     = 0;

    tag_info_t t;
    const char *p = svg_text;
    while (next_tag(p, &t)) {
        if (t.is_end) {
            if (tag_name_is(&t, "g") && group_top > 0)
                group_top--;
            p = t.after;
            continue;
        }

        if (tag_name_is(&t, "defs")) {
            if (t.self_closing) {
                p = t.after;
                continue;
            }
            const char *close = skip_to_close_tag(t.after, "defs", 4);
            if (close == NULL)
                break;
            tag_info_t ct;
            next_tag(close, &ct);
            p = ct.after;
            continue;
        }
        if (tag_name_is(&t, "style") || tag_name_is(&t, "title")) {
            if (t.self_closing) {
                p = t.after;
                continue;
            }
            const char *close = skip_to_close_tag(t.after, t.name, t.name_len);
            if (close == NULL) {
                p = t.after;
                continue;
            }
            tag_info_t ct;
            next_tag(close, &ct);
            p = ct.after;
            continue;
        }
        if (tag_name_is(&t, "svg")) {
            parse_svg_root(t.attrs, t.attrs_len, doc, scale);
            p = t.after;
            continue;
        }
        if (tag_name_is(&t, "g")) {
            nixie_affine_t own;
            int hr;
            double rd, rcx, rcy;
            size_t tvlen;
            const char *tv = find_attr(t.attrs, t.attrs_len, "transform", &tvlen);
            parse_transform_attr(tv, tv ? tvlen : 0, &own, &hr, &rd, &rcx, &rcy);
            if (!t.self_closing && group_top + 1 < NIXIE_MAX_GROUP_DEPTH) {
                group_stack[group_top + 1] = affine_compose(group_stack[group_top], own);
                group_top++;
            }
            p = t.after;
            continue;
        }
        if (tag_name_is(&t, "text")) {
            p = parse_text_block(arena, t, group_stack[group_top], &shapes, &count, &cap);
            continue;
        }

        nixie_affine_t group     = group_stack[group_top];
        double base_scale_factor = group.sx;

        if (tag_name_is(&t, "rect")) {
            nixie_svg_shape_t s;
            memset(&s, 0, sizeof(s));
            s.kind           = NIXIE_SVG_SHAPE_RECT;
            double rx        = attr_double(t.attrs, t.attrs_len, "rx", 0.0);
            double ry        = attr_double(t.attrs, t.attrs_len, "ry", rx);
            double x         = attr_double(t.attrs, t.attrs_len, "x", 0.0);
            double y         = attr_double(t.attrs, t.attrs_len, "y", 0.0);
            double w         = attr_double(t.attrs, t.attrs_len, "width", 0.0);
            double h         = attr_double(t.attrs, t.attrs_len, "height", 0.0);
            nixie_point_t p0 = affine_apply(group, (nixie_point_t){ x, y });
            s.x              = p0.x;
            s.y              = p0.y;
            s.w              = w * base_scale_factor;
            s.h              = h * base_scale_factor;
            s.rx             = rx * base_scale_factor;
            s.ry             = ry * base_scale_factor;
            s.paint          = parse_paint(t.attrs, t.attrs_len, base_scale_factor);
            push_shape(arena, &shapes, &count, &cap, s);
        } else if (tag_name_is(&t, "circle")) {
            nixie_svg_shape_t s;
            memset(&s, 0, sizeof(s));
            s.kind          = NIXIE_SVG_SHAPE_CIRCLE;
            double cx       = attr_double(t.attrs, t.attrs_len, "cx", 0.0);
            double cy       = attr_double(t.attrs, t.attrs_len, "cy", 0.0);
            double r        = attr_double(t.attrs, t.attrs_len, "r", 0.0);
            nixie_point_t c = affine_apply(group, (nixie_point_t){ cx, cy });
            s.cx            = c.x;
            s.cy            = c.y;
            s.r             = r * base_scale_factor;
            s.paint         = parse_paint(t.attrs, t.attrs_len, base_scale_factor);
            push_shape(arena, &shapes, &count, &cap, s);
        } else if (tag_name_is(&t, "ellipse")) {
            nixie_svg_shape_t s;
            memset(&s, 0, sizeof(s));
            s.kind          = NIXIE_SVG_SHAPE_ELLIPSE;
            double cx       = attr_double(t.attrs, t.attrs_len, "cx", 0.0);
            double cy       = attr_double(t.attrs, t.attrs_len, "cy", 0.0);
            double rx       = attr_double(t.attrs, t.attrs_len, "rx", 0.0);
            double ry       = attr_double(t.attrs, t.attrs_len, "ry", 0.0);
            nixie_point_t c = affine_apply(group, (nixie_point_t){ cx, cy });
            s.cx            = c.x;
            s.cy            = c.y;
            s.rx            = rx * base_scale_factor;
            s.ry            = ry * base_scale_factor;
            s.paint         = parse_paint(t.attrs, t.attrs_len, base_scale_factor);
            push_shape(arena, &shapes, &count, &cap, s);
        } else if (tag_name_is(&t, "line")) {
            nixie_svg_shape_t s;
            memset(&s, 0, sizeof(s));
            s.kind           = NIXIE_SVG_SHAPE_LINE;
            double x1        = attr_double(t.attrs, t.attrs_len, "x1", 0.0);
            double y1        = attr_double(t.attrs, t.attrs_len, "y1", 0.0);
            double x2        = attr_double(t.attrs, t.attrs_len, "x2", 0.0);
            double y2        = attr_double(t.attrs, t.attrs_len, "y2", 0.0);
            nixie_point_t p1 = affine_apply(group, (nixie_point_t){ x1, y1 });
            nixie_point_t p2 = affine_apply(group, (nixie_point_t){ x2, y2 });
            s.x1             = p1.x;
            s.y1             = p1.y;
            s.x2             = p2.x;
            s.y2             = p2.y;
            s.paint          = parse_paint(t.attrs, t.attrs_len, base_scale_factor);
            parse_marker_refs(t.attrs, t.attrs_len, &s.marker_start, &s.marker_end);
            push_shape(arena, &shapes, &count, &cap, s);
        } else if (tag_name_is(&t, "polyline") || tag_name_is(&t, "polygon")) {
            nixie_svg_shape_t s;
            memset(&s, 0, sizeof(s));
            int is_polyline = tag_name_is(&t, "polyline");
            s.kind          = is_polyline ? NIXIE_SVG_SHAPE_POLYLINE : NIXIE_SVG_SHAPE_POLYGON;
            size_t ptslen;
            const char *ptsv = find_attr(t.attrs, t.attrs_len, "points", &ptslen);
            if (ptsv != NULL) {
                nixie_point_list_t pts = parse_points_attr(arena, ptsv, ptslen);
                for (int i = 0; i < pts.count; i++)
                    pts.points[i] = affine_apply(group, pts.points[i]);
                s.points      = pts.points;
                s.point_count = pts.count;
            }
            s.paint = parse_paint(t.attrs, t.attrs_len, base_scale_factor);
            if (is_polyline)
                parse_marker_refs(t.attrs, t.attrs_len, &s.marker_start, &s.marker_end);
            push_shape(arena, &shapes, &count, &cap, s);
        } else if (tag_name_is(&t, "path")) {
            nixie_svg_shape_t s;
            memset(&s, 0, sizeof(s));
            s.kind = NIXIE_SVG_SHAPE_PATH;
            nixie_affine_t own;
            int hr;
            double rd, rcx, rcy;
            size_t tvlen;
            const char *tv = find_attr(t.attrs, t.attrs_len, "transform", &tvlen);
            parse_transform_attr(tv, tv ? tvlen : 0, &own, &hr, &rd, &rcx, &rcy);
            size_t dlen;
            const char *dv = find_attr(t.attrs, t.attrs_len, "d", &dlen);
            if (dv != NULL) {
                int opcount          = 0;
                nixie_path_op_t *ops = parse_path_d(arena, dv, dlen, &opcount);
                transform_path_ops_inplace(ops, opcount, group, own);
                s.path_ops      = ops;
                s.path_op_count = opcount;
            }
            s.paint = parse_paint(t.attrs, t.attrs_len, own.sx * base_scale_factor);
            push_shape(arena, &shapes, &count, &cap, s);
        }

        p = t.after;
    }

    doc->shapes      = shapes;
    doc->shape_count = count;
}

/* ==========================================================================
 * Public entry point
 * ========================================================================== */

nixie_svg_parse_result_t nixie_svg_parse(nixie_arena_t *arena, const char *svg_text, double scale) {
    nixie_svg_parse_result_t result;
    result.doc              = NULL;
    result.error            = NIXIE_OK;
    result.error_message[0] = '\0';
    result.error_line       = -1;

    if (svg_text == NULL) {
        result.error = NIXIE_ERROR_INVALID_ARGUMENT;
        strncpy(result.error_message, "svg_text must not be NULL", sizeof(result.error_message) - 1);
        result.error_message[sizeof(result.error_message) - 1] = '\0';
        return result;
    }
    if (scale <= 0.0)
        scale = 1.0;

    nixie_svg_document_t *doc = (nixie_svg_document_t *)nixie_arena_alloc_zeroed(arena, sizeof(nixie_svg_document_t));

    parse_defs_and_markers(arena, svg_text, doc, scale);
    parse_body(arena, svg_text, doc, scale);

    if (doc->width <= 0.0 || doc->height <= 0.0) {
        result.error = NIXIE_ERROR_PARSE;
        strncpy(result.error_message, "could not determine SVG canvas dimensions (missing viewBox/width/height)", sizeof(result.error_message) - 1);
        result.error_message[sizeof(result.error_message) - 1] = '\0';
        return result;
    }

    result.doc = doc;
    return result;
}
