/*
 * gtk-tablet-doodle: widgets shared by both front ends (status line,
 * emulator controls, event log). Compiled once per GTK version.
 *
 * Copyright 2026 gtk-tablet-doodle contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdarg.h>
#include <stdio.h>

#include "doodle-gtk.h"

#if GTK_CHECK_VERSION(4, 0, 0)
#define GTK_DLL "libgtk-4-1.dll"   /* GTK 4's Wintab code lives in libgtk */
#define check_set(w, v) gtk_check_button_set_active(GTK_CHECK_BUTTON(w), (v))
#else
#define GTK_DLL "libgdk-3-0.dll"
#define check_set(w, v) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), (v))
#endif

GtkWidget *scrolled_new(GtkWidget *child)
{
#if GTK_CHECK_VERSION(4, 0, 0)
    GtkWidget *s = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(s), child);
#else
    GtkWidget *s = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(s), child);
#endif
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    return s;
}

GtkWidget *frame_new(const char *label, GtkWidget *child)
{
    GtkWidget *f = gtk_frame_new(label);
#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_frame_set_child(GTK_FRAME(f), child);
#else
    gtk_container_add(GTK_CONTAINER(f), child);
#endif
    return f;
}

GtkWidget *label_new(gboolean monospace)
{
    GtkWidget *l = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_selectable(GTK_LABEL(l), TRUE);
    if (monospace)
    {
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_family_new("monospace"));
        gtk_label_set_attributes(GTK_LABEL(l), attrs);
        pango_attr_list_unref(attrs);
    }
    return l;
}

/* ---- status line -------------------------------------------------------- */

static char *status_text(void)
{
    char *s = doodle_status(), *gtk = doodle_module_path(GTK_DLL);
    char *text = g_strdup_printf("GTK %u.%u.%u: %s\n%s", gtk_get_major_version(), gtk_get_minor_version(),
                                 gtk_get_micro_version(), gtk ? gtk : GTK_DLL " not found", s);
    g_free(s);
    g_free(gtk);
    return text;
}

static gboolean status_tick(gpointer label)
{
    char *text = status_text();
    /* Wintab is loaded lazily, so this changes shortly after startup */
    if (g_strcmp0(text, gtk_label_get_text(label))) gtk_label_set_text(label, text);
    g_free(text);
    return G_SOURCE_CONTINUE;
}

GtkWidget *doodle_status_label(void)
{
    GtkWidget *l = label_new(FALSE);
#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
#else
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
#endif
    status_tick(l);
    g_timeout_add(1000, status_tick, l);
    return l;
}

char *doodle_report(void)
{
    char *status = status_text(), *devices = frontend_describe();
    char *r = g_strdup_printf("%s\n\n%s", status, devices);
    g_free(status);
    g_free(devices);
    return r;
}

/* ---- emulator controls ------------------------------------------------ */

static GtkWidget *demo_button, *emu_label;

static void scale_changed(GtkRange *r, gpointer target) { *(double *)target = gtk_range_get_value(r); }
static void check_toggled(GtkWidget *w, gpointer target) { *(gboolean *)target = check_active(w); }

static GtkWidget *check_new(const char *label, gboolean *target)
{
    GtkWidget *c = gtk_check_button_new_with_label(label);
    check_set(c, *target);
    g_signal_connect(c, "toggled", G_CALLBACK(check_toggled), target);
    return c;
}

static void add_scale(GtkWidget *grid, int row, const char *label, double min, double max,
                      double step, double *target)
{
    GtkWidget *l = gtk_label_new(label), *s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max, step);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_range_set_value(GTK_RANGE(s), *target);
    gtk_scale_set_value_pos(GTK_SCALE(s), GTK_POS_RIGHT);
    gtk_widget_set_hexpand(s, TRUE);
    g_signal_connect(s, "value-changed", G_CALLBACK(scale_changed), target);
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), s, 1, row, 1, 1);
}

