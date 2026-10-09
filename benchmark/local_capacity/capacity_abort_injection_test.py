#!/usr/bin/env python3
"""Inject failures into the actual coordinator run; no sockets, SQL or Docker."""
import argparse, contextlib, datetime, hashlib, importlib.util, json, pathlib, sys
from types import SimpleNamespace
from unittest import mock
p=argparse.ArgumentParser();p.add_argument('module');p.add_argument('out');p.add_argument('--expect-legacy',action='store_true');a=p.parse_args()
source=pathlib.Path(a.module).resolve();out=pathlib.Path(a.out).resolve();assert not out.exists();out.mkdir(mode=0o700)
def save(n,x):(out/n).write_text(json.dumps(x,indent=2)+chr(10))
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Actual run function; mock Docker/SQL/Popen and descriptor limit, five first-failure injections; own evidence retained, no sockets/runtime/database changes','module_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'legacy_expected':a.expect_legacy})
spec=importlib.util.spec_from_file_location('audited_capacity_module',source);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class EmptyError(Exception):
 def __str__(self):return ''
cases=[('keyboard',KeyboardInterrupt(),True),('assertion',AssertionError(),True),('empty',EmptyError(),True),('runtime',RuntimeError('injected first failure'),False),('systemexit',SystemExit(0),False)]
rows=[]
for label,fault,empty in cases:
 root=out/label;root.mkdir();(root/'.local/codex').mkdir(parents=True);(root/'build/linux-release').mkdir(parents=True);(root/'build/linux-release/tinyimx_capacity_worker').write_bytes(b'owned unused fake binary')
 args=SimpleNamespace(run=label,users=2,rate=1.0,duration=1,user_id_base=100000,username_prefix='owned_abort_',gateway_image='sha256:'+'0'*64,message_image='sha256:'+'0'*64,host='127.0.0.1',source_ips=None,mode='private')
 identity=[{'name':'/tinyimx-m21-'+role+'-1','image':args.gateway_image,'health':'healthy'} for role in ['gateway-a','gateway-b','message-service']]
 def sql(q):
  if 'FROM im_users' in q and q.startswith('SELECT COUNT(*)'):return '2'
  if q.startswith('SELECT e.u,e.v'):return '100001\t100002\t1\t1\n100002\t100001\t1\t1\n'
  if q.startswith('SELECT COUNT(*) FROM im_private_messages'):return '0'
  raise AssertionError('Unexpected mocked SQL')
 oldcwd=pathlib.Path.cwd();code=None;escaped=None
 try:
  with mock.patch.object(module,'ROOT',root),mock.patch.object(module,'container_identity',return_value=identity),mock.patch.object(module,'sql',side_effect=sql),mock.patch.object(module,'cmd',return_value='owned mocked readonly command'),mock.patch.object(module.resource,'setrlimit'),mock.patch.object(module.subprocess,'Popen',side_effect=fault):
   try:code=module.run(args)
   except BaseException as exc:escaped={'type':type(exc).__name__,'message':str(exc)}
 finally:module.os.chdir(oldcwd)
 stage=root/'.local/codex'/('capacity-'+label);summary=json.loads((stage/'summary.json').read_text()) if (stage/'summary.json').exists() else None;failure=json.loads((stage/'failure.json').read_text())
 expected_error=str(fault) or type(fault).__name__
 passed=code==2 and escaped is None and summary is not None and summary['status']=='FAIL' and summary['error']==expected_error and failure['error']==expected_error and (stage/'control/abort').read_text().strip()==expected_error and summary['metrics']=={} and summary['workers']==[]
 if a.expect_legacy and empty:assert not passed and escaped and escaped['type']=='UnboundLocalError' and summary is None and failure['error']==''
 else:assert passed,{'case':label,'code':code,'escaped':escaped,'summary':summary,'failure':failure}
 rows.append({'case':label,'empty_exception_text':empty,'code':code,'escaped':escaped,'summary_saved':summary is not None,'failure_preserved':passed,'legacy_failure_expected':a.expect_legacy and empty})
 save('cases.json',rows)
result={'status':'EXPECTED_LEGACY_EMPTY_ABORT_FAILURE_REPRODUCED' if a.expect_legacy else 'COORDINATOR_ABORT_INJECTION_PASS','cases':rows,'production_changes':False,'load_generated':False};save('summary.json',result);print(json.dumps(result))
