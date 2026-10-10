/*
 * gtk-tablet-doodle: toolkit-independent parts shared by the GTK 3 and
 * GTK 4 front ends.
 *
 * Copyright 2026 gtk-tablet-doodle contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ring.h"
#include "doodle.h"

DoodleOptions opt;
void (*doodle_note)(const char *text);
void (*canvas_changed)(void);

static gint64 start_time;
static struct ring_handle ring;
static gboolean ring_ok;

/* ---- options and environment ------------------------------------------ */

static const char usage[] =
    "Usage: %s [options]\n"
    "  --emulate          be the tablet: a private otd-wintab ring fed from the mouse\n"
    "  --instance=NAME    ring name suffix for --emulate (default doodle-<pid>)\n"
    "  --script           play the demo scenario at startup (implies --emulate;\n"
    "                     takes over the mouse for about ten seconds)\n"
    "  --system-wintab    OTD_WINTAB_DISABLE=1: GTK loads the system Wintab32.dll\n"
    "  --no-glue          OTD_WINTAB_NO_GTK_GLUE=1: no otd-wintab GTK 3 workarounds\n"
    "  --api=API          GDK_WIN32_TABLET_INPUT_API: wintab (default), winpointer,\n"
    "                     none, or default (unset: GTK's own choice)\n"
    "  --gimp             GTK 3: GIMP's device modes and stroke grab\n"
    "  --log-stdout       also print the event log on stdout\n"
    "  --smoke            print providers and devices, exit after a second; no input\n";

static gboolean env_set(const char *name)
{
    const char *v = g_getenv(name);
    return v && *v && *v != '0';
}