#if GTK_CHECK_VERSION(4, 0, 0)
static void tool_selected(GObject *dd, GParamSpec *ps, gpointer data)
{
    emu.tool = gtk_drop_down_get_selected(GTK_DROP_DOWN(dd));
}
#else
static void tool_selected(GtkComboBox *combo, gpointer data)
{
    emu.tool = gtk_combo_box_get_active(combo);
}
#endif

static gboolean run_demo(gpointer data)
{
    gtk_widget_set_sensitive(demo_button, FALSE);
    demo_run();
    gtk_widget_set_sensitive(demo_button, TRUE);
    return G_SOURCE_REMOVE;
}

/* from an idle, so the click that started it is over */
static void demo_clicked(GtkWidget *button, gpointer data)
{
    if (!demo_running()) g_idle_add(run_demo, NULL);
}

static void clear_clicked(GtkWidget *button, gpointer data)
{
    canvas_clear();
    frontend_clear();
}

static gboolean emu_label_tick(gpointer data)
{
    gtk_label_set_text(GTK_LABEL(emu_label), emu_describe());
    return G_SOURCE_CONTINUE;
}

GtkWidget *doodle_emu_panel(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *grid = gtk_grid_new(), *row, *tools, *clear;

    emu.enabled = emu_available() && !opt.smoke;
    box_add(controls, check_new("Emulate a pen over the canvas (hold Shift to lift it)", &emu.enabled));
#if GTK_CHECK_VERSION(4, 0, 0)
    tools = gtk_drop_down_new_from_strings(emu_tool_labels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(tools), emu.tool);
    g_signal_connect(tools, "notify::selected", G_CALLBACK(tool_selected), NULL);
#else
    tools = gtk_combo_box_text_new();
    for (int i = 0; emu_tool_labels[i]; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(tools), emu_tool_labels[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(tools), emu.tool);
    g_signal_connect(tools, "changed", G_CALLBACK(tool_selected), NULL);
#endif
    box_add(controls, tools);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    add_scale(grid, 0, "Pressure", 0, 1, 0.01, &emu.pressure);
    add_scale(grid, 1, "Rotation °", 0, 359, 1, &emu.rotation);
    add_scale(grid, 2, "Tilt X °", -60, 60, 1, &emu.tilt_x);
    add_scale(grid, 3, "Tilt Y °", -60, 60, 1, &emu.tilt_y);
    add_scale(grid, 4, "Wheel", 0, 1, 0.01, &emu.wheel);
    box_add(controls, grid);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    box_add(row, check_new("Pressure ramp", &emu.ramp));
    box_add(row, check_new("Spin the barrel", &emu.spin));
    box_add(controls, row);
    gtk_widget_set_sensitive(controls, emu_available());
    box_add(box, controls);

    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    demo_button = gtk_button_new_with_label("Play demo (moves the mouse)");
    gtk_widget_set_sensitive(demo_button, emu_available());
    g_signal_connect(demo_button, "clicked", G_CALLBACK(demo_clicked), NULL);
    clear = gtk_button_new_with_label("Clear canvas");
    g_signal_connect(clear, "clicked", G_CALLBACK(clear_clicked), NULL);
    box_add(row, demo_button);
    box_add(row, clear);
    box_add(box, row);
    emu_label = label_new(FALSE);
    box_add(box, emu_label);
    emu_label_tick(NULL);
    g_timeout_add(100, emu_label_tick, NULL);
    return frame_new("Emulated tablet", box);
}

/* ---- event log -------------------------------------------------------- */

static GtkWidget *log_view;
static GtkTextBuffer *log_buffer;
static GtkTextMark *log_end;
static gboolean log_paused;

void doodle_log(const char *fmt, ...)
{
    GtkTextIter start, end;
    va_list ap;
    char *line;

    va_start(ap, fmt);
    line = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    if (opt.log_stdout) puts(line);
    if (log_buffer && !log_paused)
    {
        gtk_text_buffer_get_end_iter(log_buffer, &end);
        gtk_text_buffer_insert(log_buffer, &end, line, -1);
        gtk_text_buffer_insert(log_buffer, &end, "\n", 1);
        if (gtk_text_buffer_get_line_count(log_buffer) > 5000)
        {
            gtk_text_buffer_get_start_iter(log_buffer, &start);
            gtk_text_buffer_get_iter_at_line(log_buffer, &end, 1000);
            gtk_text_buffer_delete(log_buffer, &start, &end);
        }
        gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(log_view), log_end);
    }
    g_free(line);
}

