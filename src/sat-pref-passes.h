#ifndef SAT_PREF_PASSES_H
#define SAT_PREF_PASSES_H 1

GtkWidget      *sat_pref_passes_create(GKeyFile * cfg);
void            sat_pref_passes_cancel(GKeyFile * cfg);
void            sat_pref_passes_ok(GKeyFile * cfg);

#endif
