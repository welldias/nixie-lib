#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../flowchart/graph_build.h" /* nixie_split_significant_lines() is generic (text -> lines) */

/* ==========================================================================
 * Small scanning helpers
 * ========================================================================== */

static int ieq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    }
    return 1;
}

static size_t skip_ws(const char *s) {
    size_t n = 0;
    while (s[n] == ' ' || s[n] == '\t') n++;
    return n;
}

/* Case-insensitive whole-word search, mirroring JS's /\bkeyword\b/i.test(). */
static int contains_word_ci(const char *haystack, const char *word) {
    size_t wlen = strlen(word);
    for (const char *p = haystack; *p != '\0'; p++) {
        if (ieq_n(p, word, wlen)) {
            int before_ok = (p == haystack) || !(isalnum((unsigned char)p[-1]) || p[-1] == '_');
            char after = p[wlen];
            int after_ok = !(isalnum((unsigned char)after) || after == '_');
            if (before_ok && after_ok) return 1;
        }
    }
    return 0;
}

/* ==========================================================================
 * Series array growth
 * ========================================================================== */

static void ensure_series_capacity(nixie_arena_t *arena, nixie_xy_chart_t *chart) {
    if (chart->series_count < chart->series_cap) return;
    size_t new_cap = chart->series_cap == 0 ? 4 : chart->series_cap * 2;
    nixie_xy_series_t *new_series = (nixie_xy_series_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_xy_series_t));
    if (chart->series_count > 0) memcpy(new_series, chart->series, chart->series_count * sizeof(nixie_xy_series_t));
    chart->series = new_series;
    chart->series_cap = new_cap;
}

/* ==========================================================================
 * Numeric / category array parsing (comma-separated, inside [ ... ])
 * ========================================================================== */

static double *parse_numeric_array(nixie_arena_t *arena, const char *content, const char *end, size_t *out_count) {
    size_t cap = 4, count = 0;
    double *arr = (double *)nixie_arena_alloc(arena, cap * sizeof(double));

    const char *p = content;
    while (p < end) {
        char *endptr;
        double v = strtod(p, &endptr);
        if (endptr == p || endptr > end) break;

        if (count == cap) {
            size_t new_cap = cap * 2;
            double *new_arr = (double *)nixie_arena_alloc(arena, new_cap * sizeof(double));
            memcpy(new_arr, arr, count * sizeof(double));
            arr = new_arr;
            cap = new_cap;
        }
        arr[count++] = v;

        p = endptr;
        p += skip_ws(p);
        if (p < end && *p == ',') {
            p++;
        } else {
            break;
        }
    }

    *out_count = count;
    return arr;
}

static char **parse_category_array(nixie_arena_t *arena, const char *content, const char *end, size_t *out_count) {
    size_t cap = 4, count = 0;
    char **arr = (char **)nixie_arena_alloc(arena, cap * sizeof(char *));

    const char *p = content;
    while (p <= end) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *seg_end = comma != NULL ? comma : end;

        const char *s = p, *e = seg_end;
        while (s < e && (*s == ' ' || *s == '\t')) s++;
        while (e > s && (*(e - 1) == ' ' || *(e - 1) == '\t')) e--;

        if (count == cap) {
            size_t new_cap = cap * 2;
            char **new_arr = (char **)nixie_arena_alloc(arena, new_cap * sizeof(char *));
            memcpy(new_arr, arr, count * sizeof(char *));
            arr = new_arr;
            cap = new_cap;
        }
        arr[count++] = nixie_arena_strndup(arena, s, (size_t)(e - s));

        if (comma == NULL) break;
        p = comma + 1;
    }

    *out_count = count;
    return arr;
}

/* ==========================================================================
 * Directive matchers
 * ========================================================================== */

static int try_title(nixie_arena_t *arena, const char *line, nixie_xy_chart_t *chart) {
    if (strncmp(line, "title", 5) != 0 || !(line[5] == ' ' || line[5] == '\t')) return 0;
    size_t i = 5;
    i += skip_ws(line + i);
    if (line[i] != '"') return 0;
    const char *content_start = line + i + 1;
    const char *close = strchr(content_start, '"');
    if (close == NULL || close == content_start) return 0; /* [^"]+ requires >= 1 char */

    chart->title = nixie_arena_strndup(arena, content_start, (size_t)(close - content_start));
    return 1;
}

