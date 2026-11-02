# Functional identification: one low-priority CPU thread for at most 12 seconds.
# No fan/profile changes and no GPU workload.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$path=Join-Path $PSScriptRoot '..\.scratch\fan-controller\cpu-sensor-check.json'
$report=[ordered]@{started=(Get-Date).ToString('o');status='running';samples=@()}
$worker=$null
try {
    Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Threading;
public static class FanControllerCpuPulse {
    public static double Sink;
    public static Thread Start() {
        Thread thread = new Thread(() => {
            Stopwatch timer = Stopwatch.StartNew();
            double sum = 0;
            while (timer.ElapsedMilliseconds < 12000) {
                for (int i = 1; i < 20000; ++i) sum += Math.Sqrt(i);
            }
            Sink = sum;
        });
        thread.IsBackground = true;
        thread.Priority = ThreadPriority.BelowNormal;
        thread.Start();
        return thread;
    }
}
'@
    $instance=Get-CimInstance -Namespace root/WMI -ClassName AWCCWmiMethodFunction -OperationTimeoutSec 5
    function Read-Value([uint32]$op,[uint32]$id=0) {
        if ($op -notin @(4,5,11,12)) { throw 'Consulta nao permitida.' }
        $response=Invoke-CimMethod -InputObject $instance -MethodName Thermal_Information -Arguments @{arg2=[uint32](($id-shl 8)-bor $op)} -OperationTimeoutSec 5
        if ($null -eq $response.argr -or [uint32]$response.argr -ge 4294967280) { throw 'Consulta indisponivel.' }
        return [uint32]$response.argr
    }
    $report.original=[ordered]@{profile=(Read-Value 11);boost50=(Read-Value 12 50);boost51=(Read-Value 12 51)}
    foreach ($phase in @('baseline','cpu-pulse','cooldown')) {
        if ($phase -eq 'cpu-pulse') { $worker=[FanControllerCpuPulse]::Start() }
        $count=if ($phase -eq 'cooldown') { 8 } else { 6 }
        for ($index=0;$index -lt $count;$index++) {
            $sample=[ordered]@{time=(Get-Date).ToString('o');phase=$phase;sensor1=(Read-Value 4 1);sensor6=(Read-Value 4 6);profile=(Read-Value 11);boost50=(Read-Value 12 50);boost51=(Read-Value 12 51);cpuWorkerAlive=($null -ne $worker -and $worker.IsAlive)}
            $report.samples += $sample
            if ($sample.sensor1 -ge 90 -or $sample.sensor6 -ge 90) { throw 'Temperatura alta: encerrando diagnostico.' }
            $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $path -Encoding UTF8
            Start-Sleep -Seconds 2
        }
        if ($phase -eq 'cpu-pulse' -and $worker.IsAlive) { [void]$worker.Join(15000) }
    }
    $report.status='completed'
} catch { $report.status='failed';$report.error=$_.Exception.Message }
finally {
    if ($null -ne $worker -and $worker.IsAlive) { [void]$worker.Join(15000) }
    $report.finished=(Get-Date).ToString('o')
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $path -Encoding UTF8
}
