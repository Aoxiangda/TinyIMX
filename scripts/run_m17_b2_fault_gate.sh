#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${TINYIMX_BUILD_DIR:-$ROOT_DIR/build/linux-debug}"
CONFIG_A="${TINYIMX_GATEWAY_A_CONFIG:-$ROOT_DIR/config/gateway-a.local.json}"
CONFIG_B="${TINYIMX_GATEWAY_B_CONFIG:-$ROOT_DIR/config/gateway-b.local.json}"
HOST="${TINYIMX_M17_B2_HOST:-127.0.0.1}"
GATEWAY_A_PORT="${TINYIMX_M17_B2_GATEWAY_A_PORT:-9001}"
GATEWAY_B_PORT="${TINYIMX_M17_B2_GATEWAY_B_PORT:-9002}"
USER_SERVICE_PORT="${TINYIMX_USER_SERVICE_PORT:-50052}"
MESSAGE_SERVICE_PORT="${TINYIMX_MESSAGE_SERVICE_PORT:-50053}"
GROUP_SERVICE_PORT="${TINYIMX_GROUP_SERVICE_PORT:-50054}"
USER_SERVICE_TARGET="127.0.0.1:${USER_SERVICE_PORT}"
MESSAGE_SERVICE_TARGET="127.0.0.1:${MESSAGE_SERVICE_PORT}"
GROUP_SERVICE_TARGET="127.0.0.1:${GROUP_SERVICE_PORT}"
PASSWORD="${TINYIMX_M17_B2_TCP_PASSWORD:-}"
SENDER_USERNAME="${TINYIMX_M17_B2_SENDER_USERNAME:-user10001}"
REMOTE_USERNAME="${TINYIMX_M17_B2_REMOTE_USERNAME:-user10003}"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${TINYIMX_M17_B2_FAULT_ARTIFACT_DIR:-$ROOT_DIR/artifacts/m17-b2-fault-$TIMESTAMP}"

USER_PID=""
GROUP_PID=""
MESSAGE_PID=""
GW_A_PID=""
GW_B_PID=""

mkdir -p "$ARTIFACT_DIR"

log() { printf '[M17-B2-FAULT] %s\n' "$*"; }
fail() { log "FAIL $*"; exit 1; }

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "required command missing: $1"
}

json_get() {
    python3 - "$1" "$2" "$3" <<'PY'
import json, sys
with open(sys.argv[1], encoding='utf-8') as f:
    data=json.load(f)
value=data[sys.argv[2]][sys.argv[3]]
print('1' if value is True else '0' if value is False else value)
PY
}

MYSQL_HOST="${TINYIMX_MYSQL_HOST:-$(json_get "$CONFIG_A" mysql host)}"
MYSQL_PORT="${TINYIMX_MYSQL_PORT:-$(json_get "$CONFIG_A" mysql port)}"
MYSQL_DATABASE="${TINYIMX_MYSQL_DATABASE:-$(json_get "$CONFIG_A" mysql database)}"
MYSQL_USER="${TINYIMX_MYSQL_USER:-$(json_get "$CONFIG_A" mysql user)}"
MYSQL_PASSWORD="${TINYIMX_MYSQL_PASSWORD:-$(json_get "$CONFIG_A" mysql password)}"
REDIS_HOST="${TINYIMX_REDIS_HOST:-$(json_get "$CONFIG_A" redis host)}"
REDIS_PORT="${TINYIMX_REDIS_PORT:-$(json_get "$CONFIG_A" redis port)}"
REDIS_DB="${TINYIMX_REDIS_DB:-$(json_get "$CONFIG_A" redis db)}"
REDIS_PASSWORD="${TINYIMX_REDIS_PASSWORD:-$(json_get "$CONFIG_A" redis password)}"

mysql_scalar() {
    MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp \
      -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" -D "$MYSQL_DATABASE" \
      --batch --skip-column-names -e "$1"
}

