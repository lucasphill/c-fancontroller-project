# Diagnosis only: no Thermal_Control calls and no changes to an active session.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$path=Join-Path $PSScriptRoot '..\.scratch\fan-controller\fan-observation.json'
$report=[ordered]@{started=(Get-Date).ToString('o');status='running';samples=@()}
try {
    $instance=Get-CimInstance -Namespace root/WMI -ClassName AWCCWmiMethodFunction -OperationTimeoutSec 5
    function Read-Value([uint32]$op,[uint32]$id=0) {
        if ($op -notin @(4,5,6,8,9,11,12) -or $id -gt 255) { throw 'Consulta nao permitida.' }
        $response=Invoke-CimMethod -InputObject $instance -MethodName Thermal_Information -Arguments @{arg2=[uint32](($id-shl 8)-bor $op)} -OperationTimeoutSec 5
        if ($null -eq $response.argr -or [uint32]$response.argr -eq [uint32]::MaxValue) { return $null }
        if ($op -eq 6 -and [uint32]$response.argr -gt 100) { return $null }
        return [uint32]$response.argr
    }
    $report.processes=@(Get-CimInstance Win32_Process -Filter "Name='fanctl.exe'" | Select-Object ProcessId,CreationDate,CommandLine)
    $report.otherControllers=@(Get-Process | Where-Object { $_.ProcessName -match 'AWCC|Alienware|AlienFan|tcc.?g15|Afterburner' } | Select-Object ProcessName,Id)
    $report.fanLimits=@(foreach ($id in @(50,51)) { [ordered]@{id=$id;minRpm=(Read-Value 8 $id);maxRpm=(Read-Value 9 $id)} })
    for ($index=0;$index -lt 8;$index++) {
        $sample=[ordered]@{time=(Get-Date).ToString('o');profile=(Read-Value 11);fans=@();sensors=@()}
        foreach ($id in @(50,51)) { $sample.fans += [ordered]@{id=$id;boost=(Read-Value 12 $id);rpm=(Read-Value 5 $id);firmwarePercent=(Read-Value 6 $id)} }
        foreach ($id in @(1,6)) { $sample.sensors += [ordered]@{id=$id;temperatureC=(Read-Value 4 $id)} }
        $report.samples += $sample
        $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
        if ($index -lt 7) { Start-Sleep -Seconds 2 }
    }
    $report.status='completed'
} catch { $report.status='failed';$report.error=$_.Exception.Message }
finally {
    $report.finished=(Get-Date).ToString('o')
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
}
