#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="${1:-}"
EXPECTED_BRANCH="feature/m20-observability-v1"
M19_SHA="3f42a43f91da6a83fd7a73b10630d48283a06005"
PROTECTED_FILE="gateway/GatewayPeerTransport.h"
RELEASE_LIST="$ROOT_DIR/scripts/m20_release_files.txt"

[[ -n "$TAG" ]] || { echo "Usage: $0 m20-final-YYYYMMDD-rN" >&2; exit 2; }
[[ "$TAG" =~ ^m20-final-[0-9]{8}-r[0-9]+$ ]] || { echo "FAIL invalid M20 tag format"; exit 3; }

cd "$ROOT_DIR"
BRANCH="$(git branch --show-current)"
HEAD_SHA="$(git rev-parse HEAD)"
TAG_SHA="$(git rev-parse "$TAG^{}")"
PARENT_SHA="$(git rev-parse HEAD^)"

[[ "$BRANCH" == "$EXPECTED_BRANCH" ]] || { echo "FAIL branch=$BRANCH"; exit 10; }
[[ "$TAG_SHA" == "$HEAD_SHA" ]] || { echo "FAIL local tag does not peel to HEAD"; exit 11; }
[[ "$PARENT_SHA" == "$M19_SHA" ]] || { echo "FAIL M20 release commit parent is not frozen M19 SHA"; exit 12; }
[[ -z "$(git diff --cached --name-only)" ]] || { echo "FAIL staged files remain"; exit 13; }
[[ -n "$(git diff -- "$PROTECTED_FILE")" ]] || { echo "FAIL protected local delta missing"; exit 14; }

python3 - "$RELEASE_LIST" "$M19_SHA" "$HEAD_SHA" "$PROTECTED_FILE" <<'PY'
import subprocess,sys
release,base,head,protected=sys.argv[1:]
expected=sorted(x.strip() for x in open(release,encoding='utf-8') if x.strip() and not x.startswith('#'))
committed=sorted(subprocess.check_output(['git','diff','--name-only',f'{base}..{head}'],text=True).splitlines())
assert committed==expected, {'unexpected':sorted(set(committed)-set(expected)),'missing':sorted(set(expected)-set(committed))}
status=subprocess.check_output(['git','status','--porcelain=v1','--untracked-files=all'],text=True).splitlines()
paths=[line[3:].split(' -> ',1)[-1] for line in status if line]
assert paths==[protected], paths
print(f'M20_COMMITTED_FILESET=PASS files={len(expected)}')
PY

for remote in gitlab origin gitee; do
  git remote get-url "$remote" >/dev/null 2>&1 || { echo "FAIL missing remote: $remote"; exit 20; }
  echo "========== VERIFY $remote =========="
  branch_sha="$(git ls-remote "$remote" "refs/heads/$EXPECTED_BRANCH" | awk 'NR==1{print $1}')"
  tag_sha="$(git ls-remote "$remote" "refs/tags/$TAG^{}" | awk 'NR==1{print $1}')"
  if [[ -z "$tag_sha" ]]; then
    tag_sha="$(git ls-remote "$remote" "refs/tags/$TAG" | awk 'NR==1{print $1}')"
  fi
  echo "branch=$branch_sha"
  echo "tag_peeled=$tag_sha"
  [[ "$branch_sha" == "$HEAD_SHA" ]] || { echo "FAIL $remote branch mismatch"; exit 21; }
  [[ "$tag_sha" == "$HEAD_SHA" ]] || { echo "FAIL $remote tag mismatch"; exit 22; }
done

echo
echo "M20_FINAL_FREEZE=PASS"
echo "commit=$HEAD_SHA"
echo "parent=$PARENT_SHA"
echo "tag=$TAG"
echo "protected_unrelated_delta=$PROTECTED_FILE"