redis_args=(-h "$REDIS_HOST" -p "$REDIS_PORT" -n "$REDIS_DB" --raw)
if [[ -n "$REDIS_PASSWORD" ]]; then
    redis_args+=(-a "$REDIS_PASSWORD" --no-auth-warning)
fi
redis_exists() { redis-cli "${redis_args[@]}" EXISTS "$1"; }
redis_ttl() { redis-cli "${redis_args[@]}" TTL "$1"; }

port_open() {
    python3 - "$HOST" "$1" <<'PY'
import socket, sys
s=socket.socket(); s.settimeout(0.2)
try:
    s.connect((sys.argv[1], int(sys.argv[2])))
except OSError:
    sys.exit(1)
else:
    sys.exit(0)
finally:
    s.close()
PY
}

wait_port_open() {
    local port="$1"
    local timeout_seconds="${2:-15}"
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        port_open "$port" && return 0
        sleep 0.2
    done
    return 1
}

wait_port_closed() {
    local port="$1"
    local timeout_seconds="${2:-10}"
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        ! port_open "$port" && return 0
        sleep 0.2
    done
    return 1
}

stop_pid() {
    local pid="${1:-}" label="${2:-process}"
    [[ -z "$pid" ]] && return 0
    if kill -0 "$pid" 2>/dev/null; then
        kill -TERM "$pid" 2>/dev/null || true
        for _ in $(seq 1 50); do
            ! kill -0 "$pid" 2>/dev/null && break
            sleep 0.1
        done
        if kill -0 "$pid" 2>/dev/null; then
            log "$label did not stop on TERM; sending KILL during cleanup"
            kill -KILL "$pid" 2>/dev/null || true
        fi
    fi
    wait "$pid" 2>/dev/null || true
}

stop_gateways() {
    stop_pid "$GW_A_PID" gateway-a; GW_A_PID=""
    stop_pid "$GW_B_PID" gateway-b; GW_B_PID=""
    wait_port_closed "$GATEWAY_A_PORT" 10 || true
    wait_port_closed "$GATEWAY_B_PORT" 10 || true
}

cleanup() {
    set +e
    stop_gateways
    stop_pid "$MESSAGE_PID" message-service
    stop_pid "$GROUP_PID" group-service
    stop_pid "$USER_PID" user-service
}
trap cleanup EXIT INT TERM

assert_ports_free() {
    for port in "$USER_SERVICE_PORT" "$MESSAGE_SERVICE_PORT" "$GROUP_SERVICE_PORT" \
                "$GATEWAY_A_PORT" "$GATEWAY_B_PORT"; do
        if port_open "$port"; then
            fail "port $port already has a listener; stop the existing TinyIMX runtime before the fault gate"
        fi
    done
}

start_services() {
    log "starting current User/Group/Message services"
    "$BUILD_DIR/user_service_demo" "$CONFIG_A" >"$ARTIFACT_DIR/user-service.log" 2>&1 & USER_PID=$!
    wait_port_open "$USER_SERVICE_PORT" 15 || fail "UserService not ready"

    "$BUILD_DIR/group_service_demo" "$CONFIG_A" >"$ARTIFACT_DIR/group-service.log" 2>&1 & GROUP_PID=$!
    wait_port_open "$GROUP_SERVICE_PORT" 15 || fail "GroupService not ready"

    TINYIMX_GROUP_RPC_TARGET="$GROUP_SERVICE_TARGET" \
      "$BUILD_DIR/message_service_demo" "$CONFIG_A" >"$ARTIFACT_DIR/message-service.log" 2>&1 & MESSAGE_PID=$!
    wait_port_open "$MESSAGE_SERVICE_PORT" 15 || fail "MessageService not ready"
}

