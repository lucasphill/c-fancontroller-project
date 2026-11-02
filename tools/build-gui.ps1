[CmdletBinding()]
param([string]$OutputDirectory='build',[switch]$Preview)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) {throw 'MSVC nao encontrado.'}
$build=Join-Path $root $OutputDirectory
New-Item -ItemType Directory -Path $build -Force | Out-Null
& (Join-Path $PSScriptRoot 'build-presentdata.ps1') -OutputDirectory (Join-Path $OutputDirectory 'presentdata')
$env:FANCTL_GUI_VCVARS=Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
$env:FANCTL_GUI_SOURCE=Join-Path $root 'src\gui.cpp'
$env:FANCTL_GUI_FRAMES=Join-Path $root 'src\presentmon_frames.cpp'
$env:FANCTL_GUI_PRESENTDATA=Join-Path $root 'third_party\presentmon-prototype\PresentData'
$env:FANCTL_GUI_MANIFEST=Join-Path $root 'src\gui.manifest'
$env:FANCTL_GUI_OPTIONS='/DFANCTL_PRODUCTION'
if ($Preview) {
    $previewManifest=Join-Path $build 'preview.manifest'
    (Get-Content -LiteralPath $env:FANCTL_GUI_MANIFEST -Raw).Replace('requireAdministrator','asInvoker') | Set-Content -LiteralPath $previewManifest -Encoding utf8
    $env:FANCTL_GUI_MANIFEST=$previewManifest
    $env:FANCTL_GUI_OPTIONS='/DFANCTL_PREVIEW_ONLY'
}
$env:FANCTL_SQLITE_SOURCE=Join-Path $root 'third_party\sqlite\sqlite3.c'
Push-Location $build
try {
    & $env:ComSpec /d /s /c '"call "%FANCTL_GUI_VCVARS%" >nul && cl /nologo /O2 /MT /W0 /TC /DSQLITE_THREADSAFE=1 /DSQLITE_DQS=0 /DSQLITE_OMIT_LOAD_EXTENSION /c "%FANCTL_SQLITE_SOURCE%" /Fo:sqlite3.obj"'
    if ($LASTEXITCODE -ne 0) {throw 'Compilacao do SQLite falhou.'}
    & $env:ComSpec /d /s /c '"call "%FANCTL_GUI_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE %FANCTL_GUI_OPTIONS% /I"%FANCTL_GUI_PRESENTDATA%" "%FANCTL_GUI_SOURCE%" "%FANCTL_GUI_FRAMES%" sqlite3.obj presentdata\PresentData.lib /Fe:FanController.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:NO /MANIFESTINPUT:"%FANCTL_GUI_MANIFEST%" user32.lib gdi32.lib shell32.lib comctl32.lib ole32.lib oleaut32.lib wbemuuid.lib advapi32.lib uuid.lib tdh.lib"'
    if ($LASTEXITCODE -ne 0) {throw 'Compilacao da interface falhou.'}
    Copy-Item -LiteralPath (Join-Path $root 'third_party/presentmon-prototype/LICENSE.txt') -Destination (Join-Path $build 'PresentMon-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $root 'third_party/presentmon-prototype/THIRD_PARTY.txt') -Destination (Join-Path $build 'PresentMon-THIRD-PARTY.txt')
    Get-Item .\FanController.exe | Select-Object FullName,Length
} finally {
    Pop-Location
    Remove-Item Env:FANCTL_GUI_VCVARS,Env:FANCTL_GUI_SOURCE,Env:FANCTL_GUI_FRAMES,Env:FANCTL_GUI_PRESENTDATA,Env:FANCTL_GUI_MANIFEST,Env:FANCTL_GUI_OPTIONS,Env:FANCTL_SQLITE_SOURCE -ErrorAction SilentlyContinue
}
