/*
    Gpredict: Real-time satellite tracking and orbit prediction program

    Copyright (C) 2026 The Gpredict contributors.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, visit http://www.fsf.org/
*/
/*
 * Antenna tracking view.
 *
 * Right-click a satellite and this window draws the antenna, its main
 * lobe, and the satellite on a local horizon. Drag to orbit the camera,
 * scroll to zoom. LPDA and Discovery Dish follow the satellite so the
 * mast shows the rotator position; uncheck "Follow satellite" to aim by
 * hand and see the satellite leave the lobe. The V-dipole stays level.
 *
 * The picture is a software projection with Cairo, in the same light
 * chart colours as the polar view. No OpenGL.
 *
 * World axes, metres of diagram rather than real size: +X east, +Y north,
 * +Z up. Azimuth is clockwise from north. The sky dome and the beam are
 * centred on the mast head so a satellite inside the cone really is
 * inside the drawn lobe.
 */
#ifdef HAVE_CONFIG_H
#include <build-config.h>
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>

#include "gtk-antenna-view.h"
#include "gtk-sat-data.h"
#include "predict-tools.h"
#include "time-tools.h"
#include "xrit-schedule.h"

#define SKY_R           1.50
#define GROUND_R        1.78
#define PIVOT_Z         0.50
#define PRIM_MAX        2400
#define TRACK_MAX       80
#define VDIPOLE_LOBE_EL 65.0

typedef struct {
    double x, y, z;
} vec3;

typedef struct {
    int line;
    double depth;
    double r, g, b, a;
    double width;
    double sx[3];
    double sy[3];
} prim_t;

typedef struct {
    prim_t *p;
    int n;
    int max;
    vec3 forward;
    vec3 right;
    vec3 up;
    vec3 target;
    double dist;
    double focal;
    double cx, cy;
} pen_t;

typedef struct {
    GtkWidget *window;
    GtkWidget *area;
    GtkWidget *combo;
    GtkWidget *follow_btn;
    GtkWidget *az_spin;
    GtkWidget *el_spin;
    GtkWidget *hint;
    GtkWidget *status;
    sat_t *sat;
    qth_t qth;
    antenna_kind_t kind;
    int follow;
    int updating;
    double ant_az;
    double ant_el;
    double cam_yaw;
    double cam_pitch;
    double cam_dist;
    double track_az[TRACK_MAX];
    double track_el[TRACK_MAX];
    int track_n;
    int track_wait;
    guint timer;
    int drag;
    double drag_x;
    double drag_y;
    int catnum;
    const xrit_sat_t *xrit;     /* Transmission plan, or NULL. */
} antenna_view_t;

static GSList *open_views = NULL;