/* `x-axis [cat, ...]` / `x-axis "Title" [cat, ...]` */
static int try_axis_categorical(nixie_arena_t *arena, const char *line, const char *kw, nixie_xy_axis_t *axis) {
    size_t kwlen = strlen(kw);
    if (strncmp(line, kw, kwlen) != 0) return 0;
    size_t i = kwlen;
    size_t ws = skip_ws(line + i);
    if (ws == 0) return 0;
    i += ws;

    char *title = NULL;
    if (line[i] == '"') {
        const char *cstart = line + i + 1;
        const char *close = strchr(cstart, '"');
        if (close == NULL) return 0;
        title = nixie_arena_strndup(arena, cstart, (size_t)(close - cstart));
        i = (size_t)(close - line) + 1;
        i += skip_ws(line + i); /* \s* (zero or more) */
    }

    if (line[i] != '[') return 0;
    const char *content_start = line + i + 1;
    const char *close_bracket = strchr(content_start, ']');
    if (close_bracket == NULL || close_bracket == content_start) return 0;

    size_t cat_count;
    char **cats = parse_category_array(arena, content_start, close_bracket, &cat_count);
    if (cat_count == 0) return 0;

    if (title != NULL) axis->title = title;
    axis->categories = cats;
    axis->category_count = cat_count;
    axis->has_range = 0;
    return 1;
}

/* `x-axis min --> max` / `x-axis "Title" min --> max` (also used for y-axis) */
static int try_axis_range(nixie_arena_t *arena, const char *line, const char *kw, nixie_xy_axis_t *axis) {
    size_t kwlen = strlen(kw);
    if (strncmp(line, kw, kwlen) != 0) return 0;
    size_t i = kwlen;
    size_t ws = skip_ws(line + i);
    if (ws == 0) return 0;
    i += ws;

    char *title = NULL;
    if (line[i] == '"') {
        const char *cstart = line + i + 1;
        const char *close = strchr(cstart, '"');
        if (close == NULL) return 0;
        title = nixie_arena_strndup(arena, cstart, (size_t)(close - cstart));
        i = (size_t)(close - line) + 1;
        ws = skip_ws(line + i);
        if (ws == 0) return 0; /* mandatory \s+ here */
        i += ws;
    }

    char *endptr;
    double minv = strtod(line + i, &endptr);
    if (endptr == line + i) return 0;
    i = (size_t)(endptr - line);

    i += skip_ws(line + i);
    if (strncmp(line + i, "-->", 3) != 0) return 0;
    i += 3;
    i += skip_ws(line + i);

    double maxv = strtod(line + i, &endptr);
    if (endptr == line + i) return 0;

    if (title != NULL) axis->title = title;
    axis->has_range = 1;
    axis->range_min = minv;
    axis->range_max = maxv;
    axis->categories = NULL;
    axis->category_count = 0;
    return 1;
}

/* `y-axis "Title"` (title only, no range) */
static int try_y_axis_title_only(nixie_arena_t *arena, const char *line, nixie_xy_axis_t *axis) {
    if (strncmp(line, "y-axis", 6) != 0) return 0;
    size_t i = 6;
    size_t ws = skip_ws(line + i);
    if (ws == 0) return 0;
    i += ws;
    if (line[i] != '"') return 0;
    const char *cstart = line + i + 1;
    const char *close = strchr(cstart, '"');
    if (close == NULL || close == cstart) return 0;

    size_t j = (size_t)(close - line) + 1;
    j += skip_ws(line + j);
    if (line[j] != '\0') return 0;

    axis->title = nixie_arena_strndup(arena, cstart, (size_t)(close - cstart));
    return 1;
}

