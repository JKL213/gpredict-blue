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

#include "compat.h"
#include "config-keys.h"
#include "gtk-sat-module.h"
#include "mod-cfg-get-param.h"
#include "sat-cfg.h"
#include "sat-log.h"
#include "sat-pref-layout.h"

static gboolean dirty = FALSE;
static gboolean reset = FALSE;

/* check boxes for window positioning */
static GtkWidget *mwin, *mod, *state;

/* Text entry for layout string */
static GtkWidget *gridstr;
static gulong   gridstr_sigid;

/* layout selector combo */
static GtkWidget *selector;
static gulong   selector_sigid;

/* layout thumbnail */
static GtkWidget *thumb;

/* Friendly pane editor. Writes the numeric layout code. */
static GtkWidget *pane_list;
static GtkWidget *preview;
static GtkWidget *preview_note;
static gboolean layout_syncing = FALSE;

static const gchar *const view_type_name[] = {
    N_("Table"),
    N_("Map"),
    N_("Polar"),
    N_("Single sat"),
    N_("Upcoming passes"),
    N_("Transmitters")
};

static const gchar *const view_type_short[] = {
    N_("List"),
    N_("Map"),
    N_("Polar"),
    N_("Single"),
    N_("Passes"),
    N_("Freq.")
};

static const gchar *const view_type_color[] = {
    "#d0d7de",
    "#b7e1cd",
    "#fce8b2",
    "#c6dbef",
    "#e6d5f2",
    "#f8d0c8"
};


/* the number of predefined layouts (+1 for custom). */
#define PREDEF_NUM 11

/* Predefined layouts. */
gchar          *predef_layout[PREDEF_NUM][3] = {
    {"1;0;2;0;1;2;0;1;1;2;3;1;2;1;2",
     N_("World map, polar and single sat (with frequencies)"),
     "gpredict-layout-00.png"},
    {"1;0;2;0;1", N_("World map"), "gpredict-layout-01.png"},
    {"0;0;2;0;1", N_("Table"), "gpredict-layout-02.png"},
    {"1;0;2;0;2;0;0;2;2;3", N_("World map and table"),
     "gpredict-layout-03.png"},
    {"2;0;1;0;1;3;1;2;0;1",
     N_("Polar and single sat (with frequencies)"),
     "gpredict-layout-04.png"},
    {"2;0;1;0;1;4;1;2;0;1", N_("Polar and upcoming passes"),
     "gpredict-layout-05.png"},
    {"1;0;3;0;4;0;0;3;4;6;2;0;1;6;8;3;1;2;6;8;4;2;3;6;8",
     N_("All views (narrow, with frequencies)"), "gpredict-layout-06.png"},
    {"1;0;3;0;3;0;0;3;3;4;2;3;4;0;2;4;3;4;2;3;3;3;4;3;4",
     N_("All views (wide, with frequencies)"), "gpredict-layout-07.png"},
    {"1;0;3;0;3;0;0;3;3;4;2;3;4;0;2;3;3;4;2;4",
     N_("Map, table, polar and single sat with frequencies (wide)"),
     "gpredict-layout-08.png"},
    {"5;0;2;0;1;3;0;2;1;2",
     N_("Transmitters and single sat (with frequencies)"),
     "gpredict-layout-99.png"},
    {"", N_("Custom"), "gpredict-layout-99.png"}
};


/* User pressed cancel. Any changes to config must be cancelled. */
void sat_pref_layout_cancel(GKeyFile * cfg)
{
    gchar          *str;

    (void)cfg;

    str = sat_cfg_get_str(SAT_CFG_STR_MODULE_GRID);
    gtk_entry_set_text(GTK_ENTRY(gridstr), str);
    g_free(str);

    dirty = FALSE;
}

