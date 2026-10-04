#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,shutil,os
r=pathlib.Path.cwd();d=r/'.local/codex/message-sync-poller-retention-image-20261005';assert not d.exists();d.mkdir()
summary=json.loads((r/'.local/codex/message-sync-poller-retention-build-20261005/summary.json').read_text());assert summary['status']=='BUILD_UNIT_PASS';assert json.loads((r/'.local/codex/storage-wait-pool-recovery-build-20261005/summary.json').read_text())['status']=='RECOVERY_BUILD_AND_PROBE_PASS'
assert json.loads((r/'.local/codex/storage-wait-diagnostics-isolated-mysql-20261005/summary.json').read_text())['status']=='REAL_ISOLATED_MYSQL_PASS'
buildhead=summary['compiled_head'];head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head=='f7b5a08e47bfec08e5504001cc1d4b7edbface54'
changed=set(subprocess.check_output(['git','diff','--name-only',buildhead,head],text=True).splitlines());assert not changed,'No source changes after build'
p=r/'build/linux-release/message_service_demo';assert hashlib.sha256(p.read_bytes()).hexdigest()==summary['message_binary_sha256']
base='tinyimx/runtime:codex-private-single-lease-v1';assert json.loads(subprocess.check_output(['docker','image','inspect',base],text=True))[0]['Id']=='sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898'
image='tinyimx/runtime:codex-message-poller-retention-v1';assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0,'Never replace existing candidate tag'
names=subprocess.check_output(['docker','ps','-a','--format','{{.Names}}'],text=True).splitlines();probes=['tinyimx-codex-poller-retention-ldd-20261005','tinyimx-codex-poller-retention-exec-20261005'];assert not set(names)&set(probes)
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Seal Message-only MAX_POLLERS16 candidate after205 fresh checks including75 realgRPC checks; retained unchanged SQL/recovery evidence; no network/pulls','binary_build_head':buildhead,'orchestration_head':head,'binary_sha256':summary['message_binary_sha256'],'base_image':'sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898','writes':['Own image context singlebinary0755',image,'Two named stopped UID1000 probe containers preserved'],'source_changes_after_build':sorted(changed),'runtime_service_deployment':False,'validation':'UID1000 relocated dependencies and executable controlled missing-config exit1; no actual runtime configs or token','rollback':'Old running image andsourcebinary kept, retain candidate/probes; no dockerprune/removal'},indent=2)+'\n')
context=d/'image-context';context.mkdir();shutil.copy2(p,context/'message_service_demo');os.chmod(context/'message_service_demo',0o755)
(d/'Dockerfile').write_text('FROM '+base+'\nCOPY --chmod=0755 message_service_demo /opt/tinyimx/bin/message_service_demo\nLABEL org.opencontainers.image.revision="'+buildhead+'"\nLABEL tinyimx.orchestration.revision="'+head+'"\nLABEL tinyimx.binary.sha256="'+summary['message_binary_sha256']+'"\n')
with (d/'build.log').open('w') as f:subprocess.run(['docker','build','--pull=false','--network=none','-f',str(d/'Dockerfile'),'-t',image,str(context)],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
common=['docker','run','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges','--label','tinyimx.codex.task=message-poller-retention-20261005']
with (d/'uid1000-ldd.log').open('w') as f:q=subprocess.run(common+['--name',probes[0],'--entrypoint','/bin/sh',image,'-ec','test -x "$1"; ldd -r "$1"','owned-loader-check','/opt/tinyimx/bin/message_service_demo'],stdout=f,stderr=subprocess.STDOUT,timeout=20)
assert q.returncode==0
assert not any(x in (d/'uid1000-ldd.log').read_text().lower() for x in ['not found','undefined symbol'])
with (d/'uid1000-exec.log').open('w') as f:q=subprocess.run(common+['--name',probes[1],'--entrypoint','/opt/tinyimx/bin/message_service_demo',image,'/tmp/codex-owned-nonexistent-config-20261004.json'],stdout=f,stderr=subprocess.STDOUT,timeout=15)
assert q.returncode==1,'Application must reach controlled missingconfig exit, not loader failure'
c=json.loads(subprocess.check_output(['docker','image','inspect',image],text=True))[0];assert c['Config']['Labels']['org.opencontainers.image.revision']==buildhead
out={'status':'SEALED_IMAGE_PASS','image_tag':image,'image_id':c['Id'],'binary_build_head':buildhead,'orchestration_head':head,'binary_sha256':summary['message_binary_sha256'],'uid1000_loader':'PASS','uid1000_executable':'PASS expected missing-config1','stopped_probe_containers':probes,'service_deployment':False}
(d/'summary.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
PY
