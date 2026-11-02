# Controlled, temporary boost experiment. Does not change thermal profiles.
# Protocol: https://docs.kernel.org/wmi/devices/alienware-wmi.html
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$reportPath=Join-Path $PSScriptRoot '..\.scratch\fan-controller\boost-test.json'
$report=[ordered]@{started=(Get-Date).ToString('o');status='starting';trials=@();samples=@();writes=@();restoration=@()}
$original=$null
$pending=@{}
$instance=$null
$mutex=$null
$ownsMutex=$false
function Save-Report { $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $reportPath -Encoding UTF8 }
function Read-Thermal([uint32]$op,[uint32]$id=0) {
    if ($op -notin @(2,3,4,5,11,12) -or $id -gt 255) { throw 'Consulta nao permitida.' }
    $response=Invoke-CimMethod -InputObject $instance -MethodName Thermal_Information -Arguments @{arg2=[uint32](($id -shl 8) -bor $op)} -OperationTimeoutSec 5
    if ($null -eq $response.argr) { throw 'Resposta WMI vazia.' }
    $value=[uint32]$response.argr
    if ($value -eq [uint32]::MaxValue) { throw "Consulta indisponivel: $op / $id" }
    return $value
}
function Read-State {
    $state=[ordered]@{timestamp=(Get-Date).ToString('o');profile=(Read-Thermal 11);fans=@();sensors=@()}
    foreach ($id in @(50,51)) {
        $state.fans += [ordered]@{id=$id;boost=(Read-Thermal 12 $id);rpm=(Read-Thermal 5 $id)}
    }
    foreach ($id in @(1,6)) { $state.sensors += [ordered]@{id=$id;temperatureC=(Read-Thermal 4 $id)} }
    return $state
}
function Write-Boost([uint32]$id,[uint32]$value,[string]$purpose) {
    if ($id -notin @(50,51) -or $value -gt 60) { throw 'Escrita fora dos limites deste experimento.' }
    $entry=[ordered]@{timestamp=(Get-Date).ToString('o');id=$id;value=$value;purpose=$purpose;status='pending'}
    $report.writes += $entry
    # Record new adjustments before sending them; journal failure must not block restoration.
    try { Save-Report } catch { if ($purpose -ne 'restore') { throw } }
    $response=Invoke-CimMethod -InputObject $instance -MethodName Thermal_Control -Arguments @{arg2=[uint32](($value -shl 16) -bor ($id -shl 8) -bor 2)} -OperationTimeoutSec 5
    $entry.returnValue=$response.argr
    if ($null -eq $response.argr -or [uint32]$response.argr -ne 0) {
        $entry.status='failed'
        throw "Boost rejeitado: fan $id, retorno $($response.argr)"
    }
    $entry.status='accepted'
}
function Restore-Fan([uint32]$id,[uint32]$value) {
    # A failed setter response can still have changed state: always read back.
    $commandError=$null
    try { Write-Boost $id $value 'restore' } catch { $commandError=$_.Exception.Message }
    $actual=Read-Thermal 12 $id
    $entry=[ordered]@{timestamp=(Get-Date).ToString('o');id=$id;expected=$value;actual=$actual;verified=($actual -eq $value);commandError=$commandError}
    $report.restoration += $entry
    if ($actual -ne $value) { throw "Restauracao nao confirmada: fan $id, esperado $value, atual $actual" }
    $pending.Remove([int]$id)
}
try {
    $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
    $principal=[Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrador necessario.' }
    $model=(Get-CimInstance Win32_ComputerSystem -OperationTimeoutSec 5).Model
    if ($model -ne 'Dell G15 5530') { throw "Modelo inesperado: $model" }
    $competing=@(Get-Process | Where-Object { $_.ProcessName -match 'AWCC|Alienware|AlienFan|tcc.?g15|Afterburner' })
    if ($competing.Count) { throw "Possivel controlador ativo: $($competing.ProcessName -join ', ')" }
    $mutex=[Threading.Mutex]::new($false,'Global\FanControllerBoostTest')
    try { $ownsMutex=$mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $ownsMutex=$true }
    if (-not $ownsMutex) { throw 'Outro teste de boost esta ativo.' }
    $instances=@(Get-CimInstance -Namespace root/WMI -ClassName AWCCWmiMethodFunction -OperationTimeoutSec 5)
    if ($instances.Count -ne 1) { throw 'Instancia WMI nao e unica.' }
    $instance=$instances[0]
    $description=Read-Thermal 2
    if (($description -band 255) -ne 2 -or (($description -shr 8) -band 255) -ne 2) { throw 'Topologia inesperada.' }
    if ((Read-Thermal 3 0) -ne 50 -or (Read-Thermal 3 1) -ne 51 -or
        (Read-Thermal 3 2) -ne 257 -or (Read-Thermal 3 3) -ne 262) { throw 'IDs inesperados.' }
    $original=Read-State
    $report.original=$original
    foreach ($fan in $original.fans) { if ($fan.boost -gt 40) { throw 'Boost atual alto demais para este teste moderado.' } }
    $report.status='running'
    Save-Report
    foreach ($id in @(50,51)) {
        $before=Read-State
        if ($before.profile -ne $original.profile) { throw 'Perfil alterado externamente.' }
        foreach ($fan in $before.fans) {
            $saved=@($original.fans | Where-Object { $_.id -eq $fan.id })[0]
            if ($fan.boost -ne $saved.boost) { throw 'Boost alterado externamente.' }
        }
        $saved=@($original.fans | Where-Object { $_.id -eq $id })[0]
        $target=[Math]::Max(30,([int]$saved.boost+20))
        $trial=[ordered]@{id=$id;target=$target;before=$before;status='starting'}
        $report.trials += $trial
        $pending[[int]$id]=[uint32]$saved.boost
        try {
            Write-Boost $id $target 'temporary-increase'
            $readback=Read-Thermal 12 $id
            $trial.readback=$readback
            if ($readback -ne $target) { throw "Boost nao confirmado no perfil atual: solicitado $target, lido $readback" }
            for ($sample=0;$sample -lt 6;$sample++) {
                Start-Sleep -Seconds 2
                $state=Read-State
                $report.samples += [ordered]@{activeFan=$id;state=$state}
                if ($state.profile -ne $original.profile) { throw 'Perfil alterado durante o teste.' }
                foreach ($fan in $state.fans) {
                    $expected=if ($fan.id -eq $id) { $target } else { @($original.fans | Where-Object { $_.id -eq $fan.id })[0].boost }
                    if ($fan.boost -ne $expected) { throw 'Boost alterado durante o teste.' }
                }
                Save-Report
            }
            $trial.status='completed'
        } finally {
            Restore-Fan $id $saved.boost
            Save-Report
        }
        Start-Sleep -Seconds 3
    }
    $report.status='completed'
} catch {
    $report.status='failed'
    $report.error=$_.Exception.Message
} finally {
    foreach ($id in @($pending.Keys)) {
        try { Restore-Fan $id $pending[$id] } catch { $report.emergencyRestoreError=$_.Exception.Message }
    }
    if ($null -ne $original) {
        try {
            $report.final=Read-State
            $restored=($report.final.profile -eq $original.profile)
            foreach ($fan in $report.final.fans) {
                $saved=@($original.fans | Where-Object { $_.id -eq $fan.id })[0]
                $restored=$restored -and ($fan.boost -eq $saved.boost)
            }
            $report.restorationVerified=$restored
        } catch { $report.restorationVerified=$false; $report.finalReadError=$_.Exception.Message }
        if (-not $report.restorationVerified) { $report.status='restoration-unverified' }
    }
    $report.finished=(Get-Date).ToString('o')
    Save-Report
    if ($ownsMutex) { $mutex.ReleaseMutex() }
    if ($null -ne $mutex) { $mutex.Dispose() }
}
if ($report.status -ne 'completed') { exit 1 }
