[CmdletBinding()]
param([switch]$ElevatedManifest,[string]$OutputDirectory='build/overlay-prototype')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $installation){throw 'MSVC nao encontrado.'}
$build=Join-Path $root $OutputDirectory
$upstream=Join-Path $root 'third_party/presentmon-prototype/PresentData'
$prototype=Join-Path $root '.scratch/osd-independente/prototype'
if(-not (Test-Path (Join-Path $upstream 'TraceSession.cpp'))){throw 'Fontes PresentData fixados ausentes; consulte third_party/presentmon-prototype/REVISION.txt.'}
New-Item -ItemType Directory -Force $build,(Join-Path $build 'presentdata')|Out-Null
$env:FC_OV_VCVARS=Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
$env:FC_OV_UPSTREAM=$upstream
$env:FC_OV_PROTOTYPE=$prototype
$env:FC_OV_MANIFEST=Join-Path $build 'prototype.manifest'
$manifest=(Get-Content (Join-Path $root 'src/gui.manifest') -Raw).Replace('FanController.Gui','FanController.OverlayPrototype')
if(-not $ElevatedManifest){$manifest=$manifest.Replace('requireAdministrator','asInvoker')}
$manifest|Set-Content -LiteralPath $env:FC_OV_MANIFEST -Encoding utf8
Push-Location (Join-Path $build 'presentdata')
try {
    # Third-party source is kept unmodified and compiled as its standalone static library.
    & $env:ComSpec /d /s /c '"call "%FC_OV_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W3 /O2 /MT /DNDEBUG /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /DNTDDI_VERSION=0x06010000 /c "%FC_OV_UPSTREAM%\Debug.cpp" "%FC_OV_UPSTREAM%\GpuTrace.cpp" "%FC_OV_UPSTREAM%\MixedRealityTraceConsumer.cpp" "%FC_OV_UPSTREAM%\PresentMonTraceConsumer.cpp" "%FC_OV_UPSTREAM%\TraceConsumer.cpp" "%FC_OV_UPSTREAM%\TraceSession.cpp" && lib /nologo /OUT:PresentData.lib Debug.obj GpuTrace.obj MixedRealityTraceConsumer.obj PresentMonTraceConsumer.obj TraceConsumer.obj TraceSession.obj"'
    if($LASTEXITCODE -ne 0){throw 'Compilacao PresentData estatica falhou.'}
} finally {Pop-Location}
Push-Location $build
try {
    & $env:ComSpec /d /s /c '"call "%FC_OV_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /utf-8 /DNDEBUG /DUNICODE /D_UNICODE /I"%FC_OV_UPSTREAM%" "%FC_OV_PROTOTYPE%\main.cpp" "%FC_OV_PROTOTYPE%\presentmon_frames.cpp" presentdata\PresentData.lib /Fe:FanController-Overlay-Prototype.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:NO /MANIFESTINPUT:"%FC_OV_MANIFEST%" user32.lib gdi32.lib shell32.lib comctl32.lib ole32.lib oleaut32.lib wbemuuid.lib advapi32.lib uuid.lib tdh.lib dwmapi.lib msimg32.lib"'
    if($LASTEXITCODE -ne 0){throw 'Compilacao prototipo OSD falhou.'}
    Copy-Item -LiteralPath (Join-Path $root 'third_party/presentmon-prototype/LICENSE.txt') -Destination 'PresentMon-LICENSE.txt'
    Copy-Item -LiteralPath (Join-Path $root 'third_party/presentmon-prototype/THIRD_PARTY.txt') -Destination 'PresentMon-THIRD-PARTY.txt'
    Get-Item FanController-Overlay-Prototype.exe|Select-Object FullName,Length
} finally {
    Pop-Location
    Remove-Item Env:FC_OV_VCVARS,Env:FC_OV_UPSTREAM,Env:FC_OV_PROTOTYPE,Env:FC_OV_MANIFEST -ErrorAction SilentlyContinue
}
