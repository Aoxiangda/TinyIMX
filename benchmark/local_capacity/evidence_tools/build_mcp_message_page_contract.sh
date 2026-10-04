#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,datetime,hashlib,subprocess,shlex,tarfile,re,shutil
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'mcp-message-page-contract-build-20261005';assert not d.exists()
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
source=json.loads((b/'mcp-message-page-contract-source-20261005/summary.json').read_text());assert source['head']==head
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True))
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'mcp-message-page-contract-source-20261005/runtime-after.json').read_text())
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
build=r/'build/linux-release';d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Isolated original-source red regression then cached single-thread MCP-only green build and tests','reads':'Source beforearchive, cached compiler flags/link command and candidate source/tests','writes':'Fresh owned red artifacts/logs plus existing MCP targets; before binaries backed up withSHA','impact':'Onecompiler atatime, no service deployment/restart; noGatewaybuild/dependencyinstall; originalruntime/config unchanged','rollback':'All backups/artifacts/redFAIL retained; buildartifact is distinct from runningimage','business_acceptance':False},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
binaries={}
for name in ['m19_mcp_domain_tools_tests','m19_mcp_core_tests','tinyimx_mcp_server']:
 p=build/name
 if p.exists():shutil.copy2(p,d/(name+'.before'));binaries[name]=hashlib.sha256(p.read_bytes()).hexdigest()
(d/'binary-before-sha256.json').write_text(json.dumps(binaries,indent=2)+'\n')
oldcpp=d/'original-McpDomainTools.cpp'
with tarfile.open(b/'mcp-message-page-contract-source-20261005/source-before.tar.gz') as t:oldcpp.write_bytes(t.extractfile('services/intelligence/mcp/McpDomainTools.cpp').read())
assert hashlib.sha256(oldcpp.read_bytes()).hexdigest()=='1a5ef6126f113c057fc18c2538febb896cff6b96c7b08576467e808ba9b538f5'
def flags(target):
 text=(build/'CMakeFiles'/f'{target}.dir/flags.make').read_text();result=[]
 for name in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
  line=next(x for x in text.splitlines() if x.startswith(name+' ='));result+=shlex.split(line.split('=',1)[1])
 return result
def call(args,label,ok=True,timeout=240):
 (d/(label+'-command.json')).write_text(json.dumps(args,indent=2)+'\n')
 with (d/(label+'.log')).open('w') as f:p=subprocess.run(args,cwd=build,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 if ok:assert p.returncode==0,label+' failed; preserved log'
 return p.returncode
call(['/usr/bin/c++',*flags('tinyimx_mcp_domain'),'-c',str(oldcpp),'-o',str(d/'original-domain.o')],'red-original-domain-compile')
call(['/usr/bin/c++',*flags('m19_mcp_domain_tools_tests'),'-c',str(r/'tests/mcp/mcp_domain_tools_test.cpp'),'-o',str(d/'new-tests.o')],'red-new-tests-compile')
link=shlex.split((build/'CMakeFiles/m19_mcp_domain_tools_tests.dir/link.txt').read_text());object_path='CMakeFiles/m19_mcp_domain_tools_tests.dir/tests/mcp/mcp_domain_tools_test.cpp.o';assert link.count(object_path)==1 and link.count('libtinyimx_mcp_domain.a')==1
link[link.index(object_path)]=str(d/'new-tests.o');link[link.index('libtinyimx_mcp_domain.a')]=str(d/'original-domain.o');link[link.index('-o')+1]=str(d/'original-domain-new-tests')
call(link,'red-link');red=call([str(d/'original-domain-new-tests')],'red-regression',ok=False,timeout=20)
assert red==1 and 'CHECK failed: schema matches each domain page contract' in (d/'red-regression.log').read_text(),'Original must fail specific contract, notcompiler/runtime error'
call(['cmake','--build',str(build),'--parallel','1','--target','m19_mcp_domain_tools_tests','m19_mcp_core_tests','tinyimx_mcp_server'],'candidate-mcp-only-build',timeout=600)
call([str(build/'m19_mcp_domain_tools_tests')],'green-domain',timeout=30);call([str(build/'m19_mcp_core_tests')],'green-core',timeout=30)
assert 'M19_MCP_DOMAIN_TOOLS_TESTS=PASS' in (d/'green-domain.log').read_text()
domain_checks=(d/'green-domain.log').read_text().count('[PASS]');core_checks=(d/'green-core.log').read_text().count('[PASS]')
after=runtime();assert after==before;assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
subprocess.run(['git','diff','--check'],check=True);assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
x={'status':'MCP_PAGE_CONTRACT_RED_GREEN_BUILD_COMPLETED','head':head,'original_source_sha256':hashlib.sha256(oldcpp.read_bytes()).hexdigest(),'new_tests_sha256':hashlib.sha256((r/'tests/mcp/mcp_domain_tools_test.cpp').read_bytes()).hexdigest(),'red_exit':red,'red_failure':'schema matches each domain page contract','green_domain_checks':domain_checks,'green_core_reported_pass_lines':core_checks,'green_core_exit':0,'candidate_mcp_binary_sha256':hashlib.sha256((build/'tinyimx_mcp_server').read_bytes()).hexdigest(),'all19_runtime_configs_preserved':True,'deployment':False,'performance_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