static vec3 vmk(double x, double y, double z)
{
    vec3 v;

    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

static vec3 vadd(vec3 a, vec3 b)
{
    return vmk(a.x + b.x, a.y + b.y, a.z + b.z);
}

static vec3 vsub(vec3 a, vec3 b)
{
    return vmk(a.x - b.x, a.y - b.y, a.z - b.z);
}

static vec3 vscale(vec3 a, double s)
{
    return vmk(a.x * s, a.y * s, a.z * s);
}

static double vdot(vec3 a, vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static double vlen(vec3 a)
{
    return sqrt(vdot(a, a));
}

static vec3 vnorm(vec3 a)
{
    double len = vlen(a);

    if (len < 1.0e-12)
        return vmk(0.0, 0.0, 1.0);
    return vscale(a, 1.0 / len);
}

static vec3 vcross(vec3 a, vec3 b)
{
    return vmk(a.y * b.z - a.z * b.y,
               a.z * b.x - a.x * b.z,
               a.x * b.y - a.y * b.x);
}

double antenna_pointing_offset(double az1, double el1, double az2, double el2)
{
    double a1 = az1 * de2ra;
    double e1 = el1 * de2ra;
    double a2 = az2 * de2ra;
    double e2 = el2 * de2ra;
    double c;

    c = sin(e1) * sin(e2) + cos(e1) * cos(e2) * cos(a1 - a2);
    if (c > 1.0)
        c = 1.0;
    if (c < -1.0)
        c = -1.0;
    return acos(c) / de2ra;
}

double antenna_beamwidth(antenna_kind_t kind)
{
    switch (kind) {
    case ANTENNA_VDIPOLE:
        return 100.0;
    case ANTENNA_DISH:
        return 18.0;
    case ANTENNA_LPDA:
    default:
        return 62.0;
    }
}

double antenna_lobe_elevation(antenna_kind_t kind, double ant_el)
{
    if (kind == ANTENNA_VDIPOLE)
        return VDIPOLE_LOBE_EL;
    if (ant_el < 0.0)
        return 0.0;
    if (ant_el > 90.0)
        return 90.0;
    return ant_el;
}

static const char *lobe_phrase(double offset, double beam, int above)
{
    if (!above)
        return _("Below the horizon");
    if (offset <= beam * 0.5)
        return _("Inside the main lobe");
    if (offset <= beam * 0.58)
        return _("Near the lobe edge");
    return _("Outside the main lobe");
}

static const char *kind_name(antenna_kind_t kind)
{
    switch (kind) {
    case ANTENNA_VDIPOLE:
        return _("V-dipole");
    case ANTENNA_DISH:
        return _("Discovery Dish");
    case ANTENNA_LPDA:
    default:
        return _("LPDA");
    }
}

static const char *kind_hint(antenna_kind_t kind)
{
    switch (kind) {
    case ANTENNA_VDIPOLE:
        return _("Turn the heading until the pass lies in the wide lobe. "
                 "The V stays horizontal.");
    case ANTENNA_DISH:
        return _("Keep the satellite inside the narrow cone. "
                 "The faint ring is the first sidelobe.");
    case ANTENNA_LPDA:
    default:
        return _("Point the short-element end at the satellite. "
                 "The blue cone is the -3 dB lobe.");
    }
}

/* Model +Y is the boresight. Elevation tilts that axis up, then azimuth
 * swings it clockwise from north.
 */
static vec3 aim_point(vec3 p, double az_deg, double el_deg)
{
    double az = az_deg * de2ra;
    double el = el_deg * de2ra;
    double y_el = p.y * cos(el) - p.z * sin(el);
    double z_el = p.y * sin(el) + p.z * cos(el);
    vec3 o;

    o.x = p.x * cos(az) + y_el * sin(az);
    o.y = -p.x * sin(az) + y_el * cos(az);
    o.z = z_el + PIVOT_Z;
    return o;
}

static vec3 sky_point(double az_deg, double el_deg, double radius)
{
    double az = az_deg * de2ra;
    double el = el_deg * de2ra;

    return vmk(radius * cos(el) * sin(az),
               radius * cos(el) * cos(az),
               radius * sin(el) + PIVOT_Z);
}

static int project(const pen_t *pen, vec3 p, double *sx, double *sy, double *sz)
{
    vec3 d = vsub(p, pen->target);
    double z = vdot(d, pen->forward) + pen->dist;
    double x = vdot(d, pen->right);
    double y = vdot(d, pen->up);

    if (z < 0.25)
        return 0;
    *sx = pen->cx + x * pen->focal / z;
    *sy = pen->cy - y * pen->focal / z;
    *sz = z;
    return 1;
}

static void add_prim(pen_t *pen, prim_t prim)
{
    if (pen->n >= pen->max)
        return;
    pen->p[pen->n] = prim;
    pen->n++;
}

static double shade_of(vec3 a, vec3 b, vec3 c)
{
    vec3 n = vcross(vsub(b, a), vsub(c, a));
    double len = vlen(n);
    vec3 light = vnorm(vmk(0.30, -0.50, 0.82));
    double d;

    if (len < 1.0e-8)
        return 0.72;
    n = vscale(n, 1.0 / len);
    d = fabs(vdot(n, light));
    return 0.40 + 0.60 * d;
}

static void add_face(pen_t *pen, vec3 a, vec3 b, vec3 c,
                     double r, double g, double bcol, double alpha, int shaded)
{
    prim_t prim;
    double za, zb, zc;
    double k;

    memset(&prim, 0, sizeof(prim));
    if (!project(pen, a, &prim.sx[0], &prim.sy[0], &za))
        return;
    if (!project(pen, b, &prim.sx[1], &prim.sy[1], &zb))
        return;
    if (!project(pen, c, &prim.sx[2], &prim.sy[2], &zc))
        return;

    k = shaded ? shade_of(a, b, c) : 1.0;
    prim.line = 0;
    prim.depth = (za + zb + zc) / 3.0;
    prim.r = r * k;
    prim.g = g * k;
    prim.b = bcol * k;
    prim.a = alpha;
    if (prim.r > 1.0)
        prim.r = 1.0;
    if (prim.g > 1.0)
        prim.g = 1.0;
    if (prim.b > 1.0)
        prim.b = 1.0;
    add_prim(pen, prim);
}

static void add_line(pen_t *pen, vec3 a, vec3 b,
                     double r, double g, double bcol, double alpha, double width);

/* Face cut at the ground plane. The lawn is painted first, so anything
 * below it would show on top; only the part above z = 0 is kept.
 */
static void add_face_above(pen_t *pen, vec3 a, vec3 b, vec3 c,
                           double r, double g, double bcol, double alpha)
{
    vec3 in[3];
    vec3 out[4];
    int n = 0;
    int i;

    in[0] = a;
    in[1] = b;
    in[2] = c;
    for (i = 0; i < 3; i++) {
        vec3 p = in[i];
        vec3 q = in[(i + 1) % 3];

        if (p.z >= 0.0)
            out[n++] = p;
        if ((p.z >= 0.0) != (q.z >= 0.0)) {
            double k = p.z / (p.z - q.z);

            out[n++] = vadd(p, vscale(vsub(q, p), k));
        }
    }
    for (i = 1; i + 1 < n; i++)
        add_face(pen, out[0], out[i], out[i + 1], r, g, bcol, alpha, 0);
}

/* Line cut at the ground plane, same reason. */
static void add_line_above(pen_t *pen, vec3 a, vec3 b,
                           double r, double g, double bcol, double alpha,
                           double width)
{
    if (a.z < 0.0 && b.z < 0.0)
        return;
    if (a.z < 0.0)
        a = vadd(a, vscale(vsub(b, a), a.z / (a.z - b.z)));
    else if (b.z < 0.0)
        b = vadd(b, vscale(vsub(a, b), b.z / (b.z - a.z)));
    add_line(pen, a, b, r, g, bcol, alpha, width);
}

static void add_line(pen_t *pen, vec3 a, vec3 b,
                     double r, double g, double bcol, double alpha, double width)
{
    prim_t prim;
    double za, zb;

    memset(&prim, 0, sizeof(prim));
    if (!project(pen, a, &prim.sx[0], &prim.sy[0], &za))
        return;
    if (!project(pen, b, &prim.sx[1], &prim.sy[1], &zb))
        return;
    prim.line = 1;
    prim.depth = (za + zb) / 2.0 - 0.02;
    prim.r = r;
    prim.g = g;
    prim.b = bcol;
    prim.a = alpha;
    prim.width = width;
    add_prim(pen, prim);
}

static void add_box(pen_t *pen, vec3 c, vec3 ux, vec3 uy, vec3 uz,
                    double r, double g, double bcol)
{
    vec3 p[8];
    static const int quad[6][4] = {
        {0, 1, 3, 2},
        {4, 6, 7, 5},
        {0, 4, 5, 1},
        {2, 3, 7, 6},
        {0, 2, 6, 4},
        {1, 5, 7, 3}
    };
    int ix, iy, iz, n, f;

    n = 0;
    for (ix = 0; ix < 2; ix++) {
        for (iy = 0; iy < 2; iy++) {
            for (iz = 0; iz < 2; iz++) {
                vec3 q = c;

                q = vadd(q, vscale(ux, ix ? 1.0 : -1.0));
                q = vadd(q, vscale(uy, iy ? 1.0 : -1.0));
                q = vadd(q, vscale(uz, iz ? 1.0 : -1.0));
                p[n++] = q;
            }
        }
    }

    for (f = 0; f < 6; f++) {
        vec3 a = p[quad[f][0]];
        vec3 b = p[quad[f][1]];
        vec3 c0 = p[quad[f][2]];
        vec3 d = p[quad[f][3]];

        add_face(pen, a, b, c0, r, g, bcol, 1.0, 1);
        add_face(pen, a, c0, d, r, g, bcol, 1.0, 1);
    }
}

static void add_rod(pen_t *pen, vec3 a, vec3 b, double thick,
                    double r, double g, double bcol)
{
    vec3 dir = vnorm(vsub(b, a));
    vec3 tmp = fabs(dir.z) < 0.85 ? vmk(0, 0, 1) : vmk(1, 0, 0);
    vec3 side = vnorm(vcross(dir, tmp));
    vec3 up = vnorm(vcross(side, dir));
    vec3 mid = vscale(vadd(a, b), 0.5);
    double half = vlen(vsub(b, a)) * 0.5;

    add_box(pen, mid, vscale(side, thick), vscale(dir, half), vscale(up, thick),
            r, g, bcol);
}

static void add_rod_aimed(pen_t *pen, vec3 a, vec3 b, double thick,
                          double r, double g, double bcol,
                          double az, double el)
{
    add_rod(pen, aim_point(a, az, el), aim_point(b, az, el), thick, r, g, bcol);
}

/* Hollow cone (and optional end cap) around the boresight. The rim sits
 * on the true half-angle as seen from the mast head.
 */
static void add_lobe(pen_t *pen, double az, double el,
                     double y_near, double y_far, double half_deg,
                     double r, double g, double bcol, double alpha,
                     int segs, int cap)
{
    vec3 near_ring[40];
    vec3 far_ring[40];
    double th = half_deg * de2ra;
    double rad_near = y_near * tan(th);
    double rad_far = y_far * tan(th);
    int j;

    if (segs > 36)
        segs = 36;
    if (segs < 8)
        segs = 8;

    for (j = 0; j <= segs; j++) {
        double ang = 2.0 * pi * (double)j / (double)segs;
        vec3 pn = vmk(rad_near * cos(ang), y_near, rad_near * sin(ang));
        vec3 pf = vmk(rad_far * cos(ang), y_far, rad_far * sin(ang));

        near_ring[j] = aim_point(pn, az, el);
        far_ring[j] = aim_point(pf, az, el);
    }

    for (j = 0; j < segs; j++) {
        add_face_above(pen, near_ring[j], far_ring[j], far_ring[j + 1],
                       r, g, bcol, alpha);
        add_face_above(pen, near_ring[j], far_ring[j + 1], near_ring[j + 1],
                       r, g, bcol, alpha);
    }

    if (cap) {
        vec3 centre = aim_point(vmk(0.0, y_far, 0.0), az, el);

        for (j = 0; j < segs; j++)
            add_face_above(pen, centre, far_ring[j], far_ring[j + 1],
                           r, g, bcol, alpha + 0.06);
    }

    for (j = 0; j < segs; j++) {
        add_line_above(pen, far_ring[j], far_ring[j + 1],
                       r * 0.35, g * 0.45, bcol, 0.95, 1.6);
        if (j % (segs / 4) == 0)
            add_line_above(pen, near_ring[j], far_ring[j],
                           r * 0.35, g * 0.45, bcol, 0.80, 1.0);
    }
}

static void add_lpda(pen_t *pen, double az, double el)
{
    double y = -0.32;
    double half = 0.30;
    double z = 0.04;
    int i;

    add_rod_aimed(pen, vmk(0, -0.38, z), vmk(0, 0.24, z), 0.012,
                  0.40, 0.43, 0.46, az, el);
    for (i = 0; i < 7; i++) {
        add_rod_aimed(pen, vmk(-half, y, z), vmk(half, y, z), 0.006,
                      0.72, 0.74, 0.77, az, el);
        y += 0.09 * (half / 0.30) + 0.018;
        half *= 0.82;
    }
}

static void add_vdipole(pen_t *pen, double az)
{
    vec3 vertex = vmk(0.0, 0.0, 0.06);
    double reach = 0.48;
    double s = sin(60.0 * de2ra);
    double c = cos(60.0 * de2ra);

    /* 120 degree included angle, opening toward the heading. */
    add_rod_aimed(pen, vertex, vadd(vertex, vmk(s * reach, c * reach, 0.0)),
                  0.007, 0.76, 0.78, 0.80, az, 0.0);
    add_rod_aimed(pen, vertex, vadd(vertex, vmk(-s * reach, c * reach, 0.0)),
                  0.007, 0.76, 0.78, 0.80, az, 0.0);
    add_rod_aimed(pen, vmk(-0.03, -0.03, 0.04), vmk(0.03, 0.03, 0.08),
                  0.012, 0.28, 0.30, 0.32, az, 0.0);
}

static void add_dish(pen_t *pen, double az, double el)
{
    vec3 grid[8][20];
    const int rings = 6;
    const int segs = 18;
    const double radius = 0.30;
    const double focal = 0.26;
    int i, j;

    for (i = 0; i <= rings; i++) {
        double rad = radius * (double)i / (double)rings;
        double y = (rad * rad) / (4.0 * focal);

        for (j = 0; j <= segs; j++) {
            double ang = 2.0 * pi * (double)j / (double)segs;

            grid[i][j] = aim_point(vmk(rad * cos(ang), y, rad * sin(ang)), az, el);
        }
    }

    for (i = 0; i < rings; i++) {
        for (j = 0; j < segs; j++) {
            add_face(pen, grid[i][j], grid[i][j + 1], grid[i + 1][j + 1],
                     0.82, 0.84, 0.86, 1.0, 1);
            add_face(pen, grid[i][j], grid[i + 1][j + 1], grid[i + 1][j],
                     0.82, 0.84, 0.86, 1.0, 1);
        }
    }

    for (j = 0; j < segs; j += 3) {
        for (i = 0; i < rings; i++)
            add_line(pen, grid[i][j], grid[i + 1][j], 0.32, 0.35, 0.38, 1.0, 1.1);
    }
    for (j = 0; j < segs; j++)
        add_line(pen, grid[rings][j], grid[rings][j + 1], 0.22, 0.24, 0.28, 1.0, 1.4);

    add_rod_aimed(pen, vmk(0.0, 0.20, 0.0), vmk(0.0, 0.34, 0.0), 0.018,
                  0.30, 0.32, 0.34, az, el);
    for (j = 0; j < 3; j++) {
        double ang = (90.0 + 120.0 * j) * de2ra;
        vec3 rim = vmk(radius * cos(ang), (radius * radius) / (4.0 * focal),
                       radius * sin(ang));

        add_line(pen, aim_point(rim, az, el), aim_point(vmk(0.0, 0.28, 0.0), az, el),
                 0.30, 0.32, 0.34, 1.0, 1.2);
    }
}

static void add_mount(pen_t *pen)
{
    /* Foot and mast stay upright. The antennas pivot on the mast head. */
    add_box(pen, vmk(0.0, 0.0, 0.02), vmk(0.09, 0.0, 0.0), vmk(0.0, 0.09, 0.0),
            vmk(0.0, 0.0, 0.02), 0.34, 0.36, 0.38);
    add_rod(pen, vmk(0.0, 0.0, 0.04), vmk(0.0, 0.0, PIVOT_Z), 0.016,
            0.30, 0.32, 0.34);
}

static void draw_ground(cairo_t *cr, const pen_t *pen)
{
    int seg = 56;
    int j;

    cairo_new_path(cr);
    for (j = 0; j <= seg; j++) {
        double ang = 2.0 * pi * (double)j / (double)seg;
        double sx, sy, sz;
        vec3 p = vmk(GROUND_R * sin(ang), GROUND_R * cos(ang), 0.0);

        if (!project(pen, p, &sx, &sy, &sz))
            continue;
        if (j == 0)
            cairo_move_to(cr, sx, sy);
        else
            cairo_line_to(cr, sx, sy);
    }
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, 0.91, 0.94, 0.91);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.55);
    cairo_set_line_width(cr, 1.4);
    cairo_stroke(cr);
}

