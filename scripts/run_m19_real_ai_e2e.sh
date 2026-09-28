#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${HOME}/projects/TinyIMX_publish"
BUILD="${ROOT}/build/linux-debug"

if [[ $# -ge 1 ]]; then
  SOURCE_CONFIG="$1"
elif [[ -f "${ROOT}/config/gateway-a.local.json" ]]; then
  SOURCE_CONFIG="${ROOT}/config/gateway-a.local.json"
elif [[ -f "${ROOT}/config/gateway.json" ]]; then
  SOURCE_CONFIG="${ROOT}/config/gateway.json"
else
  echo "ERROR: no local service config found."
  echo "Usage: $0 /absolute/path/to/gateway-local.json"
  exit 10
fi

[[ "${SOURCE_CONFIG}" = /* ]] || SOURCE_CONFIG="${ROOT}/${SOURCE_CONFIG}"
[[ -f "${SOURCE_CONFIG}" ]] || { echo "ERROR: config missing: ${SOURCE_CONFIG}"; exit 11; }

TS="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${HOME}/tmp/m19-real-ai-domain-e2e-${TS}"
STORAGE_ROOT="${ARTIFACT_DIR}/file-storage"
mkdir -p "${ARTIFACT_DIR}" "${STORAGE_ROOT}/objects"
chmod 700 "${ARTIFACT_DIR}"

ACTOR=990019001
PEER=990019002
GROUP_ID=990019101
DENIED_GROUP_ID=990019102
FILE_ID=990019201
DENIED_FILE_ID=990019202
MSG1=990019301
MSG2=990019302

USER_TARGET="127.0.0.1:55252"
SOCIAL_TARGET="127.0.0.1:55251"
MESSAGE_TARGET="127.0.0.1:55253"
GROUP_TARGET="127.0.0.1:55254"
FILE_TARGET="127.0.0.1:55255"
MCP_HOST="127.0.0.1"
MCP_PORT=18380
MCP_BASE="http://${MCP_HOST}:${MCP_PORT}"
TOKEN="tinyimx-m19-real-domain-e2e-token"

PIDS=()
SEEDED=0
MYSQL_READY=0

log(){ printf '[M19-REAL-DOMAIN] %s\n' "$*"; }
fail(){ log "FAIL: $*"; exit 1; }
require_cmd(){ command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"; }

for cmd in python3 curl mysql cmake sha256sum; do require_cmd "$cmd"; done

# Real provider selection. No model is downloaded by this script.
AI_ENDPOINT="${TINYIMX_REAL_AI_ENDPOINT:-}"
AI_MODEL="${TINYIMX_REAL_AI_MODEL:-}"
AI_API_KEY="${TINYIMX_REAL_AI_API_KEY:-${TINYIMX_AI_API_KEY:-}}"
AI_REQUEST_TIMEOUT_MS="${TINYIMX_REAL_AI_REQUEST_TIMEOUT_MS:-300000}"
AI_REQUEST_TIMEOUT_SEC="$(( (AI_REQUEST_TIMEOUT_MS + 999) / 1000 ))"

discover_openai_models(){
  local base="$1"
  local body status
  body="${ARTIFACT_DIR}/provider-models.json"
  status="$(curl -sS -o "${body}" -w '%{http_code}' \
    -H "Authorization: Bearer ${AI_API_KEY}" \
    "${base}/v1/models" 2>/dev/null || true)"
  [[ "${status}" == "200" ]] || return 1
  local found
  found="$(python3 - "${body}" <<'PY_MODELS'
import json,sys
try:
    x=json.load(open(sys.argv[1],encoding="utf-8"))
    data=x.get("data",[])
    if data and isinstance(data[0],dict):
        print(data[0].get("id",""))
except Exception:
    pass
PY_MODELS
)"
  [[ -n "${found}" ]] || return 1
  AI_ENDPOINT="${base}/v1/chat/completions"
  [[ -n "${AI_MODEL}" ]] || AI_MODEL="${found}"
  return 0
}

if [[ -z "${AI_ENDPOINT}" ]]; then
  for base in \
    "http://127.0.0.1:11434" \
    "http://127.0.0.1:8000" \
    "http://127.0.0.1:1234"
  do
    if discover_openai_models "${base}"; then
      break
    fi
  done
fi

# Ollama fallback for installations where /v1/models is unavailable.
if [[ -z "${AI_ENDPOINT}" ]]; then
  status="$(curl -sS -o "${ARTIFACT_DIR}/ollama-tags.json" -w '%{http_code}' \
    "http://127.0.0.1:11434/api/tags" 2>/dev/null || true)"
  if [[ "${status}" == "200" ]]; then
    found="$(python3 - "${ARTIFACT_DIR}/ollama-tags.json" <<'PY_TAGS'
import json,sys
try:
    x=json.load(open(sys.argv[1],encoding="utf-8"))
    models=x.get("models",[])
    if models:
        print(models[0].get("name",""))
except Exception:
    pass
PY_TAGS
)"
    if [[ -n "${found}" ]]; then
      AI_ENDPOINT="http://127.0.0.1:11434/v1/chat/completions"
      [[ -n "${AI_MODEL}" ]] || AI_MODEL="${found}"
    fi
  fi
fi

if [[ -z "${AI_ENDPOINT}" || -z "${AI_MODEL}" ]]; then
  echo "REAL_AI_PROVIDER_NOT_FOUND"
  echo "No model will be downloaded automatically."
  echo "Start an existing OpenAI-compatible provider, or export:"
  echo "  TINYIMX_REAL_AI_ENDPOINT=http://127.0.0.1:11434/v1/chat/completions"
  echo "  TINYIMX_REAL_AI_MODEL=<installed-model-name>"
  echo "Optional authenticated endpoint:"
  echo "  TINYIMX_REAL_AI_API_KEY=<key>"
  exit 12
fi

[[ "${AI_ENDPOINT}" == http://* ]] || fail \
  "M19 native HTTP client currently supports only http:// real-provider endpoints"

echo "REAL_AI_ENDPOINT=${AI_ENDPOINT}"
echo "REAL_AI_MODEL=${AI_MODEL}"
echo "REAL_AI_REQUEST_TIMEOUT_MS=${AI_REQUEST_TIMEOUT_MS}"

# Warm the real model before starting TinyIMX domain services. This separates
# provider cold-start/model-load latency from MCP/gRPC/MySQL failures.
WARMUP_REQUEST="${ARTIFACT_DIR}/provider-warmup.request.json"
WARMUP_RESPONSE="${ARTIFACT_DIR}/provider-warmup.response.json"

python3 - "${WARMUP_REQUEST}" "${AI_MODEL}" <<'PY_WARMUP_REQUEST'
import json,sys
dst,model=sys.argv[1:]
json.dump({
    "model": model,
    "messages": [{"role":"user","content":"Reply with exactly OK."}],
    "stream": False,
    "temperature": 0,
    "max_tokens": 8
}, open(dst,"w",encoding="utf-8"))
PY_WARMUP_REQUEST

AUTH_ARGS=()
if [[ -n "${AI_API_KEY}" ]]; then
  AUTH_ARGS=(-H "Authorization: Bearer ${AI_API_KEY}")
fi

echo "[M19-REAL-AI] provider warm-up / latency probe"
WARMUP_META="$(
  curl -sS \
    --connect-timeout 5 \
    --max-time "${AI_REQUEST_TIMEOUT_SEC}" \
    -o "${WARMUP_RESPONSE}" \
    -w '%{http_code} %{time_total}' \
    -X POST "${AI_ENDPOINT}" \
    -H 'Content-Type: application/json' \
    "${AUTH_ARGS[@]}" \
    --data-binary @"${WARMUP_REQUEST}" \
    || true
)"

WARMUP_HTTP="${WARMUP_META%% *}"
WARMUP_TIME="${WARMUP_META#* }"
echo "REAL_AI_WARMUP_HTTP=${WARMUP_HTTP}"
echo "REAL_AI_WARMUP_SECONDS=${WARMUP_TIME}"

[[ "${WARMUP_HTTP}" == "200" ]] || {
  echo "========== PROVIDER WARMUP RESPONSE =========="
  cat "${WARMUP_RESPONSE}" 2>/dev/null || true
  fail "real AI provider warm-up failed or exceeded ${AI_REQUEST_TIMEOUT_MS} ms"
}

python3 - "${WARMUP_RESPONSE}" <<'PY_WARMUP_CHECK'
import json,sys
x=json.load(open(sys.argv[1],encoding="utf-8"))
choices=x.get("choices")
assert isinstance(choices,list) and choices, f"missing choices: {x}"
msg=choices[0].get("message",{})
assert isinstance(msg,dict), f"missing message: {x}"
print("REAL_AI_PROVIDER_WARMUP=PASS")
PY_WARMUP_CHECK

cd "${ROOT}"

for f in \
  "${BUILD}/tinyimx_mcp_server" \
  "${BUILD}/user_service_demo" \
  "${BUILD}/social_service_demo" \
  "${BUILD}/message_service_demo" \
  "${BUILD}/group_service_demo" \
  "${BUILD}/file_service_demo" \
  "${BUILD}/tinyimx_ai_agent_demo"
do
  [[ -x "$f" ]] || fail "required binary missing: $f"
done

# Verify ports are free before launching anything.
python3 - 55251 55252 55253 55254 55255 "${MCP_PORT}" <<'PY'
import socket, sys
for text in sys.argv[1:]:
    port=int(text)
    s=socket.socket()
    try:
        s.bind(("127.0.0.1", port))
    except OSError as e:
        raise SystemExit(f"port already in use: {port}: {e}")
    finally:
        s.close()
print("PORT_PRECHECK=PASS")
PY

# Resolve MySQL credentials from the same local config used by the service processes.
mapfile -t MYSQL_FIELDS < <(python3 - "${SOURCE_CONFIG}" <<'PY'
import json,sys
x=json.load(open(sys.argv[1], encoding="utf-8"))
m=x["mysql"]
for k in ("host","port","database","user","password"):
    print(m.get(k,""))
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
  mysql_script >/dev/null 2>&1 <<SQL || true
SET FOREIGN_KEY_CHECKS=1;
DELETE FROM im_files WHERE file_id IN (${FILE_ID}, ${DENIED_FILE_ID});
DELETE FROM im_group_members WHERE group_id IN (${GROUP_ID}, ${DENIED_GROUP_ID});
DELETE FROM im_groups WHERE group_id IN (${GROUP_ID}, ${DENIED_GROUP_ID});
DELETE FROM im_private_messages WHERE message_id IN (${MSG1}, ${MSG2});
DELETE FROM im_user_relations
 WHERE user_id IN (${ACTOR}, ${PEER}) OR peer_user_id IN (${ACTOR}, ${PEER});
DELETE FROM im_users WHERE user_id IN (${ACTOR}, ${PEER});
SQL
}

cleanup(){
  rc=$?
  trap - EXIT INT TERM
  for ((i=${#PIDS[@]}-1; i>=0; --i)); do
    pid="${PIDS[$i]}"
    if kill -0 "$pid" 2>/dev/null; then
      kill -TERM "$pid" 2>/dev/null || true
    fi
  done
  for pid in "${PIDS[@]}"; do
    for _ in $(seq 1 40); do
      kill -0 "$pid" 2>/dev/null || break
      sleep 0.1
    done
    if kill -0 "$pid" 2>/dev/null; then
      kill -KILL "$pid" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null || true
  done
  cleanup_db

  # Verify only LISTEN sockets. TIME_WAIT must not be treated as a live service.
  python3 - 55251 55252 55253 55254 55255 "${MCP_PORT}" <<'PY' || true
import pathlib, sys

wanted={int(x) for x in sys.argv[1:]}
listening=set()

for procfile in ("/proc/net/tcp", "/proc/net/tcp6"):
    p=pathlib.Path(procfile)
    if not p.exists():
        continue
    for line in p.read_text().splitlines()[1:]:
        cols=line.split()
        if len(cols) < 4 or cols[3] != "0A":  # TCP_LISTEN
            continue
        local=cols[1]
        try:
            port=int(local.rsplit(":",1)[1],16)
        except Exception:
            continue
        if port in wanted:
            listening.add(port)

if listening:
    print("E2E_LISTEN_RELEASE=FAIL busy=" + ",".join(map(str,sorted(listening))))
else:
    print("E2E_LISTEN_RELEASE=PASS")
PY

  echo
  echo "========== ARTIFACTS =========="
  echo "ARTIFACT_DIR=${ARTIFACT_DIR}"
  echo "exit_code=${rc}"
  for f in "${ARTIFACT_DIR}"/*.stdout.log; do
    [[ -f "$f" ]] || continue
    echo "--- $(basename "$f") tail ---"
    tail -30 "$f" || true
  done
  exit "$rc"
}
trap cleanup EXIT INT TERM

log "MySQL connectivity precheck"
mysql_exec "SELECT 1;" >/dev/null
MYSQL_READY=1

log "verify retained M18 schema"
for table in im_users im_user_relations im_private_messages im_groups im_group_members im_files; do
  count="$(mysql_exec "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='${table}';")"
  [[ "$count" == "1" ]] || fail "required table missing: ${table}"
done

log "ensure fixture IDs are unused; never overwrite pre-existing business data"
existing="$(
mysql_exec "
SELECT
 (SELECT COUNT(*) FROM im_users WHERE user_id IN (${ACTOR},${PEER})) +
 (SELECT COUNT(*) FROM im_private_messages WHERE message_id IN (${MSG1},${MSG2})) +
 (SELECT COUNT(*) FROM im_groups WHERE group_id IN (${GROUP_ID},${DENIED_GROUP_ID})) +
 (SELECT COUNT(*) FROM im_files WHERE file_id IN (${FILE_ID},${DENIED_FILE_ID}));
"
)"
[[ "$existing" == "0" ]] || fail "reserved M19 fixture IDs already exist; refusing to overwrite"

FILE_CONTENT='TinyIMX M19 real MCP to gRPC file metadata E2E
'
printf '%s' "${FILE_CONTENT}" > "${STORAGE_ROOT}/objects/actor-file.txt"
FILE_SIZE="$(wc -c < "${STORAGE_ROOT}/objects/actor-file.txt" | tr -d ' ')"
FILE_SHA="$(sha256sum "${STORAGE_ROOT}/objects/actor-file.txt" | awk '{print $1}')"

log "seed minimal read-only domain fixtures"
mysql_script <<SQL
INSERT INTO im_users
(user_id, username, nickname, avatar_url, password_salt, password_hash, status)
VALUES
(${ACTOR}, 'm19e2e_actor', 'M19 Actor', 'actor.png', '', '', 1),
(${PEER},  'm19e2e_peer',  'M19 Peer',  'peer.png',  '', '', 1);

INSERT INTO im_user_relations (user_id, peer_user_id, relation_status)
VALUES
(${ACTOR}, ${PEER}, 1),
(${PEER}, ${ACTOR}, 1);

INSERT INTO im_private_messages
(message_id, client_message_id, from_user_id, to_user_id, message_type, content,
 delivery_status, created_at, delivered_at, read_at)
VALUES
(${MSG1}, 'm19-e2e-msg-1', ${ACTOR}, ${PEER}, 1, 'hello-from-actor', 1,
 CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, NULL),
(${MSG2}, 'm19-e2e-msg-2', ${PEER}, ${ACTOR}, 1, 'reply-from-peer', 1,
 CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, NULL);

INSERT INTO im_groups
(group_id, name, description, avatar_url, owner_user_id, status, join_policy,
 max_members, version, member_version)
VALUES
(${GROUP_ID}, 'm19-e2e-group', 'visible group', '', ${ACTOR}, 1, 1, 100, 1, 1),
(${DENIED_GROUP_ID}, 'm19-e2e-denied-group', 'peer-only group', '', ${PEER}, 1, 1, 100, 1, 1);

INSERT INTO im_group_members
(group_id, user_id, role, status, membership_epoch)
VALUES
(${GROUP_ID}, ${ACTOR}, 1, 1, 1),
(${GROUP_ID}, ${PEER}, 3, 1, 1),
(${DENIED_GROUP_ID}, ${PEER}, 1, 1, 1);

INSERT INTO im_files
(file_id, owner_user_id, file_name, content_type, total_size,
 checksum_algorithm, expected_checksum, verified_checksum,
 storage_backend, storage_key, status, version, available_at)
VALUES
(${FILE_ID}, ${ACTOR}, 'actor-file.txt', 'text/plain', ${FILE_SIZE},
 'sha256', '${FILE_SHA}', '${FILE_SHA}',
 'local_fs', 'objects/actor-file.txt', 3, 1, CURRENT_TIMESTAMP(3)),
(${DENIED_FILE_ID}, ${PEER}, 'peer-secret.txt', 'text/plain', 1,
 'sha256', '${FILE_SHA}', '${FILE_SHA}',
 'local_fs', 'objects/peer-secret.txt', 3, 1, CURRENT_TIMESTAMP(3));
SQL
SEEDED=1

# Generate process-specific configs from the known-good local config.
export M19_E2E_ARTIFACT_DIR="${ARTIFACT_DIR}"
export M19_E2E_ACTOR="${ACTOR}"
export M19_E2E_MCP_PORT="${MCP_PORT}"
python3 - "${SOURCE_CONFIG}" "${ARTIFACT_DIR}" <<'PY'
import copy,json,os,sys
src, outdir = sys.argv[1:]
base=json.load(open(src, encoding="utf-8"))

def isolate(c):
    for section in ("redis","rocketmq","outbox_relay","unread_projection","gateway_registry","zookeeper"):
        c.setdefault(section,{})["enable"]=False
    c.setdefault("service_discovery",{})["provider"]="static"
    c.setdefault("rpc",{})["enable"]=False
    c.setdefault("mcp",{})["enable"]=False
    c.setdefault("mysql",{})["enable"]=True
    c.setdefault("logger",{})["console"]=True
    c["logger"]["async"]=False

for name in ("user","social","message","group","file"):
    c=copy.deepcopy(base); isolate(c)
    c["logger"]["file"]=os.path.join(outdir, f"{name}.logger.log")
    with open(os.path.join(outdir,f"{name}.json"),"w",encoding="utf-8") as f:
        json.dump(c,f,indent=2)

c=copy.deepcopy(base); isolate(c)
c["mysql"]["enable"]=False
c["logger"]["file"]=os.path.join(outdir,"mcp.logger.log")
m=c.setdefault("mcp",{})
m.update({
    "enable": True,
    "endpoint": f"http://127.0.0.1:{os.environ['M19_E2E_MCP_PORT']}/mcp",
    "timeout_ms": 5000,
    "listen_host": "127.0.0.1",
    "listen_port": int(os.environ["M19_E2E_MCP_PORT"]),
    "endpoint_path": "/mcp",
    "io_threads": 1,
    "worker_threads": 4,
    "queue_capacity": 128,
    "max_request_bytes": 1048576,
    "allowed_origins": [],
    "auth_token_env": "TINYIMX_MCP_TOKEN",
    "static_user_id": int(os.environ["M19_E2E_ACTOR"]),
    "static_subject": "tinyimx:m19:real-domain-e2e"
})
with open(os.path.join(outdir,"mcp.json"),"w",encoding="utf-8") as f:
    json.dump(c,f,indent=2)
PY
chmod 600 "${ARTIFACT_DIR}"/*.json

AGENT_CONFIG="${ARTIFACT_DIR}/agent.json"
export M19_REAL_AI_ENDPOINT="${AI_ENDPOINT}"
export M19_REAL_AI_MODEL="${AI_MODEL}"
export M19_REAL_AI_TIMEOUT_MS="${AI_REQUEST_TIMEOUT_MS}"
python3 - "${AGENT_CONFIG}" "${MCP_PORT}" <<'PY_AGENT_CONFIG'
import json,os,sys
dst,mcp_port=sys.argv[1:]
cfg={
  "ai":{
    "provider":"openai_compatible",
    "endpoint":os.environ["M19_REAL_AI_ENDPOINT"],
    "api_key_env":"TINYIMX_AI_API_KEY",
    "connect_timeout_ms":3000,
    "request_timeout_ms":int(os.environ["M19_REAL_AI_TIMEOUT_MS"]),
    "max_response_bytes":4194304
  },
  "mcp":{
    "endpoint":f"http://127.0.0.1:{mcp_port}/mcp",
    "token_env":"TINYIMX_MCP_TOKEN",
    "client_name":"tinyimx-real-ai-domain-e2e",
    "client_version":"m19",
    "connect_timeout_ms":2000,
    "request_timeout_ms":10000,
    "max_response_bytes":4194304
  },
  "agent":{
    "model":os.environ["M19_REAL_AI_MODEL"],
    "system_prompt":(
      "You are the TinyIMX M19 acceptance agent. "
      "For this test you MUST use the requested TinyIMX read-only tools before answering. "
      "Never invent values. Preserve exact usernames, message content, group names, and file names "
      "returned by tools."
    ),
    "allowed_tools":[
      "tinyimx.user.get_self_profile",
      "tinyimx.social.list_friends",
      "tinyimx.message.list_conversations",
      "tinyimx.group.list_my_groups",
      "tinyimx.file.get_metadata"
    ],
    "max_tool_rounds":6,
    "max_tool_calls_per_round":8,
    "max_total_tool_calls":20,
    "repeated_identical_call_limit":3
  }
}
json.dump(cfg,open(dst,"w",encoding="utf-8"),indent=2)
PY_AGENT_CONFIG
chmod 600 "${AGENT_CONFIG}"

wait_ready(){
  local name="$1" pid="$2" log_file="$3" pattern="$4"
  for _ in $(seq 1 150); do
    if ! kill -0 "$pid" 2>/dev/null; then
      log "${name} exited before ready"
      tail -80 "$log_file" || true
      return 1
    fi
    if grep -qE "$pattern" "$log_file" 2>/dev/null; then
      log "${name}=READY"
      return 0
    fi
    sleep 0.1
  done
  log "${name} readiness timeout"
  tail -80 "$log_file" || true
  return 1
}

LAST_PID=""

start_proc(){
  local label="$1"; shift
  "$@" >"${ARTIFACT_DIR}/${label}.stdout.log" 2>&1 &
  LAST_PID=$!
  PIDS+=("${LAST_PID}")
  echo "[M19-REAL-DOMAIN] started ${label}, pid=${LAST_PID}" >&2
}

log "start five real gRPC domain services"
start_proc user "${BUILD}/user_service_demo" "${ARTIFACT_DIR}/user.json" "${USER_TARGET}"
USER_PID="${LAST_PID}"
wait_ready user "$USER_PID" "${ARTIFACT_DIR}/user.stdout.log" 'UserService ready'

start_proc social "${BUILD}/social_service_demo" "${ARTIFACT_DIR}/social.json" "${SOCIAL_TARGET}"
SOCIAL_PID="${LAST_PID}"
wait_ready social "$SOCIAL_PID" "${ARTIFACT_DIR}/social.stdout.log" 'SocialService ready'

start_proc group "${BUILD}/group_service_demo" "${ARTIFACT_DIR}/group.json" "${GROUP_TARGET}"
GROUP_PID="${LAST_PID}"
wait_ready group "$GROUP_PID" "${ARTIFACT_DIR}/group.stdout.log" 'GroupService ready'

export TINYIMX_GROUP_RPC_TARGET="${GROUP_TARGET}"
start_proc message "${BUILD}/message_service_demo" "${ARTIFACT_DIR}/message.json" "${MESSAGE_TARGET}"
MESSAGE_PID="${LAST_PID}"
wait_ready message "$MESSAGE_PID" "${ARTIFACT_DIR}/message.stdout.log" 'MessageService ready'

start_proc file "${BUILD}/file_service_demo" "${ARTIFACT_DIR}/file.json" "${FILE_TARGET}" "${STORAGE_ROOT}"
FILE_PID="${LAST_PID}"
wait_ready file "$FILE_PID" "${ARTIFACT_DIR}/file.stdout.log" 'FileService ready'

export TINYIMX_USER_RPC_TARGET="${USER_TARGET}"
export TINYIMX_SOCIAL_RPC_TARGET="${SOCIAL_TARGET}"
export TINYIMX_MESSAGE_RPC_TARGET="${MESSAGE_TARGET}"
export TINYIMX_GROUP_RPC_TARGET="${GROUP_TARGET}"
export TINYIMX_FILE_RPC_TARGET="${FILE_TARGET}"
export TINYIMX_MCP_TOKEN="${TOKEN}"

log "relink MCPServer and AI Agent against current M19 implementation"
cmake --build "${BUILD}" --target tinyimx_mcp_server -j1 >"${ARTIFACT_DIR}/mcp-build.log" 2>&1
cmake --build "${BUILD}" --target tinyimx_ai_agent_demo -j1 >"${ARTIFACT_DIR}/ai-agent-build.log" 2>&1

start_proc mcp "${BUILD}/tinyimx_mcp_server" "${ARTIFACT_DIR}/mcp.json"
MCP_PID="${LAST_PID}"
wait_ready mcp "$MCP_PID" "${ARTIFACT_DIR}/mcp.stdout.log" 'MCP server started'

# Wait for HTTP health.
for _ in $(seq 1 50); do
  code="$(curl -sS -o "${ARTIFACT_DIR}/health.json" -w '%{http_code}' "${MCP_BASE}/health" 2>/dev/null || true)"
  [[ "$code" == "200" ]] && break
  sleep 0.1
done
[[ "${code:-}" == "200" ]] || fail "MCP health did not become ready"

call_tool(){
  local id="$1" name="$2" args_json="$3" stem="$4"
  python3 - "${id}" "${name}" "${args_json}" "${ARTIFACT_DIR}/${stem}.request.json" <<'PY'
import json,sys
i=int(sys.argv[1]); name=sys.argv[2]; args=json.loads(sys.argv[3]); out=sys.argv[4]
req={
 "jsonrpc":"2.0","id":i,"method":"tools/call",
 "params":{
   "name":name,"arguments":args,
   "_meta":{
     "io.modelcontextprotocol/protocolVersion":"2026-07-28",
     "io.modelcontextprotocol/clientInfo":{"name":"tinyimx-m19-real-domain-e2e","version":"1"},
     "io.modelcontextprotocol/clientCapabilities":{}
   }
 }
}
json.dump(req,open(out,"w",encoding="utf-8"))
PY
  local status
  status="$(
    curl -sS -o "${ARTIFACT_DIR}/${stem}.response.json" -w '%{http_code}' \
      -X POST "${MCP_BASE}/mcp" \
      -H 'Content-Type: application/json' \
      -H 'MCP-Protocol-Version: 2026-07-28' \
      -H 'Mcp-Method: tools/call' \
      -H "Mcp-Name: ${name}" \
      -H "Authorization: Bearer ${TOKEN}" \
      -H "X-Request-Id: m19-real-${stem}" \
      --data-binary @"${ARTIFACT_DIR}/${stem}.request.json"
  )"
  [[ "$status" == "200" ]] || fail "${name} HTTP=${status}"
}

log "verify all eight domain tools are actually published"
cat >"${ARTIFACT_DIR}/list.request.json" <<'JSON'
{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientInfo":{"name":"tinyimx-m19-real-domain-e2e","version":"1"},"io.modelcontextprotocol/clientCapabilities":{}}}}
JSON
LIST_HTTP="$(
  curl -sS -o "${ARTIFACT_DIR}/list.response.json" -w '%{http_code}' \
    -X POST "${MCP_BASE}/mcp" \
    -H 'Content-Type: application/json' \
    -H 'MCP-Protocol-Version: 2026-07-28' \
    -H 'Mcp-Method: tools/list' \
    -H "Authorization: Bearer ${TOKEN}" \
    --data-binary @"${ARTIFACT_DIR}/list.request.json"
)"
[[ "$LIST_HTTP" == "200" ]] || fail "tools/list HTTP=${LIST_HTTP}"

python3 - "${ARTIFACT_DIR}/list.response.json" <<'PY'
import json,sys
x=json.load(open(sys.argv[1],encoding="utf-8"))
names={t["name"] for t in x["result"]["tools"]}
required={
"tinyimx.user.get_self_profile",
"tinyimx.social.list_friends",
"tinyimx.message.list_conversations",
"tinyimx.message.list_history",
"tinyimx.group.get",
"tinyimx.group.list_my_groups",
"tinyimx.group.list_members",
"tinyimx.file.get_metadata",
}
missing=sorted(required-names)
assert not missing, missing
print("REAL_DOMAIN_TOOL_DISCOVERY=PASS")
PY

call_tool 10 tinyimx.user.get_self_profile '{}' user_profile
call_tool 11 tinyimx.social.list_friends '{"limit":10}' friends
call_tool 12 tinyimx.message.list_conversations '{"limit":10}' conversations
call_tool 13 tinyimx.message.list_history "{\"peer_user_id\":${PEER},\"limit\":10}" history
call_tool 14 tinyimx.group.get "{\"group_id\":${GROUP_ID}}" group_get
call_tool 15 tinyimx.group.list_my_groups '{"limit":10}' my_groups
call_tool 16 tinyimx.group.list_members "{\"group_id\":${GROUP_ID},\"limit\":10}" group_members
call_tool 17 tinyimx.file.get_metadata "{\"file_id\":${FILE_ID}}" file_metadata

# Real domain ACL negative paths.
call_tool 18 tinyimx.group.get "{\"group_id\":${DENIED_GROUP_ID}}" denied_group
call_tool 19 tinyimx.file.get_metadata "{\"file_id\":${DENIED_FILE_ID}}" denied_file

python3 - "${ARTIFACT_DIR}" "${ACTOR}" "${PEER}" "${GROUP_ID}" "${FILE_ID}" "${MSG1}" "${MSG2}" "${FILE_SHA}" <<'PY'
import json,sys,pathlib
d=pathlib.Path(sys.argv[1])
actor,peer,group_id,file_id,msg1,msg2=map(int,sys.argv[2:8])
sha=sys.argv[8]

def response(stem):
    return json.load(open(d/f"{stem}.response.json",encoding="utf-8"))["result"]

def ok(stem):
    r=response(stem)
    assert r["isError"] is False, (stem,r)
    return r["structuredContent"]

p=ok("user_profile")["profile"]
assert p["user_id"]==actor and p["username"]=="m19e2e_actor"

friends=ok("friends")["friends"]
assert any(x["friend_user_id"]==peer and x["username"]=="m19e2e_peer" for x in friends)

convs=ok("conversations")["conversations"]
assert any(x["peer_user_id"]==peer and x["last_message_id"]==msg2 and x["last_content"]=="reply-from-peer" for x in convs)

msgs=ok("history")["messages"]
ids={x["message_id"] for x in msgs}
assert {msg1,msg2}.issubset(ids)
assert any(x["content"]=="hello-from-actor" for x in msgs)
assert any(x["content"]=="reply-from-peer" for x in msgs)

g=ok("group_get")["group"]
assert g["group_id"]==group_id and g["name"]=="m19-e2e-group"

groups=ok("my_groups")["groups"]
assert any(x["group_id"]==group_id for x in groups)

members=ok("group_members")["members"]
member_ids={x["user_id"] for x in members}
assert actor in member_ids and peer in member_ids

f=ok("file_metadata")["file"]
assert f["file_id"]==file_id
assert f["file_name"]=="actor-file.txt"
assert f["verified_checksum"]==sha

denied=response("denied_group")
assert denied["isError"] is True
assert denied["structuredContent"]["error"]["code"]=="permission_denied"

denied=response("denied_file")
assert denied["isError"] is True
# File boundary deliberately collapses nonexistent/not-owned into NOT_FOUND.
assert denied["structuredContent"]["error"]["code"]=="not_found"

print("REAL_DOMAIN_RESPONSE_CONTRACTS=PASS")
print("REAL_GROUP_ACL=PASS")
print("REAL_FILE_OWNER_ACL=PASS")
PY

log "verify direct database fixture remained read-only apart from seeded rows"
[[ "$(mysql_exec "SELECT COUNT(*) FROM im_users WHERE user_id IN (${ACTOR},${PEER});")" == "2" ]]
[[ "$(mysql_exec "SELECT COUNT(*) FROM im_private_messages WHERE message_id IN (${MSG1},${MSG2});")" == "2" ]]
[[ "$(mysql_exec "SELECT COUNT(*) FROM im_groups WHERE group_id IN (${GROUP_ID},${DENIED_GROUP_ID});")" == "2" ]]
[[ "$(mysql_exec "SELECT COUNT(*) FROM im_files WHERE file_id IN (${FILE_ID},${DENIED_FILE_ID});")" == "2" ]]

log "run REAL LLM -> Agent -> MCP -> five domain services -> MySQL"
export TINYIMX_AI_API_KEY="${AI_API_KEY}"
export TINYIMX_MCP_TOKEN="${TOKEN}"

REAL_PROMPT="This is an acceptance test. You MUST call all five of these tools before answering: tinyimx.user.get_self_profile, tinyimx.social.list_friends, tinyimx.message.list_conversations, tinyimx.group.list_my_groups, and tinyimx.file.get_metadata with file_id ${FILE_ID}. Use the returned data only. In the final answer include these five exact values with no substitutions: username, first friend username, latest conversation content, one group name, and file name."

"${BUILD}/tinyimx_ai_agent_demo" \
  "${AGENT_CONFIG}" \
  "${REAL_PROMPT}" \
  >"${ARTIFACT_DIR}/real-ai.stdout.log" \
  2>"${ARTIFACT_DIR}/real-ai.stderr.log" || {
    echo "========== REAL AI STDOUT =========="
    cat "${ARTIFACT_DIR}/real-ai.stdout.log" || true
    echo "========== REAL AI STDERR =========="
    cat "${ARTIFACT_DIR}/real-ai.stderr.log" || true
    fail "real AI agent invocation failed"
  }

echo "========== REAL AI ANSWER =========="
cat "${ARTIFACT_DIR}/real-ai.stdout.log"
echo
echo "========== REAL AI EXECUTION =========="
cat "${ARTIFACT_DIR}/real-ai.stderr.log"

python3 - "${ARTIFACT_DIR}/real-ai.stdout.log" "${ARTIFACT_DIR}/real-ai.stderr.log" <<'PY_REAL_AI'
import re,sys
answer=open(sys.argv[1],encoding="utf-8",errors="replace").read()
stderr=open(sys.argv[2],encoding="utf-8",errors="replace").read()

required=[
    "m19e2e_actor",
    "m19e2e_peer",
    "reply-from-peer",
    "m19-e2e-group",
    "actor-file.txt",
]
missing=[x for x in required if x not in answer]
assert not missing, f"real AI answer missing database-backed sentinel values: {missing}"

matches=re.findall(r"tool_calls=(\d+)",stderr)
assert matches, "AI agent did not report tool call count"
count=max(map(int,matches))
assert count >= 5, f"expected >=5 real tool calls, got {count}"

print(f"REAL_AI_TOOL_CALL_COUNT={count}")
print("REAL_AI_FIVE_DOMAIN_SENTINELS=PASS")
PY_REAL_AI

echo
echo "=================================================="
echo "M19_REAL_AI_MCP_GRPC_MYSQL_E2E=PASS"
echo "real_ai_provider=PASS"
echo "agent_orchestrator=PASS"
echo "mcp_client=PASS"
echo "five_domain_tool_calls=PASS"
echo "mysql_backed_sentinels=PASS"
echo "provider_endpoint=${AI_ENDPOINT}"
echo "provider_model=${AI_MODEL}"
echo "provider_request_timeout_ms=${AI_REQUEST_TIMEOUT_MS}"
echo "ARTIFACT_DIR=${ARTIFACT_DIR}"
echo "=================================================="

echo
echo "=================================================="
echo "M19_REAL_MCP_GRPC_DOMAIN_E2E=PASS"
echo "user_service=PASS"
echo "social_service=PASS"
echo "message_service=PASS"
echo "group_service=PASS"
echo "file_service=PASS"
echo "eight_domain_tools=PASS"
echo "group_acl=PASS"
echo "file_owner_acl=PASS"
echo "ARTIFACT_DIR=${ARTIFACT_DIR}"
echo "=================================================="