/* User pressed OK. Any changes should be stored in config. */
void sat_pref_layout_ok(GKeyFile * cfg)
{
    if (dirty)
    {
        /* we have new settings */
        if (cfg != NULL)
        {
            g_key_file_set_string(cfg,
                                  MOD_CFG_GLOBAL_SECTION,
                                  MOD_CFG_GRID,
                                  gtk_entry_get_text(GTK_ENTRY(gridstr)));
        }
        else
        {
            sat_cfg_set_str(SAT_CFG_STR_MODULE_GRID,
                            gtk_entry_get_text(GTK_ENTRY(gridstr)));
            sat_cfg_set_bool(SAT_CFG_BOOL_MAIN_WIN_POS,
                             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                                          (mwin)));
            sat_cfg_set_bool(SAT_CFG_BOOL_MOD_WIN_POS,
                             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                                          (mod)));
            sat_cfg_set_bool(SAT_CFG_BOOL_MOD_STATE,
                             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                                          (state)));
        }
    }
    else if (reset)
    {
        /* we have to reset the values to global or default settings */
        if (cfg == NULL)
        {
            /* layout */
            sat_cfg_reset_str(SAT_CFG_STR_MODULE_GRID);

            /* window placement */
            sat_cfg_reset_bool(SAT_CFG_BOOL_MAIN_WIN_POS);
            sat_cfg_reset_bool(SAT_CFG_BOOL_MOD_WIN_POS);
            sat_cfg_reset_bool(SAT_CFG_BOOL_MOD_STATE);
        }
        else
        {
            g_key_file_remove_key((GKeyFile *) (cfg),
                                  MOD_CFG_GLOBAL_SECTION, MOD_CFG_GRID, NULL);
        }
    }
    dirty = FALSE;
    reset = FALSE;
}

/**
 * Get thumbnail icon filename from selection ID.
 *
 * @param sel The ID of the predefined layout or PREDEF_NUM-1 for custom.
 * @return A newly allocated string containing the full path of the icon.
 *
 * This function generates an icon file name from the ID of a predefined
 * layout. PREDEF_NUM-1 corresponds to the last entry in predef_layout[][],
 * which is the custom layout. The returned string should be freed when no
 * longer needed.
 *
 * The function checks that sel is within valid range (0...PREDEF_NUM-1). If
 * sel is outside the range, the custom layout icon is returned.
 */
static gchar   *thumb_file_from_sel(guint sel)
{
    gchar          *fname;

    if (sel < PREDEF_NUM)
        fname = icon_file_name(predef_layout[sel][2]);
    else
        fname = icon_file_name(predef_layout[PREDEF_NUM - 1][2]);

    return fname;
}

static void layout_rebuild_panes(void);
static void layout_preview_refresh(void);
static void layout_match_preset(void);
static void layout_panes_changed(GtkWidget * widget, gpointer data);

static void layout_style_label(GtkWidget * label, const gchar * color)
{
    GtkCssProvider *provider;
    gchar          *css;

    provider = gtk_css_provider_new();
    css = g_strdup_printf("* { background-color: %s; padding: 3px; }", color);
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(label),
                                   GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    g_free(css);
}

static gint layout_pane_int(GtkWidget * row, const gchar * key)
{
    GtkWidget      *spin;

    spin = g_object_get_data(G_OBJECT(row), key);
    return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
}

