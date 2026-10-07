param()
$ErrorActionPreference = 'Stop'
$taskWorkspace = Split-Path $PSScriptRoot -Parent
$taskAudit = Join-Path $taskWorkspace ('evidence\ollama-start-' + (Get-Date -Format 'yyyyMMdd-HHmmss-ffff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $taskAudit | Out-Null
$taskExe = Join-Path $env:LOCALAPPDATA 'Programs\Ollama\ollama.exe'
$taskListeners = @(Get-NetTCPConnection -LocalPort 11434 -State Listen -ErrorAction SilentlyContinue | Select-Object LocalAddress,LocalPort,OwningProcess)
[ordered]@{utc=[DateTimeOffset]::UtcNow.ToString('o');operation='check existing local Ollama; start installed serve only if port is free';listeners=$taskListeners;executable=$taskExe;executable_sha256=$(if(Test-Path -LiteralPath $taskExe){(Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash}else{$null});model='qwen3:0.6b';download=$false;delete=$false;global_environment_change=$false;other_processes_stopped=$false} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskAudit 'audit-before.json') -Encoding utf8
$taskStarted = $null
if($taskListeners.Count -eq 0){
    if(!(Test-Path -LiteralPath $taskExe)){throw 'Installed Ollama executable is missing; no installation changes were made'}
    $taskNames = @('OLLAMA_HOST','OLLAMA_NUM_PARALLEL','OLLAMA_MAX_LOADED_MODELS')
    $taskSaved = @{}
    foreach($taskName in $taskNames){$taskSaved[$taskName]=[Environment]::GetEnvironmentVariable($taskName,'Process')}
    try{
        [Environment]::SetEnvironmentVariable('OLLAMA_HOST','127.0.0.1:11434','Process')
        [Environment]::SetEnvironmentVariable('OLLAMA_NUM_PARALLEL','1','Process')
        [Environment]::SetEnvironmentVariable('OLLAMA_MAX_LOADED_MODELS','1','Process')
        $taskStarted=Start-Process -FilePath $taskExe -ArgumentList @('serve') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskAudit 'stdout.log') -RedirectStandardError (Join-Path $taskAudit 'stderr.log')
    }finally{foreach($taskName in $taskNames){[Environment]::SetEnvironmentVariable($taskName,$taskSaved[$taskName],'Process')}}
}
$taskDeadline=[DateTime]::UtcNow.AddSeconds(30)
$taskTags=$null
do{
    try{$taskTags=Invoke-RestMethod -Uri 'http://127.0.0.1:11434/api/tags' -TimeoutSec 2}catch{if($taskStarted -eq $null){throw 'Port 11434 is occupied but the local Ollama API is unavailable; existing processes were preserved'}}
    if($taskTags -ne $null){break}
    Start-Sleep -Milliseconds 500
}while([DateTime]::UtcNow -lt $taskDeadline)
if($taskTags -eq $null){throw ('Ollama readiness timed out; logs retained in '+$taskAudit)}
$taskModels=@($taskTags.models | ForEach-Object {$_.name})
$taskReady=$taskModels -contains 'qwen3:0.6b'
[ordered]@{status=$(if($taskReady){'READY'}else{'MODEL_MISSING'});endpoint='http://127.0.0.1:11434';models=$taskModels;started_pid=$(if($taskStarted){$taskStarted.Id}else{$null});existing_service_preserved=($taskStarted -eq $null);audit=$taskAudit} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskAudit 'summary.json') -Encoding utf8
if(!$taskReady){throw 'qwen3:0.6b is not installed in this service; no models were downloaded or removed'}
Write-Output 'Ollama is ready at http://127.0.0.1:11434; use qwen3:0.6b in the client AI page.'
