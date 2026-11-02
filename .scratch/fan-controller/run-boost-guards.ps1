$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$exe=Join-Path $root 'build\fanctl.exe'
$journal=Join-Path $root 'build\fanctl-session.txt'
$report=[ordered]@{status='running'}
$owned=$false
$mutex=$null
$createdJournal=$false
$marker='fanctl guard test - no hardware writes'
try {
    $mutex=[Threading.Mutex]::new($false,'Global\FanControllerBoost')
    try { $owned=$mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $owned=$true }
    if (-not $owned) { throw 'A real boost session is active.' }
    $ErrorActionPreference='Continue' # Native stderr is expected in these refusal tests.
    & $exe boost --fan 50=30 1> (Join-Path $PSScriptRoot 'boost-lock-output.txt') 2> (Join-Path $PSScriptRoot 'boost-lock-error.txt')
    $report.lockExit=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    if ($LASTEXITCODE -ne 1) { throw 'Concurrent session was not rejected.' }
    $mutex.ReleaseMutex(); $owned=$false
    $file=[IO.File]::Open($journal,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try { $bytes=[Text.Encoding]::UTF8.GetBytes($marker); $file.Write($bytes,0,$bytes.Length); $file.Flush($true) } finally { $file.Dispose() }
    $createdJournal=$true
    $ErrorActionPreference='Continue'
    & $exe boost --fan 50=30 1> (Join-Path $PSScriptRoot 'boost-journal-output.txt') 2> (Join-Path $PSScriptRoot 'boost-journal-error.txt')
    $report.journalExit=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    if ($LASTEXITCODE -ne 1) { throw 'Existing journal was not rejected.' }
    if ((Get-Content $journal -Raw) -ne $marker) { throw 'Previous journal overwritten.' }
    $report.status='completed'
} catch { $report.status='failed'; $report.error=$_.Exception.Message }
finally {
    if ($owned) { $mutex.ReleaseMutex() }
    if ($null -ne $mutex) { $mutex.Dispose() }
    if ($createdJournal -and (Get-Content $journal -Raw) -eq $marker) { Remove-Item -LiteralPath $journal }
    $report | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'boost-guards-check.json')
}
