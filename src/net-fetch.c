/*
    Gpredict: Real-time satellite tracking and orbit prediction program

    Copyright (C)  2001-2017  Alexandru Csete, OZ9AEC.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Downloads one URL without parking the whole application on a host
    that refuses or black-holes the connection (Celestrak IP blocks).
*/
#ifdef HAVE_CONFIG_H
#include <build-config.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <string.h>

#ifdef WIN32
#include <io.h>
#include "win32-fetch.h"
#else
#include <curl/curl.h>
#include <sys/select.h>
#include <unistd.h>
#endif

#include "net-fetch.h"
#include "sat-log.h"

#ifdef PACKAGE_VERSION
#define NET_FETCH_UA "Gpredict-Blue/" PACKAGE_VERSION
#elif defined(VERSION)
#define NET_FETCH_UA "Gpredict-Blue/" VERSION
#else
#define NET_FETCH_UA "Gpredict-Blue"
#endif

#define NET_FETCH_CONNECT_TIMEOUT 10L
#define NET_FETCH_TOTAL_TIMEOUT   30L
#define NET_FETCH_SAMPLE_BYTES    4095


void net_log_line(GtkWidget *logview, const gchar *fmt, ...)
{
    va_list         ap;
    gchar          *line;
    GtkTextBuffer  *buf;
    GtkTextIter     end;

    va_start(ap, fmt);
    line = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    sat_log_log(SAT_LOG_LEVEL_INFO, "%s", line);

    if (logview != NULL && GTK_IS_TEXT_VIEW(logview))
    {
        buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logview));
        gtk_text_buffer_get_end_iter(buf, &end);
        gtk_text_buffer_insert(buf, &end, line, -1);
        gtk_text_buffer_get_end_iter(buf, &end);
        gtk_text_buffer_insert(buf, &end, "\n", -1);
        gtk_text_buffer_get_end_iter(buf, &end);
        gtk_text_buffer_place_cursor(buf, &end);
        gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(logview),
                                           gtk_text_buffer_get_insert(buf));
        while (g_main_context_iteration(NULL, FALSE));
    }

    g_free(line);
}

gchar *net_fetch_host(const gchar *url)
{
    const gchar    *start;
    const gchar    *end;
    const gchar    *mark;

    if (url == NULL || url[0] == '\0')
        return g_strdup("");

    start = strstr(url, "://");
    start = (start != NULL) ? start + 3 : url;

    mark = strchr(start, '@');
    if (mark != NULL)
        start = mark + 1;

    end = start + strlen(start);
    mark = strchr(start, '/');
    if (mark != NULL && mark < end)
        end = mark;
    mark = strchr(start, ':');
    if (mark != NULL && mark < end)
        end = mark;

    return g_strndup(start, (gsize) (end - start));
}

static void truncate_file(FILE *outfile)
{
    int             fd;

    if (outfile == NULL)
        return;

    fflush(outfile);
    if (fseek(outfile, 0, SEEK_SET) != 0)
        return;

    fd = fileno(outfile);
#ifdef WIN32
    _chsize(fd, 0);
#else
    if (ftruncate(fd, 0) != 0)
        return;
#endif
    fflush(outfile);
}

static gchar *read_sample(FILE *outfile)
{
    long            pos;
    size_t          n;
    gchar          *buf;

    if (outfile == NULL)
        return g_strdup("");

    fflush(outfile);
    pos = ftell(outfile);
    if (fseek(outfile, 0, SEEK_SET) != 0)
        return g_strdup("");

    buf = g_malloc(NET_FETCH_SAMPLE_BYTES + 1);
    n = fread(buf, 1, NET_FETCH_SAMPLE_BYTES, outfile);
    buf[n] = '\0';

    if (pos >= 0)
        fseek(outfile, pos, SEEK_SET);

    return buf;
}

static gchar *body_snippet(const gchar *sample)
{
    GString        *text;
    gboolean        tag = FALSE;
    const gchar    *p;

    if (sample == NULL)
        return NULL;

    text = g_string_sized_new(160);
    for (p = sample; *p != '\0' && text->len < 160; p++)
    {
        gchar           c = *p;

        if (c == '<')
        {
            tag = TRUE;
            continue;
        }
        if (c == '>')
        {
            tag = FALSE;
            continue;
        }
        if (tag)
            continue;
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
        if ((guchar) c < 32)
            continue;
        if (c == ' ' && (text->len == 0 || text->str[text->len - 1] == ' '))
            continue;
        g_string_append_c(text, c);
    }

    g_strstrip(text->str);
    if (text->str[0] == '\0')
    {
        g_string_free(text, TRUE);
        return NULL;
    }

    return g_string_free(text, FALSE);
}

