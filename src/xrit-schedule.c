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
 * LRIT/HRIT transmission schedule.
 *
 * A chart in the style of the operator's instrument timetable: one block
 * per satellite, a header row with the 24 hours of the UTC day and one row
 * each for HRIT and LRIT, coloured where the satellite transmits. A red
 * line marks the current time.
 *
 * The built-in plan follows amateur reception reports for the ELEKTRO-L
 * series: HRIT from minute 12 and LRIT from minute 42 of every third hour,
 * on 1691 MHz. No source states how long a transmission lasts, so the
 * 15 minutes used here are an estimate. The operator can change the plan
 * at any time; xrit-schedule.conf in the user configuration directory
 * overrides everything in this file.
 */
#ifdef HAVE_CONFIG_H
#include <build-config.h>
#endif

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"
#include "sat-log.h"
#include "time-tools.h"
#include "xrit-schedule.h"

#define DAY_MIN     1440
#define CONF_FILE   "xrit-schedule.conf"

#define MARGIN      14.0
#define LABEL_W     52.0
#define TITLE_H     46.0
#define NAME_H      22.0
#define ROW_H       20.0
#define NOTE_H      20.0
#define BLOCK_GAP   14.0
#define BLOCK_H     (NAME_H + 4.0 + 3.0 * ROW_H + NOTE_H + BLOCK_GAP)
#define LEGEND_ROW  19.0
#define LEGEND_H    (5.0 * LEGEND_ROW + 6.0)
#define FOOT_H      24.0

static const char default_conf[] =
    "# LRIT/HRIT transmission schedule shown by Gpredict.\n"
    "#\n"
    "# One group per satellite. catnum is the NORAD catalogue number.\n"
    "# Every key starting with hrit or lrit (hrit, hrit2, ...) is one\n"
    "# repeating transmission:  first start HH:MM UTC ; length in minutes ;\n"
    "# repeat interval in minutes (0 = once a day).\n"
    "# cyrillic is the name in Russian letters, shown next to the Latin one.\n"
    "# tentative=true marks a plan that reception has not confirmed.\n"
    "#\n"
    "# The lengths below are estimates. Delete this file to get the\n"
    "# built-in plan back.\n"
    "\n"
    "[ELEKTRO-L 3]\n"
    "catnum=44903\n"
    "cyrillic=\320\255\320\233\320\225\320\232\320\242\320\240\320\236-\320\233 \342\204\2263\n"
    "position=14.5\302\260 W\n"
    "frequency=1691 MHz\n"
    "tentative=false\n"
    "hrit=00:12;15;180\n"
    "lrit=00:42;15;180\n"
    "\n"
    "[ELEKTRO-L 5]\n"
    "catnum=67756\n"
    "cyrillic=\320\255\320\233\320\225\320\232\320\242\320\240\320\236-\320\233 \342\204\2265\n"
    "position=76\302\260 E\n"
    "frequency=1691 MHz\n"
    "tentative=true\n"
    "hrit=00:12;15;180\n"
    "lrit=00:42;15;180\n";

static GPtrArray *plan = NULL;

static GtkWidget *sched_window = NULL;
static GtkWidget *sched_area = NULL;
static guint sched_timer = 0;

static void free_sat(gpointer data)
{
    xrit_sat_t *sat = data;

    g_free(sat->name);
    g_free(sat->cyrillic);
    g_free(sat->position);
    g_free(sat->frequency);
    g_free(sat);
}

/* Russian name of the ELEKTRO-L series for plan files without one. */
static gchar *known_cyrillic(int catnum)
{
    int number = 0;

    switch (catnum) {
    case 41105:
        number = 2;
        break;
    case 44903:
        number = 3;
        break;
    case 55506:
        number = 4;
        break;
    case 67756:
        number = 5;
        break;
    default:
        return NULL;
    }
    return g_strdup_printf("%s%d", "\320\255\320\233\320\225\320\232\320\242\320\240\320\236-\320\233 \342\204\226", number);
}

