#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="${1:-all}"
STAMP="$(date +%Y%m%d-%H%M%S)"
RUN_ROOT="${TINYIMX_CAPSTONE_RUN_ROOT:-$HOME/tinyimx-final-evidence/final-capstone-${STAMP}}"
mkdir -p "$RUN_ROOT"

run_plain(){ local label="$1"; shift; echo; echo "========== FINAL CAPSTONE $label =========="; "$@"; }
run_gate(){
  local label="$1" dir="$2"; shift 2
  echo; echo "========== FINAL CAPSTONE $label =========="
  TINYIMX_CAPSTONE_ARTIFACT_DIR="$RUN_ROOT/$dir" "$@"
}

case "$MODE" in
  verify) run_plain VERIFY "$ROOT/scripts/tinyimx_capstone_verify.sh" "$ROOT" ;;
  failover-connection) run_gate FAILOVER_CONNECTION failover-connection "$ROOT/scripts/tinyimx_capstone_failover.sh" connection ;;
  failover-message) run_gate FAILOVER_MESSAGE failover-message "$ROOT/scripts/tinyimx_capstone_failover.sh" message ;;
  user-scale) run_gate USER_SCALE user-scale "$ROOT/scripts/tinyimx_capstone_user_scale.sh" ;;
  hotspot-group) run_gate HOTSPOT_GROUP hotspot-group "$ROOT/scripts/tinyimx_capstone_hotspot_group.sh" ;;
  file) run_gate FILE file "$ROOT/scripts/tinyimx_capstone_file.sh" ;;
  mq-fault) run_gate MQ_FAULT mq-fault "$ROOT/scripts/tinyimx_capstone_mq_fault.sh" ;;
  backpressure) run_gate BACKPRESSURE backpressure "$ROOT/scripts/tinyimx_capstone_backpressure.sh" ;;
  soak) run_gate SOAK soak "$ROOT/scripts/tinyimx_capstone_soak.sh" ;;
  all)
    run_plain VERIFY "$ROOT/scripts/tinyimx_capstone_verify.sh" "$ROOT" | tee "$RUN_ROOT/verify.log"
    run_gate FAILOVER_CONNECTION failover-connection "$ROOT/scripts/tinyimx_capstone_failover.sh" connection
    run_gate FAILOVER_MESSAGE failover-message "$ROOT/scripts/tinyimx_capstone_failover.sh" message
    run_gate USER_SCALE user-scale "$ROOT/scripts/tinyimx_capstone_user_scale.sh"
    run_gate HOTSPOT_GROUP hotspot-group "$ROOT/scripts/tinyimx_capstone_hotspot_group.sh"
    run_gate FILE file "$ROOT/scripts/tinyimx_capstone_file.sh"
    run_gate MQ_FAULT mq-fault "$ROOT/scripts/tinyimx_capstone_mq_fault.sh"
    run_gate BACKPRESSURE backpressure "$ROOT/scripts/tinyimx_capstone_backpressure.sh"
    run_gate SOAK soak "$ROOT/scripts/tinyimx_capstone_soak.sh"
    python3 "$ROOT/scripts/tinyimx_capstone_report.py" "$RUN_ROOT"
    echo "TINYIMX_FINAL_CAPSTONE_LOCAL_ACCEPTANCE=PASS"
    echo "CAPSTONE_RUN_ROOT=$RUN_ROOT"
    ;;
  report) python3 "$ROOT/scripts/tinyimx_capstone_report.py" "$RUN_ROOT" ;;
  *) echo "usage: $0 {verify|failover-connection|failover-message|user-scale|hotspot-group|file|mq-fault|backpressure|soak|all|report}" >&2; exit 64;;
esac
