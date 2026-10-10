/*
 * gtk-tablet-doodle, GTK 3 front end.
 *
 * GTK 3 gives every Wintab cursor (each stylus, each end of it) its own
 * GdkDevice. Events are delivered through the master pointer; the
 * physical device is gdk_event_get_source_device(), and that is the
 * device whose name, axes and mode an application like GIMP works with.
 *
 * Copyright 2026 gtk-tablet-doodle contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <string.h>

#include "doodle-gtk.h"
#include <gdk/gdkwin32.h>

static GtkWidget *canvas, *device_list, *readout, *counters;
static GdkSeat *seat;
static GdkDevice *current;              /* the "current device", see pick_current() */
static GPtrArray *seen;                 /* source devices seen in events */
static guint rebuild_id;

static struct
{
    int motion, press, release, crossing, proximity;
    int pen_down, mouse_down;           /* motions with button 1 held */
    int switches;                       /* source device changed within a stroke */
    int no_press;                       /* button 1 held but no press was seen */
    int no_pressure;                    /* pen motions with button 1 but pressure 0 or none */
} count;

static DoodleSample last;
static gboolean stroke;                 /* `last` is the previous point of a stroke */
static GdkDevice *stroke_source, *grabbed;
static double pointer_x, pointer_y;     /* proximity events carry no position */

static const char *dev_name(GdkDevice *d) { return d ? gdk_device_get_name(d) : "(none)"; }

static const char *source_name(GdkInputSource s)
{
    static const char *const n[] = { "mouse", "pen", "eraser", "cursor", "keyboard",
                                     "touchscreen", "touchpad", "trackpoint", "tablet-pad" };
    return (unsigned)s < G_N_ELEMENTS(n) ? n[s] : "?";
}

static const char *type_name(GdkDevice *d)
{
    switch (gdk_device_get_device_type(d))
    {
    case GDK_DEVICE_TYPE_MASTER: return "master";
    case GDK_DEVICE_TYPE_SLAVE: return "slave";
    default: return "floating";
    }
}

static const char *const mode_names[] = { "disabled", "screen", "window", NULL };

static char *axis_uses(GdkDevice *d)
{
    static const char *const n[] = { "ignore", "x", "y", "pressure", "xtilt", "ytilt",
                                     "wheel", "distance", "rotation", "slider" };
    GString *s = g_string_new(NULL);
    int i;
    for (i = 0; i < gdk_device_get_n_axes(d); i++)
    {
        unsigned use = gdk_device_get_axis_use(d, i);
        g_string_append_printf(s, "%s%s", i ? " " : "", use < G_N_ELEMENTS(n) ? n[use] : "?");
    }
    return g_string_free(s, FALSE);
}

static int device_kind(GdkDevice *d)
{
    switch (d ? gdk_device_get_source(d) : GDK_SOURCE_MOUSE)
    {
    case GDK_SOURCE_PEN: case GDK_SOURCE_CURSOR: return DOODLE_PEN;
    case GDK_SOURCE_ERASER: return DOODLE_ERASER;
    default: return DOODLE_MOUSE;
    }
}

static const char *kind_hex(GdkDevice *d)
{
    int kind = device_kind(d);
    return kind == DOODLE_MOUSE ? "#8c8c8c" : kind == DOODLE_ERASER ? "#ff9ecc" : doodle_color_hex(dev_name(d));
}

/* GTK 3 reports both Wintab's orientation twist (Art Pen rotation) and its
 * tangential pressure (airbrush wheel) as GDK_AXIS_WHEEL; only the tool
 * tells them apart. */
static gboolean wheel_is_rotation(GdkDevice *d)
{
    char *n = g_ascii_strdown(dev_name(d), -1);
    gboolean rotation = strstr(n, "airbrush") == NULL;
    g_free(n);
    return rotation;
}

/* ---- GIMP's device handling (--gimp) ------------------------------------ */

