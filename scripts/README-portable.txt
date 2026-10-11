gtk-tablet-test @VERSION@ for GTK @GTK@ (portable, Windows 64-bit)
=================================================================

A small drawing application that shows what GTK @GTK@ makes of drawing tablet
input on Windows: which devices and tools it reports, every axis of every
event (pressure, tilt, rotation, wheel), and how strokes come out on the
canvas. It is meant as a benchmark of GTK's tablet support.

https://github.com/alexiade/gkt-tablet-test-application

This package includes GTK @GTKVERSION@ (from MSYS2) and everything it needs.
Nothing is installed; delete the folder to remove it.


Running it
----------

Double-click gtk-tablet-test-gtk@GTK@.cmd, or run bin\doodle@GTK@.exe with any
of these options (bin\doodle@GTK@.exe --help lists them all):

  (no options)        Tablet through OpenTabletDriver with its otd-wintab
                      output plugin (https://github.com/alexiade/otd-wintab).
  --system-wintab     Tablet through your tablet vendor's driver (Wacom,
                      Huion, XP-Pen, ...): uses the Wintab32.dll it installed.
  --emulate           No tablet: the mouse drives an emulated pen. Pick the
                      tool, pressure, tilt, rotation and wheel in the panel.
  --emulate --script  Plays a scripted test of pens, eraser, airbrush and
                      Art Pen. It moves the mouse; don't touch it meanwhile.
  --api=winpointer    Use Windows Ink instead of Wintab, for comparison.

GTK uses Windows Ink by default; the application selects Wintab unless you
pass --api.


Testing another GTK build
-------------------------

Copy the GTK DLLs of your build (libgtk-@GTK@*.dll and, for GTK 3, libgdk-3-0.dll)
over the ones in bin\. They must be built with MSYS2 CLANG64 against
compatible library versions.


License
-------

gtk-tablet-test is free software under the GNU Lesser General Public License,
version 2.1 or later, like GTK. The bundled components and their licenses
are listed in licenses\THIRD-PARTY.txt.
