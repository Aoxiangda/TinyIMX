#!/usr/bin/env python3
"""Exercise all real companion startup assertions before any Actor sockets."""
import importlib.util,json,pathlib,sys,types,datetime,hashlib
module_path=pathlib.Path(sys.argv[1]).resolve();out=pathlib.Path(sys.argv[2]).resolve();assert not out.exists();out.mkdir(mode=0o700)
(out/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Actual list main with own synthetic root/fixture/background files and fake Run/Actor; no SQL, Docker, sockets or capacity; keep all strict bindings','module_sha256':hashlib.sha256(module_path.read_bytes()).hexdigest()})+chr(10))
sys.path.insert(0,str(module_path.parent));spec=importlib.util.spec_from_file_location('current20k_list',module_path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
root=out/'synthetic-project';(root/'.local/codex/permission20k-diagnostic-20261007/mixed20k-ON').mkdir(parents=True)
fixed=root/'.local/codex/conversation-unread-batch-gateway-run-20261006';fixed.mkdir();(fixed/'fixed-page.json').write_text(json.dumps({'response':{'success':True,'conversations':[]}}))
bg=root/'.local/codex/capacity-per20kA';(bg/'control').mkdir(parents=True);(bg/'audit-before.json').write_text(json.dumps({'scenario':{'duration':60,'users':20000}}));(bg/'control/start_ns').write_text('999999999999999999')
module.ROOT=root;made=[]
class FakeRun:
 def __init__(self,*args):self.clients=[]
class BeforeNetwork(Exception):pass
class FakeActor:
 def __init__(self,*args):made.append('actor_constructor_reached');raise BeforeNetwork('audited stop before sockets')
module.Run=FakeRun;module.Actor=FakeActor
outdir=root/'.local/codex/permission20k-diagnostic-20261007/mixed20k-ON/list-openloop';oldargv=sys.argv;escaped=None
try:
 sys.argv=[str(module_path),'mixed20k-ON',str(bg),str(outdir)]
 try:module.main()
 except BeforeNetwork as exc:escaped=str(exc)
finally:sys.argv=oldargv
assert escaped=='audited stop before sockets' and made==['actor_constructor_reached']
audit=json.loads((outdir/'audit-before.json').read_text());assert audit['planned']==1200 and audit['rate']==20 and audit['background']==str(bg) and audit['SQL_writes'] is False
failure=json.loads((outdir/'failed.json').read_text());assert failure['type']=='BeforeNetwork' and failure['recorded']==0
result={'status':'ALL_LIST_CLIENT_STARTUP_BINDINGS_PASS_BEFORE_NETWORK','strict_case':'mixed20k-ON','strict_background':'capacity-per20kA','strict_output_root':'permission20k-diagnostic-20261007','planned':1200,'rate':20,'real_load_or_runtime_changes':False};(out/'summary.json').write_text(json.dumps(result,indent=2)+chr(10));print(json.dumps(result))