/* The dwmapi proxy reads OTD_WINTAB_DISABLE in DllMain, before main(). */
static int rerun_with_environment(void)
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD code = 1;

    if (!CreateProcessW(NULL, GetCommandLineW(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        fprintf(stderr, "cannot restart with OTD_WINTAB_DISABLE=1 (error %lu)\n", GetLastError());
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

int doodle_init(int *argc, char **argv, const char *frontend)
{
    const char *api = NULL, *instance = NULL;
    gboolean no_glue = FALSE;
    int i, n = 1;

    start_time = g_get_monotonic_time();
    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 1; i < *argc; i++)
    {
        const char *a = argv[i];
        if (!strcmp(a, "--emulate")) opt.emulate = TRUE;
        else if (!strcmp(a, "--script")) opt.script = opt.emulate = TRUE;
        else if (!strcmp(a, "--smoke")) opt.smoke = TRUE;
        else if (!strcmp(a, "--system-wintab")) opt.system_wintab = TRUE;
        else if (!strcmp(a, "--no-glue")) no_glue = TRUE;
        else if (!strcmp(a, "--gimp")) opt.gimp = TRUE;
        else if (!strcmp(a, "--log-stdout")) opt.log_stdout = TRUE;
        else if (g_str_has_prefix(a, "--api=")) api = a + 6;
        else if (g_str_has_prefix(a, "--instance=")) instance = a + 11;
        else if (!strcmp(a, "--help") || !strcmp(a, "-h"))
        {
            printf(usage, frontend);
            return 0;
        }
        else argv[n++] = argv[i];
    }
    *argc = n;
    argv[n] = NULL;
    if (opt.smoke) opt.script = FALSE;     /* --smoke never touches input */

    if (opt.system_wintab && !env_set("OTD_WINTAB_DISABLE"))
    {
        g_setenv("OTD_WINTAB_DISABLE", "1", TRUE);
        return rerun_with_environment();
    }
    if (no_glue) g_setenv("OTD_WINTAB_NO_GTK_GLUE", "1", TRUE);

    /* Both GTK 3 and GTK 4 prefer Windows Ink unless told otherwise. */
    if (api && !strcmp(api, "default")) g_unsetenv("GDK_WIN32_TABLET_INPUT_API");
    else if (api || !g_getenv("GDK_WIN32_TABLET_INPUT_API"))
        g_setenv("GDK_WIN32_TABLET_INPUT_API", api ? api : "wintab", TRUE);

    if (opt.emulate)
    {
        char *name = instance ? g_strdup(instance) : g_strdup_printf("doodle-%lu", GetCurrentProcessId());
        if (opt.system_wintab)
            fprintf(stderr, "note: --emulate feeds otd-wintab, which --system-wintab bypasses\n");
        /* before GTK opens Wintab, so the provider attaches to our ring */
        g_setenv("OTD_WINTAB_INSTANCE", name, TRUE);
        g_free(name);
        ring_ok = ring_create(&ring);
        if (!ring_ok) fprintf(stderr, "cannot create the otd-wintab ring; emulation is off\n");
    }
    return -1;
}

double doodle_now(void)
{
    return (g_get_monotonic_time() - start_time) / 1e6;
}

static char *module_path(const wchar_t *name)
{
    WCHAR path[MAX_PATH];
    HMODULE m = GetModuleHandleW(name);

    if (!m || !GetModuleFileNameW(m, path, MAX_PATH)) return NULL;
    return g_utf16_to_utf8((gunichar2 *)path, -1, NULL, NULL, NULL);
}

void doodle_park_window(void *hwnd)
{
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) - 160;
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) - 120;
    SetWindowPos(hwnd, HWND_BOTTOM, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

char *doodle_module_path(const char *dll)
{
    gunichar2 *w = g_utf8_to_utf16(dll, -1, NULL, NULL, NULL);
    char *path = module_path((const wchar_t *)w);
    g_free(w);
    return path;
}

char *doodle_status(void)
{
    GString *s = g_string_new(NULL);
    char *otd = module_path(L"otd-wintab32.dll"), *sys = module_path(L"wintab32.dll");
    char *dwm = module_path(L"dwmapi.dll"), *exe = module_path(NULL);
    char *exe_dir = exe ? g_path_get_dirname(exe) : NULL, *dwm_dir = dwm ? g_path_get_dirname(dwm) : NULL;
    const char *inst = g_getenv("OTD_WINTAB_INSTANCE");

    if (otd) g_string_append_printf(s, "Wintab: otd-wintab %s\n", otd);
    if (sys) g_string_append_printf(s, "Wintab: system %s\n", sys);
    if (!otd && !sys) g_string_append(s, "Wintab: not loaded\n");
    g_string_append_printf(s, "dwmapi: %s%s\n", dwm ? dwm : "not loaded",
                           dwm_dir && exe_dir && !g_ascii_strcasecmp(dwm_dir, exe_dir) ? " (otd-wintab proxy)" : "");
    if (ring_ok)
        g_string_append_printf(s, "Tablet: emulated, ring instance \"%s\"\n", inst);
    else if (env_set("OTD_WINTAB_DISABLE") || !otd)
        g_string_append(s, "Tablet: real, through the system Wintab (if any)\n");
    else
        g_string_append_printf(s, "Tablet: real, OpenTabletDriver ring %s%s\n",
                               inst ? "instance " : "(default)", inst ? inst : "");
    g_string_append_printf(s, "GDK_WIN32_TABLET_INPUT_API=%s OTD_WINTAB_DISABLE=%s OTD_WINTAB_NO_GTK_GLUE=%s",
                           g_getenv("GDK_WIN32_TABLET_INPUT_API") ? g_getenv("GDK_WIN32_TABLET_INPUT_API") : "(unset)",
                           env_set("OTD_WINTAB_DISABLE") ? "1" : "0", env_set("OTD_WINTAB_NO_GTK_GLUE") ? "1" : "0");
    g_free(otd); g_free(sys); g_free(dwm); g_free(exe); g_free(exe_dir); g_free(dwm_dir);
    return g_string_free(s, FALSE);
}

void doodle_color(const char *key, double rgb[3])
{
    /* golden-ratio hues from a hash of the name: stable across runs */
    double h = fmod(g_str_hash(key) * 0.618033988749895, 1.0) * 6, s = 0.8, v = 0.75;
    int i = (int)h;
    double f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    double table[6][3] = { { v, t, p }, { q, v, p }, { p, v, t }, { p, q, v }, { t, p, v }, { v, p, q } };
    memcpy(rgb, table[i % 6], sizeof(table[0]));
}

const char *doodle_color_hex(const char *key)
{
    static char buf[8];
    double c[3];
    doodle_color(key, c);
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", (int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255));
    return buf;
}

void doodle_axes_text(char *buf, size_t size, const DoodleSample *s)
{
    size_t n = 0;

    buf[0] = 0;
    if (s->pressure >= 0) n += snprintf(buf + n, size - n, " p=%.3f", s->pressure);
    if (s->has_tilt && n < size) n += snprintf(buf + n, size - n, " tilt=%+.2f,%+.2f", s->tilt_x, s->tilt_y);
    if (s->wheel >= 0 && n < size) n += snprintf(buf + n, size - n, " wheel=%.3f", s->wheel);
    if (s->rotation >= 0 && n < size) snprintf(buf + n, size - n, " rot=%.0f", s->rotation);
}

/* ---- canvas ------------------------------------------------------------ */

static cairo_surface_t *surface;
static int canvas_w, canvas_h, canvas_scale = 1;
static double since_decoration;

static void changed(void)
{
    if (canvas_changed) canvas_changed();
}

static void fill_white(cairo_t *cr)
{
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
}

/* The backing surface only grows, so shrinking the window keeps the drawing. */
void canvas_set_size(int width, int height, int scale)
{
    cairo_surface_t *s;
    cairo_t *cr;

    if (surface && width <= canvas_w && height <= canvas_h && scale == canvas_scale) return;
    canvas_w = MAX(width, canvas_w);
    canvas_h = MAX(height, canvas_h);
    canvas_scale = MAX(scale, 1);
    s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, canvas_w * canvas_scale, canvas_h * canvas_scale);
    cairo_surface_set_device_scale(s, canvas_scale, canvas_scale);
    cr = cairo_create(s);
    fill_white(cr);
    if (surface)
    {
        cairo_set_source_surface(cr, surface, 0, 0);
        cairo_paint(cr);
        cairo_surface_destroy(surface);
    }
    cairo_destroy(cr);
    surface = s;
}

void canvas_paint(cairo_t *cr)
{
    if (!surface) { fill_white(cr); return; }
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);
}

