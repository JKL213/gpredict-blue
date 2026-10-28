/*
 * Preferences for desktop AOS and LOS notifications.
 */
#ifdef HAVE_CONFIG_H
#include <build-config.h>
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "sat-cfg.h"
#include "sat-pref-notify.h"

static GtkWidget *enable = NULL;
static GtkWidget *all_sats = NULL;
static GtkWidget *lead = NULL;
static gboolean dirty = FALSE;

static void changed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    dirty = TRUE;
}

static void reset_cb(GtkWidget *button, gpointer data)
{
    (void)button;
    (void)data;

    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(enable),
        sat_cfg_get_bool_def(SAT_CFG_BOOL_NOTIFY_ENABLE));
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(all_sats),
        sat_cfg_get_bool_def(SAT_CFG_BOOL_NOTIFY_ALL));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(lead),
                              sat_cfg_get_int_def(SAT_CFG_INT_NOTIFY_LEAD));
    dirty = TRUE;
}

GtkWidget *sat_pref_notify_create(void)
{
    GtkWidget *vbox;
    GtkWidget *label;
    GtkWidget *hbox;
    GtkWidget *button;
    GtkWidget *bbox;

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);

    label = gtk_label_new(NULL);
    g_object_set(label, "xalign", 0.0f, "yalign", 0.5f, NULL);
    gtk_label_set_markup(GTK_LABEL(label), _("<b>Desktop notifications:</b>"));
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);

    enable = gtk_check_button_new_with_label(_("Enable desktop notifications"));
    gtk_widget_set_tooltip_text(enable,
                                _("Announce AOS and LOS through the desktop "
                                  "notification service. A module minimum "
                                  "elevation limits this to passes that "
                                  "reach it."));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enable),
                                 sat_cfg_get_bool(SAT_CFG_BOOL_NOTIFY_ENABLE));
    g_signal_connect(enable, "toggled", G_CALLBACK(changed), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), enable, FALSE, TRUE, 0);

    all_sats = gtk_check_button_new_with_label(
        _("Notify for every satellite in the module"));
    gtk_widget_set_tooltip_text(all_sats,
                                _("When disabled, only the selected target "
                                  "satellite is announced"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(all_sats),
                                 sat_cfg_get_bool(SAT_CFG_BOOL_NOTIFY_ALL));
    g_signal_connect(all_sats, "toggled", G_CALLBACK(changed), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), all_sats, FALSE, TRUE, 0);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    label = gtk_label_new(_("Warn this many seconds before AOS:"));
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);
    lead = gtk_spin_button_new_with_range(0, 600, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(lead),
                              sat_cfg_get_int(SAT_CFG_INT_NOTIFY_LEAD));
    gtk_widget_set_tooltip_text(lead,
                                _("0 disables the early warning. AOS and LOS "
                                  "are still announced."));
    g_signal_connect(lead, "value-changed", G_CALLBACK(changed), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), lead, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

    button = gtk_button_new_with_label(_("Reset"));
    gtk_widget_set_tooltip_text(button, _("Reset settings to the default values."));
    g_signal_connect(button, "clicked", G_CALLBACK(reset_cb), NULL);
    bbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
    gtk_box_pack_end(GTK_BOX(bbox), button, FALSE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), bbox, FALSE, TRUE, 0);

    dirty = FALSE;

    return vbox;
}

void sat_pref_notify_cancel(void)
{
    dirty = FALSE;
}

void sat_pref_notify_ok(void)
{
    gint seconds;

    if (!dirty)
        return;

    sat_cfg_set_bool(SAT_CFG_BOOL_NOTIFY_ENABLE,
                     gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enable)));
    sat_cfg_set_bool(SAT_CFG_BOOL_NOTIFY_ALL,
                     gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(all_sats)));

    seconds = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(lead));
    if (seconds < 0)
        seconds = 0;
    if (seconds > 600)
        seconds = 600;
    sat_cfg_set_int(SAT_CFG_INT_NOTIFY_LEAD, seconds);

    dirty = FALSE;
}
