#!/usr/bin/env bash
set -Eeuo pipefail

# TinyIMX M12 Reliable Messaging - final release regression gate.
#
# Scope:
#   1) build + unit tests
#   2) same-gateway reliable messaging
#   3) offline/restart/pagination recovery
#   4) peer duplicate + durable replay
#   5) cross-gateway reliability
#   6) peer-response-loss fault injection
#   7) Gateway B SIGKILL/restart recovery
#   8) final semantic scan
#
# This script intentionally does NOT modify MySQL/Redis data by DELETE/FLUSH.
# Test fixtures are created through TinyIMX production paths and converged by ACK.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${TINYIMX_BUILD_DIR:-$ROOT_DIR/build/linux-debug}"
CONFIG_A="${TINYIMX_GATEWAY_A_CONFIG:-$ROOT_DIR/config/gateway-a.local.json}"
CONFIG_B="${TINYIMX_GATEWAY_B_CONFIG:-$ROOT_DIR/config/gateway-b.local.json}"
HOST="${TINYIMX_REGRESSION_HOST:-127.0.0.1}"
GATEWAY_A_PORT="${TINYIMX_GATEWAY_A_PORT:-9001}"
GATEWAY_B_PORT="${TINYIMX_GATEWAY_B_PORT:-9002}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${TINYIMX_M12_ARTIFACT_DIR:-$ROOT_DIR/artifacts/m12-regression-$TIMESTAMP}"
SUMMARY_FILE="$ARTIFACT_DIR/summary.tsv"

GW_A_PID=""
GW_B_PID=""
LAST_CASE_LOG=""
PASS_COUNT=0
FAIL_COUNT=0

mkdir -p "$ARTIFACT_DIR"
printf 'case\tstatus\tlog\n' > "$SUMMARY_FILE"

log() {
    printf '[M12] %s\n' "$*"
}