void canvas_clear(void)
{
    cairo_t *cr;
    if (!surface) return;
    cr = cairo_create(surface);
    fill_white(cr);
    cairo_destroy(cr);
    changed();
}

/* rotation as a nib-angle tick across the line, tilt as a short vector */
static void decorate(cairo_t *cr, const DoodleSample *s, double width)
{
    cairo_set_line_width(cr, 1.5);
    if (s->rotation >= 0)
    {
        double a = s->rotation * G_PI / 180, l = width / 2 + 6;
        cairo_set_source_rgb(cr, s->rgb[0] * 0.4, s->rgb[1] * 0.4, s->rgb[2] * 0.4);
        cairo_move_to(cr, s->x - l * cos(a), s->y - l * sin(a));
        cairo_line_to(cr, s->x + l * cos(a), s->y + l * sin(a));
        cairo_stroke(cr);
    }
    if (s->has_tilt)
    {
        double x = s->x + s->tilt_x * 24, y = s->y + s->tilt_y * 24;
        cairo_set_source_rgba(cr, 0.1, 0.25, 0.9, 0.8);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, s->x, s->y);
        cairo_line_to(cr, x, y);
        cairo_stroke(cr);
        cairo_arc(cr, x, y, 1.5, 0, 2 * G_PI);
        cairo_fill(cr);
    }
}

