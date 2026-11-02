[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) {throw 'MSVC nao encontrado.'}
$env:FANCTL_CHART_VCVARS=Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
$env:FANCTL_CHART_SOURCE=Join-Path $root 'tests\history_chart_test.cpp'
New-Item -ItemType Directory -Force (Join-Path $root 'build') | Out-Null
Push-Location (Join-Path $root 'build')
try {
    & $env:ComSpec /d /s /c '"call "%FANCTL_CHART_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE "%FANCTL_CHART_SOURCE%" /Fe:history-chart-test.exe /link user32.lib gdi32.lib shell32.lib comctl32.lib ole32.lib uuid.lib"'
    if ($LASTEXITCODE -ne 0) {throw 'Compilacao dos testes do grafico falhou.'}
    & .\history-chart-test.exe
    if ($LASTEXITCODE -ne 0) {throw 'Teste de continuidade da temperatura GPU falhou.'}
} finally {
    Pop-Location
    Remove-Item Env:FANCTL_CHART_VCVARS,Env:FANCTL_CHART_SOURCE -ErrorAction SilentlyContinue
}
