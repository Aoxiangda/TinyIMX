#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${HOME}/projects/TinyIMX_publish"
BUILD="${ROOT}/build/linux-debug"
CFG="${ROOT}/config/observability.example.json"
DEPLOY="${ROOT}/deploy/observability"
ARTIFACT_DIR="${HOME}/tmp/m20-observability-e2e-$(date +%Y%m%d-%H%M%S)"
PROM_ADDR="127.0.0.1:19090"
PIDS=()

mkdir -p "${ARTIFACT_DIR}"
cd "${ROOT}"

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "ERROR: required command not found: $1"
    exit 10
  }
}

require_cmd curl
require_cmd python3
require_cmd otelcol-contrib
require_cmd prometheus
[[ -x "${BUILD}/tinyimx_observability_demo" ]]

cleanup() {
  local rc=$?
  trap - EXIT INT TERM

  for ((i=${#PIDS[@]}-1; i>=0; --i)); do
    kill -TERM "${PIDS[$i]}" 2>/dev/null || true
  done

  for pid in "${PIDS[@]}"; do
    for _ in $(seq 1 40); do
      kill -0 "${pid}" 2>/dev/null || break
      sleep 0.1
    done
    if kill -0 "${pid}" 2>/dev/null; then
      kill -KILL "${pid}" 2>/dev/null || true
    fi
    wait "${pid}" 2>/dev/null || true
  done

  echo "ARTIFACT_DIR=${ARTIFACT_DIR}"
  exit "${rc}"
}
trap cleanup EXIT INT TERM

listen_port_free() {
  python3 - "$1" <<'PY'
import pathlib,sys
port=int(sys.argv[1])
for path in ('/proc/net/tcp','/proc/net/tcp6'):
    p=pathlib.Path(path)
    if not p.exists():
        continue
    for line in p.read_text().splitlines()[1:]:
        cols=line.split()
        if len(cols) < 4 or cols[3] != '0A':
            continue
        local=cols[1]
        if int(local.rsplit(':',1)[1],16) == port:
            raise SystemExit(1)
raise SystemExit(0)
PY
}

for port in 4317 9464 19090 19120; do
  listen_port_free "${port}" || { echo "ERROR: LISTEN port ${port} already in use"; exit 11; }
done

otelcol-contrib --config="${DEPLOY}/otel-collector.yaml" \
  >"${ARTIFACT_DIR}/collector.log" 2>&1 &
PIDS+=("$!")

for _ in $(seq 1 80); do
  if curl -fsS http://127.0.0.1:9464/metrics >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
curl -fsS http://127.0.0.1:9464/metrics >/dev/null

echo "COLLECTOR_PROMETHEUS_EXPORTER_READY=PASS"

(
  cd "${DEPLOY}"
  exec prometheus \
    --config.file="prometheus.yml" \
    --storage.tsdb.path="${ARTIFACT_DIR}/prometheus-data" \
    --web.listen-address="${PROM_ADDR}"
) >"${ARTIFACT_DIR}/prometheus.log" 2>&1 &
PIDS+=("$!")

for _ in $(seq 1 100); do
  if curl -fsS "http://${PROM_ADDR}/-/ready" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
curl -fsS "http://${PROM_ADDR}/-/ready" >/dev/null

echo "PROMETHEUS_READY=PASS"

"${BUILD}/tinyimx_observability_demo" "${CFG}" \
  >"${ARTIFACT_DIR}/demo.log" 2>&1

grep -q 'M20_OBSERVABILITY_DEMO=PASS' "${ARTIFACT_DIR}/demo.log"

for _ in $(seq 1 40); do
  curl -fsS http://127.0.0.1:9464/metrics \
    >"${ARTIFACT_DIR}/collector-metrics.txt" || true
  if grep -q 'tinyimx_thread_pool' "${ARTIFACT_DIR}/collector-metrics.txt" && \
     grep -q 'tinyimx_tcp_connections' "${ARTIFACT_DIR}/collector-metrics.txt"; then
    break
  fi
  sleep 0.25
done

grep -q 'tinyimx_thread_pool' "${ARTIFACT_DIR}/collector-metrics.txt"
grep -q 'tinyimx_tcp_connections' "${ARTIFACT_DIR}/collector-metrics.txt"

echo "COLLECTOR_TINYIMX_METRICS=PASS"

# Wait for Prometheus to complete an actual healthy scrape of the Collector.
# Readiness alone only proves that Prometheus is serving HTTP; it does not prove
# that the configured scrape target has produced a sample yet.
TARGET_READY=0
for _ in $(seq 1 120); do
  curl -fsSG \
    "http://${PROM_ADDR}/api/v1/targets" \
    --data-urlencode 'state=active' \
    --data-urlencode 'scrapePool=tinyimx-otel-collector' \
    >"${ARTIFACT_DIR}/prometheus-targets.json" || true

  if python3 - "${ARTIFACT_DIR}/prometheus-targets.json" <<'PY'
import json,sys
try:
    x=json.load(open(sys.argv[1],encoding='utf-8'))
except Exception:
    raise SystemExit(1)

if x.get('status') != 'success':
    raise SystemExit(1)

targets=x.get('data',{}).get('activeTargets',[])
matches=[
    t for t in targets
    if t.get('scrapePool') == 'tinyimx-otel-collector'
    or t.get('labels',{}).get('job') == 'tinyimx-otel-collector'
]
if not matches:
    raise SystemExit(1)

t=matches[0]
if t.get('health') != 'up':
    raise SystemExit(1)
if not t.get('lastScrape'):
    raise SystemExit(1)
if t.get('lastError'):
    raise SystemExit(1)

raise SystemExit(0)
PY
  then
    TARGET_READY=1
    break
  fi

  sleep 0.25
done

if [[ "${TARGET_READY}" -ne 1 ]]; then
  echo "ERROR: Prometheus target never reached healthy scraped state"
  cat "${ARTIFACT_DIR}/prometheus-targets.json" 2>/dev/null || true
  exit 30
fi

python3 - "${ARTIFACT_DIR}/prometheus-targets.json" <<'PY'
import json,sys
x=json.load(open(sys.argv[1],encoding='utf-8'))
targets=x['data']['activeTargets']
matches=[
    t for t in targets
    if t.get('scrapePool') == 'tinyimx-otel-collector'
    or t.get('labels',{}).get('job') == 'tinyimx-otel-collector'
]
assert matches, targets
t=matches[0]
assert t.get('health') == 'up', t
assert t.get('lastScrape'), t
assert not t.get('lastError'), t
print('PROMETHEUS_TARGET_HEALTH=PASS')
PY

# Prove the standard scrape-health series has entered Prometheus storage.
UP_READY=0
for _ in $(seq 1 80); do
  curl -fsSG \
    "http://${PROM_ADDR}/api/v1/query" \
    --data-urlencode 'query=up{job="tinyimx-otel-collector"}' \
    >"${ARTIFACT_DIR}/prometheus-up.json" || true

  if python3 - "${ARTIFACT_DIR}/prometheus-up.json" <<'PY'
import json,sys
try:
    x=json.load(open(sys.argv[1],encoding='utf-8'))
except Exception:
    raise SystemExit(1)

if x.get('status') != 'success':
    raise SystemExit(1)

result=x.get('data',{}).get('result',[])
if result and float(result[0]['value'][1]) == 1.0:
    raise SystemExit(0)

raise SystemExit(1)
PY
  then
    UP_READY=1
    break
  fi

  sleep 0.25
done

if [[ "${UP_READY}" -ne 1 ]]; then
  echo "ERROR: Prometheus up-series query never became visible"
  cat "${ARTIFACT_DIR}/prometheus-up.json" 2>/dev/null || true
  exit 31
fi

echo "PROMETHEUS_SCRAPE_QUERY=PASS"

# A healthy target is not enough: prove TinyIMX application metrics themselves
# crossed OTLP -> Collector -> Prometheus exporter -> Prometheus TSDB.
TINYIMX_READY=0
for _ in $(seq 1 80); do
  curl -fsSG \
    "http://${PROM_ADDR}/api/v1/query" \
    --data-urlencode 'query=count({__name__=~"tinyimx_thread_pool.*"})' \
    >"${ARTIFACT_DIR}/prometheus-tinyimx.json" || true

  if python3 - "${ARTIFACT_DIR}/prometheus-tinyimx.json" <<'PY'
import json,sys
try:
    x=json.load(open(sys.argv[1],encoding='utf-8'))
except Exception:
    raise SystemExit(1)

if x.get('status') != 'success':
    raise SystemExit(1)

result=x.get('data',{}).get('result',[])
if result and float(result[0]['value'][1]) > 0.0:
    raise SystemExit(0)

raise SystemExit(1)
PY
  then
    TINYIMX_READY=1
    break
  fi

  sleep 0.25
done

if [[ "${TINYIMX_READY}" -ne 1 ]]; then
  echo "ERROR: TinyIMX metric series never became queryable in Prometheus"
  cat "${ARTIFACT_DIR}/prometheus-tinyimx.json" 2>/dev/null || true
  exit 32
fi

echo "PROMETHEUS_TINYIMX_QUERY=PASS"

echo
echo "M20_COLLECTOR_PROMETHEUS_E2E=PASS"
echo "ARTIFACT_DIR=${ARTIFACT_DIR}"
