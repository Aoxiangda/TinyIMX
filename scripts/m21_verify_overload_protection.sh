#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT_DIR"; BUILD_DIR="${M21_BUILD_DIR:-$ROOT_DIR/build/linux-release}"; STAMP="$(date +%Y%m%d-%H%M%S)"; ART="${M21_OVERLOAD_ARTIFACT_DIR:-$ROOT_DIR/artifacts/m21-overload-gate-$STAMP}"; mkdir -p "$ART"
grep -q max_pending_tasks gateway/business/BusinessExecutor.h; grep -q per_stripe_queue_capacity gateway/business/BusinessExecutor.h; grep -q rejected_overload_total gateway/business/BusinessExecutor.h; grep -q rejected_hot_key_total gateway/business/BusinessExecutor.h; grep -q kHotKeyOverloaded gateway/business/BusinessExecutor.cpp
cmake --build "$BUILD_DIR" --target business_runtime_tests business_runtime_acceptance_tests business_runtime_benchmark -- -j1|tee "$ART/build.log"
"$BUILD_DIR/business_runtime_tests"|tee "$ART/unit.log"
"$BUILD_DIR/business_runtime_acceptance_tests"|tee "$ART/acceptance.log"
"$BUILD_DIR/business_runtime_benchmark" --workers 4 --tasks 512 --work-ms 50 --max-pending 128 --stripes 64 --per-stripe 32 --ordering unordered --keys 1 --submitters 4 --case m21-enterprise-overload|tee "$ART/global.log"
R="$(awk -F= '/rejected_overload/{gsub(/[[:space:]]/,"",$2);v=$2}END{print v+0}' "$ART/global.log")"; ((R>0))||{ echo "overload rejection not observed" >&2;exit 1; }
"$BUILD_DIR/business_runtime_benchmark" --workers 4 --tasks 128 --work-ms 50 --max-pending 128 --stripes 64 --per-stripe 32 --ordering striped --keys 1 --submitters 4 --case m21-enterprise-hot-key|tee "$ART/hot-key.log"
H="$(awk -F= '/rejected_hot_key/{gsub(/[[:space:]]/,"",$2);v=$2}END{print v+0}' "$ART/hot-key.log")"; ((H>0))||{ echo "hot-key rejection not observed" >&2;exit 1; }
echo "M21_BUSINESS_BACKPRESSURE_GATE=PASS global_rejected=$R hot_key_rejected=$H"
echo "M21_OVERLOAD_ARTIFACT_DIR=$ART"
