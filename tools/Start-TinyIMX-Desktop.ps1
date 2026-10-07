param(
    [string]$EvidenceName = 'qt6-ui-features-20261007-attempt9',
    [ValidateRange(1,3)][int]$Count = 1,
    [string]$Endpoint = '192.168.220.128:9000',
    [switch]$Demo, [switch]$Editor
)
$ErrorActionPreference = 'Stop'
if ($EvidenceName -notmatch '^qt6-ui-[a-zA-Z0-9-]+$') { throw 'Invalid evidence directory name' }
if ($Endpoint -notmatch '^[a-zA-Z0-9.-]+:[0-9]{1,5}$') { throw 'Use host:port' }
$taskWorkspace = Split-Path $PSScriptRoot -Parent
$taskExe = Join-Path $taskWorkspace ('evidence\' + $EvidenceName + '\build\tinyimx_desktop.exe')
if (!(Test-Path -LiteralPath $taskExe)) { throw 'The verified client build is missing; see the startup guide' }
$taskNames = @('desktop_alice_20261007','desktop_bob_20261007','desktop_carol_20261007')
$taskLaunched = @()
for ($taskIndex = 0; $taskIndex -lt $Count; $taskIndex++) {
    $taskArguments = @('--endpoint',$Endpoint,'--username',$taskNames[$taskIndex])
    if ($Demo) { $taskArguments += '--demo' }
    # Visible windows are intentional: the user requested multiple interactive clients.
    $taskClient = Start-Process -FilePath $taskExe -ArgumentList $taskArguments -WorkingDirectory (Split-Path $taskExe) -WindowStyle Normal -PassThru
    $taskLaunched += [ordered]@{pid=$taskClient.Id;username_prefill=$taskNames[$taskIndex];password_on_command_line=$false;mode=$(if($Demo){'demo'}else{'real'})}
}
$taskReceipt = Join-Path $taskWorkspace ('evidence\qt6-client-launch-' + (Get-Date -Format 'yyyyMMdd-HHmmss-ffff') + '.json')
[ordered]@{utc=[DateTimeOffset]::UtcNow.ToString('o');exe=$taskExe;exe_sha256=(Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash;clients=$taskLaunched;endpoint=$Endpoint;pressure='paused'} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $taskReceipt -Encoding utf8
Write-Output ('Started ' + $Count + ' independent windows. Log in with different demo accounts; demo password: 123456.')
if ($Editor) {
    $taskProject = Join-Path $taskWorkspace 'source\clients\qt6\CMakeLists.txt'
    if (!(Test-Path -LiteralPath $taskProject)) { $taskProject = Join-Path $taskWorkspace 'clients\qt6\CMakeLists.txt' }
    Start-Process -FilePath 'D:\WorkStation\QT6\QT\Tools\QtCreator\bin\qtcreator.exe' -ArgumentList @('"' + $taskProject + '"') -WindowStyle Normal
}
