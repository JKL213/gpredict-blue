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

#include "gtk-trsp-view.h"
#include "trsp-conf.h"

/* Columns of the transmitter table. All values are display strings. */
enum {
    TRSP_COL_NAME = 0,
    TRSP_COL_UPLINK,
    TRSP_COL_DOWNLINK,
    TRSP_COL_MODE,
    TRSP_COL_BAUD,
    TRSP_COL_INVERT,
    TRSP_COL_NUMBER
};

static GtkBoxClass *parent_class = NULL;

static void gtk_trsp_view_class_init(GtkTrspViewClass *class,
                                     gpointer class_data);
static void gtk_trsp_view_init(GtkTrspView *view, gpointer g_class);
static void gtk_trsp_view_destroy(GtkWidget *widget);
static gchar *freq_to_str(gint64 low, gint64 high);
static void show_notice(GtkTrspView *view, const gchar *text);
static void fill_transponders(GtkTrspView *view, guint catnum);

GType gtk_trsp_view_get_type(void)
{
    static GType gtk_trsp_view_type = 0;

    if (!gtk_trsp_view_type)
    {
        static const GTypeInfo gtk_trsp_view_info = {
            sizeof(GtkTrspViewClass),
            NULL, /* base_init */
            NULL, /* base_finalize */
            (GClassInitFunc)gtk_trsp_view_class_init,
            NULL, /* class_finalize */
            NULL, /* class_data */
            sizeof(GtkTrspView),
            5, /* n_preallocs */
            (GInstanceInitFunc)gtk_trsp_view_init,
            NULL};

        gtk_trsp_view_type = g_type_register_static(
            GTK_TYPE_BOX, "GtkTrspView", &gtk_trsp_view_info, 0);
    }

    return gtk_trsp_view_type;
}

static void gtk_trsp_view_class_init(GtkTrspViewClass *class,
                                     gpointer class_data)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(class);

    (void)class_data;

    widget_class->destroy = gtk_trsp_view_destroy;
    parent_class = g_type_class_peek_parent(class);
}

static void gtk_trsp_view_init(GtkTrspView *view, gpointer g_class)
{
    (void)view;
    (void)g_class;
}

static void gtk_trsp_view_destroy(GtkWidget *widget)
{
    (*GTK_WIDGET_CLASS(parent_class)->destroy)(widget);
}

/** Format a frequency or a frequency range in MHz. */
static gchar *freq_to_str(gint64 low, gint64 high)
{
    if (low <= 0)
        return g_strdup("");

    if (high > low)
        return g_strdup_printf("%.4f \342\200\222 %.4f", low / 1.0e6,
                               high / 1.0e6);

    return g_strdup_printf("%.4f", low / 1.0e6);
}

static void show_notice(GtkTrspView *view, const gchar *text)
{
    gtk_label_set_text(GTK_LABEL(view->notice), text);
    gtk_widget_hide(view->swin);
    gtk_widget_show(view->notice);
}

static void fill_transponders(GtkTrspView *view, guint catnum)
{
    GSList *trsplist;
    GtkListStore *store;
    guint i, n;

    trsplist = read_transponders(catnum);
    if (trsplist == NULL)
    {
        show_notice(view, _("No transponders"));
        return;
    }

    store = gtk_list_store_new(TRSP_COL_NUMBER, G_TYPE_STRING, G_TYPE_STRING,
                               G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                               G_TYPE_STRING);

    n = g_slist_length(trsplist);
    for (i = 0; i < n; i++)
    {
        trsp_t *trsp = (trsp_t *)g_slist_nth_data(trsplist, i);
        GtkTreeIter iter;
        gchar *uplink;
        gchar *downlink;
        gchar *baud;
        const gchar *invert;

        uplink = freq_to_str(trsp->uplow, trsp->uphigh);
        downlink = freq_to_str(trsp->downlow, trsp->downhigh);
        if (trsp->baud > 0.0)
            baud = g_strdup_printf("%.2f", trsp->baud);
        else
            baud = g_strdup("");
        invert = trsp->invert ? _("YES") : _("NO");

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, TRSP_COL_NAME,
                           trsp->name != NULL ? trsp->name : "",
                           TRSP_COL_UPLINK, uplink, TRSP_COL_DOWNLINK, downlink,
                           TRSP_COL_MODE, trsp->mode != NULL ? trsp->mode : "",
                           TRSP_COL_BAUD, baud, TRSP_COL_INVERT, invert, -1);

        g_free(uplink);
        g_free(downlink);
        g_free(baud);
    }
    free_transponders(trsplist);

    gtk_tree_view_set_model(GTK_TREE_VIEW(view->treeview),
                            GTK_TREE_MODEL(store));
    g_object_unref(store);

    gtk_widget_hide(view->notice);
    gtk_widget_show(view->swin);
}

