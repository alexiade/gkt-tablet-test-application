/*
 * gtk-tablet-doodle: toolkit-independent parts shared by the GTK 3 and
 * GTK 4 front ends: options and environment, Wintab provider detection,
 * the canvas, and the tablet emulator that feeds otd-wintab's ring.
 *
 * Copyright 2026 gtk-tablet-doodle contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef DOODLE_H
#define DOODLE_H

#include <glib.h>
#include <cairo.h>

typedef struct
{
    gboolean emulate;        /* --emulate: we are the tablet driver */
    gboolean script;         /* --script: play the demo at startup */
    gboolean smoke;          /* --smoke: print what GTK sees and exit; no input */
    gboolean system_wintab;  /* --system-wintab: OTD_WINTAB_DISABLE=1 */
    gboolean gimp;           /* --gimp: GIMP's device modes and stroke grab (GTK 3) */
    gboolean log_stdout;     /* --log-stdout: event log on stdout too */
} DoodleOptions;

extern DoodleOptions opt;

/* Call first thing in main(), before GTK loads Wintab: parses and removes
 * our options and sets up the environment. Returns -1 to carry on, or an
 * exit code (after --help, or after re-running itself for --system-wintab,
 * which must be in the environment before the dwmapi proxy loads). */
int doodle_init(int *argc, char **argv, const char *frontend);

char *doodle_status(void);                 /* provider and environment; g_free */
char *doodle_module_path(const char *dll); /* path of a loaded DLL or NULL; g_free */
double doodle_now(void);                   /* seconds since start */
void doodle_color(const char *key, double rgb[3]);   /* stable colour per identity */
const char *doodle_color_hex(const char *key);       /* "#rrggbb", static buffer */
extern void (*doodle_note)(const char *text);         /* demo progress; front ends log it */
/* --smoke: move a window to the bottom-right corner, behind others, without
 * activating it, so it stays out of the way of anything else using the mouse */
void doodle_park_window(void *hwnd);

/* ---- canvas ----------------------------------------------------------- */

enum { DOODLE_MOUSE, DOODLE_PEN, DOODLE_ERASER };

typedef struct
{
    double x, y;             /* canvas (widget) coordinates */
    int kind;                /* DOODLE_MOUSE / _PEN / _ERASER */
    double rgb[3];           /* identity colour of the device or tool */
    double pressure;         /* 0..1; < 0: the event has no pressure axis */
    gboolean has_tilt;
    double tilt_x, tilt_y;   /* -1..1 */
    double wheel;            /* 0..1 airbrush wheel; < 0: none */
    double rotation;         /* degrees; < 0: none */
} DoodleSample;

/* " p=0.523 tilt=+0.10,-0.20 wheel=0.600 rot=85" for the axes present */
void doodle_axes_text(char *buf, size_t size, const DoodleSample *s);

void canvas_set_size(int width, int height, int scale);
void canvas_paint(cairo_t *cr);
void canvas_clear(void);
void canvas_segment(const DoodleSample *a, const DoodleSample *b);
void canvas_mark(double x, double y, gboolean proximity_in);
void canvas_label(double x, double y, const char *text);
extern void (*canvas_changed)(void);       /* set by the front end: queue a redraw */

/* ---- tablet emulator -------------------------------------------------- */

typedef struct { int x, y, w, h, scale; } DoodleScreenRect;  /* physical pixels */
typedef gboolean (*DoodleRectFunc)(DoodleScreenRect *r);

enum { EMU_GRIP, EMU_ERASER, EMU_AIRBRUSH, EMU_ARTPEN, EMU_GRIP2, EMU_N_TOOLS };
extern const char *const emu_tool_labels[EMU_N_TOOLS + 1];

typedef struct
{
    gboolean enabled;
    int tool;                /* EMU_* */
    double pressure;         /* 0..1 */
    gboolean ramp;           /* pressure rises over half a second after the tip goes down */
    double rotation;         /* degrees */
    gboolean spin;           /* keep turning the barrel */
    double tilt_x, tilt_y;   /* degrees */
    double wheel;            /* 0..1 */
} EmuControls;

extern EmuControls emu;

gboolean emu_available(void);              /* --emulate and the ring exists */
void emu_start(DoodleRectFunc canvas_rect);
const char *emu_describe(void);
gboolean demo_running(void);
void demo_run(void);                       /* moves the real mouse; blocks while pumping */
/* --smoke --emulate: each emulated tool enters and leaves proximity over
 * the canvas, through the ring only (no OS input), so its device shows up */
void emu_announce_tools(void);

#endif
