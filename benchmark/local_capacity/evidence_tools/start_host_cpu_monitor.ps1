param([int]$DurationSeconds=360)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskFolder=Join-Path $taskRoot ('evidence/host-cpu-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'))
if(Test-Path -LiteralPath $taskFolder){throw 'Preserve previous observer'}
New-Item -ItemType Directory -Path $taskFolder|Out-Null
$taskAudit=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');operation='Ownhiddenreadonly WindowsCPU observer';scope=$taskFolder;duration_seconds=$DurationSeconds;writes=@('audit-before.json','monitor-process.json','host-cpu.jsonl','monitor-ended.json');reads='NumericCPUtotal/top12name/PID/CPU/privateRSS only; no args/privatecontent/env';impact='CIMevery5s, elapsedcaptured; alluserapps preserved';system_changes=@();rollback='Onlyownstop.request exits observer within5s; retainlogs'}
[IO.File]::WriteAllText((Join-Path $taskFolder 'audit-before.json'),($taskAudit|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
$taskPwsh='C:\Users\Administrator\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe'
$taskObserver=Start-Process -FilePath $taskPwsh -ArgumentList @('-NoProfile','-File',(Join-Path $PSScriptRoot 'monitor_host_cpu.ps1'),'-OutputDirectory',$taskFolder,'-DurationSeconds',"$DurationSeconds") -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru
$taskRecord=[ordered]@{pid=$taskObserver.Id;started_utc=$taskObserver.StartTime.ToUniversalTime().ToString('o');script_sha256=(Get-FileHash (Join-Path $PSScriptRoot 'monitor_host_cpu.ps1') -Algorithm SHA256).Hash.ToLowerInvariant();evidence_directory=$taskFolder}
[IO.File]::WriteAllText((Join-Path $taskFolder 'monitor-process.json'),($taskRecord|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
$taskRecord|ConvertTo-Json
