#!/usr/bin/env bash
set -euo pipefail
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,socket
def sql(q):return subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-actor-selection-readonly',q],text=True,timeout=20).splitlines()
users=[]
for row in sql('SELECT user_id,username,status FROM im_users WHERE user_id BETWEEN 519800 AND 519950 ORDER BY user_id'):
 uid,name,status=row.split('\t');uid=int(uid)
 if status=='1' and name==f'm21b500000_{uid-500000:06d}' and uid not in [519890,519892]:users.append(uid)
occupied=set()
for query in ['SELECT user_id,peer_user_id FROM im_user_relations WHERE user_id BETWEEN 519800 AND 519950 AND peer_user_id BETWEEN 519800 AND 519950','SELECT from_user_id,to_user_id FROM im_friend_requests WHERE from_user_id BETWEEN 519800 AND 519950 AND to_user_id BETWEEN 519800 AND 519950']:
 for row in sql(query):a,b=map(int,row.split('\t'));occupied.add(tuple(sorted((a,b))))
cfg=json.loads(pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/gateway-a.json').read_text())['redis'];cs=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-redis-1'],text=True))[0];ips=[v['IPAddress'] for v in cs['NetworkSettings']['Networks'].values() if v.get('IPAddress')];assert len(ips)==1
s=socket.create_connection((ips[0],cfg['port']),3);s.settimeout(3);f=s.makefile('rb')
def call(*args):
 wire=b'*'+str(len(args)).encode()+b'\r\n'
 for value in args:
  value=str(value).encode();wire+=b'$'+str(len(value)).encode()+b'\r\n'+value+b'\r\n'
 s.sendall(wire);line=f.readline(1024);assert line.endswith(b'\r\n')
 if line[:1]==b'+':return line[1:-2]
 if line[:1]==b':':return int(line[1:-2])
 if line[:1]==b'$':
  n=int(line[1:-2]);assert -1<=n<=1048576
  if n==-1:return None
  value=f.read(n);assert len(value)==n and f.read(2)==b'\r\n';return value
 raise RuntimeError('OwnReadonlyRedisReplyRejected')
try:
 if cfg.get('password'):assert call('AUTH',cfg['password'])==b'OK'
 if cfg.get('db',0):assert call('SELECT',cfg['db'])==b'OK'
 available=[uid for uid in users if call('EXISTS','tinyimx:online:'+str(uid))==0]
 chosen=[]
 for a in available:
  if a in chosen:continue
  for b in available:
   if b<=a or b in chosen or abs(a-b)==1 or (a,b) in occupied:continue
   chosen.extend([a,b]);break
  if len(chosen)==4:break
 assert len(chosen)==4
 print(json.dumps({'status':'READONLY_FRESH_ACTOR_PAIRS_VERIFIED','users':chosen,'pairs':[chosen[:2],chosen[2:]],'verified_usernames_status':True,'pair_relations_and_requests_empty_both_directions':True,'online_keys_absent':True,'writes':False,'password_only_RAM':True}))
finally:f.close();s.close()
PY
