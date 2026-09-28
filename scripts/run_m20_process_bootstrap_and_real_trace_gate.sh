#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${HOME}/projects/TinyIMX_publish"
PROTECTED="gateway/GatewayPeerTransport.h"
RUNTIME_BIN="${HOME}/opt/tinyimx-observability/bin"

cd "${ROOT}"
export PATH="${RUNTIME_BIN}:${PATH}"

command -v otelcol-contrib >/dev/null 2>&1 || {
  echo "ERROR: otelcol-contrib is required in ${RUNTIME_BIN}"
  exit 10
}

PROTECTED_BEFORE="$(git diff -- "${PROTECTED}" | sha256sum | awk '{print $1}')"

echo "========== M20 PROCESS BOOTSTRAP FOCUSED GATE =========="
bash scripts/run_m20_process_bootstrap_trace_gate.sh

echo "========== M20 REAL MULTI-PROCESS TRACE EXPORT GATE =========="
if [[ $# -ge 1 ]]; then
  bash scripts/run_m20_real_multiprocess_trace_e2e.sh "$1"
else
  bash scripts/run_m20_real_multiprocess_trace_e2e.sh
fi

PROTECTED_AFTER="$(git diff -- "${PROTECTED}" | sha256sum | awk '{print $1}')"
test "${PROTECTED_BEFORE}" = "${PROTECTED_AFTER}"

echo
echo "PROTECTED_GATEWAY_DELTA=PASS"
echo "M20_PROCESS_BOOTSTRAP_AND_REAL_TRACE_GATE=PASS"
