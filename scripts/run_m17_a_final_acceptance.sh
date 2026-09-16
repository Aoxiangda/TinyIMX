#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

CONFIG_PATH="${1:-config/gateway-a.local.json}"

TS="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="$ROOT_DIR/artifacts/m17-a-final-$TS"

mkdir -p "$ARTIFACT_DIR"

exec > >(tee "$ARTIFACT_DIR/final-acceptance.log") 2>&1

fail() {
    echo "[M17-A FINAL] FAIL: $*" >&2
    exit 1
}

pass() {
    echo "[M17-A FINAL] PASS: $*"
}

echo "============================================================"
echo " TinyIMX M17-A Final Acceptance"
echo "============================================================"

export VCPKG_ROOT="${VCPKG_ROOT:-$ROOT_DIR/toolchains/vcpkg-tinyimx}"
export TINYIMX_ROCKETMQ_PREFIX="${TINYIMX_ROCKETMQ_PREFIX:-$ROOT_DIR/toolchains/rocketmq-cpp-5.1.1}"
export TINYIMX_BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"

export TINYIMX_M17_A3_RUN_ZK_INTEGRATION=1
export TINYIMX_ZOOKEEPER_CONNECT="${TINYIMX_ZOOKEEPER_CONNECT:-127.0.0.1:2181}"

echo
echo "===== Git baseline ====="

BRANCH="$(git branch --show-current)"
HEAD="$(git rev-parse HEAD)"

echo "branch=$BRANCH"
echo "head=$HEAD"

[[ "$BRANCH" == "feature/m17-group-domain-foundation-v1" ]] \
    || fail "unexpected branch: $BRANCH"

git diff --check
pass "git diff --check"

echo
echo "===== Acceptance script syntax ====="

for script in \
    scripts/run_m17_a1_group_domain_acceptance.sh \
    scripts/run_m17_a2_membership_acceptance.sh \
    scripts/run_m17_a3_gateway_group_vertical_acceptance.sh
do
    bash -n "$script"
    echo "[PASS] bash -n $script"
done

echo
echo "===== Runtime dependency listeners ====="

for port in 2181 50052 50054 9001
do
    if ! ss -lntp | grep -q ":${port}\b"; then
        fail "required listener missing: $port"
    fi

    echo "[PASS] listener :$port"
done

echo
echo "===== M17-A3 aggregate regression ====="

bash scripts/run_m17_a3_gateway_group_vertical_acceptance.sh \
    "$CONFIG_PATH"

pass "A3 aggregate acceptance"

echo
echo "===== Fresh TCP vertical E2E ====="

CLIENT="$ROOT_DIR/build/linux-debug/gateway_group_client_demo"

[[ -x "$CLIENT" ]] || fail "gateway_group_client_demo missing"

HOST=127.0.0.1
PORT=9001

USER_A=user10001
USER_B=user10002
PASSWORD=123456

RUN_ID="m17af-${TS}-$$"

run_group() {
    local user="$1"
    local operation="$2"
    local body="$3"

    "$CLIENT" \
        "$HOST" \
        "$PORT" \
        "$user" \
        "$PASSWORD" \
        "$operation" \
        "$body"
}

CREATE_BODY="$(
    printf \
      '{"actor_user_id":10002,"client_operation_id":"%s-create","name":"M17-A-Final-%s","description":"M17-A final TCP acceptance","join_policy":"open","max_members":10}' \
      "$RUN_ID" "$TS"
)"

CREATE_OUT="$(run_group "$USER_A" create "$CREATE_BODY")"
echo "$CREATE_OUT"

GROUP_ID="$(
    printf '%s\n' "$CREATE_OUT" |
    python3 -c '
import json,sys
d=json.load(sys.stdin)
assert d["success"] is True, d
assert d["result"] == "applied", d
assert d["group"]["owner_user_id"] == 10001, d
print(d["group"]["group_id"])
'
)"

echo "[PASS] CreateGroup group_id=$GROUP_ID, spoofed actor ignored"

RETRY_OUT="$(run_group "$USER_A" create "$CREATE_BODY")"
echo "$RETRY_OUT"

printf '%s\n' "$RETRY_OUT" |
python3 -c '
import json,sys
d=json.load(sys.stdin)
assert d["success"] is True, d
assert d["result"] == "reused", d
print("[PASS] CreateGroup retry=REUSED")
'

INVITE_OUT="$(
    run_group "$USER_A" invite \
      "{\"client_operation_id\":\"${RUN_ID}-invite\",\"group_id\":${GROUP_ID},\"target_user_id\":10002}"
)"
echo "$INVITE_OUT"

printf '%s\n' "$INVITE_OUT" |
python3 -c '
import json,sys
d=json.load(sys.stdin)
assert d["success"] is True, d
assert d["result"] == "applied", d
print("[PASS] InviteMember")
'

MY_GROUPS="$(
    run_group "$USER_B" my-groups \
      '{"after_group_id":0,"limit":50}'
)"
echo "$MY_GROUPS"

