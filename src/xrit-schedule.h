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
#ifndef XRIT_SCHEDULE_H
#define XRIT_SCHEDULE_H 1

#include <gtk/gtk.h>

/* Daily LRIT/HRIT transmission plan of geostationary weather satellites,
 * ELEKTRO-L 3 and 5 by default. All times are minutes of the UTC day.
 * The plan is read from xrit-schedule.conf in the user configuration
 * directory; the file is written with the built-in plan when missing.
 */
typedef enum {
    XRIT_NONE = 0,
    XRIT_LRIT = 1,
    XRIT_HRIT = 2
} xrit_kind_t;

#define XRIT_SLOT_MAX 8

typedef struct {
    xrit_kind_t kind;
    int first;                  /* Start of the first transmission. */
    int length;                 /* Duration of one transmission. */
    int period;                 /* Repeat interval, 0 = once a day. */
} xrit_slot_t;

typedef struct {
    int catnum;
    gchar *name;
    gchar *cyrillic;            /* Name in Russian letters, or NULL. */
    gchar *position;            /* Orbital slot, free text. */
    gchar *frequency;           /* Downlink, free text. */
    gboolean tentative;         /* Plan not confirmed by reception. */
    xrit_slot_t slot[XRIT_SLOT_MAX];
    int slot_n;
} xrit_sat_t;

typedef struct {
    xrit_kind_t now;            /* What is on the air, or XRIT_NONE. */
    int remaining;              /* Minutes left of it, rounded up. */
    xrit_kind_t next;           /* Next transmission to start. */
    int next_start;             /* Its start, minute of the UTC day. */
    int next_in;                /* Minutes until then, rounded up. */
} xrit_state_t;

/* Read the plan file again. */
void xrit_schedule_reload(void);

int xrit_schedule_count(void);
const xrit_sat_t *xrit_schedule_get(int index);

/* Plan for a satellite, by catalogue number, or NULL. */
const xrit_sat_t *xrit_schedule_find(int catnum);

/* Minute of the UTC day, with fraction, of a Julian date. */
double xrit_minute_of_day(double jul_utc);

void xrit_schedule_state(const xrit_sat_t *sat, double minute,
                         xrit_state_t *state);

/* One line such as "HRIT on air, 8 min left". Free with g_free. */
gchar *xrit_schedule_status_text(const xrit_sat_t *sat, double jul_utc);

/* Height the chart needs for the loaded plan. */
int xrit_schedule_chart_height(void);

void xrit_schedule_render(cairo_t *cr, int width, int height, double jul_utc);

void show_xrit_schedule(GtkWindow *parent);
void show_xrit_schedule_cb(GtkWidget *widget, gpointer data);

#endif