start_gateway_b() {
    local mode="$1" recovery_ms="$2"
    local log_file="$ARTIFACT_DIR/gateway-b-${mode}.log"
    local -a envs=(
        "TINYIMX_USER_RPC_TARGET=$USER_SERVICE_TARGET"
        "TINYIMX_MESSAGE_RPC_TARGET=$MESSAGE_SERVICE_TARGET"
        "TINYIMX_GROUP_RPC_TARGET=$GROUP_SERVICE_TARGET"
        "TINYIMX_GROUP_FANOUT_ENABLE=1"
        "TINYIMX_GROUP_FANOUT_ACK_RETRY_MS=1000"
        "TINYIMX_GROUP_FANOUT_FAILURE_RETRY_MS=500"
        "TINYIMX_GROUP_FANOUT_RECOVERY_MS=$recovery_ms"
    )
    if [[ "$mode" == "peer-loss" ]]; then
        envs+=("TINYIMX_FAULT_DROP_FIRST_GROUP_PEER_SUBMITTED_RESPONSE=1")
    fi
    log "starting gateway-b mode=$mode recovery_ms=$recovery_ms"
    env "${envs[@]}" "$BUILD_DIR/gateway_demo" "$CONFIG_B" >"$log_file" 2>&1 &
    GW_B_PID=$!
    wait_port_open "$GATEWAY_B_PORT" 15 || { tail -n 120 "$log_file" || true; fail "gateway-b not ready"; }
}

start_gateway_a() {
    local mode="$1" recovery_ms="$2" lease_ms="$3"
    local log_file="$ARTIFACT_DIR/gateway-a-${mode}.log"
    local -a envs=(
        "TINYIMX_USER_RPC_TARGET=$USER_SERVICE_TARGET"
        "TINYIMX_MESSAGE_RPC_TARGET=$MESSAGE_SERVICE_TARGET"
        "TINYIMX_GROUP_RPC_TARGET=$GROUP_SERVICE_TARGET"
        "TINYIMX_GROUP_FANOUT_ENABLE=1"
        "TINYIMX_GROUP_FANOUT_ACK_RETRY_MS=1000"
        "TINYIMX_GROUP_FANOUT_FAILURE_RETRY_MS=500"
        "TINYIMX_GROUP_FANOUT_RECOVERY_MS=$recovery_ms"
        "TINYIMX_GROUP_FANOUT_LEASE_MS=$lease_ms"
    )
    if [[ "$mode" == "crash-pause" ]]; then
        envs+=("TINYIMX_FAULT_GROUP_FANOUT_PAUSE_AFTER_CLAIM_MS=30000")
    fi
    log "starting gateway-a mode=$mode recovery_ms=$recovery_ms lease_ms=$lease_ms"
    env "${envs[@]}" "$BUILD_DIR/gateway_demo" "$CONFIG_A" >"$log_file" 2>&1 &
    GW_A_PID=$!
    wait_port_open "$GATEWAY_A_PORT" 15 || { tail -n 120 "$log_file" || true; fail "gateway-a not ready"; }
}

wait_discovery() { sleep 4; }

assert_no_due_pending_group_deliveries() {
    local count
    count="$(mysql_scalar "SELECT COUNT(*) FROM im_group_message_deliveries WHERE delivery_status=1 AND next_retry_at<=NOW(3) AND (lease_until IS NULL OR lease_until<=NOW(3));")"
    [[ "$count" == "0" ]] || {
        mysql_scalar "SELECT message_id,recipient_user_id,attempt_count,COALESCE(lease_owner,''),COALESCE(lease_until,''),next_retry_at FROM im_group_message_deliveries WHERE delivery_status=1 AND next_retry_at<=NOW(3) AND (lease_until IS NULL OR lease_until<=NOW(3)) ORDER BY message_id,recipient_user_id LIMIT 20;" >&2 || true
        fail "fault gate requires no pre-existing due PENDING group deliveries; found $count"
    }
}

parse_fixture() {
    local log_file="$1" key="$2"
    sed -n "s/.*${key}=\\([^ ]*\\).*/\\1/p" "$log_file" | tail -n1
}

