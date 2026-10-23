/*
    Gpredict: Real-time satellite tracking and orbit prediction program

    Copyright (C)  2001-2017  Alexandru Csete, OZ9AEC.

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
#ifdef HAVE_CONFIG_H
#include <build-config.h>
#endif
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "config-keys.h"
#include "mod-cfg-get-param.h"
#include "sat-cfg.h"
#include "sat-pref-passes.h"

static GtkWidget *minelspin;

static gboolean dirty = FALSE;
static gboolean reset = FALSE;

void sat_pref_passes_cancel(GKeyFile * cfg)
{
    (void)cfg;

    dirty = FALSE;
    reset = FALSE;
}

void sat_pref_passes_ok(GKeyFile * cfg)
{
    if (dirty)
    {
        if (cfg != NULL)
        {
            g_key_file_set_integer(cfg,
                                   MOD_CFG_EVENT_LIST_SECTION,
                                   MOD_CFG_EVENT_LIST_MIN_EL,
                                   gtk_spin_button_get_value_as_int
                                   (GTK_SPIN_BUTTON(minelspin)));
        }
        else
        {
            sat_cfg_set_int(SAT_CFG_INT_MODULE_MIN_EL,
                            gtk_spin_button_get_value_as_int
                            (GTK_SPIN_BUTTON(minelspin)));
        }
    }
    else if (reset)
    {
        if (cfg == NULL)
        {
            sat_cfg_reset_int(SAT_CFG_INT_MODULE_MIN_EL);
        }
        else
        {
            g_key_file_remove_key(cfg,
                                  MOD_CFG_EVENT_LIST_SECTION,
                                  MOD_CFG_EVENT_LIST_MIN_EL, NULL);
        }
    }

    dirty = FALSE;
    reset = FALSE;
}

static void spin_changed_cb(GtkWidget * spinner, gpointer data)
{
    (void)spinner;
    (void)data;

    dirty = TRUE;
}

/**
 * Reset to the factory default in global mode, or drop the module override
 * so the global module default applies again.
 */
static void reset_cb(GtkWidget * button, gpointer cfg)
{
    gint            val;

    (void)button;

    if (cfg == NULL)
        val = sat_cfg_get_int_def(SAT_CFG_INT_MODULE_MIN_EL);
    else
        val = sat_cfg_get_int(SAT_CFG_INT_MODULE_MIN_EL);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(minelspin), val);

    reset = TRUE;
    dirty = FALSE;
}

GtkWidget      *sat_pref_passes_create(GKeyFile * cfg)
{
    GtkWidget      *vbox;
    GtkWidget      *table;
    GtkWidget      *label;
    GtkWidget      *button;
    GtkWidget      *butbox;
    gint            val;

    dirty = FALSE;
    reset = FALSE;

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    table = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(table), 10);
    gtk_grid_set_column_spacing(GTK_GRID(table), 5);
    gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);

    label = gtk_label_new(_("Minimum elevation"));
    g_object_set(label, "xalign", 0.0, "yalign", 0.5, NULL);
    gtk_grid_attach(GTK_GRID(table), label, 0, 0, 1, 1);

    minelspin = gtk_spin_button_new_with_range(0, 90, 1);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(minelspin), 0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(minelspin), TRUE);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(minelspin), FALSE);
    gtk_widget_set_tooltip_text(minelspin,
                                _("Only passes whose maximum elevation "
                                  "reaches this value are shown.\n"
                                  "0 shows every pass that crosses the horizon."));

    if (cfg != NULL)
    {
        val = mod_cfg_get_int(cfg,
                              MOD_CFG_EVENT_LIST_SECTION,
                              MOD_CFG_EVENT_LIST_MIN_EL,
                              SAT_CFG_INT_MODULE_MIN_EL);
    }
    else
    {
        val = sat_cfg_get_int(SAT_CFG_INT_MODULE_MIN_EL);
    }
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(minelspin), val);
    g_signal_connect(G_OBJECT(minelspin), "value-changed",
                     G_CALLBACK(spin_changed_cb), NULL);
    gtk_grid_attach(GTK_GRID(table), minelspin, 1, 0, 1, 1);

    label = gtk_label_new(_("[deg]"));
    g_object_set(label, "xalign", 0.0, "yalign", 0.5, NULL);
    gtk_grid_attach(GTK_GRID(table), label, 2, 0, 1, 1);

    button = gtk_button_new_with_label(_("Reset"));
    g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(reset_cb), cfg);
    if (cfg == NULL)
    {
        gtk_widget_set_tooltip_text(button,
                                    _("Reset settings to the default values."));
    }
    else
    {
        gtk_widget_set_tooltip_text(button,
                                    _("Reset module settings to the global values."));
    }

    butbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(butbox), GTK_BUTTONBOX_END);
    gtk_box_pack_end(GTK_BOX(butbox), button, FALSE, TRUE, 10);
    gtk_box_pack_end(GTK_BOX(vbox), butbox, FALSE, TRUE, 0);

    return vbox;
}
