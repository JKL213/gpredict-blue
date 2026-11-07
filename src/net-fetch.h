/*
    Gpredict: Real-time satellite tracking and orbit prediction program

    Copyright (C)  2001-2017  Alexandru Csete, OZ9AEC.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
#ifndef NET_FETCH_H
#define NET_FETCH_H 1

#include <gtk/gtk.h>
#include <stdio.h>

/** Result of one HTTP download. */
typedef enum {
    NET_FETCH_OK = 0,           /*!< Body saved and looks usable. */
    NET_FETCH_ERROR = 1,        /*!< This URL failed; other URLs may still work. */
    NET_FETCH_TRANSPORT = 2,    /*!< Timeout or no connection. Skip this host. */
    NET_FETCH_BLOCKED = 3,      /*!< 403/429 or a block page. Stop the batch. */
    NET_FETCH_CANCELLED = 4     /*!< User cancelled. */
} net_fetch_result_t;

/**
 * Append one line to the download log and to the gpredict log.
 * @param logview GtkTextView, or NULL when there is no download window.
 */
void net_log_line(GtkWidget *logview, const gchar *fmt, ...);

/** Hostname of an http(s) URL, or an empty string. Caller frees the result. */
gchar *net_fetch_host(const gchar *url);

/**
 * Download one URL into an open file.
 *
 * The transfer has a connect timeout and an overall timeout. When logview
 * is set, the GTK main loop keeps running so the download window stays
 * responsive. A refused or blocked response is not left in the file.
 *
 * @param cancel Optional flag. Set to TRUE to abort the transfer.
 */
net_fetch_result_t net_fetch_to_file(const gchar *url,
                                     FILE *outfile,
                                     const gchar *proxy,
                                     GtkWidget *logview,
                                     gboolean *cancel);

#endif