static gchar *conf_path(void)
{
    gchar *dir = get_user_conf_dir();
    gchar *path = g_build_filename(dir, CONF_FILE, NULL);

    g_free(dir);
    return path;
}

/* "HH:MM;length;period" into a slot. Returns 0 when the text is no use. */
static int parse_slot(GKeyFile *kf, const gchar *group, const gchar *key,
                      xrit_kind_t kind, xrit_slot_t *slot)
{
    gchar **part;
    gsize n = 0;
    int hour = 0;
    int minute = 0;
    int good = 0;

    part = g_key_file_get_string_list(kf, group, key, &n, NULL);
    if (part == NULL)
        return 0;
    if (n >= 2 && sscanf(part[0], "%d:%d", &hour, &minute) == 2) {
        slot->kind = kind;
        slot->first = hour * 60 + minute;
        slot->length = atoi(part[1]);
        slot->period = n >= 3 ? atoi(part[2]) : 0;
        good = hour >= 0 && hour < 24 && minute >= 0 && minute < 60 &&
            slot->length >= 1 && slot->length <= DAY_MIN &&
            (slot->period == 0 || slot->period >= slot->length);
    }
    g_strfreev(part);
    return good;
}

static void load_from(GKeyFile *kf)
{
    gchar **groups;
    gsize group_n = 0;
    gsize g;

    groups = g_key_file_get_groups(kf, &group_n);
    for (g = 0; g < group_n; g++) {
        xrit_sat_t *sat = g_new0(xrit_sat_t, 1);
        gchar **keys;
        gsize key_n = 0;
        gsize k;

        sat->name = g_strdup(groups[g]);
        sat->catnum = g_key_file_get_integer(kf, groups[g], "catnum", NULL);
        sat->cyrillic = g_key_file_get_string(kf, groups[g], "cyrillic", NULL);
        if (sat->cyrillic == NULL)
            sat->cyrillic = known_cyrillic(sat->catnum);
        else if (sat->cyrillic[0] == '\0') {
            g_free(sat->cyrillic);
            sat->cyrillic = NULL;
        }
        sat->position = g_key_file_get_string(kf, groups[g], "position", NULL);
        sat->frequency = g_key_file_get_string(kf, groups[g], "frequency", NULL);
        sat->tentative = g_key_file_get_boolean(kf, groups[g], "tentative", NULL);

        keys = g_key_file_get_keys(kf, groups[g], &key_n, NULL);
        for (k = 0; keys != NULL && k < key_n; k++) {
            xrit_kind_t kind = XRIT_NONE;

            if (g_str_has_prefix(keys[k], "hrit"))
                kind = XRIT_HRIT;
            else if (g_str_has_prefix(keys[k], "lrit"))
                kind = XRIT_LRIT;
            if (kind == XRIT_NONE || sat->slot_n >= XRIT_SLOT_MAX)
                continue;
            if (parse_slot(kf, groups[g], keys[k], kind, &sat->slot[sat->slot_n]))
                sat->slot_n++;
            else
                sat_log_log(SAT_LOG_LEVEL_ERROR,
                            _("%s: Ignoring schedule entry %s of %s"),
                            __func__, keys[k], groups[g]);
        }
        g_strfreev(keys);
        g_ptr_array_add(plan, sat);
    }
    g_strfreev(groups);
}

void xrit_schedule_reload(void)
{
    GKeyFile *kf = g_key_file_new();
    gchar *path = conf_path();
    int loaded = 0;

    if (plan != NULL)
        g_ptr_array_free(plan, TRUE);
    plan = g_ptr_array_new_with_free_func(free_sat);

    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        GError *error = NULL;

        if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &error)) {
            load_from(kf);
            loaded = plan->len > 0;
        } else {
            sat_log_log(SAT_LOG_LEVEL_ERROR, _("%s: Could not read %s (%s)"),
                        __func__, path, error->message);
            g_clear_error(&error);
        }
    } else {
        /* Leave a copy the user can edit. Failing to is harmless. */
        g_file_set_contents(path, default_conf, -1, NULL);
    }

    if (!loaded) {
        g_key_file_free(kf);
        kf = g_key_file_new();
        g_ptr_array_set_size(plan, 0);
        if (g_key_file_load_from_data(kf, default_conf, strlen(default_conf),
                                      G_KEY_FILE_NONE, NULL))
            load_from(kf);
    }

    g_key_file_free(kf);
    g_free(path);
}

