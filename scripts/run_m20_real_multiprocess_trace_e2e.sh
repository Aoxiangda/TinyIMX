#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${HOME}/projects/TinyIMX_publish"
BUILD="${ROOT}/build/linux-debug"
RUNTIME_BIN="${HOME}/opt/tinyimx-observability/bin"
EXPECTED_BRANCH="feature/m20-observability-v1"
EXPECTED_HEAD="3f42a43f91da6a83fd7a73b10630d48283a06005"
PROTECTED="gateway/GatewayPeerTransport.h"

if [[ $# -ge 1 ]]; then
  SOURCE_CONFIG="$1"
elif [[ -f "${ROOT}/config/gateway-a.local.json" ]]; then
  SOURCE_CONFIG="${ROOT}/config/gateway-a.local.json"
elif [[ -f "${ROOT}/config/gateway.json" ]]; then
  SOURCE_CONFIG="${ROOT}/config/gateway.json"
else
  echo "ERROR: no local service config found"
  echo "Usage: $0 /absolute/path/to/gateway-local.json"
  exit 10
fi
[[ "${SOURCE_CONFIG}" = /* ]] || SOURCE_CONFIG="${ROOT}/${SOURCE_CONFIG}"
[[ -f "${SOURCE_CONFIG}" ]] || { echo "ERROR: config missing: ${SOURCE_CONFIG}"; exit 11; }

export PATH="${RUNTIME_BIN}:${PATH}"

ACTOR=990020001
USERNAME="m20_trace_actor"
USER_TARGET="127.0.0.1:55352"
MCP_HOST="127.0.0.1"
MCP_PORT=18480
MCP_ENDPOINT="http://${MCP_HOST}:${MCP_PORT}/mcp"
OTLP_HOST="127.0.0.1"
OTLP_PORT=14317
OTLP_ENDPOINT="http://${OTLP_HOST}:${OTLP_PORT}"
TOKEN="tinyimx-m20-trace-e2e-token"

TS="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${HOME}/tmp/m20-real-trace-e2e-${TS}"
TRACE_FILE="${ARTIFACT_DIR}/traces.jsonl"
mkdir -p "${ARTIFACT_DIR}"
chmod 700 "${ARTIFACT_DIR}"

PIDS=()
SEEDED=0
MYSQL_READY=0

log(){ printf '[M20-TRACE-E2E] %s\n' "$*"; }
fail(){ log "FAIL: $*"; exit 1; }
require_cmd(){ command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"; }

check_listeners_released(){
  python3 - "${OTLP_PORT}" 55352 "${MCP_PORT}" <<'PY'
import pathlib,sys
wanted={int(x) for x in sys.argv[1:]}
listening=set()
for procfile in ('/proc/net/tcp','/proc/net/tcp6'):
    p=pathlib.Path(procfile)
    if not p.exists(): continue
    for line in p.read_text().splitlines()[1:]:
        c=line.split()
        if len(c)<4 or c[3] != '0A': continue
        try: port=int(c[1].rsplit(':',1)[1],16)
        except Exception: continue
        if port in wanted: listening.add(port)
if listening:
    print('M20_TRACE_LISTEN_RELEASE=FAIL busy=' + ','.join(map(str,sorted(listening))))
    raise SystemExit(1)
print('M20_TRACE_LISTEN_RELEASE=PASS')
PY
}

for cmd in python3 curl mysql cmake sha256sum otelcol-contrib; do require_cmd "$cmd"; done

cd "${ROOT}"

echo "========== M20 REAL TRACE E2E PRECHECK =========="
test "$(git branch --show-current)" = "${EXPECTED_BRANCH}"
test "$(git rev-parse HEAD)" = "${EXPECTED_HEAD}"
test -z "$(git diff --cached --name-only)"
PROTECTED_BEFORE="$(git diff -- "${PROTECTED}" | sha256sum | awk '{print $1}')"

python3 - "${OTLP_PORT}" 55352 "${MCP_PORT}" <<'PY'
import socket,sys
for p in map(int,sys.argv[1:]):
    s=socket.socket()
    try:
        s.bind(("127.0.0.1",p))
    except OSError as e:
        raise SystemExit(f"port already in use: {p}: {e}")
    finally:
        s.close()
print("M20_TRACE_PORT_PRECHECK=PASS")
PY

for f in \
  "${BUILD}/user_service_demo" \
  "${BUILD}/tinyimx_mcp_server" \
  "${BUILD}/tinyimx_ai_agent_demo" \
  "${BUILD}/m20_trace_agent_e2e"
do
  if [[ ! -x "$f" ]]; then
    : # built below
  fi
done

mapfile -t MYSQL_FIELDS < <(python3 - "${SOURCE_CONFIG}" <<'PY'
import json,sys
x=json.load(open(sys.argv[1],encoding='utf-8'))
m=x['mysql']
for k in ('host','port','database','user','password'):
    print(m.get(k,''))
PY
)
[[ "${#MYSQL_FIELDS[@]}" -eq 5 ]] || fail "failed to read mysql config"
MYSQL_HOST="${MYSQL_FIELDS[0]}"
MYSQL_PORT="${MYSQL_FIELDS[1]}"
MYSQL_DATABASE="${MYSQL_FIELDS[2]}"
MYSQL_USER="${MYSQL_FIELDS[3]}"
MYSQL_PASSWORD="${MYSQL_FIELDS[4]}"

mysql_exec(){
  MYSQL_PWD="${MYSQL_PASSWORD}" mysql --protocol=tcp \
    -h "${MYSQL_HOST}" -P "${MYSQL_PORT}" -u "${MYSQL_USER}" \
    -D "${MYSQL_DATABASE}" --batch --skip-column-names -e "$1"
}
mysql_script(){
  MYSQL_PWD="${MYSQL_PASSWORD}" mysql --protocol=tcp \
    -h "${MYSQL_HOST}" -P "${MYSQL_PORT}" -u "${MYSQL_USER}" \
    -D "${MYSQL_DATABASE}"
}

cleanup_db(){
  [[ "${MYSQL_READY}" -eq 1 && "${SEEDED}" -eq 1 ]] || return 0
  mysql_exec "DELETE FROM im_users WHERE user_id=${ACTOR};" >/dev/null 2>&1 || true
}

redact_generated_configs(){
  python3 - "${ARTIFACT_DIR}" <<'PY' >/dev/null 2>&1 || true
import json,pathlib,sys
root=pathlib.Path(sys.argv[1])
for name in ('user.json','mcp.json'):
    p=root/name
    if not p.exists():
        continue
    try:
        x=json.load(open(p,encoding='utf-8'))
        if isinstance(x.get('mysql'),dict) and x['mysql'].get('password'):
            x['mysql']['password']='__REDACTED_AFTER_START__'
        with open(p,'w',encoding='utf-8') as f:
            json.dump(x,f,indent=2)
            f.write('\n')
        p.chmod(0o600)
    except Exception:
        pass
PY
}

cleanup(){
  rc=$?
  trap - EXIT INT TERM
  for ((i=${#PIDS[@]}-1; i>=0; --i)); do
    pid="${PIDS[$i]}"
    if kill -0 "${pid}" 2>/dev/null; then
      kill -TERM "${pid}" 2>/dev/null || true
    fi
  done
  for pid in "${PIDS[@]}"; do
    for _ in $(seq 1 50); do
      kill -0 "${pid}" 2>/dev/null || break
      sleep 0.1
    done
    if kill -0 "${pid}" 2>/dev/null; then
      kill -KILL "${pid}" 2>/dev/null || true
    fi
    wait "${pid}" 2>/dev/null || true
  done
  cleanup_db
  redact_generated_configs
  if ! check_listeners_released; then
    rc=1
  fi
  echo "ARTIFACT_DIR=${ARTIFACT_DIR}"
  exit "${rc}"
}
trap cleanup EXIT INT TERM

log "MySQL connectivity precheck"
mysql_exec "SELECT 1;" >/dev/null
MYSQL_READY=1

existing="$(mysql_exec "SELECT COUNT(*) FROM im_users WHERE user_id=${ACTOR} OR username='${USERNAME}';")"
[[ "${existing}" == "0" ]] || fail "reserved M20 trace actor id/username already exists"

log "seed trace actor"
mysql_script >/dev/null <<SQL
INSERT INTO im_users
(user_id, username, nickname, avatar_url, password_salt, password_hash, status)
VALUES
(${ACTOR}, '${USERNAME}', 'M20 Trace Actor', 'trace.png', '', '', 1);
SQL
SEEDED=1

export M20_TRACE_ARTIFACT_DIR="${ARTIFACT_DIR}"
export M20_TRACE_ACTOR="${ACTOR}"
export M20_TRACE_MCP_PORT="${MCP_PORT}"
export M20_TRACE_OTLP_ENDPOINT="${OTLP_ENDPOINT}"
python3 - "${SOURCE_CONFIG}" "${ARTIFACT_DIR}" <<'PY'
import copy,json,os,sys
src,outdir=sys.argv[1:]
base=json.load(open(src,encoding='utf-8'))

def isolate(c):
    for section in ('redis','rocketmq','outbox_relay','unread_projection','gateway_registry','zookeeper'):
        c.setdefault(section,{})['enable']=False
    c.setdefault('service_discovery',{})['provider']='static'
    c.setdefault('rpc',{})['enable']=False
    c.setdefault('mcp',{})['enable']=False
    c.setdefault('logger',{})['console']=True
    c['logger']['async']=False
    c.setdefault('observability',{}).update({
        'enable':True,
        'metrics_enable':False,
        'traces_enable':True,
        'otlp_endpoint':os.environ['M20_TRACE_OTLP_ENDPOINT'],
        'metric_export_interval_ms':1000,
        'export_timeout_ms':2000,
        'shutdown_timeout_ms':5000,
        'trace_max_queue_size':256,
        'trace_max_export_batch_size':32,
        'trace_schedule_delay_ms':100,
    })

u=copy.deepcopy(base); isolate(u)
u.setdefault('mysql',{})['enable']=True
u.setdefault('app',{})['instance_id']='m20-trace-user-1'
u['app']['env']='test'
u['logger']['file']=os.path.join(outdir,'user.logger.log')
with open(os.path.join(outdir,'user.json'),'w',encoding='utf-8') as f:
    json.dump(u,f,indent=2)

m=copy.deepcopy(base); isolate(m)
m.setdefault('mysql',{})['enable']=False
m['mysql']['password']=''
m.setdefault('app',{})['instance_id']='m20-trace-mcp-1'
m['app']['env']='test'
m['logger']['file']=os.path.join(outdir,'mcp.logger.log')
mcp=m.setdefault('mcp',{})
mcp.update({
    'enable':True,
    'endpoint':f"http://127.0.0.1:{os.environ['M20_TRACE_MCP_PORT']}/mcp",
    'timeout_ms':5000,
    'listen_host':'127.0.0.1',
    'listen_port':int(os.environ['M20_TRACE_MCP_PORT']),
    'endpoint_path':'/mcp',
    'io_threads':1,
    'worker_threads':2,
    'queue_capacity':64,
    'max_request_bytes':1048576,
    'allowed_origins':[],
    'auth_token_env':'TINYIMX_MCP_TOKEN',
    'static_user_id':int(os.environ['M20_TRACE_ACTOR']),
    'static_subject':'tinyimx:m20:trace-e2e',
})
with open(os.path.join(outdir,'mcp.json'),'w',encoding='utf-8') as f:
    json.dump(m,f,indent=2)
PY
chmod 600 "${ARTIFACT_DIR}"/*.json

cat >"${ARTIFACT_DIR}/collector.yaml" <<EOF_COLLECTOR
receivers:
  otlp:
    protocols:
      grpc:
        endpoint: ${OTLP_HOST}:${OTLP_PORT}
processors:
  batch:
    timeout: 100ms
exporters:
  file/traces:
    path: ${TRACE_FILE}
    format: json
    append: true
    flush_interval: 100ms
service:
  pipelines:
    traces:
      receivers: [otlp]
      processors: [batch]
      exporters: [file/traces]
EOF_COLLECTOR

log "build tracing E2E targets (-j1)"
cmake --build "${BUILD}" --target \
  user_service_demo \
  social_service_demo \
  message_service_demo \
  group_service_demo \
  file_service_demo \
  tinyimx_mcp_server \
  tinyimx_ai_agent_demo \
  m20_ai_observability_config_tests \
  m20_trace_agent_e2e \
  -j1 >"${ARTIFACT_DIR}/build.log" 2>&1

for f in \
  "${BUILD}/user_service_demo" \
  "${BUILD}/tinyimx_mcp_server" \
  "${BUILD}/tinyimx_ai_agent_demo" \
  "${BUILD}/m20_trace_agent_e2e"
do
  [[ -x "$f" ]] || fail "required binary missing after build: $f"
done

"${BUILD}/m20_ai_observability_config_tests" \
  >"${ARTIFACT_DIR}/ai-observability-config-test.log" 2>&1
grep -Fq 'M20_AI_OBSERVABILITY_CONFIG_TEST=PASS' \
  "${ARTIFACT_DIR}/ai-observability-config-test.log"
echo "M20_AI_PROCESS_BOOTSTRAP_CONFIG=PASS"

LAST_PID=""
start_proc(){
  local label="$1"; shift
  "$@" >"${ARTIFACT_DIR}/${label}.stdout.log" 2>&1 &
  LAST_PID=$!
  PIDS+=("${LAST_PID}")
  log "started ${label}, pid=${LAST_PID}"
}

wait_log(){
  local label="$1" pid="$2" file="$3" pattern="$4"
  for _ in $(seq 1 150); do
    if ! kill -0 "${pid}" 2>/dev/null; then
      tail -80 "${file}" || true
      fail "${label} exited before ready"
    fi
    if grep -qE "${pattern}" "${file}" 2>/dev/null; then
      log "${label}=READY"
      return 0
    fi
    sleep 0.1
  done
  tail -80 "${file}" || true
  fail "${label} readiness timeout"
}

wait_port(){
  local port="$1"
  python3 - "${port}" <<'PY'
import socket,sys,time
port=int(sys.argv[1])
for _ in range(150):
    s=socket.socket(); s.settimeout(0.2)
    try:
        s.connect(('127.0.0.1',port)); s.close(); raise SystemExit(0)
    except OSError:
        s.close(); time.sleep(0.1)
raise SystemExit(1)
PY
}

log "start Collector trace file exporter"
start_proc collector otelcol-contrib --config "${ARTIFACT_DIR}/collector.yaml"
COLLECTOR_PID="${LAST_PID}"
wait_port "${OTLP_PORT}" || fail "Collector OTLP receiver not ready"
echo "M20_TRACE_COLLECTOR_READY=PASS"

log "start real UserService with tracing"
start_proc user "${BUILD}/user_service_demo" "${ARTIFACT_DIR}/user.json" "${USER_TARGET}"
USER_PID="${LAST_PID}"
wait_log user "${USER_PID}" "${ARTIFACT_DIR}/user.stdout.log" 'UserService ready'
redact_generated_configs
echo "M20_TRACE_ARTIFACT_SECRET_REDACTION=PASS"

export TINYIMX_USER_RPC_TARGET="${USER_TARGET}"
export TINYIMX_SOCIAL_RPC_TARGET=""
export TINYIMX_MESSAGE_RPC_TARGET=""
export TINYIMX_GROUP_RPC_TARGET=""
export TINYIMX_FILE_RPC_TARGET=""
export TINYIMX_MCP_TOKEN="${TOKEN}"

log "start real MCPServer with tracing"
start_proc mcp "${BUILD}/tinyimx_mcp_server" "${ARTIFACT_DIR}/mcp.json"
MCP_PID="${LAST_PID}"
wait_log mcp "${MCP_PID}" "${ARTIFACT_DIR}/mcp.stdout.log" 'MCP server started'

for _ in $(seq 1 60); do
  code="$(curl -sS -o "${ARTIFACT_DIR}/health.json" -w '%{http_code}' \
    "http://${MCP_HOST}:${MCP_PORT}/health" 2>/dev/null || true)"
  [[ "${code}" == "200" ]] && break
  sleep 0.1
done
[[ "${code:-}" == "200" ]] || fail "MCP health not ready"

echo "M20_TRACE_MCP_READY=PASS"

log "run deterministic Agent -> MCP -> UserService trace flow"
set +e
"${BUILD}/m20_trace_agent_e2e" \
  "${MCP_ENDPOINT}" \
  "${TOKEN}" \
  "${OTLP_ENDPOINT}" \
  "${USERNAME}" \
  >"${ARTIFACT_DIR}/agent.stdout.log" 2>&1
AGENT_RC=$?
set -e
cat "${ARTIFACT_DIR}/agent.stdout.log"
[[ "${AGENT_RC}" -eq 0 ]] || fail "trace agent returned ${AGENT_RC}"
grep -Fq "M20_TRACE_AGENT_OK username=${USERNAME}" "${ARTIFACT_DIR}/agent.stdout.log"
grep -Fq 'M20_TRACE_AGENT_TOOL_CALLS=1' "${ARTIFACT_DIR}/agent.stdout.log"
grep -Fq 'M20_TRACE_AGENT_E2E=PASS' "${ARTIFACT_DIR}/agent.stdout.log"
echo "M20_TRACE_AGENT_FLOW=PASS"

# Gracefully stop long-running traced processes so their providers ForceFlush.
for pair in "mcp:${MCP_PID}" "user:${USER_PID}"; do
  label="${pair%%:*}"; pid="${pair##*:}"
  if kill -0 "${pid}" 2>/dev/null; then
    kill -TERM "${pid}"
    for _ in $(seq 1 80); do
      kill -0 "${pid}" 2>/dev/null || break
      sleep 0.1
    done
    if kill -0 "${pid}" 2>/dev/null; then
      fail "${label} did not stop gracefully"
    fi
    wait "${pid}" 2>/dev/null || true
  fi
done

# Wait for file exporter flush and expected root span.
for _ in $(seq 1 100); do
  if [[ -s "${TRACE_FILE}" ]] && grep -Fq 'tinyimx.ai.agent.run' "${TRACE_FILE}"; then
    break
  fi
  sleep 0.1
done
[[ -s "${TRACE_FILE}" ]] || fail "Collector trace evidence file is empty"
grep -Fq 'tinyimx.ai.agent.run' "${TRACE_FILE}" || fail "agent span missing from trace export"

echo "M20_TRACE_EXPORT_FILE=PASS"

if kill -0 "${COLLECTOR_PID}" 2>/dev/null; then
  kill -TERM "${COLLECTOR_PID}"
  for _ in $(seq 1 80); do
    kill -0 "${COLLECTOR_PID}" 2>/dev/null || break
    sleep 0.1
  done
  if kill -0 "${COLLECTOR_PID}" 2>/dev/null; then
    fail "Collector did not stop gracefully"
  fi
  wait "${COLLECTOR_PID}" 2>/dev/null || true
fi

check_listeners_released

python3 - "${TRACE_FILE}" "${ARTIFACT_DIR}/agent.stdout.log" "${ARTIFACT_DIR}/trace-summary.json" <<'PY'
import json,re,sys
trace_file,agent_log,summary_file=sys.argv[1:]

spans=[]

def scalar(v):
    if not isinstance(v,dict): return None
    for k in ('stringValue','intValue','doubleValue','boolValue'):
        if k in v: return v[k]
    return None

def attrs(items):
    out={}
    for a in items or []:
        if isinstance(a,dict) and 'key' in a:
            out[a['key']]=scalar(a.get('value',{}))
    return out

with open(trace_file,encoding='utf-8') as f:
    for line in f:
        line=line.strip()
        if not line: continue
        obj=json.loads(line)
        for rs in obj.get('resourceSpans',[]):
            resource=attrs(rs.get('resource',{}).get('attributes',[]))
            service=resource.get('service.name','')
            instance=resource.get('service.instance.id','')
            namespace=resource.get('service.namespace','')
            environment=resource.get('deployment.environment.name','')
            for ss in rs.get('scopeSpans',[]):
                for sp in ss.get('spans',[]):
                    spans.append({
                        'service':service,
                        'instance':instance,
                        'namespace':namespace,
                        'environment':environment,
                        'name':sp.get('name',''),
                        'traceId':sp.get('traceId','').lower(),
                        'spanId':sp.get('spanId','').lower(),
                        'parentSpanId':sp.get('parentSpanId','').lower(),
                        'attributes':attrs(sp.get('attributes',[])),
                    })

if not spans:
    raise SystemExit('no spans decoded from trace file')

def one(service,name):
    found=[s for s in spans if s['service']==service and s['name']==name]
    if len(found)!=1:
        raise AssertionError((service,name,len(found),found))
    return found[0]

agent=one('tinyimx-trace-agent-e2e','tinyimx.ai.agent.run')
mcp_client=one('tinyimx-trace-agent-e2e','tinyimx.mcp.client.tools/call')
mcp_server=one('tinyimx-mcp-server','tinyimx.mcp.server.tools/call')
tool=one('tinyimx-mcp-server','tinyimx.mcp.tool.tinyimx.user.get_self_profile')
grpc_client=one('tinyimx-mcp-server','tinyimx.user.v1.UserService/GetUserProfile')
grpc_server=one('tinyimx-user-service','tinyimx.user.v1.UserService/GetUserProfile')

chain=[agent,mcp_client,mcp_server,tool,grpc_client,grpc_server]
trace_ids={x['traceId'] for x in chain}
assert len(trace_ids)==1 and '' not in trace_ids, trace_ids
assert agent['parentSpanId'] in ('', '0000000000000000'), agent
for span in chain:
    assert span['namespace']=='tinyimx', span
    assert span['environment']=='test', span
    assert span['instance'], span
assert tool['attributes'].get('mcp.tool.name')=='tinyimx.user.get_self_profile', tool
assert grpc_client['attributes'].get('rpc.service')=='tinyimx.user.v1.UserService', grpc_client
assert grpc_client['attributes'].get('rpc.method')=='GetUserProfile', grpc_client
assert grpc_server['attributes'].get('rpc.service')=='tinyimx.user.v1.UserService', grpc_server
assert grpc_server['attributes'].get('rpc.method')=='GetUserProfile', grpc_server
assert mcp_client['parentSpanId']==agent['spanId'], (mcp_client,agent)
assert mcp_server['parentSpanId']==mcp_client['spanId'], (mcp_server,mcp_client)
assert tool['parentSpanId']==mcp_server['spanId'], (tool,mcp_server)
assert grpc_client['parentSpanId']==tool['spanId'], (grpc_client,tool)
assert grpc_server['parentSpanId']==grpc_client['spanId'], (grpc_server,grpc_client)

log=open(agent_log,encoding='utf-8').read().splitlines()
lines=[x for x in log if 'M20_TRACE_LOG_CORRELATION_SENTINEL' in x]
assert lines, 'log correlation sentinel missing'
m=re.search(r'trace_id=([0-9a-f]{32}) span_id=([0-9a-f]{16})',lines[0])
assert m, lines[0]
assert m.group(1)==agent['traceId'], (m.group(1),agent['traceId'])
assert m.group(2)==agent['spanId'], (m.group(2),agent['spanId'])

summary={
    'trace_id':agent['traceId'],
    'chain':[
        {
            'service':s['service'],
            'instance':s['instance'],
            'name':s['name'],
            'span_id':s['spanId'],
            'parent_span_id':s['parentSpanId']
        }
        for s in chain
    ],
    'log_trace_id':m.group(1),
    'log_span_id':m.group(2),
}
json.dump(summary,open(summary_file,'w',encoding='utf-8'),indent=2)
print('M20_TRACE_SINGLE_TRACE_ID=PASS')
print('M20_TRACE_PARENT_CHAIN=PASS')
print('M20_TRACE_RESOURCE_IDENTITY=PASS')
print('M20_TRACE_RPC_ATTRIBUTES=PASS')
print('M20_TRACE_LOG_CORRELATION=PASS')
print('TRACE_ID='+agent['traceId'])
PY

echo
cat "${ARTIFACT_DIR}/trace-summary.json"

echo
PROTECTED_AFTER="$(git diff -- "${PROTECTED}" | sha256sum | awk '{print $1}')"
test "${PROTECTED_BEFORE}" = "${PROTECTED_AFTER}"
echo "PROTECTED_GATEWAY_DELTA=PASS"
echo "M20_REAL_MULTIPROCESS_TRACE_E2E=PASS"
echo "ARTIFACT_DIR=${ARTIFACT_DIR}"
