#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="${1:-m18-final-20260922-r1}"
BRANCH="${2:-feature/m18-file-transfer-v1}"
cd "$ROOT_DIR"
fail(){ printf '[M18-FREEZE] FAIL: %s\n' "$*" >&2; exit 1; }
EXPECTED="$(git rev-parse HEAD)"
[[ "$(git branch --show-current)" == "$BRANCH" ]] || fail "unexpected branch"
LOCAL_TAG="$(git rev-list -n 1 "$TAG" 2>/dev/null || true)"
[[ "$LOCAL_TAG" == "$EXPECTED" ]] || fail "local tag $TAG does not resolve to HEAD"
git diff --cached --quiet || fail "staged changes remain after final commit"
UNEXPECTED="$(git status --porcelain | grep -v '^ M gateway/GatewayPeerTransport.h$' || true)"
[[ -z "$UNEXPECTED" ]] || { printf '%s\n' "$UNEXPECTED" >&2; fail "unexpected local changes after final commit"; }
for remote in gitlab origin gitee; do
  branch_sha="$(git ls-remote "$remote" "refs/heads/$BRANCH" | awk 'NR==1{print $1}')"
  [[ "$branch_sha" == "$EXPECTED" ]] || fail "$remote branch SHA mismatch: $branch_sha"
  peeled="$(git ls-remote "$remote" "refs/tags/$TAG^{}" | awk 'NR==1{print $1}')"
  if [[ -z "$peeled" ]]; then peeled="$(git ls-remote "$remote" "refs/tags/$TAG" | awk 'NR==1{print $1}')"; fi
  [[ "$peeled" == "$EXPECTED" ]] || fail "$remote tag SHA mismatch: $peeled"
  echo "[PASS] $remote branch+tag -> $EXPECTED"
done
printf '\n[M18 FINAL FREEZE PASS] branch=%s tag=%s sha=%s\n' "$BRANCH" "$TAG" "$EXPECTED"
