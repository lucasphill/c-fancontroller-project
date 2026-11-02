# Read-only probe. Only Thermal_Information queries are permitted.
# Reference: https://docs.kernel.org/wmi/devices/alienware-wmi.html
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$reportPath = Join-Path $PSScriptRoot '..\.scratch\fan-controller\probe-result.json'
$report = [ordered]@{ timestamp = (Get-Date).ToString('o'); status = 'started'; fans = @(); sensors = @() }
try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Execute este diagnostico como administrador.'
    }
    $instance = @(Get-CimInstance -Namespace root/WMI -ClassName AWCCWmiMethodFunction -OperationTimeoutSec 10)
    if ($instance.Count -ne 1) { throw "Esperada uma instancia WMI; encontradas $($instance.Count)." }
    function Read-Thermal([uint32]$operation, [uint32]$id = 0) {
        if ($operation -notin @(2, 3, 4, 5, 11, 12)) { throw 'Consulta fora da lista permitida.' }
        if ($id -gt 255) { throw 'ID fora do intervalo permitido.' }
        $arg = [uint32](($id -shl 8) -bor $operation)
        $response = Invoke-CimMethod -InputObject $instance[0] -MethodName Thermal_Information -Arguments @{arg2=$arg} -OperationTimeoutSec 10
        if ($null -eq $response.argr) { throw 'Resposta sem argr.' }
        $value = [uint32]$response.argr
        if ($value -eq [uint32]::MaxValue) { throw "Consulta $operation / ID $id indisponivel." }
        return $value
    }
    $description = Read-Thermal 2
    $fanCount = $description -band 255
    $sensorCount = ($description -shr 8) -band 255
    $report.description = $description
    if ($fanCount -lt 1 -or $fanCount -gt 8 -or $sensorCount -gt 32) {
        throw "Contagens inesperadas: fans=$fanCount sensores=$sensorCount. Nenhum ID sera sondado."
    }
    try { $report.profile = Read-Thermal 11 } catch { $report.profileError = $_.Exception.Message }
    for ($index = 0; $index -lt $fanCount; $index++) {
        $id = Read-Thermal 3 $index
        if ($id -lt 1 -or $id -gt 255) { throw 'ID de fan invalido.' }
        $fan = [ordered]@{ id=$id }
        try { $fan.rpm = Read-Thermal 5 $id } catch { $fan.rpmError = $_.Exception.Message }
        try { $fan.boost = Read-Thermal 12 $id } catch { $fan.boostError = $_.Exception.Message }
        $report.fans += $fan
    }
    for ($index = $fanCount; $index -lt ($fanCount + $sensorCount); $index++) {
        $resource = Read-Thermal 3 $index
        # Sensor resources carry a 0x100 type marker; queries use the low byte.
        # Confirmed against AlienFan SDK discovery, not guessed from temperature.
        if ($resource -le 256 -or $resource -ge 416) {
            $report.sensors += [ordered]@{ resource=$resource; error='Tipo de recurso inesperado; nao consultado.' }
            continue
        }
        $id = $resource -band 255
        $sensor = [ordered]@{ resource=$resource; id=$id }
        try { $sensor.temperatureC = Read-Thermal 4 $id } catch { $sensor.error = $_.Exception.Message }
        $report.sensors += $sensor
    }
    $report.status = 'completed'
} catch {
    $report.status = 'failed'
    $report.error = $_.Exception.Message
} finally {
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
}
if ($report.status -ne 'completed') { exit 1 }
