/*
 * Gpredict-Blue: verify pixmaps resolve for uninstalled builds (run from src/).

  Copyright (C) 2025 Jan Koch DN9JUR
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifdef HAVE_CONFIG_H
#include <build-config.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"

static int failures;

static void expect_file(const gchar *label, const gchar *path)
{
    if (path == NULL || !g_file_test(path, G_FILE_TEST_EXISTS)) {
        g_printerr("FAIL: %s missing: %s\n", label, path ? path : "(null)");
        failures++;
    }
}

int main(void)
{
    gchar *maps;
    gchar *icons;
    gchar *logos;
    gchar *map;
    gchar *icon;
    gchar *logo;

    maps = get_maps_dir();
    icons = get_icon_dir();
    logos = get_logo_dir();

    map = g_build_filename(maps, "earth_800.png", NULL);
    icon = g_build_filename(icons, "gpredict-icon.png", NULL);
    logo = g_build_filename(logos, "gpredict_icon_color.svg", NULL);

    expect_file("default map", map);
    expect_file("app icon pixmap", icon);
    expect_file("about logo", logo);

    g_free(map);
    g_free(icon);
    g_free(logo);
    g_free(maps);
    g_free(icons);
    g_free(logos);

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
