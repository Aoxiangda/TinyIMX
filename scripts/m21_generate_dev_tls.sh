#!/usr/bin/env bash
set -Eeuo pipefail
STATE_DIR="${TINYIMX_M21_STATE_DIR:-${HOME}/.local/share/tinyimx/m21}"
TLS_DIR="${STATE_DIR}/tls"
command -v openssl >/dev/null 2>&1 || { echo "ERROR: openssl not found"; exit 10; }
mkdir -p "$TLS_DIR"
umask 077
openssl req -x509 -newkey rsa:3072 -sha256 -nodes \
  -days 30 \
  -subj "/CN=tinyimx.local" \
  -addext "subjectAltName=DNS:tinyimx.local,DNS:localhost,IP:127.0.0.1" \
  -keyout "$TLS_DIR/tinyimx.key" \
  -out "$TLS_DIR/tinyimx.crt"
chmod 600 "$TLS_DIR/tinyimx.key"
chmod 644 "$TLS_DIR/tinyimx.crt"
echo "M21_DEV_TLS=PASS"
echo "TLS_DIR=$TLS_DIR"
echo "NOTE: replace this self-signed certificate before public deployment."
