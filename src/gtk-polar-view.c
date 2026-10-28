/*
  Gpredict: Real-time satellite tracking and orbit prediction program

  Copyright (C)  2001-2017  Alexandru Csete, OZ9AEC
  Copyright (C)       2011  Charles Suprin, AA1 VS

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
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <math.h>

#ifdef HAVE_CONFIG_H
#include <build-config.h>
#endif

#include "config-keys.h"
#include "gpredict-utils.h"
#include "gtk-polar-view-popup.h"
#include "gtk-polar-view.h"
#include "gtk-sat-data.h"
#include "mod-cfg-get-param.h"
#include "orbit-tools.h"
#include "sat-cfg.h"
#include "sat-info.h"
#include "sat-log.h"
#include "sgpsdp/sgp4sdp4.h"
#include "time-tools.h"

#define POLV_DEFAULT_SIZE 100
#define POLV_DEFAULT_MARGIN 25
#define MARKER_SIZE_HALF 3

/* extra size for line outside 0 deg circle (inside margin) */
#define POLV_LINE_EXTRA 5

static void update_sat(gpointer key, gpointer value, gpointer data);
static void update_track(gpointer key, gpointer value, gpointer data);

static GtkBoxClass *parent_class = NULL;

/* Convert rgba color to cairo-friendly format */
static void rgba_to_cairo(guint32 rgba, gdouble *r, gdouble *g, gdouble *b,
                          gdouble *a)
{
    *r = ((rgba >> 24) & 0xFF) / 255.0;
    *g = ((rgba >> 16) & 0xFF) / 255.0;
    *b = ((rgba >> 8) & 0xFF) / 255.0;
    *a = (rgba & 0xFF) / 255.0;
}

static void gtk_polar_view_store_showtracks(GtkPolarView *pv)
{
    mod_cfg_set_integer_list_boolean(pv->cfgdata, pv->showtracks_on,
                                     MOD_CFG_POLAR_SECTION,
                                     MOD_CFG_POLAR_SHOWTRACKS);
    mod_cfg_set_integer_list_boolean(pv->cfgdata, pv->showtracks_off,
                                     MOD_CFG_POLAR_SECTION,
                                     MOD_CFG_POLAR_HIDETRACKS);
}

static void gtk_polar_view_load_showtracks(GtkPolarView *pv)
{
    mod_cfg_get_integer_list_boolean(pv->cfgdata, MOD_CFG_POLAR_SECTION,
                                     MOD_CFG_POLAR_HIDETRACKS,
                                     pv->showtracks_off);

    mod_cfg_get_integer_list_boolean(pv->cfgdata, MOD_CFG_POLAR_SECTION,
                                     MOD_CFG_POLAR_SHOWTRACKS,
                                     pv->showtracks_on);
}

static void free_sat_obj(gpointer data)
{
    sat_obj_t *obj = SAT_OBJ(data);
    if (obj)
    {
        g_free(obj->nickname);
        g_free(obj->tooltip);
        g_slist_free_full(obj->track_points, g_free);
        if (obj->pass)
            free_pass(obj->pass);
        g_free(obj);
    }
}

static void gtk_polar_view_destroy(GtkWidget *widget)
{
    GtkPolarView *polv = GTK_POLAR_VIEW(widget);

    gtk_polar_view_store_showtracks(polv);

    g_free(polv->curs_text);
    polv->curs_text = NULL;

    g_free(polv->next_text);
    polv->next_text = NULL;

    g_free(polv->sel_text);
    polv->sel_text = NULL;

    g_free(polv->font);
    polv->font = NULL;

    if (polv->obj)
    {
        g_hash_table_destroy(polv->obj);
        polv->obj = NULL;
    }

    if (polv->showtracks_on)
    {
        g_hash_table_destroy(polv->showtracks_on);
        polv->showtracks_on = NULL;
    }

    if (polv->showtracks_off)
    {
        g_hash_table_destroy(polv->showtracks_off);
        polv->showtracks_off = NULL;
    }

    (*GTK_WIDGET_CLASS(parent_class)->destroy)(widget);
}

static void gtk_polar_view_class_init(GtkPolarViewClass *class,
                                      gpointer class_data)
{
    GtkWidgetClass *widget_class = (GtkWidgetClass *)class;

    (void)class_data;

    widget_class->destroy = gtk_polar_view_destroy;

    parent_class = g_type_class_peek_parent(class);
}

static void gtk_polar_view_init(GtkPolarView *polview, gpointer g_class)
{
    (void)g_class;

    polview->sats = NULL;
    polview->qth = NULL;
    polview->obj = NULL;
    polview->naos = 0.0;
    polview->ncat = 0;
    polview->size = 0;
    polview->r = 0;
    polview->cx = 0;
    polview->cy = 0;
    polview->refresh = 0;
    polview->counter = 0;
    polview->swap = 0;
    polview->az_offset = 0;
    polview->rotating = FALSE;
    polview->rotate_angle = 0.0;
    polview->scroll_acc = 0.0;
    polview->showmotion = TRUE;
    polview->satname = FALSE;
    polview->satmarker = FALSE;
    polview->qthinfo = FALSE;
    polview->eventinfo = FALSE;
    polview->cursinfo = FALSE;
    polview->extratick = FALSE;
    polview->resize = FALSE;
    polview->curs_text = NULL;
    polview->next_text = NULL;
    polview->sel_text = NULL;
    polview->font = NULL;
}

GType gtk_polar_view_get_type()
{
    static GType gtk_polar_view_type = 0;

    if (!gtk_polar_view_type)
    {
        static const GTypeInfo gtk_polar_view_info = {
            sizeof(GtkPolarViewClass),
            NULL, /* base init */
            NULL, /* base finalise */
            (GClassInitFunc)gtk_polar_view_class_init,
            NULL, /* class finalise */
            NULL, /* class data */
            sizeof(GtkPolarView),
            5, /* n_preallocs */
            (GInstanceInitFunc)gtk_polar_view_init,
            NULL};

        gtk_polar_view_type = g_type_register_static(
            GTK_TYPE_BOX, "GtkPolarView", &gtk_polar_view_info, 0);
    }

    return gtk_polar_view_type;
}

/* Convert Az/El to canvas based XY coordinates. */
static void azel_to_xy(GtkPolarView *p, gdouble az, gdouble el, gfloat *x,
                       gfloat *y)
{
    gdouble rel;

    if (el < 0.0)
    {
        /* FIXME: generate bug report */

        *x = 0.0;
        *y = 0.0;

        return;
    }

    /* extra rotation is clockwise, in tenths of a degree */
    az += p->az_offset / 10.0;
    if (az >= 360.0)
        az = fmod(az, 360.0);
    else if (az < 0.0)
        az = fmod(az, 360.0) + 360.0;

    /* convert angles to radians */
    az = de2ra * az;
    el = de2ra * el;

    /* radius @ el */
    rel = p->r - (2 * p->r * el) / M_PI;

    switch (p->swap)
    {

    case POLAR_VIEW_NWSE:
        az = 2 * M_PI - az;
        break;

    case POLAR_VIEW_SENW:
        az = M_PI - az;
        break;

    case POLAR_VIEW_SWNE:
        az = M_PI + az;
        break;

    default:
        break;
    }

    *x = (gfloat)(p->cx + rel * sin(az));
    *y = (gfloat)(p->cy - rel * cos(az));
}

