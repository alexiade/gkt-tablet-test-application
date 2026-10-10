/*
 * gtk-tablet-doodle, GTK 4 front end.
 *
 * In GTK 4 every pointer event comes from the logical pointer; what makes
 * a pen a pen is the event's GdkDeviceTool (gdk_event_get_device_tool):
 * its type, serial and hardware id identify the physical stylus, and its
 * axis flags say which axes are real. Drawing applications typically use
 * a GtkGestureStylus, which only reacts to events that carry a tool.
 *
 * GTK 4 uses Windows Ink unless GDK_WIN32_TABLET_INPUT_API=wintab, which
 * this app sets by default (see --api).
 *
 * Copyright 2026 gtk-tablet-doodle contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <string.h>

#include "doodle-gtk.h"
#include <gdk/win32/gdkwin32.h>

#define MOUSE_ROW ((gpointer)1)         /* list row for events without a tool */

static GtkWidget *canvas, *device_list, *tool_list, *readout, *counters;
static GdkSeat *seat;
static GdkDeviceTool *current;          /* tool of the last pointer event; NULL: mouse */
static GPtrArray *seen;                 /* tools seen in events */
static guint rebuild_id;

static struct
{
    int motion, press, release, crossing, proximity;
    int pen_down, mouse_down;           /* motions with button 1 held */
    int switches;                       /* tool changed within a stroke */
    int no_press;                       /* button 1 held but no press was seen */
    int no_pressure;                    /* tool motions with button 1 but pressure 0 or none */
    int stylus_down, stylus_up, stylus_motion;   /* GtkGestureStylus */
} count;

static DoodleSample last;
static gboolean stroke;
static GdkDeviceTool *stroke_tool;
static double pointer_x, pointer_y;     /* proximity events carry no position */

static const char *const axis_names[] = { "ignore", "x", "y", "delta-x", "delta-y", "pressure",
                                          "xtilt", "ytilt", "wheel", "distance", "rotation", "slider" };

static const char *tool_type_name(GdkDeviceToolType t)
{
    static const char *const n[] = { "unknown", "pen", "eraser", "brush", "pencil", "airbrush", "mouse", "lens" };
    return (unsigned)t < G_N_ELEMENTS(n) ? n[t] : "?";
}

static const char *source_name(GdkInputSource s)
{
    static const char *const n[] = { "mouse", "pen", "keyboard", "touchscreen", "touchpad",
                                     "trackpoint", "tablet-pad" };
    return (unsigned)s < G_N_ELEMENTS(n) ? n[s] : "?";
}

/* "pen #0000A001 hw 823"; stable, so it also keys the tool's colour */
static const char *tool_name(GdkDeviceTool *t)
{
    static char buf[4][64];
    static int n;
    char *b = buf[n++ & 3];
    if (!t) return "(no tool: mouse)";
    snprintf(b, 64, "%s #%08llX hw %03llX", tool_type_name(gdk_device_tool_get_tool_type(t)),
             (unsigned long long)gdk_device_tool_get_serial(t),
             (unsigned long long)gdk_device_tool_get_hardware_id(t));
    return b;
}

static char *axis_flags(GdkAxisFlags flags)
{
    GString *s = g_string_new(NULL);
    unsigned i;
    for (i = 1; i < G_N_ELEMENTS(axis_names); i++)
        if (flags & (1u << i)) g_string_append_printf(s, "%s%s", s->len ? " " : "", axis_names[i]);
    return g_string_free(s, FALSE);
}

static int tool_kind(GdkDeviceTool *t)
{
    if (!t) return DOODLE_MOUSE;
    return gdk_device_tool_get_tool_type(t) == GDK_DEVICE_TOOL_TYPE_ERASER ? DOODLE_ERASER : DOODLE_PEN;
}

static const char *kind_hex(GdkDeviceTool *t)
{
    int kind = tool_kind(t);
    return kind == DOODLE_MOUSE ? "#8c8c8c" : kind == DOODLE_ERASER ? "#ff9ecc" : doodle_color_hex(tool_name(t));
}

/* ---- device and tool lists ---------------------------------------------- */

static GList *seat_devices(void)
{
    GList *l = gdk_seat_get_devices(seat, GDK_SEAT_CAPABILITY_ALL);
    return g_list_prepend(l, gdk_seat_get_pointer(seat));
}

/* the seat's tools, then tools only seen in events */
static GPtrArray *collect_tools(void)
{
    GPtrArray *a = g_ptr_array_new();
    GList *tools = gdk_seat_get_tools(seat), *l;
    guint i;
    for (l = tools; l; l = l->next) g_ptr_array_add(a, l->data);
    for (i = 0; i < seen->len; i++)
        if (!g_list_find(tools, seen->pdata[i])) g_ptr_array_add(a, seen->pdata[i]);
    g_list_free(tools);
    return a;
}

