#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-control-dns-representation-20261006';assert not d.exists()
old=json.loads((b/'group-actor-snapshot-endpoint-control-20261006/runtime-private/original-inspect.json').read_text())
now=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-group-service-1'],text=True))[0]
keys=['Dns','DnsOptions','DnsSearch'];rows={k:{'before':old['HostConfig'].get(k),'after':now['HostConfig'].get(k)} for k in keys}
assert all(v['before'] in [None,[]] and v['after'] in [None,[]] for v in rows.values())
a=dict(old['HostConfig']);z=dict(now['HostConfig'])
for k in keys:
 if a.get(k) is None:a[k]=[]
 if z.get(k) is None:z[k]=[]
assert a==z
d.mkdir(mode=0o700);x={'status':'DNS_ONLY_NULL_EMPTY_ARRAY_REPRESENTATION_CONFIRMED','fields':rows,'restored_original_image':old['Image']==now['Image'],'HostConfig_after_normalizing_only_null_dns_arrays_equal':True,'original_group_id':old['Id'],'current_group_id':now['Id'],'no_restart_or_resource_changes':True,'source_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()}
(d/'audit-before.json').write_text(json.dumps({'operation':'Readonly exactDNS schema representation andallotherHostConfig equality','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'writes':'Own evidence only'})+'\n');(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
