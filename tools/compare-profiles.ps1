# Authorized experiment: keep boost 30, compare Balanced 160 with Custom 0.
# Always attempt to restore the captured profile and boosts, including partial writes.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$reportPath=Join-Path $PSScriptRoot '..\.scratch\fan-controller\profile-comparison.json'
$report=[ordered]@{started=(Get-Date).ToString('o');status='starting';phase='preflight';samples=@();writes=@();restoreErrors=@()}
$original=$null
$instance=$null
$touched=$false
$mutex=$null
$owned=$false
function Save-Report { $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $reportPath -Encoding UTF8 }
function Read-Value([uint32]$op,[uint32]$id=0) {
    if ($op -notin @(2,3,4,5,11,12) -or $id -gt 255) { throw 'Consulta fora da lista permitida.' }
    $response=Invoke-CimMethod -InputObject $instance -MethodName Thermal_Information -Arguments @{arg2=[uint32](($id-shl 8)-bor $op)} -OperationTimeoutSec 5
    if ($null -eq $response.argr -or [uint32]$response.argr -ge 4294967280) { throw "Consulta indisponivel: $op/$id" }
    return [uint32]$response.argr
}
function Read-State {
    $state=[ordered]@{time=(Get-Date).ToString('o');profile=(Read-Value 11);fans=@();sensors=@()}
    foreach ($id in @(50,51)) { $state.fans += [ordered]@{id=$id;boost=(Read-Value 12 $id);rpm=(Read-Value 5 $id)} }
    foreach ($id in @(1,6)) { $state.sensors += [ordered]@{id=$id;temperatureC=(Read-Value 4 $id)} }
    return $state
}
function Send-Control([uint32]$op,[uint32]$id,[uint32]$value,[bool]$restoring=$false) {
    if ($op -eq 1) {
        if ($id -notin @(0,160) -or $value -ne 0) { throw 'Perfil fora da lista permitida.' }
    } elseif ($op -eq 2) {
        if ($id -notin @(50,51) -or $value -notin @(0,30)) { throw 'Boost fora da lista permitida.' }
    } else { throw 'Operacao de controle nao permitida.' }
    $entry=[ordered]@{time=(Get-Date).ToString('o');op=$op;id=$id;value=$value;restoring=$restoring;status='pending'}
    $report.writes += $entry
    try { Save-Report } catch { if (-not $restoring) { throw } }
    $script:touched=$true # A failed response can still mean firmware accepted the write.
    $response=Invoke-CimMethod -InputObject $instance -MethodName Thermal_Control -Arguments @{arg2=[uint32](($value-shl 16)-bor ($id-shl 8)-bor $op)} -OperationTimeoutSec 5
    $entry.returnValue=$response.argr
    if ($null -eq $response.argr -or [uint32]$response.argr -ne 0) {
        $entry.status='failed'; throw "Controle rejeitado: op=$op id=$id"
    }
    $entry.status='accepted'
}
try {
    $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
    $principal=[Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrador necessario.' }
    if ((Get-CimInstance Win32_ComputerSystem -OperationTimeoutSec 5).Model -ne 'Dell G15 5530') { throw 'Modelo inesperado.' }
    $mutex=[Threading.Mutex]::new($false,'Global\FanControllerBoost')
    try { $owned=$mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $owned=$true }
    if (-not $owned) { throw 'Uma sessao de controle esta ativa.' }
    if (Test-Path (Join-Path $PSScriptRoot '..\build\fanctl-session.txt')) { throw 'Registro anterior do fanctl ainda existe.' }
    $other=@(Get-Process | Where-Object { $_.ProcessName -match 'AWCC|Alienware|AlienFan|tcc.?g15|Afterburner|^fanctl$' })
    if ($other.Count) { throw "Processo concorrente: $($other.ProcessName -join ', ')" }
    $instances=@(Get-CimInstance -Namespace root/WMI -ClassName AWCCWmiMethodFunction -OperationTimeoutSec 5)
    if ($instances.Count -ne 1) { throw 'Instancia WMI nao e unica.' }
    $instance=$instances[0]
    $description=Read-Value 2
    if (($description-band 255) -ne 2 -or (($description-shr 8)-band 255) -ne 2 -or
        (Read-Value 3 0) -ne 50 -or (Read-Value 3 1) -ne 51 -or
        (Read-Value 3 2) -ne 257 -or (Read-Value 3 3) -ne 262) { throw 'Topologia inesperada.' }
    $original=Read-State
    $report.original=$original
    if ($original.profile -ne 160 -or @($original.fans | Where-Object { $_.boost -ne 0 }).Count) {
        throw 'Teste exige perfil original 160 e boosts 0 nas duas fans.'
    }
    Save-Report
    $report.status='running'
    foreach ($profile in @(160,0)) {
        $report.phase=if ($profile -eq 160) { 'balanced-30' } else { 'custom-30' }
        if ((Read-Value 11) -ne $profile) { Send-Control 1 $profile 0 }
        if ((Read-Value 11) -ne $profile) { throw 'Perfil solicitado nao confirmado.' }
        foreach ($id in @(50,51)) { Send-Control 2 $id 30 }
        for ($index=0;$index -lt 15;$index++) {
            Start-Sleep -Seconds 2
            $state=Read-State
            $report.samples += [ordered]@{phase=$report.phase;state=$state}
            if ($state.profile -ne $profile -or @($state.fans | Where-Object { $_.boost -ne 30 }).Count) {
                throw 'Perfil ou boost mudou durante a condicao.'
            }
            if (@($state.sensors | Where-Object { $_.temperatureC -ge 90 }).Count) { throw 'Temperatura alta: interrompendo e restaurando.' }
            Save-Report
        }
    }
    $report.status='completed'
} catch { $report.status='failed';$report.error=$_.Exception.Message }
finally {
    if ($touched -and $null -ne $original) {
        $report.phase='restoring'
        # Restore profile first; switching it may itself change stored fan settings.
        for ($attempt=0;$attempt -lt 2;$attempt++) {
            try {
                Send-Control 1 $original.profile 0 $true
                if ((Read-Value 11) -eq $original.profile) { break }
            } catch { $report.restoreErrors += $_.Exception.Message }
        }
        foreach ($fan in $original.fans) {
            for ($attempt=0;$attempt -lt 2;$attempt++) {
                try { Send-Control 2 $fan.id $fan.boost $true } catch { $report.restoreErrors += $_.Exception.Message }
                try { if ((Read-Value 12 $fan.id) -eq $fan.boost) { break } } catch { $report.restoreErrors += $_.Exception.Message }
            }
        }
        try {
            $report.final=Read-State
            $verified=($report.final.profile -eq $original.profile)
            foreach ($fan in $report.final.fans) {
                $saved=@($original.fans | Where-Object { $_.id -eq $fan.id })[0]
                $verified=$verified -and ($fan.boost -eq $saved.boost)
            }
            $report.restorationVerified=$verified
        } catch { $report.restorationVerified=$false;$report.finalReadError=$_.Exception.Message }
        if (-not $report.restorationVerified) { $report.status='restoration-unverified' }
    }
    $report.finished=(Get-Date).ToString('o')
    try { Save-Report } finally {
        if ($owned) { $mutex.ReleaseMutex() }
        if ($null -ne $mutex) { $mutex.Dispose() }
    }
}
if ($report.status -ne 'completed') { exit 1 }