assert_durable_identity_final() {
    local message_id="$1" recipient="$2" min_attempts="$3"
    local row_count status attempts message_count outbox_count
    row_count="$(mysql_scalar "SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id=${message_id} AND recipient_user_id=${recipient};")"
    status="$(mysql_scalar "SELECT delivery_status FROM im_group_message_deliveries WHERE message_id=${message_id} AND recipient_user_id=${recipient} LIMIT 1;")"
    attempts="$(mysql_scalar "SELECT attempt_count FROM im_group_message_deliveries WHERE message_id=${message_id} AND recipient_user_id=${recipient} LIMIT 1;")"
    message_count="$(mysql_scalar "SELECT COUNT(*) FROM im_group_messages WHERE message_id=${message_id};")"
    outbox_count="$(mysql_scalar "SELECT COUNT(*) FROM im_event_outbox WHERE event_id='group_message.created.v1:${message_id}';")"
    [[ "$row_count" == "1" ]] || fail "message_id=$message_id recipient=$recipient durable row count=$row_count"
    [[ "$status" == "3" ]] || fail "message_id=$message_id recipient=$recipient final delivery_status=$status expected=3"
    (( attempts >= min_attempts )) || fail "message_id=$message_id attempt_count=$attempts expected>=$min_attempts"
    [[ "$message_count" == "1" ]] || fail "message_id=$message_id group message count=$message_count"
    [[ "$outbox_count" == "1" ]] || fail "message_id=$message_id outbox count=$outbox_count"
}

wait_gateway_registry_lease_expired() {
    local gateway_id="$1"
    local timeout_seconds="${2:-30}"
    local key="tinyimx:gateway:registry:${gateway_id}"
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        local exists ttl
        exists="$(redis_exists "$key" 2>/dev/null || true)"
        [[ "$exists" == "0" ]] && return 0
        ttl="$(redis_ttl "$key" 2>/dev/null || true)"
        [[ "$ttl" == "-2" ]] && return 0
        [[ "$ttl" == "-1" ]] && return 1
        sleep 0.25
    done
    return 1
}

wait_fault_pause_for_message() {
    local message_id="$1" log_file="$2" deadline=$((SECONDS+12))
    while (( SECONDS < deadline )); do
        if grep -q "group fanout fault pause after durable claim.*message_id=${message_id}" "$log_file" 2>/dev/null; then
            return 0
        fi
        sleep 0.05
    done
    return 1
}

wait_fixture_line() {
    local log_file="$1" deadline=$((SECONDS+12))
    while (( SECONDS < deadline )); do
        grep -q '^M17_B2_FAULT_FIXTURE ' "$log_file" 2>/dev/null && return 0
        sleep 0.05
    done
    return 1
}

wait_pid_exit() {
    local pid="$1"
    local timeout_seconds="$2"
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        ! kill -0 "$pid" 2>/dev/null && return 0
        sleep 0.1
    done
    return 1
}

run_peer_response_loss_gate() {
    log "peer-response-loss gate: remote executes, first successful peer response is dropped"
    assert_no_due_pending_group_deliveries
    start_gateway_b peer-loss 20000
    sleep 1
    start_gateway_a normal 100 5000
    wait_discovery

    local client_log="$ARTIFACT_DIR/peer-response-loss-client.log"
    if ! "$BUILD_DIR/gateway_group_fanout_fault_e2e_client" \
        peer-loss "$HOST" "$GATEWAY_A_PORT" "$HOST" "$GATEWAY_B_PORT" \
        "$SENDER_USERNAME" "$REMOTE_USERNAME" "$PASSWORD" >"$client_log" 2>&1; then
        tail -n 160 "$client_log" || true
        fail "peer-response-loss client failed"
    fi

    local mid recipient
    mid="$(parse_fixture "$client_log" message_id)"
    recipient="$(parse_fixture "$client_log" recipient_user_id)"
    [[ -n "$mid" && -n "$recipient" ]] || fail "could not parse peer-loss durable identity"

    grep -q "gateway group peer response dropped by fault injection.*message_id=${mid}" \
        "$ARTIFACT_DIR/gateway-b-peer-loss.log" || {
        tail -n 180 "$ARTIFACT_DIR/gateway-b-peer-loss.log" || true
        fail "group peer response-loss marker missing for message_id=$mid"
    }
    assert_durable_identity_final "$mid" "$recipient" 2
    log "PASS peer-response-loss message_id=$mid recipient=$recipient"
    stop_gateways
}

