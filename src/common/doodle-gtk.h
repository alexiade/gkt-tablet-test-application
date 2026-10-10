/*
 * gtk-tablet-doodle: widgets shared by both front ends. doodle-gtk.c is
 * compiled once against GTK 3 and once against GTK 4.
 *
 * Copyright 2026 gtk-tablet-doodle contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef DOODLE_GTK_H
#define DOODLE_GTK_H

#include <gtk/gtk.h>
#include "doodle.h"

#if GTK_CHECK_VERSION(4, 0, 0)
#define box_add(box, w)      gtk_box_append(GTK_BOX(box), (w))
#define check_active(w)      gtk_check_button_get_active(GTK_CHECK_BUTTON(w))
#else
#define box_add(box, w)      gtk_box_pack_start(GTK_BOX(box), (w), FALSE, FALSE, 0)
#define check_active(w)      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))
#endif

/* implemented by each front end */
char *frontend_describe(void);       /* devices/tools as text; g_free */
void frontend_clear(void);           /* reset counters along with the canvas */

GtkWidget *scrolled_new(GtkWidget *child);
GtkWidget *frame_new(const char *label, GtkWidget *child);
GtkWidget *label_new(gboolean monospace);

GtkWidget *doodle_status_label(void);   /* refreshes itself */
GtkWidget *doodle_emu_panel(void);
GtkWidget *doodle_log_panel(void);
void doodle_log(const char *fmt, ...) G_GNUC_PRINTF(1, 2);

char *doodle_report(void);              /* status + devices; g_free */
/* starts the emulator, handles --smoke and --script, runs until the window closes */
void doodle_main(GtkWidget *window, DoodleRectFunc canvas_rect);

#endif