/* Convert canvas based coordinates to Az/El. */
static void xy_to_azel(GtkPolarView *p, gfloat x, gfloat y, gfloat *az,
                       gfloat *el)
{
    gfloat rel;

    /* distance from center to cursor */
    rel = p->r - sqrt((x - p->cx) * (x - p->cx) + (y - p->cy) * (y - p->cy));

    /* scale according to p->r = 90 deg */
    *el = 90.0 * rel / p->r;

    if (x >= p->cx)
    {
        /* 1. and 2. quadrant */
        *az = atan2(x - p->cx, p->cy - y) / de2ra;
    }
    else
    {
        /* 3 and 4. quadrant */
        *az = 360 + atan2(x - p->cx, p->cy - y) / de2ra;
    }

    /* correct for orientation */
    switch (p->swap)
    {

    case POLAR_VIEW_NWSE:
        *az = 360.0 - *az;
        break;

    case POLAR_VIEW_SENW:
        if (*az <= 180)
            *az = 180.0 - *az;
        else
            *az = 540.0 - *az;
        break;

    case POLAR_VIEW_SWNE:
        if (*az >= 180.0)
            *az = *az - 180.0;
        else
            *az = 180.0 + *az;
        break;

    default:
        break;
    }

    *az -= p->az_offset / 10.0;
    if (*az >= 360.0)
        *az = fmod(*az, 360.0);
    else if (*az < 0.0)
        *az = fmod(*az, 360.0) + 360.0;
}

/* Keep a rotation offset inside one full turn. */
static gint norm_az_offset(gint tenths)
{
    tenths %= 3600;
    if (tenths < 0)
        tenths += 3600;

    return tenths;
}

/* Push a compass label outside the circle and choose how to anchor it. */
static void offset_compass_label(GtkPolarView *polv, gfloat *x, gfloat *y,
                                 gboolean *vertical, gboolean *before)
{
    gfloat dx = *x - (gfloat)polv->cx;
    gfloat dy = *y - (gfloat)polv->cy;
    gfloat len = hypotf(dx, dy);

    if (len < 1.0f)
        len = 1.0f;

    *x += POLV_LINE_EXTRA * dx / len;
    *y += POLV_LINE_EXTRA * dy / len;

    /* The label sits off the nearest side of the circle. */
    *vertical = fabsf(dy) >= fabsf(dx);
    if (*vertical)
        *before = dy < 0.0f;
    else
        *before = dx < 0.0f;
}

static void draw_compass_label(cairo_t *cr, PangoLayout *layout,
                               GtkPolarView *polv, gdouble az, const gchar *text)
{
    gfloat x, y;
    gboolean vertical, before;
    gint tw, th;

    azel_to_xy(polv, az, 0.0, &x, &y);
    offset_compass_label(polv, &x, &y, &vertical, &before);
    pango_layout_set_text(layout, text, -1);
    pango_layout_get_pixel_size(layout, &tw, &th);

    if (vertical)
        cairo_move_to(cr, x - tw / 2.0, before ? y - th : y);
    else
        cairo_move_to(cr, before ? x - tw : x, y - th / 2.0);

    pango_cairo_show_layout(cr, layout);
}

