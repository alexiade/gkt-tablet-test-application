<#
.SYNOPSIS
    Builds the portable packages (dist\gtk-tablet-test-<version>-gtk3/gtk4-win64.zip)
    after scripts\build.ps1. A wrapper around scripts\package.sh.

.EXAMPLE
    .\scripts\build.ps1; .\scripts\package.ps1
    .\scripts\package.ps1 -OtdWintab D:\src\otd-wintab\build
#>
param(
    [string]$OtdWintab = (Join-Path $PSScriptRoot '..\..\otd-wintab\build'),  # an otd-wintab checkout next to this one
    [string]$Msys = 'C:\msys64'
)

$ErrorActionPreference = 'Stop'
$bash = Join-Path $Msys 'usr\bin\bash.exe'
if (-not (Test-Path $bash)) { throw "MSYS2 not found at $Msys; pass -Msys" }
$script = (Join-Path $PSScriptRoot 'package.sh') -replace '\\', '/'
& $bash -lc "'$script' '$((Resolve-Path $OtdWintab).Path -replace '\\', '/')'"
exit $LASTEXITCODE