static void ensure_plan(void)
{
    if (plan == NULL)
        xrit_schedule_reload();
}

int xrit_schedule_count(void)
{
    ensure_plan();
    return (int)plan->len;
}

const xrit_sat_t *xrit_schedule_get(int index)
{
    ensure_plan();
    if (index < 0 || index >= (int)plan->len)
        return NULL;
    return g_ptr_array_index(plan, index);
}

const xrit_sat_t *xrit_schedule_find(int catnum)
{
    int i;

    ensure_plan();
    for (i = 0; i < (int)plan->len; i++) {
        const xrit_sat_t *sat = g_ptr_array_index(plan, i);

        if (sat->catnum == catnum && sat->slot_n > 0)
            return sat;
    }
    return NULL;
}

double xrit_minute_of_day(double jul_utc)
{
    double frac = jul_utc + 0.5;

    frac -= floor(frac);
    return frac * DAY_MIN;
}

static const char *kind_text(xrit_kind_t kind)
{
    return kind == XRIT_HRIT ? "HRIT" : "LRIT";
}

void xrit_schedule_state(const xrit_sat_t *sat, double minute,
                         xrit_state_t *state)
{
    double best = 1.0e9;
    int i;

    memset(state, 0, sizeof(*state));
    state->next_in = -1;
    if (sat == NULL)
        return;

    for (i = 0; i < sat->slot_n; i++) {
        const xrit_slot_t *slot = &sat->slot[i];
        int day;

        /* Yesterday's last transmission may run past midnight and the
         * next one may be tomorrow's first.
         */
        for (day = -1; day <= 1; day++) {
            int start;

            for (start = slot->first; start < DAY_MIN; start += slot->period) {
                double s = start + day * DAY_MIN;

                if (minute >= s && minute < s + slot->length) {
                    state->now = slot->kind;
                    state->remaining = (int)ceil(s + slot->length - minute);
                } else if (s > minute && s < best) {
                    best = s;
                    state->next = slot->kind;
                    state->next_start = start;
                    state->next_in = (int)ceil(s - minute);
                }
                if (slot->period <= 0)
                    break;
            }
        }
    }
}

gchar *xrit_schedule_status_text(const xrit_sat_t *sat, double jul_utc)
{
    xrit_state_t st;
    gchar *text;

    xrit_schedule_state(sat, xrit_minute_of_day(jul_utc), &st);
    if (st.now != XRIT_NONE)
        text = g_strdup_printf(_("%s on air, %d min left"),
                               kind_text(st.now), st.remaining);
    else if (st.next == XRIT_NONE)
        text = g_strdup(_("No transmission planned"));
    else if (st.next_in >= 60)
        text = g_strdup_printf(_("Next: %s at %02d:%02d UTC, in %d h %02d min"),
                               kind_text(st.next), st.next_start / 60,
                               st.next_start % 60, st.next_in / 60,
                               st.next_in % 60);
    else
        text = g_strdup_printf(_("Next: %s at %02d:%02d UTC, in %d min"),
                               kind_text(st.next), st.next_start / 60,
                               st.next_start % 60, st.next_in);

    if (sat != NULL && sat->tentative) {
        gchar *full = g_strdup_printf("%s (%s)", text, _("unconfirmed plan"));

        g_free(text);
        text = full;
    }
    return text;
}

int xrit_schedule_chart_height(void)
{
    return (int)(TITLE_H + xrit_schedule_count() * BLOCK_H + LEGEND_H +
                 FOOT_H + MARGIN);
}

static void set_kind_colour(cairo_t *cr, xrit_kind_t kind)
{
    if (kind == XRIT_HRIT)
        cairo_set_source_rgb(cr, 0.57, 0.82, 0.31);
    else
        cairo_set_source_rgb(cr, 0.62, 0.82, 0.90);
}