static void add_sky(pen_t *pen)
{
    int j;
    double el_ring[2] = {30.0, 60.0};
    int ring;

    for (j = 0; j < 8; j++) {
        double az = 45.0 * j;
        vec3 prev = sky_point(az, 0.0, SKY_R);
        int step;

        for (step = 1; step <= 4; step++) {
            vec3 p = sky_point(az, 18.0 * step, SKY_R);

            add_line(pen, prev, p, 0.15, 0.15, 0.15, 0.28, 1.0);
            prev = p;
        }
    }

    for (ring = 0; ring < 2; ring++) {
        vec3 prev = sky_point(0.0, el_ring[ring], SKY_R);

        for (j = 1; j <= 36; j++) {
            vec3 p = sky_point(10.0 * j, el_ring[ring], SKY_R);

            add_line(pen, prev, p, 0.15, 0.15, 0.15, 0.32, 1.0);
            prev = p;
        }
    }

    {
        vec3 prev = sky_point(0.0, 0.0, SKY_R);

        for (j = 1; j <= 48; j++) {
            vec3 p = sky_point(7.5 * j, 0.0, SKY_R);

            add_line(pen, prev, p, 0.10, 0.10, 0.10, 0.55, 1.3);
            prev = p;
        }
    }
}

static int prim_farther_first(const void *va, const void *vb)
{
    const prim_t *a = va;
    const prim_t *b = vb;

    if (a->depth > b->depth)
        return -1;
    if (a->depth < b->depth)
        return 1;
    return 0;
}

