#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,http.client,re
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'real-ollama-timeout-analysis-20261006';assert not d.exists();d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly actualOllama timeout diagnosis, processnumeric/status +servicejournal/toolversions/memory/storage; no newinference orsettings/restarts/cleanup','reason':'Actual qwen2.5:7b cold toolloop timedout atoriginal30000ms; /api/ps empty atcasefinish,8MCPtools passed'})
def call(a,label):
 p=subprocess.run(a,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30);(private/(label+'.log')).write_text(p.stdout);return {'exit':p.returncode,'stdout':p.stdout}
rows=[]
for path in pathlib.Path('/proc').iterdir():
 if not path.name.isdigit():continue
 try:
  comm=path.joinpath('comm').read_text().strip();args=path.joinpath('cmdline').read_bytes().split(b'\0')
  if 'ollama' not in comm.lower() and not any(b'ollama' in a.lower() and b'/' in a for a in args[:2]):continue
  stat=path.joinpath('stat').read_text();status=path.joinpath('status').read_text()
  rows.append({'pid':int(path.name),'comm':comm,'stat':stat,'status':status,'exe':str(path.joinpath('exe').readlink())})
 except (FileNotFoundError,PermissionError,ProcessLookupError):pass
save('ollama-process-numeric.json',rows)
units=call(['systemctl','show','ollama','--property=ActiveState,SubState,MainPID,NRestarts,Result,ExecMainStatus,ExecMainStartTimestamp,FragmentPath'],'systemd-show')
journal=call(['journalctl','-u','ollama','--since','10 minutes ago','--no-pager','-o','short-iso'],'systemd-journal')
sudo=call(['sudo','-n','journalctl','-u','ollama','--since','10 minutes ago','--no-pager','-o','short-iso'],'sudo-readonly-journal')
version=call(['ollama','--version'],'ollama-version')
safe=[]
text=journal['stdout']+'\n'+(sudo['stdout'] if sudo['exit']==0 else '')
for line in text.splitlines():
 if any(k in line.lower() for k in ['load','memory','runner','timeout','error','offload','ctx','cuda','vulkan','avx','panic']):safe.append(line)
save('ollama-loading-log-analysis.json',{'journal_exit':journal['exit'],'sudo_readonly_exit':sudo['exit'],'service':units,'version':version,'loading_lines':safe[-100:]})
for name,path in [('meminfo','/proc/meminfo'),('cpu_pressure','/proc/pressure/cpu'),('memory_pressure','/proc/pressure/memory'),('io_pressure','/proc/pressure/io')]: (d/(name+'.txt')).write_text(pathlib.Path(path).read_text())
for endpoint in ['/api/ps','/api/tags']:
 c=http.client.HTTPConnection('127.0.0.1',11434,timeout=3)
 try:c.request('GET',endpoint);p=c.getresponse();obj=json.loads(p.read(1024*1024));save(endpoint.replace('/','-')+'.json',{'http_status':p.status,'json':obj})
 finally:c.close()
save('summary.json',{'status':'READONLY_OLLAMA_TIMEOUT_ANALYSIS_CAPTURED','processes':len(rows),'sudo_readonly_journal_available':sudo['exit']==0,'model_inference_repeated':False,'settings_ororiginalservicechanges':False})
print(json.dumps({'service':units,'version':version,'sudo_readonly_exit':sudo['exit'],'loading_lines':safe[-45:]},indent=2))
PY
