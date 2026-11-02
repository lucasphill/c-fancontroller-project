[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$folder=Join-Path $root '.scratch\fan-controller'
$report=[ordered]@{started=(Get-Date).ToString('o');status='running';samples=@()}
$probe=$null
try {
    $instance=Get-CimInstance -Namespace root/WMI -ClassName AWCCWmiMethodFunction -OperationTimeoutSec 5
    function Read-Control([uint32]$op,[uint32]$id=0) {
        (Invoke-CimMethod -InputObject $instance -MethodName Thermal_Information -Arguments @{arg2=[uint32](($id-shl 8)-bor $op)} -OperationTimeoutSec 5).argr
    }
    $report.original=@{profile=(Read-Control 11);boost50=(Read-Control 12 50);boost51=(Read-Control 12 51)}
    $probe=Start-Process -FilePath (Join-Path $root 'build\nvml-probe.exe') -WindowStyle Hidden -ArgumentList @('modern',('"'+(Join-Path $folder 'nvml-modern-metrics.json')+'"')) -RedirectStandardOutput (Join-Path $folder 'nvml-modern-samples.csv') -RedirectStandardError (Join-Path $folder 'nvml-modern-error.txt') -PassThru
    $null=$probe.Handle
    $index=0
    while (-not $probe.HasExited) {
        $sample=[ordered]@{started=(Get-Date).ToString('o');dell6=(Read-Control 4 6)}
        if ($index -lt 6) {
            $sample.smi=@(& "$env:SystemRoot\System32\nvidia-smi.exe" --query-gpu=temperature.gpu --format=csv,noheader,nounits 2>&1 | ForEach-Object { "$_" })
            $sample.smiExit=$LASTEXITCODE
            $sample.smiFinished=(Get-Date).ToString('o')
        }
        $report.samples += $sample
        $report | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $folder 'nvml-modern-check.json')
        Start-Sleep -Seconds 2
        $probe.Refresh();$index++
        if ($index -gt 25) { throw 'Sonda excedeu o prazo.' }
    }
    $probe.WaitForExit()
    $report.exitCode=$probe.ExitCode
    if ($probe.ExitCode -ne 0) { throw "NVML modern falhou: $(Get-Content (Join-Path $folder 'nvml-modern-error.txt') -Raw)" }
    $report.metrics=Get-Content (Join-Path $folder 'nvml-modern-metrics.json') -Raw | ConvertFrom-Json
    $report.final=@{profile=(Read-Control 11);boost50=(Read-Control 12 50);boost51=(Read-Control 12 51)}
    $report.controlsUnchanged=($report.final.profile -eq $report.original.profile -and $report.final.boost50 -eq $report.original.boost50 -and $report.final.boost51 -eq $report.original.boost51)
    $report.status='completed'
} catch { $report.status='failed';$report.error=$_.Exception.Message }
finally {
    if ($null -ne $probe) { if (-not $probe.HasExited) { $probe.Kill();$probe.WaitForExit() };$probe.Dispose() }
    $report.finished=(Get-Date).ToString('o')
    $report | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $folder 'nvml-modern-check.json')
}
