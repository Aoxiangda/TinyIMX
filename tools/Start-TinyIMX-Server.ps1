param([switch]$StatusOnly)
$ErrorActionPreference = 'Stop'
$taskCommand = 'python3 /home/jackson7/projects/TinyIMX_publish/clients/qt6/server/start_existing_server.py'
if (!$StatusOnly) { $taskCommand += ' --start' }
# This guard only starts exact existing audited containers; it never runs compose up/pull/build.
& (Join-Path $PSScriptRoot 'Invoke-TinyIMX.ps1') -RemoteCommand $taskCommand
