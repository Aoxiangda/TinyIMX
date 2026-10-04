#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,shutil,os
r=pathlib.Path.cwd();d=r/'.local/codex/auth-phase-diagnostics-image-20261005';assert not d.exists()
s=json.loads((r/'.local/codex/auth-phase-diagnostics-build-20261005/summary.json').read_text());assert s['status']=='AUTH_BUILD_REGRESSIONS_PASS' and sum(x['pass'] for x in s['checks'].values())==36
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head==s['compiled_head']=='374f2807e9e48312faa3653c9dde9b919d0e7300'
p=r/'build/linux-release/user_service_demo';assert hashlib.sha256(p.read_bytes()).hexdigest()==s['user_binary_sha256']
base='tinyimx/runtime:m21-final';old='sha256:38dca459e0a88141c1385503b28cbd6e29a1eed18d253f808dc1dfa89e0a0316';assert json.loads(subprocess.check_output(['docker','image','inspect',base],text=True))[0]['Id']==old
image='tinyimx/runtime:codex-auth-phase-diagnostics-v1';assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
probes=['tinyimx-codex-auth-phase-ldd-20261005','tinyimx-codex-auth-phase-exec-20261005'];names=subprocess.check_output(['docker','ps','-a','--format','{{.Names}}'],text=True).splitlines();assert not set(probes)&set(names)
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Seal User-only defaultOFF auth diagnostic image after36 regression checks','compiled_head':head,'new_binary_sha256':s['user_binary_sha256'],'original_binary_sha256':s['original_user_binary_sha256'],'base_image':old,'writes':['Freshcontext singlebinary0755',image,'Named stoppedUID1000 loader/missingconfig probes retained'],'network':'Buildnetworknone/pullfalse andprobenetworknone','deployment':False,'rollback':'Original38dca image/binary retained; no prune/removal/SQL/config changes'},indent=2)+'\n')
context=d/'image-context';context.mkdir();shutil.copy2(p,context/'user_service_demo');os.chmod(context/'user_service_demo',0o755)
(d/'Dockerfile').write_text('FROM '+base+'\nCOPY --chmod=0755 user_service_demo /opt/tinyimx/bin/user_service_demo\nLABEL org.opencontainers.image.revision="'+head+'"\nLABEL tinyimx.binary.sha256="'+s['user_binary_sha256']+'"\n')
with (d/'build.log').open('w') as f:subprocess.run(['docker','build','--pull=false','--network=none','-f',str(d/'Dockerfile'),'-t',image,str(context)],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
common=['docker','run','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges','--label','tinyimx.codex.task=auth-phase-diagnostics-20261005']
with (d/'uid1000-ldd.log').open('w') as f:p=subprocess.run(common+['--name',probes[0],'--entrypoint','/bin/sh',image,'-ec','test -x "$1"; ldd -r "$1"','owned-user-loader-check','/opt/tinyimx/bin/user_service_demo'],stdout=f,stderr=subprocess.STDOUT,timeout=20)
assert p.returncode==0 and not any(x in (d/'uid1000-ldd.log').read_text().lower() for x in ['not found','undefined symbol'])
with (d/'uid1000-exec.log').open('w') as f:p=subprocess.run(common+['--name',probes[1],'--entrypoint','/opt/tinyimx/bin/user_service_demo',image,'/tmp/codex-owned-nonexistent-auth-config-20261005.json','127.0.0.1:50052'],stdout=f,stderr=subprocess.STDOUT,timeout=15)
assert p.returncode==1
c=json.loads(subprocess.check_output(['docker','image','inspect',image],text=True))[0];assert c['Config']['Labels']['org.opencontainers.image.revision']==head and c['Config']['Labels']['tinyimx.binary.sha256']==s['user_binary_sha256']
x={'status':'SEALED_AUTH_IMAGE_PASS','image_tag':image,'image_id':c['Id'],'compiled_head':head,'binary_sha256':s['user_binary_sha256'],'original_user_image':old,'original_user_binary_sha256':s['original_user_binary_sha256'],'uid1000_loader':'PASS','uid1000_executable':'PASS_expected_missingconfig1','stopped_probes':probes,'deployment':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
