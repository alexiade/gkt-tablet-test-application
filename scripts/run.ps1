<#
.SYNOPSIS
    Runs doodle3 (GTK 3) or doodle4 (GTK 4) with otd-wintab injected, on
    MSYS2's GTK or on a patched GTK build placed in front of it.

.DESCRIPTION
    Copies otd-wintab's dwmapi.dll proxy and otd-wintab32.dll next to the
    executable (so GTK's load of the system Wintab32.dll gets otd-wintab
    instead), sets up PATH and the otd-wintab environment, and runs the
    app. The environment of the calling shell is restored afterwards.

.EXAMPLE
    .\scripts\run.ps1                          # GTK 3, real tablet through OpenTabletDriver
    .\scripts\run.ps1 -Gtk 4 -Emulate          # GTK 4, the mouse drives an emulated pen
    .\scripts\run.ps1 -Gtk 3 -NoGlue -GtkOverride S:\repos\gtk-wintab-build\gdk, S:\repos\gtk-wintab-build\gtk
    .\scripts\run.ps1 -Gtk 4 -GtkOverride S:\repos\gtk4-wintab-build\gtk -Emulate -Script
    .\scripts\run.ps1 -SystemWintab            # the vendor's Wintab32.dll (e.g. Wacom)
    .\scripts\run.ps1 -Gtk 4 -Smoke            # print providers and devices, exit; no input
#>
param(
    [ValidateSet(3, 4)][int]$Gtk = 3,
    [string[]]$GtkOverride = @(),      # directories searched before GtkBin (patched GTK builds)
    [switch]$NoGlue,                   # OTD_WINTAB_NO_GTK_GLUE=1: test GTK itself, not otd-wintab's workarounds
    [switch]$SystemWintab,             # OTD_WINTAB_DISABLE=1: GTK gets the system Wintab32.dll
    [switch]$NoProxy,                  # don't put otd-wintab next to the exe at all (removes old copies)
    [switch]$Emulate,                  # the app feeds a private otd-wintab ring from the mouse
    [switch]$Script,                   # play the demo at startup (moves the mouse!); implies -Emulate
    [switch]$Smoke,                    # print what GTK sees and exit after a second; no input
    [switch]$Gimp,                     # GTK 3: GIMP's device modes and stroke grab
    [ValidateSet('', 'wintab', 'winpointer', 'none', 'default')][string]$Api = '',
    [switch]$Log,                      # otd-wintab log to build\doodle<N>-wintab.log, events to stdout
    [string]$OtdWintab = 'S:\repos\otd-wintab\build',
    [string]$GtkBin = 'C:\msys64\clang64\bin',
    [Parameter(ValueFromRemainingArguments)][string[]]$Rest = @()
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$exe = Join-Path $build "doodle$Gtk.exe"
if (-not (Test-Path $exe)) { throw "$exe not found; run .\scripts\build.ps1 first" }

foreach ($f in 'dwmapi.dll', 'otd-wintab32.dll') {
    $dst = Join-Path $build $f
    if ($NoProxy) { Remove-Item $dst -ErrorAction SilentlyContinue; continue }
    $src = Join-Path $OtdWintab $f
    if (-not (Test-Path $src)) { throw "$src not found; build otd-wintab or pass -OtdWintab" }
    $old = Get-Item $dst -ErrorAction SilentlyContinue
    $new = Get-Item $src
    if (-not $old -or $old.LastWriteTime -ne $new.LastWriteTime -or $old.Length -ne $new.Length) {
        Copy-Item $src $dst -Force
    }
}

$dirs = @($GtkOverride) + $GtkBin
$dll = if ($Gtk -eq 3) { 'libgdk-3-0.dll' } else { 'libgtk-4-1.dll' }
foreach ($d in $dirs) {
    $p = Join-Path $d $dll
    if (Test-Path $p) { Write-Host "GTK: $p ($((Get-Item $p).LastWriteTime))"; break }
}

$a = @()
if ($Emulate -or $Script) { $a += '--emulate' }
if ($Script) { $a += '--script' }
if ($Smoke) { $a += '--smoke' }
if ($NoGlue) { $a += '--no-glue' }
if ($Gimp) { $a += '--gimp' }
if ($Api) { $a += "--api=$Api" }
if ($Log) { $a += '--log-stdout' }
$a += $Rest

$vars = 'PATH', 'OTD_WINTAB_DISABLE', 'OTD_WINTAB_NO_GTK_GLUE', 'OTD_WINTAB_INSTANCE', 'OTD_WINTAB_LOG', 'OTD_WINTAB_FAKE'
$saved = @{}
foreach ($v in $vars) { $saved[$v] = [Environment]::GetEnvironmentVariable($v, 'Process') }
try {
    $env:PATH = ($dirs + $env:PATH) -join ';'
    foreach ($v in $vars | Select-Object -Skip 1) { [Environment]::SetEnvironmentVariable($v, $null, 'Process') }
    # set here rather than with --system-wintab, which makes the app restart itself
    if ($SystemWintab) { $env:OTD_WINTAB_DISABLE = '1' }
    if ($Log) { $env:OTD_WINTAB_LOG = Join-Path $build "doodle$Gtk-wintab.log"; Write-Host "otd-wintab log: $env:OTD_WINTAB_LOG" }
    & $exe @a
    $code = $LASTEXITCODE
} finally {
    foreach ($v in $vars) { [Environment]::SetEnvironmentVariable($v, $saved[$v], 'Process') }
}
exit $code
