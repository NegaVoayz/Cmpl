# scripts/gpu_demo.ps1 -- CMPL's GPU path end to end on Windows.
#
#   demo/matmul.c       --cmpl -gpu-->  <base>.host.ll + <base>.device.spv
#   demo/matmul_load.c    --clang------->  a native COFF object for the host
#                        --cl / link--->  <base>_demo.exe (demo Vulkan
#                                         runtime + the Vulkan loader import
#                                         library)
#                        --run--------->  the kernel executes on the Vulkan
#                                         device (prefers a discrete GPU) and
#                                         the result is compared with a CPU
#                                         reference
#
# demo/matmul.c is the correctness smoke test; demo/matmul_load.c is the
# heavy one (4x4 register-tiled n x n multiply, CMPL_DEMO_N /
# CMPL_DEMO_REPEAT override the size, reports device time and GFLOP/s).
#
# Mirrors scripts/gpu_demo.sh for the WSL/Linux side.  Requirements:
#   * build\win\cmpl.exe (or build\win-cross\cmpl.exe, build\bootstrap\cmpl.exe)
#   * clang (LLVM for Windows, e.g. C:\Program Files\LLVM\bin\clang.exe)
#   * the Vulkan SDK (headers + vulkan-1.lib) and a Vulkan driver
#   * Visual Studio C++ tools (cl.exe / link.exe)
#
# Usage: powershell -File scripts\gpu_demo.ps1 [-Quiet] [-Small]
param([switch]$Quiet, [switch]$Small)

$ErrorActionPreference = "Stop"
$ROOT = Split-Path -Parent $PSScriptRoot

function Say($msg) { if (-not $Quiet) { Write-Host $msg } }
function Bad($msg) { Write-Host "FAIL (gpu demo): $msg"; exit 1 }

# ---- locate the tools -----------------------------------------------------

$CMPL = @("$ROOT\build\win\cmpl.exe", "$ROOT\build\win-cross\cmpl.exe",
          "$ROOT\build\bootstrap\cmpl.exe", "$ROOT\build2\cmpl.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $CMPL) { Bad "no cmpl.exe (cmake build, or scripts\build_win_cross.sh + .ps1)" }

$clangCmd = Get-Command clang -ErrorAction SilentlyContinue
$CLANG = if ($clangCmd) { $clangCmd.Source } else { $null }
if (-not $CLANG -and (Test-Path 'C:\Program Files\LLVM\bin\clang.exe')) {
    $CLANG = 'C:\Program Files\LLVM\bin\clang.exe'
}
$CLANG_WSL = $false
if (-not $CLANG) {
    # no native clang: the Linux clang can still emit the COFF object for the
    # LLVM IR (no CRT headers are needed to compile .ll), which keeps the
    # whole compile+link+run native apart from that one step
    $wslClang = (& wsl -e bash -lc 'command -v clang') 2>$null
    if (-not $wslClang) { Bad "no clang (install LLVM: winget install LLVM.LLVM)" }
    $CLANG_WSL = $true
    Say "  (no native clang: using the WSL clang for the .ll -> .obj step)"
}

$VK = $env:VULKAN_SDK
if (-not $VK) {
    $VK = Get-ChildItem 'C:\VulkanSDK' -Directory -ErrorAction SilentlyContinue |
          Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $VK -or -not (Test-Path "$VK\Include\vulkan\vulkan.h")) {
    Bad "Vulkan SDK not found (headers + Lib\vulkan-1.lib)"
}
if (-not (Test-Path "$VK\Lib\vulkan-1.lib")) { Bad "no $VK\Lib\vulkan-1.lib" }

# cl.exe/link.exe need the MSVC environment
$VSDEV = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $VSDEV)) { Bad "vcvars64.bat not found" }

# the demo runtime: every demo/vk_rt_*.c source and its object file
$RT_FILES = Get-ChildItem "$ROOT\demo\vk_rt_*.c"
$RT_SRC   = ($RT_FILES | ForEach-Object { '"' + $_.FullName + '"' }) -join ' '
$RT_OBJ   = ($RT_FILES | ForEach-Object { '"' + $_.BaseName + '.obj"' }) -join ' '

# ---- one demo -------------------------------------------------------------

