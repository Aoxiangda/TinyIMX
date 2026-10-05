#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,tarfile
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'online-path-source-review-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
paths=['gateway/GatewayServer.cpp','gateway/GatewayServer.h','gateway/SessionManager.cpp','gateway/SessionManager.h','common/cache/RedisConnection.cpp','common/cache/RedisConnection.h','common/cache/RedisConnectionPool.cpp','common/cache/RedisConnectionPool.h','common/db/MySqlConnection.cpp','common/db/MySqlConnection.h','common/db/MySqlConnectionPool.cpp','common/db/MySqlConnectionPool.h','services/social/application/FriendApplicationService.cpp','services/social/repository/FriendRepositoryAdapter.cpp','services/outbox/OutboxRelay.cpp','services/outbox/OutboxRepository.cpp']
gwref='33fc9bbbf7b8a8e828ce70d1ae0035d8e8c71ce8';msgref='ddc7e8ec9a4c29a14f46b2f3c7970843a1850432'
refs={'running-gateway-source':gwref,'running-message-source':msgref,'current-source':head}
running=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-gateway-a-1','tinyimx-m21-gateway-b-1','tinyimx-m21-message-service-1'],text=True))
assert all(c['Image']==('sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898' if 'message-service' in c['Name'] else 'sha256:1d8d71faa91a5a1e0a261c2a89efa1027a93515ae2b94b2c2d06f48010a7486e') for c in running)
entries=[]
for label,ref in refs.items():
 selected=paths if label=='current-source' else [p for p in paths if p.startswith('gateway/') or p.startswith('common/cache/')] if label=='running-gateway-source' else [p for p in paths if p.startswith('common/db/')]
 for path in selected:
  data=subprocess.check_output(['git','show',ref+':'+path]);assert len(data)<1024*1024 and b'\x00' not in data
  entries.append((label,path,data))
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Snapshot only exact selected tracked C++ sources from current and actual Gateway/Message compiled Git refs for read-only cost review','paths':paths,'refs':refs,'writes':'Own fresh bounded source archive/files, no repository/service/config/load changes','limits':'Social/outbox current source is review context, no assertion that unlabeled binaries equalHEAD. CurrentGateway includes previously rejected fairness code, compare running33fc before proposals. No credentials/config/binaries/production records.','rollback':'Retain all snapshots without overwrite/deletion'},indent=2)+'\n')
files=[]
for label,path,data in entries:
 p=d/label/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);files.append({'path':str(p.relative_to(d)),'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)})
x={'status':'ONLINE_PATH_SOURCE_SNAPSHOT_COMPLETE','head':head,'refs':refs,'files':files,'runtime_before':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in running},'runtime_changes':False,'load_started':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n')
archive=d/'online-path-source-review.tar.gz'
with tarfile.open(archive,'w:gz') as t:
 t.add(d/'audit-before.json',arcname='audit-before.json');t.add(d/'summary.json',arcname='summary.json')
 for item in files:t.add(d/item['path'],arcname=item['path'])
print('FILES='+str(len(files)));print('SHA256='+hashlib.sha256(archive.read_bytes()).hexdigest());print('ARCHIVE='+str(archive))
PY