void canvas_segment(const DoodleSample *a, const DoodleSample *b)
{
    static const double dash = 4;
    double width = 1, alpha = 1, r, g, bl;
    cairo_t *cr;

    if (!surface) return;
    cr = cairo_create(surface);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    if (b->kind == DOODLE_MOUSE)
        r = g = bl = 0.55;                          /* thin grey: the mouse */
    else
    {
        if (b->kind == DOODLE_ERASER) { r = 1; g = 0.62; bl = 0.8; }   /* pale pink trail */
        else { r = b->rgb[0]; g = b->rgb[1]; bl = b->rgb[2]; }
        if (b->pressure >= 0) width = 1 + 18 * b->pressure;
        else cairo_set_dash(cr, &dash, 1, 0);      /* a pen without pressure: dashed */
        if (b->wheel >= 0) alpha = 0.15 + 0.85 * CLAMP(b->wheel, 0, 1);
    }
    cairo_set_source_rgba(cr, r, g, bl, alpha);
    cairo_set_line_width(cr, width);
    cairo_move_to(cr, a->x, a->y);
    cairo_line_to(cr, b->x, b->y);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);

    since_decoration += hypot(b->x - a->x, b->y - a->y);
    if (b->kind != DOODLE_MOUSE && since_decoration >= 14)
    {
        since_decoration = 0;
        decorate(cr, b, width);
    }
    cairo_destroy(cr);
    changed();
}

/* proximity in: open green circle; out: red cross */
void canvas_mark(double x, double y, gboolean proximity_in)
{
    cairo_t *cr;
    if (!surface) return;
    cr = cairo_create(surface);
    cairo_set_line_width(cr, 1.5);
    if (proximity_in)
    {
        cairo_set_source_rgb(cr, 0.1, 0.65, 0.2);
        cairo_arc(cr, x, y, 5, 0, 2 * G_PI);
    }
    else
    {
        cairo_set_source_rgb(cr, 0.85, 0.1, 0.1);
        cairo_move_to(cr, x - 4, y - 4); cairo_line_to(cr, x + 4, y + 4);
        cairo_move_to(cr, x + 4, y - 4); cairo_line_to(cr, x - 4, y + 4);
    }
    cairo_stroke(cr);
    cairo_destroy(cr);
    changed();
}

void canvas_label(double x, double y, const char *text)
{
    cairo_t *cr;
    if (!surface) return;
    cr = cairo_create(surface);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text);
    cairo_destroy(cr);
    changed();
}

/* ---- tablet emulator: we are the OpenTabletDriver side of the ring ---- */

struct tool { const char *name; uint32_t tool_id, serial, flags, axes; };

#define AX_PT (OTDWT_AX_PRESSURE | OTDWT_AX_TILT)
static const struct tool tools[EMU_N_TOOLS] = {
    [EMU_GRIP]     = { "Grip Pen", 0x823, 0xA001, 0, AX_PT },
    [EMU_ERASER]   = { "Grip Pen Eraser", 0x82b, 0xA001, OTDWT_F_ERASER, AX_PT },
    [EMU_AIRBRUSH] = { "Airbrush", 0x913, 0xB002, 0, AX_PT | OTDWT_AX_WHEEL },
    [EMU_ARTPEN]   = { "6D Art Pen", 0x885, 0xC003, 0, AX_PT | OTDWT_AX_ROTATION },
    [EMU_GRIP2]    = { "Grip Pen", 0x823, 0xA0F2, 0, AX_PT },
};
const char *const emu_tool_labels[EMU_N_TOOLS + 1] = {
    "Grip Pen #A001", "Grip Pen #A001, eraser end", "Airbrush #B002", "6D Art Pen #C003",
    "Second Grip Pen #A0F2", NULL
};

EmuControls emu = { .tool = EMU_GRIP, .pressure = 0.6, .ramp = TRUE, .wheel = 0.6 };

struct pen_state
{
    int32_t x, y;               /* screen pixels */
    int32_t tip;
    uint32_t pressure;          /* 0..OTDWT_PRESSURE_MAX */
    int32_t tilt_x, tilt_y;     /* tenths of a degree */
    uint32_t wheel;
    int32_t rotation;           /* tenths of a degree */
};

