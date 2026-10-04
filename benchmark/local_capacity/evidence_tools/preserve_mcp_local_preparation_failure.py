from pathlib import Path
import json,hashlib,datetime,shutil
r=Path(__file__).resolve().parent.parent;d=r/'evidence/mcp-page-preparation-encoding-failure-20261005';assert not d.exists()
audit=json.loads((r/'audit/mcp-message-page-contract-before.json').read_text(encoding='utf-8'))
paths=['services/intelligence/mcp/McpDomainTools.cpp','tests/mcp/mcp_domain_tools_test.cpp']
before={x['path']:x['before_sha256'] for x in audit['files']};rows=[]
for name in paths:
 p=r/'source'/name;original=r/'reference/mcp-message-page-contract'/name
 assert hashlib.sha256(original.read_bytes()).hexdigest()==before[name]
 rows.append({'path':name,'partial_preparation_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'restore_exact_before_sha256':before[name]})
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Preserve local partial candidate, restoreexact auditedlocalpreimages then rerunfixed UTF8 generator','reason':'Windows defaultGBK read ofChinese docs caused UnicodeDecodeError beforepackage/upload; Ubuntu andruntimeunchanged','paths':rows,'writes':'Onlytwoauditedlocalsourcefiles andfreshown failureevidence, no deletion'},indent=2)+'\n',encoding='utf-8')
for name in paths:
 p=r/'source'/name;dest=d/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest);shutil.copy2(r/'reference/mcp-message-page-contract'/name,p)
print('LOCAL_PARTIAL_CANDIDATE_PRESERVED_EXACT_PREIMAGES_RESTORED')
