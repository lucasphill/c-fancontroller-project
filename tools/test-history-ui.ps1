[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) {throw 'MSVC nao encontrado.'}
$env:FANCTL_PREVIEW_VCVARS=Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
$env:FANCTL_PREVIEW_SOURCE=Join-Path $root 'tests\history_preview.cpp'
New-Item -ItemType Directory -Force (Join-Path $root 'build') | Out-Null
Push-Location (Join-Path $root 'build')
try {
    & $env:ComSpec /d /s /c '"call "%FANCTL_PREVIEW_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE "%FANCTL_PREVIEW_SOURCE%" /Fe:history-preview.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib gdiplus.lib shell32.lib comctl32.lib ole32.lib uuid.lib"'
    if ($LASTEXITCODE -ne 0) {throw 'Compilacao da previa falhou.'}
    Get-Item history-preview.exe | Select-Object FullName,Length
} finally {
    Pop-Location
    Remove-Item Env:FANCTL_PREVIEW_VCVARS,Env:FANCTL_PREVIEW_SOURCE -ErrorAction SilentlyContinue
}