static void draw_el_label(cairo_t *cr, PangoLayout *layout, GtkPolarView *polv,
                          gdouble el, const gchar *text)
{
    gfloat x, y;
    gint tw, th;

    azel_to_xy(polv, 45.0, el, &x, &y);
    pango_layout_set_text(layout, text, -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_move_to(cr, x - tw / 2.0, y - th / 2.0);
    pango_cairo_show_layout(cr, layout);
}

/* Arrow from the satellite toward where it will be a short time later. */
static void draw_motion_arrow(cairo_t *cr, gdouble x0, gdouble y0, gdouble x1,
                              gdouble y1)
{
    gdouble dx = x1 - x0;
    gdouble dy = y1 - y0;
    gdouble len = hypot(dx, dy);
    gdouble ux, uy, px, py;

    if (len < 1.0)
        return;

    ux = dx / len;
    uy = dy / len;
    px = -uy;
    py = ux;

    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y1);
    cairo_stroke(cr);

    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x1 - 8.0 * ux + 3.5 * px, y1 - 8.0 * uy + 3.5 * py);
    cairo_line_to(cr, x1 - 8.0 * ux - 3.5 * px, y1 - 8.0 * uy - 3.5 * py);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    GtkPolarView *polv = GTK_POLAR_VIEW(data);
    gdouble r, g, b, a;
    gfloat x, y;
    PangoLayout *layout;
    PangoFontDescription *font_desc;
    gint tw, th;
    GHashTableIter iter;
    gpointer key, value;
    sat_obj_t *obj;
    GSList *node;
    gdouble *point;
    guint i;

    (void)widget;

    /* Background */
    rgba_to_cairo(polv->col_bgd, &r, &g, &b, &a);
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_paint(cr);

    /* Set up font */
    layout = pango_cairo_create_layout(cr);
    font_desc =
        pango_font_description_from_string(polv->font ? polv->font : "Sans 9");
    pango_layout_set_font_description(layout, font_desc);

    /* Axis color for circles and lines */
    rgba_to_cairo(polv->col_axis, &r, &g, &b, &a);
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_set_line_width(cr, 1.0);

    /* 0 degree circle */
    cairo_arc(cr, polv->cx, polv->cy, polv->r, 0, 2 * M_PI);
    cairo_stroke(cr);

    /* 30 degree circle */
    cairo_arc(cr, polv->cx, polv->cy, 0.6667 * polv->r, 0, 2 * M_PI);
    cairo_stroke(cr);

    /* 60 degree circle */
    cairo_arc(cr, polv->cx, polv->cy, 0.333 * polv->r, 0, 2 * M_PI);
    cairo_stroke(cr);

    /* Horizontal line */
    cairo_move_to(cr, polv->cx - polv->r - POLV_LINE_EXTRA, polv->cy);
    cairo_line_to(cr, polv->cx + polv->r + POLV_LINE_EXTRA, polv->cy);
    cairo_stroke(cr);

    /* Vertical line */
    cairo_move_to(cr, polv->cx, polv->cy - polv->r - POLV_LINE_EXTRA);
    cairo_line_to(cr, polv->cx, polv->cy + polv->r + POLV_LINE_EXTRA);
    cairo_stroke(cr);

    /* Extra azimuth ticks every 30 degrees, skipping the cardinal cross. */
    if (polv->extratick && polv->r > 0)
    {
        for (i = 1; i < 12; i++)
        {
            gfloat x0, y0, x1, y1;
            gdouble az = i * 30.0;

            if (i % 3 == 0)
                continue;

            azel_to_xy(polv, az, 0.0, &x0, &y0);
            x1 = polv->cx + (x0 - polv->cx) * (1.0f - 8.0f / polv->r);
            y1 = polv->cy + (y0 - polv->cy) * (1.0f - 8.0f / polv->r);
            cairo_move_to(cr, x1, y1);
            cairo_line_to(cr, x0, y0);
            cairo_stroke(cr);
        }
    }

    /* N/S/E/W labels */
    rgba_to_cairo(polv->col_tick, &r, &g, &b, &a);
    cairo_set_source_rgba(cr, r, g, b, a);

    draw_compass_label(cr, layout, polv, 0.0, _("N"));
    draw_compass_label(cr, layout, polv, 90.0, _("E"));
    draw_compass_label(cr, layout, polv, 180.0, _("S"));
    draw_compass_label(cr, layout, polv, 270.0, _("W"));

    if (polv->extratick)
    {
        draw_el_label(cr, layout, polv, 30.0, "30\302\260");
        draw_el_label(cr, layout, polv, 60.0, "60\302\260");
    }

    /* Location name (if enabled) */
    if (polv->qthinfo && polv->qth)
    {
        rgba_to_cairo(polv->col_info, &r, &g, &b, &a);
        cairo_set_source_rgba(cr, r, g, b, a);
        pango_layout_set_text(layout, polv->qth->name, -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, polv->cx - polv->r - 2 * POLV_LINE_EXTRA,
                      polv->cy - polv->r - POLV_LINE_EXTRA - th);
        pango_cairo_show_layout(cr, layout);
    }

    /* Cursor tracking text */
    if (polv->cursinfo && polv->curs_text)
    {
        rgba_to_cairo(polv->col_info, &r, &g, &b, &a);
        cairo_set_source_rgba(cr, r, g, b, a);
        pango_layout_set_text(layout, polv->curs_text, -1);
        cairo_move_to(cr, polv->cx - polv->r - 2 * POLV_LINE_EXTRA,
                      polv->cy + polv->r + POLV_LINE_EXTRA);
        pango_cairo_show_layout(cr, layout);
    }

    /* Next event text */
    if (polv->eventinfo && polv->next_text)
    {
        rgba_to_cairo(polv->col_info, &r, &g, &b, &a);
        cairo_set_source_rgba(cr, r, g, b, a);
        pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT);
        pango_layout_set_text(layout, polv->next_text, -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, polv->cx + polv->r + 2 * POLV_LINE_EXTRA - tw,
                      polv->cy - polv->r - POLV_LINE_EXTRA - th/2);
        pango_cairo_show_layout(cr, layout);
        pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    }

    /* Selected satellite text */
    if (polv->sel_text)
    {
        rgba_to_cairo(polv->col_info, &r, &g, &b, &a);
        cairo_set_source_rgba(cr, r, g, b, a);
        pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT);
        pango_layout_set_text(layout, polv->sel_text, -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, 2.0 * polv->cx - tw - 4.0,
                      2.0 * polv->cy - th - 4.0);
        pango_cairo_show_layout(cr, layout);
        pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    }

    /* Draw satellite objects */
    if (polv->obj)
    {
        g_hash_table_iter_init(&iter, polv->obj);
        while (g_hash_table_iter_next(&iter, &key, &value))
        {
            obj = SAT_OBJ(value);

            /* Draw track if enabled */
            if (obj->showtrack && obj->track_points)
            {
                rgba_to_cairo(polv->col_track, &r, &g, &b, &a);
                cairo_set_source_rgba(cr, r, g, b, a);
                cairo_set_line_width(cr, 1.0);

                node = obj->track_points;
                if (node)
                {
                    point = (gdouble *)node->data;
                    cairo_move_to(cr, point[0], point[1]);
                    node = node->next;

                    while (node)
                    {
                        point = (gdouble *)node->data;
                        cairo_line_to(cr, point[0], point[1]);
                        node = node->next;
                    }
                    cairo_stroke(cr);
                }

                /* Draw time ticks */
                for (i = 0; i < TRACK_TICK_NUM; i++)
                {
                    if (obj->trtick[i].text[0] != '\0')
                    {
                        x = obj->trtick[i].x;
                        y = obj->trtick[i].y;
                        pango_layout_set_text(layout, obj->trtick[i].text, -1);
                        pango_layout_get_pixel_size(layout, &tw, &th);

                        if (x > polv->cx)
                            cairo_move_to(cr, x - tw - 5, y - th / 2);
                        else
                            cairo_move_to(cr, x + 5, y - th / 2);

                        pango_cairo_show_layout(cr, layout);
                    }
                }
            }

            /* Motion arrow sits under the marker so the position stays visible. */
            if (polv->showmotion && obj->has_arrow)
            {
                if (obj->selected)
                    rgba_to_cairo(polv->col_sat_sel, &r, &g, &b, &a);
                else
                    rgba_to_cairo(polv->col_sat, &r, &g, &b, &a);

                cairo_set_source_rgba(cr, r, g, b, a);
                draw_motion_arrow(cr, obj->x, obj->y, obj->arrow_x, obj->arrow_y);
            }

            /* Draw satellite marker */
            if (polv->satmarker)
            {
                if (obj->selected)
                    rgba_to_cairo(polv->col_sat_sel, &r, &g, &b, &a);
                else
                    rgba_to_cairo(polv->col_sat, &r, &g, &b, &a);

                cairo_set_source_rgba(cr, r, g, b, a);
                cairo_rectangle(cr, obj->x - MARKER_SIZE_HALF,
                                obj->y - MARKER_SIZE_HALF, 2 * MARKER_SIZE_HALF,
                                2 * MARKER_SIZE_HALF);
                cairo_fill(cr);
            }

            /* Draw satellite name */
            if (polv->satname && obj->nickname)
            {
                if (obj->selected)
                    rgba_to_cairo(polv->col_sat_sel, &r, &g, &b, &a);
                else
                    rgba_to_cairo(polv->col_sat, &r, &g, &b, &a);

                cairo_set_source_rgba(cr, r, g, b, a);
                pango_layout_set_text(layout, obj->nickname, -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                cairo_move_to(cr, obj->x - tw / 2, obj->y + 2);
                pango_cairo_show_layout(cr, layout);
            }
        }
    }

    pango_font_description_free(font_desc);
    g_object_unref(layout);

    return FALSE;
}

#define MOTION_AHEAD_SEC 20.0
#define MOTION_MIN_SPAN_DEG 0.05

/* Angular distance on the sky between two azimuth/elevation pairs. */
static gdouble sky_angle_deg(gdouble az1, gdouble el1, gdouble az2, gdouble el2)
{
    gdouble a1 = az1 * de2ra;
    gdouble e1 = el1 * de2ra;
    gdouble a2 = az2 * de2ra;
    gdouble e2 = el2 * de2ra;
    gdouble x1 = cos(e1) * sin(a1);
    gdouble y1 = cos(e1) * cos(a1);
    gdouble z1 = sin(e1);
    gdouble x2 = cos(e2) * sin(a2);
    gdouble y2 = cos(e2) * cos(a2);
    gdouble z2 = sin(e2);
    gdouble dot = x1 * x2 + y1 * y2 + z1 * z2;

    if (dot > 1.0)
        dot = 1.0;
    if (dot < -1.0)
        dot = -1.0;

    return acos(dot) / de2ra;
}

/* Place the arrow tip from the stored look-ahead azimuth and elevation. */
static void place_motion_arrow(GtkPolarView *polv, sat_obj_t *obj)
{
    gfloat x1, y1;
    gdouble dx, dy, dist, length, fel;

    obj->has_arrow = FALSE;

    if (!polv->showmotion || !obj->has_ahead)
        return;
    if (obj->sky_rate * MOTION_AHEAD_SEC < MOTION_MIN_SPAN_DEG)
        return;

    fel = obj->ahead_el;
    if (fel < 0.0)
        fel = 0.0;

    azel_to_xy(polv, obj->ahead_az, fel, &x1, &y1);
    dx = x1 - obj->x;
    dy = y1 - obj->y;
    dist = hypot(dx, dy);
    if (dist < 0.5)
        return;

    /* 0.05 deg over the look-ahead is 12 px; about 0.2 deg/s reaches 36 px. */
    length = 12.0 + (obj->sky_rate * MOTION_AHEAD_SEC - MOTION_MIN_SPAN_DEG) *
                        (24.0 / 4.0);
    if (length < 12.0)
        length = 12.0;
    if (length > 36.0)
        length = 36.0;

    obj->arrow_x = obj->x + (gfloat)(dx / dist * length);
    obj->arrow_y = obj->y + (gfloat)(dy / dist * length);
    obj->has_arrow = TRUE;
}

