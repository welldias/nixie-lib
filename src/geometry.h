#ifndef NIXIE_GEOMETRY_H
#define NIXIE_GEOMETRY_H

typedef struct nixie_point {
    double x;
    double y;
} nixie_point_t;

typedef struct nixie_rect {
    double x;
    double y;
    double w;
    double h;
} nixie_rect_t;

#endif /* NIXIE_GEOMETRY_H */