static void layout_preview_refresh(void)
{
    GList          *children;
    GList          *item;
    GArray         *rects;
    gint            cols = 0;
    gint            rows = 0;
    gint            r, c;
    gboolean        invalid = FALSE;
    gboolean        overlap = FALSE;
    GString        *note;

    if (preview == NULL)
        return;

    children = gtk_container_get_children(GTK_CONTAINER(preview));
    for (item = children; item != NULL; item = item->next)
        gtk_widget_destroy(item->data);
    g_list_free(children);

    rects = g_array_new(FALSE, FALSE, sizeof(gint) * 5);
    children = gtk_container_get_children(GTK_CONTAINER(pane_list));
    for (item = children; item != NULL; item = item->next)
    {
        GtkWidget      *row = item->data;
        GtkWidget      *combo;
        gint            values[5];

        combo = g_object_get_data(G_OBJECT(row), "combo");
        values[0] = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        values[1] = layout_pane_int(row, "left");
        values[2] = layout_pane_int(row, "right");
        values[3] = layout_pane_int(row, "top");
        values[4] = layout_pane_int(row, "bottom");
        if (values[0] < 0 || values[0] >= GTK_SAT_MOD_VIEW_NUM ||
            values[2] <= values[1] || values[4] <= values[3])
        {
            invalid = TRUE;
            continue;
        }
        cols = MAX(cols, values[2]);
        rows = MAX(rows, values[4]);
        g_array_append_val(rects, values);
    }
    g_list_free(children);

    if (cols > 12)
        cols = 12;
    if (rows > 12)
        rows = 12;

    for (r = 0; r < rows; r++)
    {
        for (c = 0; c < cols; c++)
        {
            GtkWidget      *cell;
            gint            hits = 0;
            gint            type = 0;
            guint           p;

            for (p = 0; p < rects->len; p++)
            {
                gint           *values = &g_array_index(rects, gint, p * 5);

                if (c >= values[1] && c < values[2] &&
                    r >= values[3] && r < values[4])
                {
                    hits++;
                    type = values[0];
                }
            }

            cell = gtk_label_new("");
            gtk_widget_set_size_request(cell, 64, 28);
            g_object_set(cell, "xalign", 0.5, NULL);
            if (hits > 1)
            {
                overlap = TRUE;
                gtk_label_set_text(GTK_LABEL(cell), "!");
                layout_style_label(cell, "#f4a8a8");
            }
            else if (hits == 1)
            {
                gtk_label_set_text(GTK_LABEL(cell), _(view_type_short[type]));
                layout_style_label(cell, view_type_color[type]);
            }
            else
            {
                layout_style_label(cell, "#f4f4f4");
            }
            gtk_grid_attach(GTK_GRID(preview), cell, c, r, 1, 1);
        }
    }
    g_array_free(rects, TRUE);
    gtk_widget_show_all(preview);

    note = g_string_new(NULL);
    if (invalid)
        g_string_append(note,
                        _("A pane is skipped in the preview when right is not "
                          "greater than left, or bottom is not greater than top. "
                          "The code is still saved."));
    if (overlap)
    {
        if (note->len > 0)
            g_string_append(note, "\n");
        g_string_append(note, _("Overlapping panes are marked in red."));
    }
    gtk_label_set_text(GTK_LABEL(preview_note), note->str);
    g_string_free(note, TRUE);
}

/* Select the preset whose code matches the entry, or Custom. */
static void layout_match_preset(void)
{
    const gchar    *text;
    guint           i;
    gint            sel = PREDEF_NUM - 1;
    gchar          *icon;

    if (gridstr == NULL || selector == NULL)
        return;

    text = gtk_entry_get_text(GTK_ENTRY(gridstr));
    for (i = 0; i < PREDEF_NUM - 1; i++)
    {
        if (!g_ascii_strcasecmp(text, predef_layout[i][0]))
        {
            sel = (gint) i;
            break;
        }
    }

    g_signal_handler_block(selector, selector_sigid);
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(selector)) != sel)
        gtk_combo_box_set_active(GTK_COMBO_BOX(selector), sel);
    g_signal_handler_unblock(selector, selector_sigid);

    gtk_widget_set_sensitive(gridstr, sel == PREDEF_NUM - 1);
    if (thumb != NULL)
    {
        icon = thumb_file_from_sel((guint) sel);
        gtk_image_set_from_file(GTK_IMAGE(thumb), icon);
        g_free(icon);
    }
}

static void layout_panes_changed(GtkWidget * widget, gpointer data)
{
    GString        *code;
    GList          *children;
    GList          *item;
    gboolean        first = TRUE;

    (void)widget;
    (void)data;

    if (layout_syncing || pane_list == NULL || gridstr == NULL)
        return;

    layout_syncing = TRUE;
    code = g_string_new(NULL);
    children = gtk_container_get_children(GTK_CONTAINER(pane_list));
    for (item = children; item != NULL; item = item->next)
    {
        GtkWidget      *row = item->data;
        GtkWidget      *combo;
        gint            type;

        combo = g_object_get_data(G_OBJECT(row), "combo");
        type = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        if (type < 0)
            type = 0;
        if (!first)
            g_string_append_c(code, ';');
        first = FALSE;
        g_string_append_printf(code, "%d;%d;%d;%d;%d", type,
                               layout_pane_int(row, "left"),
                               layout_pane_int(row, "right"),
                               layout_pane_int(row, "top"),
                               layout_pane_int(row, "bottom"));
    }
    g_list_free(children);

    g_signal_handler_block(gridstr, gridstr_sigid);
    gtk_entry_set_text(GTK_ENTRY(gridstr), code->str);
    g_signal_handler_unblock(gridstr, gridstr_sigid);
    g_string_free(code, TRUE);
    layout_syncing = FALSE;

    dirty = TRUE;
    layout_match_preset();
    layout_preview_refresh();
}