/* Predict a point 20 s ahead so the arrow follows the pass, not the chart. */
static void update_motion_arrow(GtkPolarView *polv, sat_obj_t *obj, sat_t *sat)
{
    sat_t *future;
    gdouble span;
    gdouble dt = MOTION_AHEAD_SEC / 86400.0;

    obj->has_ahead = FALSE;
    obj->has_arrow = FALSE;
    obj->sky_rate = 0.0;

    if (sat->el < 0.0 || polv->qth == NULL)
        return;

    /*
     * Clone the live satellite, including its converted elements and the
     * SGP4/SDP4 state. gtk_sat_data_copy_sat() runs select_ephemeris(), which
     * converts those elements again and would point the arrow the wrong way.
     * The clone is on the heap because gtk_sat_data_free_sat() releases it.
     */
    future = g_new(sat_t, 1);
    *future = *sat;
    future->name = g_strdup(sat->name);
    future->nickname = g_strdup(sat->nickname);
    future->website = g_strdup(sat->website);
    predict_calc(future, polv->qth, polv->tstamp + dt);

    span = sky_angle_deg(sat->az, sat->el, future->az, future->el);
    obj->sky_rate = span / MOTION_AHEAD_SEC;

    if (span >= MOTION_MIN_SPAN_DEG)
    {
        obj->has_ahead = TRUE;
        obj->ahead_az = future->az;
        obj->ahead_el = future->el;
        place_motion_arrow(polv, obj);
    }

    gtk_sat_data_free_sat(future);
}

static void store_az_offset(GtkPolarView *polv)
{
    if (polv->cfgdata == NULL)
        return;

    g_key_file_set_integer(polv->cfgdata, MOD_CFG_POLAR_SECTION,
                           MOD_CFG_POLAR_AZ_OFFSET, polv->az_offset);
}

static gint load_az_offset(GKeyFile *cfg)
{
    GError *error = NULL;
    gint value = 0;

    if (cfg != NULL &&
        g_key_file_has_key(cfg, MOD_CFG_POLAR_SECTION, MOD_CFG_POLAR_AZ_OFFSET,
                           &error))
    {
        value = g_key_file_get_integer(cfg, MOD_CFG_POLAR_SECTION,
                                       MOD_CFG_POLAR_AZ_OFFSET, NULL);
    }
    g_clear_error(&error);

    return norm_az_offset(value);
}

/* Clockwise screen rotation. Positive phi matches a positive azimuth offset. */
static void rotate_xy(GtkPolarView *polv, gdouble *x, gdouble *y, gdouble phi)
{
    gdouble dx = *x - polv->cx;
    gdouble dy = *y - polv->cy;
    gdouble c = cos(phi);
    gdouble s = sin(phi);

    *x = polv->cx + c * dx - s * dy;
    *y = polv->cy + s * dx + c * dy;
}

static void rotate_tracks(GtkPolarView *polv, gdouble phi)
{
    GHashTableIter iter;
    gpointer key, value;

    if (polv->obj == NULL || phi == 0.0)
        return;

    g_hash_table_iter_init(&iter, polv->obj);
    while (g_hash_table_iter_next(&iter, &key, &value))
    {
        sat_obj_t *obj = SAT_OBJ(value);
        GSList *node;
        guint i;

        for (node = obj->track_points; node != NULL; node = node->next)
        {
            gdouble *point = (gdouble *)node->data;

            rotate_xy(polv, &point[0], &point[1], phi);
        }

        for (i = 0; i < TRACK_TICK_NUM; i++)
        {
            gdouble x, y;

            if (obj->trtick[i].text[0] == '\0')
                continue;

            x = obj->trtick[i].x;
            y = obj->trtick[i].y;
            rotate_xy(polv, &x, &y, phi);
            obj->trtick[i].x = (gfloat)x;
            obj->trtick[i].y = (gfloat)y;
        }
    }
}

static void reposition_marker(gpointer key, gpointer value, gpointer data)
{
    sat_t *sat = SAT(value);
    GtkPolarView *polv = GTK_POLAR_VIEW(data);
    gint catnum = sat->tle.catnr;
    sat_obj_t *obj;
    gfloat x, y;

    (void)key;

    if (sat->el < 0.0 || polv->obj == NULL)
        return;

    obj = SAT_OBJ(g_hash_table_lookup(polv->obj, &catnum));
    if (obj == NULL)
        return;

    azel_to_xy(polv, sat->az, sat->el, &x, &y);
    obj->x = x;
    obj->y = y;
    place_motion_arrow(polv, obj);
}

static void reposition_sat(gpointer key, gpointer value, gpointer data)
{
    sat_t *sat = SAT(value);
    GtkPolarView *polv = GTK_POLAR_VIEW(data);
    gint catnum = sat->tle.catnr;
    sat_obj_t *obj;

    reposition_marker(key, value, data);

    if (polv->obj == NULL)
        return;

    obj = SAT_OBJ(g_hash_table_lookup(polv->obj, &catnum));
    if (obj != NULL && obj->showtrack && obj->pass != NULL)
        gtk_polar_view_create_track(polv, obj, sat);
}

static void apply_offset_delta(GtkPolarView *polv, gint delta_tenths,
                               gboolean rebuild)
{
    gdouble phi;

    if (delta_tenths == 0 || polv->sats == NULL)
        return;

    phi = (delta_tenths / 10.0) * (G_PI / 180.0);
    polv->az_offset = norm_az_offset(polv->az_offset + delta_tenths);

    if (rebuild)
        g_hash_table_foreach(polv->sats, reposition_sat, polv);
    else
    {
        rotate_tracks(polv, phi);
        g_hash_table_foreach(polv->sats, reposition_marker, polv);
    }

    gtk_widget_queue_draw(polv->canvas);
}

static gdouble pointer_angle(GtkPolarView *polv, gdouble x, gdouble y)
{
    return atan2(x - polv->cx, (gdouble)polv->cy - y);
}

static void reset_rotation_cb(GtkMenuItem *item, gpointer data)
{
    GtkPolarView *polv = GTK_POLAR_VIEW(data);

    (void)item;

    polv->az_offset = 0;
    store_az_offset(polv);
    if (polv->sats != NULL)
        g_hash_table_foreach(polv->sats, reposition_sat, polv);
    gtk_widget_queue_draw(polv->canvas);
}

static void popup_rotation_menu(GtkPolarView *polv, GdkEventButton *event)
{
    GtkWidget *menu;
    GtkWidget *item;

    menu = gtk_menu_new();
    item = gtk_menu_item_new_with_label(_("Reset rotation"));
    g_signal_connect(item, "activate", G_CALLBACK(reset_rotation_cb), polv);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show_all(menu);

#if GTK_MINOR_VERSION < 22
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                   (event != NULL) ? event->button : 0,
                   gdk_event_get_time((GdkEvent *)event));
