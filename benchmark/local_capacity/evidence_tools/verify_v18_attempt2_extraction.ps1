param()
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskDirectory=Join-Path $taskRoot 'evidence/post-restart-v18-attempt2-20261005'
$taskArchive=Join-Path $taskRoot 'evidence/post-restart-evidence-v18-attempt2.tar.gz'
$taskAudit=Join-Path $taskRoot 'audit/post-restart-v18-attempt2-extraction-verification.json'
if(Test-Path -LiteralPath $taskAudit){throw 'Preserve existing verification'}
$taskHash=(Get-FileHash -LiteralPath $taskArchive -Algorithm SHA256).Hash.ToLowerInvariant()
if($taskHash -ne 'ece928ff8e5251c4fd6cdb8b3d5533bd9c1e912e0dca8a708682b1f0ed51f4f8'){throw 'Archive mismatch'}
$taskManifest=Get-Content -Raw -LiteralPath (Join-Path $taskDirectory 'post-restart-export-audit-before-v18-attempt2.json') | ConvertFrom-Json
$taskPrefix=[IO.Path]::GetFullPath($taskDirectory)+[IO.Path]::DirectorySeparatorChar
$taskPaths=@{}
foreach($taskFile in $taskManifest.files){
 $taskPath=[IO.Path]::GetFullPath((Join-Path $taskDirectory $taskFile.path))
 if(-not $taskPath.StartsWith($taskPrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe manifest path'}
 if($taskPaths.ContainsKey($taskPath)){throw 'Duplicate file path'}
 $taskPaths[$taskPath]=$true
 if((Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskFile.sha256){throw 'File hash mismatch'}
}
if($taskPaths.Count -ne 100){throw 'Unexpected file count'}
$taskObject=[ordered]@{timestamp_utc=[DateTime]::UtcNow.ToString('o');status='VERIFIED_EXISTING_EXTRACTION_100_FILE_HASHES';archive_sha256=$taskHash;files_verified=$taskPaths.Count;prior_failure='Legacy extractor completed safe extraction but rejected the explicit -attempt2 manifest name; preserve its audit and directory';operation='Read-only archive and each extracted file SHA validation; fresh verification record only';no_reextract_or_delete=$true}
[IO.File]::WriteAllText($taskAudit,($taskObject|ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
'MANIFEST_HASHES_VERIFIED='+$taskPaths.Count