static GtkWidget *layout_spin(gint value)
{
    GtkWidget      *spin;

    spin = gtk_spin_button_new_with_range(0, 16, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    g_signal_connect(spin, "value-changed", G_CALLBACK(layout_panes_changed),
                     NULL);
    return spin;
}

static void layout_pane_remove_cb(GtkWidget * button, gpointer row)
{
    (void)button;
    gtk_widget_destroy(GTK_WIDGET(row));
    layout_panes_changed(NULL, NULL);
}

static void layout_add_pane(gint type, gint left, gint right, gint top,
                            gint bottom)
{
    GtkWidget      *row;
    GtkWidget      *combo;
    GtkWidget      *remove;
    GtkWidget      *left_w, *right_w, *top_w, *bottom_w;
    guint           i;

    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    combo = gtk_combo_box_text_new();
    for (i = 0; i < GTK_SAT_MOD_VIEW_NUM; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo),
                                       _(view_type_name[i]));
    if (type < 0 || type >= GTK_SAT_MOD_VIEW_NUM)
        type = 0;
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), type);
    g_signal_connect(combo, "changed", G_CALLBACK(layout_panes_changed), NULL);
    gtk_box_pack_start(GTK_BOX(row), combo, TRUE, TRUE, 0);

    left_w = layout_spin(left);
    right_w = layout_spin(right);
    top_w = layout_spin(top);
    bottom_w = layout_spin(bottom);
    gtk_widget_set_tooltip_text(left_w, _("Left"));
    gtk_widget_set_tooltip_text(right_w, _("Right"));
    gtk_widget_set_tooltip_text(top_w, _("Top"));
    gtk_widget_set_tooltip_text(bottom_w, _("Bottom"));
    gtk_box_pack_start(GTK_BOX(row), left_w, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), right_w, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), top_w, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), bottom_w, FALSE, FALSE, 0);

    remove = gtk_button_new_with_label(_("Remove"));
    g_signal_connect(remove, "clicked", G_CALLBACK(layout_pane_remove_cb),
                     row);
    gtk_box_pack_start(GTK_BOX(row), remove, FALSE, FALSE, 0);

    g_object_set_data(G_OBJECT(row), "combo", combo);
    g_object_set_data(G_OBJECT(row), "left", left_w);
    g_object_set_data(G_OBJECT(row), "right", right_w);
    g_object_set_data(G_OBJECT(row), "top", top_w);
    g_object_set_data(G_OBJECT(row), "bottom", bottom_w);
    gtk_box_pack_start(GTK_BOX(pane_list), row, FALSE, FALSE, 0);
    gtk_widget_show_all(row);
}

static void layout_pane_add_cb(GtkWidget * button, gpointer data)
{
    (void)button;
    (void)data;
    layout_add_pane(GTK_SAT_MOD_VIEW_LIST, 0, 1, 0, 1);
    layout_panes_changed(NULL, NULL);
}

static void layout_rebuild_panes(void)
{
    const gchar    *text;
    gchar         **parts;
    guint           n, i;

    if (pane_list == NULL || gridstr == NULL)
        return;

    text = gtk_entry_get_text(GTK_ENTRY(gridstr));
    parts = g_strsplit(text != NULL ? text : "", ";", 0);
    n = g_strv_length(parts);

    layout_syncing = TRUE;
    {
        GList          *children;
        GList          *item;

        children = gtk_container_get_children(GTK_CONTAINER(pane_list));
        for (item = children; item != NULL; item = item->next)
            gtk_widget_destroy(item->data);
        g_list_free(children);
    }
    for (i = 0; i + 4 < n; i += 5)
    {
        layout_add_pane((gint) g_ascii_strtoll(parts[i], NULL, 10),
                        (gint) g_ascii_strtoll(parts[i + 1], NULL, 10),
                        (gint) g_ascii_strtoll(parts[i + 2], NULL, 10),
                        (gint) g_ascii_strtoll(parts[i + 3], NULL, 10),
                        (gint) g_ascii_strtoll(parts[i + 4], NULL, 10));
    }
    layout_syncing = FALSE;
    g_strfreev(parts);
    layout_preview_refresh();
}