#else
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
#endif
}

static sat_obj_t *find_sat_at_pos(GtkPolarView *polv, gfloat mx, gfloat my)
{
    GHashTableIter iter;
    gpointer key, value;
    sat_obj_t *obj;
    gfloat dx, dy;
    const gfloat hit_radius = 10.0;

    if (polv->obj == NULL)
        return NULL;

    g_hash_table_iter_init(&iter, polv->obj);
    while (g_hash_table_iter_next(&iter, &key, &value))
    {
        obj = SAT_OBJ(value);
        dx = mx - obj->x;
        dy = my - obj->y;
        if (dx * dx + dy * dy < hit_radius * hit_radius)
            return obj;
    }
    return NULL;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event,
                                gpointer data)
{
    GtkPolarView *polv = GTK_POLAR_VIEW(data);
    sat_obj_t *obj;
    sat_t *sat = NULL;
    gint *catpoint = NULL;

    (void)widget;

    /* Ctrl+drag rotates the chart without selecting a satellite. */
    if (event->button == 1 && (event->state & GDK_CONTROL_MASK) &&
        event->type == GDK_BUTTON_PRESS)
    {
        polv->rotating = TRUE;
        polv->rotate_angle = pointer_angle(polv, event->x, event->y);
        return TRUE;
    }

    obj = find_sat_at_pos(polv, event->x, event->y);

    if (obj == NULL)
    {
        if (event->button == 3 && event->type == GDK_BUTTON_PRESS)
        {
            popup_rotation_menu(polv, event);
            return TRUE;
        }

        return FALSE;
    }

    switch (event->button)
    {
    case 1:
        if (event->type == GDK_2BUTTON_PRESS)
        {
            /* Double-click: show satellite info */
            catpoint = g_try_new0(gint, 1);
            *catpoint = obj->catnum;
            sat = SAT(g_hash_table_lookup(polv->sats, catpoint));
            if (sat != NULL)
            {
                show_sat_info(sat, gtk_widget_get_toplevel(GTK_WIDGET(polv)));
            }
            g_free(catpoint);
        }
        break;

    case 3:
        /* Right-click: popup menu */
        catpoint = g_try_new0(gint, 1);
        *catpoint = obj->catnum;
        sat = SAT(g_hash_table_lookup(polv->sats, catpoint));
        if (sat != NULL)
        {
            gtk_polar_view_popup_exec(
                sat, polv->qth, polv, event,
                gtk_widget_get_toplevel(GTK_WIDGET(polv)));
        }
        g_free(catpoint);
        break;

    default:
        break;
    }

    return TRUE;
}

static void clear_selection(gpointer key, gpointer val, gpointer data)
{
    gint *old = key;
    gint *new = data;
    sat_obj_t *obj = SAT_OBJ(val);

    if ((*old != *new) && (obj->selected))
    {
        obj->selected = FALSE;
    }
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event,
                                  gpointer data)
{
    GtkPolarView *polv = GTK_POLAR_VIEW(data);
    sat_obj_t *obj;
    gint *catpoint;

    (void)widget;

    if (event->button == 1 && polv->rotating)
    {
        polv->rotating = FALSE;
        if (polv->sats != NULL)
            g_hash_table_foreach(polv->sats, reposition_sat, polv);
        store_az_offset(polv);
        gtk_widget_queue_draw(polv->canvas);
        return TRUE;
    }

    if (event->button != 1)
        return FALSE;

    obj = find_sat_at_pos(polv, event->x, event->y);

    if (obj == NULL)
        return FALSE;

    obj->selected = !obj->selected;

    catpoint = g_try_new0(gint, 1);
    *catpoint = obj->catnum;

    if (!obj->selected)
    {
        g_free(polv->sel_text);
        polv->sel_text = NULL;
        *catpoint = 0;
    }

    /* clear other selections */
    g_hash_table_foreach(polv->obj, clear_selection, catpoint);

    g_free(catpoint);

    gtk_widget_queue_draw(polv->canvas);

    return TRUE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event,
                                 gpointer data)
{
    GtkPolarView *polv = GTK_POLAR_VIEW(data);
    gfloat az, el;

    (void)widget;

    if (polv->rotating && (event->state & GDK_BUTTON1_MASK))
    {
        gdouble angle = pointer_angle(polv, event->x, event->y);
        gdouble delta = angle - polv->rotate_angle;
        gint tenths;

        while (delta > G_PI)
            delta -= 2.0 * G_PI;
        while (delta < -G_PI)
            delta += 2.0 * G_PI;

        tenths = (gint)lround(delta * (180.0 / G_PI) * 10.0);
        if (tenths != 0)
        {
            apply_offset_delta(polv, tenths, FALSE);
            polv->rotate_angle = angle;
        }
    }

    if (polv->cursinfo)
    {
        xy_to_azel(polv, event->x, event->y, &az, &el);

        if (el > 0.0)
        {
            g_free(polv->curs_text);
            polv->curs_text =
                g_strdup_printf("AZ: %.0f\302\260   EL: %.0f\302\260", az, el);
        }
        else
        {
            g_free(polv->curs_text);
            polv->curs_text = NULL;
        }

        gtk_widget_queue_draw(polv->canvas);
    }

    return TRUE;
}

/* The wheel turns the chart in 5 degree steps. */
static gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event,
                          gpointer data)
{
    GtkPolarView *polv = GTK_POLAR_VIEW(data);
    gint tenths = 0;

    (void)widget;

    if (event->direction == GDK_SCROLL_UP || event->direction == GDK_SCROLL_LEFT)
        tenths = -50;
    else if (event->direction == GDK_SCROLL_DOWN ||
             event->direction == GDK_SCROLL_RIGHT)
        tenths = 50;
    else if (event->direction == GDK_SCROLL_SMOOTH)
    {
        polv->scroll_acc += event->delta_y;
        while (polv->scroll_acc >= 1.0)
        {
            tenths += 50;
            polv->scroll_acc -= 1.0;
        }
        while (polv->scroll_acc <= -1.0)
        {
            tenths -= 50;
            polv->scroll_acc += 1.0;
        }
    }

    if (tenths != 0)
    {
        apply_offset_delta(polv, tenths, TRUE);
        store_az_offset(polv);
    }

    return TRUE;
}

static gboolean on_query_tooltip(GtkWidget *widget, gint x, gint y,
                                 gboolean keyboard, GtkTooltip *tooltip,
                                 gpointer data)
{
    GtkPolarView *polv = GTK_POLAR_VIEW(data);
    sat_obj_t *obj;

    (void)widget;
    (void)keyboard;

    obj = find_sat_at_pos(polv, (gfloat)x, (gfloat)y);
    if (obj == NULL || obj->tooltip == NULL)
        return FALSE;

    gtk_tooltip_set_markup(tooltip, obj->tooltip);
    return TRUE;
}

static void size_allocate_cb(GtkWidget *widget, GtkAllocation *allocation,
                             gpointer data)
{
    (void)widget;
    (void)allocation;
    GTK_POLAR_VIEW(data)->resize = TRUE;
}

static void on_canvas_realized(GtkWidget *canvas, gpointer data)
{
    GtkAllocation aloc;

    gtk_widget_get_allocation(canvas, &aloc);
    size_allocate_cb(canvas, &aloc, data);
}

