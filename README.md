# gtk-tablet-doodle

A tiny drawing app for Windows that shows what GTK's Wintab tablet
support delivers to an application, in a GTK 3 (`doodle3`) and a GTK 4
(`doodle4`) flavour. It is meant for testing GTK patches and for
demonstrating tablet bugs in GTK merge requests: every stroke on the
canvas shows which device or tool GTK attributed it to, with which axes,
and a side panel lists the devices, the last event and an event log you
can copy into a report.

It works with a real tablet (through
[OpenTabletDriver](https://opentabletdriver.net) and
[otd-wintab](https://github.com/alexiade/otd-wintab), or through a vendor
Wintab driver such as Wacom's) and with an **emulated** one: the app can
itself act as the OpenTabletDriver side of otd-wintab and turn the mouse
into a pen with any of several tools, so tablet bugs can be reproduced on
a machine without a tablet.

## Download

Portable builds for 64-bit Windows are on the
[releases page](https://github.com/alexiade/gkt-tablet-test-application/releases/latest),
one zip for GTK 3 and one for GTK 4. Each contains the app, the GTK
runtime it was built with, and otd-wintab; unpack it anywhere and run
`gtk-tablet-test-gtk3.cmd` or `gtk-tablet-test-gtk4.cmd`. The `README.txt`
inside explains the options for real tablets, vendor drivers and the
emulated pen.

## Layout

```
src/common/doodle.[ch]      options, provider detection, canvas, emulator, demo
src/common/doodle-gtk.[ch]  status line, emulator panel, event log (built per GTK version)
src/common/ring.[ch]        otd-wintab ring producer, vendored from otd-wintab
src/common/otdwt_ring.h     otd-wintab ring protocol, vendored from otd-wintab
src/gtk3/doodle3.c          GTK 3 front end: GdkDevice per stylus, device modes
src/gtk4/doodle4.c          GTK 4 front end: GdkDeviceTool per stylus, GtkGestureStylus
scripts/build.ps1           build with MSYS2 CLANG64 (calls scripts/build.sh)
scripts/run.ps1             run against stock or patched GTK, with otd-wintab injected
scripts/package.ps1         portable zips with the GTK runtime (calls scripts/package.sh)
```

## Building

Needs MSYS2 with the CLANG64 packages `mingw-w64-clang-x86_64-gtk3`,
`-gtk4`, `-meson`, `-ninja` and `-pkgconf`.

```powershell
.\scripts\build.ps1            # build\doodle3.exe and build\doodle4.exe
.\scripts\build.ps1 -Clean
```

The front end for a GTK version that isn't installed is skipped
(`-Dgtk3=disabled` / `-Dgtk4=disabled` to choose).

### Portable packages

```powershell
.\scripts\build.ps1; .\scripts\package.ps1
```

This puts `dist\gtk-tablet-test-<version>-gtk3-win64.zip` and `-gtk4-` next
to each other: the executable with every MSYS2 DLL it links (found with
`ldd`), the gdk-pixbuf loaders, GTK's settings schemas, the Adwaita and
hicolor icon themes, otd-wintab's `dwmapi.dll` proxy and provider (from
`-OtdWintab`, default `..\otd-wintab\build`), and the license of every
bundled MSYS2 package with a `THIRD-PARTY.txt` overview. The README in the
zips is `scripts\README-portable.txt`.

## Running

`scripts\run.ps1` copies otd-wintab's `dwmapi.dll` proxy and
`otd-wintab32.dll` next to the executable (from `-OtdWintab`, default
`..\otd-wintab\build`: an otd-wintab checkout next to this one), puts `-GtkOverride` directories and then
`C:\msys64\clang64\bin` in front of `PATH`, sets the otd-wintab
environment and runs the app. The calling shell's environment is restored
afterwards.

| What | Command |
|---|---|
| GTK 3, real tablet via OpenTabletDriver + otd-wintab | `.\scripts\run.ps1 -Gtk 3` |
| GTK 4, same | `.\scripts\run.ps1 -Gtk 4` |
| the vendor's Wintab32.dll (e.g. Wacom) | `.\scripts\run.ps1 -Gtk 3 -SystemWintab` |
| emulated tablet, mouse is the pen | `.\scripts\run.ps1 -Gtk 4 -Emulate` |
| emulated tablet, play the demo at startup | `.\scripts\run.ps1 -Gtk 3 -Emulate -Script` |
| GTK 3 itself, without otd-wintab's GTK 3 workarounds | add `-NoGlue` |
| a GTK 3 build | `-GtkOverride <gtk3-build>\gdk, <gtk3-build>\gtk` |
| a GTK 4 build | `-GtkOverride <gtk4-build>\gtk` |
| GIMP's device modes and stroke grab (GTK 3) | add `-Gimp` |
| Windows Ink instead of Wintab | add `-Api winpointer` |
| otd-wintab log + event log on stdout | add `-Log` |
| print providers and devices, exit; no input | add `-Smoke` |

For example, stock GTK 3 against a patched one, both without the glue,
with the emulator:

```powershell
.\scripts\run.ps1 -Gtk 3 -NoGlue -Emulate
.\scripts\run.ps1 -Gtk 3 -NoGlue -Emulate -GtkOverride <gtk3-build>\gdk, <gtk3-build>\gtk
```

otd-wintab's `scripts\build-gtk.ps1` builds GTK 3 or 4 from a checkout
and prints the directories to pass to `-GtkOverride`; see its
[docs/gtk-testing.md](https://github.com/alexiade/otd-wintab/blob/main/docs/gtk-testing.md).

The executables take the same options directly (`doodle3 --help`):
`--emulate`, `--instance=NAME`, `--script`, `--system-wintab`, `--no-glue`,
`--api=wintab|winpointer|none|default`, `--gimp`, `--log-stdout`,
`--smoke`. Both GTK 3 and GTK 4 prefer Windows Ink by default, so the app
sets `GDK_WIN32_TABLET_INPUT_API=wintab` unless told otherwise.
`--system-wintab` makes the app restart itself, because the dwmapi proxy
reads `OTD_WINTAB_DISABLE` before `main()` runs.

The status line at the top of the side panel says which GTK DLL and which
Wintab provider were actually loaded (`otd-wintab32.dll` next to the exe,
or the system `Wintab32.dll`), whether the dwmapi proxy is active, and
which ring the tablet comes from.

### Real tablet

With OpenTabletDriver running otd-wintab's *Wintab Absolute Mode*, the pen
arrives through the default ring. With `-SystemWintab` the proxy steps
aside and GTK gets the system's `Wintab32.dll`, whatever driver installed
it.

### Emulated tablet

With `-Emulate` the app creates its own ring (`OTD_WINTAB_INSTANCE=doodle-<pid>`,
set before GTK loads Wintab, so it never mixes with a running
OpenTabletDriver) and becomes the tablet driver:

* While the cursor is over the canvas the selected tool is in proximity;
  leaving the canvas (or holding **Shift**) takes it out of range.
* The real left mouse button is the tip, with the pressure from the
  slider (optionally ramping up over half a second), and the rotation,
  tilt and wheel sliders apply to every packet. *Spin the barrel* keeps
  the Art Pen turning.
* Tools: Grip Pen #A001, its eraser end, Airbrush #B002, 6D Art Pen #C003
  and a second Grip Pen #A0F2.

This mirrors what OpenTabletDriver does: the system cursor is where the
pen is and the real left button is held while the tip is down, so GTK
receives both Wintab packets and ordinary mouse messages, exactly as with
a real tablet. The emulator reads the raw cursor position and button state
on a timer (`GetCursorPos`, `GetAsyncKeyState`), never GTK events, so it
does not depend on the GTK behaviour under test. Leaving the canvas with
the button still held is how you make a pen leave range with its tip down.

**Play demo** (or `-Script`) plays the scenario of otd-wintab's GTK
integration tests, one row of the canvas per step, by moving the real
mouse with `SendInput` and pressing the left button like OpenTabletDriver:
mouse stroke, grip pen, airbrush, grip pen again, pen leaving range with
the tip down followed by mouse hovering, eraser, two strokes in one
proximity, Art Pen twisting, mouse stroke. Don't touch the mouse for the
ten seconds it takes.

## Reading the canvas

* **Pen strokes** are drawn in a colour that belongs to the device (GTK 3)
  or tool (GTK 4), the same colour as its dot in the side panel; width
  follows pressure. A pen stroke **without a pressure axis** is dashed.
* **Mouse strokes** are thin and grey.
* **Eraser** strokes are a pale pink trail.
* **Rotation** draws a dark tick across the line at the nib angle,
  **tilt** a short blue vector, the **airbrush wheel** sets the opacity.
* **Proximity** in and out (when GTK delivers those events) are a green
  circle and a red cross.

The counters under the last-event readout count the bug classes below.

## What to look for

* **Pen events turning into mouse events during a stroke.** A coloured
  stroke that turns into a thin grey line partway, *source changed
  mid-stroke* above zero, or *button 1 held: N mouse motions* during pen
  strokes. With `--gimp` (GTK 3) the device list selection jumping to the
  core pointer while drawing is the same thing as GIMP sees it.
* **Pressure lost on the second stroke.** In demo step 7 the second
  stroke is a hairline or dashed while the first one has width; *pen
  without pressure* counts such motions.
* **Wrong or unknown device.** The stroke has the colour of a different
  stylus than the one used, the device or tool is missing from the list
  or marked *only seen in events*, or the two Grip Pens (#A001, #A0F2)
  share one colour.
* **Eraser not distinguished.** The eraser end draws in a pen colour
  instead of pink: GTK reported it as a pen (GTK 3 `GDK_SOURCE_PEN`
  instead of `GDK_SOURCE_ERASER`, GTK 4 a tool that isn't
  `GDK_DEVICE_TOOL_TYPE_ERASER`).
* **Rotation or wheel missing.** No ticks on Art Pen strokes, a fully
  opaque airbrush stroke, or the axis absent from the device's/tool's
  axis list.
* **Stuck tip after leaving range.** After demo step 5 the mouse only
  hovers, so nothing should be drawn; a grey line, motions marked `[B1]`
  in the log, or *held without press* above zero mean GTK still thinks the
  button is down.

### GTK 3 and GTK 4 axes

* GTK 3 reports the Art Pen's barrel rotation (Wintab orientation twist)
  and the airbrush wheel (tangential pressure) both on `GDK_AXIS_WHEEL`,
  normalised to 0..1. Only the device tells them apart, so `doodle3`
  treats the wheel axis of a device whose name contains "Airbrush" as the
  wheel and everything else's as rotation (value × 360°).
* GTK 4 has separate axes: rotation is `GDK_AXIS_ROTATION` (0..1 of a
  turn), and the airbrush wheel is `GDK_AXIS_WHEEL` in GTK's Wintab code
  (Wayland uses `GDK_AXIS_SLIDER`; `doodle4` accepts either). An axis
  counts as present only if the event's tool lists it in its axis flags,
  because `gdk_event_get_axis()` returns TRUE for any axis of an event
  that has axes at all.
* In GTK 4 the event device is always the logical pointer; the pen is
  identified by `gdk_event_get_device_tool()`. The log also shows what a
  `GtkGestureStylus` on the canvas sees (`stylus down`/`up`), which is
  what most GTK 4 drawing applications build on.

## License

LGPL-2.1-or-later, see [COPYING](COPYING). `src/common/ring.[ch]` and
`otdwt_ring.h` come from otd-wintab under the same license.