static void create_layout_editor(GtkGrid * table)
{
    GtkWidget      *frame;
    GtkWidget      *box;
    GtkWidget      *intro;
    GtkWidget      *add;

    frame = gtk_frame_new(_("Panes"));
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(box), 6);

    intro = gtk_label_new(_("Each row is one view. Choose its type and the "
                            "rectangle it occupies: left, right, top, bottom. "
                            "The layout code is filled in from these rows."));
    gtk_label_set_line_wrap(GTK_LABEL(intro), TRUE);
    g_object_set(intro, "xalign", 0.0, NULL);
    gtk_box_pack_start(GTK_BOX(box), intro, FALSE, FALSE, 0);

    pane_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(box), pane_list, FALSE, FALSE, 0);

    add = gtk_button_new_with_label(_("Add pane"));
    gtk_widget_set_halign(add, GTK_ALIGN_START);
    g_signal_connect(add, "clicked", G_CALLBACK(layout_pane_add_cb), NULL);
    gtk_box_pack_start(GTK_BOX(box), add, FALSE, FALSE, 0);

    preview = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(preview), 2);
    gtk_grid_set_column_spacing(GTK_GRID(preview), 2);
    gtk_box_pack_start(GTK_BOX(box), preview, FALSE, FALSE, 0);

    preview_note = gtk_label_new(NULL);
    gtk_label_set_line_wrap(GTK_LABEL(preview_note), TRUE);
    g_object_set(preview_note, "xalign", 0.0, NULL);
    gtk_box_pack_start(GTK_BOX(box), preview_note, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    gtk_grid_attach(table, frame, 0, 3, 4, 1);
    layout_rebuild_panes();
}

static void layout_code_changed(GtkWidget * widget, gpointer data)
{
    gchar          *entry, *end, *j;
    gint            len, pos;

    (void)data;

    /* step 1: ensure that only valid characters are entered
       (stolen from xlog, tnx pg4i)
     */
    entry = gtk_editable_get_chars(GTK_EDITABLE(widget), 0, -1);
    if ((len = g_utf8_strlen(entry, -1)) > 0)
    {
        end = entry + g_utf8_strlen(entry, -1);
        for (j = entry; j < end; ++j)
        {
            int             c = *j;

            if (g_ascii_isdigit(c) || c == ';')
            {
                dirty = TRUE;
            }
            else
            {
                gdk_display_beep(gdk_display_get_default());
                pos = gtk_editable_get_position(GTK_EDITABLE(widget));
                gtk_editable_delete_text(GTK_EDITABLE(widget), pos, pos + 1);
            }
        }
    }

    if (!layout_syncing)
    {
        layout_rebuild_panes();
        layout_match_preset();
    }
}

/* Callback to manage layout selection via combo box */
static void layout_selected_cb(GtkComboBox * combo, gpointer data)
{
    gint            idx;
    gchar          *icon;

    (void)data;

    idx = gtk_combo_box_get_active(combo);
    if (idx < PREDEF_NUM)
    {
        dirty = TRUE;

        /* update icon */
        icon = thumb_file_from_sel(idx);
        gtk_image_set_from_file(GTK_IMAGE(thumb), icon);
        g_free(icon);

        /* update layout code, unless Custom is selected */
        if (idx < PREDEF_NUM - 1)
        {
            g_signal_handler_block(gridstr, gridstr_sigid);
            gtk_entry_set_text(GTK_ENTRY(gridstr), predef_layout[idx][0]);
            g_signal_handler_unblock(gridstr, gridstr_sigid);
            gtk_widget_set_sensitive(gridstr, FALSE);
        }
        else
        {
            gtk_widget_set_sensitive(gridstr, TRUE);
        }
        layout_rebuild_panes();
    }
}

