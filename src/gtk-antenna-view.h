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
#ifndef GTK_ANTENNA_VIEW_H
#define GTK_ANTENNA_VIEW_H 1

#include <gtk/gtk.h>

#include "qth-data.h"
#include "sgpsdp/sgp4sdp4.h"

/* Antenna drawn in the tracking view. Beamwidths are representative
 * ham values used to judge whether the satellite sits in the lobe:
 * a short VHF/UHF LPDA, a horizontal 120 degree V, and a Discovery
 * Dish sized for L-band (about 70 cm near 1.6 GHz, HPBW about 18 deg).
 */
typedef enum {
    ANTENNA_LPDA = 0,
    ANTENNA_VDIPOLE = 1,
    ANTENNA_DISH = 2
} antenna_kind_t;

typedef struct {
    antenna_kind_t kind;
    double ant_az;              /* Mast azimuth, degrees, north = 0. */
    double ant_el;              /* Mast elevation. Ignored for the V. */
    double sat_az;
    double sat_el;
    int sat_above;              /* Non-zero when sat_el >= 0. */
    const double *track_az;     /* Upcoming sky track, may be NULL. */
    const double *track_el;
    int track_n;
    double cam_yaw;             /* Orbit camera, degrees. */
    double cam_pitch;
    double cam_dist;
    const char *name;           /* Satellite label, may be NULL. */
} antenna_scene_t;

void antenna_scene_render(cairo_t *cr, int width, int height,
                          const antenna_scene_t *scene);

/* Angle between two az/el directions, degrees, in [0, 180]. */
double antenna_pointing_offset(double az1, double el1, double az2, double el2);

/* Full -3 dB beamwidth of the main lobe, degrees. */
double antenna_beamwidth(antenna_kind_t kind);

/* Elevation of the main-lobe axis. The V-dipole stays horizontal and
 * its lobe is fixed steeply upward; the other antennas use ant_el.
 */
double antenna_lobe_elevation(antenna_kind_t kind, double ant_el);

void show_antenna_view(sat_t *sat, qth_t *qth, GtkWindow *parent);
void show_antenna_view_cb(GtkWidget *menuitem, gpointer data);

#endif
