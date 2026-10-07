param([string]$EvidenceName = 'qt6-ui-20261007-attempt3', [switch]$Editor)
$ErrorActionPreference = 'Stop'
if ($EvidenceName -notmatch '^qt6-ui-[a-zA-Z0-9-]+$') { throw 'Invalid evidence directory name' }
$taskWorkspace = Split-Path $PSScriptRoot -Parent
$taskExe = Join-Path $taskWorkspace ('evidence\' + $EvidenceName + '\build\tinyimx_desktop.exe')
if (!(Test-Path -LiteralPath $taskExe)) { throw 'Build the client first with Build-TinyIMX-Desktop.ps1' }
# Visible windows are intentional: the user asked to design and review this native client.
Start-Process -FilePath $taskExe -WorkingDirectory (Split-Path $taskExe) -WindowStyle Normal
if ($Editor) {
    $taskProject = Join-Path $taskWorkspace 'source\clients\qt6\CMakeLists.txt'
    if (!(Test-Path -LiteralPath $taskProject)) { $taskProject = Join-Path $taskWorkspace 'clients\qt6\CMakeLists.txt' }
    Start-Process -FilePath 'D:\WorkStation\QT6\QT\Tools\QtCreator\bin\qtcreator.exe' -ArgumentList @('"' + $taskProject + '"') -WindowStyle Normal
}