static gboolean body_says_blocked(const gchar *sample)
{
    gchar          *lower;
    gboolean        blocked = FALSE;

    if (sample == NULL || sample[0] == '\0')
        return FALSE;

    lower = g_ascii_strdown(sample, -1);
    if (strstr(lower, "blocked") != NULL ||
        strstr(lower, "access denied") != NULL ||
        strstr(lower, "too many requests") != NULL ||
        strstr(lower, "rate limit") != NULL ||
        strstr(lower, "rate-limit") != NULL ||
        strstr(lower, "forbidden") != NULL)
    {
        blocked = TRUE;
    }
    g_free(lower);

    return blocked;
}

static gboolean looks_like_markup(const gchar *ctype, const gchar *sample)
{
    const gchar    *p;

    if (ctype != NULL && g_strstr_len(ctype, -1, "html") != NULL)
        return TRUE;

    if (sample == NULL)
        return FALSE;

    p = sample;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
        p++;

    return g_str_has_prefix(p, "<!DOCTYPE") ||
        g_str_has_prefix(p, "<!doctype") ||
        g_str_has_prefix(p, "<html") ||
        g_str_has_prefix(p, "<HTML") ||
        g_str_has_prefix(p, "<head") ||
        g_str_has_prefix(p, "<HEAD");
}

static net_fetch_result_t classify_response(long http,
                                            gboolean transport,
                                            const gchar *ctype,
                                            const gchar *sample,
                                            double nbytes)
{
    gboolean        markup;

    if (transport && http == 0)
        return NET_FETCH_TRANSPORT;

    if (http == 403 || http == 429 || http == 451)
        return NET_FETCH_BLOCKED;

    markup = looks_like_markup(ctype, sample);
    if ((http >= 400 || markup) && body_says_blocked(sample))
        return NET_FETCH_BLOCKED;
    if (markup)
        return NET_FETCH_ERROR;
    if (http >= 400)
        return NET_FETCH_ERROR;
    if (transport)
        return NET_FETCH_TRANSPORT;
    if (nbytes < 1.0)
        return NET_FETCH_ERROR;

    return NET_FETCH_OK;
}

static void log_result(GtkWidget *logview, long http, double seconds,
                       double nbytes, const gchar *ctype,
                       const gchar *sample, const gchar *errstr)
{
    gchar          *snippet;

    if (errstr != NULL && http == 0)
    {
        net_log_line(logview, "< %s", errstr);
        return;
    }

    net_log_line(logview, "< HTTP %ld  %s  %.0f bytes  %.2fs",
                 http,
                 (ctype != NULL && ctype[0] != '\0') ? ctype : "-",
                 nbytes, seconds);

    snippet = body_snippet(sample);
    if (snippet != NULL &&
        (http != 200 || looks_like_markup(ctype, sample)))
    {
        net_log_line(logview, "< %s", snippet);
    }
    g_free(snippet);
}

#ifdef WIN32

net_fetch_result_t net_fetch_to_file(const gchar *url, FILE *outfile,
                                     const gchar *proxy, GtkWidget *logview,
                                     gboolean *cancel)
{
    int             res;
    long            http = 0;
    gboolean        transport = FALSE;
    gchar          *sample;
    net_fetch_result_t result;
    const gchar    *proxy_arg;

    if (cancel != NULL && *cancel)
    {
        net_log_line(logview, "%s", _("Download cancelled."));
        return NET_FETCH_CANCELLED;
    }

    net_log_line(logview, "* GET %s", url);
    net_log_line(logview, "* User-Agent: %s", NET_FETCH_UA);
    if (proxy != NULL && proxy[0] != '\0')
        net_log_line(logview, "%s", _("* using proxy"));

    proxy_arg = (proxy != NULL && proxy[0] != '\0') ? proxy : NULL;
    res = win32_fetch((char *) url, outfile, (char *) proxy_arg, NET_FETCH_UA);

    if (res == 0)
        http = 200;
    else if (res >= 400 && res < 600)
        http = res;
    else
        transport = TRUE;

    sample = read_sample(outfile);
    if (transport)
    {
        net_log_line(logview, "< win32 fetch failed (0x%x)", (unsigned) res);
    }
    else
    {
        log_result(logview, http, 0.0, (double) ftell(outfile), NULL, sample,
                   NULL);
    }

    result = classify_response(http, transport, NULL, sample,
                               (double) ftell(outfile));
    if (result != NET_FETCH_OK)
        truncate_file(outfile);

    g_free(sample);
    return result;
}

#else

static size_t write_to_file(void *ptr, size_t size, size_t nmemb, void *stream)
{
    return fwrite(ptr, size, nmemb, (FILE *) stream);
}