function Run-Demo($base, $pat) {
    $TMP = Join-Path $env:TEMP ("cmpl_demo_" + $base + "_" + [guid]::NewGuid().ToString("N").Substring(0, 8))
    New-Item -ItemType Directory -Path $TMP | Out-Null
    Copy-Item "$ROOT\demo\$base.c" "$TMP\$base.c"

    Say "### $base.c: compile with cmpl -gpu ###"
    Push-Location $TMP
    & $CMPL -gpu -I"$ROOT\include" -I"$ROOT" "$base.c" > "$TMP\cmpl.log" 2>&1
    $rc = $LASTEXITCODE
    Pop-Location
    if ($rc -ne 0) { Get-Content "$TMP\cmpl.log" | Select-Object -Last 5; Bad "${base}: cmpl -gpu failed" }
    foreach ($f in "$base.host.ll", "$base.device.spv") {
        if (-not (Test-Path "$TMP\$f")) { Bad "${base}: no $f emitted" }
    }
    Say ("  {0}.host.ll    {1} bytes" -f $base, (Get-Item "$TMP\$base.host.ll").Length)
    Say ("  {0}.device.spv {1} bytes" -f $base, (Get-Item "$TMP\$base.device.spv").Length)

    # ---- the device module + the host launch ------------------------------
    $DIS = "$VK\Bin\spirv-dis.exe"
    if (Test-Path $DIS) {
        Say ""
        Say "### $base.c: the device module cmpl emitted ###"
        & $DIS "$TMP\$base.device.spv" |
            Select-String -Pattern 'OpCapability|OpEntryPoint|OpExecutionMode|OpDecorate .* Block|Offset' |
            ForEach-Object { Say ("  " + $_.Line.Trim()) }
    }
    Say ""
    Say "### $base.c: the host launch cmpl emitted ###"
    Select-String -Path "$TMP\$base.host.ll" -Pattern 'call void .*cmpl_vk_launch' -List |
        ForEach-Object { Say ("  " + $_.Line.Trim()) }

    # ---- validate --------------------------------------------------------
    Say ""
    Say "### $base.c: validate the SPIR-V ###"
    $VAL = "$VK\Bin\spirv-val.exe"
    if (Test-Path $VAL) {
        & $VAL --target-env vulkan1.2 "$TMP\$base.device.spv"
        if ($LASTEXITCODE -ne 0) { Bad "${base}: spirv-val rejected the module" }
        Say "  spirv-val: OK"
    }
    & python "$ROOT\scripts\spirv_check.py" "$TMP\$base.device.spv"
    if ($LASTEXITCODE -ne 0) { Bad "${base}: spirv_check.py rejected the module" }
    & python "$ROOT\scripts\spirv_localsize.py" "$TMP\$base.device.spv"
    if ($LASTEXITCODE -ne 0) { Bad "${base}: LocalSize/blockDim mismatch" }

    # ---- compile + link natively -----------------------------------------
    Say ""
    Say "### $base.c: compile and link natively (clang + cl + link) ###"
    $HOSTOBJ = "$TMP\${base}_host.obj"
    # the module carries the Linux triple: retarget it for the Windows ABI.
    # cmd /c keeps a native tool's stderr from becoming a PowerShell error record.
    if ($CLANG_WSL) {
        $winTmp = (& wsl -e wslpath -a "$TMP") 2>$null
        cmd /c ('wsl -e bash -lc "cd ' + $winTmp + ' && clang -O2 ' +
                '--target=x86_64-pc-windows-msvc -c ' + $base + '.host.ll -o ' +
                $base + '_host.obj 2>&1"') |
            Where-Object { $_ -notmatch 'overriding the module target triple|warning generated' } |
            ForEach-Object { Say ("  " + $_) }
    } else {
        cmd /c ('"' + $CLANG + '" -O2 --target=x86_64-pc-windows-msvc -c "' + $TMP +
                '\' + $base + '.host.ll" -o "' + $HOSTOBJ + '" 2>&1') |
            Where-Object { $_ -notmatch 'overriding the module target triple' } |
            ForEach-Object { Say ("  " + $_) }
    }
    if (-not (Test-Path $HOSTOBJ)) { Bad "${base}: clang failed on $base.host.ll" }

    $cl = @(
        'call "' + $VSDEV + '" >nul &&',
        'cl /nologo /O2 /W3 /c /I"' + $VK + '\Include" ' + $RT_SRC,
        '&& link /nologo "' + $HOSTOBJ + '" ' + $RT_OBJ,
        '"' + $VK + '\Lib\vulkan-1.lib" /OUT:' + $base + '_demo.exe'
    ) -join ' '
    Push-Location $TMP
    cmd /c $cl 2>&1 | Select-Object -Last 6
    $rc = $LASTEXITCODE
    Pop-Location
    if ($rc -ne 0) { Bad "${base}: native compile/link failed" }
    Say "  $TMP\${base}_demo.exe"

    # ---- run on the device ------------------------------------------------
    Say ""
    Say "### $base.c: run it on the Vulkan device ###"
    Push-Location $TMP
    $env:CMPL_DEMO_SPV = "$base.device.spv"
    & "$TMP\${base}_demo.exe" > "$TMP\run.out" 2>&1
    $rc = $LASTEXITCODE
    Remove-Item Env:\CMPL_DEMO_SPV
    Pop-Location
    Get-Content "$TMP\run.out" | ForEach-Object { Say ("  " + $_) }

    if ($rc -ne 0) { Bad "${base}: the kernel did not produce the CPU reference product (rc=$rc)" }
    if (-not (Select-String -Path "$TMP\run.out" -Pattern $pat -Quiet)) {
        Bad "${base}: unexpected demo output"
    }
    Say ""
}

# ---- the demos ------------------------------------------------------------

Run-Demo 'matmul' 'PASS: all 16 elements'
if (-not $Small) { Run-Demo 'matmul_load' 'PASS: all' }

Say "DEMO PASS: cmpl's SPIR-V ran on a Windows Vulkan device"
exit 0
