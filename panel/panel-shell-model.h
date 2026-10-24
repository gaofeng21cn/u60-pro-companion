#ifndef PANEL_SHELL_MODEL_H
#define PANEL_SHELL_MODEL_H
#define SHELL_MAX_FIELDS 24
#define SHELL_VALUE_CAP 256
#define SHELL_SEARCH_CAP 64
struct panel_shell {
 cJSON *snapshot; /* root owns; UI only reads. */
 int tab, subpage, item_page, choice_page, field_page, menu_page;
 cJSON *report_lines; /* transient text only; never persisted */
 int report_page; /* page fields now store pixel offsets, not page numbers */
 int *scroll_offset, scroll_max, scroll_top, scroll_bottom;
 int drag_active, drag_moved, drag_x, drag_y, drag_last_y, drag_hit, drag_scroll;
 int menu_offsets[5];
 int busy;
 long status_until; /* worker activity; root updates, UI displays and blocks duplicate submits */
 char status[160]; /* optional root operation result/status */
 char section[32];
 int modal, editor, field, key_page, shift, reveal, power_open;
 int locked; /* Visual lock only; it does not authenticate or restrict device access. */
 cJSON *draft; /* owned deep copy; never replaced by background refresh */
 cJSON *pending_args;
 int nfields;
 char choice_search[SHELL_SEARCH_CAP], search_edit[SHELL_SEARCH_CAP];
 int search_saved_page; /* keyboard cancel restores the existing result page */
 char values[SHELL_MAX_FIELDS][SHELL_VALUE_CAP];
 char message[256];
};
struct app;
static void panel_screen_lock_initial(struct panel_shell *s,int supported){
 s->locked=!!supported;
}
static void panel_screen_lock_wake(struct panel_shell *s,int was_blanked,int supported){
 if(was_blanked&&supported)s->locked=1;
}
static void panel_screen_lock_unlock(struct panel_shell *s){
 s->locked=0;
}
/* Dispatch borrows args; copy synchronously before asynchronous execution. */
static void shell_dispatch(struct app *,const char *,cJSON *);
static void shell_request_refresh(struct app *);
static void shell_factory(struct app *);
static void shell_power(struct app *,int);
#endif
