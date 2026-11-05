[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'build-gui.ps1') -OutputDirectory 'build/package'
if (Test-Path (Join-Path $root 'build/fanctl-curves.txt')) {
    Copy-Item -LiteralPath (Join-Path $root 'build/fanctl-curves.txt') -Destination (Join-Path $root 'build/package/fanctl-curves.txt')
} else {throw 'Salve as curvas na interface antes de empacotar.'}
Set-Content -LiteralPath (Join-Path $root 'build/package/fanctl-startup.txt') -Encoding ascii -Value "FANCTL_STARTUP 1`n1 30 30 0"
$compiler=Join-Path $root '.tools/inno/ISCC.exe'
if (-not (Test-Path $compiler)) {throw 'Compilador Inno Setup portatil nao encontrado em .tools/inno.'}
& $compiler (Join-Path $root 'installer/FanController.iss')
if ($LASTEXITCODE -ne 0) {throw 'Compilacao do instalador falhou.'}
Get-ChildItem (Join-Path $root 'dist/aeri-Setup-*.exe') | Select-Object FullName,Length
