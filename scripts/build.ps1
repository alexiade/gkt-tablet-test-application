<#
.SYNOPSIS
    Builds doodle3.exe (GTK 3) and doodle4.exe (GTK 4) into build\ with
    MSYS2's CLANG64 toolchain (packages: mingw-w64-clang-x86_64-gtk3,
    -gtk4, -meson, -ninja, -pkgconf).

.EXAMPLE
    .\scripts\build.ps1
    .\scripts\build.ps1 -Clean
    .\scripts\build.ps1 -Dgtk3=disabled     # extra arguments go to meson
#>
param(
    [string]$Msys = 'C:\msys64',
    [switch]$Clean,
    [Parameter(ValueFromRemainingArguments)][string[]]$MesonArgs = @()
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if ($Clean) { Remove-Item -Recurse -Force (Join-Path $root 'build') -ErrorAction SilentlyContinue }
$script = & "$Msys\usr\bin\cygpath.exe" -u (Join-Path $PSScriptRoot 'build.sh')
& "$Msys\usr\bin\bash.exe" -lc "'$script' $($MesonArgs -join ' ')"
exit $LASTEXITCODE