GtkWidget *gtk_polar_view_new(GKeyFile *cfgdata, GHashTable *sats, qth_t *qth)
{
    GtkPolarView *polv;
    GValue font_value = G_VALUE_INIT;

    polv = GTK_POLAR_VIEW(g_object_new(GTK_TYPE_POLAR_VIEW, NULL));

    polv->cfgdata = cfgdata;
    polv->sats = sats;
    polv->qth = qth;

    polv->obj =
        g_hash_table_new_full(g_int_hash, g_int_equal, g_free, free_sat_obj);
    polv->showtracks_on =
        g_hash_table_new_full(g_int_hash, g_int_equal, g_free, NULL);
    polv->showtracks_off =
        g_hash_table_new_full(g_int_hash, g_int_equal, g_free, NULL);

    /* get settings */
    polv->refresh =
        mod_cfg_get_int(cfgdata, MOD_CFG_POLAR_SECTION, MOD_CFG_POLAR_REFRESH,
                        SAT_CFG_INT_POLAR_REFRESH);

    polv->showtrack = mod_cfg_get_bool(cfgdata, MOD_CFG_POLAR_SECTION,
                                       MOD_CFG_POLAR_SHOW_TRACK_AUTO,
                                       SAT_CFG_BOOL_POL_SHOW_TRACK_AUTO);

    polv->counter = 1;

    polv->swap = mod_cfg_get_int(cfgdata, MOD_CFG_POLAR_SECTION,
                                 MOD_CFG_POLAR_ORIENTATION,
                                 SAT_CFG_INT_POLAR_ORIENTATION);
    polv->az_offset = load_az_offset(cfgdata);

    polv->satname = mod_cfg_get_bool(cfgdata, MOD_CFG_POLAR_SECTION,
                                     MOD_CFG_POLAR_SHOW_SAT_NAME,
                                     SAT_CFG_BOOL_POL_SHOW_SAT_NAME);
    polv->satmarker = mod_cfg_get_bool(cfgdata, MOD_CFG_POLAR_SECTION,
                                       MOD_CFG_POLAR_SHOW_SAT_MARKER,
                                       SAT_CFG_BOOL_POL_SHOW_SAT_MARKER);

    polv->qthinfo = mod_cfg_get_bool(cfgdata, MOD_CFG_POLAR_SECTION,
                                     MOD_CFG_POLAR_SHOW_QTH_INFO,
                                     SAT_CFG_BOOL_POL_SHOW_QTH_INFO);

    polv->eventinfo = mod_cfg_get_bool(cfgdata, MOD_CFG_POLAR_SECTION,
                                       MOD_CFG_POLAR_SHOW_NEXT_EVENT,
                                       SAT_CFG_BOOL_POL_SHOW_NEXT_EV);

    polv->cursinfo = mod_cfg_get_bool(cfgdata, MOD_CFG_POLAR_SECTION,
                                      MOD_CFG_POLAR_SHOW_CURS_TRACK,
                                      SAT_CFG_BOOL_POL_SHOW_CURS_TRACK);

    polv->extratick = mod_cfg_get_bool(cfgdata, MOD_CFG_POLAR_SECTION,
                                       MOD_CFG_POLAR_SHOW_EXTRA_AZ_TICKS,
                                       SAT_CFG_BOOL_POL_SHOW_EXTRA_AZ_TICKS);

    polv->showmotion = mod_cfg_get_bool(cfgdata, MOD_CFG_POLAR_SECTION,
                                        MOD_CFG_POLAR_SHOW_MOTION,
                                        SAT_CFG_BOOL_POL_SHOW_MOTION);

    gtk_polar_view_load_showtracks(polv);

    /* get colors */
    polv->col_bgd =
        mod_cfg_get_int(polv->cfgdata, MOD_CFG_POLAR_SECTION,
                        MOD_CFG_POLAR_BGD_COL, SAT_CFG_INT_POLAR_BGD_COL);
    polv->col_axis =
        mod_cfg_get_int(polv->cfgdata, MOD_CFG_POLAR_SECTION,
                        MOD_CFG_POLAR_AXIS_COL, SAT_CFG_INT_POLAR_AXIS_COL);
    polv->col_tick =
        mod_cfg_get_int(polv->cfgdata, MOD_CFG_POLAR_SECTION,
                        MOD_CFG_POLAR_TICK_COL, SAT_CFG_INT_POLAR_TICK_COL);
    polv->col_info =
        mod_cfg_get_int(polv->cfgdata, MOD_CFG_POLAR_SECTION,
                        MOD_CFG_POLAR_INFO_COL, SAT_CFG_INT_POLAR_INFO_COL);
    polv->col_sat =
        mod_cfg_get_int(polv->cfgdata, MOD_CFG_POLAR_SECTION,
                        MOD_CFG_POLAR_SAT_COL, SAT_CFG_INT_POLAR_SAT_COL);
    polv->col_sat_sel = mod_cfg_get_int(polv->cfgdata, MOD_CFG_POLAR_SECTION,
                                        MOD_CFG_POLAR_SAT_SEL_COL,
                                        SAT_CFG_INT_POLAR_SAT_SEL_COL);
    polv->col_track =
        mod_cfg_get_int(polv->cfgdata, MOD_CFG_POLAR_SECTION,
                        MOD_CFG_POLAR_TRACK_COL, SAT_CFG_INT_POLAR_TRACK_COL);

    /* get default font */
    g_value_init(&font_value, G_TYPE_STRING);
    g_object_get_property(G_OBJECT(gtk_settings_get_default()), "gtk-font-name",
                          &font_value);
    polv->font = g_value_dup_string(&font_value);
    g_value_unset(&font_value);

    /* graph dimensions */
    polv->size = POLV_DEFAULT_SIZE;
    polv->r = (polv->size / 2) - POLV_DEFAULT_MARGIN;
    polv->cx = POLV_DEFAULT_SIZE / 2;
    polv->cy = POLV_DEFAULT_SIZE / 2;

    /* create the canvas (drawing area) */
    polv->canvas = gtk_drawing_area_new();
    gtk_widget_set_has_tooltip(polv->canvas, TRUE);
    gtk_widget_set_size_request(polv->canvas, POLV_DEFAULT_SIZE,
                                POLV_DEFAULT_SIZE);
    gtk_widget_add_events(polv->canvas, GDK_POINTER_MOTION_MASK |
                                            GDK_BUTTON_PRESS_MASK |
                                            GDK_BUTTON_RELEASE_MASK |
                                            GDK_BUTTON1_MOTION_MASK |
                                            GDK_SCROLL_MASK |
                                            GDK_SMOOTH_SCROLL_MASK);

    /* connect signals */
    g_signal_connect(polv->canvas, "draw", G_CALLBACK(on_draw), polv);
    g_signal_connect(polv->canvas, "motion-notify-event",
                     G_CALLBACK(on_motion_notify), polv);
    g_signal_connect(polv->canvas, "button-press-event",
                     G_CALLBACK(on_button_press), polv);
    g_signal_connect(polv->canvas, "button-release-event",
                     G_CALLBACK(on_button_release), polv);
    g_signal_connect(polv->canvas, "scroll-event", G_CALLBACK(on_scroll), polv);
    g_signal_connect(polv->canvas, "query-tooltip",
                     G_CALLBACK(on_query_tooltip), polv);
    g_signal_connect(polv->canvas, "size-allocate",
                     G_CALLBACK(size_allocate_cb), polv);
    g_signal_connect_after(polv->canvas, "realize",
                           G_CALLBACK(on_canvas_realized), polv);

    gtk_widget_show(polv->canvas);
    gtk_box_pack_start(GTK_BOX(polv), polv->canvas, TRUE, TRUE, 0);

    return GTK_WIDGET(polv);
}

