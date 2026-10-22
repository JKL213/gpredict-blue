#ifndef __GTK_TRSP_VIEW_H__
#define __GTK_TRSP_VIEW_H__ 1

#include <glib.h>
#include <gtk/gtk.h>

#include "gtk-sat-data.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GTK_TYPE_TRSP_VIEW (gtk_trsp_view_get_type())
#define GTK_TRSP_VIEW(obj)                                                     \
    G_TYPE_CHECK_INSTANCE_CAST(obj, gtk_trsp_view_get_type(), GtkTrspView)
#define GTK_TRSP_VIEW_CLASS(klass)                                             \
    G_TYPE_CHECK_CLASS_CAST(klass, gtk_trsp_view_get_type(), GtkTrspViewClass)
#define IS_GTK_TRSP_VIEW(obj)                                                  \
    G_TYPE_CHECK_INSTANCE_TYPE(obj, gtk_trsp_view_get_type())

typedef struct _gtk_trsp_view GtkTrspView;
typedef struct _GtkTrspViewClass GtkTrspViewClass;

struct _gtk_trsp_view {
    GtkBox box;

    GtkWidget *header;   /* Satellite name. */
    GtkWidget *treeview; /* Transponder table. */
    GtkWidget *swin;     /* Scrolled window around the table. */
    GtkWidget *notice;   /* Shown when nothing can be listed. */

    GHashTable *satellites; /* Satellites tracked by the parent module. */
    qth_t *qth;             /* Current location. Not used for the table. */
    GKeyFile *cfgdata;

    gint catnum;    /* Catalogue number of the satellite on display, or -1. */
    gdouble tstamp; /* Time stamp set by GtkSatModule. */

    void (*update)(GtkWidget *widget);
};

struct _GtkTrspViewClass {
    GtkBoxClass parent_class;
};

GType gtk_trsp_view_get_type(void);
GtkWidget *gtk_trsp_view_new(GKeyFile *cfgdata, GHashTable *sats, qth_t *qth);
void gtk_trsp_view_update(GtkWidget *widget);
void gtk_trsp_view_reload_sats(GtkWidget *view, GHashTable *sats);
void gtk_trsp_view_select_sat(GtkWidget *view, gint catnum);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GTK_TRSP_VIEW_H__ */
