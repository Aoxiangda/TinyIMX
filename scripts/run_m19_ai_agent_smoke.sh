#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${ROOT:-$HOME/projects/TinyIMX_publish}"
BUILD="${BUILD:-$ROOT/build/linux-debug}"
ARTIFACT_DIR="${ARTIFACT_DIR:-$HOME/tmp/m19-ai-agent-smoke-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$ARTIFACT_DIR"

MCP_PORT="${M19_SMOKE_MCP_PORT:-18280}"
AI_PORT="${M19_SMOKE_AI_PORT:-18281}"
TOKEN="tinyimx-m19-ai-smoke-token"
MCP_CONFIG="$ARTIFACT_DIR/mcp.json"
AGENT_CONFIG="$ARTIFACT_DIR/agent.json"
PIDS=()

cd "$ROOT"
for f in "$BUILD/tinyimx_mcp_server" "$BUILD/tinyimx_ai_agent_demo"; do
  [[ -x "$f" ]] || { echo "ERROR: binary missing: $f"; exit 10; }
done

python3 - "$ROOT/config/mcp_server.example.json" "$MCP_CONFIG" "$MCP_PORT" "$ARTIFACT_DIR/mcp.log" <<'PY_MCP_CFG'
import json,sys
src,dst,port,log=sys.argv[1:]
x=json.load(open(src,encoding="utf-8"))
for section in ("mysql","redis","rocketmq","outbox_relay","unread_projection","gateway_registry","zookeeper"):
    x.setdefault(section,{})["enable"]=False
x.setdefault("service_discovery",{})["provider"]="static"
x.setdefault("logger",{})["file"]=log
x["logger"]["console"]=True
m=x.setdefault("mcp",{})
m.update({"enable":True,"endpoint":f"http://127.0.0.1:{port}/mcp","listen_host":"127.0.0.1","listen_port":int(port),"endpoint_path":"/mcp","auth_token_env":"TINYIMX_MCP_TOKEN","static_user_id":42,"static_subject":"tinyimx:m19:ai-smoke","allowed_origins":[]})
json.dump(x,open(dst,"w",encoding="utf-8"),indent=2)
PY_MCP_CFG

python3 - "$AGENT_CONFIG" "$MCP_PORT" "$AI_PORT" <<'PY_AGENT_CFG'
import json,sys
dst,mcp_port,ai_port=sys.argv[1:]
x={
 "ai":{"provider":"openai_compatible","endpoint":f"http://127.0.0.1:{ai_port}/v1/chat/completions","api_key_env":"TINYIMX_AI_API_KEY","connect_timeout_ms":1000,"request_timeout_ms":5000,"max_response_bytes":1048576},
 "mcp":{"endpoint":f"http://127.0.0.1:{mcp_port}/mcp","token_env":"TINYIMX_MCP_TOKEN","client_name":"tinyimx-ai-smoke","client_version":"m19","connect_timeout_ms":1000,"request_timeout_ms":5000,"max_response_bytes":1048576},
 "agent":{"model":"fake-m19","system_prompt":"Use the echo tool.","allowed_tools":["tinyimx.system.echo"],"max_tool_rounds":4,"max_tool_calls_per_round":4,"max_total_tool_calls":8,"repeated_identical_call_limit":3}
}
json.dump(x,open(dst,"w",encoding="utf-8"),indent=2)
PY_AGENT_CFG
chmod 600 "$MCP_CONFIG" "$AGENT_CONFIG"

cleanup(){
  rc=$?
  trap - EXIT INT TERM
  for ((i=${#PIDS[@]}-1;i>=0;--i)); do kill -TERM "${PIDS[$i]}" 2>/dev/null || true; done
  for pid in "${PIDS[@]}"; do
    for _ in $(seq 1 30); do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
    kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  done
  echo "ARTIFACT_DIR=$ARTIFACT_DIR"
  echo "exit_code=$rc"
  exit "$rc"
}
trap cleanup EXIT INT TERM

export TINYIMX_MCP_TOKEN="$TOKEN"
export TINYIMX_AI_API_KEY="fake-key"

python3 "$ROOT/scripts/m19_fake_openai_provider.py" --host 127.0.0.1 --port "$AI_PORT" >"$ARTIFACT_DIR/fake-ai.stdout.log" 2>&1 &
PIDS+=("$!")
"$BUILD/tinyimx_mcp_server" "$MCP_CONFIG" >"$ARTIFACT_DIR/mcp.stdout.log" 2>&1 &
PIDS+=("$!")

for _ in $(seq 1 80); do
  ai_code="$(curl -sS -o /dev/null -w '%{http_code}' "http://127.0.0.1:$AI_PORT/health" 2>/dev/null || true)"
  mcp_code="$(curl -sS -o /dev/null -w '%{http_code}' "http://127.0.0.1:$MCP_PORT/health" 2>/dev/null || true)"
  [[ "$ai_code" == "200" && "$mcp_code" == "200" ]] && break
  sleep 0.1
done
[[ "${ai_code:-}" == "200" && "${mcp_code:-}" == "200" ]] || {
  echo "ERROR: smoke dependencies did not become healthy"
  tail -80 "$ARTIFACT_DIR/fake-ai.stdout.log" || true
  tail -80 "$ARTIFACT_DIR/mcp.stdout.log" || true
  exit 20
}

"$BUILD/tinyimx_ai_agent_demo" "$AGENT_CONFIG" "Please test the TinyIMX AI runtime." >"$ARTIFACT_DIR/agent.stdout.log" 2>"$ARTIFACT_DIR/agent.stderr.log"
cat "$ARTIFACT_DIR/agent.stdout.log"
grep -Fq 'AI_RUNTIME_SMOKE_OK echo=hello-ai-runtime principal=tinyimx:m19:ai-smoke' "$ARTIFACT_DIR/agent.stdout.log"
grep -Fq 'tool_calls=1' "$ARTIFACT_DIR/agent.stderr.log"

echo "M19_AI_AGENT_NATIVE_HTTP_SMOKE=PASS"