static void publish(uint32_t type, const struct tool *t, const struct pen_state *p)
{
    otdwt_event ev = { 0 };

    ev.type = type;
    ev.flags = t->flags;
    ev.axes = t->axes;
    ev.tool_id = t->tool_id;
    ev.serial = t->serial;
    ev.buttons = p->tip ? 1 : 0;
    ev.x = (p->x - GetSystemMetrics(SM_XVIRTUALSCREEN)) * OTDWT_POS_SCALE;
    ev.y = (p->y - GetSystemMetrics(SM_YVIRTUALSCREEN)) * OTDWT_POS_SCALE;
    ev.pressure = p->pressure;
    ev.tilt_x = p->tilt_x;
    ev.tilt_y = p->tilt_y;
    ev.wheel = p->wheel;
    ev.rotation = p->rotation;
    ev.time_ms = GetTickCount();
    strncpy(ev.name, t->name, OTDWT_NAME_LEN - 1);
    ring_publish(&ring, &ev);
}

static DoodleRectFunc canvas_rect;
static gboolean demo_active;
static int prox_tool = -1;      /* emulated tool in range, -1: none */
static struct pen_state last;
static gint64 tip_since;
static double spin_angle;
static char emu_text[160] = "off";

gboolean emu_available(void) { return ring_ok; }
gboolean demo_running(void) { return demo_active; }
const char *emu_describe(void) { return ring_ok ? emu_text : "not available: start with --emulate"; }

static void emu_leave(void)
{
    if (prox_tool < 0) return;
    last.tip = 0;
    last.pressure = 0;
    publish(OTDWT_EV_PROXIMITY_OUT, &tools[prox_tool], &last);
    prox_tool = -1;
}

/* Polls the real cursor and button rather than GTK events, so the
 * emulated pen does not depend on the GTK behaviour under test. The OS
 * cursor already is where the pen is, and the real left button is held
 * while the tip is down: the same conditions OpenTabletDriver creates. */
static gboolean emu_tick(gpointer data)
{
    struct pen_state p = { 0 };
    DoodleScreenRect r;
    POINT pt;
    gboolean inside, button;

    if (demo_active) return G_SOURCE_CONTINUE;
    GetCursorPos(&pt);
    button = GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) < 0;
    inside = emu.enabled && GetAsyncKeyState(VK_SHIFT) >= 0 && canvas_rect(&r) &&
             pt.x >= r.x && pt.x < r.x + r.w && pt.y >= r.y && pt.y < r.y + r.h;

    if (prox_tool >= 0 && (!inside || prox_tool != emu.tool)) emu_leave();
    if (!inside)
    {
        snprintf(emu_text, sizeof(emu_text), emu.enabled ? "out of range" : "off");
        return G_SOURCE_CONTINUE;
    }
    p.x = pt.x;
    p.y = pt.y;
    if (prox_tool < 0)
    {
        if (button)
        {
            snprintf(emu_text, sizeof(emu_text), "release the button to bring the pen in");
            return G_SOURCE_CONTINUE;
        }
        prox_tool = emu.tool;
        publish(OTDWT_EV_PROXIMITY_IN, &tools[prox_tool], &p);
        last = p;
    }

    if (button && !last.tip) tip_since = g_get_monotonic_time();
    p.tip = button;
    if (button)
    {
        double f = emu.ramp ? CLAMP((g_get_monotonic_time() - tip_since) / 500000.0, 0.05, 1.0) : 1.0;
        p.pressure = (uint32_t)lround(emu.pressure * f * OTDWT_PRESSURE_MAX);
    }
    spin_angle = emu.spin ? fmod(spin_angle + 3, 360) : emu.rotation;
    p.rotation = (int32_t)lround(spin_angle * 10) % 3600;
    p.tilt_x = (int32_t)lround(emu.tilt_x * 10);
    p.tilt_y = (int32_t)lround(emu.tilt_y * 10);
    p.wheel = (uint32_t)lround(emu.wheel * OTDWT_WHEEL_MAX);
    if (memcmp(&p, &last, sizeof(p)))
    {
        publish(OTDWT_EV_PACKET, &tools[prox_tool], &p);
        last = p;
    }
    snprintf(emu_text, sizeof(emu_text), "%s in range, tip %s, pressure %.2f, rotation %d°",
             emu_tool_labels[prox_tool], p.tip ? "down" : "up",
             (double)p.pressure / OTDWT_PRESSURE_MAX, p.rotation / 10);
    return G_SOURCE_CONTINUE;
}

