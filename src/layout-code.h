/*
  Gpredict-Blue: Real-time satellite tracking and orbit prediction program

  Copyright (C)  2025  Jan Koch DN9JUR

  Layout pane code strings for the module preferences editor (testable core).
*/
#ifndef LAYOUT_CODE_H
#define LAYOUT_CODE_H 1

#include <glib.h>

/** Number of predefined layouts plus the custom entry. */
#define LAYOUT_PREDEF_NUM 11

/** One view pane in the module grid. */
typedef struct {
    gint            type;
    gint            left;
    gint            right;
    gint            top;
    gint            bottom;
} layout_pane_t;

/** Maximum view type index (exclusive); pass GTK_SAT_MOD_VIEW_NUM from callers. */
gboolean        layout_pane_valid(const layout_pane_t *pane, gint max_view_types);
gboolean        layout_code_parse(const gchar *code, GArray **panes_out);
gchar          *layout_code_format(const GArray *panes);
gint            layout_code_match_predef(const gchar *code);
const gchar    *layout_code_predef_string(guint index);

#endif