static int try_series(nixie_arena_t *arena, const char *line, const char *kw, nixie_xy_series_type_t type, nixie_xy_chart_t *chart) {
    size_t kwlen = strlen(kw);
    if (strncmp(line, kw, kwlen) != 0) return 0;
    size_t i = kwlen;
    size_t ws = skip_ws(line + i);
    if (ws == 0) return 0;
    i += ws;
    if (line[i] != '[') return 0;

    const char *content_start = line + i + 1;
    const char *close = strchr(content_start, ']');
    if (close == NULL || close == content_start) return 0;

    size_t data_count;
    double *data = parse_numeric_array(arena, content_start, close, &data_count);
    if (data_count == 0) return 0;

    ensure_series_capacity(arena, chart);
    chart->series[chart->series_count].type = type;
    chart->series[chart->series_count].data = data;
    chart->series[chart->series_count].data_count = data_count;
    chart->series_count++;
    return 1;
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

nixie_xy_parse_result_t nixie_xy_parse(nixie_arena_t *arena, const char *text) {
    nixie_xy_parse_result_t result;
    result.chart = NULL;
    result.error = NIXIE_OK;
    result.error_message[0] = '\0';
    result.error_line = -1;

    nixie_sig_lines_t sig = nixie_split_significant_lines(arena, text);

    if (sig.count == 0) {
        result.error = NIXIE_ERROR_EMPTY_INPUT;
        snprintf(result.error_message, sizeof(result.error_message), "Empty mermaid diagram");
        return result;
    }

    const char *header = sig.lines[0].content;
    if (!(strncmp(header, "xychart", 7) == 0 && (header[7] == '\0' || !isalnum((unsigned char)header[7])))) {
        result.error = NIXIE_ERROR_UNKNOWN_HEADER;
        snprintf(result.error_message, sizeof(result.error_message),
                 "Invalid mermaid header: \"%s\". Expected \"xychart-beta\".", header);
        result.error_line = sig.lines[0].line_no;
        return result;
    }

    nixie_xy_chart_t *chart = (nixie_xy_chart_t *)nixie_arena_alloc_zeroed(arena, sizeof(nixie_xy_chart_t));
    if (contains_word_ci(header, "horizontal")) {
        chart->horizontal = 1;
    }

    for (size_t li = 1; li < sig.count; li++) {
        const char *line = sig.lines[li].content;

        if (try_title(arena, line, chart)) continue;
        if (try_axis_categorical(arena, line, "x-axis", &chart->x_axis)) continue;
        if (try_axis_range(arena, line, "x-axis", &chart->x_axis)) continue;
        if (try_axis_range(arena, line, "y-axis", &chart->y_axis)) continue;
        if (try_y_axis_title_only(arena, line, &chart->y_axis)) continue;
        if (try_series(arena, line, "bar", NIXIE_XY_BAR, chart)) continue;
        if (try_series(arena, line, "line", NIXIE_XY_LINE, chart)) continue;

        /* Anything else (unsupported syntax) is silently ignored. */
    }

    /* Auto-derive the y-axis range from the data if not specified. */
    if (!chart->y_axis.has_range && chart->series_count > 0) {
        double min = 0.0, max = 0.0;
        int have_value = 0;
        for (size_t si = 0; si < chart->series_count; si++) {
            for (size_t vi = 0; vi < chart->series[si].data_count; vi++) {
                double v = chart->series[si].data[vi];
                if (!have_value) {
                    min = max = v;
                    have_value = 1;
                } else {
                    if (v < min) min = v;
                    if (v > max) max = v;
                }
            }
        }
        if (have_value) {
            double span = (max - min) != 0.0 ? (max - min) : 1.0;
            min = min - span * 0.1;
            max = max + span * 0.1;
            if (min > 0 && min < span * 0.5) min = 0.0;
            chart->y_axis.has_range = 1;
            chart->y_axis.range_min = min;
            chart->y_axis.range_max = max;
        }
    }
    if (!chart->y_axis.has_range) {
        chart->y_axis.has_range = 1;
        chart->y_axis.range_min = 0.0;
        chart->y_axis.range_max = 100.0;
    }

    result.chart = chart;
    return result;
}