fail() {
    log "FAIL: $*"
    exit 1
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

json_get() {
    local file="$1"
    local section="$2"
    local key="$3"

    python3 - "$file" "$section" "$key" <<'PY'
import json, sys
with open(sys.argv[1], 'r', encoding='utf-8') as f:
    data = json.load(f)
value = data[sys.argv[2]][sys.argv[3]]
if isinstance(value, bool):
    print('1' if value else '0')
else:
    print(value)
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

port_open() {
    python3 - "$HOST" "$1" <<'PY'
import socket, sys
s = socket.socket()
s.settimeout(0.2)
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
        if port_open "$port"; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

wait_port_closed() {
    local port="$1"
    local timeout_seconds="${2:-10}"
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        if ! port_open "$port"; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

stop_pid() {
    local pid="${1:-}"
    local label="${2:-process}"
    [[ -z "$pid" ]] && return 0

    if kill -0 "$pid" 2>/dev/null; then
        kill -TERM "$pid" 2>/dev/null || true
        for _ in $(seq 1 30); do
            if ! kill -0 "$pid" 2>/dev/null; then
                break
            fi
            sleep 0.1
        done
        if kill -0 "$pid" 2>/dev/null; then
            log "$label did not stop on TERM; sending KILL"
            kill -KILL "$pid" 2>/dev/null || true
        fi
    fi
    wait "$pid" 2>/dev/null || true
}

cleanup() {
    set +e
    stop_pid "$GW_B_PID" "gateway-b"
    stop_pid "$GW_A_PID" "gateway-a"
}
trap cleanup EXIT INT TERM

assert_ports_free() {
    if port_open "$GATEWAY_A_PORT"; then
        fail "port $GATEWAY_A_PORT already has a listener; stop the existing gateway-a first"
    fi
    if port_open "$GATEWAY_B_PORT"; then
        fail "port $GATEWAY_B_PORT already has a listener; stop the existing gateway-b first"
    fi
}

start_gateway_a() {
    local log_file="$ARTIFACT_DIR/gateway-a.log"
    log "starting gateway-a -> $log_file"
    "$BUILD_DIR/gateway_demo" "$CONFIG_A" >"$log_file" 2>&1 &
    GW_A_PID=$!
    wait_port_open "$GATEWAY_A_PORT" 15 || {
        tail -n 100 "$log_file" || true
        fail "gateway-a did not become ready"
    }
}

start_gateway_b() {
    local mode="${1:-normal}"
    local log_file="$ARTIFACT_DIR/gateway-b-${mode}-$(date +%H%M%S).log"
    log "starting gateway-b mode=$mode -> $log_file"
    if [[ "$mode" == "drop-first-peer-response" ]]; then
        TINYIMX_FAULT_DROP_FIRST_PEER_DELIVERED_RESPONSE=1 \
            "$BUILD_DIR/gateway_demo" "$CONFIG_B" >"$log_file" 2>&1 &
    else
        "$BUILD_DIR/gateway_demo" "$CONFIG_B" >"$log_file" 2>&1 &
    fi
    GW_B_PID=$!
    wait_port_open "$GATEWAY_B_PORT" 15 || {
        tail -n 100 "$log_file" || true
        fail "gateway-b did not become ready"
    }
}

stop_gateway_a() {
    stop_pid "$GW_A_PID" "gateway-a"
    GW_A_PID=""
    wait_port_closed "$GATEWAY_A_PORT" 10 || fail "gateway-a port did not close"
}

stop_gateway_b() {
    stop_pid "$GW_B_PID" "gateway-b"
    GW_B_PID=""
    wait_port_closed "$GATEWAY_B_PORT" 10 || fail "gateway-b port did not close"
}

record_result() {
    local name="$1"
    local status="$2"
    local log_file="$3"
    printf '%s\t%s\t%s\n' "$name" "$status" "$log_file" >> "$SUMMARY_FILE"
    if [[ "$status" == "PASS" ]]; then
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

run_case() {
    local name="$1"
    shift
    local log_file="$ARTIFACT_DIR/${name}.log"
    LAST_CASE_LOG="$log_file"
    log "RUN $name"
    if "$@" >"$log_file" 2>&1; then
        record_result "$name" PASS "$log_file"
        log "PASS $name"
        return 0
    fi

    record_result "$name" FAIL "$log_file"
    log "FAIL $name"
    tail -n 120 "$log_file" || true
    return 1
}

mysql_scalar() {
    local sql="$1"
    MYSQL_PWD="$MYSQL_PASSWORD" mysql \
        --protocol=tcp \
        -h "$MYSQL_HOST" \
        -P "$MYSQL_PORT" \
        -u "$MYSQL_USER" \
        -D "$MYSQL_DATABASE" \
        --batch --skip-column-names \
        -e "$sql"
}

assert_delivery_status() {
    local message_id="$1"
    local expected="$2"
    local actual
    actual="$(mysql_scalar "SELECT delivery_status FROM im_private_messages WHERE message_id=${message_id} LIMIT 1;")"
    [[ "$actual" == "$expected" ]] || fail "message_id=$message_id delivery_status expected=$expected actual=$actual"
}

redis_get() {
    local key="$1"
    local args=(-h "$REDIS_HOST" -p "$REDIS_PORT" -n "$REDIS_DB" --raw)
    if [[ -n "$REDIS_PASSWORD" ]]; then
        args+=(-a "$REDIS_PASSWORD" --no-auth-warning)
    fi
    redis-cli "${args[@]}" GET "$key"
}

redis_exists() {
    local key="$1"
    local args=(-h "$REDIS_HOST" -p "$REDIS_PORT" -n "$REDIS_DB" --raw)
    if [[ -n "$REDIS_PASSWORD" ]]; then
        args+=(-a "$REDIS_PASSWORD" --no-auth-warning)
    fi
    redis-cli "${args[@]}" EXISTS "$key"
}

redis_ttl() {
    local key="$1"
    local args=(-h "$REDIS_HOST" -p "$REDIS_PORT" -n "$REDIS_DB" --raw)
    if [[ -n "$REDIS_PASSWORD" ]]; then
        args+=(-a "$REDIS_PASSWORD" --no-auth-warning)
    fi
    redis-cli "${args[@]}" TTL "$key"
}

wait_gateway_registry_lease_expired() {
    local gateway_id="$1"
    local timeout_seconds="${2:-30}"
    local key="tinyimx:gateway:registry:${gateway_id}"
    local deadline=$((SECONDS + timeout_seconds))
    local exists ttl last_ttl=""

    while (( SECONDS < deadline )); do
        exists="$(redis_exists "$key" 2>/dev/null || true)"

        if [[ "$exists" == "0" ]]; then
            log "registry lease expired after crash, gateway_id=$gateway_id"
            return 0
        fi

        ttl="$(redis_ttl "$key" 2>/dev/null || true)"

        # Redis TTL semantics:
        #   -2 => key no longer exists
        #   -1 => key exists but has no expiry (invalid for our lease model)
        if [[ "$ttl" == "-2" ]]; then
            log "registry lease expired after crash, gateway_id=$gateway_id"
            return 0
        fi

        if [[ "$ttl" == "-1" ]]; then
            log "registry lease lost its TTL unexpectedly, gateway_id=$gateway_id key=$key"
            return 1
        fi

        if [[ -n "$ttl" && "$ttl" != "$last_ttl" ]]; then
            log "waiting for crashed gateway lease fencing window, gateway_id=$gateway_id ttl_seconds=$ttl"
            last_ttl="$ttl"
        fi

        sleep 0.25
    done

    ttl="$(redis_ttl "$key" 2>/dev/null || true)"
    log "registry lease did not expire before timeout, gateway_id=$gateway_id ttl_seconds=${ttl:-unknown}"
    return 1
}

wait_gateway_a_lease_token() {
    local key="tinyimx:gateway:registry:gateway-a"
    local raw token
    for _ in $(seq 1 50); do
        raw="$(redis_get "$key" 2>/dev/null || true)"
        if [[ -n "$raw" ]]; then
            token="$(python3 -c 'import json,sys; d=json.loads(sys.stdin.read()); print(d.get("lease_token", ""))' <<<"$raw" 2>/dev/null || true)"
            if [[ -n "$token" ]]; then
                printf '%s\n' "$token"
                return 0
            fi
        fi
        sleep 0.2
    done
    return 1
}

wait_discovery_settle() {
    # Current config refresh interval is 3 seconds. Keep one full refresh margin.
    sleep 4
}

build_all() {
    log "configuring linux-debug preset"
    (cd "$ROOT_DIR" && cmake --preset linux-debug >"$ARTIFACT_DIR/cmake-configure.log" 2>&1)

    local targets=(
        concurrency_tests
        net_tests
        protocol_tests
        gateway_tests
        gateway_demo
        gateway_session_client_demo
        gateway_same_gateway_client_demo
        gateway_client_retry_demo
        gateway_client_ack_loss_demo
        gateway_client_retry_exhaustion_demo
        gateway_offline_client_demo
        gateway_offline_idempotency_client_demo
        gateway_offline_recovery_client_demo
        gateway_receiver_reconnect_dedup_demo
        gateway_peer_duplicate_client_demo
        gateway_peer_durable_replay_client_demo
        gateway_cross_gateway_client_demo
        gateway_cross_gateway_reconnect_dedup_demo
        gateway_cross_gateway_gateway_b_crash_recovery_demo
        gateway_cross_gateway_peer_response_loss_demo
        gateway_pending_replay_pagination_demo
    )

    log "building M12 release targets"
    (cd "$ROOT_DIR" && cmake --build --preset build-debug --target "${targets[@]}" -j"$(nproc)" \
        >"$ARTIFACT_DIR/build.log" 2>&1) || {
        tail -n 160 "$ARTIFACT_DIR/build.log" || true
        fail "M12 build gate failed"
    }
    record_result build PASS "$ARTIFACT_DIR/build.log"
}

run_unit_tests() {
    run_case unit-ctest \
        ctest --test-dir "$BUILD_DIR" --output-on-failure \
        -R '^tinyimx\.(concurrency|net|protocol|gateway)$'
}

run_single_gateway_suite() {
    start_gateway_a

    run_case pagination-105 \
        "$BUILD_DIR/gateway_pending_replay_pagination_demo" "$HOST" "$GATEWAY_A_PORT"

    local first_id last_id pending_after
    first_id="$(sed -n 's/.*first_message_id=\([0-9][0-9]*\).*/\1/p' "$LAST_CASE_LOG" | tail -n1)"
    last_id="$(sed -n 's/.*last_message_id=\([0-9][0-9]*\).*/\1/p' "$LAST_CASE_LOG" | tail -n1)"
    [[ -n "$first_id" && -n "$last_id" ]] || fail "could not parse pagination message-id range"
    pending_after="$(mysql_scalar "SELECT COUNT(*) FROM im_private_messages WHERE message_id BETWEEN ${first_id} AND ${last_id} AND delivery_status=0;")"
    [[ "$pending_after" == "0" ]] || fail "pagination batch still has $pending_after Pending rows after Receiver ACK"

    run_case session-happy-path \
        "$BUILD_DIR/gateway_session_client_demo" "$HOST" "$GATEWAY_A_PORT"

    run_case same-gateway-idempotency \
        "$BUILD_DIR/gateway_same_gateway_client_demo" "$HOST" "$GATEWAY_A_PORT"

    run_case client-retry \
        "$BUILD_DIR/gateway_client_retry_demo" "$HOST" "$GATEWAY_A_PORT"

    run_case sender-ack-loss \
        "$BUILD_DIR/gateway_client_ack_loss_demo" "$HOST" "$GATEWAY_A_PORT"

    run_case retry-exhaustion-uncertain \
        "$BUILD_DIR/gateway_client_retry_exhaustion_demo" "$HOST" "$GATEWAY_A_PORT"

    run_case offline-replay \
        "$BUILD_DIR/gateway_offline_client_demo" "$HOST" "$GATEWAY_A_PORT"

    run_case receiver-reconnect-dedup \
        "$BUILD_DIR/gateway_receiver_reconnect_dedup_demo" "$HOST" "$GATEWAY_A_PORT"

    # This fixture intentionally remains Pending so we can prove restart recovery.
    run_case offline-idempotency-fixture \
        "$BUILD_DIR/gateway_offline_idempotency_client_demo" "$HOST" "$GATEWAY_A_PORT"

    local cid mid
    cid="$(sed -n 's/^verification_client_message_id=//p' "$LAST_CASE_LOG" | tail -n1)"
    mid="$(sed -n 's/^verification_server_message_id=//p' "$LAST_CASE_LOG" | tail -n1)"
    [[ -n "$cid" && -n "$mid" ]] || fail "could not parse offline restart fixture identity"
    assert_delivery_status "$mid" 0

    stop_gateway_a
    start_gateway_a

    run_case offline-restart-recovery \
        "$BUILD_DIR/gateway_offline_recovery_client_demo" "$HOST" "$GATEWAY_A_PORT" "$cid" "$mid"

    # Receiver recovery must make the durable state non-Pending.
    local post_status
    post_status="$(mysql_scalar "SELECT delivery_status FROM im_private_messages WHERE message_id=${mid} LIMIT 1;")"
    [[ "$post_status" == "1" || "$post_status" == "2" ]] || fail "offline restart recovery did not advance durable state, message_id=$mid status=$post_status"
}

run_peer_duplicate_and_durable_replay() {
    local lease_token
    lease_token="$(wait_gateway_a_lease_token)" || fail "could not resolve gateway-a lease token from Redis registry"

    run_case peer-duplicate \
        "$BUILD_DIR/gateway_peer_duplicate_client_demo" \
        "$HOST" "$GATEWAY_B_PORT" gateway-a "$lease_token" "$CONFIG_B"

    local mid mtext
    mid="$(sed -n 's/^fixture_message_id=//p' "$LAST_CASE_LOG" | tail -n1)"
    mtext="$(sed -n 's/^fixture_message_text=//p' "$LAST_CASE_LOG" | tail -n1)"
    [[ -n "$mid" && -n "$mtext" ]] || fail "could not parse peer durable fixture"

    # Updated peer duplicate demo sends Receiver ACK, so DB must now be ReceiverConfirmed.
    assert_delivery_status "$mid" 1

    # Restart Gateway B to erase process-local dedup and force durable duplicate recovery.
    stop_gateway_b
    start_gateway_b normal
    wait_discovery_settle

    run_case peer-durable-replay-after-restart \
        "$BUILD_DIR/gateway_peer_durable_replay_client_demo" \
        "$HOST" "$GATEWAY_B_PORT" gateway-a "$lease_token" "$mid" "$mtext"
}

run_cross_gateway_suite() {
    if [[ -z "$GW_B_PID" ]]; then
        start_gateway_b normal
    fi
    wait_discovery_settle

    run_case cross-gateway \
        "$BUILD_DIR/gateway_cross_gateway_client_demo" "$HOST" "$GATEWAY_A_PORT" "$GATEWAY_B_PORT"

    run_case cross-gateway-reconnect-dedup \
        "$BUILD_DIR/gateway_cross_gateway_reconnect_dedup_demo" "$HOST" "$GATEWAY_A_PORT" "$GATEWAY_B_PORT"

    run_peer_duplicate_and_durable_replay

    # Response-loss fault must be enabled only on Gateway B, where the peer response originates.
    stop_gateway_b
    start_gateway_b drop-first-peer-response
    wait_discovery_settle

    run_case cross-gateway-peer-response-loss \
        "$BUILD_DIR/gateway_cross_gateway_peer_response_loss_demo" "$HOST" "$GATEWAY_A_PORT" "$GATEWAY_B_PORT"

    stop_gateway_b
    start_gateway_b normal
    wait_discovery_settle
}

run_gateway_b_crash_recovery() {
    local name="cross-gateway-b-crash-recovery"
    local log_file="$ARTIFACT_DIR/${name}.log"
    local fifo="$ARTIFACT_DIR/${name}.fifo"
    rm -f "$fifo"
    mkfifo "$fifo"

    # Open both ends in the parent so child redirection cannot block on FIFO open.
    exec 9<>"$fifo"

    log "RUN $name (automated SIGKILL + MySQL Pending assertion + restart)"
    "$BUILD_DIR/gateway_cross_gateway_gateway_b_crash_recovery_demo" \
        "$HOST" "$GATEWAY_A_PORT" "$GATEWAY_B_PORT" "$GW_B_PID" \
        <&9 >"$log_file" 2>&1 &
    local demo_pid=$!

    local action_seen=0
    for _ in $(seq 1 150); do
        if grep -q '\[ACTION REQUIRED\]' "$log_file" 2>/dev/null; then
            action_seen=1
            break
        fi
        if ! kill -0 "$demo_pid" 2>/dev/null; then
            break
        fi
        sleep 0.1
    done

    if [[ "$action_seen" != "1" ]]; then
        exec 9>&-
        rm -f "$fifo"
        wait "$demo_pid" 2>/dev/null || true
        record_result "$name" FAIL "$log_file"
        tail -n 120 "$log_file" || true
        return 1
    fi

    # Demo has already SIGKILLed the old Gateway B before printing ACTION REQUIRED.
    wait "$GW_B_PID" 2>/dev/null || true
    GW_B_PID=""
    wait_port_closed "$GATEWAY_B_PORT" 10 || fail "gateway-b port stayed open after SIGKILL"

    local mid status
    mid="$(sed -n 's/.*verify MySQL message_id=\([0-9][0-9]*\).*/\1/p' "$log_file" | tail -n1)"
    [[ -n "$mid" ]] || fail "could not parse crash-recovery message_id"
    status="$(mysql_scalar "SELECT delivery_status FROM im_private_messages WHERE message_id=${mid} LIMIT 1;")"
    [[ "$status" == "0" ]] || fail "crash-recovery fixture must still be Pending before Gateway B restart, message_id=$mid status=$status"

    # SIGKILL is intentionally different from our normal stop_gateway_b path:
    # the dead process cannot run GatewayRegistryLease::Stop()/UnregisterIfMatch().
    # Therefore the old Redis lease key must remain alive until its TTL expires.
    # Starting a new process with the same gateway_id before that fencing window
    # closes is correctly rejected as "gateway_id owned by another lease".
    #
    # Do NOT DEL the registry key here: that would bypass the lease/fencing
    # semantics this crash-recovery test is supposed to validate.
    wait_gateway_registry_lease_expired gateway-b 30 || \
        fail "gateway-b crashed lease did not expire before restart"

    start_gateway_b normal
    wait_discovery_settle

    # Release the demo's manual checkpoint after the automatic assertions/restart are complete.
    printf '\n' >&9
    exec 9>&-

    if wait "$demo_pid"; then
        record_result "$name" PASS "$log_file"
        log "PASS $name"
        rm -f "$fifo"
        return 0
    fi

    record_result "$name" FAIL "$log_file"
    log "FAIL $name"
    tail -n 160 "$log_file" || true
    rm -f "$fifo"
    return 1
}

run_semantic_scan() {
    local log_file="$ARTIFACT_DIR/semantic-scan.log"
    : > "$log_file"

    local scan_paths=(
        "$ROOT_DIR/gateway"
        "$ROOT_DIR/common/protocol"
        "$ROOT_DIR/examples"
        "$ROOT_DIR/tests"
        "$ROOT_DIR/docs"
    )

    local old_reason_hits
    old_reason_hits="$(rg -n -i 'local_delivered|local_already_delivered' "${scan_paths[@]}" 2>/dev/null || true)"
    if [[ -n "$old_reason_hits" ]]; then
        printf '%s\n' "$old_reason_hits" >> "$log_file"
        record_result semantic-scan FAIL "$log_file"
        log "FAIL semantic-scan: stale delivered reason remains"
        cat "$log_file"
        return 1
    fi

    # Exactly-Once is allowed only in explicit negative/non-guarantee statements.
    local exact_hits disallowed_exact
    exact_hits="$(rg -n -i 'exactly[ -]?once|exactly_once' "${scan_paths[@]}" 2>/dev/null || true)"
    disallowed_exact="$(printf '%s\n' "$exact_hits" | grep -Ev '不提供|不保证|不是|does not|do not|not provide|not guarantee|rather than|without claiming|非Exactly|非 Exactly' || true)"
    if [[ -n "$disallowed_exact" ]]; then
        printf '%s\n' "$disallowed_exact" >> "$log_file"
        record_result semantic-scan FAIL "$log_file"
        log "FAIL semantic-scan: positive/ambiguous Exactly-Once wording remains"
        cat "$log_file"
        return 1
    fi

    printf 'No stale local_delivered/local_already_delivered reason found.\n' >> "$log_file"
    printf 'No positive or ambiguous Exactly-Once claim found.\n' >> "$log_file"
    printf '%s\n' "$exact_hits" >> "$log_file"
    record_result semantic-scan PASS "$log_file"
    log "PASS semantic-scan"
}

main() {
    cd "$ROOT_DIR"

    require_cmd cmake
    require_cmd ctest
    require_cmd python3
    require_cmd mysql
    require_cmd redis-cli
    require_cmd rg

    [[ -f "$CONFIG_A" ]] || fail "missing config: $CONFIG_A"
    [[ -f "$CONFIG_B" ]] || fail "missing config: $CONFIG_B"

    assert_ports_free
    build_all
    run_unit_tests
    run_single_gateway_suite

    start_gateway_b normal
    run_cross_gateway_suite
    run_gateway_b_crash_recovery
    run_semantic_scan

    log "M12 regression gate completed"
    log "PASS=$PASS_COUNT FAIL=$FAIL_COUNT"
    log "summary: $SUMMARY_FILE"
    log "artifacts: $ARTIFACT_DIR"

    if (( FAIL_COUNT != 0 )); then
        exit 1
    fi

    printf '\n[M12 PASS] Reliable Messaging final regression and acceptance gate passed.\n'
}

main "$@"
