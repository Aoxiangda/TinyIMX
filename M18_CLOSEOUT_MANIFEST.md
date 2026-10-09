# TinyIMX M18 Closeout Manifest

Status: **RELEASE CANDIDATE — final acceptance and Git/tag freeze required**

## Scope

M18 closes the File Transfer domain as an enterprise-style microservice slice:

- M18-A: durable file metadata/upload session + authenticated Gateway control plane.
- M18-B: durable chunk manifest, retry/resume, verified atomic finalization.
- M18-C1: authorized stateless bounded Range Read + reconnect resume.
- M18-C2: process restart/fault recovery, immutable-object integrity, concurrency/stress evidence, full release E2E.

## Architecture boundary

Control plane:

`Client -> authenticated ingress/Gateway -> FileService -> MySQL`

Data plane:

`Client/data-plane ingress -> FileService -> FileStoragePort -> storage backend`

Large file bytes MUST NOT traverse the ordinary Gateway -> BusinessExecutor -> MessageService/RocketMQ chat-message path.

## Durable truth

- MySQL is authoritative for file ownership, metadata, upload lifecycle and chunk manifest.
- Storage is authoritative for bytes.
- A DB file row alone does not imply a usable file.
- Only an AVAILABLE file with matching expected/verified SHA-256 and a verified immutable final object is downloadable.
- Download resume is intentionally stateless: `(file_id, verified_sha256, next_offset)`; no persisted `download_offset` or download-session table.

## Reliability / fault semantics

- Upload retry uses durable business identity, never Packet.seq.
- Finalize publishes an atomic immutable object before AVAILABLE is committed.
- Service process restart does not change download identity or resume offset semantics.
- Missing/corrupt AVAILABLE bytes fail closed as DATA_LOSS.
- Same-size corruption is detected by whole-object SHA-256 verification on download open/resume.
- Restoring the exact object permits revalidation and download recovery.
- Range reads stay bounded to 1 MiB and stateless under concurrent readers.

## Final release acceptance

Run:

`./scripts/run_m18_final_acceptance.sh config/gateway-a.local.json`

A release candidate is accepted only when it ends with:

`[M18 FINAL PASS] Durable file upload/finalize/download + restart/fault recovery release accepted.`

## Freeze

After acceptance:

1. Commit only the accepted M18-C2/closeout files; keep unrelated local changes isolated.
2. Create annotated tag `m18-final-20260922-r1` at that commit.
3. Push the branch and tag to `gitlab`, `origin`, and `gitee`.
4. Run `./scripts/m18_final_freeze_check.sh m18-final-20260922-r1`.
5. Only a final `[M18 FINAL FREEZE PASS]` changes M18 status from release candidate to **CLOSED**.


## Release tag correction record

An initial tag `m18-final-20260922` was created before the M18-C2
release commit because Git preflight correctly rejected the original
freeze-verifier filename. That published tag resolves to the preceding
M18-C1 commit and is **not** the authoritative M18 release.

The authoritative M18 release tag is:

`m18-final-20260922-r1`

Only `m18-final-20260922-r1` together with a successful
`[M18 FINAL FREEZE PASS]` constitutes M18 closure.