void emu_start(DoodleRectFunc rect)
{
    canvas_rect = rect;
    if (ring_ok && !opt.smoke) g_timeout_add(5, emu_tick, NULL);
}

void emu_announce_tools(void)
{
    struct pen_state p = { 0 };
    DoodleScreenRect r;
    int i;

    if (!ring_ok || !canvas_rect || !canvas_rect(&r)) return;
    p.x = r.x + r.w / 2;
    p.y = r.y + r.h / 2;
    for (i = 0; i < EMU_N_TOOLS; i++)
    {
        publish(OTDWT_EV_PROXIMITY_IN, &tools[i], &p);
        publish(OTDWT_EV_PROXIMITY_OUT, &tools[i], &p);
    }
}

/* ---- demo: the otd-wintab GTK probes' scenario, one row per step ------ */

#define DEMO_ROWS 9
static DoodleScreenRect R;
static int row, x0, len;        /* current row; stroke start and length (screen px) */
static gboolean os_down;

static void pump(int ms)
{
    gint64 end = g_get_monotonic_time() + ms * 1000;
    while (g_get_monotonic_time() < end)
    {
        while (g_main_context_iteration(NULL, FALSE));
        Sleep(2);
    }
}

static void mouse_to(int x, int y)
{
    INPUT in = { 0 };
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    in.type = INPUT_MOUSE;
    in.mi.dx = (LONG)(((double)(x - vx) + 0.5) * 65536.0 / vw);
    in.mi.dy = (LONG)(((double)(y - vy) + 0.5) * 65536.0 / vh);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(in));
}

/* OpenTabletDriver's tip binding: the real left button follows the tip */
static void os_button(gboolean down)
{
    INPUT in = { 0 };
    if (down == os_down) return;
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(in));
    os_down = down;
}

static int row_y(void) { return R.y + R.h * (2 * row + 1) / (2 * DEMO_ROWS); }

static void step(const char *label)
{
    char *text = g_strdup_printf("%d. %s", row + 1, label);
    char *note = g_strdup_printf("demo step %s", text);
    canvas_label((x0 - R.x) / (double)R.scale, (row_y() - R.y) / (double)R.scale - 16, text);
    if (doodle_note) doodle_note(note);
    g_free(note);
    g_free(text);
}

static void mouse_wiggle(int x, int y)
{
    int i;
    for (i = 0; i < 10; i++)
    {
        mouse_to(x + i * 7 * R.scale, y + (i & 1) * 9 * R.scale);
        pump(15);
    }
    pump(100);
}

static void mouse_stroke(void)
{
    int i, y = row_y();
    for (i = 0; i < 20; i++)
    {
        mouse_to(x0 + i * len / 20, y + (int)(6 * R.scale * sin(i * 0.6)));
        os_button(i > 2 && i < 17);
        pump(15);
    }
    os_button(FALSE);
    pump(100);
}

static struct pen_state P;

static void pen_at(int x, int y, gboolean down, double t)
{
    P.x = x;
    P.y = y;
    P.tip = down;
    P.pressure = down ? (uint32_t)(6000 + 22000 * sin(G_PI * CLAMP(t, 0, 1))) : 0;
    mouse_to(x, y);
}

/* a real pen: the system cursor follows it, as OpenTabletDriver does */
static void pen_stroke(const struct tool *t, gboolean lift_tip)
{
    int i, y = row_y();

    P = (struct pen_state){ .tilt_y = 200, .wheel = 600 };
    pen_at(x0, y, FALSE, 0);
    publish(OTDWT_EV_PROXIMITY_IN, t, &P);
    pump(60);
    for (i = 0; i < 20; i++)
    {
        gboolean down = i > 2 && i < 17;
        P.tilt_x = -400 + i * 40;
        P.wheel = i * OTDWT_WHEEL_MAX / 19;
        pen_at(x0 + i * len / 20, y + (int)(6 * R.scale * sin(i * 0.6)), down, (i - 3) / 13.0);
        publish(OTDWT_EV_PACKET, t, &P);
        os_button(down);
        pump(15);
    }
    if (!lift_tip)
    {
        P.tip = 1;                      /* tip goes down again ... */
        P.pressure = 20000;
        publish(OTDWT_EV_PACKET, t, &P);
        os_button(TRUE);
        pump(30);
    }
    P.tip = 0;
    P.pressure = 0;
    publish(OTDWT_EV_PROXIMITY_OUT, t, &P);   /* ... and the pen leaves */
    os_button(FALSE);
    pump(150);
}

