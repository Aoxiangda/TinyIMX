#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

failed=0

echo "=============================================="
echo " TinyIMX Git Preflight"
echo "=============================================="

echo
echo "[1/5] forbidden tracked files"

forbidden_tracked="$(
  git ls-files | grep -E \
  '(^|/)(build|artifacts|logs|Testing|vcpkg_installed|toolchains)/|(^|/)SHA256SUMS(\.txt)?$|BASELINE_SHA256SUMS|CANDIDATE_SHA256SUMS|REPLACEMENT_MANIFEST|README_M.*_APPLY|M[0-9]+.*_AUDIT|(^|/)apply_m[0-9]+|(^|/)verify_m[0-9]+' \
  || true
)"

if [[ -n "$forbidden_tracked" ]]; then
  echo "[FAIL] forbidden files are tracked:"
  echo "$forbidden_tracked"
  failed=1
else
  echo "[PASS] no forbidden tracked files"
fi


echo
echo "[2/5] forbidden staged files"

forbidden_staged="$(
  git diff --cached --name-only | grep -E \
  '(^|/)(build|artifacts|logs|Testing|vcpkg_installed|toolchains)/|SHA256|AUDIT|APPLY|REPLACEMENT_MANIFEST|existing_files\.diff|\.local\.(json|ya?ml)$' \
  || true
)"

if [[ -n "$forbidden_staged" ]]; then
  echo "[FAIL] forbidden staged files:"
  echo "$forbidden_staged"
  failed=1
else
  echo "[PASS] no forbidden staged files"
fi


echo
echo "[3/5] untracked source-like files"

untracked_sources="$(
  git ls-files --others --exclude-standard |
  grep -E \
  '(^|/)(CMakeLists\.txt|vcpkg\.json)$|\.(cpp|cc|cxx|c|h|hpp|proto|cmake)$|(^|/)scripts/.*\.sh$' \
  || true
)"

if [[ -n "$untracked_sources" ]]; then
  echo "[FAIL] source-like files remain untracked:"
  echo "$untracked_sources"
  echo
  echo "These may be files you forgot to git add."
  failed=1
else
  echo "[PASS] no suspicious untracked source files"
fi


echo
echo "[4/5] executable-bit sanity"

bad_exec="$(
  git ls-files -s |
  awk '
    $1 == "100755" &&
    $4 ~ /\.(cpp|cc|cxx|c|h|hpp|proto|cmake)$/ {
      print $4
    }
  '
)"

if [[ -n "$bad_exec" ]]; then
  echo "[FAIL] source files incorrectly marked executable:"
  echo "$bad_exec"
  failed=1
else
  echo "[PASS] source executable bits are clean"
fi


echo
echo "[5/5] local config / secret-name sanity"

bad_local="$(
  git ls-files |
  grep -E \
  '(^|/)config/.*\.local\.(json|ya?ml)$|(^|/)config/gateway\.json$|(^|/)\.env$' \
  || true
)"

if [[ -n "$bad_local" ]]; then
  echo "[FAIL] local configuration appears tracked:"
  echo "$bad_local"
  failed=1
else
  echo "[PASS] no local configuration tracked"
fi


echo
echo "=============================================="

if [[ "$failed" -ne 0 ]]; then
  echo "[PRE-FLIGHT FAIL] Git repository is not safe to commit/push."
  exit 1
fi

echo "[PRE-FLIGHT PASS] Git repository is safe for commit/push."
