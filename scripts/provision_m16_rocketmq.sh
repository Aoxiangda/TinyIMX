#!/usr/bin/env bash

set +e
set +u
set +o pipefail

ROOT="${TINYIMX_ROOT:-$HOME/projects/TinyIMX_publish}"
cd "$ROOT" || exit 90

MQADMIN="${TINYIMX_MQADMIN:-$ROOT/.local/rocketmq-5.5.1/dist/bin/mqadmin}"

NAMESRV="${TINYIMX_ROCKETMQ_NAMESRV:-127.0.0.1:9876}"
CLUSTER="${TINYIMX_ROCKETMQ_CLUSTER:-DefaultCluster}"

TOPIC="${TINYIMX_ROCKETMQ_TOPIC:-tinyimx-message-events}"
TOPIC_QUEUES="${TINYIMX_ROCKETMQ_TOPIC_QUEUES:-4}"

GROUP="${TINYIMX_ROCKETMQ_CONSUMER_GROUP:-tinyimx-unread-projector-v1}"

RETRY_QUEUE_NUMS="${TINYIMX_ROCKETMQ_RETRY_QUEUE_NUMS:-1}"
RETRY_MAX_TIMES="${TINYIMX_ROCKETMQ_RETRY_MAX_TIMES:-16}"

DRY_RUN="${TINYIMX_ROCKETMQ_PROVISION_DRY_RUN:-0}"

fail() {
    echo "[FAIL] $*" >&2
    exit 1
}

run() {
    echo "+ $*"

    if [[ "$DRY_RUN" == "1" ]]; then
        return 0
    fi

    "$@"
}

[[ -x "$MQADMIN" ]] \
    || fail "mqadmin not executable: $MQADMIN"

[[ "$TOPIC_QUEUES" =~ ^[1-9][0-9]*$ ]] \
    || fail "TOPIC_QUEUES must be a positive integer"

[[ "$RETRY_QUEUE_NUMS" =~ ^[1-9][0-9]*$ ]] \
    || fail "RETRY_QUEUE_NUMS must be a positive integer"

[[ "$RETRY_MAX_TIMES" =~ ^[0-9]+$ ]] \
    || fail "RETRY_MAX_TIMES must be a non-negative integer"

echo "================================================"
echo "TinyIMX M16 RocketMQ Resource Provisioning"
echo "================================================"
echo "namesrv=$NAMESRV"
echo "cluster=$CLUSTER"
echo "topic=$TOPIC"
echo "topic_queues=$TOPIC_QUEUES"
echo "consumer_group=$GROUP"
echo "retry_queue_nums=$RETRY_QUEUE_NUMS"
echo "retry_max_times=$RETRY_MAX_TIMES"
echo "dry_run=$DRY_RUN"

###############################################################################
# Verify the installed mqadmin has the contract we depend on.
###############################################################################

HELP="$("$MQADMIN" help updateSubGroup 2>&1)"

echo "$HELP" | grep -q -- '-q' \
    || fail "mqadmin updateSubGroup does not expose -q retryQueueNums"

echo "$HELP" | grep -q -- '-r' \
    || fail "mqadmin updateSubGroup does not expose -r retryMaxTimes"

echo "[PASS] mqadmin updateSubGroup supports retry queue/max retry contract"

###############################################################################
# Topic: idempotent create/update.
#
# Production currently uses four queues, matching the established M16 runtime
# route. Environment override remains available for deployment topology.
###############################################################################

TOPIC_LOG="$(mktemp)"

if [[ "$DRY_RUN" == "1" ]]; then
    run "$MQADMIN" updateTopic \
        -n "$NAMESRV" \
        -c "$CLUSTER" \
        -t "$TOPIC" \
        -r "$TOPIC_QUEUES" \
        -w "$TOPIC_QUEUES" \
        -p 6
else
    "$MQADMIN" updateTopic \
        -n "$NAMESRV" \
        -c "$CLUSTER" \
        -t "$TOPIC" \
        -r "$TOPIC_QUEUES" \
        -w "$TOPIC_QUEUES" \
        -p 6 \
        >"$TOPIC_LOG" 2>&1

    TOPIC_RC=$?

    cat "$TOPIC_LOG"

    [[ "$TOPIC_RC" -eq 0 ]] \
        || fail "updateTopic returned rc=$TOPIC_RC"

    if grep -qiE \
        'SubCommandException|Exception in thread|RemotingConnectException' \
        "$TOPIC_LOG"
    then
        fail "updateTopic emitted an exception despite rc=0"
    fi
fi

###############################################################################
# Consumer group: this is the production retry/DLQ contract.
#
# - consumeEnable=true
# - consumeBroadcastEnable=false
# - retryQueueNums=1
# - retryMaxTimes=16
# - notifyConsumerIdsChanged=true
###############################################################################

GROUP_LOG="$(mktemp)"

if [[ "$DRY_RUN" == "1" ]]; then
    run "$MQADMIN" updateSubGroup \
        -n "$NAMESRV" \
        -c "$CLUSTER" \
        -g "$GROUP" \
        -s true \
        -d false \
        -m false \
        -q "$RETRY_QUEUE_NUMS" \
        -r "$RETRY_MAX_TIMES" \
        -a true
else
    "$MQADMIN" updateSubGroup \
        -n "$NAMESRV" \
        -c "$CLUSTER" \
        -g "$GROUP" \
        -s true \
        -d false \
        -m false \
        -q "$RETRY_QUEUE_NUMS" \
        -r "$RETRY_MAX_TIMES" \
        -a true \
        >"$GROUP_LOG" 2>&1

    GROUP_RC=$?

    cat "$GROUP_LOG"

    [[ "$GROUP_RC" -eq 0 ]] \
        || fail "updateSubGroup returned rc=$GROUP_RC"

    if grep -qiE \
        'SubCommandException|Exception in thread|RemotingConnectException' \
        "$GROUP_LOG"
    then
        fail "updateSubGroup emitted an exception despite rc=0"
    fi

    grep -qi 'success' "$GROUP_LOG" \
        || fail "updateSubGroup did not emit observable success"
fi

###############################################################################
# Topic route is our postcondition for the topic.
###############################################################################

if [[ "$DRY_RUN" != "1" ]]; then
    ROUTE="$(
        "$MQADMIN" topicRoute \
            -n "$NAMESRV" \
            -t "$TOPIC" \
            2>&1
    )"

    echo "$ROUTE"

    echo "$ROUTE" | grep -q '"brokerName"' \
        || fail "topic route not observable after provisioning"

    echo "$ROUTE" | grep -q '"queueDatas"' \
        || fail "topic queues not observable after provisioning"
fi

echo
echo "[PASS] RocketMQ topic provisioning contract satisfied"
echo "[PASS] ConsumerGroup retry contract requested:"
echo "       group=$GROUP"
echo "       retryQueueNums=$RETRY_QUEUE_NUMS"
echo "       retryMaxTimes=$RETRY_MAX_TIMES"