/* gimp_device_manager_device_defaults() */
static void gimp_defaults(GdkDevice *d)
{
    GdkInputSource src = gdk_device_get_source(d);

    if (src == GDK_SOURCE_KEYBOARD) return;
    if (d == gdk_seat_get_pointer(seat))
        gdk_device_set_mode(d, GDK_MODE_SCREEN);
    else if (gdk_device_get_device_type(d) != GDK_DEVICE_TYPE_MASTER)
        gdk_device_set_mode(d, src == GDK_SOURCE_PEN || src == GDK_SOURCE_ERASER ||
                               src == GDK_SOURCE_CURSOR ? GDK_MODE_SCREEN : GDK_MODE_DISABLED);
}

/* gimp_devices_get_from_event(): a disabled slave counts as its master */
static GdkDevice *pick_current(GdkDevice *src)
{
    if (!src || gdk_device_get_source(src) == GDK_SOURCE_KEYBOARD) return NULL;
    if (!opt.gimp) return src;
    switch (gdk_device_get_device_type(src))
    {
    case GDK_DEVICE_TYPE_MASTER: return NULL;
    case GDK_DEVICE_TYPE_SLAVE:
        return gdk_device_get_mode(src) == GDK_MODE_DISABLED ? gdk_device_get_associated_device(src) : src;
    default: return src;
    }
}

/* ---- device list ------------------------------------------------------ */

struct entry { GdkDevice *device; const char *note; };

static gboolean contains(GArray *a, GdkDevice *d)
{
    guint i;
    for (i = 0; i < a->len; i++)
        if (g_array_index(a, struct entry, i).device == d) return TRUE;
    return FALSE;
}

static void collect_add(GArray *a, GdkDevice *d, const char *note)
{
    struct entry e = { d, note };
    if (d && gdk_device_get_source(d) != GDK_SOURCE_KEYBOARD && !contains(a, d)) g_array_append_val(a, e);
}

/* the seat's pointer and slaves (what applications enumerate), floating
 * devices from the deprecated device manager, and anything else that
 * showed up as a source device in events */
static GArray *collect_devices(void)
{
    GArray *a = g_array_new(FALSE, FALSE, sizeof(struct entry));
    GList *slaves = gdk_seat_get_slaves(seat, GDK_SEAT_CAPABILITY_ALL), *floating, *l;
    guint i;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    floating = gdk_device_manager_list_devices(gdk_display_get_device_manager(gdk_display_get_default()),
                                               GDK_DEVICE_TYPE_FLOATING);
    G_GNUC_END_IGNORE_DEPRECATIONS
    collect_add(a, gdk_seat_get_pointer(seat), "");
    for (l = slaves; l; l = l->next) collect_add(a, l->data, "");
    for (l = floating; l; l = l->next) collect_add(a, l->data, " (not a seat slave)");
    for (i = 0; i < seen->len; i++) collect_add(a, seen->pdata[i], " (only seen in events)");
    g_list_free(slaves);
    g_list_free(floating);
    return a;
}

static void select_current(void)
{
    GList *rows = gtk_container_get_children(GTK_CONTAINER(device_list)), *l;
    for (l = rows; l; l = l->next)
        if (g_object_get_data(l->data, "device") == current)
            gtk_list_box_select_row(GTK_LIST_BOX(device_list), l->data);
    g_list_free(rows);
}

static gboolean rebuild_devices(gpointer data);

static void mode_changed(GtkComboBox *combo, GdkDevice *d)
{
    int mode = gtk_combo_box_get_active(combo);
    gboolean ok = gdk_device_set_mode(d, mode);
    doodle_log("---- %s: mode %s%s", dev_name(d), mode_names[mode], ok ? "" : " refused");
    if (!rebuild_id) rebuild_id = g_idle_add(rebuild_devices, NULL);
}

static void add_row(GdkDevice *d, const char *note)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), *label = gtk_label_new(NULL);
    char *uses = axis_uses(d);
    char *markup = g_markup_printf_escaped(
        "<span foreground=\"%s\">●</span> <b>%s</b>%s\n<small>%s, %s, mode %s; axes: %s</small>",
        kind_hex(d), dev_name(d), note, source_name(gdk_device_get_source(d)), type_name(d),
        mode_names[gdk_device_get_mode(d)], uses);

    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_widget_set_hexpand(label, TRUE);
    box_add(box, label);
    if (gdk_device_get_device_type(d) != GDK_DEVICE_TYPE_MASTER)
    {
        GtkWidget *combo = gtk_combo_box_text_new();
        int i;
        for (i = 0; mode_names[i]; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), mode_names[i]);
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), gdk_device_get_mode(d));
        gtk_widget_set_valign(combo, GTK_ALIGN_CENTER);
        g_signal_connect(combo, "changed", G_CALLBACK(mode_changed), d);
        box_add(box, combo);
    }
    gtk_list_box_insert(GTK_LIST_BOX(device_list), box, -1);
    g_object_set_data(G_OBJECT(gtk_widget_get_parent(box)), "device", d);
    g_free(markup);
    g_free(uses);
}

