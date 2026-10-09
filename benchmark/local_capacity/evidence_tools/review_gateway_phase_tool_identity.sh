#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib
r=pathlib.Path.cwd();b=r/'.local/codex';old=b/'gateway-controlled-phase-log-review-20261005';d=b/'gateway-controlled-phase-log-tool-review-20261005';assert old.is_dir() and not d.exists() and not (old/'summary.json').exists() and not (old/'gateway-phase-numeric.json').exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
ref='33fc9bbbf7b8a8e828ce70d1ae0035d8e8c71ce8'
source=subprocess.check_output(['git','show',ref+':gateway/GatewayServer.cpp'],text=True);types=subprocess.check_output(['git','show',ref+':gateway/business/BusinessRuntimeTypes.h'],text=True)
helper=source.split('SubmitMustRunConnectionBusinessTask(',1)[1].split('SubmitSessionBusinessTask(',1)[0]
assert 'task.request.user_id' not in helper and 'task.request.session_epoch' not in helper and 'std::uint64_t user_id{0}' in types and 'std::uint64_t session_epoch{0}' in types
peer=source.split('void GatewayServer::HandleGatewayForwardChatRequest(',1)[1].split('void GatewayServer::ExecuteGatewayForwardChatRequest(',1)[0];assert 'SubmitMustRunConnectionBusinessTask(' in peer and 'connection, 0, 0' in peer
current=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-gateway-a-1','tinyimx-m21-gateway-b-1','tinyimx-m21-message-service-1'],text=True));audit=json.loads((old/'audit-before.json').read_text());rollback=json.loads((b/'receiver-guarded-update-rollback-20261005/summary.json').read_text())
assert all(c['Id']==audit['container_ids'][c['Name']] for c in current if 'gateway-' in c['Name']);assert next(c for c in current if 'message-service' in c['Name'])['Id']==rollback['message_service_id']
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly classify first phase parsing assertion, exact running33fc source proves peer internal context user/epoch0','old_audit_sha256':hashlib.sha256((old/'audit-before.json').read_bytes()).hexdigest(),'original_stage_unchanged':True,'product_changes':False,'new_load':False,'fix':'Separate chat boundsender/seq/positiveepoch checks from peer internaluser0/epoch0; keepMID bound to completepositiveclientledger and everyactualsource semantic, no traffic auth changes.'},indent=2)+'\n')
x={'status':'GATEWAY_PHASE_TOOL_CONTEXT_IDENTITY_CLASSIFIED','first_error':'AssertionError at embedded Python line35: peer user_id incorrectly assumedrecipient','source_ref':ref,'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'context_types_sha256':hashlib.sha256(types.encode()).hexdigest(),'actual_peer_context_user_id':0,'actual_peer_context_session_epoch':0,'all_readonly_runtime_preserved':True,'first_incomplete_stage_preserved':True,'capacity_or_product_failure':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
