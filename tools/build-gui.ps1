[CmdletBinding()]
param([string]$OutputDirectory='build',[switch]$Preview)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) {throw 'MSVC nao encontrado.'}
$build=Join-Path $root $OutputDirectory
$webview=& (Join-Path $PSScriptRoot 'prepare-webview.ps1')
New-Item -ItemType Directory -Path $build -Force | Out-Null
$presentLibrary=Join-Path $build 'presentdata/PresentData.lib'
$presentInputs=@(Get-ChildItem (Join-Path $root 'third_party/presentmon-prototype/PresentData') -Recurse -File | Where-Object { $_.Extension -in '.cpp','.h' }) + @(Get-Item (Join-Path $PSScriptRoot 'build-presentdata.ps1'))
if (-not (Test-Path -LiteralPath $presentLibrary) -or ($presentInputs | Where-Object { $_.LastWriteTimeUtc -gt (Get-Item $presentLibrary).LastWriteTimeUtc })) {
    & (Join-Path $PSScriptRoot 'build-presentdata.ps1') -OutputDirectory (Join-Path $OutputDirectory 'presentdata')
}
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
$env:AERI_WEBVIEW=Join-Path $webview 'build/native'
$page=(Get-Content -LiteralPath (Join-Path $root 'src/ui/aeri.html') -Raw).Replace('{{STYLE}}',(Get-Content -LiteralPath (Join-Path $root 'src/ui/aeri.css') -Raw)).Replace('{{SCRIPT}}',(Get-Content -LiteralPath (Join-Path $root 'src/ui/aeri.js') -Raw))
[IO.File]::WriteAllText((Join-Path $build 'aeri-page.html'),$page,[Text.UTF8Encoding]::new($false))
$icon=(Join-Path $root 'assets/aeri.ico').Replace('\','\\')
[IO.File]::WriteAllText((Join-Path $build 'aeri.rc'),("400 RCDATA `"aeri-page.html`"`n101 ICON `"$icon`""),[Text.UTF8Encoding]::new($false))
Push-Location $build
try {
    & $env:ComSpec /d /s /c '"call "%FANCTL_GUI_VCVARS%" >nul && cl /nologo /O2 /MT /W0 /TC /DSQLITE_THREADSAFE=1 /DSQLITE_DQS=0 /DSQLITE_OMIT_LOAD_EXTENSION /c "%FANCTL_SQLITE_SOURCE%" /Fo:sqlite3.obj"'
    if ($LASTEXITCODE -ne 0) {throw 'Compilacao do SQLite falhou.'}
    & $env:ComSpec /d /s /c '"call "%FANCTL_GUI_VCVARS%" >nul && rc /nologo /fo aeri.res aeri.rc && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE %FANCTL_GUI_OPTIONS% /I"%AERI_WEBVIEW%\include" /I"%FANCTL_GUI_PRESENTDATA%" "%FANCTL_GUI_SOURCE%" "%FANCTL_GUI_FRAMES%" sqlite3.obj aeri.res presentdata\PresentData.lib "%AERI_WEBVIEW%\x64\WebView2LoaderStatic.lib" /Fe:FanController.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:NO /MANIFESTINPUT:"%FANCTL_GUI_MANIFEST%" user32.lib gdi32.lib shell32.lib comctl32.lib ole32.lib oleaut32.lib wbemuuid.lib advapi32.lib uuid.lib tdh.lib dwmapi.lib version.lib shlwapi.lib"'
    if ($LASTEXITCODE -ne 0) {throw 'Compilacao da interface falhou.'}
    Copy-Item -LiteralPath (Join-Path $root 'third_party/presentmon-prototype/LICENSE.txt') -Destination (Join-Path $build 'PresentMon-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $root 'third_party/presentmon-prototype/THIRD_PARTY.txt') -Destination (Join-Path $build 'PresentMon-THIRD-PARTY.txt')
    Copy-Item -LiteralPath (Join-Path $webview 'LICENSE.txt') -Destination (Join-Path $build 'WebView2-LICENSE.txt')
    Copy-Item -LiteralPath .\FanController.exe -Destination .\aeri.exe
    Get-Item .\aeri.exe | Select-Object FullName,Length
} finally {
    Pop-Location
    Remove-Item Env:FANCTL_GUI_VCVARS,Env:FANCTL_GUI_SOURCE,Env:FANCTL_GUI_FRAMES,Env:FANCTL_GUI_PRESENTDATA,Env:FANCTL_GUI_MANIFEST,Env:FANCTL_GUI_OPTIONS,Env:FANCTL_SQLITE_SOURCE,Env:AERI_WEBVIEW -ErrorAction SilentlyContinue
}
