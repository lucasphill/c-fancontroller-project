[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'MSVC nao encontrado.' }
$env:FANCTL_NVML_VCVARS=Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
$env:FANCTL_NVML_SOURCE=Join-Path $PSScriptRoot 'nvml-probe.cpp'
$build=Join-Path $root 'build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
Push-Location $build
try {
    & $env:ComSpec /d /s /c '"call "%FANCTL_NVML_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT "%FANCTL_NVML_SOURCE%" /Fe:nvml-probe.exe /link psapi.lib"'
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao compilar sonda NVML.' }
    Get-Item .\nvml-probe.exe | Select-Object FullName,Length
} finally {
    Pop-Location
    Remove-Item Env:FANCTL_NVML_VCVARS,Env:FANCTL_NVML_SOURCE -ErrorAction SilentlyContinue
}
