#ifndef NIXIE_STATE_PARSER_H
#define NIXIE_STATE_PARSER_H

#include "../arena.h"
#include "../flowchart/parser.h" /* reuses nixie_parse_result_t as-is */

/*
 * Parses stateDiagram/stateDiagram-v2 mermaid text into the SAME
 * nixie_mm_graph_t the flowchart parser produces (states become 'rounded'
 * nodes, [*] pseudostates become 'state-start'/'state-end' nodes) -- so the
 * rest of the pipeline (nixie_flowchart_layout, nixie_flowchart_render_svg,
 * nixie_flowchart_render_ascii) is reused unchanged. Composite states
 * (`state X { ... }`) are recognized just enough to not break parsing, but
 * are flattened: their contents become top-level nodes/edges with no visual
 * grouping box (see the project plan's "estados compostos" scoping note).
 */
nixie_parse_result_t nixie_state_parse(nixie_arena_t *arena, const char *text);

#endif /* NIXIE_STATE_PARSER_H */
