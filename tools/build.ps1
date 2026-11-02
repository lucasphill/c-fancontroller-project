[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Build Tools ausente. Consulte docs/toolchain.md.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'MSVC nao encontrado ou instalacao incompleta.' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$source = Join-Path $root 'src\main.cpp'
$output = Join-Path $build 'fanctl.exe'
Push-Location $build
try {
    # Paths are passed as environment values, not injected as shell source.
    $env:FANCTL_VCVARS = $vcvars
    $env:FANCTL_SOURCE = $source
    $env:FANCTL_OUTPUT = $output
    & $env:ComSpec /d /s /c '"call "%FANCTL_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE "%FANCTL_SOURCE%" /Fe:"%FANCTL_OUTPUT%" /link ole32.lib oleaut32.lib wbemuuid.lib advapi32.lib"'
    if ($LASTEXITCODE -ne 0) { throw "Compilacao falhou: $LASTEXITCODE" }
    Get-Item $output | Select-Object FullName,Length
} finally {
    Remove-Item Env:FANCTL_VCVARS,Env:FANCTL_SOURCE,Env:FANCTL_OUTPUT -ErrorAction SilentlyContinue
    Pop-Location
}
