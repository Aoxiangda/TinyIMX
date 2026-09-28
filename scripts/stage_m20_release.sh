#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXPECTED_BRANCH="feature/m20-observability-v1"
M19_SHA="3f42a43f91da6a83fd7a73b10630d48283a06005"
PROTECTED_FILE="gateway/GatewayPeerTransport.h"
RELEASE_LIST="$ROOT_DIR/scripts/m20_release_files.txt"

cd "$ROOT_DIR"

[[ "$(git branch --show-current)" == "$EXPECTED_BRANCH" ]] || { echo "FAIL unexpected branch"; exit 10; }
[[ "$(git rev-parse HEAD)" == "$M19_SHA" ]] || { echo "FAIL pre-commit HEAD is not M19 SHA"; exit 11; }
[[ -z "$(git diff --cached --name-only)" ]] || { echo "FAIL index must be empty"; exit 12; }
[[ -n "$(git diff -- "$PROTECTED_FILE")" ]] || { echo "FAIL protected Gateway delta missing"; exit 13; }
[[ -f "$RELEASE_LIST" ]] || { echo "FAIL release list missing"; exit 14; }

PROTECTED_BEFORE="$(git diff -- "$PROTECTED_FILE" | sha256sum | awk '{print $1}')"

python3 - "$RELEASE_LIST" "$PROTECTED_FILE" <<'PY'
import subprocess,sys
release,protected=sys.argv[1:]
expected={x.strip() for x in open(release,encoding='utf-8') if x.strip() and not x.startswith('#')}
expected.add(protected)
status=subprocess.check_output(['git','status','--porcelain=v1','--untracked-files=all'],text=True)
actual={line[3:].split(' -> ',1)[-1] for line in status.splitlines() if line}
assert actual==expected, {'unexpected':sorted(actual-expected),'missing':sorted(expected-actual)}
print('M20_PRESTAGE_WORKTREE=PASS')
PY

while IFS= read -r path; do
  [[ -n "$path" && "$path" != \#* ]] || continue
  git add -- "$path"
done < "$RELEASE_LIST"

git diff --cached --check

python3 - "$RELEASE_LIST" "$PROTECTED_FILE" <<'PY'
import subprocess,sys
release,protected=sys.argv[1:]
expected=sorted(x.strip() for x in open(release,encoding='utf-8') if x.strip() and not x.startswith('#'))
staged=sorted(subprocess.check_output(['git','diff','--cached','--name-only'],text=True).splitlines())
assert staged==expected, {'unexpected':sorted(set(staged)-set(expected)),'missing':sorted(set(expected)-set(staged))}
unstaged=subprocess.check_output(['git','diff','--name-only'],text=True).splitlines()
assert unstaged==[protected], unstaged
untracked=subprocess.check_output(['git','ls-files','--others','--exclude-standard'],text=True).splitlines()
assert not untracked, untracked
print(f'M20_STAGED_FILESET=PASS files={len(expected)}')
PY

PROTECTED_AFTER="$(git diff -- "$PROTECTED_FILE" | sha256sum | awk '{print $1}')"
[[ "$PROTECTED_BEFORE" == "$PROTECTED_AFTER" ]] || { echo "FAIL protected delta changed"; exit 20; }
if git diff --cached --name-only | grep -Fxq "$PROTECTED_FILE"; then
  echo "FAIL protected Gateway file staged"
  exit 21
fi

echo
echo "M20_EXACT_STAGING=PASS"
echo "staged_files=$(git diff --cached --name-only | wc -l)"
echo "protected_unstaged=$PROTECTED_FILE"
echo "NOTE: review 'git diff --cached' before committing."