static void paint_prims(cairo_t *cr, pen_t *pen)
{
    int i;

    qsort(pen->p, (size_t)pen->n, sizeof(prim_t), prim_farther_first);
    for (i = 0; i < pen->n; i++) {
        prim_t *prim = &pen->p[i];

        cairo_set_source_rgba(cr, prim->r, prim->g, prim->b, prim->a);
        if (prim->line) {
            cairo_set_line_width(cr, prim->width);
            cairo_move_to(cr, prim->sx[0], prim->sy[0]);
            cairo_line_to(cr, prim->sx[1], prim->sy[1]);
            cairo_stroke(cr);
        } else {
            cairo_move_to(cr, prim->sx[0], prim->sy[0]);
            cairo_line_to(cr, prim->sx[1], prim->sy[1]);
            cairo_line_to(cr, prim->sx[2], prim->sy[2]);
            cairo_close_path(cr);
            cairo_fill(cr);
        }
    }
}

static void draw_label(cairo_t *cr, const pen_t *pen, vec3 p, const char *text,
                       double r, double g, double b, double size)
{
    double sx, sy, sz;
    cairo_text_extents_t ext;

    if (!project(pen, p, &sx, &sy, &sz))
        return;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, size);
    cairo_text_extents(cr, text, &ext);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_move_to(cr, sx - ext.width / 2.0, sy + ext.height / 2.0);
    cairo_show_text(cr, text);
}

static void draw_track(cairo_t *cr, const pen_t *pen, const antenna_scene_t *scene)
{
    int i;
    int pen_down = 0;
    double prev_az = 0.0;
    double prev_el = 0.0;

    if (scene->track_n < 2 || scene->track_az == NULL || scene->track_el == NULL)
        return;

    cairo_set_source_rgba(cr, 0.0, 0.20, 0.85, 0.95);
    cairo_set_line_width(cr, 2.0);
    for (i = 0; i < scene->track_n; i++) {
        double sx, sy, sz;
        vec3 p;
        double gap;

        if (scene->track_el[i] < 0.0) {
            if (pen_down)
                cairo_stroke(cr);
            pen_down = 0;
            continue;
        }
        gap = 0.0;
        if (pen_down)
            gap = antenna_pointing_offset(prev_az, prev_el,
                                         scene->track_az[i], scene->track_el[i]);
        p = sky_point(scene->track_az[i], scene->track_el[i], SKY_R + 0.02);
        if (!project(pen, p, &sx, &sy, &sz) || gap > 25.0) {
            if (pen_down)
                cairo_stroke(cr);
            pen_down = 0;
            if (!project(pen, p, &sx, &sy, &sz))
                continue;
        }
        if (!pen_down)
            cairo_move_to(cr, sx, sy);
        else
            cairo_line_to(cr, sx, sy);
        pen_down = 1;
        prev_az = scene->track_az[i];
        prev_el = scene->track_el[i];
    }
    if (pen_down)
        cairo_stroke(cr);
}

