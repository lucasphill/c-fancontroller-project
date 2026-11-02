# Read-only comparison; no fan setters or GPU configuration commands.
[CmdletBinding()]
param([switch]$ResumeFromIdle,[switch]$ResumeFromInit)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$folder=Join-Path $root '.scratch\fan-controller'
$path=Join-Path $folder 'nvml-investigation.json'
$exe=Join-Path $root 'build\nvml-probe.exe'
$report=[ordered]@{started=(Get-Date).ToString('o');status='running';phase='setup';samples=@();modes=@();smiQueries=@()}
$probe=$null
try {
    $instance=Get-CimInstance -Namespace root/WMI -ClassName AWCCWmiMethodFunction -OperationTimeoutSec 5
    function Read-DellValue([uint32]$op,[uint32]$id=0) {
        if ($op -notin @(4,11,12)) { throw 'Consulta nao permitida.' }
        $response=Invoke-CimMethod -InputObject $instance -MethodName Thermal_Information -Arguments @{arg2=[uint32](($id-shl 8)-bor $op)} -OperationTimeoutSec 5
        if ($null -eq $response.argr -or [uint32]$response.argr -ge 4294967280) { throw 'Consulta Dell indisponivel.' }
        return [uint32]$response.argr
    }
    function Read-Dell {
        return [ordered]@{time=(Get-Date).ToString('o');sensor1=(Read-DellValue 4 1);sensor6=(Read-DellValue 4 6);profile=(Read-DellValue 11);boost50=(Read-DellValue 12 50);boost51=(Read-DellValue 12 51)}
    }
    $report.original=Read-Dell
    $modes=@('idle','init','temperature','telemetry')
    if ($ResumeFromIdle -or $ResumeFromInit) {
        $previousPath=if ($ResumeFromInit) { 'nvml-investigation-attempt2.json' } else { 'nvml-investigation-attempt1.json' }
        $previous=Get-Content (Join-Path $folder $previousPath) -Raw | ConvertFrom-Json
        $idle=Get-Content (Join-Path $folder 'nvml-idle-metrics.json') -Raw | ConvertFrom-Json
        if ($idle.mode -ne 'idle' -or $idle.samples -ne 15) { throw 'Baseline anterior incompleta.' }
        $report.baselineStarted=$previous.started
        $report.samples=@($previous.samples)
        $report.modes=@($idle)
        $modes=@('init','temperature','telemetry')
        if ($ResumeFromInit) {
            $init=Get-Content (Join-Path $folder 'nvml-init-metrics.json') -Raw | ConvertFrom-Json
            if ($init.mode -ne 'init' -or $init.samples -ne 15) { throw 'Medicao de inicializacao incompleta.' }
            $report.modes += $init
            $report.samples=@($previous.samples | Where-Object { $_.phase -in @('idle','init','after-init') })
            $modes=@('temperature','telemetry')
        }
    }
    foreach ($mode in $modes) {
        $report.phase=$mode
        $metrics=Join-Path $folder "nvml-$mode-metrics.json"
        $output=Join-Path $folder "nvml-$mode-samples.csv"
        $errors=Join-Path $folder "nvml-$mode-error.txt"
        $probe=Start-Process -FilePath $exe -WindowStyle Hidden -ArgumentList @($mode,('"'+$metrics+'"')) -RedirectStandardOutput $output -RedirectStandardError $errors -PassThru
        # Retain the process handle so ExitCode remains available after Refresh/exit.
        $null=$probe.Handle
        $index=0
        $timer=[Diagnostics.Stopwatch]::StartNew()
        while (-not $probe.HasExited) {
            $sample=[ordered]@{phase=$mode;dell=(Read-Dell)}
            if ($mode -eq 'telemetry' -and $index -lt 6) {
                $query=[ordered]@{started=(Get-Date).ToString('o')}
                $watch=[Diagnostics.Stopwatch]::StartNew()
                $query.output=@(& "$env:SystemRoot\System32\nvidia-smi.exe" --query-gpu=temperature.gpu,pstate,power.draw --format=csv,noheader,nounits 2>&1 | ForEach-Object { "$_" })
                $query.exit=$LASTEXITCODE
                $query.wallMs=$watch.Elapsed.TotalMilliseconds
                $query.finished=(Get-Date).ToString('o')
                $report.smiQueries += $query
                $sample.smi=$query
                $sample.dellAfter=Read-Dell
            }
            $report.samples += $sample
            $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding UTF8
            if ($timer.Elapsed.TotalSeconds -gt 45) { throw 'Sonda excedeu limite de tempo.' }
            Start-Sleep -Seconds 2
            $probe.Refresh()
            $index++
        }
        $probe.WaitForExit()
        if ($probe.ExitCode -ne 0) { throw "Sonda $mode falhou: $(Get-Content $errors -Raw)" }
        $report.modes += (Get-Content $metrics -Raw | ConvertFrom-Json)
        $probe.Dispose();$probe=$null
        # Observe cooldown without loading NVML in a process or querying nvidia-smi.
        $report.phase="after-$mode"
        for ($index=0;$index -lt 5;$index++) {
            $report.samples += [ordered]@{phase=$report.phase;dell=(Read-Dell)}
            $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding UTF8
            Start-Sleep -Seconds 2
        }
    }
    $report.final=Read-Dell
    $report.controlsUnchanged=($report.final.profile -eq $report.original.profile -and $report.final.boost50 -eq $report.original.boost50 -and $report.final.boost51 -eq $report.original.boost51)
    $report.status='completed'
} catch { $report.status='failed';$report.error=$_.Exception.Message }
finally {
    if ($null -ne $probe) {
        if (-not $probe.HasExited) { $probe.Kill();$probe.WaitForExit() }
        $probe.Dispose()
    }
    $report.finished=(Get-Date).ToString('o')
    $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding UTF8
}