static void text_at(cairo_t *cr, double x, double y, const char *text,
                    double size, int bold, double align)
{
    cairo_text_extents_t ext;

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents(cr, text, &ext);
    cairo_move_to(cr, x - align * ext.width - ext.x_bearing, y);
    cairo_show_text(cr, text);
}

/* Diagonal strokes over a cell: the mark of an unconfirmed plan. */
static void hatch(cairo_t *cr, double x, double y, double w, double h)
{
    double d;

    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85);
    cairo_set_line_width(cr, 1.6);
    for (d = -h; d < w; d += 5.0) {
        cairo_move_to(cr, x + d, y + h);
        cairo_line_to(cr, x + d + h, y);
    }
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void slot_cell(cairo_t *cr, xrit_kind_t kind, int tentative,
                      double x, double y, double w, double h)
{
    set_kind_colour(cr, kind);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    if (tentative)
        hatch(cr, x, y, w, h);
}

static void draw_row(cairo_t *cr, const xrit_sat_t *sat, xrit_kind_t kind,
                     double x0, double y, double chart_w)
{
    double per_min = chart_w / DAY_MIN;
    int i;
    int hour;

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    text_at(cr, x0 - 6.0, y + ROW_H - 6.0, kind_text(kind), 11.0, 1, 1.0);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, x0, y, chart_w, ROW_H);
    cairo_fill(cr);

    for (i = 0; i < sat->slot_n; i++) {
        const xrit_slot_t *slot = &sat->slot[i];
        int start;

        if (slot->kind != kind)
            continue;
        for (start = slot->first; start < DAY_MIN; start += slot->period) {
            int end = start + slot->length;

            if (end > DAY_MIN) {
                slot_cell(cr, kind, sat->tentative, x0, y,
                          (end - DAY_MIN) * per_min, ROW_H);
                end = DAY_MIN;
            }
            slot_cell(cr, kind, sat->tentative, x0 + start * per_min, y,
                      (end - start) * per_min, ROW_H);
            if (slot->period <= 0)
                break;
        }
    }

    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.25, 0.42, 0.55, 0.75);
    for (hour = 0; hour <= 24; hour++) {
        double x = floor(x0 + hour * chart_w / 24.0) + 0.5;

        cairo_move_to(cr, x, y);
        cairo_line_to(cr, x, y + ROW_H);
    }
    cairo_rectangle(cr, x0 + 0.5, floor(y) + 0.5, chart_w - 1.0, ROW_H);
    cairo_stroke(cr);
}

