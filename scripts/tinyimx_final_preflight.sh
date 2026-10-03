#!/usr/bin/env bash
set -Eeuo pipefail

MODE="${1:-benchmark}"
[[ "$MODE" == "build" || "$MODE" == "benchmark" ]] || { echo "usage: $0 [build|benchmark]" >&2; exit 64; }

fail(){ echo "FIRST_FAILURE=$1"; exit "${2:-1}"; }

CPUS="$(nproc)"
MEM_KB="$(awk '/MemTotal:/ {print $2}' /proc/meminfo)"
MEM_GIB=$(( MEM_KB / 1024 / 1024 ))
FREE_BYTES="$(df -B1 --output=avail / | tail -n1 | tr -d ' ')"
SWAP_USED_KB="$(awk '/SwapTotal:/ {t=$2} /SwapFree:/ {f=$2} END {print t-f}' /proc/meminfo)"
SOFT_NOFILE="$(ulimit -Sn)"
HARD_NOFILE="$(ulimit -Hn)"
SOMAX="$(sysctl -n net.core.somaxconn)"
SYNMAX="$(sysctl -n net.ipv4.tcp_max_syn_backlog)"

printf 'cpus=%s\nmem_gib=%s\nroot_free_gib=%s\nsoft_nofile=%s\nhard_nofile=%s\nsomaxconn=%s\ntcp_max_syn_backlog=%s\nswap_used_kib=%s\n' \
  "$CPUS" "$MEM_GIB" "$((FREE_BYTES/1024/1024/1024))" "$SOFT_NOFILE" "$HARD_NOFILE" "$SOMAX" "$SYNMAX" "$SWAP_USED_KB"

(( CPUS >= 8 )) || fail "CPU_LT_8"
(( MEM_KB >= 14*1024*1024 )) || fail "MEM_LT_14G"
(( FREE_BYTES >= 20*1024*1024*1024 )) || fail "ROOT_FREE_LT_20G"
(( HARD_NOFILE >= 262144 )) || fail "HARD_NOFILE_LT_262144"
(( SOMAX >= 16384 )) || fail "SOMAXCONN_LT_16384"
(( SYNMAX >= 16384 )) || fail "SYN_BACKLOG_LT_16384"

if [[ "$MODE" == "benchmark" ]]; then
  (( SOFT_NOFILE >= 262144 )) || fail "SOFT_NOFILE_LT_262144"
  (( SWAP_USED_KB <= 64*1024 )) || fail "SWAP_IN_USE_FOR_BENCHMARK"
fi

command -v docker >/dev/null 2>&1 || fail "DOCKER_MISSING"
docker info >/dev/null 2>&1 || fail "DOCKER_DAEMON_UNAVAILABLE"

if ip -4 addr show dev ens37 >/dev/null 2>&1; then
  ip -4 addr show dev ens37 | awk '/inet / {print "benchmark_nic_ens37=" $2}'
fi

echo "TINYIMX_FINAL_PREFLIGHT=PASS mode=$MODE"