static void select_current(void)
{
    GtkListBoxRow *row;
    int i;
    for (i = 0; (row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(tool_list), i)); i++)
        if (g_object_get_data(G_OBJECT(row), "tool") == (current ? (gpointer)current : MOUSE_ROW))
            gtk_list_box_select_row(GTK_LIST_BOX(tool_list), row);
}

static void add_row(GtkWidget *list, const char *markup, const char *key, gpointer data)
{
    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_list_box_append(GTK_LIST_BOX(list), label);
    if (key) g_object_set_data(G_OBJECT(gtk_widget_get_parent(label)), key, data);
}

static gboolean rebuild_lists(gpointer data)
{
    GList *devices = seat_devices(), *seat_tools = gdk_seat_get_tools(seat), *l;
    GPtrArray *tools = collect_tools();
    guint i;

    rebuild_id = 0;
    gtk_list_box_remove_all(GTK_LIST_BOX(device_list));
    gtk_list_box_remove_all(GTK_LIST_BOX(tool_list));
    for (l = devices; l; l = l->next)
    {
        GdkDevice *d = l->data;
        char *markup;
        if (gdk_device_get_source(d) == GDK_SOURCE_KEYBOARD) continue;
        markup = g_markup_printf_escaped("<b>%s</b>\n<small>%s%s%s, tool now: %s</small>",
                                         gdk_device_get_name(d), source_name(gdk_device_get_source(d)),
                                         d == gdk_seat_get_pointer(seat) ? ", logical pointer" : "",
                                         gdk_device_get_has_cursor(d) ? ", has cursor" : "",
                                         tool_name(gdk_device_get_device_tool(d)));
        add_row(device_list, markup, NULL, NULL);
        g_free(markup);
    }
    add_row(tool_list, "<span foreground=\"#8c8c8c\">●</span> <b>no tool</b> (mouse)", "tool", MOUSE_ROW);
    for (i = 0; i < tools->len; i++)
    {
        GdkDeviceTool *t = tools->pdata[i];
        char *axes = axis_flags(gdk_device_tool_get_axes(t));
        char *markup = g_markup_printf_escaped("<span foreground=\"%s\">●</span> <b>%s</b>%s\n<small>axes: %s</small>",
                                               kind_hex(t), tool_name(t),
                                               g_list_find(seat_tools, t) ? "" : " (only seen in events)",
                                               *axes ? axes : "(none)");
        add_row(tool_list, markup, "tool", t);
        g_free(markup);
        g_free(axes);
    }
    g_list_free(devices);
    g_list_free(seat_tools);
    g_ptr_array_free(tools, TRUE);
    select_current();
    return G_SOURCE_REMOVE;
}

static void queue_rebuild(void)
{
    if (!rebuild_id) rebuild_id = g_idle_add(rebuild_lists, NULL);
}

static void device_changed(GdkSeat *s, GdkDevice *d, gpointer what)
{
    doodle_log("---- device %s: %s", (const char *)what, gdk_device_get_name(d));
    queue_rebuild();
}

static void tool_changed(GdkSeat *s, GdkDeviceTool *t, gpointer what)
{
    doodle_log("---- tool %s: %s", (const char *)what, tool_name(t));
    queue_rebuild();
}

char *frontend_describe(void)
{
    GString *s = g_string_new("GTK 4 seat devices:\n");
    GList *devices = seat_devices(), *l;
    GPtrArray *tools = collect_tools();
    guint i;

    for (l = devices; l; l = l->next)
        g_string_append_printf(s, "  %-44s %-8s%s\n", gdk_device_get_name(l->data),
                               source_name(gdk_device_get_source(l->data)),
                               l->data == gdk_seat_get_pointer(seat) ? " (logical pointer)" : "");
    g_string_append(s, "GTK 4 tools (seat, seen in events):\n");
    for (i = 0; i < tools->len; i++)
    {
        char *axes = axis_flags(gdk_device_tool_get_axes(tools->pdata[i]));
        g_string_append_printf(s, "  %-28s axes: %s%s\n", tool_name(tools->pdata[i]), axes,
                               tools->pdata[i] == current ? "  <- current" : "");
        g_free(axes);
    }
    if (!tools->len) g_string_append(s, "  (none)\n");
    g_list_free(devices);
    g_ptr_array_free(tools, TRUE);
    return g_string_free(s, FALSE);
}

/* ---- events ------------------------------------------------------------- */