static void log_clear(GtkWidget *button, gpointer data)
{
    gtk_text_buffer_set_text(log_buffer, "", 0);
}

/* status, devices and the log, for pasting into a bug report */
static void log_copy(GtkWidget *button, gpointer data)
{
    GtkTextIter start, end;
    char *report = doodle_report(), *log, *text;

    gtk_text_buffer_get_bounds(log_buffer, &start, &end);
    log = gtk_text_buffer_get_text(log_buffer, &start, &end, FALSE);
    text = g_strdup_printf("%s\n\nEvent log:\n%s", report, log);
#if GTK_CHECK_VERSION(4, 0, 0)
    gdk_clipboard_set_text(gtk_widget_get_clipboard(log_view), text);
#else
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
#endif
    g_free(report);
    g_free(log);
    g_free(text);
}

GtkWidget *doodle_log_panel(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), *b, *scroll;
    GtkTextIter end;

    box_add(row, check_new("Pause", &log_paused));
    b = gtk_button_new_with_label("Clear log");
    g_signal_connect(b, "clicked", G_CALLBACK(log_clear), NULL);
    box_add(row, b);
    b = gtk_button_new_with_label("Copy report");
    gtk_widget_set_tooltip_text(b, "Copy status, devices and the event log to the clipboard");
    g_signal_connect(b, "clicked", G_CALLBACK(log_copy), NULL);
    box_add(row, b);
    box_add(box, row);

    log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view), TRUE);
    log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    gtk_text_buffer_get_end_iter(log_buffer, &end);
    log_end = gtk_text_buffer_create_mark(log_buffer, NULL, &end, FALSE);
    scroll = scrolled_new(log_view);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_size_request(scroll, -1, 160);
    box_add(box, scroll);
    gtk_widget_set_vexpand(box, TRUE);
    return frame_new("Event log", box);
}

/* ---- main loop ---------------------------------------------------------- */

static GMainLoop *loop;
static DoodleRectFunc rect_fn;

static void note(const char *text) { doodle_log("---- %s", text); }
static void on_destroy(GtkWidget *w, gpointer data) { g_main_loop_quit(loop); }

static gboolean smoke_done(gpointer data)
{
    char *report = doodle_report();
    DoodleScreenRect r;

    printf("%s\n", report);
    if (rect_fn(&r))
        printf("Canvas on screen: %d,%d %dx%d, scale %d\n", r.x, r.y, r.w, r.h, r.scale);
    g_free(report);
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static gboolean smoke_announce(gpointer data)
{
    emu_announce_tools();
    return G_SOURCE_REMOVE;
}

static gboolean script_start(gpointer data)
{
    demo_clicked(NULL, NULL);
    return G_SOURCE_REMOVE;
}

void doodle_main(GtkWidget *window, DoodleRectFunc canvas_rect)
{
    loop = g_main_loop_new(NULL, FALSE);
    rect_fn = canvas_rect;
    doodle_note = note;
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);
    emu_start(canvas_rect);
    if (opt.smoke)
    {
        g_timeout_add(300, smoke_announce, NULL);
        g_timeout_add(1200, smoke_done, NULL);
    }
    if (opt.script) g_timeout_add(1500, script_start, NULL);
    g_main_loop_run(loop);
}