static void draw_satellite(cairo_t *cr, const pen_t *pen, const antenna_scene_t *scene,
                           double offset, double beam)
{
    double el = scene->sat_above ? scene->sat_el : 0.0;
    vec3 p = sky_point(scene->sat_az, el, SKY_R + 0.04);
    vec3 from;
    double lobe_el = antenna_lobe_elevation(scene->kind, scene->ant_el);
    double sx, sy, sz;
    double fx, fy, fz;
    double dashes[2] = {7.0, 4.0};
    int inside = scene->sat_above && offset <= beam * 0.5;

    from = aim_point(vmk(0.0, 0.18, 0.0), scene->ant_az, lobe_el);
    if (project(pen, from, &fx, &fy, &fz) && project(pen, p, &sx, &sy, &sz)) {
        cairo_set_dash(cr, dashes, 2, 0.0);
        cairo_set_line_width(cr, 1.3);
        if (scene->sat_above)
            cairo_set_source_rgba(cr, 0.56, 0.0, 0.0, 0.90);
        else
            cairo_set_source_rgba(cr, 0.40, 0.40, 0.40, 0.80);
        cairo_move_to(cr, fx, fy);
        cairo_line_to(cr, sx, sy);
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0.0);
    }

    if (!project(pen, p, &sx, &sy, &sz))
        return;

    if (inside)
        cairo_set_source_rgb(cr, 1.0, 0.05, 0.04);
    else if (scene->sat_above)
        cairo_set_source_rgb(cr, 0.56, 0.0, 0.0);
    else
        cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
    cairo_arc(cr, sx, sy, 5.5, 0.0, 2.0 * pi);
    cairo_fill(cr);

    if (inside) {
        cairo_set_source_rgba(cr, 0.0, 0.25, 0.80, 0.95);
        cairo_set_line_width(cr, 1.6);
        cairo_arc(cr, sx, sy, 9.0, 0.0, 2.0 * pi);
        cairo_stroke(cr);
    }

    if (scene->name != NULL && scene->name[0] != '\0') {
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 13.0);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.50);
        cairo_move_to(cr, sx + 10.0, sy - 8.0);
        cairo_show_text(cr, scene->name);
    }
}

static void draw_banner(cairo_t *cr, const antenna_scene_t *scene)
{
    double beam = antenna_beamwidth(scene->kind);
    double lobe_el = antenna_lobe_elevation(scene->kind, scene->ant_el);
    double offset = antenna_pointing_offset(scene->ant_az, lobe_el,
                                            scene->sat_az, scene->sat_el);
    const char *phrase = lobe_phrase(offset, beam, scene->sat_above);
    gchar *line1;
    gchar *line2;
    cairo_text_extents_t ext;
    double width;
    double red, green, blue;

    line1 = g_strdup_printf("%s: %s", kind_name(scene->kind), phrase);
    line2 = g_strdup_printf(_("offset %.0f\302\260    beam %.0f\302\260"), offset, beam);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14.0);
    cairo_text_extents(cr, line1, &ext);
    width = ext.width;
    cairo_set_font_size(cr, 12.0);
    cairo_text_extents(cr, line2, &ext);
    if (ext.width > width)
        width = ext.width;

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.88);
    cairo_rectangle(cr, 8.0, 8.0, width + 16.0, 40.0);
    cairo_fill(cr);

    if (!scene->sat_above) {
        red = 0.35;
        green = 0.35;
        blue = 0.35;
    } else if (offset <= beam * 0.5) {
        red = 0.0;
        green = 0.42;
        blue = 0.0;
    } else if (offset <= beam * 0.58) {
        red = 0.45;
        green = 0.32;
        blue = 0.0;
    } else {
        red = 0.56;
        green = 0.0;
        blue = 0.0;
    }

    cairo_set_source_rgb(cr, red, green, blue);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14.0);
    cairo_move_to(cr, 16.0, 24.0);
    cairo_show_text(cr, line1);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, 16.0, 42.0);
    cairo_show_text(cr, line2);

    g_free(line1);
    g_free(line2);
}

static void setup_camera(pen_t *pen, const antenna_scene_t *scene, int width, int height)
{
    double yaw;
    double pitch;
    double dist;
    vec3 rel;
    vec3 world_up = vmk(0.0, 0.0, 1.0);
    double side;

    yaw = scene->cam_yaw * de2ra;
    pitch = scene->cam_pitch * de2ra;
    dist = scene->cam_dist;
    if (dist < 2.2)
        dist = 4.4;
    if (pitch < 0.05)
        pitch = 0.05;

    pen->target = vmk(0.0, 0.0, 0.30);
    rel = vmk(dist * cos(pitch) * sin(yaw),
              -dist * cos(pitch) * cos(yaw),
              dist * sin(pitch));
    pen->forward = vnorm(vscale(rel, -1.0));
    pen->right = vnorm(vcross(pen->forward, world_up));
    pen->up = vnorm(vcross(pen->right, pen->forward));
    pen->dist = dist;
    side = width < height ? (double)width : (double)height;
    pen->focal = 1.05 * side;
    pen->cx = width / 2.0;
    pen->cy = height / 2.0 + 8.0;
}

