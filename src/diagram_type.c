#include "diagram_type.h"

#include <ctype.h>
#include <string.h>

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* Finds the start of the first line that is non-blank and not a "%%"
 * comment, skipping leading whitespace on that line. Returns a pointer into
 * `text` (never allocates). */
static const char *first_significant_line(const char *text) {
    const char *p = text;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        const char *line_start = p;
        while (*p != '\0' && *p != '\n') {
            p++;
        }

        if (!starts_with(line_start, "%%")) {
            return line_start;
        }
        /* comment line: loop continues, advancing past it */
    }
    return NULL;
}

nixie_diagram_type_t nixie_detect_diagram_type(const char *text) {
    if (text == NULL) {
        return NIXIE_DIAGRAM_UNKNOWN;
    }

    const char *header = first_significant_line(text);
    if (header == NULL) {
        return NIXIE_DIAGRAM_UNKNOWN;
    }

    if (starts_with(header, "graph") || starts_with(header, "flowchart")) {
        return NIXIE_DIAGRAM_FLOWCHART;
    }
    if (starts_with(header, "stateDiagram")) {
        return NIXIE_DIAGRAM_STATE;
    }
    if (starts_with(header, "sequenceDiagram")) {
        return NIXIE_DIAGRAM_SEQUENCE;
    }
    if (starts_with(header, "classDiagram")) {
        return NIXIE_DIAGRAM_CLASS;
    }
    if (starts_with(header, "erDiagram")) {
        return NIXIE_DIAGRAM_ER;
    }
    if (starts_with(header, "xychart")) {
        return NIXIE_DIAGRAM_XYCHART;
    }

    return NIXIE_DIAGRAM_UNKNOWN;
}
