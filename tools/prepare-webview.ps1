[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$folder=Join-Path $root '.tools/webview2'
$sdk=Join-Path $folder 'sdk'
$archive=Join-Path $folder 'sdk.zip'
$expected='911A472128C82AC8BAA0C486C23342CC9DD6E7DC50D754E676726642CA065C60'
if (-not (Test-Path (Join-Path $sdk 'build/native/include/WebView2.h'))) {
    New-Item -ItemType Directory -Force $folder | Out-Null
    Invoke-WebRequest -Uri 'https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.3650.58/microsoft.web.webview2.1.0.3650.58.nupkg' -OutFile $archive
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) {throw 'Hash do SDK WebView2 inesperado.'}
    Expand-Archive -LiteralPath $archive -DestinationPath $sdk -Force
}
return $sdk
