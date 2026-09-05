#ifndef NIXIE_DIAGRAM_TYPE_H
#define NIXIE_DIAGRAM_TYPE_H

typedef enum nixie_diagram_type {
    NIXIE_DIAGRAM_FLOWCHART,
    NIXIE_DIAGRAM_STATE,
    NIXIE_DIAGRAM_SEQUENCE,
    NIXIE_DIAGRAM_CLASS,
    NIXIE_DIAGRAM_ER,
    NIXIE_DIAGRAM_XYCHART,
    NIXIE_DIAGRAM_UNKNOWN
} nixie_diagram_type_t;

/* Sniffs the diagram type from the first non-blank, non-comment ("%%") line
 * of `text`, mirroring beautiful-mermaid/src/index.ts's detectDiagramType().
 * Does not validate the rest of the header (e.g. the direction token on a
 * flowchart header) -- that's the per-type parser's job. */
nixie_diagram_type_t nixie_detect_diagram_type(const char *text);

#endif /* NIXIE_DIAGRAM_TYPE_H */
