#ifndef MOD_MGR_H
#define MOD_MGR_H 1

GtkWidget      *mod_mgr_create(void);
gint            mod_mgr_add_module(GtkWidget * module, gboolean dock);
gint            mod_mgr_remove_module(GtkWidget * module);
void            mod_mgr_save_state(void);
gboolean        mod_mgr_mod_is_visible(GtkWidget * module);
gint            mod_mgr_dock_module(GtkWidget * module);
gint            mod_mgr_undock_module(GtkWidget * module);
void            mod_mgr_reload_sats(void);
GtkWidget      *mod_mgr_get_module(const gchar * name);
void            mod_mgr_raise_module(GtkWidget * module);

#endif
