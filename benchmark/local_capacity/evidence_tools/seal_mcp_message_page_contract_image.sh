#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,datetime,hashlib,shutil,subprocess,os
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'mcp-message-page-contract-image-20261005';assert not d.exists()
s=json.loads((b/'mcp-message-page-contract-build-20261005/summary.json').read_text());live=json.loads((b/'mcp-message-page-contract-live-20261005/summary.json').read_text());assert s['green_domain_checks']==89 and live['status']=='MCP_MESSAGE_PAGE_CONTRACT_REAL_DOMAIN_COMPLETED_AND_STOPPED' and live['owned_process_stopped']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
p=r/'build/linux-release/tinyimx_mcp_server';assert hashlib.sha256(p.read_bytes()).hexdigest()==s['candidate_mcp_binary_sha256']
base='tinyimx/runtime:m21-final';old='sha256:38dca459e0a88141c1385503b28cbd6e29a1eed18d253f808dc1dfa89e0a0316';assert json.loads(subprocess.check_output(['docker','image','inspect',base],text=True))[0]['Id']==old
current=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-mcp-server-1'],text=True))[0];assert current['Image']==old
oldsha=subprocess.check_output(['docker','exec',current['Id'],'sha256sum','/opt/tinyimx/bin/tinyimx_mcp_server'],text=True).split()[0];assert oldsha=='8cd229e2522d857f3612835ae0af5fc2f28d0fb3cbe7cf17cc28a84e784c77ed'
image='tinyimx/runtime:codex-mcp-message-page-contract-v1';assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
probes=['tinyimx-codex-mcp-page-loader-20261005','tinyimx-codex-mcp-page-exec-20261005'];names=subprocess.check_output(['docker','ps','-a','--format','{{.Names}}'],text=True).splitlines();assert not set(probes)&set(names)
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Seal MCP-only image after89domainchecks/corepass and8realRPCdomainchecks withboundaries/auth','compiled_head':s['head'],'binary_sha256':s['candidate_mcp_binary_sha256'],'base_image':old,'original_mcp_binary_sha256':oldsha,'writes':'Fresh singlebinary context0755/image/2ownedstoppedloaderprobes retained','network':'Buildnone/pullfalse/probesnone','deployment':False,'rollback':'Originalbase/image/MCPbinary retained, no prune/delete/SQL/configchange'},indent=2)+'\n')
context=d/'image-context';context.mkdir();shutil.copy2(p,context/'tinyimx_mcp_server');os.chmod(context/'tinyimx_mcp_server',0o755)
(d/'Dockerfile').write_text('FROM '+base+'\nCOPY --chmod=0755 tinyimx_mcp_server /opt/tinyimx/bin/tinyimx_mcp_server\nLABEL org.opencontainers.image.revision="'+s['head']+'"\nLABEL tinyimx.binary.sha256="'+s['candidate_mcp_binary_sha256']+'"\n')
with (d/'build.log').open('w') as f:subprocess.run(['docker','build','--pull=false','--network=none','-f',str(d/'Dockerfile'),'-t',image,str(context)],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
common=['docker','run','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges','--label','codex.task=mcp-message-page-contract-20261005']
with (d/'uid1000-loader.log').open('w') as f:result=subprocess.run(common+['--name',probes[0],'--entrypoint','/bin/sh',image,'-ec','test -x "$1"; ldd -r "$1"','owned-mcp-loader','/opt/tinyimx/bin/tinyimx_mcp_server'],stdout=f,stderr=subprocess.STDOUT,timeout=20)
assert result.returncode==0 and not any(x in (d/'uid1000-loader.log').read_text().lower() for x in ['not found','undefined symbol'])
with (d/'uid1000-exec.log').open('w') as f:result=subprocess.run(common+['--name',probes[1],'--entrypoint','/opt/tinyimx/bin/tinyimx_mcp_server',image,'/tmp/codex-mcp-page-nonexistent-20261005.json'],stdout=f,stderr=subprocess.STDOUT,timeout=15)
assert result.returncode==1
c=json.loads(subprocess.check_output(['docker','image','inspect',image],text=True))[0];assert c['Config']['Labels']['org.opencontainers.image.revision']==s['head'] and c['Config']['Labels']['tinyimx.binary.sha256']==s['candidate_mcp_binary_sha256']
x={'status':'MCP_MESSAGE_PAGE_CONTRACT_IMAGE_SEALED','image_tag':image,'image_id':c['Id'],'compiled_head':s['head'],'binary_sha256':s['candidate_mcp_binary_sha256'],'original_image':old,'original_image_tag':base,'original_mcp_binary_sha256':oldsha,'uid1000_loader':'PASS','uid1000_exec':'PASS_expected_missingconfig1','probes_retained':probes,'deployment':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
