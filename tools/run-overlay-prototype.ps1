[CmdletBinding()]
param([switch]$Elevated,[switch]$Record,[ValidateRange(0,3600)][int]$Seconds=0)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$prototype=Join-Path $root 'build/overlay-prototype/FanController-Overlay-Prototype.exe'
if(-not(Test-Path -LiteralPath $prototype)){& (Join-Path $PSScriptRoot 'build-overlay-prototype.ps1')}
$arguments=@('--seconds',"$Seconds")
if($Record){$arguments+='--record'}
$launch=@{FilePath=$prototype;WorkingDirectory=$root;ArgumentList=$arguments;PassThru=$true}
if($Elevated){$launch.Verb='RunAs'}
# The interactive diagnostic window is part of the approved prototype.
Start-Process @launch | Select-Object Id,ProcessName