/* Create layout selector. */
static void create_layout_selector(GKeyFile * cfg, GtkGrid * table)
{
    GtkWidget      *label;
    gchar          *buffer;
    gchar          *thumbfile;
    guint           i, sel = PREDEF_NUM - 1;

    /* get the current settings */
    if (cfg != NULL)
    {
        buffer = mod_cfg_get_str(cfg,
                                 MOD_CFG_GLOBAL_SECTION,
                                 MOD_CFG_GRID, SAT_CFG_STR_MODULE_GRID);
    }
    else
    {
        buffer = sat_cfg_get_str(SAT_CFG_STR_MODULE_GRID);
    }

    /* create header */
    label = gtk_label_new(_("Select layout:"));
    g_object_set(label, "xalign", 1.0, "yalign", 0.5, NULL);
    gtk_grid_attach(table, label, 0, 0, 1, 1);

    /* layout selector */
    selector = gtk_combo_box_text_new();
    gtk_grid_attach(table, selector, 1, 0, 2, 1);

    for (i = 0; i < PREDEF_NUM; i++)
    {
        /* append default layout string to combo box */
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(selector),
                                       _(predef_layout[i][1]));

        /* check if this layout corresponds to the settings */
        if (!g_ascii_strcasecmp(buffer, predef_layout[i][0]))
        {
            sel = i;
        }
    }

    gtk_combo_box_set_active(GTK_COMBO_BOX(selector), sel);
    selector_sigid =
        g_signal_connect(selector, "changed", G_CALLBACK(layout_selected_cb),
                         NULL);

    /* layout preview thumbnail */
    thumbfile = thumb_file_from_sel(sel);
    thumb = gtk_image_new_from_file(thumbfile);
    g_free(thumbfile);
    gtk_grid_attach(table, thumb, 1, 1, 2, 1);

    /* layout string */
    label = gtk_label_new(_("Layout code:"));
    g_object_set(label, "xalign", 1.0, "yalign", 0.5, NULL);
    gtk_grid_attach(table, label, 0, 2, 1, 1);

    gridstr = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(gridstr), buffer);
    g_free(buffer);
    gtk_widget_set_tooltip_text(gridstr,
                                _("This entry holds the layout code for the "
                                  "module.\n"
                                  "Each view is type;left;right;top;bottom.\n"
                                  "Types: 0 table, 1 map, 2 polar, 3 single sat, "
                                  "4 upcoming passes, 5 transmitters.\n"
                                  "Consult the user manual for further details."));

    /* disable if it is a predefined layout */
    if (sel < PREDEF_NUM - 1)
    {
        gtk_widget_set_sensitive(gridstr, FALSE);
    }

    /* connect changed signal handler */
    gridstr_sigid =
        g_signal_connect(gridstr, "changed", G_CALLBACK(layout_code_changed),
                         NULL);

    gtk_grid_attach(table, gridstr, 1, 2, 3, 1);
    create_layout_editor(table);
}

/* Toggle window positioning settings. */
static void window_pos_toggle_cb(GtkWidget * toggle, gpointer data)
{
    (void)toggle;
    (void)data;
    dirty = TRUE;
}

/* window placement widgets */
static void create_window_placement(GtkBox * vbox)
{
    GtkWidget      *label;

    /* create header */
    label = gtk_label_new(NULL);
    g_object_set(label, "xalign", 0.0, "yalign", 0.5, NULL);
    gtk_label_set_markup(GTK_LABEL(label), _("<b>Window Placements:</b>"));
    gtk_box_pack_start(vbox, label, FALSE, FALSE, 0);

    /* main window setting */
    mwin =
        gtk_check_button_new_with_label(_("Restore position of main window"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mwin),
                                 sat_cfg_get_bool(SAT_CFG_BOOL_MAIN_WIN_POS));
    gtk_widget_set_tooltip_text(mwin,
                                _
                                ("If you check this button, gpredict will try "
                                 "to place the main window at the position it was "
                                 "during the last session.\n"
                                 "Note that window managers can ignore this request."));
    g_signal_connect(G_OBJECT(mwin), "toggled",
                     G_CALLBACK(window_pos_toggle_cb), NULL);
    gtk_box_pack_start(vbox, mwin, FALSE, FALSE, 0);

    /* module window setting */
    mod = gtk_check_button_new_with_label(_
                                          ("Restore position of module windows"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mod),
                                 sat_cfg_get_bool(SAT_CFG_BOOL_MOD_WIN_POS));
    gtk_widget_set_tooltip_text(mod,
                                _
                                ("If you check this button, gpredict will try "
                                 "to place the module windows at the position "
                                 "they were the last time.\n"
                                 "Note that window managers can ignore this request."));
    g_signal_connect(G_OBJECT(mod), "toggled",
                     G_CALLBACK(window_pos_toggle_cb), NULL);
    gtk_box_pack_start(vbox, mod, FALSE, FALSE, 0);

    /* module state */
    state =
        gtk_check_button_new_with_label(_
                                        ("Restore the state of modules when reopened (docked or window)"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state),
                                 sat_cfg_get_bool(SAT_CFG_BOOL_MOD_STATE));
    gtk_widget_set_tooltip_text(state,
                                _("If you check this button, gpredict will "
                                  "restore the states of the modules from the last time they were used."));
    g_signal_connect(G_OBJECT(state), "toggled",
                     G_CALLBACK(window_pos_toggle_cb), NULL);
    gtk_box_pack_start(vbox, state, FALSE, FALSE, 0);
}

