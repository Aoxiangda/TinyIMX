#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
PROTECTED="gateway/GatewayPeerTransport.h"
cd "$ROOT"
test "$(git branch --show-current)" = "feature/m21-production-release-v1"
test -z "$(git diff --cached --name-only)"
PROTECTED_BEFORE="$(git diff -- "$PROTECTED" | sha256sum | awk '{print $1}')"
echo "========== M21 DISK BEFORE =========="
df -h /
if [[ -d build/linux-debug ]]; then
  echo "Removing rebuildable frozen M20 Debug tree: build/linux-debug"
  rm -rf build/linux-debug
fi
if [[ -d "${HOME}/.cache/vscode-cpptools" ]]; then
  echo "Removing rebuildable VS Code C/C++ cache"
  rm -rf "${HOME}/.cache/vscode-cpptools"/*
fi
PROTECTED_AFTER="$(git diff -- "$PROTECTED" | sha256sum | awk '{print $1}')"
test "$PROTECTED_BEFORE" = "$PROTECTED_AFTER"
echo "========== M21 DISK AFTER =========="
df -h /
echo "PROTECTED_GATEWAY_DELTA=PASS"
echo "M21_WORKSPACE_PREPARE=PASS"
