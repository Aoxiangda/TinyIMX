param([Parameter(Mandatory=$true)][string]$OutputDirectory,[int]$DurationSeconds=300)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskResolved=[IO.Path]::GetFullPath($OutputDirectory)
$taskEvidenceRoot=[IO.Path]::GetFullPath((Join-Path $taskRoot 'evidence'))+[IO.Path]::DirectorySeparatorChar
if(-not $taskResolved.StartsWith($taskEvidenceRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Observer output outside task evidence'}
if(-not (Test-Path -LiteralPath (Join-Path $taskResolved 'audit-before.json'))){throw 'Missing own observer audit'}
$taskEnd=[DateTime]::UtcNow.AddSeconds($DurationSeconds)
while([DateTime]::UtcNow -lt $taskEnd -and -not (Test-Path -LiteralPath (Join-Path $taskResolved 'stop.request'))){
 $taskTimer=[Diagnostics.Stopwatch]::StartNew()
 try {
  $taskCpu=Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'"
  $taskProcesses=Get-CimInstance Win32_PerfFormattedData_PerfProc_Process | Where-Object { $_.Name -notin @('_Total','Idle') -and $_.PercentProcessorTime -gt 0 } | Sort-Object PercentProcessorTime -Descending | Select-Object -First 12
  $taskRows=@($taskProcesses|ForEach-Object {[ordered]@{name=$_.Name;pid=$_.IDProcess;percent_processor_time_onecore100=$_.PercentProcessorTime;private_working_set_mib=[Math]::Round($_.WorkingSetPrivate/1MB,2)}})
  $taskTimer.Stop()
  $taskRow=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');status='OBSERVED';host_total_percent_processor_time=$taskCpu.PercentProcessorTime;host_total_percent_privileged_time=$taskCpu.PercentPrivilegedTime;top_processes=$taskRows;collect_elapsed_ms=$taskTimer.Elapsed.TotalMilliseconds;interpretation='Process percent uses one logical CPU as100; host Total is0to100. Numeric snapshot only, no argument/privatecontent or app control'}
 } catch { $taskTimer.Stop();$taskRow=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');status='SAMPLING_ERROR';error_type=$_.Exception.GetType().Name;collect_elapsed_ms=$taskTimer.Elapsed.TotalMilliseconds} }
 [IO.File]::AppendAllText((Join-Path $taskResolved 'host-cpu.jsonl'),(($taskRow|ConvertTo-Json -Compress -Depth 5)+"`n"),[Text.UTF8Encoding]::new($false))
 Start-Sleep -Seconds 5
}
[IO.File]::WriteAllText((Join-Path $taskResolved 'monitor-ended.json'),(@{utc=[DateTime]::UtcNow.ToString('o');status='ENDED'}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