void frontend_clear(void)
{
    memset(&count, 0, sizeof(count));
    gtk_label_set_text(GTK_LABEL(counters), "");
}

static const char *event_name(GdkEventType t)
{
    switch (t)
    {
    case GDK_MOTION_NOTIFY: return "motion";
    case GDK_BUTTON_PRESS: return "press";
    case GDK_BUTTON_RELEASE: return "release";
    case GDK_ENTER_NOTIFY: return "enter";
    case GDK_LEAVE_NOTIFY: return "leave";
    case GDK_PROXIMITY_IN: return "prox-in";
    case GDK_PROXIMITY_OUT: return "prox-out";
    default: return NULL;
    }
}

/* an axis is there if the event has it and the tool (if any) claims it */
static gboolean axis(GdkEvent *ev, GdkDeviceTool *tool, GdkAxisUse use, double *v)
{
    return gdk_event_get_axis(ev, use, v) && (!tool || (gdk_device_tool_get_axes(tool) & (1u << use)));
}

/* legacy controllers see surface coordinates */
static void canvas_position(GdkEvent *ev, double *x, double *y)
{
    GtkNative *native = gtk_widget_get_native(canvas);
    graphene_point_t p;
    double ex, ey, sx, sy;

    *x = pointer_x;
    *y = pointer_y;
    if (!gdk_event_get_position(ev, &ex, &ey)) return;
    gtk_native_get_surface_transform(native, &sx, &sy);
    if (gtk_widget_compute_point(GTK_WIDGET(native), canvas, &GRAPHENE_POINT_INIT(ex - sx, ey - sy), &p))
    {
        *x = p.x;
        *y = p.y;
    }
}

static void read_sample(GdkEvent *ev, GdkDeviceTool *tool, DoodleSample *s)
{
    double v, ty;

    memset(s, 0, sizeof(*s));
    canvas_position(ev, &s->x, &s->y);
    s->kind = tool_kind(tool);
    doodle_color(tool_name(tool), s->rgb);
    s->pressure = axis(ev, tool, GDK_AXIS_PRESSURE, &v) ? v : -1;
    s->has_tilt = axis(ev, tool, GDK_AXIS_XTILT, &v) && axis(ev, tool, GDK_AXIS_YTILT, &ty);
    if (s->has_tilt) { s->tilt_x = v; s->tilt_y = ty; }
    s->rotation = axis(ev, tool, GDK_AXIS_ROTATION, &v) ? v * 360 : -1;
    /* the airbrush wheel is GDK_AXIS_SLIDER on Wayland; accept either */
    s->wheel = axis(ev, tool, GDK_AXIS_SLIDER, &v) || axis(ev, tool, GDK_AXIS_WHEEL, &v) ? v : -1;
}

static void show_state(const char *name, GdkEvent *ev, GdkDeviceTool *tool, const DoodleSample *s,
                       GdkModifierType state)
{
    GString *axes = g_string_new(NULL);
    char *flags = tool ? axis_flags(gdk_device_tool_get_axes(tool)) : g_strdup("-");
    char *text;
    double v;
    unsigned i;

    for (i = GDK_AXIS_PRESSURE; i < G_N_ELEMENTS(axis_names); i++)
        if (axis(ev, tool, i, &v)) g_string_append_printf(axes, " %s %.3f", axis_names[i], v);
    text = g_strdup_printf("event    %s%s\ntool     %s\n         axes: %s\ndevice   %s\n"
                           "position %.1f, %.1f\naxes    %s",
                           name, state & GDK_BUTTON1_MASK ? "  [button 1 held]" : "", tool_name(tool), flags,
                           gdk_device_get_name(gdk_event_get_device(ev)), s->x, s->y,
                           axes->len ? axes->str : " (none)");
    gtk_label_set_text(GTK_LABEL(readout), text);
    g_free(text);
    g_free(flags);
    g_string_free(axes, TRUE);

    text = g_strdup_printf("motion %d  press %d  release %d  crossing %d  proximity %d\n"
                           "button 1 held: %d tool and %d mouse motions\n"
                           "tool changed mid-stroke %d, held without press %d, pen without pressure %d\n"
                           "GtkGestureStylus: down %d, up %d, motion %d",
                           count.motion, count.press, count.release, count.crossing, count.proximity,
                           count.pen_down, count.mouse_down, count.switches, count.no_press, count.no_pressure,
                           count.stylus_down, count.stylus_up, count.stylus_motion);
    gtk_label_set_text(GTK_LABEL(counters), text);
    g_free(text);
}

