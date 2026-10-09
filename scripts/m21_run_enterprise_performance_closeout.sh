#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)";cd "$ROOT_DIR"
MODE="${1:-local}";STAMP="$(date +%Y%m%d-%H%M%S)";ART="${M21_CLOSEOUT_ARTIFACT_DIR:-$ROOT_DIR/artifacts/m21-performance-closeout-$STAMP}";mkdir -p "$ART"
case "$MODE" in
  local)
    "$ROOT_DIR/scripts/m21_verify_production_schema_complete.sh"|tee "$ART/schema.log"
    "$ROOT_DIR/scripts/m21_verify_overload_protection.sh"|tee "$ART/overload.log"
    M21_MATRIX_ARTIFACT_DIR="$ART/local-matrix" "$ROOT_DIR/scripts/m21_run_perf_matrix.sh"|tee "$ART/matrix.log"
    M21_HOT_USER_ARTIFACT_DIR="$ART/hot-user" "$ROOT_DIR/scripts/m21_run_hot_user_benchmark.sh" "${M21_LOCAL_HOT_USER_CONNECTIONS:-500}" 60 "${M21_LOCAL_HOT_USER_RATE:-500}" 350000|tee "$ART/hot-user.log"
    M21_HOT_GROUP_ARTIFACT_DIR="$ART/hot-group" "$ROOT_DIR/scripts/m21_run_hot_group_benchmark.sh" "${M21_LOCAL_HOT_GROUP_MEMBERS:-100}" "${M21_LOCAL_HOT_GROUP_MESSAGES:-5}" 45 360000|tee "$ART/hot-group.log"
    echo "M21_LOCAL_ENTERPRISE_PERF_CLOSEOUT=COMPLETE"
    ;;
  cloud-sut)
    "$ROOT_DIR/scripts/m21_cloud_sut_prepare.sh" "${2:-10000}" "${3:-500000}"|tee "$ART/cloud-sut.log"
    echo "M21_CLOUD_SUT_STAGE=READY"
    ;;
  cloud-loadgen)
    HOST="${2:?usage: $0 cloud-loadgen <sut-host> [connections] [rate] [base]}";CONNS="${3:-10000}";RATE="${4:-1000}";BASE="${5:-500000}"
    M21_REMOTE_ARTIFACT_DIR="$ART/hold" "$ROOT_DIR/scripts/m21_run_remote_loadgen.sh" "$HOST" 9000 hold "$CONNS" 300 1 "$BASE"|tee "$ART/cloud-hold.log"
    M21_REMOTE_ARTIFACT_DIR="$ART/private" "$ROOT_DIR/scripts/m21_run_remote_loadgen.sh" "$HOST" 9000 private "$CONNS" 300 "$RATE" "$BASE"|tee "$ART/cloud-private.log"
    echo "M21_CLOUD_LOADGEN_STAGE=COMPLETE"
    ;;
  *) echo "usage: $0 <local|cloud-sut|cloud-loadgen> ..." >&2;exit 64;;
esac
echo "M21_CLOSEOUT_ARTIFACT_DIR=$ART"
