#include <nixie/nixie.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "class/layout.h"
#include "class/parser.h"
#include "er/layout.h"
#include "er/parser.h"
#include "flowchart/layout.h"
#include "flowchart/parser.h"
#include "sequence/layout.h"
#include "sequence/parser.h"
#include "state/parser.h"
#include "xychart/layout.h"
#include "xychart/parser.h"

#ifndef NIXIE_FIXTURES_DIR
#define NIXIE_FIXTURES_DIR "."
#endif

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("    FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);   \
            return 0;                                                    \
        }                                                                \
    } while (0)

#define RUN(test_fn)                     \
    do {                                 \
        printf("- %s\n", #test_fn);      \
        if (test_fn()) {                 \
            g_pass++;                    \
        } else {                         \
            g_fail++;                    \
        }                                \
    } while (0)

/* ==========================================================================
 * parser
 * ========================================================================== */

static int test_basic_edge(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_flowchart_parse(a, "graph TD\nA-->B\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.graph->direction == NIXIE_DIR_TD, "expected TD direction");
    CHECK(r.graph->node_count == 2, "expected 2 nodes");
    CHECK(r.graph->edge_count == 1, "expected 1 edge");
    CHECK(strcmp(r.graph->nodes[0].id, "A") == 0, "node 0 id should be A");
    CHECK(r.graph->nodes[0].shape == NIXIE_SHAPE_RECTANGLE, "default shape should be rectangle");
    CHECK(r.graph->edges[0].has_arrow_end == 1, "expected an arrow end");
    CHECK(r.graph->edges[0].style == NIXIE_EDGE_SOLID, "expected solid style");
    nixie_arena_destroy(a);
    return 1;
}

typedef struct {
    const char *src;
    nixie_node_shape_t shape;
} shape_case_t;

static const shape_case_t SHAPE_CASES[] = {
    {"A[Rect]", NIXIE_SHAPE_RECTANGLE},
    {"A(Round)", NIXIE_SHAPE_ROUNDED},
    {"A{Diamond}", NIXIE_SHAPE_DIAMOND},
    {"A([Stadium])", NIXIE_SHAPE_STADIUM},
    {"A((Circle))", NIXIE_SHAPE_CIRCLE},
    {"A[[Sub]]", NIXIE_SHAPE_SUBROUTINE},
    {"A(((Double)))", NIXIE_SHAPE_DOUBLECIRCLE},
    {"A{{Hex}}", NIXIE_SHAPE_HEXAGON},
    {"A[(Cyl)]", NIXIE_SHAPE_CYLINDER},
    {"A>Flag]", NIXIE_SHAPE_ASYMMETRIC},
    {"A[/Trap\\]", NIXIE_SHAPE_TRAPEZOID},
    {"A[\\TrapAlt/]", NIXIE_SHAPE_TRAPEZOID_ALT},
};

static int test_all_shapes(void) {
    for (size_t i = 0; i < sizeof(SHAPE_CASES) / sizeof(SHAPE_CASES[0]); i++) {
        nixie_arena_t *a = nixie_arena_create(0);
        char text[128];
        snprintf(text, sizeof(text), "graph TD\n%s\n", SHAPE_CASES[i].src);
        nixie_parse_result_t r = nixie_flowchart_parse(a, text);
        CHECK(r.error == NIXIE_OK, "expected NIXIE_OK for shape case");
        CHECK(r.graph->node_count == 1, "expected exactly 1 node for shape case");
        CHECK(r.graph->nodes[0].shape == SHAPE_CASES[i].shape, "shape mismatch");
        nixie_arena_destroy(a);
    }
    return 1;
}

typedef struct {
    const char *op;
    nixie_edge_style_t style;
    int has_end;
} edge_op_case_t;

static const edge_op_case_t EDGE_CASES[] = {
    {"-->", NIXIE_EDGE_SOLID, 1},
    {"---", NIXIE_EDGE_SOLID, 0},
    {"-.->", NIXIE_EDGE_DOTTED, 1},
    {"-.-", NIXIE_EDGE_DOTTED, 0},
    {"==>", NIXIE_EDGE_THICK, 1},
    {"===", NIXIE_EDGE_THICK, 0},
};

static int test_edge_styles(void) {
    for (size_t i = 0; i < sizeof(EDGE_CASES) / sizeof(EDGE_CASES[0]); i++) {
        nixie_arena_t *a = nixie_arena_create(0);
        char text[64];
        snprintf(text, sizeof(text), "graph TD\nA%sB\n", EDGE_CASES[i].op);
        nixie_parse_result_t r = nixie_flowchart_parse(a, text);
        CHECK(r.error == NIXIE_OK, "expected NIXIE_OK for edge case");
        CHECK(r.graph->edge_count == 1, "expected 1 edge");
        CHECK(r.graph->edges[0].style == EDGE_CASES[i].style, "style mismatch");
        CHECK(r.graph->edges[0].has_arrow_end == EDGE_CASES[i].has_end, "arrow-end mismatch");
        nixie_arena_destroy(a);
    }
    return 1;
}

static int test_chained_edges(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_flowchart_parse(a, "graph TD\nA-->B-->C\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.graph->node_count == 3, "expected 3 nodes");
    CHECK(r.graph->edge_count == 2, "expected 2 edges");
    nixie_arena_destroy(a);
    return 1;
}

static int test_ampersand_groups(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_flowchart_parse(a, "graph TD\nA & B --> C & D\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.graph->node_count == 4, "expected 4 nodes");
    CHECK(r.graph->edge_count == 4, "expected 4 edges (cartesian product)");
    nixie_arena_destroy(a);
    return 1;
}

static int test_bare_node_default(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_flowchart_parse(a, "graph TD\nA-->B\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strcmp(r.graph->nodes[0].label, "A") == 0, "bare node label should equal its id");
    CHECK(r.graph->nodes[0].shape == NIXIE_SHAPE_RECTANGLE, "bare node shape should be rectangle");
    nixie_arena_destroy(a);
    return 1;
}

static int test_invalid_header(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_flowchart_parse(a, "not a header\nA-->B\n");
    CHECK(r.error == NIXIE_ERROR_UNKNOWN_HEADER, "expected NIXIE_ERROR_UNKNOWN_HEADER");
    CHECK(r.error_line == 1, "expected the error on line 1");
    nixie_arena_destroy(a);
    return 1;
}

static int test_empty_input(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_flowchart_parse(a, "");
    CHECK(r.error == NIXIE_ERROR_EMPTY_INPUT, "expected NIXIE_ERROR_EMPTY_INPUT");
    nixie_arena_destroy(a);
    return 1;
}

static int test_text_arrow_label(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_flowchart_parse(a, "graph TD\nA -- Yes --> B\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.graph->edge_count == 1, "expected 1 edge");
    CHECK(r.graph->edges[0].label != NULL, "expected an edge label");
    CHECK(strcmp(r.graph->edges[0].label, "Yes") == 0, "expected label 'Yes'");
    CHECK(r.graph->edges[0].style == NIXIE_EDGE_SOLID, "expected solid style");
    nixie_arena_destroy(a);
    return 1;
}

static int test_pipe_label(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_flowchart_parse(a, "graph TD\nA -->|Yes| B\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.graph->edge_count == 1, "expected 1 edge");
    CHECK(r.graph->edges[0].label != NULL, "expected an edge label");
    CHECK(strcmp(r.graph->edges[0].label, "Yes") == 0, "expected label 'Yes'");
    nixie_arena_destroy(a);
    return 1;
}

/* ==========================================================================
 * state
 * ========================================================================== */

static int test_state_basic_transition(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_state_parse(a, "stateDiagram-v2\nA --> B\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.graph->direction == NIXIE_DIR_TD, "expected default TD direction");
    CHECK(r.graph->node_count == 2, "expected 2 nodes");
    CHECK(r.graph->edge_count == 1, "expected 1 edge");
    CHECK(r.graph->nodes[0].shape == NIXIE_SHAPE_ROUNDED, "state nodes default to rounded shape");
    CHECK(r.graph->edges[0].style == NIXIE_EDGE_SOLID, "state transitions are always solid");
    CHECK(r.graph->edges[0].has_arrow_end == 1, "expected an arrow end");
    CHECK(r.graph->edges[0].has_arrow_start == 0, "expected no arrow start");
    nixie_arena_destroy(a);
    return 1;
}

static int test_state_alias(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_state_parse(a, "stateDiagram-v2\nstate \"My State\" as s1\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.graph->node_count == 1, "expected 1 node");
    CHECK(strcmp(r.graph->nodes[0].id, "s1") == 0, "expected id 's1'");
    CHECK(strcmp(r.graph->nodes[0].label, "My State") == 0, "expected label 'My State'");
    nixie_arena_destroy(a);
    return 1;
}

static int test_state_description_before_reference(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_state_parse(a, "stateDiagram-v2\ns1 : Description\nA --> s1\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    int idx = nixie_strmap_get(r.graph->node_index, "s1", 2);
    CHECK(idx >= 0, "expected s1 to be registered");
    CHECK(strcmp(r.graph->nodes[idx].label, "Description") == 0, "expected the description as label");
    nixie_arena_destroy(a);
    return 1;
}

static int test_state_description_after_reference(void) {
    /* Matches beautiful-mermaid's own "first definition wins" behavior: a
     * description arriving after the state was already registered (with a
     * default label) does not retroactively update the label. */
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_state_parse(a, "stateDiagram-v2\nA --> s1\ns1 : Description\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    int idx = nixie_strmap_get(r.graph->node_index, "s1", 2);
    CHECK(idx >= 0, "expected s1 to be registered");
    CHECK(strcmp(r.graph->nodes[idx].label, "s1") == 0, "expected the default id-as-label, description ignored");
    nixie_arena_destroy(a);
    return 1;
}

static int test_state_pseudostates_unique(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_state_parse(a, "stateDiagram-v2\n[*] --> A\n[*] --> B\nA --> [*]\nB --> [*]\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    /* A, B, and 2 distinct state-start + 2 distinct state-end nodes. */
    CHECK(r.graph->node_count == 6, "expected 6 nodes (2 real + 4 distinct pseudostates)");
    CHECK(r.graph->edge_count == 4, "expected 4 edges");
    int start_count = 0, end_count = 0;
    for (size_t i = 0; i < r.graph->node_count; i++) {
        if (r.graph->nodes[i].shape == NIXIE_SHAPE_STATE_START) start_count++;
        if (r.graph->nodes[i].shape == NIXIE_SHAPE_STATE_END) end_count++;
    }
    CHECK(start_count == 2, "expected 2 distinct state-start nodes");
    CHECK(end_count == 2, "expected 2 distinct state-end nodes");
    nixie_arena_destroy(a);
    return 1;
}

static int test_state_composite_flatten_unreferenced(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_state_parse(a, "stateDiagram-v2\nstate Foo {\n    A --> B\n}\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.graph->node_count == 2, "expected only A and B (Foo never referenced from outside)");
    CHECK(r.graph->edge_count == 1, "expected the inner transition to become a top-level edge");
    nixie_arena_destroy(a);
    return 1;
}

static int test_state_composite_flatten_referenced(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_state_parse(a, "stateDiagram-v2\nX --> Foo\nstate Foo {\n    A --> B\n}\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.graph->node_count == 4, "expected X, Foo, A, B all as top-level nodes");
    CHECK(r.graph->edge_count == 2, "expected X->Foo and A->B");
    nixie_arena_destroy(a);
    return 1;
}

static int test_state_invalid_header(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_state_parse(a, "not a header\nA --> B\n");
    CHECK(r.error == NIXIE_ERROR_UNKNOWN_HEADER, "expected NIXIE_ERROR_UNKNOWN_HEADER");
    CHECK(r.error_line == 1, "expected the error on line 1");
    nixie_arena_destroy(a);
    return 1;
}

static int test_state_empty_input(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_state_parse(a, "");
    CHECK(r.error == NIXIE_ERROR_EMPTY_INPUT, "expected NIXIE_ERROR_EMPTY_INPUT");
    nixie_arena_destroy(a);
    return 1;
}

/* ==========================================================================
 * class
 * ========================================================================== */

static int test_class_basic_relationship(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_class_parse_result_t r = nixie_class_parse(a, "classDiagram\nAnimal <|-- Dog\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->class_count == 2, "expected 2 classes");
    CHECK(r.diagram->relationship_count == 1, "expected 1 relationship");
    CHECK(r.diagram->relationships[0].type == NIXIE_REL_INHERITANCE, "expected inheritance");
    CHECK(r.diagram->relationships[0].marker_at == NIXIE_MARKER_FROM, "expected marker at 'from' for <|--");
    nixie_arena_destroy(a);
    return 1;
}

static int test_class_body_members(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_class_parse_result_t r = nixie_class_parse(
        a, "classDiagram\nclass Animal {\n    +String name\n    -int age$\n    +makeSound(sound) void\n}\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->class_count == 1, "expected 1 class");
    const nixie_class_node_t *cls = &r.diagram->classes[0];
    CHECK(cls->attribute_count == 2, "expected 2 attributes");
    CHECK(cls->method_count == 1, "expected 1 method");
    CHECK(cls->attributes[0].visibility == NIXIE_VIS_PUBLIC, "expected public visibility");
    CHECK(strcmp(cls->attributes[0].name, "name") == 0, "expected attribute name 'name'");
    CHECK(strcmp(cls->attributes[0].type, "String") == 0, "expected attribute type 'String'");
    CHECK(cls->attributes[1].is_static == 1, "expected 'age' to be static ($)");
    CHECK(strcmp(cls->attributes[1].name, "age") == 0, "expected static marker stripped from name");
    CHECK(cls->methods[0].is_method == 1, "expected a method");
    CHECK(strcmp(cls->methods[0].params, "sound") == 0, "expected method params 'sound'");
    CHECK(strcmp(cls->methods[0].type, "void") == 0, "expected method return type 'void'");
    nixie_arena_destroy(a);
    return 1;
}

static int test_class_annotation(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_class_parse_result_t r = nixie_class_parse(a, "classDiagram\nclass Shape {\n    <<abstract>>\n}\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->classes[0].annotation != NULL, "expected an annotation");
    CHECK(strcmp(r.diagram->classes[0].annotation, "abstract") == 0, "expected annotation 'abstract'");
    nixie_arena_destroy(a);
    return 1;
}

static int test_class_generic_label(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_class_parse_result_t r = nixie_class_parse(a, "classDiagram\nclass Box~T~\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strcmp(r.diagram->classes[0].label, "Box<T>") == 0, "expected label 'Box<T>'");
    nixie_arena_destroy(a);
    return 1;
}

static int test_class_inline_attribute(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_class_parse_result_t r = nixie_class_parse(a, "classDiagram\nAnimal : +String name\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->class_count == 1, "expected 1 class");
    CHECK(r.diagram->classes[0].attribute_count == 1, "expected 1 attribute");
    nixie_arena_destroy(a);
    return 1;
}

static int test_class_cardinality_and_label(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_class_parse_result_t r = nixie_class_parse(a, "classDiagram\nOwner \"1\" --> \"*\" Animal : owns\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->relationship_count == 1, "expected 1 relationship");
    const nixie_class_relationship_t *rel = &r.diagram->relationships[0];
    CHECK(rel->type == NIXIE_REL_ASSOCIATION, "expected association");
    CHECK(strcmp(rel->from_cardinality, "1") == 0, "expected from-cardinality '1'");
    CHECK(strcmp(rel->to_cardinality, "*") == 0, "expected to-cardinality '*'");
    CHECK(strcmp(rel->label, "owns") == 0, "expected label 'owns'");
    nixie_arena_destroy(a);
    return 1;
}

static int test_class_namespace_flatten(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_class_parse_result_t r = nixie_class_parse(a, "classDiagram\nnamespace NS {\n    class A\n    class B\n}\nA --> B\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->class_count == 2, "expected A and B as top-level classes");
    CHECK(r.diagram->relationship_count == 1, "expected 1 relationship");
    nixie_arena_destroy(a);
    return 1;
}

static int test_class_invalid_header(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_class_parse_result_t r = nixie_class_parse(a, "not a header\nA --> B\n");
    CHECK(r.error == NIXIE_ERROR_UNKNOWN_HEADER, "expected NIXIE_ERROR_UNKNOWN_HEADER");
    CHECK(r.error_line == 1, "expected the error on line 1");
    nixie_arena_destroy(a);
    return 1;
}

static int test_class_empty_input(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_class_parse_result_t r = nixie_class_parse(a, "");
    CHECK(r.error == NIXIE_ERROR_EMPTY_INPUT, "expected NIXIE_ERROR_EMPTY_INPUT");
    nixie_arena_destroy(a);
    return 1;
}

typedef struct {
    const char *op;
    nixie_relationship_type_t type;
    nixie_marker_at_t marker_at;
} class_rel_op_case_t;

static const class_rel_op_case_t CLASS_REL_CASES[] = {
    {"<|--", NIXIE_REL_INHERITANCE, NIXIE_MARKER_FROM},
    {"--|>", NIXIE_REL_INHERITANCE, NIXIE_MARKER_TO},
    {"*--", NIXIE_REL_COMPOSITION, NIXIE_MARKER_FROM},
    {"--*", NIXIE_REL_COMPOSITION, NIXIE_MARKER_TO},
    {"o--", NIXIE_REL_AGGREGATION, NIXIE_MARKER_FROM},
    {"--o", NIXIE_REL_AGGREGATION, NIXIE_MARKER_TO},
    {"-->", NIXIE_REL_ASSOCIATION, NIXIE_MARKER_TO},
    {"..>", NIXIE_REL_DEPENDENCY, NIXIE_MARKER_TO},
    {"..|>", NIXIE_REL_REALIZATION, NIXIE_MARKER_TO},
};

static int test_class_relationship_types(void) {
    for (size_t i = 0; i < sizeof(CLASS_REL_CASES) / sizeof(CLASS_REL_CASES[0]); i++) {
        nixie_arena_t *a = nixie_arena_create(0);
        char text[64];
        snprintf(text, sizeof(text), "classDiagram\nA %s B\n", CLASS_REL_CASES[i].op);
        nixie_class_parse_result_t r = nixie_class_parse(a, text);
        CHECK(r.error == NIXIE_OK, "expected NIXIE_OK for relationship case");
        CHECK(r.diagram->relationship_count == 1, "expected 1 relationship");
        CHECK(r.diagram->relationships[0].type == CLASS_REL_CASES[i].type, "relationship type mismatch");
        CHECK(r.diagram->relationships[0].marker_at == CLASS_REL_CASES[i].marker_at, "marker_at mismatch");
        nixie_arena_destroy(a);
    }
    return 1;
}

static int test_class_layer_respects_hierarchy(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_class_parse_result_t r = nixie_class_parse(a, "classDiagram\nA <|-- B\nB <|-- C\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    nixie_positioned_class_diagram_t *pcd = nixie_class_layout(a, r.diagram);
    CHECK(pcd->nodes[0].layer == 0, "A should be layer 0");
    CHECK(pcd->nodes[1].layer == 1, "B should be layer 1");
    CHECK(pcd->nodes[2].layer == 2, "C should be layer 2");
    nixie_arena_destroy(a);
    return 1;
}

static int test_class_no_overlap(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_class_parse_result_t r = nixie_class_parse(a, "classDiagram\nclass A\nclass B\nclass C\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    nixie_positioned_class_diagram_t *pcd = nixie_class_layout(a, r.diagram);
    for (size_t i = 0; i < pcd->node_count; i++) {
        for (size_t j = i + 1; j < pcd->node_count; j++) {
            nixie_rect_t ra = {pcd->nodes[i].x, pcd->nodes[i].y, pcd->nodes[i].w, pcd->nodes[i].h};
            nixie_rect_t rb = {pcd->nodes[j].x, pcd->nodes[j].y, pcd->nodes[j].w, pcd->nodes[j].h};
            CHECK(!(ra.x < rb.x + rb.w && ra.x + ra.w > rb.x && ra.y < rb.y + rb.h && ra.y + ra.h > rb.y),
                "classes in the same layer must not overlap");
        }
    }
    nixie_arena_destroy(a);
    return 1;
}

static int test_class_svg_smoke(void) {
    nixie_result_t r = nixie_render_svg("classDiagram\nAnimal <|-- Dog\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strncmp(r.output, "<svg", 4) == 0, "should start with <svg");
    CHECK(strstr(r.output, "class=\"class-node\"") != NULL, "expected class-node groups");
    CHECK(strstr(r.output, "cls-inherit") != NULL, "expected the inheritance marker to be referenced");
    nixie_free(r.output);
    return 1;
}

static int test_class_ascii_smoke(void) {
    nixie_result_t r = nixie_render_ascii("classDiagram\nclass Animal {\n    +String name\n}\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strstr(r.output, "Animal") != NULL, "expected the class name in the output");
    CHECK(strstr(r.output, "name") != NULL, "expected the attribute name in the output");
    nixie_free(r.output);
    return 1;
}

static int test_class_ascii_empty(void) {
    nixie_result_t r = nixie_render_ascii("classDiagram\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK even for a header-only diagram");
    CHECK(strstr(r.output, "empty") != NULL, "expected an empty-diagram placeholder message");
    nixie_free(r.output);
    return 1;
}

/* ==========================================================================
 * xychart
 * ========================================================================== */

static int test_xy_basic_bar(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_xy_parse_result_t r = nixie_xy_parse(a, "xychart-beta\nbar [1, 2, 3]\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.chart->series_count == 1, "expected 1 series");
    CHECK(r.chart->series[0].type == NIXIE_XY_BAR, "expected a bar series");
    CHECK(r.chart->series[0].data_count == 3, "expected 3 data points");
    CHECK(r.chart->series[0].data[1] == 2.0, "expected data[1] == 2");
    nixie_arena_destroy(a);
    return 1;
}

static int test_xy_categories(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_xy_parse_result_t r = nixie_xy_parse(a, "xychart-beta\nx-axis \"Month\" [jan, feb, mar]\nbar [1,2,3]\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strcmp(r.chart->x_axis.title, "Month") == 0, "expected x-axis title 'Month'");
    CHECK(r.chart->x_axis.category_count == 3, "expected 3 categories");
    CHECK(strcmp(r.chart->x_axis.categories[1], "feb") == 0, "expected category 'feb'");
    nixie_arena_destroy(a);
    return 1;
}

static int test_xy_numeric_range(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_xy_parse_result_t r = nixie_xy_parse(a, "xychart-beta\nx-axis 0 --> 10\ny-axis 0 --> 100\nline [1,2,3]\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.chart->x_axis.has_range == 1, "expected x-axis to have a numeric range");
    CHECK(r.chart->x_axis.range_min == 0.0 && r.chart->x_axis.range_max == 10.0, "expected x-axis range 0..10");
    CHECK(r.chart->y_axis.range_min == 0.0 && r.chart->y_axis.range_max == 100.0, "expected y-axis range 0..100");
    nixie_arena_destroy(a);
    return 1;
}

static int test_xy_title_and_y_title_only(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_xy_parse_result_t r = nixie_xy_parse(a, "xychart-beta\ntitle \"Revenue\"\ny-axis \"USD\"\nbar [1,2]\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strcmp(r.chart->title, "Revenue") == 0, "expected title 'Revenue'");
    CHECK(strcmp(r.chart->y_axis.title, "USD") == 0, "expected y-axis title 'USD'");
    CHECK(r.chart->y_axis.has_range == 1, "expected an auto-derived y-axis range even with a title-only y-axis");
    nixie_arena_destroy(a);
    return 1;
}

static int test_xy_horizontal_flag(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_xy_parse_result_t r = nixie_xy_parse(a, "xychart-beta horizontal\nbar [1,2,3]\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.chart->horizontal == 1, "expected horizontal flag set");
    nixie_arena_destroy(a);
    return 1;
}

static int test_xy_auto_yaxis_range(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_xy_parse_result_t r = nixie_xy_parse(a, "xychart-beta\nbar [10, 20, 30]\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.chart->y_axis.has_range == 1, "expected an auto-derived y-axis range");
    CHECK(r.chart->y_axis.range_min == 0.0, "expected the range floored to 0 (all-positive data close to 0)");
    CHECK(r.chart->y_axis.range_max > 30.0, "expected the range padded above the max value");
    nixie_arena_destroy(a);
    return 1;
}

static int test_xy_invalid_header(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_xy_parse_result_t r = nixie_xy_parse(a, "not a header\nbar [1,2]\n");
    CHECK(r.error == NIXIE_ERROR_UNKNOWN_HEADER, "expected NIXIE_ERROR_UNKNOWN_HEADER");
    CHECK(r.error_line == 1, "expected the error on line 1");
    nixie_arena_destroy(a);
    return 1;
}

static int test_xy_empty_input(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_xy_parse_result_t r = nixie_xy_parse(a, "");
    CHECK(r.error == NIXIE_ERROR_EMPTY_INPUT, "expected NIXIE_ERROR_EMPTY_INPUT");
    nixie_arena_destroy(a);
    return 1;
}

static int test_xy_layout_basic(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_xy_parse_result_t r = nixie_xy_parse(a, "xychart-beta\nx-axis [a,b,c]\nbar [1,2,3]\nline [2,3,1]\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    nixie_positioned_xy_chart_t *pc = nixie_xy_layout(a, r.chart);
    CHECK(pc->width > 0 && pc->height > 0, "expected positive chart dimensions");
    CHECK(pc->bar_count == 3, "expected 3 positioned bars (one per category)");
    CHECK(pc->line_count == 1, "expected 1 positioned line series");
    CHECK(pc->lines[0].point_count == 3, "expected 3 points on the line");
    CHECK(pc->x_axis.tick_count == 3, "expected 3 x-axis ticks");
    CHECK(pc->horizontal == 0, "expected vertical layout by default");
    nixie_arena_destroy(a);
    return 1;
}

static int test_xy_layout_horizontal(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_xy_parse_result_t r = nixie_xy_parse(a, "xychart-beta horizontal\nx-axis [a,b]\nbar [1,2]\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    nixie_positioned_xy_chart_t *pc = nixie_xy_layout(a, r.chart);
    CHECK(pc->horizontal == 1, "expected horizontal layout");
    nixie_arena_destroy(a);
    return 1;
}

static int test_xy_svg_smoke(void) {
    nixie_result_t r = nixie_render_svg("xychart-beta\nx-axis [a,b,c]\nbar [1,2,3]\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strncmp(r.output, "<svg", 4) == 0, "should start with <svg");
    CHECK(strstr(r.output, "<path") != NULL, "expected at least one bar path");
    nixie_free(r.output);
    return 1;
}

static int test_xy_ascii_smoke(void) {
    nixie_result_t r = nixie_render_ascii("xychart-beta\nx-axis [jan, feb]\nbar [1,2]\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strstr(r.output, "jan") != NULL, "expected category label in the output");
    nixie_free(r.output);
    return 1;
}

static int test_xy_ascii_empty(void) {
    nixie_result_t r = nixie_render_ascii("xychart-beta\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK even for a header-only chart");
    CHECK(strstr(r.output, "empty") != NULL, "expected an empty-chart placeholder message");
    nixie_free(r.output);
    return 1;
}

/* ==========================================================================
 * er
 * ========================================================================== */

static int test_er_basic_relationship(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_er_parse_result_t r = nixie_er_parse(a, "erDiagram\nCUSTOMER ||--o{ ORDER : places\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->entity_count == 2, "expected 2 entities");
    CHECK(r.diagram->relationship_count == 1, "expected 1 relationship");
    const nixie_er_relationship_t *rel = &r.diagram->relationships[0];
    CHECK(rel->cardinality1 == NIXIE_ER_ONE, "expected entity1 cardinality 'one'");
    CHECK(rel->cardinality2 == NIXIE_ER_ZERO_MANY, "expected entity2 cardinality 'zero-many'");
    CHECK(rel->identifying == 1, "expected an identifying (solid) relationship");
    CHECK(strcmp(rel->label, "places") == 0, "expected label 'places'");
    nixie_arena_destroy(a);
    return 1;
}

static int test_er_entity_attributes(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_er_parse_result_t r = nixie_er_parse(
        a, "erDiagram\nCUSTOMER {\n    string id PK\n    string email UK \"unique contact email\"\n}\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->entity_count == 1, "expected 1 entity");
    const nixie_er_entity_t *e = &r.diagram->entities[0];
    CHECK(e->attribute_count == 2, "expected 2 attributes");
    CHECK(strcmp(e->attributes[0].type, "string") == 0, "expected type 'string'");
    CHECK(strcmp(e->attributes[0].name, "id") == 0, "expected name 'id'");
    CHECK(e->attributes[0].keys == NIXIE_ER_KEY_PK, "expected PK key");
    CHECK(e->attributes[1].keys == NIXIE_ER_KEY_UK, "expected UK key");
    CHECK(e->attributes[1].comment != NULL, "expected a comment");
    CHECK(strcmp(e->attributes[1].comment, "unique contact email") == 0, "expected the comment text");
    nixie_arena_destroy(a);
    return 1;
}

typedef struct {
    const char *symbol;
    nixie_er_cardinality_t expected;
} er_cardinality_case_t;

static const er_cardinality_case_t ER_CARDINALITY_CASES[] = {
    {"||", NIXIE_ER_ONE},
    {"o|", NIXIE_ER_ZERO_ONE},
    {"|o", NIXIE_ER_ZERO_ONE},
    {"}|", NIXIE_ER_MANY},
    {"|{", NIXIE_ER_MANY},
    {"o{", NIXIE_ER_ZERO_MANY},
    {"{o", NIXIE_ER_ZERO_MANY},
    {"}o", NIXIE_ER_ZERO_MANY}, /* the gap beautiful-mermaid's own parser misses -- see parser.c */
};

static int test_er_cardinality_variants(void) {
    for (size_t i = 0; i < sizeof(ER_CARDINALITY_CASES) / sizeof(ER_CARDINALITY_CASES[0]); i++) {
        nixie_arena_t *a = nixie_arena_create(0);
        char text[64];
        snprintf(text, sizeof(text), "erDiagram\nA %s--|| B : rel\n", ER_CARDINALITY_CASES[i].symbol);
        nixie_er_parse_result_t r = nixie_er_parse(a, text);
        CHECK(r.error == NIXIE_OK, "expected NIXIE_OK for cardinality case");
        CHECK(r.diagram->relationship_count == 1, "expected 1 relationship for cardinality case");
        CHECK(r.diagram->relationships[0].cardinality1 == ER_CARDINALITY_CASES[i].expected, "cardinality1 mismatch");
        nixie_arena_destroy(a);
    }
    return 1;
}

static int test_er_non_identifying(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_er_parse_result_t r = nixie_er_parse(a, "erDiagram\nA |o..o| B : maybe\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->relationship_count == 1, "expected 1 relationship");
    CHECK(r.diagram->relationships[0].identifying == 0, "expected a non-identifying (dashed) relationship");
    nixie_arena_destroy(a);
    return 1;
}

static int test_er_label_quote_stripping(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_er_parse_result_t r = nixie_er_parse(a, "erDiagram\nA ||--|| B : \"has a\"\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strcmp(r.diagram->relationships[0].label, "has a") == 0, "expected surrounding quotes stripped");
    nixie_arena_destroy(a);
    return 1;
}

static int test_er_invalid_header(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_er_parse_result_t r = nixie_er_parse(a, "not a header\nA ||--|| B : rel\n");
    CHECK(r.error == NIXIE_ERROR_UNKNOWN_HEADER, "expected NIXIE_ERROR_UNKNOWN_HEADER");
    CHECK(r.error_line == 1, "expected the error on line 1");
    nixie_arena_destroy(a);
    return 1;
}

static int test_er_empty_input(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_er_parse_result_t r = nixie_er_parse(a, "");
    CHECK(r.error == NIXIE_ERROR_EMPTY_INPUT, "expected NIXIE_ERROR_EMPTY_INPUT");
    nixie_arena_destroy(a);
    return 1;
}

static int test_er_layout_basic(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_er_parse_result_t r = nixie_er_parse(a, "erDiagram\nA ||--o{ B : rel1\nB ||--o{ C : rel2\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    nixie_positioned_er_diagram_t *ped = nixie_er_layout(a, r.diagram);
    CHECK(ped->width > 0 && ped->height > 0, "expected positive dimensions");
    CHECK(ped->nodes[0].layer == 0, "A should be layer 0");
    CHECK(ped->nodes[1].layer == 1, "B should be layer 1");
    CHECK(ped->nodes[2].layer == 2, "C should be layer 2");
    nixie_arena_destroy(a);
    return 1;
}

static int test_er_no_overlap(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_er_parse_result_t r = nixie_er_parse(a, "erDiagram\nA {\n string x\n}\nB {\n string y\n}\nC {\n string z\n}\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    nixie_positioned_er_diagram_t *ped = nixie_er_layout(a, r.diagram);
    for (size_t i = 0; i < ped->node_count; i++) {
        for (size_t j = i + 1; j < ped->node_count; j++) {
            nixie_rect_t ra = {ped->nodes[i].x, ped->nodes[i].y, ped->nodes[i].w, ped->nodes[i].h};
            nixie_rect_t rb = {ped->nodes[j].x, ped->nodes[j].y, ped->nodes[j].w, ped->nodes[j].h};
            CHECK(!(ra.x < rb.x + rb.w && ra.x + ra.w > rb.x && ra.y < rb.y + rb.h && ra.y + ra.h > rb.y),
                "entities in the same layer must not overlap");
        }
    }
    nixie_arena_destroy(a);
    return 1;
}

static int test_er_svg_smoke(void) {
    nixie_result_t r = nixie_render_svg("erDiagram\nCUSTOMER ||--o{ ORDER : places\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strncmp(r.output, "<svg", 4) == 0, "should start with <svg");
    CHECK(strstr(r.output, "class=\"entity\"") != NULL, "expected entity groups");
    CHECK(strstr(r.output, "er-relationship") != NULL, "expected a relationship polyline");
    nixie_free(r.output);
    return 1;
}

static int test_er_ascii_smoke(void) {
    nixie_result_t r = nixie_render_ascii("erDiagram\nCUSTOMER {\n    string id PK\n}\nCUSTOMER ||--o{ ORDER : places\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strstr(r.output, "CUSTOMER") != NULL, "expected entity name in the output");
    CHECK(strstr(r.output, "ORDER") != NULL, "expected the other entity name in the output");
    nixie_free(r.output);
    return 1;
}

static int test_er_ascii_empty(void) {
    nixie_result_t r = nixie_render_ascii("erDiagram\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK even for a header-only diagram");
    CHECK(strstr(r.output, "empty") != NULL, "expected an empty-diagram placeholder message");
    nixie_free(r.output);
    return 1;
}

/* ==========================================================================
 * sequence
 * ========================================================================== */

static int test_sequence_basic_message(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_sequence_parse_result_t r = nixie_sequence_parse(a, "sequenceDiagram\nA->>B: Hello\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->actor_count == 2, "expected 2 actors");
    CHECK(strcmp(r.diagram->actors[0].id, "A") == 0, "actor 0 should be A");
    CHECK(strcmp(r.diagram->actors[1].id, "B") == 0, "actor 1 should be B");
    CHECK(r.diagram->message_count == 1, "expected 1 message");
    CHECK(r.diagram->messages[0].from_idx == 0, "message should be from A");
    CHECK(r.diagram->messages[0].to_idx == 1, "message should be to B");
    CHECK(strcmp(r.diagram->messages[0].label, "Hello") == 0, "expected label 'Hello'");
    CHECK(r.diagram->messages[0].line_style == NIXIE_SEQ_SOLID, "expected solid line style");
    CHECK(r.diagram->messages[0].arrow_head == NIXIE_SEQ_FILLED, "expected filled arrow head");
    nixie_arena_destroy(a);
    return 1;
}

typedef struct {
    const char *arrow;
    nixie_seq_line_style_t style;
    nixie_seq_arrow_head_t head;
} seq_arrow_case_t;

static const seq_arrow_case_t SEQ_ARROW_CASES[] = {
    {"->", NIXIE_SEQ_SOLID, NIXIE_SEQ_OPEN},   {"-->", NIXIE_SEQ_DASHED, NIXIE_SEQ_OPEN},
    {"->>", NIXIE_SEQ_SOLID, NIXIE_SEQ_FILLED}, {"-->>", NIXIE_SEQ_DASHED, NIXIE_SEQ_FILLED},
    {"-)", NIXIE_SEQ_SOLID, NIXIE_SEQ_OPEN},    {"--)", NIXIE_SEQ_DASHED, NIXIE_SEQ_OPEN},
    {"-x", NIXIE_SEQ_SOLID, NIXIE_SEQ_FILLED},  {"--x", NIXIE_SEQ_DASHED, NIXIE_SEQ_FILLED},
};

static int test_sequence_arrow_variants(void) {
    for (size_t i = 0; i < sizeof(SEQ_ARROW_CASES) / sizeof(SEQ_ARROW_CASES[0]); i++) {
        nixie_arena_t *a = nixie_arena_create(0);
        char text[64];
        snprintf(text, sizeof(text), "sequenceDiagram\nA%sB: hi\n", SEQ_ARROW_CASES[i].arrow);
        nixie_sequence_parse_result_t r = nixie_sequence_parse(a, text);
        CHECK(r.error == NIXIE_OK, "expected NIXIE_OK for arrow case");
        CHECK(r.diagram->message_count == 1, "expected 1 message for arrow case");
        CHECK(r.diagram->messages[0].line_style == SEQ_ARROW_CASES[i].style, "line style mismatch for arrow case");
        CHECK(r.diagram->messages[0].arrow_head == SEQ_ARROW_CASES[i].head, "arrow head mismatch for arrow case");
        nixie_arena_destroy(a);
    }
    return 1;
}

static int test_sequence_hyphenated_id_no_space_arrow(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_sequence_parse_result_t r = nixie_sequence_parse(a, "sequenceDiagram\nweb-server->>db: Query\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->actor_count == 2, "expected 2 actors");
    CHECK(strcmp(r.diagram->actors[0].id, "web-server") == 0, "expected hyphenated id preserved");
    CHECK(strcmp(r.diagram->actors[1].id, "db") == 0, "expected second actor 'db'");
    nixie_arena_destroy(a);
    return 1;
}

static int test_sequence_participant_and_actor_labels(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_sequence_parse_result_t r =
        nixie_sequence_parse(a, "sequenceDiagram\nparticipant A as Alice\nactor B as Bob\nA->>B: hi\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->actor_count == 2, "expected 2 actors");
    CHECK(strcmp(r.diagram->actors[0].label, "Alice") == 0, "expected label 'Alice'");
    CHECK(r.diagram->actors[0].type == NIXIE_SEQ_PARTICIPANT, "expected participant type");
    CHECK(strcmp(r.diagram->actors[1].label, "Bob") == 0, "expected label 'Bob'");
    CHECK(r.diagram->actors[1].type == NIXIE_SEQ_ACTOR, "expected actor type");
    nixie_arena_destroy(a);
    return 1;
}

static int test_sequence_first_reference_wins(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    /* A is referenced by a message before its explicit "participant A as
     * Alice" declaration -- the later declaration must not override the
     * auto-created default label, mirroring parser.ts's own
     * actorIds-gated no-op (see sequence/parser.c's ensure_actor() doc
     * comment). */
    nixie_sequence_parse_result_t r = nixie_sequence_parse(a, "sequenceDiagram\nA->>B: hi\nparticipant A as Alice\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strcmp(r.diagram->actors[0].label, "A") == 0, "expected the auto-created label to stick");
    nixie_arena_destroy(a);
    return 1;
}

static int test_sequence_activation_marks(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_sequence_parse_result_t r = nixie_sequence_parse(a, "sequenceDiagram\nA->>+B: hi\nB-->>-A: bye\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->message_count == 2, "expected 2 messages");
    CHECK(r.diagram->messages[0].activate == 1, "expected activate on message 0");
    CHECK(r.diagram->messages[0].deactivate == 0, "expected no deactivate on message 0");
    CHECK(r.diagram->messages[1].deactivate == 1, "expected deactivate on message 1");
    nixie_arena_destroy(a);
    return 1;
}

static int test_sequence_note_over_two_actors(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_sequence_parse_result_t r = nixie_sequence_parse(a, "sequenceDiagram\nNote over A,B: Session start\nA->>B: hi\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->note_count == 1, "expected 1 note");
    CHECK(r.diagram->notes[0].position == NIXIE_SEQ_NOTE_OVER, "expected 'over' position");
    CHECK(r.diagram->notes[0].actor_count == 2, "expected 2 actors referenced");
    CHECK(strcmp(r.diagram->notes[0].text, "Session start") == 0, "expected note text");
    CHECK(r.diagram->notes[0].after_index == -1, "expected after_index -1 (before any message)");
    nixie_arena_destroy(a);
    return 1;
}

static int test_sequence_block_loop_and_alt_divider(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_sequence_parse_result_t r = nixie_sequence_parse(
        a, "sequenceDiagram\nloop Health check\nA->>B: Ping\nend\nalt is sick\nB->>A: bad\nelse is well\nB->>A: good\nend\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.diagram->block_count == 2, "expected 2 blocks");
    CHECK(r.diagram->blocks[0].type == NIXIE_SEQ_BLOCK_LOOP, "expected loop block");
    CHECK(strcmp(r.diagram->blocks[0].label, "Health check") == 0, "expected loop label");
    CHECK(r.diagram->blocks[0].start_index == 0 && r.diagram->blocks[0].end_index == 0, "expected loop spanning message 0");
    CHECK(r.diagram->blocks[1].type == NIXIE_SEQ_BLOCK_ALT, "expected alt block");
    CHECK(r.diagram->blocks[1].divider_count == 1, "expected 1 divider (else)");
    CHECK(strcmp(r.diagram->blocks[1].dividers[0].label, "is well") == 0, "expected divider label");
    nixie_arena_destroy(a);
    return 1;
}

static int test_sequence_autonumber_header_accepted(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_sequence_parse_result_t r = nixie_sequence_parse(a, "sequenceDiagram autonumber\nA->>B: hi\n");
    CHECK(r.error == NIXIE_OK, "expected the 'autonumber' directive to be tolerated");
    nixie_arena_destroy(a);
    return 1;
}

static int test_sequence_invalid_header(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_sequence_parse_result_t r = nixie_sequence_parse(a, "not a header\nA->>B: hi\n");
    CHECK(r.error == NIXIE_ERROR_UNKNOWN_HEADER, "expected NIXIE_ERROR_UNKNOWN_HEADER");
    CHECK(r.error_line == 1, "expected the error on line 1");
    nixie_arena_destroy(a);
    return 1;
}

static int test_sequence_empty_input(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_sequence_parse_result_t r = nixie_sequence_parse(a, "");
    CHECK(r.error == NIXIE_ERROR_EMPTY_INPUT, "expected NIXIE_ERROR_EMPTY_INPUT");
    nixie_arena_destroy(a);
    return 1;
}

static int test_sequence_layout_basic(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_sequence_parse_result_t r = nixie_sequence_parse(a, "sequenceDiagram\nA->>B: hi\nB-->>A: bye\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    nixie_positioned_sequence_diagram_t *psd = nixie_sequence_layout(a, r.diagram);
    CHECK(psd->width > 0 && psd->height > 0, "expected positive dimensions");
    CHECK(psd->actor_count == 2, "expected 2 positioned actors");
    CHECK(psd->actors[0].x < psd->actors[1].x, "expected A left of B");
    CHECK(psd->lifeline_count == 2, "expected 2 lifelines");
    CHECK(psd->message_count == 2, "expected 2 positioned messages");
    CHECK(psd->messages[1].y > psd->messages[0].y, "expected the second message below the first");
    nixie_arena_destroy(a);
    return 1;
}

static int test_sequence_layout_note_before_first_message(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    /* Regression check for the fix documented in sequence/layout.c's
     * position_notes_for(): a note before the first message must still be
     * positioned, not silently dropped the way the JS reference drops it. */
    nixie_sequence_parse_result_t r = nixie_sequence_parse(a, "sequenceDiagram\nNote over A,B: start\nA->>B: hi\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    nixie_positioned_sequence_diagram_t *psd = nixie_sequence_layout(a, r.diagram);
    CHECK(psd->note_count == 1, "expected the note to be positioned");
    nixie_arena_destroy(a);
    return 1;
}

static int test_sequence_svg_smoke(void) {
    nixie_result_t r = nixie_render_svg("sequenceDiagram\nA->>B: Hello\nNote over A,B: done\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strncmp(r.output, "<svg", 4) == 0, "should start with <svg");
    CHECK(strstr(r.output, "class=\"actor\"") != NULL, "expected actor groups");
    CHECK(strstr(r.output, "class=\"message\"") != NULL, "expected a message group");
    CHECK(strstr(r.output, "class=\"note\"") != NULL, "expected a note group");
    nixie_free(r.output);
    return 1;
}

static int test_sequence_ascii_smoke(void) {
    nixie_result_t r = nixie_render_ascii("sequenceDiagram\nparticipant A as Alice\nA->>B: Hello\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strstr(r.output, "Alice") != NULL, "expected actor label in the output");
    CHECK(strstr(r.output, "Hello") != NULL, "expected message label in the output");
    nixie_free(r.output);
    return 1;
}

static int test_sequence_ascii_empty(void) {
    nixie_result_t r = nixie_render_ascii("sequenceDiagram\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK even for a header-only diagram");
    CHECK(strstr(r.output, "empty") != NULL, "expected an empty-diagram placeholder message");
    nixie_free(r.output);
    return 1;
}

/* ==========================================================================
 * layout
 * ========================================================================== */


static int rects_overlap(nixie_rect_t a, nixie_rect_t b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

static int test_no_overlap_same_layer(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_flowchart_parse(a, "graph TD\nA\nB\nC\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    nixie_positioned_flowchart_t *pf = nixie_flowchart_layout(a, r.graph, NULL);
    CHECK(pf->node_count == 3, "expected 3 nodes");
    for (size_t i = 0; i < pf->node_count; i++) {
        for (size_t j = i + 1; j < pf->node_count; j++) {
            nixie_rect_t ra = {pf->nodes[i].x, pf->nodes[i].y, pf->nodes[i].w, pf->nodes[i].h};
            nixie_rect_t rb = {pf->nodes[j].x, pf->nodes[j].y, pf->nodes[j].w, pf->nodes[j].h};
            CHECK(!rects_overlap(ra, rb), "nodes in the same layer must not overlap");
        }
    }
    nixie_arena_destroy(a);
    return 1;
}

static int test_layer_respects_direction(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_flowchart_parse(a, "graph TD\nA-->B-->C\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    nixie_positioned_flowchart_t *pf = nixie_flowchart_layout(a, r.graph, NULL);
    CHECK(pf->nodes[0].layer == 0, "A should be in layer 0");
    CHECK(pf->nodes[1].layer == 1, "B should be in layer 1");
    CHECK(pf->nodes[2].layer == 2, "C should be in layer 2");
    nixie_arena_destroy(a);
    return 1;
}

static int test_cycle_terminates(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_flowchart_parse(a, "graph TD\nA-->B\nB-->C\nC-->A\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    nixie_positioned_flowchart_t *pf = nixie_flowchart_layout(a, r.graph, NULL);
    CHECK(pf != NULL, "layout should return a result for a cyclic graph");
    CHECK(pf->node_count == 3, "expected 3 nodes");
    nixie_arena_destroy(a);
    return 1;
}

static int test_disconnected_no_overlap(void) {
    nixie_arena_t *a = nixie_arena_create(0);
    nixie_parse_result_t r = nixie_flowchart_parse(a, "graph TD\nA-->B\nC-->D\n");
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    nixie_positioned_flowchart_t *pf = nixie_flowchart_layout(a, r.graph, NULL);
    for (size_t i = 0; i < pf->node_count; i++) {
        for (size_t j = i + 1; j < pf->node_count; j++) {
            nixie_rect_t ra = {pf->nodes[i].x, pf->nodes[i].y, pf->nodes[i].w, pf->nodes[i].h};
            nixie_rect_t rb = {pf->nodes[j].x, pf->nodes[j].y, pf->nodes[j].w, pf->nodes[j].h};
            CHECK(!rects_overlap(ra, rb), "disconnected components must not overlap");
        }
    }
    nixie_arena_destroy(a);
    return 1;
}

/* ==========================================================================
 * render_svg
 * ========================================================================== */

static int count_occurrences(const char *haystack, const char *needle) {
    int count = 0;
    const char *p = haystack;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += nlen;
    }
    return count;
}

static int test_svg_starts_ends(void) {
    nixie_result_t r = nixie_render_svg("graph TD\nA-->B\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strncmp(r.output, "<svg", 4) == 0, "output should start with <svg");
    size_t len = strlen(r.output);
    CHECK(len >= 6 && strcmp(r.output + len - 6, "</svg>") == 0, "output should end with </svg>");
    nixie_free(r.output);
    return 1;
}

static int test_svg_node_count(void) {
    nixie_result_t r = nixie_render_svg("graph TD\nA-->B\nB-->C\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(count_occurrences(r.output, "class=\"node\"") == 3, "expected 3 node groups");
    nixie_free(r.output);
    return 1;
}

static int test_svg_theme_colors(void) {
    nixie_render_options_t opts = nixie_render_options_default();
    opts.theme = NIXIE_THEME_TOKYO_NIGHT;
    nixie_result_t r = nixie_render_svg("graph TD\nA-->B\n", &opts);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strstr(r.output, "#1A1B26") != NULL, "expected the tokyo-night background color in the output");
    nixie_free(r.output);
    return 1;
}

static int test_svg_balanced_tags(void) {
    nixie_result_t r = nixie_render_svg("graph TD\nA-->B\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(count_occurrences(r.output, "<svg") == 1, "expected exactly one <svg open tag");
    CHECK(count_occurrences(r.output, "</svg>") == 1, "expected exactly one </svg> close tag");
    CHECK(count_occurrences(r.output, "<g ") == count_occurrences(r.output, "</g>"), "expected balanced <g>/</g> tags");
    nixie_free(r.output);
    return 1;
}

/* ==========================================================================
 * render_ascii
 * ========================================================================== */

static int test_ascii_nonempty(void) {
    nixie_result_t r = nixie_render_ascii("graph TD\nA-->B\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strlen(r.output) > 0, "expected non-empty ascii output");
    nixie_free(r.output);
    return 1;
}

static int test_ascii_contains_labels(void) {
    nixie_result_t r = nixie_render_ascii("graph TD\nAlpha[Alpha]-->Beta[Beta]\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(strstr(r.output, "Alpha") != NULL, "expected the Alpha label in the output");
    CHECK(strstr(r.output, "Beta") != NULL, "expected the Beta label in the output");
    nixie_free(r.output);
    return 1;
}

static int test_ascii_unicode_and_ascii_modes(void) {
    nixie_render_options_t opts = nixie_render_options_default();

    opts.use_unicode = 1;
    nixie_result_t r1 = nixie_render_ascii("graph TD\nA-->B\n", &opts);
    CHECK(r1.error == NIXIE_OK, "expected NIXIE_OK for unicode mode");
    CHECK(strlen(r1.output) > 0, "expected non-empty output in unicode mode");
    nixie_free(r1.output);

    opts.use_unicode = 0;
    nixie_result_t r2 = nixie_render_ascii("graph TD\nA-->B\n", &opts);
    CHECK(r2.error == NIXIE_OK, "expected NIXIE_OK for ascii mode");
    CHECK(strlen(r2.output) > 0, "expected non-empty output in ascii mode");
    nixie_free(r2.output);
    return 1;
}

static int test_ascii_empty_graph(void) {
    nixie_result_t r = nixie_render_ascii("graph TD\n", NULL);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK even for a header-only graph");
    CHECK(strstr(r.output, "empty") != NULL, "expected an empty-flowchart placeholder message");
    nixie_free(r.output);
    return 1;
}

/* ==========================================================================
 * png
 * ========================================================================== */

static unsigned read_be32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
}

static int test_png_signature(void) {
    char font_path[512];
    snprintf(font_path, sizeof(font_path), "%s/test-font.ttf", NIXIE_FIXTURES_DIR);

    nixie_render_options_t opts = nixie_render_options_default();
    opts.font_path = font_path;

    nixie_png_result_t r = nixie_render_png("graph TD\nA-->B\n", &opts);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(r.size > 8, "PNG too small");
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    CHECK(memcmp(r.data, sig, 8) == 0, "missing PNG signature");
    nixie_free_png(r.data);
    return 1;
}

static int test_png_ihdr_matches_viewbox(void) {
    char font_path[512];
    snprintf(font_path, sizeof(font_path), "%s/test-font.ttf", NIXIE_FIXTURES_DIR);

    nixie_result_t svg = nixie_render_svg("graph TD\nA-->B\n", NULL);
    CHECK(svg.error == NIXIE_OK, "expected NIXIE_OK for svg");
    double svg_w = 0.0, svg_h = 0.0;
    const char *vb = strstr(svg.output, "viewBox=\"");
    CHECK(vb != NULL, "expected a viewBox attribute");
    CHECK(sscanf(vb, "viewBox=\"%*f %*f %lf %lf", &svg_w, &svg_h) == 2, "failed to parse viewBox");
    nixie_free(svg.output);

    nixie_render_options_t opts = nixie_render_options_default();
    opts.font_path = font_path;
    nixie_png_result_t r = nixie_render_png("graph TD\nA-->B\n", &opts);
    CHECK(r.error == NIXIE_OK, "expected NIXIE_OK");
    CHECK(memcmp(r.data + 12, "IHDR", 4) == 0, "expected IHDR as first chunk");
    unsigned png_w = read_be32(r.data + 16);
    unsigned png_h = read_be32(r.data + 20);
    /* nixie_svg_to_png rounds the viewBox's fractional pixel dimensions to
     * the nearest integer canvas size (see svg_to_png.c), so compare
     * against the same rounding rather than requiring an exact match. */
    CHECK(png_w == (unsigned)(svg_w + 0.5), "PNG width should match SVG viewBox width");
    CHECK(png_h == (unsigned)(svg_h + 0.5), "PNG height should match SVG viewBox height");
    nixie_free_png(r.data);
    return 1;
}

static int test_png_missing_font_is_error(void) {
    nixie_render_options_t opts = nixie_render_options_default();
    opts.font_path = NULL;
    nixie_png_result_t r = nixie_render_png("graph TD\nA-->B\n", &opts);
    CHECK(r.error == NIXIE_ERROR_INVALID_ARGUMENT, "expected NIXIE_ERROR_INVALID_ARGUMENT");
    CHECK(r.data == NULL, "expected NULL data on error");
    return 1;
}

static int test_png_scale_doubles_dimensions(void) {
    char font_path[512];
    snprintf(font_path, sizeof(font_path), "%s/test-font.ttf", NIXIE_FIXTURES_DIR);

    nixie_render_options_t opts1x = nixie_render_options_default();
    opts1x.font_path = font_path;
    nixie_png_result_t r1x = nixie_render_png("graph TD\nA-->B\n", &opts1x);
    CHECK(r1x.error == NIXIE_OK, "expected NIXIE_OK at 1x");
    unsigned w1x = read_be32(r1x.data + 16);
    unsigned h1x = read_be32(r1x.data + 20);
    nixie_free_png(r1x.data);

    nixie_render_options_t opts2x = nixie_render_options_default();
    opts2x.font_path = font_path;
    opts2x.scale = 2.0;
    nixie_png_result_t r2x = nixie_render_png("graph TD\nA-->B\n", &opts2x);
    CHECK(r2x.error == NIXIE_OK, "expected NIXIE_OK at 2x");
    unsigned w2x = read_be32(r2x.data + 16);
    unsigned h2x = read_be32(r2x.data + 20);
    nixie_free_png(r2x.data);

    CHECK(w2x == w1x * 2, "2x scale should double the PNG width");
    CHECK(h2x == h1x * 2, "2x scale should double the PNG height");
    return 1;
}

/* ==========================================================================
 * fixtures (golden-file comparisons; bootstraps missing golden files)
 * ========================================================================== */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)len + 1);
    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

static int write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) return 0;
    fwrite(content, 1, strlen(content), f);
    fclose(f);
    return 1;
}

static int check_fixture(const char *name) {
    char mmd_path[512], svg_path[512], txt_path[512];
    snprintf(mmd_path, sizeof(mmd_path), "%s/%s.mmd", NIXIE_FIXTURES_DIR, name);
    snprintf(svg_path, sizeof(svg_path), "%s/%s.svg", NIXIE_FIXTURES_DIR, name);
    snprintf(txt_path, sizeof(txt_path), "%s/%s.txt", NIXIE_FIXTURES_DIR, name);

    char *mmd = read_file(mmd_path);
    if (mmd == NULL) {
        printf("    FAIL: could not read fixture %s\n", mmd_path);
        return 0;
    }

    nixie_result_t svg = nixie_render_svg(mmd, NULL);
    if (svg.error != NIXIE_OK) {
        printf("    FAIL: svg render error for %s: %s\n", name, svg.error_message);
        free(mmd);
        return 0;
    }

    nixie_result_t ascii = nixie_render_ascii(mmd, NULL);
    if (ascii.error != NIXIE_OK) {
        printf("    FAIL: ascii render error for %s: %s\n", name, ascii.error_message);
        nixie_free(svg.output);
        free(mmd);
        return 0;
    }

    int ok = 1;

    char *golden_svg = read_file(svg_path);
    if (golden_svg == NULL) {
        printf("    NOTE: bootstrapping golden SVG for %s\n", name);
        write_file(svg_path, svg.output);
    } else {
        if (strcmp(golden_svg, svg.output) != 0) {
            printf("    FAIL: SVG output for %s does not match its golden file\n", name);
            ok = 0;
        }
        free(golden_svg);
    }

    char *golden_txt = read_file(txt_path);
    if (golden_txt == NULL) {
        printf("    NOTE: bootstrapping golden ASCII for %s\n", name);
        write_file(txt_path, ascii.output);
    } else {
        if (strcmp(golden_txt, ascii.output) != 0) {
            printf("    FAIL: ASCII output for %s does not match its golden file\n", name);
            ok = 0;
        }
        free(golden_txt);
    }

    nixie_free(svg.output);
    nixie_free(ascii.output);
    free(mmd);
    return ok;
}

static int test_fixture_basic(void) { return check_fixture("basic"); }
static int test_fixture_shapes(void) { return check_fixture("shapes"); }
static int test_fixture_state_basic(void) { return check_fixture("state_basic"); }
static int test_fixture_class_basic(void) { return check_fixture("class_basic"); }
static int test_fixture_xy_basic(void) { return check_fixture("xy_basic"); }
static int test_fixture_er_basic(void) { return check_fixture("er_basic"); }
static int test_fixture_sequence_basic(void) { return check_fixture("sequence_basic"); }

/* ==========================================================================
 * main
 * ========================================================================== */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <parser|state|class|xychart|er|sequence|layout|render_svg|render_ascii|png|fixtures>\n",
                argv[0]);
        return 1;
    }

    const char *group = argv[1];

    if (strcmp(group, "parser") == 0) {
        RUN(test_basic_edge);
        RUN(test_all_shapes);
        RUN(test_edge_styles);
        RUN(test_chained_edges);
        RUN(test_ampersand_groups);
        RUN(test_bare_node_default);
        RUN(test_invalid_header);
        RUN(test_empty_input);
        RUN(test_text_arrow_label);
        RUN(test_pipe_label);
    } else if (strcmp(group, "class") == 0) {
        RUN(test_class_basic_relationship);
        RUN(test_class_body_members);
        RUN(test_class_annotation);
        RUN(test_class_generic_label);
        RUN(test_class_inline_attribute);
        RUN(test_class_cardinality_and_label);
        RUN(test_class_namespace_flatten);
        RUN(test_class_invalid_header);
        RUN(test_class_empty_input);
        RUN(test_class_relationship_types);
        RUN(test_class_layer_respects_hierarchy);
        RUN(test_class_no_overlap);
        RUN(test_class_svg_smoke);
        RUN(test_class_ascii_smoke);
        RUN(test_class_ascii_empty);
    } else if (strcmp(group, "xychart") == 0) {
        RUN(test_xy_basic_bar);
        RUN(test_xy_categories);
        RUN(test_xy_numeric_range);
        RUN(test_xy_title_and_y_title_only);
        RUN(test_xy_horizontal_flag);
        RUN(test_xy_auto_yaxis_range);
        RUN(test_xy_invalid_header);
        RUN(test_xy_empty_input);
        RUN(test_xy_layout_basic);
        RUN(test_xy_layout_horizontal);
        RUN(test_xy_svg_smoke);
        RUN(test_xy_ascii_smoke);
        RUN(test_xy_ascii_empty);
    } else if (strcmp(group, "er") == 0) {
        RUN(test_er_basic_relationship);
        RUN(test_er_entity_attributes);
        RUN(test_er_cardinality_variants);
        RUN(test_er_non_identifying);
        RUN(test_er_label_quote_stripping);
        RUN(test_er_invalid_header);
        RUN(test_er_empty_input);
        RUN(test_er_layout_basic);
        RUN(test_er_no_overlap);
        RUN(test_er_svg_smoke);
        RUN(test_er_ascii_smoke);
        RUN(test_er_ascii_empty);
    } else if (strcmp(group, "sequence") == 0) {
        RUN(test_sequence_basic_message);
        RUN(test_sequence_arrow_variants);
        RUN(test_sequence_hyphenated_id_no_space_arrow);
        RUN(test_sequence_participant_and_actor_labels);
        RUN(test_sequence_first_reference_wins);
        RUN(test_sequence_activation_marks);
        RUN(test_sequence_note_over_two_actors);
        RUN(test_sequence_block_loop_and_alt_divider);
        RUN(test_sequence_autonumber_header_accepted);
        RUN(test_sequence_invalid_header);
        RUN(test_sequence_empty_input);
        RUN(test_sequence_layout_basic);
        RUN(test_sequence_layout_note_before_first_message);
        RUN(test_sequence_svg_smoke);
        RUN(test_sequence_ascii_smoke);
        RUN(test_sequence_ascii_empty);
    } else if (strcmp(group, "state") == 0) {
        RUN(test_state_basic_transition);
        RUN(test_state_alias);
        RUN(test_state_description_before_reference);
        RUN(test_state_description_after_reference);
        RUN(test_state_pseudostates_unique);
        RUN(test_state_composite_flatten_unreferenced);
        RUN(test_state_composite_flatten_referenced);
        RUN(test_state_invalid_header);
        RUN(test_state_empty_input);
    } else if (strcmp(group, "layout") == 0) {
        RUN(test_no_overlap_same_layer);
        RUN(test_layer_respects_direction);
        RUN(test_cycle_terminates);
        RUN(test_disconnected_no_overlap);
    } else if (strcmp(group, "render_svg") == 0) {
        RUN(test_svg_starts_ends);
        RUN(test_svg_node_count);
        RUN(test_svg_theme_colors);
        RUN(test_svg_balanced_tags);
    } else if (strcmp(group, "render_ascii") == 0) {
        RUN(test_ascii_nonempty);
        RUN(test_ascii_contains_labels);
        RUN(test_ascii_unicode_and_ascii_modes);
        RUN(test_ascii_empty_graph);
    } else if (strcmp(group, "png") == 0) {
        RUN(test_png_signature);
        RUN(test_png_ihdr_matches_viewbox);
        RUN(test_png_missing_font_is_error);
        RUN(test_png_scale_doubles_dimensions);
    } else if (strcmp(group, "fixtures") == 0) {
        RUN(test_fixture_basic);
        RUN(test_fixture_shapes);
        RUN(test_fixture_state_basic);
        RUN(test_fixture_class_basic);
        RUN(test_fixture_xy_basic);
        RUN(test_fixture_er_basic);
        RUN(test_fixture_sequence_basic);
    } else {
        fprintf(stderr, "unknown test group: %s\n", group);
        return 1;
    }

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
