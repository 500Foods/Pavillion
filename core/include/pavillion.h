#ifndef PAVILLION_H
#define PAVILLION_H

#include <stddef.h>

/* Incompatible-change epoch. pavillion_api_version() returns this number.
   build.rs copies it into the shell. Adding a command or an optional field
   does not bump it. The line is a bare decimal integer so the shell and the
   C compiler cannot disagree about its value. */
#define PAVILLION_API_VERSION 1

typedef struct PavillionStatus {
    char profile_name[256];
    char profile_id[64];
    char state[32]; /* "ready" or "error" */
    char error[512];
} PavillionStatus;

/* Layout, zoom, and the OS window. Doubles are first so the Rust shell can
   match this layout with repr(C). zoom_shell is the master scale; every pane
   scale below it multiplies on top of it. */
typedef struct PavShellPrefs {
    double zoom_shell;
    double zoom_nav_buttons;
    double zoom_nav_body;
    double zoom_mail_list;
    double zoom_mail_detail;
    double mail_split;
    double mail_min;
    double mail_max;
    double contacts_split;
    double contacts_min;
    double contacts_max;
    double files_split;
    double files_min;
    double files_max;
    int window_maximized;
    int window_width;
    int window_height;
    int window_x_set;
    int window_y_set;
    int window_x;
    int window_y;
    int nav_width;
    int nav_min;
    int nav_max;
    char chrome[16];
} PavShellPrefs;

/* Bits for pavillion_prefs_apply. A field outside the mask is left as stored. */
#define PAV_PREFS_ZOOM   1u
#define PAV_PREFS_WINDOW 2u
#define PAV_PREFS_UI     4u

/* Returns PAVILLION_API_VERSION. Safe before pavillion_start. The shell
   calls this symbol; it does not substitute the macro. */
int pavillion_api_version(void);

/* Starts the core thread and returns after it has published state "ready"
   or "error". Returns 0, or -1 when the core is already running, the path
   does not fit, or the thread cannot be created. */
int pavillion_start(const char *workdir, char *err, size_t err_len);

/* Pushes stop and joins the core thread. A second call does nothing. */
void pavillion_stop(void);

/* Copies the status under a lock. Returns 0, or -1 when out is NULL. */
int pavillion_status(PavillionStatus *out);

/* Copies the shell preferences. Returns 0, or -1 when out is NULL or the
   profile is not ready. */
int pavillion_prefs_get(PavShellPrefs *out);

/* Asks the core thread to store the masked fields and waits until that write
   finishes. Returns 0, or -1. On failure err receives a short reason. */
int pavillion_prefs_apply(const PavShellPrefs *in, unsigned mask, char *err, size_t err_len);

/* Reads preferences.ui.icons as a JSON string (caller frees). Returns 0, or -1. */
int pavillion_icons_get(char **out, char *err, size_t err_len);

/* Writes preferences.ui.icons from a JSON object string and persists. Returns 0, or -1. */
int pavillion_icons_apply(const char *json, char *err, size_t err_len);

/* Writes preferences.ui.fontawesome_kit from `kit` and persists. Returns 0, or -1. */
int pavillion_kit_apply(const char *kit, char *err, size_t err_len);

/* Reads preferences.ui.fontawesome_kit into out. Returns 0, or -1. */
int pavillion_kit_get(char *out, size_t out_len, char *err, size_t err_len);

/* Test-only. 1 while the core thread is inside its loop, otherwise 0. */
int pav_core_thread_alive(void);

/* A line numerically above the current level is dropped. Default level is info. */
#define PAV_ERROR 0
#define PAV_WARN  1
#define PAV_INFO  2
#define PAV_DEBUG 3

/* num_args is the number of format specifiers in fmt. A mismatch is logged
   at error and fmt is not formatted. */
void pav_log(const char *subsystem, int level, int num_args, const char *fmt, ...);

/* Logs a frontend (shell) message into the core log. subsystem is a short
   tag like "ui". level is one of PAV_ERROR/PAV_WARN/PAV_INFO/PAV_DEBUG.
   message is logged verbatim. Returns 0, or -1. */
int pav_ui_log(const char *subsystem, int level, const char *message);

/* Test-only. Truncates path and writes later lines there. NULL closes that
   file. Returns 0, or -1 when path cannot be opened. */
int pav_log_set_output(const char *path);

/* The log thread writes queued lines. A second start while it runs returns -1.
   Shutdown drains the queue and joins. A second shutdown does nothing. */
int pav_log_thread_start(void);
void pav_log_thread_shutdown(void);

#endif