GtkWidget *gtk_trsp_view_new(GKeyFile *cfgdata, GHashTable *sats, qth_t *qth)
{
    GtkWidget *widget;
    GtkTrspView *view;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    const gchar *titles[TRSP_COL_NUMBER] = {
        N_("Name"), N_("Uplink"), N_("Downlink"),
        N_("Mode"), N_("Baud"),   N_("Invert")};
    guint i;

    widget = g_object_new(GTK_TYPE_TRSP_VIEW, NULL);
    g_object_set(widget, "orientation", GTK_ORIENTATION_VERTICAL, "spacing", 4,
                 NULL);
    view = GTK_TRSP_VIEW(widget);

    view->update = gtk_trsp_view_update;
    view->cfgdata = cfgdata;
    view->satellites = sats;
    view->qth = qth;
    view->catnum = -1;
    view->tstamp = 0.0;

    view->header = gtk_label_new(NULL);
    g_object_set(view->header, "xalign", 0.0, "yalign", 0.5, NULL);
    gtk_label_set_markup(GTK_LABEL(view->header), _("<b>Transmitters</b>"));
    gtk_box_pack_start(GTK_BOX(view), view->header, FALSE, FALSE, 0);

    view->treeview = gtk_tree_view_new();
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(view->treeview),
                                 GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    for (i = 0; i < TRSP_COL_NUMBER; i++)
    {
        renderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new_with_attributes(_(titles[i]),
                                                          renderer, "text", i,
                                                          NULL);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(view->treeview), column);
    }

    view->swin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(view->swin),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(view->swin), view->treeview);
    gtk_box_pack_start(GTK_BOX(view), view->swin, TRUE, TRUE, 0);

    view->notice = gtk_label_new(_("No satellite selected"));
    g_object_set(view->notice, "xalign", 0.0, "yalign", 0.5, NULL);
    gtk_box_pack_start(GTK_BOX(view), view->notice, FALSE, FALSE, 0);

    gtk_widget_show_all(widget);
    gtk_widget_hide(view->swin);

    return widget;
}

/** Nothing to refresh on the timer; the table follows the selected satellite. */
void gtk_trsp_view_update(GtkWidget *widget)
{
    (void)widget;
}

void gtk_trsp_view_reload_sats(GtkWidget *widget, GHashTable *sats)
{
    GtkTrspView *view = GTK_TRSP_VIEW(widget);

    view->satellites = sats;
    gtk_trsp_view_select_sat(widget, view->catnum);
}

void gtk_trsp_view_select_sat(GtkWidget *widget, gint catnum)
{
    GtkTrspView *view = GTK_TRSP_VIEW(widget);
    sat_t *sat = NULL;
    gchar *title;

    view->catnum = catnum;

    if (catnum > 0 && view->satellites != NULL)
        sat = SAT(g_hash_table_lookup(view->satellites, &view->catnum));

    if (sat == NULL)
    {
        gtk_label_set_markup(GTK_LABEL(view->header), _("<b>Transmitters</b>"));
        show_notice(view, _("No satellite selected"));
        return;
    }

    title = g_markup_printf_escaped("<b>%s</b>",
                                    sat->nickname != NULL ? sat->nickname
                                                          : sat->name);
    gtk_label_set_markup(GTK_LABEL(view->header), title);
    g_free(title);

    fill_transponders(view, (guint)catnum);
}
