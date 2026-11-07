/*
  Gpredict-Blue: Real-time satellite tracking and orbit prediction program

  Copyright (C)  2025  Jan Koch DN9JUR
*/
#ifdef HAVE_CONFIG_H
#include <build-config.h>
#endif

#include "layout-code.h"

static const gchar *const predef_codes[LAYOUT_PREDEF_NUM] = {
    "1;0;2;0;1;2;0;1;1;2;3;1;2;1;2",
    "1;0;2;0;1",
    "0;0;2;0;1",
    "1;0;2;0;2;0;0;2;2;3",
    "2;0;1;0;1;3;1;2;0;1",
    "2;0;1;0;1;4;1;2;0;1",
    "1;0;3;0;4;0;0;3;4;6;2;0;1;6;8;3;1;2;6;8;4;2;3;6;8",
    "1;0;3;0;3;0;0;3;3;4;2;3;4;0;2;4;3;4;2;3;3;3;4;3;4",
    "1;0;3;0;3;0;0;3;3;4;2;3;4;0;2;3;3;4;2;4",
    "5;0;2;0;1;3;0;2;1;2",
    ""
};

const gchar    *
layout_code_predef_string(guint index)
{
    if (index >= LAYOUT_PREDEF_NUM)
        return "";

    return predef_codes[index];
}

gboolean
layout_pane_valid(const layout_pane_t *pane, gint max_view_types)
{
    if (pane == NULL || max_view_types < 1)
        return FALSE;

    if (pane->type < 0 || pane->type >= max_view_types)
        return FALSE;
    if (pane->right <= pane->left)
        return FALSE;
    if (pane->bottom <= pane->top)
        return FALSE;

    return TRUE;
}

gboolean
layout_code_parse(const gchar *code, GArray **panes_out)
{
    gchar         **parts;
    guint           n;
    guint           i;
    GArray         *panes;

    if (panes_out == NULL)
        return FALSE;

    *panes_out = NULL;
    if (code == NULL)
        code = "";

    parts = g_strsplit(code, ";", 0);
    n = g_strv_length(parts);

    panes = g_array_new(FALSE, FALSE, sizeof(layout_pane_t));
    for (i = 0; i + 4 < n; i += 5)
    {
        layout_pane_t   pane;

        pane.type = (gint) g_ascii_strtoll(parts[i], NULL, 10);
        pane.left = (gint) g_ascii_strtoll(parts[i + 1], NULL, 10);
        pane.right = (gint) g_ascii_strtoll(parts[i + 2], NULL, 10);
        pane.top = (gint) g_ascii_strtoll(parts[i + 3], NULL, 10);
        pane.bottom = (gint) g_ascii_strtoll(parts[i + 4], NULL, 10);
        g_array_append_val(panes, pane);
    }
    g_strfreev(parts);

    *panes_out = panes;
    return TRUE;
}

gchar          *
layout_code_format(const GArray *panes)
{
    GString        *code;
    guint           i;

    code = g_string_new(NULL);
    if (panes == NULL)
        return g_string_free(code, FALSE);

    for (i = 0; i < panes->len; i++)
    {
        const layout_pane_t *pane =
            &g_array_index(panes, layout_pane_t, i);

        if (i > 0)
            g_string_append_c(code, ';');
        g_string_append_printf(code, "%d;%d;%d;%d;%d",
                               pane->type, pane->left, pane->right,
                               pane->top, pane->bottom);
    }

    return g_string_free(code, FALSE);
}

gint
layout_code_match_predef(const gchar *code)
{
    guint           i;

    if (code == NULL)
        return LAYOUT_PREDEF_NUM - 1;

    for (i = 0; i < LAYOUT_PREDEF_NUM - 1; i++)
    {
        if (predef_codes[i][0] == '\0')
            continue;
        if (!g_ascii_strcasecmp(code, predef_codes[i]))
            return (gint) i;
    }

    return LAYOUT_PREDEF_NUM - 1;
}
