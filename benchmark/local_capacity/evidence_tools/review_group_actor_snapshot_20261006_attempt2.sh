#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime,re,shlex
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-actor-snapshot-preflight-20261006-attempt2';assert not d.exists()
def run(a,t=35):return subprocess.check_output(a,text=True,timeout=t)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def sql(q):
 assert q.startswith('SELECT ') and ';' not in q
 return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','group-readonly-review',q])
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
group=[c for c in cs if c['Name']=='/tinyimx-m21-group-service-1'];assert len(group)==1;g=group[0]
assert any(x=='/opt/tinyimx/bin/group_service_demo' for x in g['Config']['Cmd'])
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
build=r/'build/linux-release';mods=['services/repository/GroupRepository.cpp','services/group/repository/GroupRepositoryAdapter.cpp','services/group/repository/GroupMembershipRepositoryAdapter.cpp'];plans={}
for n in mods:
 hits=[]
 for dep in build.glob('CMakeFiles/*/DependInfo.cmake'):
  tokens=re.findall(r'"([^"]+)"',dep.read_text())
  for i,x in enumerate(tokens):
   if x==str(r/n):
    obj=tokens[i+1];lf=dep.parent/'link.txt';ar=shlex.split(lf.read_text().splitlines()[0]);libs=[v for v in ar if v.startswith('libtinyimx_') and v.endswith('.a')];assert len(libs)==1
    hits.append({'target':dep.parent.name.removesuffix('.dir'),'archive':libs[0],'member':pathlib.Path(obj).name,'object':obj,'flags_path':str(dep.parent/'flags.make'),'flags_sha256':sha(dep.parent/'flags.make'),'archive_sha256':sha(build/libs[0])})
 assert len(hits)==1,(n,len(hits));plans[n]=hits[0]
links=build/'CMakeFiles/group_service_demo.dir/link.txt';assert links.exists()
groups=[list(map(int,line.split('\t'))) for line in sql('SELECT group_id,owner_user_id,status,member_version FROM im_groups WHERE owner_user_id BETWEEN 500001 AND 520000 ORDER BY group_id LIMIT 40').splitlines()]
assert groups
cases=[]
for gid,owner,status,mv in groups:
 members=[list(map(int,line.split('\t'))) for line in sql('SELECT user_id,role,status,membership_epoch,CASE WHEN muted_until>UTC_TIMESTAMP(3) THEN 1 ELSE 0 END FROM im_group_members WHERE group_id='+str(gid)+' AND user_id BETWEEN 500001 AND 520000 ORDER BY user_id LIMIT 12').splitlines()]
 for uid,role,ms,epoch,muted in members:cases.append({'group_id':gid,'user_id':uid,'group_status':status,'role':role,'member_status':ms,'epoch':epoch,'muted':muted,'exists':True})
 for uid in [519870,519871,519872,519950]:
  if not any(v[0]==uid for v in members):
   found=sql('SELECT COUNT(*) FROM im_group_members WHERE group_id='+str(gid)+' AND user_id='+str(uid)).strip()
   if found=='0':cases.append({'group_id':gid,'user_id':uid,'group_status':status,'exists':False});break
missing=int(sql('SELECT COALESCE(MAX(group_id),0)+1000 FROM im_groups').strip())
cases.append({'group_id':missing,'user_id':519870,'group_status':0,'exists':False})
assert any(v['group_status']==1 and v['exists'] for v in cases)
d.mkdir(mode=0o700)
x={'status':'GROUP_ACTOR_SNAPSHOT_READONLY_PREFLIGHT_PASS','head':run(['git','rev-parse','HEAD']).strip(),'plans':plans,'source_before_sha256':{n:sha(r/n) for n in [*mods,'services/repository/GroupRepository.h']},'group_image':g['Image'],'group_elf_sha256':run(['docker','exec',g['Id'],'sha256sum','/opt/tinyimx/bin/group_service_demo']).split()[0],'runtime':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'cases':cases,'mysql_native_ip':next(v['IPAddress'] for c in cs if c['Name']=='/tinyimx-m21-mysql-1' for v in c['NetworkSettings']['Networks'].values() if v.get('IPAddress')),'link_path':str(links),'link_sha256':sha(links)}
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly group schema/cached compile metadata and existing synthetic-owned group/member cases','no_mutation':True,'queries':'Only boundedSELECT existing owned500001..520000 group/member metadata; no data/config/env export or changes'})+'\n')
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n')
print(json.dumps({'status':x['status'],'plans':plans,'cases':len(cases),'group_statuses':sorted(set(c['group_status'] for c in cases)),'member_statuses':sorted(set(c.get('member_status',0) for c in cases)),'member_roles':sorted(set(c.get('role',0) for c in cases)),'active_muted_cases':sum(c.get('muted',0) for c in cases),'group_image':x['group_image'],'group_elf_sha256':x['group_elf_sha256']},indent=2))
PY
