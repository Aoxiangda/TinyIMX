$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskDir=Join-Path $taskRoot 'evidence/vmware-host-mode-review-20261005'
if(Test-Path -LiteralPath $taskDir){throw 'Preserve existing review'}
$taskVms=@(Get-CimInstance Win32_Process -Filter "Name='vmware-vmx.exe'")
$taskInputs=foreach($taskVm in $taskVms){
 foreach($taskMatch in [regex]::Matches($taskVm.CommandLine,'"([^\"]+\.vmx)"|(\S+\.vmx)')){
  $taskVmx=if($taskMatch.Groups[1].Success){$taskMatch.Groups[1].Value}else{$taskMatch.Groups[2].Value}
  if(-not [IO.Path]::IsPathFullyQualified($taskVmx)){continue}
  $taskLog=Join-Path (Split-Path $taskVmx -Parent) 'vmware.log'
  if(-not(Test-Path -LiteralPath $taskLog -PathType Leaf)){continue}
  [ordered]@{pid=$taskVm.ProcessId;vmx_path=$taskVmx;log_path=$taskLog;vmware_executable=$taskVm.ExecutablePath}
 }
}
if(@($taskInputs).Count -ne 1){throw 'Require exactly one identified running VM'}
New-Item -ItemType Directory -Path $taskDir|Out-Null
$taskAudit=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');operation='Readonly VMware mode and host VBS review';reads=@($taskInputs);writes='Fresh own audit/numeric metadata only';excluded='No full command line, VMX or log exported';mutations='No host security/boot/VM settings changes, no reboot, cleanup or process termination';limits='Current environment observation, no paired native-mode comparison; never assign all guest latency to ULM'}
[IO.File]::WriteAllText((Join-Path $taskDir 'audit-before.json'),($taskAudit|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
$taskRows=foreach($taskInput in $taskInputs){
 $taskLogText=Get-Content -LiteralPath $taskInput.log_path -Raw
 $taskVmxText=Get-Content -LiteralPath $taskInput.vmx_path -Raw
 $taskModes=@([regex]::Matches($taskLogText,'Monitor Mode:\s*(\w+)')|ForEach-Object {$_.Groups[1].Value}|Select-Object -Unique)
 $taskSettings=[ordered]@{}
 foreach($taskKey in @('numvcpus','memsize','virtualHW.version','ethernet0.virtualDev','ethernet0.pciSlotNumber','ulm.disableMitigations')){
  $taskSetting=[regex]::Match($taskVmxText,'(?m)^'+[regex]::Escape($taskKey)+'\s*=\s*"([^"\r\n]*)"')
  if($taskSetting.Success){$taskSettings[$taskKey]=$taskSetting.Groups[1].Value}
 }
 [ordered]@{pid=$taskInput.pid;vmx_path=$taskInput.vmx_path;vmx_sha256=(Get-FileHash -LiteralPath $taskInput.vmx_path -Algorithm SHA256).Hash.ToLowerInvariant();monitor_modes=$taskModes;mentions_whp=($taskLogText -match 'Windows Hypervisor Platform|WHP|WinHv');vmware_version=(Get-Item -LiteralPath $taskInput.vmware_executable).VersionInfo.ProductVersion;allowlisted_settings=$taskSettings;log_last_write_utc=(Get-Item -LiteralPath $taskInput.log_path).LastWriteTimeUtc.ToString('o')}
}
$taskSystem=Get-CimInstance Win32_ComputerSystem|Select-Object HypervisorPresent,TotalPhysicalMemory
$taskGuard=Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard|Select-Object VirtualizationBasedSecurityStatus,SecurityServicesConfigured,SecurityServicesRunning
$taskOut=[ordered]@{status='VMWARE_HOST_MODE_READONLY_REVIEW_COMPLETED';utc=[DateTime]::UtcNow.ToString('o');vm=@($taskRows);host=$taskSystem;device_guard=$taskGuard;source='Actual current VMX and vmware.log plus CIM metadata';primary_reference='https://blogs.vmware.com/cloud-foundation/2020/05/28/vmware-workstation-now-supports-hyper-v-mode/';inference='ULM/WHP is an environment factor for measured high kernel and scheduling costs; no causal percentage established';host_vm_settings_changed=$false;capacity_proof=$false}
[IO.File]::WriteAllText((Join-Path $taskDir 'summary.json'),($taskOut|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
$taskOut|ConvertTo-Json -Depth 8
