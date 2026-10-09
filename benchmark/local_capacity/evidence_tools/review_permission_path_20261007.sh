#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,subprocess,json,hashlib,urllib.request,urllib.parse,datetime
r=pathlib.Path.cwd();d=r/'.local/codex/permission-path-review-20261007';assert not d.exists()
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19
 cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=runtime();d.mkdir(mode=0o700)
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();save=lambda n,x:(d/n).write_text(json.dumps(x,indent=2)+chr(10))
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Only bounded SQL metadata EXPLAIN and Prometheus GET plus selected boolean config; no change/deployment or new load','runtime_before':before})
selected={}
for name in ['gateway-a','gateway-b','social','message']:
 p=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')/(name+'.json')
 if not p.is_file():continue
 o=json.loads(p.read_text()).get('observability',{});selected[name]={k:v for k,v in o.items() if isinstance(v,bool)}
 for k in ['metrics','tracing','traces']:
  if isinstance(o.get(k),dict):selected[name][k+'_enabled']=o[k].get('enabled')
save('observability-selected-booleans.json',selected)
def sql(q):return subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','codex-readonly-permission',q],text=True,timeout=20)
q='EXPLAIN FORMAT=JSON SELECT user_id,peer_user_id,relation_status FROM im_user_relations WHERE (user_id=10001 AND peer_user_id=10002) OR (user_id=10002 AND peer_user_id=10001)'
(d/'permission-explain.sql').write_text(q+chr(10));(d/'permission-explain.json').write_text(sql(q));(d/'permission-indexes.tsv').write_text(sql('SHOW INDEX FROM im_user_relations'))
metrics={};errors=[]
def get(api,params=None):
 u='http://127.0.0.1:19090/api/v1/'+api
 if params:u+='?'+urllib.parse.urlencode(params)
 with urllib.request.urlopen(u,timeout=10) as f:return json.load(f)
try:
 names=get('label/__name__/values');save('metric-names.json',names);rpc=[n for n in names.get('data',[]) if 'rpc' in n];metrics['rpc_names']=rpc
 for n in rpc:
  if n.endswith(('_sum','_count')):
   v=get('query',{'query':n+'{rpc_method="CheckPrivateChatPermission"}'});save(n+'.json',v);metrics[n]=v.get('data',{}).get('result',[])
except Exception as e:errors.append({'type':type(e).__name__,'message':str(e)})
save('summary.json',{'status':'PERMISSION_PATH_READONLY_REVIEW_COMPLETE','head':head,'observability':selected,'metrics':metrics,'metric_read_errors':errors,'runtime_preserved':runtime()==before,'limits':'EXPLAIN is idle metadata, not actual call duration; cumulative metrics cannot prove same-call queue or transport. All prior slow samples remain biased. No permission cache/index/poller change.'});assert runtime()==before
print((d/'summary.json').read_text())
PY