/*
 * Reset settings.
 *
 * @param button The RESET button.
 * @param cfg Pointer to the module config or NULL in global mode.
 *
 * This function is called when the user clicks on the RESET button. In global mode
 * (when cfg = NULL) the function will reset the settings to the efault values, while
 * in "local" mode (when cfg != NULL) the function will reset the module settings to
 * the global settings. This is done by removing the corresponding key from the GKeyFile.
 */
static void reset_cb(GtkWidget * button, gpointer cfg)
{
    guint           i, sel = PREDEF_NUM - 1;
    gchar          *buffer;

    (void)button;

    /* views */
    if (cfg == NULL)
    {
        /* global mode, get defaults */
        buffer = sat_cfg_get_str_def(SAT_CFG_STR_MODULE_GRID);
        gtk_entry_set_text(GTK_ENTRY(gridstr), buffer);
    }
    else
    {
        /* local mode, get global value */
        buffer = sat_cfg_get_str(SAT_CFG_STR_MODULE_GRID);
        gtk_entry_set_text(GTK_ENTRY(gridstr), buffer);
    }

    /* findcombo box setting */
    for (i = 0; i < PREDEF_NUM; i++)
    {
        /* check if this layout corresponds to the settings */
        if (!g_ascii_strcasecmp(buffer, predef_layout[i][0]))
        {
            sel = i;
        }
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(selector), sel);
    g_free(buffer);

    /* window placement settings */
    if (cfg == NULL)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mwin),
                                     sat_cfg_get_bool_def
                                     (SAT_CFG_BOOL_MAIN_WIN_POS));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mod),
                                     sat_cfg_get_bool_def
                                     (SAT_CFG_BOOL_MOD_WIN_POS));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state),
                                     sat_cfg_get_bool_def
                                     (SAT_CFG_BOOL_MOD_STATE));
    }

    /* reset flags */
    reset = TRUE;
    dirty = FALSE;
}

static void create_reset_button(GKeyFile * cfg, GtkBox * vbox)
{
    GtkWidget      *button;
    GtkWidget      *butbox;

    button = gtk_button_new_with_label(_("Reset"));
    g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(reset_cb), cfg);

    if (cfg == NULL)
    {
        gtk_widget_set_tooltip_text(button,
                                    _
                                    ("Reset settings to the default values."));
    }
    else
    {
        gtk_widget_set_tooltip_text(button,
                                    _
                                    ("Reset module settings to the global values."));
    }

    butbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(butbox), GTK_BUTTONBOX_END);
    gtk_box_pack_end(GTK_BOX(butbox), button, FALSE, TRUE, 10);
    gtk_box_pack_end(vbox, butbox, FALSE, TRUE, 0);

}

GtkWidget      *sat_pref_layout_create(GKeyFile * cfg)
{
    GtkWidget      *table;
    GtkWidget      *vbox;

    /* create the table */
    table = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(table), 10);
    gtk_grid_set_column_spacing(GTK_GRID(table), 5);

    /* layout selector */
    create_layout_selector(cfg, GTK_GRID(table));

    /* separator */
    gtk_grid_attach(GTK_GRID(table),
                    gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, 4, 5, 1);

    /* create vertical box */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_set_homogeneous(GTK_BOX(vbox), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, TRUE, 0);

    /* window placement */
    if (cfg == NULL)
        create_window_placement(GTK_BOX(vbox));

    /* create RESET button */
    create_reset_button(cfg, GTK_BOX(vbox));

    /* reset flags */
    dirty = FALSE;
    reset = FALSE;

    return vbox;;
}
