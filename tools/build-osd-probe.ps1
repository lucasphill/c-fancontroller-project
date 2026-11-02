[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'MSVC nao encontrado.' }
$env:FANCTL_PROBE_VCVARS=Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
$env:FANCTL_PROBE_SOURCE=Join-Path $root 'tests\osd_probe.cpp'
Push-Location (Join-Path $root 'build')
try {
    & $env:ComSpec /d /s /c '"call "%FANCTL_PROBE_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE "%FANCTL_PROBE_SOURCE%" /Fe:osd-probe.exe /link user32.lib shell32.lib advapi32.lib ole32.lib oleaut32.lib wbemuuid.lib uuid.lib"'
    if ($LASTEXITCODE -ne 0) { throw 'Compilacao da sonda OSD falhou.' }
} finally {
    Pop-Location
    Remove-Item Env:FANCTL_PROBE_VCVARS,Env:FANCTL_PROBE_SOURCE -ErrorAction SilentlyContinue
}
