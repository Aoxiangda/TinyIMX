#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-get-boundary-pollers-git-backup-20261007';assert not d.exists()
run=lambda a:subprocess.check_output(a,text=True,stderr=subprocess.STDOUT,timeout=60)
head=run(['git','rev-parse','HEAD']).strip();assert head==json.loads((b/'message-rpc-pollers-outcome-source-20261007/summary.json').read_text())['head'];base='2664cb7bd67dce7c34b893a6eb41f85fc781bf35';assert run(['git','rev-parse',base]).strip()==base
stages=['group-get-boundary-source-20261007','group-get-boundary-build-repair-source-20261007','group-get-boundary-control-repair-source-20261007','message-rpc-pollers-source-20261007','message-rpc-pollers-build-repair-source-20261007','message-rpc-pollers-provenance-source-20261007','message-rpc-pollers-outcome-source-20261007'];expected=set().union(*(json.loads((b/n/'summary.json').read_text())['files'].keys() for n in stages));files=set(run(['git','diff','--name-only',base,'HEAD']).splitlines());assert files==expected and not run(['git','diff','--cached','--name-only']).strip();commits=run(['git','rev-list',base+'..HEAD']).splitlines();assert len(commits)==7
cs=json.loads(run(['docker','inspect',*run(['docker','ps','--format','{{.Names}}']).splitlines()]));runtime={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs};assert runtime==json.loads((b/'message-rpc-pollers-control-20261007/restore-summary.json').read_text())['runtime']
d.mkdir(mode=0o700);out=d/'group-get-boundary-pollers-20261007.bundle';(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Only new incremental Git bundle, exact source receipt union. Preserve existing bundles. No runtime/config/SQL/reset/push/delete.','head':head,'prerequisite_base':base,'commits':commits,'files':sorted(files),'destination':str(out)},indent=2)+chr(10));subprocess.run(['git','bundle','create',str(out),'HEAD','^'+base],check=True);v=run(['git','bundle','verify',str(out)]);(d/'verify.log').write_text(v);assert run(['git','rev-parse','HEAD']).strip()==head
x={'status':'GROUP_GET_BOUNDARY_POLLERS_GIT_BUNDLE_VERIFIED','head':head,'prerequisite_base':base,'commits':commits,'files':len(files),'bundle':str(out),'bytes':out.stat().st_size,'sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'complete_repository':False,'restore_requires_existing_base_repository':True};(d/'summary.json').write_text(json.dumps(x,indent=2)+chr(10));print(json.dumps(x,indent=2))
PY