static void draw_block(cairo_t *cr, const xrit_sat_t *sat, double x0, double y,
                       double chart_w, double minute, double jul_utc)
{
    cairo_text_extents_t ext;
    gchar *status;
    gchar *note;
    gchar *title;
    double mid = x0 + chart_w / 2.0;
    double now_x;
    int hour;

    /* Name in a blue frame, as on the operator's chart. */
    if (sat->cyrillic != NULL)
        title = g_strdup_printf("%s  \302\267  %s", sat->name, sat->cyrillic);
    else
        title = g_strdup(sat->name);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12.0);
    cairo_text_extents(cr, title, &ext);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, mid - ext.width / 2.0 - 8.0, y, ext.width + 16.0, NAME_H - 3.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.05, 0.36, 0.72);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    text_at(cr, mid, y + NAME_H - 8.5, title, 12.0, 1, 0.5);
    g_free(title);
    y += NAME_H + 4.0;

    /* Hours of the UTC day. */
    cairo_set_source_rgb(cr, 0.98, 0.87, 0.60);
    cairo_rectangle(cr, x0, y, chart_w, ROW_H);
    cairo_fill(cr);
    cairo_set_line_width(cr, 1.0);
    for (hour = 0; hour < 24; hour++) {
        gchar label[8];
        double x = floor(x0 + hour * chart_w / 24.0) + 0.5;

        g_snprintf(label, sizeof(label), "%02d", hour);
        cairo_set_source_rgba(cr, 0.25, 0.42, 0.55, 0.75);
        cairo_move_to(cr, x, y);
        cairo_line_to(cr, x, y + ROW_H);
        cairo_stroke(cr);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        text_at(cr, x0 + (hour + 0.5) * chart_w / 24.0, y + ROW_H - 6.0,
                label, 10.5, 1, 0.5);
    }
    cairo_set_source_rgba(cr, 0.25, 0.42, 0.55, 0.75);
    cairo_rectangle(cr, x0 + 0.5, floor(y) + 0.5, chart_w - 1.0, ROW_H);
    cairo_stroke(cr);

    draw_row(cr, sat, XRIT_HRIT, x0, y + ROW_H, chart_w);
    draw_row(cr, sat, XRIT_LRIT, x0, y + 2.0 * ROW_H, chart_w);

    /* Now. */
    now_x = floor(x0 + minute * chart_w / DAY_MIN) + 0.5;
    cairo_set_source_rgb(cr, 0.86, 0.05, 0.05);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, now_x, y - 3.0);
    cairo_line_to(cr, now_x, y + 3.0 * ROW_H + 3.0);
    cairo_stroke(cr);
    y += 3.0 * ROW_H;

    status = xrit_schedule_status_text(sat, jul_utc);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    text_at(cr, x0, y + 14.0, status, 11.0, 0, 0.0);
    g_free(status);

    note = g_strdup_printf("%s%s%s",
                           sat->position != NULL ? sat->position : "",
                           sat->position != NULL && sat->frequency != NULL ?
                           "  \302\267  " : "",
                           sat->frequency != NULL ? sat->frequency : "");
    cairo_set_source_rgb(cr, 0.05, 0.25, 0.60);
    text_at(cr, x0 + chart_w, y + 14.0, note, 10.5, 1, 1.0);
    g_free(note);
}

static void legend_row(cairo_t *cr, double x, double y, double w, int what,
                       const char *text)
{
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, x, y, 30.0, LEGEND_ROW - 2.0);
    cairo_fill(cr);
    if (what == 0 || what == 1)
        slot_cell(cr, what == 0 ? XRIT_HRIT : XRIT_LRIT, 0, x, y, 30.0,
                  LEGEND_ROW - 2.0);
    else if (what == 2) {
        slot_cell(cr, XRIT_HRIT, 1, x, y, 15.0, LEGEND_ROW - 2.0);
        slot_cell(cr, XRIT_LRIT, 1, x + 15.0, y, 15.0, LEGEND_ROW - 2.0);
    } else if (what == 3) {
        cairo_set_source_rgb(cr, 0.86, 0.05, 0.05);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, x + 15.0, y);
        cairo_line_to(cr, x + 15.0, y + LEGEND_ROW - 2.0);
        cairo_stroke(cr);
    }
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.25, 0.42, 0.55, 0.75);
    cairo_rectangle(cr, x + 0.5, floor(y) + 0.5, w - 1.0, LEGEND_ROW - 2.0);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    text_at(cr, x + 36.0, y + LEGEND_ROW - 7.0, text, 11.0, 1, 0.0);
}

void xrit_schedule_render(cairo_t *cr, int width, int height, double jul_utc)
{
    double x0 = MARGIN + LABEL_W;
    double chart_w = width - x0 - MARGIN;
    double minute = xrit_minute_of_day(jul_utc);
    double y = TITLE_H;
    double legend_w;
    gchar stamp[64];
    int i;

    (void)height;
    if (cr == NULL || chart_w < 120.0)
        return;

    cairo_save(cr);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0.30, 0.30, 0.30);
    text_at(cr, MARGIN, 30.0, _("LRIT/HRIT transmission schedule"), 19.0, 0, 0.0);
    g_snprintf(stamp, sizeof(stamp), "%02d:%02d UTC",
               (int)minute / 60, (int)minute % 60);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    text_at(cr, width - MARGIN, 30.0, stamp, 14.0, 1, 1.0);

    for (i = 0; i < xrit_schedule_count(); i++) {
        draw_block(cr, xrit_schedule_get(i), x0, y, chart_w, minute, jul_utc);
        y += BLOCK_H;
    }

    legend_w = chart_w < 330.0 ? chart_w : 330.0;
    legend_row(cr, MARGIN, y, legend_w, 0, _("HRIT transmission"));
    y += LEGEND_ROW;
    legend_row(cr, MARGIN, y, legend_w, 1, _("LRIT transmission"));
    y += LEGEND_ROW;
    legend_row(cr, MARGIN, y, legend_w, 2, _("Plan not confirmed by reception"));
    y += LEGEND_ROW;
    legend_row(cr, MARGIN, y, legend_w, 3, _("Current time"));
    y += LEGEND_ROW;
    legend_row(cr, MARGIN, y, legend_w, 4, _("No transmission"));
    y += LEGEND_ROW + 6.0;

    cairo_set_source_rgb(cr, 0.05, 0.25, 0.60);
    text_at(cr, MARGIN, y + 12.0,
            _("All times UTC. Transmission lengths are estimates; "
              "the operator may change the plan."), 10.5, 0, 0.0);
    cairo_restore(cr);
}

