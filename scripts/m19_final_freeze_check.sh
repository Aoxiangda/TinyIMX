#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="${1:-}"
EXPECTED_BRANCH="feature/m19-ai-mcpserver-v1"
PROTECTED_FILE="gateway/GatewayPeerTransport.h"

[[ -n "$TAG" ]] || {
  echo "Usage: $0 m19-final-YYYYMMDD-rN" >&2
  exit 2
}

cd "$ROOT_DIR"

BRANCH="$(git branch --show-current)"
HEAD_SHA="$(git rev-parse HEAD)"
TAG_SHA="$(git rev-parse "$TAG^{}")"

[[ "$BRANCH" == "$EXPECTED_BRANCH" ]] || { echo "FAIL branch=$BRANCH"; exit 10; }
[[ "$TAG_SHA" == "$HEAD_SHA" ]] || { echo "FAIL local tag does not peel to HEAD"; exit 11; }
[[ -z "$(git diff --cached --name-only)" ]] || { echo "FAIL staged files remain"; exit 12; }
[[ -n "$(git diff -- "$PROTECTED_FILE")" ]] || { echo "FAIL protected local delta missing"; exit 13; }

for remote in gitlab origin gitee; do
  echo "========== VERIFY $remote =========="
  branch_sha="$(git ls-remote "$remote" "refs/heads/$EXPECTED_BRANCH" | awk 'NR==1{print $1}')"
  tag_sha="$(git ls-remote "$remote" "refs/tags/$TAG^{}" | awk 'NR==1{print $1}')"
  if [[ -z "$tag_sha" ]]; then
    tag_sha="$(git ls-remote "$remote" "refs/tags/$TAG" | awk 'NR==1{print $1}')"
  fi
  echo "branch=$branch_sha"
  echo "tag_peeled=$tag_sha"
  [[ "$branch_sha" == "$HEAD_SHA" ]] || { echo "FAIL $remote branch mismatch"; exit 20; }
  [[ "$tag_sha" == "$HEAD_SHA" ]] || { echo "FAIL $remote tag mismatch"; exit 21; }
done

echo
echo "M19_FINAL_FREEZE=PASS"
echo "commit=$HEAD_SHA"
echo "tag=$TAG"
echo "protected_unrelated_delta=$PROTECTED_FILE"
