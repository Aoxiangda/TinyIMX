#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,re,hashlib,math,statistics,os
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'private-batch-abba-analysis-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();source=json.loads((b/'private-batch-abba-outcome-source-20261005/summary.json').read_text());assert head==source['head']
names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19
cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));ref=json.loads((b/'private-batch-message-deployment-off-before-A2-20261005/runtime-after.json').read_text())
cfgroot=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
def runtime():
    current=json.loads(subprocess.check_output(['docker','inspect',*names],text=True))
    return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in current},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfgroot.glob('*.json')}}
before=runtime();assert before==ref
msg=next(c for c in cs if c['Name']=='/tinyimx-m21-message-service-1')
assert msg['Id']=='f642479a9be4c4a0f89a6c3bd7856129fb5b899ca9f4d2ee430a0c6c13ef87ed' and msg['Image']=='sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060'
assert 'TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE=0' in msg['Config']['Env']
runs=['sqlbatch150A1','sqlbatch150B1','sqlbatch150B2','sqlbatch150A2'];input_paths=[];lookup={};ledgers={};official=[]
for name in runs:
    raw=b/('capacity-'+name);control=b/('private-batch-endpoint-control-'+name);x=json.loads((control/'summary.json').read_text());assert x['private_exit']==2 and x['all19_runtime_configs_preserved'] and not x['full_feature_acceptance'];official.append(x)
    paths=sorted(raw.glob('worker-*/ledger.tsv'));assert len(paths)==1
    input_paths+=[control/'summary.json',*sorted(control.glob('before-*')),*sorted(control.glob('after-*')),raw/'summary.json',raw/'sql-reconciliation.tsv',paths[0],raw/'worker-0/final.json']
    sends={};acks={};delivery={}
    for line in paths[0].read_text().splitlines():
        f=line.split('\t');assert len(f)==7;kind=f[0];row={'uid':int(f[1]),'to':int(f[2]),'mid':int(f[3]),'seq':int(f[4]),'cid':f[5],'ns':int(f[6])};assert 700001<=row['uid']<=710000 and 700001<=row['to']<=710000
        if kind=='send':assert row['cid'] not in sends;sends[row['cid']]=row
        elif kind=='ack':assert row['mid']>0 and row['mid'] not in acks;acks[row['mid']]=row
        elif kind=='delivery':assert row['mid']>0 and row['mid'] not in delivery;delivery[row['mid']]=row
    assert len(sends)==len(acks)==len(delivery)==9000
    for mid,ack in acks.items():
        sent=sends[ack['cid']];got=delivery[mid];assert sent['uid']==ack['uid']==got['uid'] and sent['to']==ack['to']==got['to'] and sent['seq']==ack['seq'];assert mid not in lookup
        lookup[mid]={'run':name,'sender':sent['uid'],'recipient':sent['to'],'client_seq':sent['seq']}
    ledgers[name]=(sends,acks,delivery)
helpers=[r/'benchmark/local_capacity/evidence_tools'/n for n in ['review_private_batch_window_costs.sh','review_private_batch_observer_overlap.sh']];input_paths+=helpers
d.mkdir();private=d/'runtime-private';private.mkdir(mode=0o700)
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly completed ABBA CPU SQL observer and sameMID phase analysis, no load or runtime change','head':head,'inputs_sha256':{str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in input_paths},'runtime':before,'writes':'Fresh own private full interval capture; explicit numeric allowlist matching9000 positive MID percase; no full log/env/config export','limits':'All original failing gates preserved; SQL elapsed not CPU, whole cgroups include background, overlap not causality, sparse slow-biased phase intersections not population','rollback':'Keep all original and new stages, no deletion'},indent=2)+'\n')
costs=[];observers=[]
for helper,target in zip(helpers,[costs,observers]):
    data=subprocess.check_output(['bash',str(helper)],text=True,timeout=45);target.extend(json.loads(line) for line in data.splitlines());assert [x['run'] for x in target]==runs