run_crashed_coordinator_lease_takeover_gate() {
    log "coordinator-crash gate: committed lease survives SIGKILL and another gateway reclaims after expiry"
    assert_no_due_pending_group_deliveries

    # B runs a real coordinator but sleeps long enough after its initial empty
    # scan for A to claim the freshly-created row first. After A is SIGKILLed,
    # B's next recovery scan occurs after A's durable lease expiry.
    start_gateway_b normal 20000
    sleep 1
    start_gateway_a crash-pause 100 8000
    wait_discovery

    local client_log="$ARTIFACT_DIR/crash-claim-client.log"
    "$BUILD_DIR/gateway_group_fanout_fault_e2e_client" \
        crash-claim "$HOST" "$GATEWAY_A_PORT" "$HOST" "$GATEWAY_B_PORT" \
        "$SENDER_USERNAME" "$REMOTE_USERNAME" "$PASSWORD" >"$client_log" 2>&1 &
    local client_pid=$!

    wait_fixture_line "$client_log" || { tail -n 120 "$client_log" || true; fail "crash fixture not created"; }
    local mid recipient
    mid="$(parse_fixture "$client_log" message_id)"
    recipient="$(parse_fixture "$client_log" recipient_user_id)"
    [[ -n "$mid" && -n "$recipient" ]] || fail "could not parse crash durable identity"

    wait_fault_pause_for_message "$mid" "$ARTIFACT_DIR/gateway-a-crash-pause.log" || {
        tail -n 180 "$ARTIFACT_DIR/gateway-a-crash-pause.log" || true
        fail "gateway-a did not enter deterministic post-claim crash window for message_id=$mid"
    }

    local before
    before="$(mysql_scalar "SELECT CONCAT(delivery_status,'|',attempt_count,'|',COALESCE(lease_owner,''),'|',IF(lease_until>NOW(3),1,0)) FROM im_group_message_deliveries WHERE message_id=${mid} AND recipient_user_id=${recipient} LIMIT 1;")"
    [[ "$before" == "1|1|gateway-a|1" ]] || fail "unexpected durable lease before crash: $before"

    local crashed_pid="$GW_A_PID"
    log "SIGKILL gateway-a pid=$crashed_pid while durable lease is committed"
    kill -KILL "$crashed_pid"
    wait "$crashed_pid" 2>/dev/null || true
    GW_A_PID=""
    wait_port_closed "$GATEWAY_A_PORT" 10 || fail "gateway-a port stayed open after SIGKILL"

    local after_kill
    after_kill="$(mysql_scalar "SELECT CONCAT(delivery_status,'|',attempt_count,'|',COALESCE(lease_owner,''),'|',IF(lease_until IS NOT NULL,1,0)) FROM im_group_message_deliveries WHERE message_id=${mid} AND recipient_user_id=${recipient} LIMIT 1;")"
    [[ "$after_kill" == "1|1|gateway-a|1" ]] || fail "crash lost or rewrote durable lease: $after_kill"

    wait_gateway_registry_lease_expired gateway-a 30 || fail "crashed gateway-a Redis registry lease did not expire"

    local deadline=$((SECONDS+15))
    while (( SECONDS < deadline )); do
        local expired
        expired="$(mysql_scalar "SELECT IF(lease_until IS NULL OR lease_until<=NOW(3),1,0) FROM im_group_message_deliveries WHERE message_id=${mid} AND recipient_user_id=${recipient} LIMIT 1;")"
        [[ "$expired" == "1" ]] && break
        sleep 0.2
    done

    if ! wait_pid_exit "$client_pid" 35; then
        tail -n 160 "$client_log" || true
        fail "receiver did not observe takeover delivery after crashed lease expiry"
    fi
    if ! wait "$client_pid"; then
        tail -n 160 "$client_log" || true
        fail "crash-claim client failed"
    fi

    assert_durable_identity_final "$mid" "$recipient" 2
    local final_lease
    final_lease="$(mysql_scalar "SELECT CONCAT(COALESCE(lease_owner,''),'|',COALESCE(lease_token,''),'|',IF(lease_until IS NULL,1,0)) FROM im_group_message_deliveries WHERE message_id=${mid} AND recipient_user_id=${recipient} LIMIT 1;")"
    [[ "$final_lease" == "||1" ]] || fail "final delivery retained stale lease metadata: $final_lease"
    log "PASS coordinator-crash-lease-takeover message_id=$mid recipient=$recipient"
}

