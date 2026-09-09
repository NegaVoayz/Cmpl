# scripts/build_win_cross.ps1 -- link the COFF objects produced by
# scripts/build_win_cross.sh (WSL clang, x86_64-pc-windows-msvc target)
# into build\win-cross\cmpl.exe with the native MSVC linker.
#
# Usage: powershell -File scripts\build_win_cross.ps1

$ErrorActionPreference = "Stop"
$ROOT = Split-Path -Parent $PSScriptRoot
$OBJ  = "$ROOT\build\win-cross"

if (-not (Test-Path $OBJ)) { Write-Host "no $OBJ -- run scripts/build_win_cross.sh first"; exit 1 }
$objects = Get-ChildItem "$OBJ\*.o" -ErrorAction SilentlyContinue
if (-not $objects) { Write-Host "no objects in $OBJ"; exit 1 }

$VSDEV = @(
    'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $VSDEV) { Write-Host "vcvars64.bat not found"; exit 1 }

# 208 object paths exceed the cmd.exe line limit: hand them to link.exe in
# a response file (one quoted path per line)
$rsp = "$OBJ\link.rsp"
$lines = $objects | ForEach-Object { '"' + $_.FullName + '"' }
$lines += 'libcmt.lib', 'libvcruntime.lib', 'libucrt.lib', 'oldnames.lib', 'kernel32.lib'
Set-Content -Path $rsp -Value $lines -Encoding ASCII

$cmd = 'call "' + $VSDEV + '" >nul && link /nologo /INCREMENTAL:NO @' + $rsp +
       ' /OUT:"' + $OBJ + '\cmpl.exe"'

Push-Location $OBJ
cmd /c $cmd 2>&1 | Select-Object -Last 10
$rc = $LASTEXITCODE
Pop-Location

if ($rc -ne 0) { Write-Host "FAIL: link cmpl.exe"; exit 1 }
Write-Host ("OK: {0} objects -> {1}\cmpl.exe" -f $objects.Count, $OBJ)
