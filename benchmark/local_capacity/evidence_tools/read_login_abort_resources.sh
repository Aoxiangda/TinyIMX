#!/usr/bin/env bash
set -euo pipefail
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,re,subprocess
r=pathlib.Path.cwd();p=r/'.local/codex/capacity-io150base/guest-resources.log';text=p.read_text();blocks=re.split(r'(?m)^(\d{4}-\d\d-\d\dT[^\n]+)\n',text)
for i in range(1,len(blocks),2):
 t=blocks[i+1];stats=[]
 for name,cpu,mem,pids in re.findall(r'(tinyimx-[a-zA-Z0-9_-]+) CPU=([\d.]+)% MEM=([^\n]+?) PIDS=(\d+)',t):stats.append({'container':name,'cpu_pct_one_core100':float(cpu),'memory':mem,'pids':int(pids)})
 psi=[line for line in t.splitlines() if line.startswith(('some ','full '))]
 print(json.dumps({'utc':blocks[i],'mem_available_kib':int(re.search(r'MemAvailable:\s+(\d+)',t).group(1)),'psi_rows_memory_thenCPU':psi,'stats':stats}))
print('GUEST_CPU_INFO='+subprocess.check_output(['getconf','_NPROCESSORS_ONLN'],text=True).strip())
PY
