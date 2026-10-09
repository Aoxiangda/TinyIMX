#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-control-abort-review-20261006';assert not d.exists()
control=b/'group-actor-snapshot-endpoint-control-20261006';orig=json.loads((control/'runtime-private/original-inspect.json').read_text());audit=json.loads((control/'audit-before.json').read_text())
names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19
cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));g=next(c for c in cs if c['Name']==orig['Name'])
def env(c):return dict(e.split('=',1) for e in c['Config']['Env'] if '=' in e)
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
x={'status':'READONLY_GROUP_CONTROL_ABORT_REVIEW','group':ident(g),'restored_original_image':g['Image']==orig['Image'],'restored_exact_Env_map':env(g)==env(orig),'env_differing_keys':[k for k in set(env(g))|set(env(orig)) if env(g).get(k)!=env(orig).get(k)],'HostConfig_differing_keys':[k for k in set(g['HostConfig'])|set(orig['HostConfig']) if g['HostConfig'].get(k)!=orig['HostConfig'].get(k)],'Mounts_equal_ordered':g['Mounts']==orig['Mounts'],'Mounts_equal_sorted':sorted(g['Mounts'],key=lambda m:m['Destination'])==sorted(orig['Mounts'],key=lambda m:m['Destination']),'Config_differing_keys':[k for k in ['User','WorkingDir','Cmd','Entrypoint','Healthcheck','StopSignal'] if g['Config'].get(k)!=orig['Config'].get(k)],'all19_healthy':all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs),'other18_preserved':all(c['Name']==orig['Name'] or ident(c)==audit['runtime_before'][c['Name']] for c in cs),'group_elf_sha256':subprocess.check_output(['docker','exec',g['Id'],'sha256sum','/opt/tinyimx/bin/group_service_demo'],text=True).split()[0],'previous_failure':json.loads((control/'failed.json').read_text()) if (control/'failed.json').exists() else None,'source_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()}
d.mkdir(mode=0o700);(d/'audit-before.json').write_text(json.dumps({'operation':'Readonlypost-abortcomparison; no restart/repair orsecret export','utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})+'\n');(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
