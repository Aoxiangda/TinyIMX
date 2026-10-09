#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/tinyimx_capstone_common.sh"
STAMP="$(date +%Y%m%d-%H%M%S)"
ART="${TINYIMX_CAPSTONE_ARTIFACT_DIR:-$HOME/tinyimx-final-evidence/capstone-file-${STAMP}}"
BUILD="${TINYIMX_BUILD_DIR:-$ROOT/build/linux-release}"
mkdir -p "$ART"

capstone_require_clean_benchmark_host
capstone_wait_healthy file-service 60 || capstone_fail FILE_SERVICE_NOT_HEALTHY
cmake --build "$BUILD" --target file_transfer_release_e2e_client file_download_stress_client -- -j1 | tee "$ART/build.log"

FILE_CID="$(capstone_compose ps -q file-service)"
FILE_POLICY="$(docker inspect -f '{{.HostConfig.RestartPolicy.Name}}' "$FILE_CID")"
cleanup(){
  set +e
  docker update --restart="$FILE_POLICY" "$FILE_CID" >/dev/null 2>&1 || true
  [[ "$(docker inspect -f '{{.State.Running}}' "$FILE_CID" 2>/dev/null)" == true ]] || docker start "$FILE_CID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

target(){
  local ip
  ip="$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$FILE_CID")"
  [[ -n "$ip" ]] || capstone_fail FILE_SERVICE_IP_MISSING
  echo "$ip:50055"
}

STATE="$ART/restart-resume"
mkdir -p "$STATE"
"$BUILD/file_transfer_release_e2e_client" prepare "$(target)" "$STATE" 500001 | tee "$ART/prepare.log"
grep -q '\[PASS\] M18-C2 upload->finalize->partial-download before restart' "$ART/prepare.log" || capstone_fail FILE_PREPARE_FAILED

OLD_IMAGE="$(docker inspect -f '{{.Image}}' "$FILE_CID")"
docker update --restart=no "$FILE_CID" >/dev/null
docker kill "$FILE_CID" >/dev/null
sleep 2
docker start "$FILE_CID" >/dev/null
capstone_wait_healthy file-service 60 || capstone_fail FILE_SERVICE_RESTART_FAILED
NEW_IMAGE="$(docker inspect -f '{{.Image}}' "$FILE_CID")"
[[ "$NEW_IMAGE" == "$OLD_IMAGE" ]] || capstone_fail FILE_SERVICE_IMAGE_CHANGED_DURING_RECOVERY
"$BUILD/file_transfer_release_e2e_client" resume "$(target)" "$STATE" | tee "$ART/resume.log"
grep -q '\[PASS\] M18-C2 service-restart resume reconstructs exact immutable object' "$ART/resume.log" || capstone_fail FILE_RESUME_FAILED

docker update --restart="$FILE_POLICY" "$FILE_CID" >/dev/null

# Current Final runtime download scaling curve.
: > "$ART/download-summary.txt"
for threads in ${TINYIMX_FILE_READER_THREADS:-4 8 16 32}; do
  dir="$ART/readers-${threads}"
  M21_FILE_STRESS_ARTIFACT_DIR="$dir" \
    "$ROOT/scripts/m21_run_file_stress.sh" "$(target)" "$STATE" "$threads" 64 131072 \
      | tee "$ART/readers-${threads}.runner.log"
  grep -E 'throughput|MiB|M21_FILE_STRESS' "$dir/stress.log" "$ART/readers-${threads}.runner.log" 2>/dev/null \
    >> "$ART/download-summary.txt" || true
done

# Concurrent end-to-end transfer smoke: each worker performs upload/finalize/partial read.
: > "$ART/transfer-concurrency-summary.txt"
for workers in ${TINYIMX_FILE_TRANSFER_CONCURRENCY:-1 4 8}; do
  start_ms="$(capstone_epoch_ms)"
  pids=()
  for i in $(seq 1 "$workers"); do
    d="$ART/transfer-${workers}-${i}"; mkdir -p "$d"
    "$BUILD/file_transfer_release_e2e_client" prepare "$(target)" "$d" "$((500100+i))" > "$d/run.log" 2>&1 &
    pids+=("$!")
  done
  failures=0
  for p in "${pids[@]}"; do wait "$p" || failures=$((failures+1)); done
  end_ms="$(capstone_epoch_ms)"
  bytes="$(find "$ART" -maxdepth 1 -type d -name "transfer-${workers}-*" -exec sh -c 'for d; do stat -c %s "$d/expected.bin" 2>/dev/null || true; done' sh {} + | awk '{s+=$1} END{print s+0}')"
  elapsed=$((end_ms-start_ms))
  python3 - "$workers" "$bytes" "$elapsed" "$failures" <<'PY' | tee -a "$ART/transfer-concurrency-summary.txt"
import sys
w,b,ms,f=map(int,sys.argv[1:]); mib=b/1048576; sec=max(ms/1000,1e-9); print(f"workers={w} bytes={b} elapsed_ms={ms} aggregate_mib_s={mib/sec:.3f} failures={f}")
if f: raise SystemExit(42)
PY
done

sha256sum "$ART"/*.log "$ART"/*.txt "$STATE"/* 2>/dev/null > "$ART/SHA256SUMS" || true
echo "TINYIMX_CAPSTONE_FILE_GATE=PASS" | tee "$ART/PASS.marker"
echo "ARTIFACT_DIR=$ART"