/* Recreate sky-track pixel coordinates after geometry changes (e.g. resize). */
static void update_track(gpointer key, gpointer value, gpointer data)
{
    sat_obj_t *obj = SAT_OBJ(value);
    GtkPolarView *pv = GTK_POLAR_VIEW(data);

    (void)key;

    if (obj != NULL && obj->showtrack && obj->pass != NULL)
        gtk_polar_view_create_track(pv, obj, NULL);
}

static void update_polv_size(GtkPolarView *polv)
{
    GtkAllocation allocation;

    if (gtk_widget_get_realized(GTK_WIDGET(polv)))
    {
        gtk_widget_get_allocation(GTK_WIDGET(polv), &allocation);

        polv->size = MIN(allocation.width, allocation.height);
        polv->r = (polv->size / 2) - POLV_DEFAULT_MARGIN;
        polv->cx = allocation.width / 2;
        polv->cy = allocation.height / 2;

        /* Update satellite marker positions for the new scale */
        g_hash_table_foreach(polv->sats, update_sat, polv);

        /* Sky tracks are stored as absolute canvas pixels; rebuild them
         * from Az/El so they stay aligned with the resized chart. */
        if (polv->obj != NULL)
            g_hash_table_foreach(polv->obj, update_track, polv);

        gtk_widget_queue_draw(polv->canvas);
    }
}

/* Convert LOS timestamp to human readable countdown string */
static gchar *los_time_to_str(GtkPolarView *polv, sat_t *sat)
{
    guint h, m, s;
    gdouble number, now;
    gchar *text = NULL;

    now = polv->tstamp;
    number = sat->los - now;

    /* convert julian date to seconds */
    s = (guint)(number * 86400);

    /* extract hours */
    h = (guint)floor(s / 3600);
    s -= 3600 * h;

    /* extract minutes */
    m = (guint)floor(s / 60);
    s -= 60 * m;

    if (h > 0)
        text = g_strdup_printf(_("LOS in %02d:%02d:%02d"), h, m, s);
    else
        text = g_strdup_printf(_("LOS in %02d:%02d"), m, s);

    return text;
}

void gtk_polar_view_update(GtkWidget *widget)
{
    GtkPolarView *polv = GTK_POLAR_VIEW(widget);
    gdouble number, now;
    gchar *buff;
    guint h, m, s;
    sat_t *sat = NULL;
    gint *catnr;

    if (polv->resize)
    {
        update_polv_size(polv);
        polv->resize = FALSE;
    }

    /* check refresh rate and refresh sats if time */
    /* FIXME need to add location based update */
    if (polv->counter < polv->refresh)
    {
        polv->counter++;
    }
    else
    {
        /* reset data */
        polv->counter = 1;
        polv->naos = 0.0;
        polv->ncat = 0;

        /* update sats */
        g_hash_table_foreach(polv->sats, update_sat, polv);

        /* update countdown to NEXT AOS label */
        if (polv->eventinfo)
        {
            if (polv->ncat > 0)
            {
                catnr = g_try_new0(gint, 1);
                *catnr = polv->ncat;
                sat = SAT(g_hash_table_lookup(polv->sats, catnr));
                g_free(catnr);

                if (sat != NULL)
                {
                    now = polv->tstamp;
                    number = polv->naos - now;

                    s = (guint)(number * 86400);
                    h = (guint)floor(s / 3600);
                    s -= 3600 * h;
                    m = (guint)floor(s / 60);
                    s -= 60 * m;

                    if (h > 0)
                        buff = g_strdup_printf(_("Next: %s\nin %02d:%02d:%02d"),
                                               sat->nickname, h, m, s);
                    else
                        buff = g_strdup_printf(_("Next: %s\nin %02d:%02d"),
                                               sat->nickname, m, s);

                    g_free(polv->next_text);
                    polv->next_text = buff;
                }
                else
                {
                    sat_log_log(SAT_LOG_LEVEL_ERROR,
                                _("%s: Can not find NEXT satellite."),
                                __func__);
                    g_free(polv->next_text);
                    polv->next_text = g_strdup(_("Next: ERR"));
                }
            }
            else
            {
                g_free(polv->next_text);
                polv->next_text = g_strdup(_("Next: N/A"));
            }
        }
        else
        {
            g_free(polv->next_text);
            polv->next_text = NULL;
        }

        gtk_widget_queue_draw(polv->canvas);
    }
}

static void refresh_sat_readout(GtkPolarView *polv, sat_obj_t *obj, sat_t *sat)
{
    gchar *losstr;
    gchar *plain;

    if (sat->los > 0.0)
        losstr = los_time_to_str(polv, sat);
    else
        losstr = g_strdup(_("Always in range"));

    plain = g_strdup_printf("%s\nAz %5.1f\302\260  El %4.1f\302\260  "
                            "Sky %.2f\302\260/s\nRng %.0f km  RR %+.2f km/s\n%s",
                            sat->nickname, sat->az, sat->el, obj->sky_rate,
                            sat->range, sat->range_rate, losstr);

    g_free(obj->tooltip);
    obj->tooltip = g_markup_printf_escaped(
        "<b>%s</b>\nAz: %5.1f\302\260\nEl: %5.1f\302\260\n"
        "Range: %.0f km\nRate: %+.2f km/s\n%s\nSky: %.2f\302\260/s",
        sat->nickname, sat->az, sat->el, sat->range, sat->range_rate, losstr,
        obj->sky_rate);

    if (obj->selected)
    {
        g_free(polv->sel_text);
        polv->sel_text = plain;
    }
    else
        g_free(plain);

    g_free(losstr);
}

