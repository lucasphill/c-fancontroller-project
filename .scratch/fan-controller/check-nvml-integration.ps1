$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$exe=Join-Path $root 'build\fanctl.exe'
$folder=$PSScriptRoot
$report=[ordered]@{started=(Get-Date).ToString('o');status='running';checks=@()}
$before=$null
$instance=$null
function Read-Value([uint32]$op,[uint32]$id=0) {
    $value=(Invoke-CimMethod -InputObject $instance -MethodName Thermal_Information -Arguments @{arg2=[uint32](($id-shl 8)-bor $op)} -OperationTimeoutSec 5).argr
    if ($null -eq $value -or [uint32]$value -ge 4294967280) { throw 'Consulta independente indisponivel.' }
    return [uint32]$value
}
function Read-Controls {
    [ordered]@{profile=(Read-Value 11);boost50=(Read-Value 12 50);boost51=(Read-Value 12 51)}
}
function Monitor-Check([string]$name,[string]$arguments,[bool]$expectNvml) {
    $log=Join-Path $folder "$name-output.txt"
    $metrics=Join-Path $folder "$name-metrics.json"
    $runner=Start-Process -FilePath (Join-Path $root 'build\watch-check.exe') -WindowStyle Hidden -ArgumentList @(('"'+$exe+'"'),('"'+$log+'"'),('"'+$metrics+'"'),('"'+$arguments+'"')) -PassThru
    $null=$runner.Handle
    try {
        Start-Sleep -Seconds 3
        $children=@(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($runner.Id)" | Where-Object { $_.Name -eq 'fanctl.exe' })
        if ($children.Count -ne 1) { throw "Processo filho nao encontrado em $name." }
        $process=Get-Process -Id $children[0].ProcessId
        $loaded=@($process.Modules | Where-Object { $_.ModuleName -ieq 'nvml.dll' }).Count -gt 0
        if ($loaded -ne $expectNvml) { throw "Carregamento NVML inesperado em $name." }
        if (-not $runner.WaitForExit(55000)) { throw 'Harness excedeu limite de tempo.' }
        if ($runner.ExitCode -ne 0) { throw "Teste $name falhou: $(Get-Content $log -Raw)" }
        $result=Get-Content $metrics -Raw | ConvertFrom-Json
        $output=Get-Content $log -Raw
        if ($expectNvml -and $output -notmatch 'GPU NVIDIA: \d+ C \[NVML\]') { throw 'Temperatura NVML nao exibida.' }
        $report.checks += [ordered]@{name=$name;exit=$runner.ExitCode;nvmlLoaded=$loaded;metrics=$result}
    } finally {
        if (-not $runner.HasExited) { [void]$runner.WaitForExit(55000) }
        $runner.Dispose()
    }
}
try {
    if (@(Get-Process fanctl,fanctl-next -ErrorAction SilentlyContinue).Count) { throw 'Sessao do usuario ainda ativa.' }
    if (Test-Path (Join-Path $root 'build\fanctl-session.txt')) { throw 'Registro anterior existe.' }
    $instance=Get-CimInstance -Namespace root/WMI -ClassName AWCCWmiMethodFunction
    $before=Read-Controls
    $report.before=$before
    if ($before.profile -ne 160 -or $before.boost50 -ne 0 -or $before.boost51 -ne 0) { throw 'Estado inicial inesperado para o teste.' }
    & $exe status --gpu-source nvml 1> (Join-Path $folder 'nvml-status-output.txt') 2> (Join-Path $folder 'nvml-status-error.txt')
    $report.statusExit=$LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'Status NVML falhou.' }
    Monitor-Check 'gpu-default-watch' 'watch' $false
    $report | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $folder 'nvml-integration-check.json')
    Monitor-Check 'gpu-nvml-watch' 'watch --gpu-source nvml' $true
    $report | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $folder 'nvml-integration-check.json')
    Monitor-Check 'gpu-nvml-boost' 'boost --gpu-source nvml --fan 50=30 --fan 51=30' $true
    $report.after=Read-Controls
    $report.restorationVerified=($report.after.profile -eq $before.profile -and $report.after.boost50 -eq $before.boost50 -and $report.after.boost51 -eq $before.boost51)
    $report.journalRemoved=(-not (Test-Path (Join-Path $root 'build\fanctl-session.txt')))
    if (-not $report.restorationVerified -or -not $report.journalRemoved) { throw 'Restauracao final nao confirmada.' }
    $report.status='completed'
} catch { $report.status='failed';$report.error=$_.Exception.Message }
finally {
    if ($null -ne $before) {
        try {
            $current=Read-Controls
            if ($current.profile -ne $before.profile -or $current.boost50 -ne $before.boost50 -or $current.boost51 -ne $before.boost51) {
                $report.emergencyRestoreUsed=$true
                [void](Invoke-CimMethod -InputObject $instance -MethodName Thermal_Control -Arguments @{arg2=[uint32](($before.profile-shl 8)-bor 1)} -OperationTimeoutSec 5)
                foreach ($id in @(50,51)) {
                    $value=if ($id -eq 50) {$before.boost50} else {$before.boost51}
                    [void](Invoke-CimMethod -InputObject $instance -MethodName Thermal_Control -Arguments @{arg2=[uint32](($value-shl 16)-bor ($id-shl 8)-bor 2)} -OperationTimeoutSec 5)
                }
                $report.emergencyFinal=Read-Controls
            }
        } catch { $report.emergencyError=$_.Exception.Message }
    }
    $report.finished=(Get-Date).ToString('o')
    $report | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $folder 'nvml-integration-check.json')
}
