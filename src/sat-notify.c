/*
 * Desktop notifications for acquisition and loss of signal.
 *
 * Uses the FreeDesktop notification service over the session bus. If that
 * service is not available the failure is logged once and further attempts
 * stay quiet.
 */
#ifdef HAVE_CONFIG_H
#include <build-config.h>
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <math.h>

#include "orbit-tools.h"
#include "sat-cfg.h"
#include "sat-log.h"
#include "sat-notify.h"

#define NOTIFY_EXPIRE_MS 7000

static GHashTable *prev_el = NULL;
static GHashTable *sent = NULL;
static GDBusProxy *proxy = NULL;
static gboolean unavailable = FALSE;

static void remember_el(gint catnum, gdouble el, gdouble *previous,
                        gboolean *have_previous)
{
    gint *key;
    gdouble *slot;

    if (prev_el == NULL)
        prev_el = g_hash_table_new_full(g_int_hash, g_int_equal, g_free, g_free);

    key = g_new(gint, 1);
    *key = catnum;
    slot = g_hash_table_lookup(prev_el, key);
    if (slot == NULL)
    {
        slot = g_new(gdouble, 1);
        *slot = el;
        g_hash_table_insert(prev_el, key, slot);
        *have_previous = FALSE;
        return;
    }

    g_free(key);
    *previous = *slot;
    *slot = el;
    *have_previous = TRUE;
}

static gboolean prune_sent(gpointer key, gpointer value, gpointer data)
{
    gdouble *when = value;
    gdouble now = *(gdouble *)data;

    (void)key;

    return *when < now - 2.0;
}

/* Return TRUE when this satellite/event/time has already been announced. */
static gboolean already_sent(gint catnum, const gchar *kind, gdouble stamp,
                             gdouble now)
{
    gchar *key;
    gdouble *when;

    if (sent == NULL)
        sent = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    if (g_hash_table_size(sent) > 256)
        g_hash_table_foreach_remove(sent, prune_sent, &now);

    key = g_strdup_printf("%d:%s:%.6f", catnum, kind, stamp);
    if (g_hash_table_contains(sent, key))
    {
        g_free(key);
        return TRUE;
    }

    when = g_new(gdouble, 1);
    *when = stamp;
    g_hash_table_insert(sent, key, when);
    return FALSE;
}

static void notify_done(GObject *source, GAsyncResult *result, gpointer data)
{
    GError *error = NULL;
    GVariant *reply;

    (void)data;

    reply = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), result, &error);
    if (reply != NULL)
    {
        g_variant_unref(reply);
        return;
    }

    if (!unavailable && error != NULL)
    {
        sat_log_log(SAT_LOG_LEVEL_WARN,
                    _("Desktop notifications are unavailable: %s"),
                    error->message);
        unavailable = TRUE;
    }
    g_clear_error(&error);
}

static gboolean ensure_proxy(void)
{
    GError *error = NULL;
    gchar *owner;

    if (unavailable)
        return FALSE;
    if (proxy != NULL)
        return TRUE;

    proxy = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION,
        G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
            G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
        NULL, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", NULL, &error);

    if (proxy == NULL)
    {
        sat_log_log(SAT_LOG_LEVEL_WARN,
                    _("Desktop notifications are unavailable: %s"),
                    error != NULL ? error->message : _("unknown error"));
        g_clear_error(&error);
        unavailable = TRUE;
        return FALSE;
    }

    owner = g_dbus_proxy_get_name_owner(proxy);
    if (owner == NULL)
    {
        sat_log_log(SAT_LOG_LEVEL_WARN,
                    _("Desktop notifications are unavailable: no notification "
                      "service on the session bus"));
        g_object_unref(proxy);
        proxy = NULL;
        unavailable = TRUE;
        return FALSE;
    }
    g_free(owner);

    return TRUE;
}

static void send_notice(gint catnum, const gchar *kind, gdouble stamp,
                        gdouble now, const gchar *summary, const gchar *body)
{
    GVariantBuilder actions;
    GVariantBuilder hints;

    if (already_sent(catnum, kind, stamp, now))
        return;
    if (!ensure_proxy())
        return;

    g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));

    g_dbus_proxy_call(proxy, "Notify",
                      g_variant_new("(susssasa{sv}i)", "gpredict", (guint32)0,
                                    "gpredict", summary, body, &actions, &hints,
                                    NOTIFY_EXPIRE_MS),
                      G_DBUS_CALL_FLAGS_NONE, -1, NULL, notify_done, NULL);
}

static gchar *countdown_text(gdouble seconds)
{
    gint total = (gint)seconds;
    gint minutes;
    gint secs;

    if (total < 0)
        total = 0;

    minutes = total / 60;
    secs = total % 60;

    return g_strdup_printf("%02d:%02d", minutes, secs);
}

void sat_notify_consider(GtkSatModule *module, sat_t *sat)
{
    gdouble previous = 0.0;
    gboolean have_previous = FALSE;
    gboolean realtime;
    gboolean selected_only;
    const gchar *label;
    gint lead;
    gdouble now;

    if (module == NULL || sat == NULL || decayed(sat))
        return;

    remember_el(sat->tle.catnr, sat->el, &previous, &have_previous);

    realtime = (module->throttle == 1);
    if (!realtime || !have_previous)
        return;
    if (!sat_cfg_get_bool(SAT_CFG_BOOL_NOTIFY_ENABLE))
        return;

    selected_only = !sat_cfg_get_bool(SAT_CFG_BOOL_NOTIFY_ALL);
    if (selected_only && module->target != sat->tle.catnr)
        return;

    label = sat->nickname != NULL ? sat->nickname : sat->name;
    if (label == NULL)
        label = _("satellite");

    now = module->tmgCdnum;
    lead = sat_cfg_get_int(SAT_CFG_INT_NOTIFY_LEAD);
    if (lead < 0)
        lead = 0;
    if (lead > 600)
        lead = 600;

    if (lead > 0 && sat->aos > now)
    {
        gdouble seconds = (sat->aos - now) * 86400.0;

        if (seconds <= lead)
        {
            gchar *left = countdown_text(seconds);
            gchar *summary = g_strdup_printf(_("AOS soon: %s"), label);
            gchar *body = g_strdup_printf(_("%s rises in %s."), label, left);

            send_notice(sat->tle.catnr, "warn", sat->aos, now, summary, body);
            g_free(body);
            g_free(summary);
            g_free(left);
        }
    }

    if (previous < 0.0 && sat->el >= 0.0)
    {
        gdouble stamp = (sat->aos > 0.0) ? sat->aos : now;
        gchar *summary = g_strdup_printf(_("AOS: %s"), label);
        gchar *body =
            g_strdup_printf(_("%s is above the horizon at %.1f\302\260."),
                            label, sat->az);

        send_notice(sat->tle.catnr, "aos", stamp, now, summary, body);
        g_free(body);
        g_free(summary);
    }

    if (previous >= 0.0 && sat->el < 0.0)
    {
        gdouble stamp = (sat->los > 0.0) ? sat->los : now;
        gchar *summary = g_strdup_printf(_("LOS: %s"), label);
        gchar *body = g_strdup_printf(_("%s has set."), label);

        send_notice(sat->tle.catnr, "los", stamp, now, summary, body);
        g_free(body);
        g_free(summary);
    }
}
