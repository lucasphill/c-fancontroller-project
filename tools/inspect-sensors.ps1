# Sensor diagnosis only. No Thermal_Control, driver installation or fan changes.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$path=Join-Path $PSScriptRoot '..\.scratch\fan-controller\sensor-investigation.json'
$report=[ordered]@{started=(Get-Date).ToString('o');status='running';phase='inventory';relations=@();samples=@();optionalSources=@()}
try {
    $report.cpu=@(Get-CimInstance Win32_Processor -OperationTimeoutSec 5 | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors)
    $report.gpus=@(Get-CimInstance Win32_VideoController -OperationTimeoutSec 5 | Select-Object Name)
    $instances=@(Get-CimInstance -Namespace root/WMI -ClassName AWCCWmiMethodFunction -OperationTimeoutSec 5)
    if ($instances.Count -ne 1) { throw 'Instancia Dell nao e unica.' }
    $instance=$instances[0]
    function Read-Thermal([uint32]$op,[uint32]$id=0) {
        if ($op -notin @(4,5,11,12) -or $id -gt 255) { throw 'Consulta termica nao permitida.' }
        $response=Invoke-CimMethod -InputObject $instance -MethodName Thermal_Information -Arguments @{arg2=[uint32](($id-shl 8)-bor $op)} -OperationTimeoutSec 5
        if ($null -eq $response.argr -or [uint32]$response.argr -ge 4294967280) { throw 'Consulta Dell indisponivel.' }
        return [uint32]$response.argr
    }
    function Read-Dell {
        return [ordered]@{time=(Get-Date).ToString('o');sensor1=(Read-Thermal 4 1);sensor6=(Read-Thermal 4 6);profile=(Read-Thermal 11);fan50rpm=(Read-Thermal 5 50);fan51rpm=(Read-Thermal 5 51);boost50=(Read-Thermal 12 50);boost51=(Read-Thermal 12 51)}
    }
    foreach ($id in @(50,51)) {
        $count=(Invoke-CimMethod -InputObject $instance -MethodName GetFanSensors -Arguments @{arg2=[uint32](($id-shl 8)-bor 1)} -OperationTimeoutSec 5).argr
        if ($null -eq $count -or [uint32]$count -gt 16) { throw 'Contagem de sensores inesperada.' }
        $relation=[ordered]@{fan=$id;count=$count;sensors=@()}
        for ($index=0;$index -lt $count;$index++) {
            $sensor=(Invoke-CimMethod -InputObject $instance -MethodName GetFanSensors -Arguments @{arg2=[uint32](($index-shl 16)-bor ($id-shl 8)-bor 2)} -OperationTimeoutSec 5).argr
            $relation.sensors += $sensor
        }
        $report.relations += $relation
    }
    foreach ($source in @(
        @{namespace='root\WMI';class='MSAcpi_ThermalZoneTemperature';properties=@('InstanceName','CurrentTemperature')},
        @{namespace='root\LibreHardwareMonitor';class='Sensor';properties=@('Name','SensorType','Value')},
        @{namespace='root\OpenHardwareMonitor';class='Sensor';properties=@('Name','SensorType','Value')}
    )) {
        $entry=[ordered]@{namespace=$source.namespace;class=$source.class;available=$false}
        try {
            $entry.values=@(Get-CimInstance -Namespace $source.namespace -ClassName $source.class -OperationTimeoutSec 5 | Select-Object -Property $source.properties)
            $entry.available=$true
        } catch { $entry.error=$_.Exception.Message }
        $report.optionalSources += $entry
    }
    foreach ($phase in @('dell-only','dell-and-nvidia')) {
        $report.phase=$phase
        for ($index=0;$index -lt 8;$index++) {
            $sample=[ordered]@{phase=$phase;dellBefore=(Read-Dell)}
            if ($phase -eq 'dell-and-nvidia') {
                $sample.nvidiaTime=(Get-Date).ToString('o')
                $sample.nvidiaRaw=@(& "$env:SystemRoot\System32\nvidia-smi.exe" --query-gpu=name,temperature.gpu,pstate,utilization.gpu,power.draw --format=csv,noheader,nounits 2>&1 | ForEach-Object { "$_" })
                $sample.nvidiaExit=$LASTEXITCODE
                $sample.dellAfter=Read-Dell
            }
            $report.samples += $sample
            $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding UTF8
            if ($index -lt 7) { Start-Sleep -Seconds 2 }
        }
    }
    $report.status='completed'
} catch { $report.status='failed';$report.error=$_.Exception.Message }
finally {
    $report.finished=(Get-Date).ToString('o')
    $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding UTF8
}
