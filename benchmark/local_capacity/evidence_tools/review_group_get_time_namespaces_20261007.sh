#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,os,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-get-time-namespace-review-20261007';assert not d.exists()
run=lambda a:subprocess.check_output(a,text=True,stderr=subprocess.STDOUT)
names=run(['docker','ps','--format','{{.Names}}']).splitlines();cs=json.loads(run(['docker','inspect',*names]));runtime={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs};assert runtime==json.loads((b/'group-confirm128-control-20261006/restore-summary.json').read_text())['runtime']
clocks={'guest_host':{'namespace':os.readlink('/proc/self/ns/time'),'offsets':pathlib.Path('/proc/self/timens_offsets').read_text(),'boot_id':pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip()}}
for role in ['gateway-a','gateway-b','message-service']:
 c=next(x for x in cs if x['Name']=='/tinyimx-m21-'+role+'-1');clocks[role]={'namespace':run(['docker','exec',c['Id'],'readlink','/proc/self/ns/time']).strip(),'offsets':run(['docker','exec',c['Id'],'cat','/proc/self/timens_offsets']),'boot_id':run(['docker','exec',c['Id'],'cat','/proc/sys/kernel/random/boot_id']).strip()}
for z in clocks.values():z['monotonic_offset_ns']=next(int(v.split()[1])*1000000000+int(v.split()[2]) for v in z['offsets'].splitlines() if v.split()[0]=='monotonic')
x={'status':'GROUP_GET_TIME_NAMESPACE_REVIEWED','head':run(['git','rev-parse','HEAD']).strip(),'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly time namespace/offset/boot fingerprints, verify initialcontrol failed before anydeployment/actors; write onlyonefresh reviewstage','runtime':runtime,'clocks':clocks,'namespace_ids_equal':len({z['namespace'] for z in clocks.values()})==1,'monotonic_offsets_equal':len({z['monotonic_offset_ns'] for z in clocks.values()})==1,'boot_ids_equal':len({z['boot_id'] for z in clocks.values()})==1,'initial_failure':'Assert equalnamespace inodes wastoo strict; inspect offsets before any crossprocess clock comparison','pressure':False}
d.mkdir(mode=0o700);(d/'summary.json').write_text(json.dumps(x,indent=2)+chr(10));print(json.dumps({k:v for k,v in x.items() if k!='runtime'},indent=2))
PY
