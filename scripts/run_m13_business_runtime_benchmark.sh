#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/linux-release"
BENCHMARK_BIN="${BUILD_DIR}/business_runtime_benchmark"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m13-c1-benchmark-${TIMESTAMP}"
RAW_DIR="${ARTIFACT_DIR}/raw"
SUMMARY_CSV="${ARTIFACT_DIR}/summary.csv"
SUMMARY_TSV="${ARTIFACT_DIR}/summary.tsv"

mkdir -p "${RAW_DIR}"

cd "${ROOT_DIR}"

echo "[M13-C1] configuring linux-release preset"
cmake --preset linux-release

echo "[M13-C1] building business_runtime_benchmark"
cmake --build --preset build-release \
  --target business_runtime_benchmark \
  -j2

if [[ ! -x "${BENCHMARK_BIN}" ]]; then
  echo "[M13-C1] benchmark executable missing: ${BENCHMARK_BIN}" >&2
  exit 1
fi

run_case() {
  local case_name="$1"
  shift

  local log_file="${RAW_DIR}/${case_name}.log"

  echo
  echo "[M13-C1] RUN ${case_name}"

  "${BENCHMARK_BIN}" \
    --case "${case_name}" \
    --csv "${SUMMARY_CSV}" \
    "$@" 2>&1 | tee "${log_file}"

  local status=${PIPESTATUS[0]}
  if [[ ${status} -ne 0 ]]; then
    echo "[M13-C1] FAIL ${case_name}, status=${status}" >&2
    exit "${status}"
  fi

  echo "[M13-C1] PASS ${case_name}"
}

# -----------------------------------------------------------------------------
# 0. Frozen production baseline after M13-C1 parameter finalization.
#    Keep the diagnostic sweeps below unchanged; this case makes future
#    re-runs directly comparable with the deployed baseline.
# -----------------------------------------------------------------------------
run_case \
  "production-baseline" \
  --workers 4 \
  --tasks 256 \
  --work-ms 10 \
  --max-pending 128 \
  --stripes 64 \
  --per-stripe 32 \
  --ordering unordered \
  --keys 1 \
  --submitters 2

# -----------------------------------------------------------------------------
# 1. Worker-count sweep: pure unordered capacity scaling.
# -----------------------------------------------------------------------------
for workers in 1 2 4 8; do
  run_case \
    "workers-${workers}" \
    --workers "${workers}" \
    --tasks 256 \
    --work-ms 10 \
    --max-pending 512 \
    --stripes 64 \
    --per-stripe 128 \
    --ordering unordered \
    --keys 1 \
    --submitters 2
done

# -----------------------------------------------------------------------------
# 2. Simulated blocking-service-time sweep.
# -----------------------------------------------------------------------------
for work_ms in 1 5 10 50; do
  run_case \
    "service-${work_ms}ms" \
    --workers 4 \
    --tasks 256 \
    --work-ms "${work_ms}" \
    --max-pending 512 \
    --stripes 64 \
    --per-stripe 128 \
    --ordering unordered \
    --keys 1 \
    --submitters 2
done

# -----------------------------------------------------------------------------
# 3. Ordering-domain sweep.  Keep per-stripe capacity high enough that this
#    group measures serialization cost rather than hot-key rejection.
# -----------------------------------------------------------------------------
for keys in 1 4 16 64 1024; do
  run_case \
    "ordering-keys-${keys}" \
    --workers 4 \
    --tasks 128 \
    --work-ms 10 \
    --max-pending 512 \
    --stripes 64 \
    --per-stripe 256 \
    --ordering striped \
    --keys "${keys}" \
    --submitters 2
done

# -----------------------------------------------------------------------------
# 4. Dedicated hot-key isolation case.
# -----------------------------------------------------------------------------
run_case \
  "hot-key" \
  --workers 4 \
  --tasks 256 \
  --work-ms 25 \
  --max-pending 256 \
  --stripes 64 \
  --per-stripe 16 \
  --ordering striped \
  --keys 1 \
  --submitters 2

HOT_REJECTED="$(
  awk -F '=' '
    /rejected_hot_key/ {
      gsub(/[[:space:]]/, "", $2);
      value=$2
    }
    END { print value+0 }
  ' "${RAW_DIR}/hot-key.log"
)"

if [[ "${HOT_REJECTED}" -le 0 ]]; then
  echo "[M13-C1] hot-key case did not trigger hot-key rejection" >&2
  exit 1
fi

# -----------------------------------------------------------------------------
# 5. Dedicated global-overload case.
# -----------------------------------------------------------------------------
run_case \
  "global-overload" \
  --workers 4 \
  --tasks 512 \
  --work-ms 50 \
  --max-pending 128 \
  --stripes 64 \
  --per-stripe 128 \
  --ordering unordered \
  --keys 1 \
  --submitters 4

OVERLOAD_REJECTED="$(
  awk -F '=' '
    /rejected_overload/ {
      gsub(/[[:space:]]/, "", $2);
      value=$2
    }
    END { print value+0 }
  ' "${RAW_DIR}/global-overload.log"
)"

if [[ "${OVERLOAD_REJECTED}" -le 0 ]]; then
  echo "[M13-C1] overload case did not trigger global rejection" >&2
  exit 1
fi

# -----------------------------------------------------------------------------
# 6. Global pending-capacity sweep.
# -----------------------------------------------------------------------------
for pending in 64 128 256 512 1024; do
  per_stripe=128
  if [[ "${pending}" -lt "${per_stripe}" ]]; then
    per_stripe="${pending}"
  fi

  run_case \
    "pending-${pending}" \
    --workers 4 \
    --tasks 512 \
    --work-ms 10 \
    --max-pending "${pending}" \
    --stripes 64 \
    --per-stripe "${per_stripe}" \
    --ordering unordered \
    --keys 1 \
    --submitters 4
done

# -----------------------------------------------------------------------------
# 7. Stripe-count sweep under distributed ordering keys.
# -----------------------------------------------------------------------------
for stripes in 16 32 64 128; do
  run_case \
    "stripes-${stripes}" \
    --workers 4 \
    --tasks 256 \
    --work-ms 10 \
    --max-pending 512 \
    --stripes "${stripes}" \
    --per-stripe 128 \
    --ordering striped \
    --keys 128 \
    --submitters 2
done

tr ',' '\t' < "${SUMMARY_CSV}" > "${SUMMARY_TSV}"

cat > "${ARTIFACT_DIR}/README.txt" <<REPORT
TinyIMX M13-C1 Business Runtime Benchmark

Generated: ${TIMESTAMP}
Binary: ${BENCHMARK_BIN}

Artifacts:
- summary.csv : machine-readable parameter matrix
- summary.tsv : tab-separated copy
- raw/        : one complete log per workload

Required targeted conditions observed:
- hot-key rejected tasks = ${HOT_REJECTED}
- global overload rejected tasks = ${OVERLOAD_REJECTED}

Interpretation and final production parameter selection are intentionally
performed after reviewing the whole matrix.  This script does not silently
rewrite runtime defaults.
REPORT

echo
echo "[M13-C1 PASS] benchmark matrix completed"
echo "[M13-C1] hot-key rejected=${HOT_REJECTED}"
echo "[M13-C1] overload rejected=${OVERLOAD_REJECTED}"
echo "[M13-C1] summary: ${SUMMARY_CSV}"
echo "[M13-C1] artifacts: ${ARTIFACT_DIR}"