/* two strokes without leaving proximity: lift the tip, hover, touch again */
static void pen_two_strokes(const struct tool *t)
{
    int s, i, y = row_y();

    P = (struct pen_state){ .tilt_y = 200 };
    pen_at(x0, y, FALSE, 0);
    publish(OTDWT_EV_PROXIMITY_IN, t, &P);
    pump(60);
    for (s = 0; s < 2; s++)
    {
        for (i = 0; i < 16; i++)
        {
            gboolean down = i >= 3 && i < 13;
            pen_at(x0 + (s * 16 + i) * len / 32, y, down, (i - 3) / 9.0);
            publish(OTDWT_EV_PACKET, t, &P);
            os_button(down);
            pump(15);
        }
        pump(100);                      /* hovering between the strokes */
    }
    publish(OTDWT_EV_PROXIMITY_OUT, t, &P);
    pump(150);
}

/* the Art Pen turning its barrel: hovering, then with the tip down */
static void art_pen_twisting(void)
{
    const struct tool *t = &tools[EMU_ARTPEN];
    int i, y = row_y();

    P = (struct pen_state){ .tilt_y = 200 };
    pen_at(x0, y, FALSE, 0);
    publish(OTDWT_EV_PROXIMITY_IN, t, &P);
    pump(60);
    for (i = 0; i < 40; i++)
    {
        P.rotation = (i * 85) % 3600;   /* keeps turning throughout */
        pen_at(x0 + i * len / 40, y, i >= 20, 0.5);
        publish(OTDWT_EV_PACKET, t, &P);
        os_button(i >= 20);
        pump(15);
    }
    P.tip = 0;
    P.pressure = 0;
    publish(OTDWT_EV_PACKET, t, &P);
    os_button(FALSE);
    pump(60);                           /* hover a moment before leaving */
    publish(OTDWT_EV_PROXIMITY_OUT, t, &P);
    pump(150);
}

void demo_run(void)
{
    POINT saved;

    if (demo_active) return;
    if (!ring_ok || !canvas_rect || !canvas_rect(&R))
    {
        if (doodle_note) doodle_note("demo needs --emulate and a visible canvas");
        return;
    }
    emu_leave();
    demo_active = TRUE;
    GetCursorPos(&saved);
    x0 = R.x + 40 * R.scale;
    len = MAX(R.w - 80 * R.scale, 100);

    row = 0; step("mouse stroke (thin grey)");             mouse_stroke();
    row = 1; step("grip pen stroke");                      mouse_wiggle(x0, row_y() - 20);
                                                           pen_stroke(&tools[EMU_GRIP], TRUE);
    row = 2; step("airbrush (wheel = opacity)");           mouse_wiggle(x0, row_y() - 20);
                                                           pen_stroke(&tools[EMU_AIRBRUSH], TRUE);
    row = 3; step("back to the grip pen");                 pen_stroke(&tools[EMU_GRIP], TRUE);
    row = 4; step("leaves tip down; mouse hovers after");  pen_stroke(&tools[EMU_GRIP], FALSE);
                                                           mouse_wiggle(x0 + len / 2, row_y() + 6 * R.scale);
    row = 5; step("eraser end");                           pen_stroke(&tools[EMU_ERASER], TRUE);
    row = 6; step("two strokes, one proximity");           pen_two_strokes(&tools[EMU_GRIP]);
    row = 7; step("art pen twisting (hover, then down)");  art_pen_twisting();
    row = 8; step("mouse stroke again");                   mouse_stroke();

    os_button(FALSE);
    SetCursorPos(saved.x, saved.y);
    demo_active = FALSE;
    if (doodle_note) doodle_note("demo finished");
}
