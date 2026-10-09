#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/tinyimx_capstone_common.sh"
STAMP="$(date +%Y%m%d-%H%M%S)"
ART="${TINYIMX_CAPSTONE_ARTIFACT_DIR:-$HOME/tinyimx-final-evidence/capstone-hotspot-group-${STAMP}}"
mkdir -p "$ART"

capstone_require_clean_benchmark_host

HOT_RATES="${TINYIMX_HOT_USER_RATES:-100 200 250 300}"
HOT_CONNS="${TINYIMX_HOT_USER_CONNECTIONS:-500}"
HOT_DUR="${TINYIMX_HOT_USER_DURATION:-60}"
BASE=610000
: > "$ART/hot-user-summary.txt"
for rate in $HOT_RATES; do
  dir="$ART/hot-user-${rate}"
  M21_HOT_USER_ARTIFACT_DIR="$dir" \
  "$ROOT/scripts/m21_run_hot_user_benchmark.sh" "$HOT_CONNS" "$HOT_DUR" "$rate" "$BASE" \
    | tee "$ART/hot-user-${rate}.runner.log"
  grep 'M21_HOT_USER_RESULT' "$ART/hot-user-${rate}.runner.log" >> "$ART/hot-user-summary.txt" || true
  BASE=$((BASE+2000))
  sleep 5
done

GROUP_SIZES="${TINYIMX_HOT_GROUP_SIZES:-10 50 100 500}"
GROUP_MESSAGES="${TINYIMX_HOT_GROUP_MESSAGES:-3}"
BASE=630000
: > "$ART/hot-group-summary.txt"
for members in $GROUP_SIZES; do
  dir="$ART/hot-group-${members}"
  M21_HOT_GROUP_ARTIFACT_DIR="$dir" \
  "$ROOT/scripts/m21_run_hot_group_benchmark.sh" "$members" "$GROUP_MESSAGES" 45 "$BASE" \
    | tee "$ART/hot-group-${members}.runner.log"
  grep 'M21_HOT_GROUP_RESULT' "$ART/hot-group-${members}.runner.log" >> "$ART/hot-group-summary.txt" || true
  BASE=$((BASE+2000))
  sleep 5
done

[[ -s "$ART/hot-user-summary.txt" ]] || capstone_fail HOT_USER_RESULTS_MISSING
[[ -s "$ART/hot-group-summary.txt" ]] || capstone_fail HOT_GROUP_RESULTS_MISSING
sha256sum "$ART"/*.txt "$ART"/*.log 2>/dev/null > "$ART/SHA256SUMS" || true
echo "TINYIMX_CAPSTONE_HOTSPOT_GROUP_GATE=PASS" | tee "$ART/PASS.marker"
echo "ARTIFACT_DIR=$ART"
