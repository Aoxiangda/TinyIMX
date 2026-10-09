#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
PROTECTED="gateway/GatewayPeerTransport.h"
cd "$ROOT"
test "$(git -C "$ROOT" rev-parse --show-toplevel)" = "$ROOT"
test -z "$(git diff --cached --name-only)"
PROTECTED_BEFORE="$(git diff -- "$PROTECTED" | sha256sum | awk '{print $1}')"
echo "========== M21 READ-ONLY WORKSPACE AUDIT =========="
df -h /
if [[ -d build/linux-debug ]]; then
  du -sh build/linux-debug
fi
if [[ -d "${HOME}/.cache/vscode-cpptools" ]]; then
  du -sh "${HOME}/.cache/vscode-cpptools"
fi
PROTECTED_AFTER="$(git diff -- "$PROTECTED" | sha256sum | awk '{print $1}')"
test "$PROTECTED_BEFORE" = "$PROTECTED_AFTER"
echo "No files, build trees or editor caches were removed."
echo "PROTECTED_GATEWAY_DELTA=PASS"
echo "M21_WORKSPACE_PREPARE=PASS"
