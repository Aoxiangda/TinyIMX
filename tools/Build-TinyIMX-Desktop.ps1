param(
    [string]$QtRoot = 'D:\WorkStation\QT6\QT',
    [string]$EvidenceName = ('qt6-ui-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [switch]$Capture
)
$ErrorActionPreference = 'Stop'
$taskWorkspace = Split-Path $PSScriptRoot -Parent
$taskSource = Join-Path $taskWorkspace 'source\clients\qt6'
if (!(Test-Path -LiteralPath $taskSource)) { $taskSource = Join-Path $taskWorkspace 'clients\qt6' }
if (!(Test-Path -LiteralPath (Join-Path $taskSource 'CMakeLists.txt'))) { throw 'Client source directory not found' }
$taskKit = Join-Path $QtRoot '6.10.2\mingw_64'
$taskCompiler = Join-Path $QtRoot 'Tools\mingw1310_64\bin\g++.exe'
$taskCmake = Join-Path $QtRoot 'Tools\CMake_64\bin\cmake.exe'
$taskNinja = Join-Path $QtRoot 'Tools\Ninja\ninja.exe'
if ($EvidenceName -notmatch '^qt6-ui-[a-zA-Z0-9-]+$') { throw 'Use a simple new evidence directory name' }
$taskEvidence = Join-Path $taskWorkspace ('evidence\' + $EvidenceName)
if (Test-Path -LiteralPath $taskEvidence) { throw 'Evidence directory already exists; preserve it and choose a new name' }
foreach ($taskPath in @($taskCompiler, $taskCmake, $taskNinja, (Join-Path $taskKit 'bin\qmake.exe'))) {
    if (!(Test-Path -LiteralPath $taskPath)) { throw "Installed tool missing: $taskPath" }
}
New-Item -ItemType Directory -Path $taskEvidence | Out-Null
Copy-Item -LiteralPath $taskSource -Destination (Join-Path $taskEvidence 'source-snapshot') -Recurse
$taskBuild = Join-Path $taskEvidence 'build'
$taskOriginalPath = $env:PATH
$taskSourceHashes = [ordered]@{}
Get-ChildItem -LiteralPath $taskSource -Recurse -File | ForEach-Object {
    $taskSourceHashes[$_.FullName.Substring($taskSource.Length+1)] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
}
[ordered]@{ utc=[DateTimeOffset]::UtcNow.ToString('o'); operation='Build standalone client with existing Qt6; real network integration compiled, local state/protocol checks and optional demo own-window captures'; source=$taskSource; build=$taskBuild; tools=@($taskKit,$taskCompiler,$taskCmake,$taskNinja); source_sha256=$taskSourceHashes; pressure=$false; network=$false; deletion=$false; global_environment_changes=$false } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskEvidence 'audit-before.json') -Encoding utf8
try {
    $env:PATH = (Join-Path $taskKit 'bin') + ';' + (Split-Path $taskCompiler) + ';' + $taskOriginalPath
    & $taskCmake -S $taskSource -B $taskBuild -G Ninja "-DCMAKE_PREFIX_PATH=$taskKit" "-DCMAKE_CXX_COMPILER=$taskCompiler" "-DCMAKE_MAKE_PROGRAM=$taskNinja" -DCMAKE_BUILD_TYPE=Release 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'configure.log')
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed; evidence retained' }
    & $taskCmake --build $taskBuild --parallel 2 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'build.log')
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed; evidence retained' }
    & (Join-Path (Split-Path $taskCmake) 'ctest.exe') --test-dir $taskBuild --output-on-failure 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'state-tests.log')
    if ($LASTEXITCODE -ne 0) { throw 'State contract failed; evidence retained' }
    $taskExe = Join-Path $taskBuild 'tinyimx_desktop.exe'
    & (Join-Path $taskKit 'bin\windeployqt.exe') --verbose 0 --release --no-translations --qmldir (Join-Path $taskSource 'qml') $taskExe 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'deploy.log')
    if ($LASTEXITCODE -ne 0) { throw 'Qt runtime packaging failed; evidence retained' }
    if ($Capture) {
        $taskCapturePath = Join-Path $taskEvidence 'captures'
        $taskCapture = Start-Process -FilePath $taskExe -ArgumentList @('--capture', ('"' + $taskCapturePath + '"')) -WorkingDirectory $taskBuild -WindowStyle Hidden -Wait -PassThru
        if ($taskCapture.ExitCode -ne 0) { throw 'Native page capture failed; evidence retained' }
        $taskCaptureSummary = Get-Content -LiteralPath (Join-Path $taskCapturePath 'summary.json') -Raw | ConvertFrom-Json
        if ($taskCaptureSummary.qt_warnings -ne 0 -or $taskCaptureSummary.status -ne 'QT6_NATIVE_PAGES_CAPTURED') { throw 'Native Qt runtime warnings found; see capture logs' }
        Write-Output ('Native captures: ' + $taskCaptureSummary.pages + '; Qt warnings: ' + $taskCaptureSummary.qt_warnings)
    }
    [ordered]@{status='QT6_CLIENT_BUILD_STATE_TESTS_AND_DEPLOY_PASS'; exe=$taskExe; exe_sha256=(Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash; qt='6.10.2 MinGW 64'; captured=[bool]$Capture; data='real server client by default; --demo and --capture use isolated demo; real E2E receipt saved separately'; pressure='paused'; source_sha256=$taskSourceHashes} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskEvidence 'summary.json') -Encoding utf8
} catch {
    [ordered]@{status='FAIL'; error=$_.Exception.Message; evidence_preserved=$true; pressure=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskEvidence 'failed.json') -Encoding utf8
    throw
} finally { $env:PATH = $taskOriginalPath }
