#ifndef SAT_PREF_NOTIFY_H
#define SAT_PREF_NOTIFY_H 1

#include <gtk/gtk.h>

GtkWidget *sat_pref_notify_create(void);
void sat_pref_notify_cancel(void);
void sat_pref_notify_ok(void);

#endif
