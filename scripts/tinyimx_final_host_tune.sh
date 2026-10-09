#!/usr/bin/env bash
set -Eeuo pipefail

if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  echo "ERROR: run with sudo/root" >&2
  exit 2
fi

cat >/etc/sysctl.d/99-tinyimx-performance.conf <<'SYSCTL'
# TinyIMX Final Candidate: accept-queue headroom for 10k+ long connections.
net.core.somaxconn = 16384
net.ipv4.tcp_max_syn_backlog = 16384
SYSCTL

cat >/etc/security/limits.d/99-tinyimx-nofile.conf <<'LIMITS'
*    soft    nofile    524288
*    hard    nofile    524288
root soft    nofile    524288
root hard    nofile    524288
LIMITS

sysctl --system >/tmp/tinyimx-sysctl-apply.log
cat /tmp/tinyimx-sysctl-apply.log

echo "somaxconn=$(sysctl -n net.core.somaxconn)"
echo "tcp_max_syn_backlog=$(sysctl -n net.ipv4.tcp_max_syn_backlog)"
echo "nofile limits persisted in /etc/security/limits.d/99-tinyimx-nofile.conf"
echo "NOTE: log out/in (or reboot) for interactive shell nofile limits to refresh."
echo "TINYIMX_FINAL_HOST_TUNE=PASS"
