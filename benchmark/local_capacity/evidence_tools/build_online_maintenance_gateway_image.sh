#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,shutil,os
r=pathlib.Path.cwd();d=r/'.local/codex/online-maintenance-gateway-image-20261005';assert not d.exists();d.mkdir()
summary=json.loads((r/'.local/codex/online-maintenance-gateway-candidate-build-20261005/summary.json').read_text());assert summary['status']=='ONLINE_MAINTENANCE_GATEWAY_CANDIDATE_BUILD_COMPLETE'
assert json.loads((r/'.local/codex/online-maintenance-lifecycle-test-20261005/summary.json').read_text())['total_checks']==224
assert summary['candidate_sha256']=='c2894a21cf308ad35ef665c603d17b1a2577c9216aa9e74856772befa99d640f'
buildhead=summary['head'];head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert buildhead=='38f41a9663603d959331340926e26ca6c29d08ac'
changed=set(subprocess.check_output(['git','diff','--name-only',buildhead,head],text=True).splitlines());assert all(path.startswith('benchmark/local_capacity/evidence_tools/') or path.startswith('docs/performance/') for path in changed),'Product/worker source changed after sealed build'
p=r/summary['candidate_relative_path'];assert hashlib.sha256(p.read_bytes()).hexdigest()==summary['candidate_sha256']
base='tinyimx/runtime:codex-unread-atomic-counts-v1';assert json.loads(subprocess.check_output(['docker','image','inspect',base],text=True))[0]['Id']=='sha256:1d8d71faa91a5a1e0a261c2a89efa1027a93515ae2b94b2c2d06f48010a7486e'
image='tinyimx/runtime:codex-online-maintenance-gateway-v1';assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0,'Never replace existing candidate tag'
names=subprocess.check_output(['docker','ps','-a','--format','{{.Names}}'],text=True).splitlines();probes=['tinyimx-codex-online-maintenance-ldd-20261005','tinyimx-codex-online-maintenance-exec-20261005'];assert not set(names)&set(probes)
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Build sealed single-Gateway candidate over exact existing Releasebase, no network/pulls','binary_build_head':buildhead,'orchestration_head':head,'binary_sha256':summary['candidate_sha256'],'base_image':'sha256:1d8d71faa91a5a1e0a261c2a89efa1027a93515ae2b94b2c2d06f48010a7486e','writes':['Own image context singlebinary0755',image,'Two named stopped UID1000 probe containers preserved'],'source_changes_after_build':sorted(changed),'runtime_service_deployment':False,'validation':'UID1000 relocated dependencies and executable controlled missing-config exit1; no actual runtime configs or token','rollback':'Old running image andsourcebinary kept, retain candidate/probes; no dockerprune/removal'},indent=2)+'\n')
context=d/'image-context';context.mkdir();shutil.copy2(p,context/'gateway_demo');os.chmod(context/'gateway_demo',0o755)
(d/'Dockerfile').write_text('FROM '+base+'\nCOPY --chmod=0755 gateway_demo /opt/tinyimx/bin/gateway_demo\nLABEL org.opencontainers.image.revision="'+buildhead+'"\nLABEL tinyimx.orchestration.revision="'+head+'"\nLABEL tinyimx.binary.sha256="'+summary['candidate_sha256']+'"\n')
with (d/'build.log').open('w') as f:subprocess.run(['docker','build','--pull=false','--network=none','-f',str(d/'Dockerfile'),'-t',image,str(context)],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
common=['docker','run','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges','--label','tinyimx.codex.task=online-maintenance-gateway-20261005']
with (d/'uid1000-ldd.log').open('w') as f:q=subprocess.run(common+['--name',probes[0],'--entrypoint','/bin/sh',image,'-ec','test -x "$1"; ldd -r "$1"','owned-loader-check','/opt/tinyimx/bin/gateway_demo'],stdout=f,stderr=subprocess.STDOUT,timeout=20)
assert q.returncode==0
assert not any(x in (d/'uid1000-ldd.log').read_text().lower() for x in ['not found','undefined symbol'])
with (d/'uid1000-exec.log').open('w') as f:q=subprocess.run(common+['--name',probes[1],'--entrypoint','/opt/tinyimx/bin/gateway_demo',image,'/tmp/codex-owned-nonexistent-config-20261004.json'],stdout=f,stderr=subprocess.STDOUT,timeout=15)
assert q.returncode==1,'Application must reach controlled missingconfig exit, not loader failure'
c=json.loads(subprocess.check_output(['docker','image','inspect',image],text=True))[0];assert c['Config']['Labels']['org.opencontainers.image.revision']==buildhead
out={'status':'SEALED_IMAGE_PASS','image_tag':image,'image_id':c['Id'],'binary_build_head':buildhead,'orchestration_head':head,'binary_sha256':summary['candidate_sha256'],'uid1000_loader':'PASS','uid1000_executable':'PASS expected missing-config1','stopped_probe_containers':probes,'service_deployment':False}
(d/'summary.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
PY