require_cmd python3
require_cmd mysql
require_cmd redis-cli
[[ -n "$PASSWORD" ]] || fail "TINYIMX_M17_B2_TCP_PASSWORD is required"
[[ "$BUILD_JOBS" == "1" ]] || fail "M17 VM fault gate requires TINYIMX_BUILD_JOBS=1"

assert_ports_free

log "static fault-seam hard gate"
grep -q 'TINYIMX_FAULT_DROP_FIRST_GROUP_PEER_SUBMITTED_RESPONSE' "$ROOT_DIR/gateway/GatewayServer.cpp" || fail "group peer response-loss seam missing"
grep -q 'TINYIMX_FAULT_GROUP_FANOUT_PAUSE_AFTER_CLAIM_MS' "$ROOT_DIR/examples/gateway_demo.cpp" || fail "post-claim crash-window seam missing"
grep -q 'fault_pause_after_claim' "$ROOT_DIR/gateway/GroupFanoutCoordinator.cpp" || fail "coordinator crash-window pause missing"

log "configure/build fault targets (-j1)"
(cd "$ROOT_DIR" && cmake --preset linux-debug >"$ARTIFACT_DIR/configure.log" 2>&1)
(cd "$ROOT_DIR" && cmake --build "$BUILD_DIR" --target \
    user_service_demo group_service_demo message_service_demo gateway_demo \
    gateway_group_fanout_fault_e2e_client gateway_tests \
    -j1 >"$ARTIFACT_DIR/build.log" 2>&1) || {
    tail -n 180 "$ARTIFACT_DIR/build.log" || true
    fail "fault-gate build failed"
}

log "apply idempotent M17 migrations"
MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp -h "$MYSQL_HOST" -P "$MYSQL_PORT" \
  -u "$MYSQL_USER" -D "$MYSQL_DATABASE" < "$ROOT_DIR/db/migrations/007_create_group_message_domain.sql"
MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp -h "$MYSQL_HOST" -P "$MYSQL_PORT" \
  -u "$MYSQL_USER" -D "$MYSQL_DATABASE" < "$ROOT_DIR/db/migrations/008_create_group_message_delivery.sql"

start_services
run_peer_response_loss_gate
run_crashed_coordinator_lease_takeover_gate

log "focused regression after real faults"
ctest --test-dir "$BUILD_DIR" --output-on-failure \
  -R '^(rpc_contract_tests|tinyimx\.protocol|message_application_service_tests|message_service_integration_tests|group_service_integration_tests|group_rpc_client_tests|tinyimx\.gateway)$' \
  >"$ARTIFACT_DIR/ctest-focused.log" 2>&1 || {
    tail -n 180 "$ARTIFACT_DIR/ctest-focused.log" || true
    fail "focused regression failed after fault tests"
}

git -C "$ROOT_DIR" diff --check >"$ARTIFACT_DIR/git-diff-check.log"
find "$ROOT_DIR" -type f \( -name '*.rej' -o -name '*.orig' \) -print >"$ARTIFACT_DIR/reject-files.log"
[[ ! -s "$ARTIFACT_DIR/reject-files.log" ]] || fail "reject/orig files remain in source tree"

printf '\n[M17-B2 FAULT PASS] Real peer-response-loss and crashed-coordinator lease takeover accepted.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
printf 'NOTE: run retained M12/M15/M16/M17-A/B1 regression gates before declaring M17-B2 CLOSED.\n'