static void update_sat(gpointer key, gpointer value, gpointer data)
{
    gint *catnum;
    sat_t *sat = SAT(value);
    GtkPolarView *polv = GTK_POLAR_VIEW(data);
    sat_obj_t *obj = NULL;
    gfloat x, y;
    gdouble now;

    (void)key;

    catnum = g_new0(gint, 1);
    *catnum = sat->tle.catnr;

    now = polv->tstamp;

    /* update next AOS */
    if (sat->aos > now)
    {
        if ((sat->aos < polv->naos) || (polv->naos == 0.0))
        {
            polv->naos = sat->aos;
            polv->ncat = sat->tle.catnr;
        }
    }

    /* if sat is out of range */
    if ((sat->el < 0.00) || decayed(sat))
    {
        obj = SAT_OBJ(g_hash_table_lookup(polv->obj, catnum));

        if (obj != NULL)
        {
            /* if this was the selected satellite we need to
               clear the info text
             */
            if (obj->selected)
            {
                g_free(polv->sel_text);
                polv->sel_text = NULL;
            }

            /* remove sat object from hash table (this will free it) */
            g_hash_table_remove(polv->obj, catnum);
        }

        g_free(catnum);
    }
    else
    {
        /* sat is within range */
        obj = SAT_OBJ(g_hash_table_lookup(polv->obj, catnum));
        azel_to_xy(polv, sat->az, sat->el, &x, &y);

        if (obj != NULL)
        {
            /* update existing satellite */
            obj->x = x;
            obj->y = y;

            /* update nickname */
            g_free(obj->nickname);
            obj->nickname = g_strdup(sat->nickname);

            update_motion_arrow(polv, obj, sat);
            refresh_sat_readout(polv, obj, sat);

            /* Check if pass needs update */
            if (obj->pass)
            {
                /** FIXME: threshold */
                gboolean qth_upd =
                    qth_small_dist(polv->qth, (obj->pass->qth_comp)) > 1.0;
                gboolean time_upd =
                    !((obj->pass->aos <= now) && (obj->pass->los >= now));

                if (qth_upd || time_upd)
                {
                    sat_log_log(
                        SAT_LOG_LEVEL_DEBUG,
                        _("%s:%s: Updating satellite pass SAT:%d Q:%d T:%d\n"),
                        __FILE__, __func__, *catnum, qth_upd, time_upd);

                    /* Free old track and pass */
                    g_slist_free_full(obj->track_points, g_free);
                    obj->track_points = NULL;
                    free_pass(obj->pass);
                    obj->pass = NULL;

                    /* Compute new pass */
                    obj->pass = get_current_pass(sat, polv->qth, now);

                    /* Recreate track if needed */
                    if (obj->showtrack && obj->pass)
                        gtk_polar_view_create_track(polv, obj, sat);
                }
            }

            g_free(catnum); // FIXME: why free here, what about else?
        }
        else
        {
            /* add new satellite */
            obj = g_try_new0(sat_obj_t, 1);

            if (obj != NULL)
            {
                obj->selected = FALSE;
                obj->x = x;
                obj->y = y;
                obj->catnum = sat->tle.catnr;
                obj->nickname = g_strdup(sat->nickname);
                obj->track_points = NULL;

                if (g_hash_table_lookup_extended(polv->showtracks_on, catnum,
                                                 NULL, NULL))
                    obj->showtrack = TRUE;
                else if (g_hash_table_lookup_extended(polv->showtracks_off,
                                                      catnum, NULL, NULL))
                    obj->showtrack = FALSE;
                else
                    obj->showtrack = polv->showtrack;

                obj->istarget = FALSE;

                update_motion_arrow(polv, obj, sat);
                refresh_sat_readout(polv, obj, sat);

                /* get info about the current pass */
                obj->pass = get_current_pass(sat, polv->qth, now);

                /* add sat to hash table */
                g_hash_table_insert(polv->obj, catnum, obj);

                /* create the sky track if necessary */
                if (obj->showtrack)
                    gtk_polar_view_create_track(polv, obj, sat);
            }
            else
            {
                sat_log_log(SAT_LOG_LEVEL_ERROR,
                            _("%s: Cannot allocate memory for satellite %d."),
                            __func__, sat->tle.catnr);
                g_free(catnum);
                return;
            }
        }
    }
}

void gtk_polar_view_create_track(GtkPolarView *pv, sat_obj_t *obj, sat_t *sat)
{
    guint num, i;
    pass_detail_t *detail;
    gfloat x, y;
    gdouble *point;
    guint tres, ttidx;

    (void)sat;

    if (obj == NULL)
    {
        sat_log_log(SAT_LOG_LEVEL_ERROR,
                    _("%s:%d: Failed to get satellite object."), __FILE__,
                    __LINE__);
        return;
    }

    if (obj->pass == NULL)
    {
        sat_log_log(SAT_LOG_LEVEL_ERROR,
                    _("%s:%d: Failed to get satellite pass."), __FILE__,
                    __LINE__);
        return;
    }

    /* Clear existing track points */
    g_slist_free_full(obj->track_points, g_free);
    obj->track_points = NULL;

    /* Create points */
    num = g_slist_length(obj->pass->details);
    if (num == 0)
    {
        sat_log_log(SAT_LOG_LEVEL_ERROR, _("%s:%d: Pass had no points in it."),
                    __FILE__, __LINE__);
        return;
    }

    /* time resolution for time ticks */
    tres = (num > 2) ? (num - 2) / (TRACK_TICK_NUM - 1) : 1;

    /* first point should be (aos_az,0.0) */
    azel_to_xy(pv, obj->pass->aos_az, 0.0, &x, &y);
    point = g_new(gdouble, 2);
    point[0] = x;
    point[1] = y;
    obj->track_points = g_slist_append(obj->track_points, point);

    /* first time tick */
    obj->trtick[0].x = x;
    obj->trtick[0].y = y;
    daynum_to_str(obj->trtick[0].text, 6, "%H:%M", obj->pass->aos);

    ttidx = 1;

    for (i = 1; i < num - 1; i++)
    {
        detail = PASS_DETAIL(g_slist_nth_data(obj->pass->details, i));
        if (detail->el >= 0.0)
            azel_to_xy(pv, detail->az, detail->el, &x, &y);

        point = g_new(gdouble, 2);
        point[0] = x;
        point[1] = y;
        obj->track_points = g_slist_append(obj->track_points, point);

        if (tres != 0 && !(i % tres))
        {
            if (ttidx < TRACK_TICK_NUM)
            {
                gfloat tx = x;
                if (tx > pv->cx)
                    tx -= 5;
                else
                    tx += 5;

                obj->trtick[ttidx].x = tx;
                obj->trtick[ttidx].y = y;
                daynum_to_str(obj->trtick[ttidx].text, 6, "%H:%M",
                              detail->time);
            }
            ttidx++;
        }
    }

    /* last point should be (los_az, 0.0) */
    azel_to_xy(pv, obj->pass->los_az, 0.0, &x, &y);
    point = g_new(gdouble, 2);
    point[0] = x;
    point[1] = y;
    obj->track_points = g_slist_append(obj->track_points, point);
}

void gtk_polar_view_delete_track(GtkPolarView *pv, sat_obj_t *obj, sat_t *sat)
{
    (void)pv;
    (void)sat;

    if (obj)
    {
        g_slist_free_full(obj->track_points, g_free);
        obj->track_points = NULL;

        /* Clear time ticks */
        memset(obj->trtick, 0, sizeof(obj->trtick));
    }
}

void gtk_polar_view_reload_sats(GtkWidget *polv, GHashTable *sats)
{
    GTK_POLAR_VIEW(polv)->sats = sats;
    GTK_POLAR_VIEW(polv)->naos = 0.0;
    GTK_POLAR_VIEW(polv)->ncat = 0;
}

void gtk_polar_view_select_sat(GtkWidget *widget, gint catnum)
{
    GtkPolarView *polv = GTK_POLAR_VIEW(widget);
    gint *catpoint = NULL;
    sat_obj_t *obj = NULL;

    catpoint = g_try_new0(gint, 1);
    *catpoint = catnum;

    obj = SAT_OBJ(g_hash_table_lookup(polv->obj, catpoint));
    if (obj == NULL)
    {
        sat_log_log(SAT_LOG_LEVEL_DEBUG,
                    _("%s Requested satellite (%d) is not within range"),
                    __func__, catnum);
    }
    else
    {
        obj->selected = TRUE;
    }

    /* clear previous selection, if any */
    g_hash_table_foreach(polv->obj, clear_selection, catpoint);

    g_free(catpoint);

    gtk_widget_queue_draw(polv->canvas);
}
