#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_CONFIG="${1:-config/gateway-a.local.json}"
if [[ "$SOURCE_CONFIG" != /* ]]; then SOURCE_CONFIG="$ROOT_DIR/$SOURCE_CONFIG"; fi
BUILD_DIR="${TINYIMX_BUILD_DIR:-$ROOT_DIR/build/linux-debug}"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="$ROOT_DIR/artifacts/m18-final-$TIMESTAMP"
mkdir -p "$ARTIFACT_DIR"
log(){ printf '[M18-FINAL] %s\n' "$*"; }
fail(){ printf '[M18-FINAL] FAIL: %s\n' "$*" >&2; exit 1; }
for c in cmake python3 mysql ss git sha256sum; do command -v "$c" >/dev/null || fail "missing command: $c"; done
[[ -f "$SOURCE_CONFIG" ]] || fail "config missing: $SOURCE_CONFIG"

log "static architecture/security/correctness gates"
grep -q 'VerifyObject' "$ROOT_DIR/services/file/storage/FileStoragePort.h" || fail "whole-object verification port missing"
grep -q 'O_NOFOLLOW' "$ROOT_DIR/services/file/storage/LocalFilesystemStorage.cpp" || fail "filesystem symlink fence missing"
grep -q 'ReadFileRange' "$ROOT_DIR/proto/tinyimx/file/v1/file_service.proto" || fail "range RPC missing"
grep -q 'FileService 8-method M18-C1 contract frozen' "$ROOT_DIR/tests/rpc/rpc_contract_test.cpp" || fail "8-method FileService contract gate missing"
grep -q 'file_transfer_release_e2e_client' "$ROOT_DIR/cmake/TinyIMXFileService.cmake" || fail "release E2E client target missing"
grep -q 'file_download_stress_client' "$ROOT_DIR/cmake/TinyIMXFileService.cmake" || fail "download stress target missing"
if grep -R -E -n 'im_file_download|download(ed)?_offset' "$ROOT_DIR/db/migrations" >/dev/null; then fail "server-side durable download progress unexpectedly introduced"; fi
if grep -R -E -n 'ReadFileRange|UploadChunk' "$ROOT_DIR/gateway" >/dev/null; then fail "large-file data plane leaked into Gateway ordinary path"; fi
log "PASS static-architecture-security"

log "source integrity before build"
(cd "$ROOT_DIR" && git diff --check) | tee "$ARTIFACT_DIR/git-diff-check-pre.log"
find "$ROOT_DIR" -type f \( -name '*.rej' -o -name '*.orig' \) -print | tee "$ARTIFACT_DIR/reject-orig-pre.log"
[[ ! -s "$ARTIFACT_DIR/reject-orig-pre.log" ]] || fail "reject/orig files present"
(cd "$ROOT_DIR" && { git branch --show-current; git rev-parse HEAD; git status --short; }) > "$ARTIFACT_DIR/git-state-pre.txt"

log "configure + focused M18 release build (-j${BUILD_JOBS})"
(cd "$ROOT_DIR" && cmake --preset linux-debug) | tee "$ARTIFACT_DIR/configure.log"
cmake --build "$BUILD_DIR" --target \
  rpc_contract_tests file_application_service_tests file_storage_tests file_service_integration_tests \
  file_repository_integration_tests file_chunk_integration_tests file_finalize_integration_tests file_download_integration_tests \
  file_service_demo file_transfer_release_e2e_client file_download_stress_client \
  -j"$BUILD_JOBS" | tee "$ARTIFACT_DIR/build-focused.log"
log "PASS focused-build"

ctest --test-dir "$BUILD_DIR" --output-on-failure -R '^(rpc_contract_tests|file_application_service_tests|file_storage_tests|file_service_integration_tests)$' \
  | tee "$ARTIFACT_DIR/ctest-focused.log"
log "PASS focused-ctest"

log "real MySQL/filesystem retained M18-A/B/C1 gates"
"$BUILD_DIR/file_repository_integration_tests" "$SOURCE_CONFIG" | tee "$ARTIFACT_DIR/a1-real-mysql.log"
grep -q '\[PASS\] M18-A1 file repository integration' "$ARTIFACT_DIR/a1-real-mysql.log" || fail "A1 marker missing"
"$BUILD_DIR/file_chunk_integration_tests" "$SOURCE_CONFIG" "$ARTIFACT_DIR/b1-storage" | tee "$ARTIFACT_DIR/b1-real.log"
grep -q '\[PASS\] M18-B1 durable chunk/filesystem integration' "$ARTIFACT_DIR/b1-real.log" || fail "B1 marker missing"
"$BUILD_DIR/file_finalize_integration_tests" "$SOURCE_CONFIG" "$ARTIFACT_DIR/b2-storage" | tee "$ARTIFACT_DIR/b2-real.log"
grep -q '\[PASS\] M18-B2 resume/finalize/filesystem integration' "$ARTIFACT_DIR/b2-real.log" || fail "B2 marker missing"
"$BUILD_DIR/file_download_integration_tests" "$SOURCE_CONFIG" "$ARTIFACT_DIR/c1-storage" | tee "$ARTIFACT_DIR/c1-real.log"
grep -q '\[PASS\] M18-C1 download authorization/range-resume integration' "$ARTIFACT_DIR/c1-real.log" || fail "C1 marker missing"
log "PASS retained-real-integration"

log "M18-C2 process restart / fault / stress gate"
"$ROOT_DIR/scripts/run_m18_c2_release_gate.sh" "$SOURCE_CONFIG" "$ARTIFACT_DIR/c2-release" | tee "$ARTIFACT_DIR/c2-release.log"
grep -q '\[M18-C2 PASS\]' "$ARTIFACT_DIR/c2-release.log" || fail "C2 release marker missing"
log "PASS c2-release-gate"

log "ordinary full build/test (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" -j"$BUILD_JOBS" | tee "$ARTIFACT_DIR/build-full.log"
ctest --test-dir "$BUILD_DIR" --output-on-failure | tee "$ARTIFACT_DIR/ctest-full.log"
grep -q '100% tests passed, 0 tests failed' "$ARTIFACT_DIR/ctest-full.log" || fail "full CTest summary missing"
log "PASS ordinary-regression"

(cd "$ROOT_DIR" && git diff --check) | tee "$ARTIFACT_DIR/git-diff-check-final.log"
find "$ROOT_DIR" -type f \( -name '*.rej' -o -name '*.orig' \) -print | tee "$ARTIFACT_DIR/reject-orig-final.log"
[[ ! -s "$ARTIFACT_DIR/reject-orig-final.log" ]] || fail "reject/orig files present after final"
{
  uname -a
  cmake --version | head -1
  c++ --version | head -1
  "$ROOT_DIR/vcpkg_installed/x64-linux/tools/protobuf/protoc" --version 2>/dev/null || true
  sha256sum "$BUILD_DIR/file_service_demo" "$BUILD_DIR/file_transfer_release_e2e_client" "$BUILD_DIR/file_download_stress_client"
} > "$ARTIFACT_DIR/runtime-build-evidence.txt"

BRANCH="$(cd "$ROOT_DIR" && git branch --show-current)"
HEAD_SHA="$(cd "$ROOT_DIR" && git rev-parse HEAD)"
cat > "$ARTIFACT_DIR/M18_CLOSEOUT_MANIFEST_ACCEPTED.md" <<EOF
# TinyIMX M18 Accepted Closeout Manifest

Status: **FINAL ACCEPTANCE PASS — Git/tag three-remote freeze pending**

Acceptance timestamp: ${TIMESTAMP}
Branch at acceptance: \`${BRANCH}\`
Pre-freeze HEAD: \`${HEAD_SHA}\`
Artifact directory: \`${ARTIFACT_DIR}\`

## Closed engineering scope
- M18-A — durable File Domain + authenticated Gateway control plane.
- M18-B — durable chunk identity, resume manifest, verified finalize.
- M18-C1 — authorized stateless bounded range download + reconnect resume.
- M18-C2 — process restart recovery, object-loss/corruption fencing, concurrent stress, full release E2E.

## Authoritative data ownership
- MySQL: durable file/upload/chunk metadata and lifecycle truth.
- Storage backend: immutable final bytes and chunk bytes.
- Gateway: authenticated control plane only; large bytes do not traverse the ordinary IM message path.
- Download resume: stateless client offset + immutable SHA-256 validator; no durable server download offset/session.

## Final release evidence
- Focused build/CTest: PASS.
- Real MySQL M18-A1 regression: PASS.
- Real MySQL/filesystem M18-B1: PASS.
- Resume/finalize/whole-file M18-B2: PASS.
- Authorized Range Resume M18-C1: PASS.
- Real FileService process stop/restart and resumed download: PASS.
- Missing final object: fail-closed DATA_LOSS, recovery verified.
- Same-size final object corruption: whole-object SHA-256 detects corruption, recovery verified.
- Concurrent 4/8/16-reader bounded range stress: zero correctness failures.
- Ordinary full build + CTest: PASS.

## Release freeze rule
M18 becomes **CLOSED** only after the accepted source tree is committed, annotated tag \`m18-final-20260922\` is created, and branch + peeled tag SHA are identical across gitlab/origin/gitee.
EOF

printf '\n[M18 FINAL PASS] Durable file upload/finalize/download + restart/fault recovery release accepted.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
printf 'next: commit accepted C2 tree, create annotated tag m18-final-20260922, push branch+tag to three remotes, run verify_m18_final_freeze.sh\n'
