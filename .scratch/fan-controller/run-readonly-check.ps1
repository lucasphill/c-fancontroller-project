$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$exe=Join-Path $root 'build\fanctl.exe'
$result=[ordered]@{started=(Get-Date).ToString('o');status='running'}
try {
    $result | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'readonly-check.json')
    & $exe status 1> (Join-Path $PSScriptRoot 'status-output.txt') 2> (Join-Path $PSScriptRoot 'status-error.txt')
    $result.statusExit=$LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'Elevated status failed.' }
    & (Join-Path $root 'build\watch-check.exe') $exe (Join-Path $PSScriptRoot 'watch-output.txt') (Join-Path $PSScriptRoot 'watch-metrics.json')
    $result.watchExit=$LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'Watch integration check failed.' }
    $result.status='completed'
} catch {
    $result.status='failed'
    $result.error=$_.Exception.Message
} finally {
    $result.finished=(Get-Date).ToString('o')
    $result | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'readonly-check.json')
}
