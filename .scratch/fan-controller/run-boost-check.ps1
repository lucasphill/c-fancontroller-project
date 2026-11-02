$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$exe=Join-Path $root 'build\fanctl.exe'
$report=[ordered]@{started=(Get-Date).ToString('o');status='running'}
$instance=$null
$before=$null
function Read-Controls {
    $result=[ordered]@{profile=0;fans=@()}
    $result.profile=(Invoke-CimMethod -InputObject $instance -MethodName Thermal_Information -Arguments @{arg2=[uint32]11} -OperationTimeoutSec 5).argr
    foreach ($id in @(50,51)) {
        $value=(Invoke-CimMethod -InputObject $instance -MethodName Thermal_Information -Arguments @{arg2=[uint32](($id -shl 8)-bor 12)} -OperationTimeoutSec 5).argr
        if ($null -eq $value -or [uint32]$value -gt 255) { throw 'Boost original invalido.' }
        $result.fans += [ordered]@{id=$id;boost=[uint32]$value}
    }
    if ($null -eq $result.profile -or [uint32]$result.profile -ne 160) { throw 'Perfil inesperado.' }
    return $result
}
try {
    $report | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'boost-cli-check.json')
    $competing=@(Get-Process | Where-Object { $_.ProcessName -match 'AWCC|Alienware|AlienFan|tcc.?g15|Afterburner' })
    if ($competing.Count) { throw 'Possivel controlador concorrente.' }
    $instance=Get-CimInstance -Namespace root/WMI -ClassName AWCCWmiMethodFunction
    $before=Read-Controls
    $report.before=$before
    if (@($before.fans | Where-Object { $_.boost -ne 0 }).Count) { throw 'Teste exige boost inicial 0 nas duas fans.' }
    & (Join-Path $root 'build\watch-check.exe') $exe (Join-Path $PSScriptRoot 'boost-cli-output.txt') (Join-Path $PSScriptRoot 'boost-cli-metrics.json') 'boost --fan 50=30 --fan 51=30'
    $report.exitCode=$LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'Teste do comando boost falhou.' }
    $after=Read-Controls
    $report.after=$after
    $report.restorationVerified=($after.profile -eq $before.profile -and
        $after.fans[0].boost -eq $before.fans[0].boost -and $after.fans[1].boost -eq $before.fans[1].boost)
    $report.journalRemoved=(-not (Test-Path (Join-Path $root 'build\fanctl-session.txt')))
    if (-not $report.restorationVerified -or -not $report.journalRemoved) { throw 'Restauracao ou limpeza nao confirmada.' }
    $report.status='completed'
} catch {
    $report.status='failed'
    $report.error=$_.Exception.Message
} finally {
    if ($null -ne $before) {
        foreach ($fan in $before.fans) {
            try {
                $actual=(Invoke-CimMethod -InputObject $instance -MethodName Thermal_Information -Arguments @{arg2=[uint32](($fan.id -shl 8)-bor 12)} -OperationTimeoutSec 5).argr
                if ([uint32]$actual -ne $fan.boost) {
                    $report.emergencyRestoreUsed=$true
                    $response=Invoke-CimMethod -InputObject $instance -MethodName Thermal_Control -Arguments @{arg2=[uint32](($fan.boost -shl 16)-bor ($fan.id -shl 8)-bor 2)} -OperationTimeoutSec 5
                    if ($response.argr -ne 0) { throw 'Restauracao independente rejeitada.' }
                    $actual=(Invoke-CimMethod -InputObject $instance -MethodName Thermal_Information -Arguments @{arg2=[uint32](($fan.id -shl 8)-bor 12)} -OperationTimeoutSec 5).argr
                    if ([uint32]$actual -ne $fan.boost) { throw 'Restauracao independente nao confirmada.' }
                }
            } catch { $report.emergencyError=$_.Exception.Message }
        }
    }
    $report.finished=(Get-Date).ToString('o')
    $report | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $PSScriptRoot 'boost-cli-check.json')
}