static gboolean rebuild_devices(gpointer data)
{
    GArray *a = collect_devices();
    guint i;

    rebuild_id = 0;
    gtk_container_foreach(GTK_CONTAINER(device_list), (GtkCallback)gtk_widget_destroy, NULL);
    for (i = 0; i < a->len; i++)
        add_row(g_array_index(a, struct entry, i).device, g_array_index(a, struct entry, i).note);
    g_array_free(a, TRUE);
    gtk_widget_show_all(device_list);
    select_current();
    return G_SOURCE_REMOVE;
}

static void device_added(GObject *seat_or_manager, GdkDevice *d, gpointer data)
{
    if (data) doodle_log("---- device %s: %s", (const char *)data, dev_name(d));
    if (opt.gimp && data && !strcmp(data, "added")) gimp_defaults(d);
    if (!rebuild_id) rebuild_id = g_idle_add(rebuild_devices, NULL);
}

char *frontend_describe(void)
{
    GString *s = g_string_new("GTK 3 devices (seat pointer and slaves, floating, seen in events):\n");
    GArray *a = collect_devices();
    guint i;

    for (i = 0; i < a->len; i++)
    {
        GdkDevice *d = g_array_index(a, struct entry, i).device;
        char *uses = axis_uses(d);
        g_string_append_printf(s, "  %-44s %-6s %-8s %-8s axes: %s%s%s\n", dev_name(d),
                               source_name(gdk_device_get_source(d)), type_name(d),
                               mode_names[gdk_device_get_mode(d)], uses,
                               g_array_index(a, struct entry, i).note, d == current ? "  <- current" : "");
        g_free(uses);
    }
    g_array_free(a, TRUE);
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

static void read_sample(GdkEvent *ev, GdkDevice *src, DoodleSample *s)
{
    double v, ty;

    memset(s, 0, sizeof(*s));
    if (!gdk_event_get_coords(ev, &s->x, &s->y)) { s->x = pointer_x; s->y = pointer_y; }
    s->kind = device_kind(src);
    doodle_color(dev_name(src), s->rgb);
    s->pressure = gdk_event_get_axis(ev, GDK_AXIS_PRESSURE, &v) ? v : -1;
    s->has_tilt = gdk_event_get_axis(ev, GDK_AXIS_XTILT, &v) && gdk_event_get_axis(ev, GDK_AXIS_YTILT, &ty);
    if (s->has_tilt) { s->tilt_x = v; s->tilt_y = ty; }
    s->wheel = s->rotation = -1;
    if (gdk_event_get_axis(ev, GDK_AXIS_WHEEL, &v))
    {
        if (wheel_is_rotation(src)) s->rotation = v * 360;
        else s->wheel = v;
    }
}

static void show_state(const char *name, GdkDevice *dev, GdkDevice *src, const DoodleSample *s,
                       GdkModifierType state, const char *axes)
{
    char *uses = src ? axis_uses(src) : g_strdup("");
    char *text = g_strdup_printf(
        "event    %s%s\nsource   %s\n         %s, %s, mode %s; axes: %s\ndevice   %s\ncurrent  %s\n"
        "position %.1f, %.1f\naxes    %s%s",
        name, state & GDK_BUTTON1_MASK ? "  [button 1 held]" : "", dev_name(src),
        src ? source_name(gdk_device_get_source(src)) : "-", src ? type_name(src) : "-",
        src ? mode_names[gdk_device_get_mode(src)] : "-", uses, dev_name(dev), dev_name(current),
        s->x, s->y, *axes ? axes : " (none)",
        s->rotation >= 0 || s->wheel >= 0 ? "\n         (both come from GDK_AXIS_WHEEL in GTK 3)" : "");
    gtk_label_set_text(GTK_LABEL(readout), text);
    g_free(text);
    g_free(uses);

    text = g_strdup_printf("motion %d  press %d  release %d  crossing %d  proximity %d\n"
                           "button 1 held: %d pen and %d mouse motions\n"
                           "source changed mid-stroke %d, held without press %d, pen without pressure %d",
                           count.motion, count.press, count.release, count.crossing, count.proximity,
                           count.pen_down, count.mouse_down, count.switches, count.no_press, count.no_pressure);
    gtk_label_set_text(GTK_LABEL(counters), text);
    g_free(text);
}

static gboolean on_event(GtkWidget *widget, GdkEvent *ev, gpointer data)
{
    GdkEventType type = gdk_event_get_event_type(ev);
    GdkDevice *dev = gdk_event_get_device(ev), *src = gdk_event_get_source_device(ev), *cur;
    const char *name = event_name(type);
    GdkModifierType state = 0;
    guint button = 0;
    char axes[128], btn[16] = "";
    DoodleSample s;

    if (!name) return FALSE;
    gdk_event_get_state(ev, &state);
    if (gdk_event_get_button(ev, &button)) snprintf(btn, sizeof(btn), " button %u", button);
    read_sample(ev, src, &s);
    pointer_x = s.x;
    pointer_y = s.y;
    doodle_axes_text(axes, sizeof(axes), &s);
    doodle_log("%8.3f %-8s %-32s via %-20s%s%s%s", doodle_now(), name, dev_name(src), dev_name(dev),
               btn, axes, state & GDK_BUTTON1_MASK ? " [B1]" : "");

    if (src && !g_ptr_array_find(seen, src, NULL))
    {
        g_ptr_array_add(seen, src);
        if (!rebuild_id) rebuild_id = g_idle_add(rebuild_devices, NULL);
    }
    if ((cur = pick_current(src)) && cur != current)
    {
        current = cur;
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
        stroke_source = src;
        if (opt.gimp && !grabbed)   /* gimp_display_shell_pointer_grab() */
        {
            gdk_seat_grab(seat, gtk_widget_get_window(canvas), GDK_SEAT_CAPABILITY_ALL_POINTING, FALSE,
                          NULL, ev, NULL, NULL);
            grabbed = src;
        }
    }
    else if ((type == GDK_MOTION_NOTIFY && (state & GDK_BUTTON1_MASK)) ||
             (type == GDK_BUTTON_RELEASE && button == 1))
    {
        if (!stroke)
        {
            count.no_press++;
            last = s;
            stroke = TRUE;
            stroke_source = src;
        }
        if (src != stroke_source)
        {
            count.switches++;
            stroke_source = src;
        }
        if (type == GDK_MOTION_NOTIFY)
        {
            if (s.kind == DOODLE_MOUSE) count.mouse_down++;
            else count.pen_down++;
            if (s.kind != DOODLE_MOUSE && s.pressure <= 0) count.no_pressure++;
        }
        canvas_segment(&last, &s);
        last = s;
        if (type == GDK_BUTTON_RELEASE)
        {
            stroke = FALSE;
            if (grabbed) gdk_seat_ungrab(seat);
            grabbed = NULL;
        }
    }
    show_state(name, dev, src, &s, state, axes);
    return FALSE;
}

/* ---- canvas widget ------------------------------------------------------ */

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
    canvas_paint(cr);
    return TRUE;
}

