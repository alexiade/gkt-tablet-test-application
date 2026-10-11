#!/bin/bash
# Builds portable Windows packages of the GTK 3 and GTK 4 front ends: a zip
# per GTK version with the executable, the MSYS2 CLANG64 GTK runtime it was
# built against, otd-wintab, and the licenses of everything bundled.
#
#   scripts/package.sh [otd-wintab build dir]
#
# Run in MSYS2 (scripts/package.ps1 does that) after scripts/build.ps1.
# The otd-wintab build dir defaults to ../otd-wintab/build. Output goes to
# dist/.
export MSYSTEM=CLANG64
source /etc/profile >/dev/null 2>&1 || true
set -e

root="$(cd "$(dirname "$0")/.." && pwd)"
otd="$(cygpath -u "${1:-$root/../otd-wintab/build}")"
otd_src="$(cd "$otd/.." && pwd)"
version="$(sed -n "s/^ *version: '\([^']*\)'.*/\1/p" "$root/meson.build" | head -1)"
prefix=/clang64
dist="$root/dist"

for f in "$otd/dwmapi.dll" "$otd/otd-wintab32.dll"; do
  [ -f "$f" ] || { echo "missing $f: build otd-wintab first" >&2; exit 1; }
done

# The MSYS2 DLLs a module needs, transitively
deps() {
  ldd "$@" | awk '{print $3}' | grep -i "^$prefix/" | sort -u
}

package() {
  local gtk="$1" exe="$root/build/doodle$1.exe"
  local name="gtk-tablet-test-$version-gtk$gtk-win64"
  local out="$dist/$name"
  local loaders=$prefix/lib/gdk-pixbuf-2.0/2.10.0
  local gtkpkg; [ "$gtk" = 3 ] && gtkpkg=gtk3 || gtkpkg=gtk4

  [ -f "$exe" ] || { echo "missing $exe: run scripts/build.ps1 first" >&2; exit 1; }
  echo "== $name"
  rm -rf "$out" "$out.zip"
  mkdir -p "$out/bin" "$out/lib/gdk-pixbuf-2.0/2.10.0/loaders" \
           "$out/share/glib-2.0/schemas" "$out/share/icons" "$out/licenses"

  # The executable and otd-wintab: the dwmapi.dll proxy makes GTK load
  # otd-wintab32.dll instead of the system's Wintab32.dll
  cp "$exe" "$out/bin/"
  cp "$otd/dwmapi.dll" "$otd/otd-wintab32.dll" "$out/bin/"

  # Image loaders (SVG for the symbolic icons) and everything they and the
  # executable link to
  cp "$loaders/loaders.cache" "$out/lib/gdk-pixbuf-2.0/2.10.0/"
  cp "$loaders"/loaders/*.dll "$out/lib/gdk-pixbuf-2.0/2.10.0/loaders/"
  deps "$exe" "$loaders"/loaders/*.dll > "$out/dlls.txt"
  # GTK 4 needs the Vulkan loader, which not every Windows has
  [ "$gtk" = 4 ] && echo "$prefix/bin/vulkan-1.dll" >> "$out/dlls.txt"
  xargs -a "$out/dlls.txt" cp -t "$out/bin/"

  # GTK's settings schemas and the icon themes
  if [ "$gtk" = 3 ]; then
    cp $prefix/share/glib-2.0/schemas/org.gtk.Settings.*.gschema.xml "$out/share/glib-2.0/schemas/"
  else
    cp $prefix/share/glib-2.0/schemas/org.gtk.gtk4.Settings.*.gschema.xml "$out/share/glib-2.0/schemas/"
  fi
  glib-compile-schemas "$out/share/glib-2.0/schemas"
  cp -r $prefix/share/icons/Adwaita $prefix/share/icons/hicolor "$out/share/icons/"

  # Licenses: the MSYS2 package of every bundled DLL
  {
    echo "Third-party components bundled with gtk-tablet-test $version (GTK $gtk)."
    echo "They come from MSYS2's CLANG64 packages; their license files are in"
    echo "the directories named below. Sources: https://packages.msys2.org"
    echo
    printf '%-44s %-24s %s\n' Package Version License
    for f in $(cat "$out/dlls.txt") "$loaders"/loaders/*.dll; do pacman -Qoq "$f"; done | sort -u |
    while read -r pkg; do
      ver="$(pacman -Q "$pkg" | cut -d' ' -f2)"
      lic="$(pacman -Qi "$pkg" | sed -n 's/^Licenses *: //p')"
      short="${pkg#mingw-w64-clang-x86_64-}"
      if [ -d "$prefix/share/licenses/$short" ]; then
        printf '%-44s %-24s %s\n' "$short" "$ver" "$lic"
        cp -r "$prefix/share/licenses/$short" "$out/licenses/"
      else
        # MSYS2 ships no license files for some packages (GStreamer)
        printf '%-44s %-24s %s (text: LGPL-2.1.txt)\n' "$short" "$ver" "$lic"
      fi
    done
    echo
    printf '%-44s %-24s %s\n' otd-wintab "$(git -C "$otd_src" describe --always 2>/dev/null)" LGPL-2.1-or-later
  } > "$out/licenses/THIRD-PARTY.txt"
  mkdir -p "$out/licenses/otd-wintab"
  cp "$otd_src/COPYING" "$out/licenses/otd-wintab/"
  cp "$root/COPYING" "$out/licenses/gtk-tablet-test-COPYING"
  cp "$root/COPYING" "$out/licenses/LGPL-2.1.txt"
  rm "$out/dlls.txt"

  # Launcher and instructions
  printf '@echo off\r\nstart "" "%%~dp0bin\\doodle%s.exe" %%*\r\n' "$gtk" > "$out/gtk-tablet-test-gtk$gtk.cmd"
  sed -e "s/@GTK@/$gtk/g" -e "s/@VERSION@/$version/g" \
      -e "s/@GTKVERSION@/$(pacman -Q mingw-w64-clang-x86_64-$gtkpkg | cut -d' ' -f2)/g" \
      "$root/scripts/README-portable.txt" | sed 's/$/\r/' > "$out/README.txt"

  (cd "$dist" && bsdtar -a -cf "$name.zip" "$name")
  echo "   $(du -sh "$out" | cut -f1) unpacked, $(du -h "$dist/$name.zip" | cut -f1) zipped"
}

mkdir -p "$dist"
package 3
package 4
echo "Packages are in $(cygpath -w "$dist")"