void antenna_scene_render(cairo_t *cr, int width, int height,
                          const antenna_scene_t *scene)
{
    pen_t pen;
    double lobe_el;
    double half;

    if (cr == NULL || scene == NULL || width < 8 || height < 8)
        return;

    memset(&pen, 0, sizeof(pen));
    pen.p = g_new0(prim_t, PRIM_MAX);
    pen.max = PRIM_MAX;
    setup_camera(&pen, scene, width, height);

    cairo_save(cr);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    draw_ground(cr, &pen);
    add_sky(&pen);
    add_mount(&pen);

    lobe_el = antenna_lobe_elevation(scene->kind, scene->ant_el);
    half = antenna_beamwidth(scene->kind) * 0.5;

    if (scene->kind == ANTENNA_VDIPOLE) {
        add_vdipole(&pen, scene->ant_az);
        add_lobe(&pen, scene->ant_az, lobe_el, 0.08, 1.05, half,
                 0.20, 0.42, 0.82, 0.12, 28, 1);
    } else if (scene->kind == ANTENNA_DISH) {
        add_dish(&pen, scene->ant_az, scene->ant_el);
        add_lobe(&pen, scene->ant_az, lobe_el, 0.36, 1.05, 16.0,
                 0.72, 0.48, 0.22, 0.05, 24, 0);
        add_lobe(&pen, scene->ant_az, lobe_el, 0.32, 1.46, half,
                 0.16, 0.38, 0.86, 0.14, 28, 1);
    } else {
        add_lpda(&pen, scene->ant_az, scene->ant_el);
        add_lobe(&pen, scene->ant_az + 180.0, -scene->ant_el, 0.12, 0.46, 28.0,
                 0.82, 0.46, 0.18, 0.10, 16, 1);
        add_lobe(&pen, scene->ant_az, lobe_el, 0.22, 1.40, half,
                 0.16, 0.38, 0.86, 0.13, 28, 1);
    }

    paint_prims(cr, &pen);
    draw_track(cr, &pen, scene);
    draw_satellite(cr, &pen, scene,
                   antenna_pointing_offset(scene->ant_az, lobe_el,
                                           scene->sat_az, scene->sat_el),
                   antenna_beamwidth(scene->kind));

    draw_label(cr, &pen, sky_point(0.0, 0.0, SKY_R + 0.22), _("N"),
               0.0, 0.50, 0.0, 15.0);
    draw_label(cr, &pen, sky_point(90.0, 0.0, SKY_R + 0.22), _("E"),
               0.0, 0.50, 0.0, 15.0);
    draw_label(cr, &pen, sky_point(180.0, 0.0, SKY_R + 0.22), _("S"),
               0.0, 0.50, 0.0, 15.0);
    draw_label(cr, &pen, sky_point(270.0, 0.0, SKY_R + 0.22), _("W"),
               0.0, 0.50, 0.0, 15.0);
    draw_label(cr, &pen, sky_point(75.0, 30.0, SKY_R + 0.06), "30",
               0.20, 0.38, 0.20, 11.0);
    draw_label(cr, &pen, sky_point(75.0, 60.0, SKY_R + 0.06), "60",
               0.20, 0.38, 0.20, 11.0);

    draw_banner(cr, scene);
    cairo_restore(cr);
    g_free(pen.p);
}

static antenna_view_t *find_view(int catnum)
{
    GSList *item;

    for (item = open_views; item != NULL; item = item->next) {
        antenna_view_t *view = item->data;

        if (view->catnum == catnum)
            return view;
    }
    return NULL;
}

static void update_status(antenna_view_t *view)
{
    double beam = antenna_beamwidth(view->kind);
    double lobe_el = antenna_lobe_elevation(view->kind, view->ant_el);
    double offset = antenna_pointing_offset(view->ant_az, lobe_el,
                                            view->sat->az, view->sat->el);
    const char *phrase = lobe_phrase(offset, beam, view->sat->el >= 0.0);
    gchar *text;

    if (view->sat->el >= 0.0) {
        text = g_strdup_printf(_("%s    Az %.0f\302\260    El %.0f\302\260    %.0f km    "
                                 "offset %.0f\302\260    beam %.0f\302\260"),
                               phrase, view->sat->az, view->sat->el,
                               view->sat->range, offset, beam);
    } else {
        text = g_strdup_printf(_("%s    Az %.0f\302\260    El %.0f\302\260"),
                               phrase, view->sat->az, view->sat->el);
    }
    if (view->xrit != NULL) {
        gchar *plan = xrit_schedule_status_text(view->xrit, get_current_daynum());
        gchar *both = g_strdup_printf("%s\n%s", text, plan);

        g_free(plan);
        g_free(text);
        text = both;
    }
    gtk_label_set_text(GTK_LABEL(view->status), text);
    g_free(text);
}

static void apply_follow(antenna_view_t *view)
{
    double el;

    if (!view->follow || view->sat == NULL)
        return;
    view->ant_az = view->sat->az;
    if (view->ant_az < 0.0)
        view->ant_az += 360.0;
    if (view->ant_az >= 360.0)
        view->ant_az -= 360.0;
    el = view->sat->el;
    if (el < 0.0)
        el = 0.0;
    if (el > 90.0)
        el = 90.0;
    if (view->kind == ANTENNA_VDIPOLE)
        el = VDIPOLE_LOBE_EL;
    view->ant_el = el;
    view->updating = 1;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(view->az_spin),
                              view->ant_az >= 359.5 ? 0.0 : view->ant_az);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(view->el_spin), view->ant_el);
    view->updating = 0;
}

/* Private copy of a live satellite, converted elements and SGP4/SDP4
 * state included. gtk_sat_data_copy_sat() runs select_ephemeris(), which
 * converts the already converted elements a second time and puts the
 * satellite in the wrong place.
 */
static sat_t *clone_sat(const sat_t *sat)
{
    sat_t *copy = g_new(sat_t, 1);

    *copy = *sat;
    copy->name = g_strdup(sat->name);
    copy->nickname = g_strdup(sat->nickname);
    copy->website = g_strdup(sat->website);
    return copy;
}

static void push_track(antenna_view_t *view, double az, double el)
{
    if (view->track_n >= TRACK_MAX)
        return;
    if (el != el || az != az)
        return;
    view->track_az[view->track_n] = az;
    view->track_el[view->track_n] = el;
    view->track_n++;
}

/* Sky track of the current pass, or of the next one while the satellite
 * is down, sampled evenly from AOS to LOS. Satellites without AOS
 * (geostationary) get no track; they do not move across the sky.
 */
static void rebuild_track(antenna_view_t *view)
{
    sat_t *probe;
    double now;
    double aos;
    double los;
    int i;

    view->track_n = 0;
    probe = clone_sat(view->sat);
    now = get_current_daynum();

    predict_calc(probe, &view->qth, now);
    if (probe->el >= 0.0) {
        /* Walk back to the rise, half a minute at a time. A satellite
         * that is still up six hours earlier never sets as far as this
         * view is concerned (geostationary, also when inclined).
         */
        aos = now;
        for (i = 0; i < 720 && probe->el >= 0.0; i++) {
            aos -= 30.0 / 86400.0;
            predict_calc(probe, &view->qth, aos);
        }
        if (probe->el >= 0.0)
            aos = 0.0;
    } else {
        aos = find_aos(probe, &view->qth, now, 2.0);
    }
    los = aos > 0.1 ? find_los(probe, &view->qth, now, 2.0) : 0.0;

    /* A pass longer than half a day is no pass to draw. */
    if (aos > 0.1 && los > aos && los - aos < 0.5) {
        for (i = 0; i < TRACK_MAX; i++) {
            double t = aos + (los - aos) * (double)i / (double)(TRACK_MAX - 1);

            predict_calc(probe, &view->qth, t);
            push_track(view, probe->az, probe->el < 0.0 ? 0.0 : probe->el);
        }
    }

    gtk_sat_data_free_sat(probe);
}