static void set_common_options(CURL *curl, const gchar *url,
                               const gchar *proxy, FILE *outfile)
{
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, NET_FETCH_UA);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, NET_FETCH_CONNECT_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, NET_FETCH_TOTAL_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 50L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, outfile);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
#elif LIBCURL_VERSION_NUM >= 0x071304
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                     (long) (CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    if (proxy != NULL && proxy[0] != '\0')
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy);
}

static void multi_wait_slice(CURLM *multi)
{
    fd_set          rd, wr, ex;
    int             maxfd = -1;
    long            timeout_ms = -1;
    struct timeval  tv;

    FD_ZERO(&rd);
    FD_ZERO(&wr);
    FD_ZERO(&ex);

    if (curl_multi_fdset(multi, &rd, &wr, &ex, &maxfd) != CURLM_OK || maxfd < 0)
    {
        g_usleep(100 * 1000);
        return;
    }

    curl_multi_timeout(multi, &timeout_ms);
    if (timeout_ms < 0 || timeout_ms > 100)
        timeout_ms = 100;

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (int) (timeout_ms % 1000) * 1000;
    select(maxfd + 1, &rd, &wr, &ex, &tv);
}

net_fetch_result_t net_fetch_to_file(const gchar *url, FILE *outfile,
                                     const gchar *proxy, GtkWidget *logview,
                                     gboolean *cancel)
{
    CURL           *curl;
    CURLM          *multi = NULL;
    CURLcode        res = CURLE_OK;
    long            http = 0;
    double          seconds = 0.0;
    double          nbytes = 0.0;
    char           *ctype = NULL;
    gchar          *sample = NULL;
    const gchar    *errstr = NULL;
    gboolean        transport = FALSE;
    gboolean        pump = (logview != NULL);
    net_fetch_result_t result = NET_FETCH_ERROR;

    if (cancel != NULL && *cancel)
    {
        net_log_line(logview, "%s", _("Download cancelled."));
        return NET_FETCH_CANCELLED;
    }

    net_log_line(logview, "* GET %s", url);
    net_log_line(logview, "* User-Agent: %s", NET_FETCH_UA);
    if (proxy != NULL && proxy[0] != '\0')
        net_log_line(logview, "%s", _("* using proxy"));

    curl = curl_easy_init();
    if (curl == NULL)
    {
        net_log_line(logview, "%s", _("< curl_easy_init() failed"));
        return NET_FETCH_ERROR;
    }
    set_common_options(curl, url, proxy, outfile);

    if (!pump)
    {
        res = curl_easy_perform(curl);
    }
    else
    {
        int             still = 1;
        CURLMcode       mc = CURLM_OK;

        multi = curl_multi_init();
        if (multi == NULL)
        {
            res = CURLE_FAILED_INIT;
        }
        else
        {
            curl_multi_add_handle(multi, curl);

            while (still)
            {
                if (cancel != NULL && *cancel)
                {
                    res = CURLE_ABORTED_BY_CALLBACK;
                    break;
                }

#ifdef CURLM_CALL_MULTI_PERFORM
                while ((mc = curl_multi_perform(multi, &still)) ==
                       CURLM_CALL_MULTI_PERFORM)
                    ;
#else
                mc = curl_multi_perform(multi, &still);
#endif
                if (mc != CURLM_OK)
                {
                    res = CURLE_FAILED_INIT;
                    break;
                }
                if (still)
                {
                    multi_wait_slice(multi);
                    while (g_main_context_iteration(NULL, FALSE));
                }
            }

            if (res != CURLE_ABORTED_BY_CALLBACK && res != CURLE_FAILED_INIT)
            {
                CURLMsg        *msg;
                int             left = 0;

                while ((msg = curl_multi_info_read(multi, &left)) != NULL)
                {
                    if (msg->msg == CURLMSG_DONE && msg->easy_handle == curl)
                        res = msg->data.result;
                }
            }
        }
    }

    if (cancel != NULL && *cancel)
    {
        net_log_line(logview, "%s", _("Download cancelled."));
        result = NET_FETCH_CANCELLED;
        goto cleanup;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &seconds);
#if LIBCURL_VERSION_NUM >= 0x073700
    {
        curl_off_t      downloaded = 0;

        curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &downloaded);
        nbytes = (double) downloaded;
    }
#else
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &nbytes);
#endif
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ctype);
    sample = read_sample(outfile);

    if (res != CURLE_OK)
    {
        errstr = curl_easy_strerror(res);
        if (http == 0)
            transport = TRUE;
    }

    log_result(logview, http, seconds, nbytes, ctype, sample, errstr);
    result = classify_response(http, transport, ctype, sample, nbytes);

cleanup:
    if (result != NET_FETCH_OK)
        truncate_file(outfile);
    g_free(sample);
    if (multi != NULL)
    {
        curl_multi_remove_handle(multi, curl);
        curl_multi_cleanup(multi);
    }
    curl_easy_cleanup(curl);
    return result;
}

#endif
