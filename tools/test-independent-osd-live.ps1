[CmdletBinding()]
param([switch]$Run,[switch]$Elevated,[ValidateRange(1,600)][int]$Seconds=180)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output='build/independent-osd-live'
& (Join-Path $PSScriptRoot 'build-presentdata.ps1') -OutputDirectory (Join-Path $output 'presentdata')
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$env:FC_LIVE_VCVARS=Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
$env:FC_LIVE_SOURCE=Join-Path $root 'tests/independent_osd_live.cpp'
$env:FC_LIVE_FRAMES=Join-Path $root 'src/presentmon_frames.cpp'
$env:FC_LIVE_UPSTREAM=Join-Path $root 'third_party/presentmon-prototype/PresentData'
$build=Join-Path $root $output
Push-Location $build
try {
    & $env:ComSpec /d /s /c '"call "%FC_LIVE_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE /I"%FC_LIVE_UPSTREAM%" "%FC_LIVE_SOURCE%" "%FC_LIVE_FRAMES%" presentdata\PresentData.lib /Fe:FanController-Osd-Validation.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib wbemuuid.lib advapi32.lib uuid.lib tdh.lib"'
    if($LASTEXITCODE -ne 0){throw 'Compilacao da validacao OSD falhou.'}
} finally {
    Pop-Location
    Remove-Item Env:FC_LIVE_VCVARS,Env:FC_LIVE_SOURCE,Env:FC_LIVE_FRAMES,Env:FC_LIVE_UPSTREAM -ErrorAction SilentlyContinue
}
if($Run) {
    $launch=@{FilePath=(Join-Path $build 'FanController-Osd-Validation.exe');WorkingDirectory=$root;ArgumentList=@('--record','--seconds',"$Seconds");PassThru=$true}
    if($Elevated){$launch.Verb='RunAs'}
    Start-Process @launch | Select-Object Id,ProcessName
}