static gboolean on_tick(gpointer data)
{
    antenna_view_t *view = data;

    predict_calc(view->sat, &view->qth, get_current_daynum());
    if (view->follow)
        apply_follow(view);
    if (view->track_wait <= 0) {
        rebuild_track(view);
        view->track_wait = 45;
    }
    view->track_wait--;
    update_status(view);
    if (view->area != NULL)
        gtk_widget_queue_draw(view->area);
    return TRUE;
}

static void reset_camera(antenna_view_t *view)
{
    view->cam_yaw = 34.0;
    view->cam_pitch = 22.0;
    view->cam_dist = 4.5;
    if (view->area != NULL)
        gtk_widget_queue_draw(view->area);
}

static void on_kind_changed(GtkComboBox *combo, gpointer data)
{
    antenna_view_t *view = data;
    int active = gtk_combo_box_get_active(combo);

    if (view->updating)
        return;
    if (active < ANTENNA_LPDA || active > ANTENNA_DISH)
        active = ANTENNA_LPDA;
    view->kind = (antenna_kind_t)active;

    view->updating = 1;
    if (view->kind == ANTENNA_VDIPOLE) {
        view->follow = 0;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(view->follow_btn), FALSE);
        gtk_widget_set_sensitive(view->az_spin, TRUE);
        gtk_widget_set_sensitive(view->el_spin, FALSE);
        view->ant_el = VDIPOLE_LOBE_EL;
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(view->el_spin), view->ant_el);
    } else {
        view->follow = 1;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(view->follow_btn), TRUE);
        gtk_widget_set_sensitive(view->az_spin, FALSE);
        gtk_widget_set_sensitive(view->el_spin, FALSE);
    }
    view->updating = 0;

    if (view->follow)
        apply_follow(view);
    gtk_label_set_text(GTK_LABEL(view->hint), kind_hint(view->kind));
    update_status(view);
    gtk_widget_queue_draw(view->area);
}

static void on_follow_toggled(GtkToggleButton *button, gpointer data)
{
    antenna_view_t *view = data;

    if (view->updating)
        return;
    view->follow = gtk_toggle_button_get_active(button);
    gtk_widget_set_sensitive(view->az_spin, !view->follow);
    gtk_widget_set_sensitive(view->el_spin,
                             !view->follow && view->kind != ANTENNA_VDIPOLE);
    if (view->follow)
        apply_follow(view);
    update_status(view);
    gtk_widget_queue_draw(view->area);
}

static void on_az_changed(GtkSpinButton *spin, gpointer data)
{
    antenna_view_t *view = data;

    if (view->updating)
        return;
    view->ant_az = gtk_spin_button_get_value(spin);
    update_status(view);
    gtk_widget_queue_draw(view->area);
}

static void on_el_changed(GtkSpinButton *spin, gpointer data)
{
    antenna_view_t *view = data;

    if (view->updating)
        return;
    view->ant_el = gtk_spin_button_get_value(spin);
    update_status(view);
    gtk_widget_queue_draw(view->area);
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    antenna_view_t *view = data;
    antenna_scene_t scene;
    GtkAllocation alloc;

    gtk_widget_get_allocation(widget, &alloc);
    memset(&scene, 0, sizeof(scene));
    scene.kind = view->kind;
    scene.ant_az = view->ant_az;
    scene.ant_el = view->ant_el;
    scene.sat_az = view->sat->az;
    scene.sat_el = view->sat->el;
    scene.sat_above = view->sat->el >= 0.0;
    scene.track_az = view->track_az;
    scene.track_el = view->track_el;
    scene.track_n = view->track_n;
    scene.cam_yaw = view->cam_yaw;
    scene.cam_pitch = view->cam_pitch;
    scene.cam_dist = view->cam_dist;
    scene.name = view->sat->nickname != NULL ? view->sat->nickname : view->sat->name;
    antenna_scene_render(cr, alloc.width, alloc.height, &scene);
    return FALSE;
}

static gboolean on_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    antenna_view_t *view = data;

    (void)widget;
    if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
        reset_camera(view);
        view->drag = 0;
        return TRUE;
    }
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        view->drag = 1;
        view->drag_x = event->x;
        view->drag_y = event->y;
        return TRUE;
    }
    return FALSE;
}

static gboolean on_release(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    antenna_view_t *view = data;

    (void)widget;
    if (event->button == 1)
        view->drag = 0;
    return TRUE;
}

static gboolean on_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    antenna_view_t *view = data;
    double dx;
    double dy;

    (void)widget;
    if (!view->drag)
        return FALSE;
    dx = event->x - view->drag_x;
    dy = event->y - view->drag_y;
    view->drag_x = event->x;
    view->drag_y = event->y;
    /* The scene follows the pointer, so the camera goes the other way. */
    view->cam_yaw -= dx * 0.40;
    view->cam_pitch += dy * 0.30;
    if (view->cam_pitch < 8.0)
        view->cam_pitch = 8.0;
    if (view->cam_pitch > 80.0)
        view->cam_pitch = 80.0;
    gtk_widget_queue_draw(view->area);
    return TRUE;
}

static gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    antenna_view_t *view = data;
    double dy = 0.0;

    (void)widget;
    if (event->direction == GDK_SCROLL_UP)
        dy = -1.0;
    else if (event->direction == GDK_SCROLL_DOWN)
        dy = 1.0;
    else if (event->direction == GDK_SCROLL_SMOOTH)
        dy = event->delta_y;
    if (dy > 0.0)
        view->cam_dist *= 1.08;
    else if (dy < 0.0)
        view->cam_dist /= 1.08;
    if (view->cam_dist < 2.6)
        view->cam_dist = 2.6;
    if (view->cam_dist > 9.0)
        view->cam_dist = 9.0;
    gtk_widget_queue_draw(view->area);
    return TRUE;
}

static void on_reset_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    reset_camera(data);
}

