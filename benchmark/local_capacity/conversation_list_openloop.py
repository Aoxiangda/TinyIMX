#!/usr/bin/env python3
"""Owned fixed50page open-loop companion. No data mutations, retain every schedule/outcome."""

import pathlib,json,time,sys,types,hashlib,math,os
from cross_feature_actor import Actor,Run
ROOT=pathlib.Path('/home/jackson7/projects/TinyIMX_publish')
def main():
 assert len(sys.argv)==4
 case,bg,out=sys.argv[1:];assert case in ['mixed-A1','mixed-B1','mixed-B2','mixed-A2']
 out=pathlib.Path(out);root=ROOT/'.local/codex/conversation-unread-mixed-10k-20261006'
 assert out==root/case/'list-openloop' and not out.exists();out.mkdir(mode=0o700);os.umask(0o077)
 def save(n,x):(out/n).write_text(json.dumps(x,indent=2)+'\n')
 expected=json.loads((ROOT/'.local/codex/conversation-unread-batch-gateway-run-20261006/fixed-page.json').read_text())['response']
 bg=pathlib.Path(bg);assert bg.parent==ROOT/'.local/codex' and bg.name.startswith('capacity-cumix')
 audit=json.loads((bg/'audit-before.json').read_text());duration=audit['scenario']['duration'];assert duration==60 and audit['scenario']['users']==10000
 start=int((bg/'control/start_ns').read_text());end=start+duration*1_000_000_000
 save('audit-before.json',{'operation':'One existing owned actor read-only fixed50page scheduled20/s companion inside original private10k steady barrier','background':str(bg),'duration':duration,'start_mono_ns':start,'end_mono_ns':end,'rate':20,'planned':1200,'uid':519950,'socket_timeout_seconds':3,'response_deadline_seconds':3,'SQL_writes':False,'expected_full_response_sha256':hashlib.sha256(json.dumps(expected,sort_keys=True,separators=(',',':')).encode()).hexdigest(),'limits':'1200requests from one actor, scheduled/send/response latency +all failures/skips; nottotaluser-all-feature or independentCPU attribution'})
 v=Run(types.SimpleNamespace(run='cumix'+case,host='192.168.220.128',port=9000,users=[]),out);rows=[];pending={};late={};bad=0;timeouts=0;failure=None;c=None
 def emit(event,**kw):
  body=kw.pop('body',None)
  if body is not None:kw['body_sha256']=hashlib.sha256(json.dumps(body,sort_keys=True,separators=(',',':')).encode()).hexdigest()
  with (out/'timeline.jsonl').open('a') as f:f.write(json.dumps({'monotonic_ns':time.monotonic_ns(),'event':event,**kw})+'\n')
 v.emit=emit
 try:
  c=Actor(v,519950,'192.168.220.128',9000)
  assert time.monotonic_ns()<start,'Owned companion login missed steady start; preserve failure, no delayedwindow'
  while time.monotonic_ns()<start:v.pump(min(.01,max(0,(start-time.monotonic_ns())/1e9)))
  i=0;rate=20;planned=1200;deadline_ns=3_000_000_000
  with (out/'requests.jsonl').open('a') as raw:
   while i<planned or pending:
    now=time.monotonic_ns();assert not (bg/'control/abort').exists(),'Background aborted; stop companion'
    while i<planned and now>=start+i*1_000_000_000//rate:
     due=start+i*1_000_000_000//rate;sent=time.monotonic_ns();seq=c.send(2007,{'limit':50});pending[seq]={'index':i,'seq':seq,'scheduled_mono_ns':due,'sent_mono_ns':sent,'send_lateness_ms':(sent-due)/1e6};i+=1;now=time.monotonic_ns()
    wait=.01
    if i<planned:wait=min(wait,max(0,(start+i*1_000_000_000//rate-time.monotonic_ns())/1e9))
    v.pump(wait,heartbeats=time.monotonic_ns()<end);received=time.monotonic_ns()
    for (kind,seq),reply in list(c.replies.items()):
     if kind not in [2008,9999]:raise AssertionError('Unexpected response type')
     c.replies.pop((kind,seq))
     item=pending.pop(seq,None)
     if item is None:
      assert seq in late,'Unexpected/duplicate response seq';emit('late-response',seq=seq,kind=kind);continue
     valid=kind==2008 and reply==expected
     if not valid:
      bad+=1;save('invalid-response-'+str(seq)+'.json',{'kind':kind,'reply':reply})
     item.update({'received_mono_ns':received,'sent_to_response_ms':(received-item['sent_mono_ns'])/1e6,'scheduled_to_response_ms':(received-item['scheduled_mono_ns'])/1e6,'status':'PASS' if valid else 'INVALID','full_response_equal':valid})
     rows.append(item);raw.write(json.dumps(item)+'\n');raw.flush()
    for seq,item in list(pending.items()):
     if time.monotonic_ns()-item['sent_mono_ns']>deadline_ns:
      pending.pop(seq);late[seq]=item;timeouts+=1;item.update({'status':'TIMEOUT','full_response_equal':False});rows.append(item);raw.write(json.dumps(item)+'\n');raw.flush()
    assert time.monotonic_ns()<end+10_000_000_000,'Own companion drain deadline'
   until=time.monotonic()+10
   while c.hb_sent!=c.hb_ack:assert time.monotonic()<until,'Owned heartbeat drain';v.pump(.01,heartbeats=False)
   assert c.hb_sent and c.hb_sent==c.hb_ack
  def stats(field):
   a=sorted(x[field] for x in rows if field in x);return {'count':len(a),'mean_ms':sum(a)/len(a),'p50_ms':a[math.ceil(len(a)*.5)-1],'p99_ms':a[math.ceil(len(a)*.99)-1],'max_ms':a[-1]} if a else {'count':0}
  x={'status':'PASS' if len(rows)==1200 and bad==0 and timeouts==0 else 'FAIL','planned':1200,'sent':i,'recorded':len(rows),'invalid':bad,'timeouts':timeouts,'skipped':1200-i,'socket_closed_early':False,'sent_to_response':stats('sent_to_response_ms'),'scheduled_to_response':stats('scheduled_to_response_ms'),'send_lateness':stats('send_lateness_ms'),'heartbeats_sent':len(c.hb_sent),'heartbeats_ack':len(c.hb_ack),'background_users':10000,'list_actor_count':1,'list_rate':20,'limits':'Full fixed50page verified, one read actor20requests/s alongside10000private ring; response sampling includes clientJSON/evidence work. Notall50k capacity.'}
  save('summary.json',x);print(json.dumps(x));return 0 if x['status']=='PASS' else 2
 except BaseException as e:
  save('failed.json',{'type':type(e).__name__,'message':str(e),'recorded':len(rows),'invalid':bad,'timeouts':timeouts});raise
 finally:
  for c in list(v.clients):c.close()
if __name__=='__main__':raise SystemExit(main())
