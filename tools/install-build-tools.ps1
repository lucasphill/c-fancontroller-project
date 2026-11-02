# Installs only the native compiler and Windows SDK; no automatic restart.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$resultPath = Join-Path $PSScriptRoot '..\.scratch\fan-controller\build-tools-install.json'
$result = [ordered]@{ started=(Get-Date).ToString('o'); status='running' }
try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrator required.' }
    $installer = Join-Path $env:TEMP 'fancontroller-vs-buildtools.exe'
    $signature = Get-AuthenticodeSignature -LiteralPath $installer
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
        throw 'Microsoft signature not validated.'
    }
    $result | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
    $process = Start-Process -FilePath $installer -WindowStyle Hidden -ArgumentList @(
        '--quiet', '--wait', '--norestart', '--nocache',
        '--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.26100'
    ) -Wait -PassThru
    $result.exitCode = $process.ExitCode
    if ($process.ExitCode -eq 0) { $result.status='completed' }
    elseif ($process.ExitCode -eq 3010) { $result.status='completed-restart-required' }
    else { throw "Installer exited with code $($process.ExitCode)." }
} catch {
    $result.status='failed'
    $result.error=$_.Exception.Message
} finally {
    $result.finished=(Get-Date).ToString('o')
    $result | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
}
if ($result.status -eq 'failed') { exit 1 }