/* GtkEventControllerLegacy: every event the canvas gets */
static gboolean on_event(GtkEventControllerLegacy *controller, GdkEvent *ev, gpointer data)
{
    GdkEventType type = gdk_event_get_event_type(ev);
    const char *name = event_name(type);
    GdkDeviceTool *tool;
    GdkModifierType state;
    guint button = 0;
    char axes[128], btn[16] = "";
    DoodleSample s;

    if (!name) return FALSE;
    tool = gdk_event_get_device_tool(ev);
    state = gdk_event_get_modifier_state(ev);
    if (type == GDK_BUTTON_PRESS || type == GDK_BUTTON_RELEASE)
    {
        button = gdk_button_event_get_button(ev);
        snprintf(btn, sizeof(btn), " button %u", button);
    }
    read_sample(ev, tool, &s);
    pointer_x = s.x;
    pointer_y = s.y;
    doodle_axes_text(axes, sizeof(axes), &s);
    doodle_log("%8.3f %-8s %-28s via %-16s%s%s%s", doodle_now(), name, tool_name(tool),
               gdk_device_get_name(gdk_event_get_device(ev)), btn, axes, state & GDK_BUTTON1_MASK ? " [B1]" : "");

    if (tool && !g_ptr_array_find(seen, tool, NULL))
    {
        g_ptr_array_add(seen, g_object_ref(tool));
        queue_rebuild();
    }
    if (tool != current)
    {
        current = tool;
        select_current();
    }

    switch (type)
    {
    case GDK_MOTION_NOTIFY: count.motion++; break;
    case GDK_BUTTON_PRESS: count.press++; break;
    case GDK_BUTTON_RELEASE: count.release++; break;
    case GDK_PROXIMITY_IN: case GDK_PROXIMITY_OUT: count.proximity++; break;
    default: count.crossing++; break;
    }

    if (type == GDK_PROXIMITY_IN || type == GDK_PROXIMITY_OUT)
        canvas_mark(s.x, s.y, type == GDK_PROXIMITY_IN);
    else if (type == GDK_BUTTON_PRESS && button == 1)
    {
        stroke = TRUE;
        last = s;
        stroke_tool = tool;
    }
    else if ((type == GDK_MOTION_NOTIFY && (state & GDK_BUTTON1_MASK)) ||
             (type == GDK_BUTTON_RELEASE && button == 1))
    {
        if (!stroke)
        {
            count.no_press++;
            last = s;
            stroke = TRUE;
            stroke_tool = tool;
        }
        if (tool != stroke_tool)
        {
            count.switches++;
            stroke_tool = tool;
        }
        if (type == GDK_MOTION_NOTIFY)
        {
            if (tool) count.pen_down++;
            else count.mouse_down++;
            if (tool && s.pressure <= 0) count.no_pressure++;
        }
        canvas_segment(&last, &s);
        last = s;
        if (type == GDK_BUTTON_RELEASE) stroke = FALSE;
    }
    show_state(name, ev, tool, &s, state);
    return FALSE;
}

/* what a GTK 4 drawing application built on GtkGestureStylus would see */
static void on_stylus(GtkGestureStylus *g, double x, double y, gpointer what)
{
    double p = -1;

    if (!strcmp(what, "motion")) { count.stylus_motion++; return; }
    gtk_gesture_stylus_get_axis(g, GDK_AXIS_PRESSURE, &p);
    if (!strcmp(what, "down")) count.stylus_down++;
    else count.stylus_up++;
    doodle_log("%8.3f stylus %-4s %-28s p=%.3f  (GtkGestureStylus)", doodle_now(), (const char *)what,
               tool_name(gtk_gesture_stylus_get_device_tool(g)), p);
}

/* ---- canvas widget ------------------------------------------------------ */

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    canvas_paint(cr);
}

static void on_resize(GtkDrawingArea *area, int width, int height, gpointer data)
{
    canvas_set_size(width, height, gtk_widget_get_scale_factor(GTK_WIDGET(area)));
}

static void queue_canvas_draw(void) { gtk_widget_queue_draw(canvas); }

/* the canvas in physical screen pixels, for the emulator */
static gboolean canvas_rect(DoodleScreenRect *r)
{
    GtkNative *native = gtk_widget_get_native(canvas);
    GdkSurface *surface = native ? gtk_native_get_surface(native) : NULL;
    graphene_point_t p;
    POINT o = { 0, 0 };
    double sx, sy;

    if (!surface || !gtk_widget_get_mapped(canvas) ||
        !gtk_widget_compute_point(canvas, GTK_WIDGET(native), &GRAPHENE_POINT_INIT(0, 0), &p))
        return FALSE;
    gtk_native_get_surface_transform(native, &sx, &sy);
    ClientToScreen(gdk_win32_surface_get_handle(surface), &o);
    r->scale = gdk_surface_get_scale_factor(surface);
    r->x = o.x + (int)((p.x + sx) * r->scale);
    r->y = o.y + (int)((p.y + sy) * r->scale);
    r->w = gtk_widget_get_width(canvas) * r->scale;
    r->h = gtk_widget_get_height(canvas) * r->scale;
    return TRUE;
}

