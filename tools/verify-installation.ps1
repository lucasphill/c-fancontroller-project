[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root '.scratch/fan-controller/installation-check.json'
$report=[ordered]@{started=(Get-Date).ToString('o');status='running'}
try {
    $installed=Join-Path $env:ProgramFiles 'FanController'
    & (Join-Path $installed 'manage-startup.ps1') -Mode Verify | Out-Null
    $task=Get-ScheduledTask -TaskName 'FanController - Lucas'
    $report.task=[ordered]@{name=[string]$task.TaskName;state=[string]$task.State;user=[string]$task.Principal.UserId;runLevel=[string]$task.Principal.RunLevel;logonType=[string]$task.Principal.LogonType;arguments=[string]$task.Actions.Arguments;delay=[string]$task.Triggers.Delay;executionTimeLimit=[string]$task.Settings.ExecutionTimeLimit}
    if ($task.State -ne 'Running') {throw 'Tarefa nao esta em execucao.'}
    $processes=@(Get-CimInstance Win32_Process -Filter "Name='FanController.exe'")
    $report.processes=@($processes | Select-Object ProcessId,ExecutablePath,CommandLine)
    if ($processes.Count -ne 1 -or $processes[0].ExecutablePath -ne (Join-Path $installed 'FanController.exe') -or $processes[0].CommandLine -notmatch '--startup') {throw 'Processo instalado nao confere.'}
    $report.hidden=(Get-Process -Id $processes[0].ProcessId).MainWindowHandle -eq 0
    $before=$processes[0].ProcessId
    Start-ScheduledTask -TaskName $task.TaskName
    Start-Sleep -Seconds 2
    $after=@(Get-CimInstance Win32_Process -Filter "Name='FanController.exe'")
    if ($after.Count -ne 1 -or $after[0].ProcessId -ne $before) {throw 'A tarefa iniciou instancia duplicada.'}
    $report.ignoreDuplicateRun=$true
    # ReadAllText avoids provider annotations on strings in Windows PowerShell 5.1 JSON.
    $report.lastMode=[System.IO.File]::ReadAllText((Join-Path $installed 'fanctl-startup.txt'))
    $report.journal=[System.IO.File]::ReadAllText((Join-Path $installed 'fanctl-session.txt'))
    $report.binaryHash=(Get-FileHash -LiteralPath (Join-Path $installed 'FanController.exe')).Hash
    if ($report.binaryHash -ne (Get-FileHash -LiteralPath (Join-Path $root 'build/package/FanController.exe')).Hash) {throw 'Binario instalado difere do pacote.'}
    $instance=Get-CimInstance -Namespace root/WMI -ClassName AWCCWmiMethodFunction -OperationTimeoutSec 5
    function Read-Thermal([uint32]$op,[uint32]$id=0) {
        if ($op -notin @(4,5,11,12)) {throw 'Consulta nao permitida.'}
        $response=Invoke-CimMethod -InputObject $instance -MethodName Thermal_Information -Arguments @{arg2=[uint32](($id-shl 8)-bor $op)} -OperationTimeoutSec 5
        return [uint32]$response.argr
    }
    $report.samples=@(for ($i=0;$i -lt 4;$i++) {
        [ordered]@{time=(Get-Date).ToString('o');profile=(Read-Thermal 11);cpuProbable=(Read-Thermal 4 1);fan50Boost=(Read-Thermal 12 50);fan51Boost=(Read-Thermal 12 51);fan50Rpm=(Read-Thermal 5 50);fan51Rpm=(Read-Thermal 5 51)}
        if ($i -lt 3) {Start-Sleep -Seconds 2}
    })
    foreach ($sample in $report.samples) {
        if ($sample.profile -ne 0 -or $sample.fan50Boost -lt 30 -or $sample.fan51Boost -lt 30 -or
            $sample.fan50Boost -gt 100 -or $sample.fan51Boost -gt 100 -or $sample.fan50Rpm -eq 0 -or $sample.fan51Rpm -eq 0) {throw 'Controle esperado nao foi observado.'}
    }
    $report.status='passed'
} catch {$report.status='failed';$report.error=$_.Exception.Message}
finally {$report.finished=(Get-Date).ToString('o');$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $output -Encoding UTF8}
if ($report.status -ne 'passed') {throw $report.error}
