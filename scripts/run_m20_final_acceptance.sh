#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${TINYIMX_BUILD_DIR:-$ROOT_DIR/build/linux-debug}"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
SOURCE_CONFIG="${1:-config/gateway-a.local.json}"
[[ "$SOURCE_CONFIG" = /* ]] || SOURCE_CONFIG="$ROOT_DIR/$SOURCE_CONFIG"

EXPECTED_BRANCH="feature/m20-observability-v1"
M19_SHA="3f42a43f91da6a83fd7a73b10630d48283a06005"
PROTECTED_FILE="gateway/GatewayPeerTransport.h"
RELEASE_LIST="$ROOT_DIR/scripts/m20_release_files.txt"
OBS_RUNTIME_BIN="${HOME}/opt/tinyimx-observability/bin"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${HOME}/tmp/m20-final-${TIMESTAMP}"
mkdir -p "$ARTIFACT_DIR"
chmod 700 "$ARTIFACT_DIR"

log(){ printf '[M20-FINAL] %s\n' "$*"; }
fail(){ printf '[M20-FINAL] FAIL: %s\n' "$*" >&2; exit 1; }

for c in cmake ctest git python3 curl mysql sha256sum; do
  command -v "$c" >/dev/null 2>&1 || fail "missing command: $c"
done
[[ -f "$SOURCE_CONFIG" ]] || fail "config missing: $SOURCE_CONFIG"
[[ -f "$RELEASE_LIST" ]] || fail "release file list missing: $RELEASE_LIST"

export PATH="$OBS_RUNTIME_BIN:$PATH"
export TINYIMX_BUILD_JOBS="$BUILD_JOBS"
export TINYIMX_BUILD_DIR="$BUILD_DIR"
command -v otelcol-contrib >/dev/null 2>&1 || fail "otelcol-contrib missing from PATH"
command -v prometheus >/dev/null 2>&1 || fail "prometheus missing from PATH"

cd "$ROOT_DIR"

log "release-candidate identity"
[[ "$(git branch --show-current)" == "$EXPECTED_BRANCH" ]] || fail "unexpected branch"
[[ "$(git rev-parse HEAD)" == "$M19_SHA" ]] || fail "pre-commit HEAD must remain M19 release SHA"
[[ -z "$(git diff --cached --name-only)" ]] || fail "index must be empty before final acceptance"
[[ -n "$(git diff -- "$PROTECTED_FILE")" ]] || fail "protected GatewayPeerTransport.h delta missing"

PROTECTED_BEFORE="$(git diff -- "$PROTECTED_FILE" | sha256sum | awk '{print $1}')"

log "exact M20 working-tree contract"
python3 - "$RELEASE_LIST" "$PROTECTED_FILE" <<'PY'
import subprocess, sys
release_list, protected = sys.argv[1:]
expected={x.strip() for x in open(release_list,encoding='utf-8') if x.strip() and not x.startswith('#')}
expected.add(protected)
status=subprocess.check_output(
    ['git','status','--porcelain=v1','--untracked-files=all'],
    text=True
)
actual=set()
for line in status.splitlines():
    if not line:
        continue
    path=line[3:]
    if ' -> ' in path:
        path=path.split(' -> ',1)[1]
    actual.add(path)
unknown=sorted(actual-expected)
missing=sorted(expected-actual)
if unknown:
    raise SystemExit('unexpected worktree paths: '+repr(unknown))
if missing:
    raise SystemExit('expected M20/protected paths missing from worktree: '+repr(missing))
print(f'M20_EXACT_WORKTREE_CONTRACT=PASS files={len(expected)-1} protected=1')
PY

log "dependency contract"
python3 <<'PY'
import json
x=json.load(open('vcpkg.json',encoding='utf-8'))
assert x['builtin-baseline']=='a44707152fabbedcf1c17fe80d7bfb7d9370d8d5'
otel=[d for d in x['dependencies'] if isinstance(d,dict) and d.get('name')=='opentelemetry-cpp']
assert len(otel)==1, otel
assert otel[0].get('features')==['otlp-grpc'], otel[0]
names={d if isinstance(d,str) else d.get('name') for d in x['dependencies']}
assert 'prometheus-cpp' not in names
assert 'curl' not in names
print('M20_DEPENDENCY_CONTRACT=PASS')
PY

otelcol-contrib --version | tee "$ARTIFACT_DIR/otelcol-version.txt"
prometheus --version | head -n 3 | tee "$ARTIFACT_DIR/prometheus-version.txt"
grep -q '0.161.0' "$ARTIFACT_DIR/otelcol-version.txt" || fail "unexpected otelcol-contrib version"
grep -q '3.15.0' "$ARTIFACT_DIR/prometheus-version.txt" || fail "unexpected Prometheus version"

log "source integrity"
git diff --check | tee "$ARTIFACT_DIR/git-diff-check-pre.log"
find "$ROOT_DIR" -type f \( -name '*.rej' -o -name '*.orig' \) -print > "$ARTIFACT_DIR/reject-orig-pre.log"
[[ ! -s "$ARTIFACT_DIR/reject-orig-pre.log" ]] || fail "reject/orig files present"
FREE_BYTES="$(df -B1 --output=avail / | tail -n1 | tr -d ' ')"
[[ "$FREE_BYTES" -ge 1073741824 ]] || fail "less than 1 GiB free before final acceptance"
df -h / > "$ARTIFACT_DIR/disk-pre.txt"

log "M20 distributed tracing core + retained foundation"
bash "$ROOT_DIR/scripts/run_m20_distributed_tracing_core_gate.sh" \
  2>&1 | tee "$ARTIFACT_DIR/tracing-core.log"
grep -q 'M20_DISTRIBUTED_TRACING_CORE_GATE=PASS' "$ARTIFACT_DIR/tracing-core.log" \
  || fail "tracing core gate marker missing"
grep -q 'M20_OBSERVABILITY_FOUNDATION_GATE=PASS' "$ARTIFACT_DIR/tracing-core.log" \
  || fail "retained foundation marker missing"

log "real Collector -> Prometheus metrics E2E"
bash "$ROOT_DIR/scripts/run_m20_collector_prometheus_e2e.sh" \
  2>&1 | tee "$ARTIFACT_DIR/collector-prometheus.log"
for marker in \
  COLLECTOR_PROMETHEUS_EXPORTER_READY=PASS \
  PROMETHEUS_READY=PASS \
  COLLECTOR_TINYIMX_METRICS=PASS \
  PROMETHEUS_TARGET_HEALTH=PASS \
  PROMETHEUS_SCRAPE_QUERY=PASS \
  PROMETHEUS_TINYIMX_QUERY=PASS \
  M20_COLLECTOR_PROMETHEUS_E2E=PASS
do
  grep -q "$marker" "$ARTIFACT_DIR/collector-prometheus.log" || fail "missing marker: $marker"
done

log "seven-process bootstrap + real multi-process trace E2E"
bash "$ROOT_DIR/scripts/run_m20_process_bootstrap_and_real_trace_gate.sh" "$SOURCE_CONFIG" \
  2>&1 | tee "$ARTIFACT_DIR/process-real-trace.log"
for marker in \
  M20_PROCESS_TELEMETRY_BOOTSTRAP_GATE=PASS \
  M20_TRACE_ARTIFACT_SECRET_REDACTION=PASS \
  M20_TRACE_SINGLE_TRACE_ID=PASS \
  M20_TRACE_PARENT_CHAIN=PASS \
  M20_TRACE_RESOURCE_IDENTITY=PASS \
  M20_TRACE_RPC_ATTRIBUTES=PASS \
  M20_TRACE_LOG_CORRELATION=PASS \
  M20_REAL_MULTIPROCESS_TRACE_E2E=PASS \
  M20_PROCESS_BOOTSTRAP_AND_REAL_TRACE_GATE=PASS
do
  grep -q "$marker" "$ARTIFACT_DIR/process-real-trace.log" || fail "missing marker: $marker"
done

log "post-acceptance integrity"
git diff --check | tee "$ARTIFACT_DIR/git-diff-check-final.log"
[[ -z "$(git diff --cached --name-only)" ]] || fail "acceptance unexpectedly changed Git index"
PROTECTED_AFTER="$(git diff -- "$PROTECTED_FILE" | sha256sum | awk '{print $1}')"
[[ "$PROTECTED_BEFORE" == "$PROTECTED_AFTER" ]] || fail "protected Gateway delta changed"

python3 - "$RELEASE_LIST" "$PROTECTED_FILE" <<'PY'
import subprocess, sys
release_list, protected = sys.argv[1:]
expected={x.strip() for x in open(release_list,encoding='utf-8') if x.strip() and not x.startswith('#')}
expected.add(protected)
status=subprocess.check_output(['git','status','--porcelain=v1','--untracked-files=all'],text=True)
actual=set()
for line in status.splitlines():
    path=line[3:]
    if ' -> ' in path:
        path=path.split(' -> ',1)[1]
    actual.add(path)
assert actual==expected, {'unexpected':sorted(actual-expected),'missing':sorted(expected-actual)}
print('M20_POST_ACCEPTANCE_WORKTREE=PASS')
PY

{
  echo "branch=$(git branch --show-current)"
  echo "precommit_head=$(git rev-parse HEAD)"
  echo "m19_parent=$M19_SHA"
  echo "artifact_dir=$ARTIFACT_DIR"
  echo "release_file_count=$(grep -cv '^\s*$' "$RELEASE_LIST")"
  echo "protected_delta_sha=$PROTECTED_AFTER"
  echo "dependency_contract=PASS"
  echo "foundation=PASS"
  echo "collector_prometheus=PASS"
  echo "distributed_tracing_core=PASS"
  echo "process_bootstrap=PASS"
  echo "real_multiprocess_trace=PASS"
  echo "log_trace_correlation=PASS"
  echo
  sha256sum "$RELEASE_LIST"
  for b in tinyimx_observability_demo tinyimx_mcp_server tinyimx_ai_agent_demo user_service_demo m20_trace_agent_e2e; do
    [[ -f "$BUILD_DIR/$b" ]] && sha256sum "$BUILD_DIR/$b"
  done
} > "$ARTIFACT_DIR/M20_ACCEPTANCE_EVIDENCE.txt"

df -h / > "$ARTIFACT_DIR/disk-post.txt"

echo
echo "=================================================="
echo "M20_FINAL_ACCEPTANCE=PASS"
echo "M20_RELEASE_CANDIDATE_READY_FOR_STAGING=PASS"
echo "M20_ACCEPTANCE_ARTIFACT_DIR=$ARTIFACT_DIR"
echo "=================================================="
echo "NOTE: M20 is accepted but not committed/tagged/frozen yet."