printf '%s\n' "$MY_GROUPS" |
python3 -c "import json,sys
d=json.load(sys.stdin)
gid=int('$GROUP_ID')
assert d['success'] is True, d
assert any(x['group_id']==gid for x in d['groups']), d
print('[PASS] ListMyGroups contains group')
"

MEMBERS="$(
    run_group "$USER_A" members \
      "{\"group_id\":${GROUP_ID},\"after_user_id\":0,\"limit\":50}"
)"
echo "$MEMBERS"

printf '%s\n' "$MEMBERS" |
python3 -c '
import json,sys
d=json.load(sys.stdin)
assert d["success"] is True, d
m={x["user_id"]:x for x in d["members"]}
assert m[10001]["role"] == "owner", m
assert m[10002]["role"] == "member", m
print("[PASS] member snapshot OWNER/MEMBER")
'

DENIED="$(
    run_group "$USER_B" kick \
      "{\"client_operation_id\":\"${RUN_ID}-deny\",\"group_id\":${GROUP_ID},\"target_user_id\":10001}"
)"
echo "$DENIED"

printf '%s\n' "$DENIED" |
python3 -c '
import json,sys
d=json.load(sys.stdin)
assert d["success"] is False, d
assert d["reason"] == "group_permission_denied", d
print("[PASS] MEMBER cannot kick OWNER")
'

ROLE="$(
    run_group "$USER_A" role \
      "{\"client_operation_id\":\"${RUN_ID}-role\",\"group_id\":${GROUP_ID},\"target_user_id\":10002,\"role\":\"admin\"}"
)"
echo "$ROLE"

printf '%s\n' "$ROLE" |
python3 -c '
import json,sys
d=json.load(sys.stdin)
assert d["success"] is True, d
print("[PASS] SetMemberRole")
'

TRANSFER="$(
    run_group "$USER_A" transfer \
      "{\"client_operation_id\":\"${RUN_ID}-transfer\",\"group_id\":${GROUP_ID},\"target_user_id\":10002}"
)"
echo "$TRANSFER"

printf '%s\n' "$TRANSFER" |
python3 -c '
import json,sys
d=json.load(sys.stdin)
assert d["success"] is True, d
assert d["group"]["owner_user_id"] == 10002, d
print("[PASS] TransferOwnership")
'

LEAVE="$(
    run_group "$USER_A" leave \
      "{\"client_operation_id\":\"${RUN_ID}-leave\",\"group_id\":${GROUP_ID}}"
)"
echo "$LEAVE"

printf '%s\n' "$LEAVE" |
python3 -c '
import json,sys
d=json.load(sys.stdin)
assert d["success"] is True, d
assert d["group"]["owner_user_id"] == 10002, d
print("[PASS] old owner LeaveGroup")
'

CURRENT="$(
    run_group "$USER_B" get \
      "{\"group_id\":${GROUP_ID}}"
)"
echo "$CURRENT"

GROUP_VERSION="$(
    printf '%s\n' "$CURRENT" |
    python3 -c '
import json,sys
d=json.load(sys.stdin)
assert d["success"] is True, d
assert d["group"]["status"] == "active", d
assert d["group"]["owner_user_id"] == 10002, d
print(d["group"]["version"])
'
)"

DISBAND="$(
    run_group "$USER_B" disband \
      "{\"client_operation_id\":\"${RUN_ID}-disband\",\"group_id\":${GROUP_ID},\"expected_version\":${GROUP_VERSION}}"
)"
echo "$DISBAND"

printf '%s\n' "$DISBAND" |
python3 -c '
import json,sys
d=json.load(sys.stdin)
assert d["success"] is True, d
assert d["result"] == "applied", d
assert d["group"]["status"] == "disbanded", d
assert d["group"]["owner_user_id"] == 10002, d
print("[PASS] new owner DisbandGroup")
'

pass "fresh TCP vertical E2E"

echo
echo "===== Final source integrity ====="

git diff --check
pass "final git diff --check"

git status --short \
    > "$ARTIFACT_DIR/git-status.txt"

git diff --stat \
    > "$ARTIFACT_DIR/git-diff-stat.txt"

git diff --name-status \
    > "$ARTIFACT_DIR/git-diff-name-status.txt"

git ls-files --others --exclude-standard \
    > "$ARTIFACT_DIR/git-untracked.txt"

cat > "$ARTIFACT_DIR/SUMMARY.txt" <<EOF
M17-A FINAL ACCEPTANCE
status=PASS
timestamp=$TS
branch=$BRANCH
pre_freeze_head=$HEAD
fresh_e2e_group_id=$GROUP_ID

A1_group_domain=PASS
A2_durable_membership=PASS
A3_gateway_vertical=PASS
zookeeper_group_discovery=PASS
ordinary_regression=PASS
fresh_tcp_e2e=PASS
actor_spoof_protection=PASS
durable_retry_reused=PASS
permission_negative_case=PASS
ownership_lifecycle=PASS
EOF

echo
echo "============================================================"
echo "[M17-A FINAL ACCEPTANCE PASS]"
echo "artifact_dir=$ARTIFACT_DIR"
echo "fresh_group_id=$GROUP_ID"
echo "============================================================"