static void park(GtkWidget *window, gpointer data)
{
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (surface) doodle_park_window(gdk_win32_surface_get_handle(surface));
}

static GtkWidget *list_frame(const char *title, GtkWidget **list, int height)
{
    GtkWidget *scroll;
    *list = gtk_list_box_new();
    scroll = scrolled_new(*list);
    gtk_widget_set_size_request(scroll, -1, height);
    gtk_widget_set_vexpand(scroll, TRUE);
    return frame_new(title, scroll);
}

int main(int argc, char **argv)
{
    int rc = doodle_init(&argc, argv, "doodle4");
    GtkWidget *window, *hpaned, *vpaned, *side, *side_scroll;
    GtkEventController *legacy;
    GtkGesture *stylus;
    static const char *const stylus_signals[] = { "down", "motion", "up" };
    unsigned i;

    if (rc >= 0) return rc;
    if (argc > 1)
    {
        fprintf(stderr, "doodle4: unknown option %s (see --help)\n", argv[1]);
        return 2;
    }
    gtk_init();

    seen = g_ptr_array_new();
    seat = gdk_display_get_default_seat(gdk_display_get_default());
    g_signal_connect(seat, "device-added", G_CALLBACK(device_changed), "added");
    g_signal_connect(seat, "device-removed", G_CALLBACK(device_changed), "removed");
    g_signal_connect(seat, "tool-added", G_CALLBACK(tool_changed), "added");
    g_signal_connect(seat, "tool-removed", G_CALLBACK(tool_changed), "removed");

    window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Tablet doodle (GTK 4)");
    gtk_window_set_default_size(GTK_WINDOW(window), 1280, 900);

    canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(canvas, 640, 480);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(canvas), on_draw, NULL, NULL);
    g_signal_connect(canvas, "resize", G_CALLBACK(on_resize), NULL);
    legacy = gtk_event_controller_legacy_new();
    g_signal_connect(legacy, "event", G_CALLBACK(on_event), NULL);
    gtk_widget_add_controller(canvas, legacy);
    stylus = gtk_gesture_stylus_new();
    for (i = 0; i < G_N_ELEMENTS(stylus_signals); i++)
        g_signal_connect(stylus, stylus_signals[i], G_CALLBACK(on_stylus), (gpointer)stylus_signals[i]);
    gtk_widget_add_controller(canvas, GTK_EVENT_CONTROLLER(stylus));
    canvas_changed = queue_canvas_draw;

    side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(side, 6);
    gtk_widget_set_margin_end(side, 6);
    gtk_widget_set_margin_top(side, 6);
    gtk_widget_set_margin_bottom(side, 6);
    gtk_widget_set_size_request(side, 460, -1);
    box_add(side, doodle_status_label());
    box_add(side, list_frame("Seat devices", &device_list, 90));
    box_add(side, list_frame("Tools (current one selected)", &tool_list, 150));
    readout = label_new(TRUE);
    box_add(side, frame_new("Last event", readout));
    counters = label_new(FALSE);
    box_add(side, counters);
    box_add(side, doodle_emu_panel());

    vpaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_start_child(GTK_PANED(vpaned), canvas);
    gtk_paned_set_end_child(GTK_PANED(vpaned), doodle_log_panel());
    gtk_paned_set_shrink_start_child(GTK_PANED(vpaned), FALSE);
    gtk_paned_set_resize_end_child(GTK_PANED(vpaned), FALSE);
    side_scroll = scrolled_new(side);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(side_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(hpaned), vpaned);
    gtk_paned_set_end_child(GTK_PANED(hpaned), side_scroll);
    gtk_paned_set_shrink_start_child(GTK_PANED(hpaned), FALSE);
    gtk_paned_set_resize_end_child(GTK_PANED(hpaned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(hpaned), FALSE);
    gtk_window_set_child(GTK_WINDOW(window), hpaned);

    rebuild_lists(NULL);
    if (opt.smoke)              /* stay out of the way of anything using the mouse */
        g_signal_connect_after(window, "map", G_CALLBACK(park), NULL);
    gtk_window_present(GTK_WINDOW(window));
    doodle_main(window, canvas_rect);
    return 0;
}