static void on_size_allocate(GtkWidget *w, GdkRectangle *a, gpointer data)
{
    canvas_set_size(a->width, a->height, gtk_widget_get_scale_factor(w));
}

static void queue_canvas_draw(void) { gtk_widget_queue_draw(canvas); }

/* the canvas in physical screen pixels, for the emulator */
static gboolean canvas_rect(DoodleScreenRect *r)
{
    GtkWidget *top = gtk_widget_get_toplevel(canvas);
    GdkWindow *win = gtk_widget_get_window(top);
    POINT p = { 0, 0 };
    int x, y;

    if (!win || !gtk_widget_get_mapped(canvas) ||
        !gtk_widget_translate_coordinates(canvas, top, 0, 0, &x, &y))
        return FALSE;
    ClientToScreen(gdk_win32_window_get_handle(win), &p);
    r->scale = gtk_widget_get_scale_factor(canvas);
    r->x = p.x + x * r->scale;
    r->y = p.y + y * r->scale;
    r->w = gtk_widget_get_allocated_width(canvas) * r->scale;
    r->h = gtk_widget_get_allocated_height(canvas) * r->scale;
    return TRUE;
}

int main(int argc, char **argv)
{
    int rc = doodle_init(&argc, argv, "doodle3");
    GtkWidget *window, *hpaned, *vpaned, *side, *list_scroll;
    GObject *manager;
    GList *slaves, *l;

    if (rc >= 0) return rc;
    gtk_init(&argc, &argv);
    if (argc > 1)
    {
        fprintf(stderr, "doodle3: unknown option %s (see --help)\n", argv[1]);
        return 2;
    }

    seen = g_ptr_array_new();
    seat = gdk_display_get_default_seat(gdk_display_get_default());
    current = gdk_seat_get_pointer(seat);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    manager = G_OBJECT(gdk_display_get_device_manager(gdk_display_get_default()));
    G_GNUC_END_IGNORE_DEPRECATIONS
    g_signal_connect(seat, "device-added", G_CALLBACK(device_added), "added");
    g_signal_connect(seat, "device-removed", G_CALLBACK(device_added), "removed");
    g_signal_connect(manager, "device-added", G_CALLBACK(device_added), NULL);
    g_signal_connect(manager, "device-removed", G_CALLBACK(device_added), NULL);
    g_signal_connect(manager, "device-changed", G_CALLBACK(device_added), NULL);
    if (opt.gimp)
    {
        gimp_defaults(current);
        slaves = gdk_seat_get_slaves(seat, GDK_SEAT_CAPABILITY_ALL);
        for (l = slaves; l; l = l->next) gimp_defaults(l->data);
        g_list_free(slaves);
    }

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Tablet doodle (GTK 3)");
    gtk_window_set_default_size(GTK_WINDOW(window), 1280, 900);

    canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(canvas, 640, 480);
    gtk_widget_add_events(canvas, GDK_ALL_EVENTS_MASK);
    g_signal_connect(canvas, "event", G_CALLBACK(on_event), NULL);
    g_signal_connect(canvas, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(canvas, "size-allocate", G_CALLBACK(on_size_allocate), NULL);
    canvas_changed = queue_canvas_draw;

    side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    g_object_set(side, "margin", 6, NULL);
    gtk_widget_set_size_request(side, 460, -1);
    box_add(side, doodle_status_label());
    device_list = gtk_list_box_new();
    list_scroll = scrolled_new(device_list);
    gtk_widget_set_size_request(list_scroll, -1, 220);
    gtk_widget_set_vexpand(list_scroll, TRUE);
    box_add(side, frame_new("Devices (mode as in GIMP's input devices)", list_scroll));
    readout = label_new(TRUE);
    box_add(side, frame_new("Last event", readout));
    counters = label_new(FALSE);
    box_add(side, counters);
    box_add(side, doodle_emu_panel());

    vpaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_pack1(GTK_PANED(vpaned), canvas, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(vpaned), doodle_log_panel(), FALSE, TRUE);
    hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(hpaned), vpaned, TRUE, FALSE);
    list_scroll = scrolled_new(side);     /* the side panel scrolls on small screens */
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_paned_pack2(GTK_PANED(hpaned), list_scroll, FALSE, FALSE);
    gtk_container_add(GTK_CONTAINER(window), hpaned);

    rebuild_devices(NULL);
    if (opt.smoke)              /* stay out of the way of anything using the mouse */
    {
        gtk_window_set_focus_on_map(GTK_WINDOW(window), FALSE);
        gtk_window_move(GTK_WINDOW(window), GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) - 160,
                        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) - 120);
    }
    gtk_widget_show_all(window);
    if (opt.smoke) doodle_park_window(gdk_win32_window_get_handle(gtk_widget_get_window(window)));
    doodle_main(window, canvas_rect);
    return 0;
}