static gboolean on_sched_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    GtkAllocation alloc;

    (void)data;
    gtk_widget_get_allocation(widget, &alloc);
    xrit_schedule_render(cr, alloc.width, alloc.height, get_current_daynum());
    return FALSE;
}

static gboolean on_sched_tick(gpointer data)
{
    (void)data;
    if (sched_area != NULL)
        gtk_widget_queue_draw(sched_area);
    return TRUE;
}

static void on_sched_reload(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    xrit_schedule_reload();
    if (sched_area != NULL) {
        gtk_widget_set_size_request(sched_area, 720, xrit_schedule_chart_height());
        gtk_widget_queue_draw(sched_area);
    }
}

static void on_sched_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    if (sched_timer != 0) {
        g_source_remove(sched_timer);
        sched_timer = 0;
    }
    sched_window = NULL;
    sched_area = NULL;
}

void show_xrit_schedule(GtkWindow *parent)
{
    GtkWidget *vbox;
    GtkWidget *hbox;
    GtkWidget *label;
    GtkWidget *reload;
    gchar *path;
    gchar *text;

    if (sched_window != NULL) {
        gtk_window_present(GTK_WINDOW(sched_window));
        return;
    }

    sched_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(sched_window), _("Transmission schedule"));
    gtk_window_set_default_size(GTK_WINDOW(sched_window), 900, -1);
    if (parent != NULL && GTK_IS_WINDOW(parent))
        gtk_window_set_transient_for(GTK_WINDOW(sched_window), parent);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
    gtk_container_add(GTK_CONTAINER(sched_window), vbox);

    sched_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(sched_area, 720, xrit_schedule_chart_height());
    gtk_widget_set_hexpand(sched_area, TRUE);
    gtk_widget_set_vexpand(sched_area, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), sched_area, TRUE, TRUE, 0);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    path = conf_path();
    text = g_strdup_printf(_("Plan file: %s"), path);
    label = gtk_label_new(text);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    g_free(text);
    g_free(path);

    reload = gtk_button_new_with_label(_("Reload plan"));
    gtk_widget_set_tooltip_text(reload,
                                _("Read the plan file again after editing it."));
    gtk_box_pack_end(GTK_BOX(hbox), reload, FALSE, FALSE, 0);

    g_signal_connect(sched_area, "draw", G_CALLBACK(on_sched_draw), NULL);
    g_signal_connect(reload, "clicked", G_CALLBACK(on_sched_reload), NULL);
    g_signal_connect(sched_window, "destroy", G_CALLBACK(on_sched_destroy), NULL);

    sched_timer = g_timeout_add_seconds(15, on_sched_tick, NULL);
    gtk_widget_show_all(sched_window);
    gtk_widget_grab_focus(reload);
}

void show_xrit_schedule_cb(GtkWidget *widget, gpointer data)
{
    GtkWindow *parent = NULL;

    (void)widget;
    if (data != NULL && GTK_IS_WINDOW(data))
        parent = GTK_WINDOW(data);
    show_xrit_schedule(parent);
}