(d/'window-costs.json').write_text(json.dumps(costs,indent=2)+'\n');(d/'observer-overlap.json').write_text(json.dumps(observers,indent=2)+'\n')
since=json.loads((b/'private-batch-endpoint-control-sqlbatch150A1/audit-before.json').read_text())['utc'];until=datetime.datetime.now(datetime.timezone.utc).isoformat();paths=[]
for c in cs:
    if c['Name'] not in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1','/tinyimx-m21-message-service-1']:continue
    p=private/(c['Name'].split('/')[-1]+'.log')
    with p.open('w') as f:subprocess.run(['docker','logs','--since',since,'--until',until,c['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=45)
    paths.append((c['Name'],p,{'sqlbatch150A2'} if c['Name']=='/tinyimx-m21-message-service-1' else set(runs)))
paths += [('old-message-off',b/'private-batch-message-deployment-on-before-B1-20261005/runtime-private/message-before.log',{'sqlbatch150A1'}),('old-message-on',b/'private-batch-message-deployment-off-before-A2-20261005/runtime-private/message-before.log',{'sqlbatch150B1','sqlbatch150B2'})]
gw=[];persist=[];gw_keys={'user_id','message_id','request_seq','session_epoch','dispatch_age_us','entry_budget_us','has_deadline','work_us','permission_us','permission_budget_us','route_us','persist_us','persist_budget_us','unread_us','peer_validate_us','peer_validate_budget_us','failed','suppressed_samples'}
repo_keys={'from','to','mid','started_us','total_us','status','outcome','threw','tid','cpu_us','precheck_us','acquire_us','begin_us','insert_us','identity_read_us','outbox_insert_us','commit_us','recovery_read_us','rollback_us','begin_insert_read_us','begin_insert_read_cpu_us','acquire_cpu_us','precheck_cpu_us','begin_cpu_us','insert_cpu_us','identity_read_cpu_us','outbox_insert_cpu_us','commit_cpu_us'}
for container,p,allowed in paths:
    assert p.is_file()
    with p.open() as stream:
        for line in stream:
            if 'gateway private chat phase sample, ' in line:
                row={'gateway':container}
                for pair in line.split('gateway private chat phase sample, ',1)[1].strip().split(', '):
                    k,sep,v=pair.partition('=')
                    if sep and k=='path' and v in ['chat','peer']:row[k]=v
                    elif sep and k in gw_keys and re.fullmatch('-?[0-9]+',v):row[k]=int(v)
                identity=lookup.get(row.get('message_id',0))
                if identity is None:continue
                assert identity['run'] in allowed and row.get('path') in ['chat','peer']
                if row['path']=='chat':assert row['user_id']==identity['sender'] and row['request_seq']==identity['client_seq'] and row['session_epoch']>0
                row['run']=identity['run'];gw.append(row)
            if 'private_persist_phase ' in line:
                row={k:int(v) for k,v in re.findall(r'([a-z_]+)=(-?\d+)',line.split('private_persist_phase ',1)[1]) if k in repo_keys};identity=lookup.get(row.get('mid',0))
                if identity is None:continue
                assert identity['run'] in allowed and row['from']==identity['sender'] and row['to']==identity['recipient'];row['run']=identity['run'];persist.append(row)
(d/'gateway-phase-numeric.json').write_text(json.dumps(gw,indent=2)+'\n');(d/'persistence-phase-numeric.json').write_text(json.dumps(persist,indent=2)+'\n')
matched=[];reports=[]
def percentile(v,p):return sorted(v)[math.ceil(len(v)*p)-1]
for name in runs:
    sends,acks,delivery=ledgers[name];chat={};repo={}
    for row in gw:
        if row['run']==name and row['path']=='chat':assert row['message_id'] not in chat;chat[row['message_id']]=row
    for row in persist:
        if row['run']==name:assert row['mid'] not in repo;repo[row['mid']]=row
    selected=[]
    for mid in sorted(chat.keys()&repo.keys()):
        c,q,ack=chat[mid],repo[mid],acks[mid];sent=sends[ack['cid']];assert c['user_id']==q['from']==ack['uid'] and q['to']==ack['to'] and c['request_seq']==ack['seq'];assert q['status']==0 and not q['threw'] and not c['failed']
        ack_ms=(ack['ns']-sent['ns'])/1e6;before_repo=(q['started_us']*1000-sent['ns'])/1e6;repo_ms=q['total_us']/1000;after_repo=(ack['ns']-(q['started_us']+q['total_us'])*1000)/1e6;outside=(c['persist_us']-q['total_us'])/1000
        assert min(before_repo,after_repo,outside)>=-.002 and abs(ack_ms-before_repo-repo_ms-after_repo)<.002
        row={'run':name,'mid':mid,'send_to_ack_ms':ack_ms,'before_repository_ms':before_repo,'repository_ms':repo_ms,'after_repository_ms':after_repo,'gateway_persist_rpc_ms':c['persist_us']/1000,'rpc_minus_repository_ms':outside,'dispatch_age_ms':c['dispatch_age_us']/1000,'permission_ms':c['permission_us']/1000,'route_ms':c['route_us']/1000,'unread_ms':c['unread_us']/1000,'commit_ms':q['commit_us']/1000,'batch_wall_ms':q['begin_insert_read_us']/1000};selected.append(row);matched.append(row)
    metrics={key:{'samples':len(v),'mean_ms':statistics.mean(v),'max_ms':max(v)} for key in ['send_to_ack_ms','before_repository_ms','repository_ms','after_repository_ms','gateway_persist_rpc_ms','rpc_minus_repository_ms','dispatch_age_ms','permission_ms','route_ms','unread_ms','commit_ms'] if (v:=[row[key] for row in selected])}
    worker=json.loads((b/('capacity-'+name)/'worker-0/final.json').read_text());wire=[(delivery[mid]['ns']-sends[a['cid']]['ns'])/1e6 for mid,a in acks.items()];assert min(wire)>=0
    reports.append({'run':name,'exact9000_send_ack_delivery_pairs':True,'histogram_mean_ms':{key:worker[key]['sum_us']/worker[key]['count']/1000 for key in ['ack_histogram','scheduled_to_ack_histogram','schedule_lag_histogram']},'ledger_diagnostic_wire_p99_ms':percentile(wire,.99),'chat_slow_biased_samples':len(chat),'repo_slow_biased_samples':len(repo),'same_mid_intersection':len(selected),'matched_slow_biased_metrics':metrics})
(d/'matched-numeric-rows.json').write_text(json.dumps(matched,indent=2)+'\n');assert runtime()==before
x={'status':'PRIVATE_BATCH_ABBA_ANALYSIS_COMPLETE','head':head,'official':official,'reports':reports,'private_log_sha256':{str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for _,p,_ in paths},'all19_runtime_configs_preserved':True,'new_load_tests':False,'full_feature_acceptance':False,'limits':'9000 official gates intact. Ledger wire timestamp is diagnostic. Sparse threshold/rate-limited samples cannot establish population P99 or sole cause; RPC minus repo mixes application/response/transport/scheduling, no CPU sample. Whole SQL elapsed notCPU; identitySELECTcount includes bothbyMID andLAST_INSERT_ID fullrecord validation.'}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps({'status':x['status'],'reports':reports,'full_feature_acceptance':False},indent=2))
PY