static void on_destroy(GtkWidget *widget, gpointer data)
{
    antenna_view_t *view = data;

    (void)widget;
    if (view->timer != 0) {
        g_source_remove(view->timer);
        view->timer = 0;
    }
    open_views = g_slist_remove(open_views, view);
    if (view->sat != NULL)
        gtk_sat_data_free_sat(view->sat);
    g_free(view);
}

void show_antenna_view(sat_t *sat, qth_t *qth, GtkWindow *parent)
{
    antenna_view_t *view;
    antenna_view_t *existing;
    GtkWidget *vbox;
    GtkWidget *hbox;
    GtkWidget *frame;
    GtkWidget *reset;
    gchar *title;
    const char *nick;

    if (sat == NULL || qth == NULL)
        return;

    existing = find_view(sat->tle.catnr);
    if (existing != NULL) {
        gtk_window_present(GTK_WINDOW(existing->window));
        return;
    }

    view = g_new0(antenna_view_t, 1);
    view->qth.lat = qth->lat;
    view->qth.lon = qth->lon;
    view->qth.alt = qth->alt;
    view->sat = clone_sat(sat);
    view->catnum = sat->tle.catnr;
    view->xrit = xrit_schedule_find(view->catnum);
    view->kind = ANTENNA_LPDA;
    view->follow = 1;
    view->cam_yaw = 34.0;
    view->cam_pitch = 22.0;
    view->cam_dist = 4.5;

    nick = view->sat->nickname != NULL ? view->sat->nickname : view->sat->name;
    if (nick == NULL)
        nick = _("Satellite");
    title = g_strdup_printf("%s - %s", nick, _("Antenna view"));

    view->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(view->window), title);
    g_free(title);
    gtk_window_set_default_size(GTK_WINDOW(view->window), 760, 680);
    if (parent != NULL && GTK_IS_WINDOW(parent)) {
        gtk_window_set_transient_for(GTK_WINDOW(view->window), parent);
        gtk_window_set_destroy_with_parent(GTK_WINDOW(view->window), TRUE);
    }

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
    gtk_container_add(GTK_CONTAINER(view->window), vbox);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("Antenna")), FALSE, FALSE, 0);
    view->combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view->combo), _("LPDA"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view->combo), _("V-dipole"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view->combo), _("Discovery Dish"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(view->combo), 0);
    gtk_box_pack_start(GTK_BOX(hbox), view->combo, FALSE, FALSE, 0);

    view->follow_btn = gtk_check_button_new_with_label(_("Follow satellite"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(view->follow_btn), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), view->follow_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("Az")), FALSE, FALSE, 0);
    view->az_spin = gtk_spin_button_new_with_range(0.0, 359.0, 1.0);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(view->az_spin), TRUE);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(view->az_spin), 0);
    gtk_widget_set_sensitive(view->az_spin, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), view->az_spin, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("El")), FALSE, FALSE, 0);
    view->el_spin = gtk_spin_button_new_with_range(0.0, 90.0, 1.0);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(view->el_spin), 0);
    gtk_widget_set_sensitive(view->el_spin, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), view->el_spin, FALSE, FALSE, 0);

    reset = gtk_button_new_with_label(_("Reset view"));
    gtk_box_pack_end(GTK_BOX(hbox), reset, FALSE, FALSE, 0);

    if (view->xrit != NULL) {
        GtkWidget *plan = gtk_button_new_with_label(_("Transmission schedule"));

        gtk_widget_set_tooltip_text(plan,
                                    _("Show when this satellite sends LRIT and HRIT."));
        g_signal_connect(plan, "clicked", G_CALLBACK(show_xrit_schedule_cb),
                         view->window);
        gtk_box_pack_end(GTK_BOX(hbox), plan, FALSE, FALSE, 0);
    }

    view->hint = gtk_label_new(kind_hint(view->kind));
    gtk_widget_set_halign(view->hint, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(view->hint), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), view->hint, FALSE, FALSE, 0);

    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);

    view->area = gtk_drawing_area_new();
    gtk_widget_set_size_request(view->area, 520, 420);
    gtk_widget_set_hexpand(view->area, TRUE);
    gtk_widget_set_vexpand(view->area, TRUE);
    gtk_widget_add_events(view->area,
                          GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK |
                          GDK_SMOOTH_SCROLL_MASK);
    gtk_widget_set_tooltip_text(view->area,
                                _("Drag to rotate. Scroll to zoom. Double-click to reset."));
    gtk_container_add(GTK_CONTAINER(frame), view->area);

    view->status = gtk_label_new("");
    gtk_widget_set_halign(view->status, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), view->status, FALSE, FALSE, 0);

    g_signal_connect(view->combo, "changed", G_CALLBACK(on_kind_changed), view);
    g_signal_connect(view->follow_btn, "toggled", G_CALLBACK(on_follow_toggled), view);
    g_signal_connect(view->az_spin, "value-changed", G_CALLBACK(on_az_changed), view);
    g_signal_connect(view->el_spin, "value-changed", G_CALLBACK(on_el_changed), view);
    g_signal_connect(reset, "clicked", G_CALLBACK(on_reset_clicked), view);
    g_signal_connect(view->area, "draw", G_CALLBACK(on_draw), view);
    g_signal_connect(view->area, "button-press-event", G_CALLBACK(on_press), view);
    g_signal_connect(view->area, "button-release-event", G_CALLBACK(on_release), view);
    g_signal_connect(view->area, "motion-notify-event", G_CALLBACK(on_motion), view);
    g_signal_connect(view->area, "scroll-event", G_CALLBACK(on_scroll), view);
    g_signal_connect(view->window, "destroy", G_CALLBACK(on_destroy), view);

    open_views = g_slist_append(open_views, view);
    on_tick(view);
    view->timer = g_timeout_add_seconds(1, on_tick, view);
    gtk_widget_show_all(view->window);
}

void show_antenna_view_cb(GtkWidget *menuitem, gpointer data)
{
    sat_t *sat;
    qth_t *qth;
    GtkWindow *parent = NULL;

    sat = SAT(g_object_get_data(G_OBJECT(menuitem), "sat"));
    qth = (qth_t *)g_object_get_data(G_OBJECT(menuitem), "qth");
    if (GTK_IS_WINDOW(data))
        parent = GTK_WINDOW(data);
    show_antenna_view(sat, qth, parent);
}
