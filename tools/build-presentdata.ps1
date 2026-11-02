[CmdletBinding()]
param([string]$OutputDirectory='build/presentdata')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $installation){throw 'MSVC nao encontrado.'}
$build=Join-Path $root $OutputDirectory
$upstream=Join-Path $root 'third_party/presentmon-prototype/PresentData'
if(-not(Test-Path -LiteralPath (Join-Path $upstream 'TraceSession.cpp'))){throw 'Fontes PresentData fixadas ausentes; consulte third_party/presentmon-prototype/REVISION.txt.'}
New-Item -ItemType Directory -Force $build|Out-Null
$env:FC_PD_VCVARS=Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
$env:FC_PD_UPSTREAM=$upstream
Push-Location $build
try {
    # Unmodified upstream v1.10.0, pinned by REVISION.txt. Our callers use /W4 /WX.
    & $env:ComSpec /d /s /c '"call "%FC_PD_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W3 /O2 /MT /DNDEBUG /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /DNTDDI_VERSION=0x06010000 /c "%FC_PD_UPSTREAM%\Debug.cpp" "%FC_PD_UPSTREAM%\GpuTrace.cpp" "%FC_PD_UPSTREAM%\MixedRealityTraceConsumer.cpp" "%FC_PD_UPSTREAM%\PresentMonTraceConsumer.cpp" "%FC_PD_UPSTREAM%\TraceConsumer.cpp" "%FC_PD_UPSTREAM%\TraceSession.cpp" && lib /nologo /OUT:PresentData.lib Debug.obj GpuTrace.obj MixedRealityTraceConsumer.obj PresentMonTraceConsumer.obj TraceConsumer.obj TraceSession.obj"'
    if($LASTEXITCODE -ne 0){throw 'Compilacao PresentData estatica falhou.'}
    Get-Item PresentData.lib|Select-Object FullName,Length
} finally {
    Pop-Location
    Remove-Item Env:FC_PD_VCVARS,Env:FC_PD_UPSTREAM -ErrorAction SilentlyContinue
}
