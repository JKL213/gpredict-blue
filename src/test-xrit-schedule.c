/*
 * Gpredict-Blue: LRIT/HRIT transmission schedule, plan file and timing.

  Copyright (C) 2026 The Gpredict contributors.
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifdef HAVE_CONFIG_H
#include <build-config.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"
#include "sat-log.h"
#include "xrit-schedule.h"

static int failures;

/* The schedule only needs these two from the rest of the program. */
void sat_log_log(sat_log_level_t level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}

gdouble get_current_daynum(void)
{
    return 2451545.0;
}

static void check(int good, const char *what)
{
    if (good) {
        g_print("ok %s\n", what);
    } else {
        g_printerr("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    const xrit_sat_t *sat;
    xrit_state_t st;
    gchar *tmp;
    gchar *dir;
    gchar *file;

    /* Keep the plan file out of the real configuration directory. */
    tmp = g_dir_make_tmp("gpredict-xrit-XXXXXX", NULL);
    if (tmp == NULL)
        return 1;
    g_setenv("XDG_CONFIG_HOME", tmp, TRUE);
    dir = get_user_conf_dir();
    g_mkdir_with_parents(dir, 0700);
    file = g_build_filename(dir, "xrit-schedule.conf", NULL);

    check(fabs(xrit_minute_of_day(2451545.0) - 720.0) < 1.0e-6,
          "Julian date .0 is noon UTC");
    check(fabs(xrit_minute_of_day(2451544.5) - 0.0) < 1.0e-6,
          "Julian date .5 is midnight UTC");

    check(xrit_schedule_count() == 2, "built-in plan has two satellites");
    check(g_file_test(file, G_FILE_TEST_EXISTS), "plan file is written");
    check(xrit_schedule_find(41105) == NULL, "ELEKTRO-L 2 has no plan");
    sat = xrit_schedule_find(44903);
    check(sat != NULL && sat->slot_n == 2 && !sat->tentative, "ELEKTRO-L 3 plan");
    check(xrit_schedule_find(67756) != NULL &&
          xrit_schedule_find(67756)->tentative, "ELEKTRO-L 5 plan is tentative");

    xrit_schedule_state(sat, 3 * 60 + 20, &st);
    check(st.now == XRIT_HRIT && st.remaining == 7, "03:20 HRIT, 7 min left");
    check(st.next == XRIT_LRIT && st.next_start == 3 * 60 + 42 && st.next_in == 22,
          "03:20 next is LRIT at 03:42");

    xrit_schedule_state(sat, 21 * 60 + 50, &st);
    check(st.now == XRIT_LRIT && st.remaining == 7, "21:50 LRIT, 7 min left");

    xrit_schedule_state(sat, 30, &st);
    check(st.now == XRIT_NONE && st.next == XRIT_LRIT && st.next_in == 12,
          "00:30 silent, LRIT in 12 min");

    xrit_schedule_state(sat, 12, &st);
    check(st.now == XRIT_HRIT && st.remaining == 15, "00:12 HRIT starts");
    xrit_schedule_state(sat, 27, &st);
    check(st.now == XRIT_NONE, "00:27 HRIT is over");

    xrit_schedule_state(sat, 23 * 60 + 59, &st);
    check(st.now == XRIT_NONE && st.next == XRIT_HRIT && st.next_start == 12 &&
          st.next_in == 13, "23:59 next is tomorrow's HRIT at 00:12");

    /* A plan of the user's own, with a transmission across midnight. */
    g_file_set_contents(file,
                        "[TEST SAT]\ncatnum=99999\n"
                        "hrit=23:50;20;0\nlrit=06:00;10;720\nlrit2=bogus\n",
                        -1, NULL);
    xrit_schedule_reload();
    sat = xrit_schedule_find(99999);
    check(xrit_schedule_count() == 1 && sat != NULL && sat->slot_n == 2,
          "user plan replaces the built-in one, bad entry dropped");
    xrit_schedule_state(sat, 5, &st);
    check(st.now == XRIT_HRIT && st.remaining == 5, "00:05 still in the 23:50 slot");
    check(st.next == XRIT_LRIT && st.next_start == 360, "then LRIT at 06:00");
    xrit_schedule_state(sat, 18 * 60 + 5, &st);
    check(st.now == XRIT_LRIT, "18:05 second LRIT of the day");

    /* An unreadable file falls back to the built-in plan. */
    g_file_set_contents(file, "this is not a key file\n", -1, NULL);
    xrit_schedule_reload();
    check(xrit_schedule_count() == 2, "broken file falls back to built-in plan");

    g_unlink(file);
    g_rmdir(dir);
    g_rmdir(tmp);
    g_free(file);
    g_free(dir);
    g_free(tmp);
    return failures == 0 ? 0 : 1;
}
