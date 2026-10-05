# Audited capacity validation

Use `benchmark/local_capacity/capacity_run.py` against the verified candidate image and real nginx port 9000. Existing private configs and baseline image remain preserved. Never call legacy swap-reset or fixture-reset helpers as part of this runner.

The coordinator verifies runtime identity and owned synthetic user identities before starting. It inserts only explicitly audited missing ring relations; existing rows are not modified. It starts epoll workers with distinct loopback source addresses, ramps 100 users/second in total, waits until every user is authenticated, and starts the offered-load window at a common future monotonic time.

Each run has an immutable unique directory under `.local/codex/capacity-<run>`. Raw ledgers, finite histograms with overflow and raw maximum, worker logs, process IDs, all-online evidence, container identities, memory/pressure samples, and SQL reconciliation are retained on success and failure. SQL is read while recipients remain connected. Positive ACK identity, durable message identity, actual wire delivery, and ReceiverConfirmed state must agree. Snapshot absence must be investigated for late commits and is never called permanent loss without evidence.

Required private gates: all expected users authenticated, no unexpected disconnects, heartbeat success at least 99.99%, all offered requests sent with no scheduler skips, no negative ACK, all sends positively acknowledged, positive ACK P99 at most 100ms, active positive ACK throughput at least 95% of offered load, and every sent logical message durable and receiver-confirmed. Scheduled-to-ACK latency is also retained so client scheduling delay remains visible. The bin value reported for P99 is an upper bound rounded to 0.1ms.

This runner covers normal plaintext private chat and connection hold. It does not establish group/file/MCP/TLS/failover/soak or mixed-workflow capacity. Those coverage obligations remain NOT_RUN until their own explicit scenario and reconciliation pass at the claimed user count.

Packaging regression: source compilation under umask 077 can produce 0700 executable files. Docker COPY preserves that mode and root ownership, preventing the UID1000 service from executing. Normalize image-copy binary modes to 0755 and library modes to 0644, then verify all ten binaries and dependency resolution as UID1000 before deployment. Preserve the failed image and logs. Apply only audited application services with no dependency recreation; keep the original image and private configuration hashes for rollback.

Resource constraint: retain other Windows applications under the latest user instruction. Do not suspend Python or the game, trim unrelated working sets, reset swap, or clear global caches. Capture host memory/paging and guest PSI with the workload. Task-owned cold build artifacts may only be advised cold after a separate exact-path/content audit; keep all files and hashes.

Database candidates: the audited temporary 1GiB InnoDB buffer pool is runtime-only, with 128MiB rollback, resize-completion verification and no durability reduction. Do not mistake warm-cache or zero-negative-ACK runs for P99 acceptance. Both first and warmed 1k100/s trials still failed P99. Preserve the runtime candidate and its memory budget in every run identity; it is not a permanent deployment setting.

Run `python3 benchmark/local_capacity/apply_private_indexes.py --run <unique-id>` only outside an active owned capacity run after source audit. Migration 012 adds only two named nonunique query indexes. The tool binds the existing project database inside its container without exporting credentials, rejects conflicting definitions, saves audit/DDL/rollback before mutation, uses explicit INPLACE/NONE and a 5-second metadata-lock wait, then checks old/new query parity in a consistent read-only transaction. It retains failures and leaves rollback for an explicit reviewed step. After any index build, record the full workload again; no capacity success is inferred from EXPLAIN or parity alone.

Migration 012 is currently a rejected performance candidate: actual `idx1k16a` failed with 303 negative ACKs and P99 2894.6ms. Its two indexes were removed by an exact-definition audited rollback; the original index set is restored. Keep the migration and tool for reproducibility, do not reapply it as an accepted tuning preset. The subsequent control `rb1k16a` removed negative ACKs but still failed P99 at 1092.2ms. All original summaries, ledgers, database aggregate deltas and rollback evidence remain retained.

Independent fixture ranges are explicit with `--user-id-base` and `--username-prefix` (defaults preserve the old 500000/m21b500000_ fixture). Both SQL identity checks and all workers must use these exact same options. Never assume the old 20k fixture supports 50k users. The existing worker already supports these arguments; no runtime binary change is needed.

Use `seed_owned_fixture.py --run <unique>` for a SELECT-only dry run. Use a different unique run and `--apply` only after reviewing the empty-range, exact prefix, foreign-key reference and owned-active-worker audit. The proposed new fixture is UID700001..750000 with prefix `codex50k_20261004_`; 50k users and 100k mutual ring edges, at most1000 rows per transaction. It refuses any existing target user or matching prefix and never updates, ignores, upserts or deletes old rows. Partial batches remain for a separately audited recovery decision. Every exact batch is retained privately and must not be exported. These synthetic accounts alone share a demo PBKDF2 salt/password; no existing credential is read or reset. Inserting explicit UIDs may raise the auto-increment counter; record before/after and keep it, never lower or reset it. Actual authentication and per-scale performance are independent subsequent checks.

Coordinator preflight failures now produce FAIL evidence even before client startup. Duplicate run names are rejected before the error handler can overwrite prior evidence. The process descriptor change is only the test process's own limit, documented before it occurs.

`cross_feature_actor.py --run <unique> --users <four-unused-nonadjacent-owned-UIDs>` executes 49 public request/response operations plus real delivery ACKs and existing actual file-stream RPCs. A successful isolated chain is functional coverage with explicit low sample counts. `--background-run <capacity-run>` additionally requires each operation to fall inside that background's original steady measurement window; capture that window and runtime image identities. Heartbeat verification requires a real nonzero probe for every actor; zero sent/zero received is not proof. Reserve actor IDs outside the background fixture range and never reset old relationships to reuse a test pair. All failed contracts remain immutable.

If all insertion batches completed but the final SELECT verification failed, do not rerun `--apply` or remove rows. `seed_owned_fixture.py --run <new-id> --verify-run <prior-id>` is explicitly read-only: it binds prior audit arguments, verifies every retained private SQL batch SHA and the complete committed batch list, then checks all canonical identities/hash/status and exact ring edges. Partial committed plans are refused and require their own narrowly scoped audit. The prior FAIL remains immutable. Unsigned UID arithmetic must add the ring size before subtracting two for the previous neighbor; verify the first and wraparound identities against the real MySQL expression.

Fresh50k fixture is now verified, with actual AUTO_INCREMENT750001/MAX750000; metadata cache originally returned520001. Read uncached engine statistics through a task SQL session only when checking this value. Never reset the counter. nf10k/nf20k both fail latency; nf20k has1134 negativeACK. nf30k/nf50k failed before all-online and are not completed capacity measurements. The49-operation nf10k chain passed inside the real60s window, with nonzero actor heartbeats; this remains functional rather than per-feature P99 evidence.

For diagnostic worker revisions preserve the prior binary and source hashes. Failed login/privateACK responses must retain bounded allowlisted reason/message/sequence/identity fields without raw bodies or credentials. Abort summaries are generated after owned workers stop and must explicitly distinguish partial authentication from a reached all-online barrier. Keep identical ramp, rate, deadlines and acceptance gates for comparable reproductions; do not improve a result by hiding failed requests or extending the original throughput window.

`--host <existing-local-IPv4> --source-ips <comma-separated-existing-local-IPv4s>` selects the actual published guest-address nginx port with one unique source per worker. Overrides are checked against `ip -j -4 address show`; no aliases/routes/firewall/sysctls are created. Default127.0.0.1/loopback sources remain reproducible. Two loopback sources were observed at nginx as one172.18.0.1 peer; a28,230 authenticated abort motivates a client-path control, but does not by itself prove port exhaustion. All five existing sources have passed real published-endpoint authentication. Keep ingress mode, runtime identities and CPU PSI in each result; source-only pool recovery changes remain undeployed and cannot explain runtime improvements until deployed.

Published30k/50k controls still failed; actual nginx alerts establish per-worker32768 socket-budget exhaustion. The131072 candidate changes this one directive only. Each proxied client consumes both downstream/upstream slots; acceptance may be uneven. Validate an owned candidate config with nginx-t before changing the original mounted file. Preserve the original inode when updating a file bind, archive exact prior bytes, audit a graceful reload, and verify all container identities remain unchanged. Reserve at least1GiB headroom for additional worker arrays and record actual RSS; no global network limits or cache changes. Rollback restores exact prior bytes to that inode and reloads after syntax validation. Hold success is separate from private P99/full-feature acceptance.

`TINYIMX_PERSIST_PHASE_TRACE_ENABLE=1` enables bounded numeric-only private transaction diagnostics in a separately deployed candidate. Default OFF, at most8logs/process/second, slow>=10ms or failure. Preserve statement order and no additional reads/writes; phases include pool acquisition/Ping and commit separately. Correlate numeric M/from/to and monotonic timestamps with Gateway traces; sampled records are not full population P99. Instrumentation tests preserve values/move ownership/exceptions and assert disabled clocks/sink are unused. Do not compile/deploy while a live capacity control is measuring.

ng30khold1 and ng50khold1 now passed real all-user authentication and30s hold with380377/380377 and971681/971681 heartbeats, no disconnects. The50k553s wall includes login ramp; it is not a553s all-online soak. No new messages were offered in these hold controls. Current all-feature/P99 acceptance remains FAIL/NOT_RUN.

Use `--message-image sha256:<exact>` for an audited newMessageService image; the original05380 pin remains default. Keep Gateway pin and healthy requirements. Record C++binary build revision/SHA independently of orchestration HEAD. A subsequent Python/docs-only commit does not create a newbinary revision. Capture old-runtime control before deployment, replace only intendedservice under separate audit, then keep comparable offered rate/deadlines and original positive/scheduled P99 gates.

Mutual friendship is required on every ring edge, including a smaller subset's last-to-first closure. Enumerate both directions and deduplicate the two-user case before auditing exact absent rows. Refuse all existing non-friend statuses; never update or delete a relationship to make the fixture pass. The original pctrl1k1 six not_friend failures all came from701000->700001, whose reciprocal closure was absent. Preserve that failed run and its349.5ms P99; repeat the old-runtime control after the audited fixture fix before comparing a new service image.

Receiver ACK candidate removes the duplicate Gateway GetPrivateMessage RPC. Acknowledge only a known private M/authenticatedrecipient/registered D through ReceiverDeliveryTracker, then call the existing ConfirmReceiver RPC for valid or duplicate evidence. MessageService still verifies the durable receiver and allowed state; failed/uncertain confirmation cannot undo local ACK evidence, and duplicate valid ACK remains a repair opportunity. Unknown/foreign/invalid attempts call no storage. Build the explicit private_receiver_ack_tests and existing receiver/durable/application regressions outside live capacity measurement, then audit Gateway-only deployment and validate real wire invalid ACKs, SQL states and identical offered-load controls. Never infer P99 acceptance from fewer RPCs alone.

OfferedRequestCount is shared by send admission and missing-tail accounting. The terminal ordinal at exactly duration must never emit a request because floating-point offset conversion truncated it a nanosecond below the endpoint. Keep every scheduler skip, late offer, bounded catchup and fixed drain. Actual nm30kp1 offered6003 rather than6000, so retain its failure. Compile the explicit8-case offered_schedule_tests and save the previousworkerbinary before replacement; repeat30k with an exact6000 gate. Worker/tooling revision is independent of alreadysealedGatewayc7 andMessage667 binary revisions.

Pool16 is a runtime-only, separately audited MessageService candidate, not a global MySQL or all-service tuning preset. Original message.json SHA8ff1e9c368fd6c15e2996800db133a60b29efae05cf8b48fab931da462721263 has pool8; candidate SHA2ead45f4c44348c6fad9a5526f3a7e816d4c3db43f21bcf8a8276aab1e324736 has pool16, with all other semantic fields and inode/owner/mode preserved. Keep the private exact before-copy and both raw log histories. Recreate only MessageService with the same55d image/trace1/environment/command; preserve other18 identities and all other config bytes. Before and after each run, bind poolsize/configSHA/containerID/image in the parent control evidence, in addition to the coordinator image pins. MySQL max_connections151 and durability settings remain unchanged; about57 live connections were observed with pool16.

The first pool16 control p161k1 passes current private gates at100msg/s60s:6000positive/durable/confirmed/wire,5433real heartbeats, ACKP9986.5ms andscheduled91.6ms. p1610k1 still fails both latency gates at140.0/143.0ms despite6000positive,82818real heartbeats andsame-window49-operation functionalPASS. These are one trial each under preserved shared host applications; do not infer causal or all-feature acceptance. Real20k/30k/50k results, exact30k6000 count, repeatability and high-volume function-specific latency remain separate obligations. The old pool8 ACK controls739.0/1625.7ms remain immutable.

Observe actual performance_schema.data_lock_waits and normalized DIGEST_TEXT under a real active window before attributing a failure to InnoDB row locks. The40 pool16 samples captured zero current waits, which does not exclude short waits between samples. SUM_LOCK_TIME is statement/table-lock accounting, not a direct InnoDB row-wait proof. No isolation change is authorized merely by a slow query aggregate. Rollback requires its own preflight/audit: restore exact private pool8 bytes, force-recreate only MessageService with the same candidate override, verify other18 preservation and health, and retain all evidence. Never reset application rows, caches, swap, unrelated processes, or Git history.

The first real receiver ACK probe timed out because the helper waited for the wrong delivery type. Preserve that FAIL; actual delivery2019 andACK2020. The corrected10-check probe reuses its owned client ID through normal idempotence and confirms SQL remains pending for invalidD/foreignuid, then becomesReceiverConfirmed for validACK and remainsconfirmed for a duplicate. This is low-volume protocol correctness, not capacity. Match AI provider probing to the actual AI profile backend network and extra_hosts host-gateway; a hostname result from the MCP container is not equivalent. Keep AI inference and MCP business capacity NOT_RUN/FAIL until real positive domain results and their own performance gates pass.

Completed evidence-v5 contains459 individually SHA-verified files across33 completed success/failure stages. ArchiveSHA=a8ea74fbbc354602abcfa5e52d422c94bd3c1133783a56db97fe3329883bb498. Export only explicit completed directories, exclude runtime-private/image-context/binary/before-files, scan actual private sensitive values in memory, and rehash immediately before archiving. Extract into a fresh task evidence directory, reject traversal/links and verify the current versioned manifest rather than assuming the filename ends in v3. Active pool16 scale matrix is excluded from this archive.

Pool16 matrix is now complete and separately preserved in evidence-v6 (201 verified files, archiveSHA66440687b1c543e86eeee973e82f5bf793694580ce62e430762d60b1b76f4e9f). p1620k1:6000positive/confirmed/wire, HB230348/230348, P99151.6ms/scheduled156.0ms, FAIL. p1630k1:exact6000planned/sent/positive/confirmed/wire, HB454521/454521, P99311.8ms/scheduled313.9ms, FAIL. No negativeACK/skip/late/disconnect in these completed runs. p1650k1 aborted at36353authenticated, no all-online/business/P99; auth_timeout3 andbusiness_deadline_exceeded2, no artificialauthdelay. All19 production ID/image/StartedAt remained the same across50k; lifetime cumulative restart counters were nonzero on some older containers, so distinguish historical counts from this experiment.

Actor range is519800..519950 inclusive. The parent20k/30k matrix mistakenly selected actors above that guard and was refused before business operations; preserve those logs and originalFAIL. A separate unchanged-runtime companion p1630chainfix1 using519870/872/874/876 passed49operations strictly inside p1630k1's original60s window. 20k chain remains NOT_RUN because its window ended;50k companion isNOT_RUN because no all-online window existed. Read the guard and verify canonical unused nonadjacent pairs before assigning new actor IDs; never broaden bounds or overwrite existing relations to mask an orchestration error.

Actual AI backend/host-gateway probe resolved172.17.0.1 and failed local/api/tags connection (curl7,HTTP0); the earlierMCP DNS result was a different namespace. Probe used no credentials or inference, retained a stopped owned container and preserved production identities. Formal MCP principal and reachable AI provider remain configuration questions while backend optimization continues. None of these low-volume probes establish50k all-feature performance. Keep historical pending rows intact: current global pending-discovery EXPLAIN uses the existing covering index with loose group-by scan, not a proven full-table scan. Any candidate status index or connection reuse must have independent audit, correctness/failure recovery tests and real same-offering controls.

Single-lease private persistence candidate acquires/Pings once before idempotency precheck and uses the existing OnConnection query, then reuses that same session for the unchanged atomic transaction. Retain the UNIQUE-key concurrent arbiter, all identity checks, rollback and explicit Reset before insert/ambiguous-commit recovery reads. Test-only preinsert race hook releases the lease before its barrier and reacquires afterward, so more race participants than pool slots cannot deadlock. Validate in fresh audited isolated schemas atpool1 andpool4 using existing real transactional outbox tests; never point them atproduction10001/10002 fixtures. Add slot-return assertions forCreated/reuse/conflict/concurrent paths. Keep all schemas/results and beforebinary/image for rollback. Trace Acquire now owns initial lease/Ping time andPrecheck is query-only; document this timing-boundary change rather than making an invalid old/new phase comparison. Existing55d/pool16 runtime is retained until separate sealed-image validation and deployment audit.

Single-lease b24 is now deployed atpool16;169 checks passed,1k control6000positive/realHB5208 and68.0/73.8ms passes;10k6000positive/realHB82143 and142.5/145.1ms stillfails, same-window49chainPASS. No10k speedup is established over old140.0/143.0ms. Read BOTTLENECK_ANALYSIS_20261004.md before claiming optimized capacity: distinguish local CPU/ramp sample, slow sampled server phases, matched RPC overhead, durable COMMIT and global pending discovery from populationP99 and causality. Continue business-rate curves and high-volume per-function/mixed/soak measurements; do not equate connectionhold with activeusers throughput.

013 is an independent one-index candidate, not012's rejected paired change. Only add status/to index, strict original-index definitions and no ownedload/inference preflight, INPLACE/NONE/session lock_wait_timeout5. Preserve old indexes, all Pending/history and durability. Record fivecursor forced old/new result parity in one readonlysnapshot, chosen plans, seeded randomized database execution timings, then exact runtime/index/configSHA before/after same-offering controls. Any regression requires its own exact-definition DROP audit outside load; never reset data or silently accept a SQLmicrobenchmark as business improvement.

Existing Ollama isguestloopback11434/qwen2.5:7b, notproductionconfiguredhostgateway/qwen3:8b. Isolated page50 MCP eight realdomain andtwo negative cases pass with owned519870/group26/file16; page100 realconversation fails because advertised100 exceeds MessageRpcClient50. Retain all original helper and product failures. Owned MCP is stopped byexactID after domain tests; preserve original19/configSHA and allrows, no inference until resource-audited isolatedAIphase, and no modeldownload/globalbindchanges. Formalprincipal1 remains invalid. Store test source snapshots underbenchmark/local_capacity/evidence_tools with original exactruntimeHEAD guards; these are historical evidence scripts, not interchangeable new-release benchmark entrypoints.
Next unread atomic-count candidate is local-only until the current one-index scale matrix ends. OriginalGateway performs three Redis leases/PINGs and EVAL+GET+GET beforeACK. The opt-in Lua response returns scalar status plus exact bulk-string counts at the same atomic projection point; legacy callers stay scalar. Never convert counters through Lua numeric precision or weaken marker identity/overflow checks. Gateway uses each parsed optionalcount only after projection success, and retains the old per-counter GET fallback on failure/invalidcount. Build/test in a dedicated auditedRedis127.0.0.1:16390 only, pool1, prefixowned; preserve exact high-integer, race, duplicate/Read and closedconnection cases. Deploy onlybothGateways after image sealing under separate audit and retain7fimage/binary/config/env for exact rollback. Query-index/runtimeMessageb24 remains unchanged for causality.

Clarification: capacity_run.py passes --heartbeat-seconds15 to workers. The5s interval is resource sampling and some server maintenance, not the capacity client heartbeat interval. Read full merged histograms and bounded steady PSI/resource samples from completed-capacity-distributions-20261004. PSI CPU waiting is not CPU utilization; guest availablememory remained about8GiB during measured10k/30k with lowmemoryPSI, so do not blame memory alone. Latestone-index10k77.4/79.4ms andfull6000positive passes threshold,20k135.5/138.1ms and30k180.4/183.6ms fail despiteall6000positive andsamewindow49chainsPASS. Single trials/sharedhost applications limit causal percentages; higherbusinessload/repeat/allspecificfunctions stillrequired.
Fixed10k business-rate curve at Gateway1d8/Messageb24/pool16/index013/messageworkers16 failed300/s (10408/18000positive,P993006.7ms,167.967active/s) and500/s (11332/30000,P993024.7ms,184.2active/s,529positive reconciliation anomalies). Preserve every raw failure. Do not attribute all relation deadlines toSocialService: exhausted remaining budget also returns the same error beforeRPC. Slow-sampled Gateway queue median1644.245ms greatly exceeds persistRPC median69.057ms; samples are not full populationP99. Chat and receiver-confirmation tasks shareMessageExecutor, so queueing also affects correctness. Same-image32-worker experiment changes exactlyone environment variable; unchanged64stripes/queues/deadlines/other17/privateconfig; exact16 rollback is retained. New100/s and300/s controls required before acceptance.

The1000/s run aborted because coordinator read an in-progress partial ledger line. Never call its partial metrics capacity PASS. Newcomplete_ledger_snapshot retries non-newline tail for atmost2s, rejects any malformed complete seven-field row, and saves exact immutable snapshot plusSHA/attempts. Preserve original ledger and oldFAILED run. Validate concurrent append, malformed interior, permanent incomplete tail and empty hold ledger inguest, then real fixedruntime controls; no omission or gate relaxation. Source benchmark commit and compiled service revision must be reported separately.
Reject32workers:300/s6815negativeACK,P993008.2ms, queue median1651.452ms, sampledpersistRPCmean136.777ms andlease22.708ms. Restored exact1d8/16 under separate audit after ownedworkers ended, preservingother17/config and everyrawFAIL. Late readonly500/s verification foundall11580identities unchanged,Pending545->0,529originalpositive anomalies nowSQLconfirmed; retain originalfaileddeadline/wire snapshot. Evidence-v7 downloaded and771file hashesverified, archiveSHAa09e40cfb4913c1b4d8b7faf42deed8f75a8bec13371aa94a114faeb6c308e0b; newer32controls/rollback require nextarchive.

Stripe fair-handoff candidate retains running=true while scheduling continuation toFIFO tail afteronecomplete task; onlyone drainer owns businesswork, same-stripeFIFO preserved. Emptyqueue transition remains locked. IfTrySubmit rejects duringpoolclose/full or allocationthrows, originaldrainer continues soacceptedmustRun responsibility is never discarded. Validate deterministichot/coldordering andexisting shutdown, deadlines, cancellation, parallel, completion fences andGateway/ACK/recovery contracts. Sourcechange/buildseparate fromsealed runtime/deployment/capacity: noactualspeedup accepted beforematching realcontrols.

User now requires analysis before further load. Do not run additional capacity matrices, parameter trials, builds or deployment as part of this review. Fair handoff184 correctness checks passed, but10k100/sP9984.7ms and300/s7365negative/P993016.1ms failed; both Gateways restored1d8/16 with other17/config preserved. Read ROOT_CAUSE_REVIEW_20261005.md for the completed150/200/250 curve, actual mounted512pending/64stripes/perstripe64/3s settings, synchronous sender/receiver competition and two-lease receiver confirmation. Pending includes active/completion responsibility and is not pure waiting queue length. Preserve every failed run, including1k300 skipped1245 offered requests, and separate late confirmation from originaldeadline acceptance.

Before resuming performance validation: identify concrete cause/cost, audit a targeted repair, implement it with unchanged authorization/durable state semantics, and pass relevant correctness/failure regressions. First candidate is a single-lease recipient-validated confirmation; independent bounded ACK capacity must also cap shared downstream concurrency. Pool slot-wait/Ping/CPU and RPC admission evidence are not yet measured separately. Opt-in MySQL trace remains an unapplied local draft, with no performance proof; this documentation update invalidates its previous doc-baseline hashes and future application requires a fresh audit. No blind thread/pool increases, infinite queues, weaker durability, earlyACK, fixture/history deletion or global memory cleanup. Verifiedv9 preserves155 files including all four completed controls and readonly analysis; historical evidence helpers must not be interpreted as authorization to restart tests now.

User subsequently requested continued problem-driven iteration. First audited candidate is recipient-aware single-lease confirmation: full OnConnection query/parsing and ownership/state checks before UPDATE guarded byM/recipient/status0. Default repository port keeps legacy validated behavior; production adapter avoids a second lease/Ping without any new transaction, Gateway capacity change, earlyACK or durability relaxation. Validate original application/crash/ACK contracts and new actual owned-schema pool1/pool4 concurrency/Read/invalid-state/lease-return cases before any sealed Message-only image or deployment. Each stage needs fresh exact identities/audit and retained b24 rollback. Performance controls must answer whether this specific removed cost changes the known100/150/200 rate behavior; do not blindly restart a large matrix or call a source fix full-feature acceptance.

The first pool1 real regression failed a new incorrect Pending/Read race expectation after46 passed checks. Original Read only advances alreadyconfirmed status1, so Read-before-confirmation legitimately leaves status1; source behavior must not be changed to satisfy an invented contract. Keep the failure/schema and correct only test/docs: concurrent result must be1 or2 with both operations successful, then explicit Read reaches2 and late ACK is no-op. New schema attempt after test-only rebuild; source message binary remains compileddc7083a while test/orchestration head is newer. No deployment until real regressions pass, no ignoring assertions or relaxing ownership/state checks.

Recipient confirmation237 checks passed but0a9657 controls100/sP9994.3 and150/sP99177.2 did not improve;150 stillFAIL. Preserve source/tests/initialfailure/allcontrols, restore exact runningb24 via auditedMessage-only rollback; other18/env/config/pool16/index013 retained. New diagnostic source returns adapter confirmation to compatible original validated two-lease path. Opt-in TINYIMX_MYSQL_POOL_TRACE separates mutex/slot wait/Ping/reconnect; TINYIMX_STORAGE_WAIT_TRACE_ENABLE records wall vs threadCPU for handler/repository phases and correlates successfulM%64 logs. DefaultOFF; existing normaltrace stays intact when storageflagOFF. Bound logging, no payloads/config/credentials, no SQL/deadline/transaction/durability changes. WallminusCPU is not by itself proof of IO rather than scheduler waiting; RPC method timing excludes pre-handler admission/return transport. Run original correctness/real-schema/owned-relay recovery checks before a separate sealed diagnosticMessage-only image and fixed150 window. Diagnostic timing is evidence, not optimized capacity acceptance.

Storage diagnostic a315 completed278 checks and sw150a10k/150/s9000 durable confirmed wire positives but P99179.0msFAIL. Active-window141 exactM/TID samples: repositorymean20.312ms/CPU1.829ms; handler20.614ms/CPU1.998ms, extra0.302ms.468 pool samples mutex0.132ms/slot0.095ms/Ping2.020ms; no pool expansion justified. ActualGateway persist timer wraps one MessageRpcClient call. Seven matched slowGateway samples have RPCminushandlermean13.595ms, cannot subtract different141-population means. Keep nonrandom/sampling limits and allFAIL results.

Both141 sampledpersist and141 confirm handlers used141 distinctTIDs, consistent with threadchurn hypothesis; linkedgrpc1.76.0 defaultsMIN1/MAX2 confirmedlocally. Only candidateMessage MAX_POLLERS16 idle retention changes, not activeRPCmax/Gatewayworkers/DBpool. Test realgRPC regressions before fixed150 diagnostic sameflags+observer; evaluateTIDreuse, CPU/wall/correctness. If promising, diagnosticOFF100/150controls and preserved49featurechain; no earlyACK/durability weakening orthreadmatrixguessing. schedstatsdisabled0 makes runqueue aggregates unusable; preserve raw and explicitly omit causalclaims. Diagnostic snapshot overhead max104.953ms, measurementnotacceptance. Exact Messageb24/environment restored after collection, original18andconfig retained; verifiedv12has88files. Every laterstage needs freshauditedidentities and rollback.

Reject MAX_POLLERS16 f7 after205 freshchecks including75realgRPC: mp150diag9000validpositives/wire/confirmed butP99225.5/scheduled226.0FAIL.141persist/140confirm used107/113TIDs; CPUminorchange0.969to0.9135cores is not delay improvement. Revert source exactdefault2 and runtimeb24/exactoriginalenvironment; keepf7/image/allFAIL and verifiedv13with74files. Historical evidencehelpers are guarded preservedrecords, not authorization to overwrite/reexecute oldstages. Gateway actualserver path is confirmed; privateadmission startedentry is erased, not infiniteempty-poll; globaldurable discovery100ms needswindowcounters beforeblame.

Nextguarded baseline run only observes active-window file/status/digest/procstat/IO+CPUpressure deltas under originalimages/fsync1/sync_binlog1/groupdelay0 and strict10k150/s gates. Cumulative storagecounters and fileMISC cannot imply exactfsync latency. No countersreset/instrumentsenabled or SQLglobalchange. Only if evidence supports should a separate1000us groupcommitcandidate be tested, retainingdurability double1, noSET PERSIST, exactvariable rollback in finally and retainedresult. MySQL officialreference: https://dev.mysql.com/doc/mysql-replication-excerpt/8.0/en/replication-options-binary-log.html. Candidate mayaddlatency/competition, not promise performance. Allfeature10k50k acceptance remainsopen; everynegative/skip/SQL/wire/memory limitation retained.

That original-runtime I/O baseline was attempted and aborted during login (io150base1222 connected/1023login_ok/one actualprestart deadlinefailure); there is no activewindow or validI/Odelta. Preserve observer-error and rawFAILED, do not execute analyzerwithout both actualsnapshots and completewindow, do not tune groupcommit beforeauthfailure is understood. Successful-subset loginmean886.673ms/P992627.6ms differs greatly from previouscomplete10k~33ms/P99~90ms; failingrequests are outside thatsuccesshistogram. Exact3s deadline and100users/s ramp remain unchanged. No falsePASS byslowerramp/deadlinerelaxation.

DefaultOFF authentication diagnostics are a separate audited User-only experiment. TINYIMX_AUTH_PHASE_TRACE_ENABLE captures numeric repository lookup/password wall+threadCPU and handlerwall+CPU, successfuluid%16 samples, combinedmaximum8 loglines/secondbucket/process. No credential, username, metadata or payload is included. Allauth/KDF100000/constant-timecompare semantics remain. Handler excludesgRPC preadmission andtransport; wallminusCPU cannot byitselfproveI/O. Only uid/TID/time-enclosed records may bepaired. ActualGateway compiled33fc has prestartdeadlinefailure andold stripe drain; currentglobalfair-source candidate is not running and remains rejected. Do notbuild/deploy currentGateway sourceincidentally in the Userexperiment. Source/compiled/runtime revisions andunlabeled originalUserbinary hash must be recorded separately. First passdiagnostic/oldUserapplication/realgRPC regressions, seal exact original38dca base+onlyUserbinary, thenaudit User-only recreation withsameprivateconfig/allother18 unchanged and exactenv/38dca rollback. Diagnosticresult is evidence, not full-feature acceptance.

Auth374f280 completed36checks and one unchanged100users/s/3s10klogin+30shold:all10000login/HB61440 exact, populationloginP9986.5ms/mean35.3858ms.402 uid/TID/time-enclosedpairs show lookupwall2.114ms/CPU0.497, passwordwall18.086/CPU13.205, repositorywall20.353/CPU13.797 andhandlerextra0.312/0.191ms. Sampleslimited/nonrandom; do notsubtractdifferentpopulationmeans orcallphaseP99 fullpopulation. Oldseconds-longabort not reproduced orfixedbydiagnostics. Restore exactoriginalUser38dca/ae1b6f/environment beforeoriginal-runtime storagewindow. Actualenvkeys/values match butarrayorder differed; failedvalidatorstage retained, separate readonlypostreview PASS with no secondrecreation. Compare unique ENVmapping, notlistorder.

Correctmeasurement defects with separatelyretainedreviews: Histogrambinsalreadyceil100us, so storedkeyisupperboundwithout+step; oldreview0.1ms high retained. DockerStatePid isinit and its/taskcount1 is notserverthreadcount; cgroupCPU remainsvalid. Resolveexactdirectservicechild, confirmpididentity/samecgroup, labelinit/servercounts separately infuture, neverreconstruct historicalserverthreadsfromcurrentsnapshot. Sys_gettid authsamples326TIDs/402requests areactualhandler/repositoryevidence, notpeakconcurrency. Cachedoldhelpers/readiness/sourcehead guards arehistoricalrecords; do notreplaycompletedstage orclaimscriptpreservationaloneverifiesperformance. Currentall19/config/SQLrestored; no userappsclosed/memorycleanup/VMchanges. v14exportpending, retainfailedsourcegeneration/auto-reviewtimeoutaudit separately. NextoriginalI/Owindow onlyaftercompletedauthanalysis, strictgates unchanged, nogroupcommitcandidateuntilcostevidence.

Original io150base2 completed9000validpositive/wire/SQLconfirmed/HB82296 butP99188.0/scheduled190.7FAIL. Actual58.3998s counters:confirmUPDATEmean9.3398ms/COMMIT7.5416, redo222.912fsync/s, binlogfileMISC210.908/s mean1.8746ms; MISCnotpurefsync, MAXcumulative notwindowpercentile, sums overlap. CPUbusy85.297% withsystem30.290/softirq9.700, guest-widefork/thread315.241/s andCPU PSI63.328% stall, memorypressure minimal. Capture865.949/1138.598ms andnonsimultaneousSQLreadboundaries matter. No singlecause accepted. v14verified109files SHAefe242c22d61b3dc90f333444a114bbf7047afa26a5d4f3bfe016d63ae00f4b1; localWindowUTCparserfixed PythonISO-stringmatching, original0-matchreviewpreserved.

Pending groupcommit diagnostic hasone1000us dynamicchange only in exactprojectMySQL8.0.40CID:retainflush1/sync_binlog1/logbin1/no_delay_count0, noSET PERSIST/configedit/restart. Pinall19runtime/config/index tooriginalio150base2, no activeworker. Same10k150/s60s/ramp100/3s/fullSQLwireHBgates, matchingthree-readSQLsnapshots. Officialmanual acknowledges possiblelowerfsynccount andhigherlatency/contention. Verify actualcounts/COMMIT/confirmUPDATE/CPUpressure andfullACKhistograms; reject ifno materialbenefit, nevercallphaseaveragepopulationP99. Mainfinally restores0 evenifclientcleanupfails; signalcleanup and360s owncoordinatorlimit; independent450s ownwatchdog onlyrestores exactcandidateonunchangedMySQLCID andrefusesunknownexternalsettings. Cancelwatchdog onlyafterverifiedrestore; persistedvariables/other19/config unchanged. Preserveall partialFAIL/raw/parameters/rollback. Source/helper/audit savedGit beforeexecution; v15onlycompletedbaseline/source, candidateNOT_RUN atthisstage. DiagnosticPASSstilldoesnotacceptallfeature10k50k orlongtermcapacity. C++diagnostic source remainsdefaultOFF; currentGateway sourceglobalfaircandidate stillrejected, neverrebuild/deployit incidentally.


### 2026-10-05：提交合并候选否决，转向消息增量成本分析

实际执行的 gc150u1000 保留完整 10k 认证、9000 计划/发送/正 ACK/wire/SQL 接收确认；心跳 82382/82382，零负 ACK、skip、late、断连。P99 227.5ms、计划到 ACK 230.0ms、最大 511.192ms，仍然 FAIL；原版 io150base2 为 188.0/190.7ms。主 finally 已恢复 delay=0，独立看护在验证后正常取消；19 个容器的 ID/镜像/启动时间、全部私有配置 SHA、持久参数均未变化，SQL 保持 1/1/1/0/0。没有保留 1000us 参数，也不继续尝试盲目延时矩阵。

有效计数窗口 58.3976s：binlog file MISC 6069/103.925 次每秒（原版 12317/210.908），但精确 redo fsync 12844/219.941 次每秒（原版 13018/222.912）基本没有减少。COMMIT 均值 11.3823ms（原版 7.5416），确认 UPDATE 13.6419ms（原版 9.3398）。guest 忙碌 85.570%，system+softirq 39.833%，CPU PSI stall 64.362%，全 guest fork/thread 320.030 次每秒；提交和消息延迟都没有显示收益。MISC 不是纯 fsync，SQL 均值不是总体 P99，顺序运行且保留其他应用，不能把所有差异因果归给该参数。

两轮各 12 个 Docker CPU 帧均显示 MySQL（约 1.46/1.48 核）、Message（0.89/0.93）、两 Gateway（各约 0.75）、Redis（0.61/0.62）是当前主要服务成本；outbox/unread 投影器各不足 0.01 核。Docker 帧是滚动样本，不能与 /proc 连续积分相减后把差额归给独占内核成本。

冻结 auth10kdiag 的纯在线窗口，排除前 5s，仅纳入完整包围的四段共 20.0467s：guest 忙碌 31.404%，system+softirq 13.743%，CPU PSI 11.668%，context switch 19707.79/s、全 guest fork/thread 51.180/s。Gateway 精确 cgroup CPU 各 0.259/0.260 核，User 0.032；相比消息窗口 85% 忙碌和 315–320/s 创建量，消息链路增加了大量工作。此对照跨时间且 User 诊断镜像不同（选中阶段无登录），不能报告严格因果百分比或把所有新线程归给 Message。

另对已有 sw150a/mp150diag 的七服务 cgroup CPU 做有效分项：Message 原版 0.969 核中用户态 0.463、内核态 0.506；被拒绝 poller16 为 0.913 中 0.454/0.460。MySQL 原版 1.403 中 0.861/0.542；Gateway 各约 0.71 中内核态各约 0.33。没有观测这些 cgroup 自身限流，cpu.max 均 max；这不排除 VMware 或共享主机等待。init PID 的线程数不能当业务线程数，cgroup 包含所有子进程故 CPU 分项仍有效。大比例内核时间支持继续定位 RPC/网络往返/调度，但尚没有 syscall/flamegraph 级的唯一根因证明。

所有候选失败、参数、SQL 计数、分析、看护、精确回滚和源码工具保存；v15 本机已验证 57 文件，SHA ece33159841d81f10a1cf9060cda9d4cf0c99e86e4bfe134bc8bb5727e34f372。v16 将保存完成的失败候选及这次只读复核，不覆盖 v14/v15。后续应测量实际消息成本再改代码，避免重复扩池/扩线程/改提交延时。当前全部功能 10k–50k 的极致性能目标尚未实现。


### 2026-10-05：修正 MCP 消息分页工具契约（验证待完成）

先前真实隔离 MCP 的 page100 会话查询失败有确定代码原因：MCP schema/校验接受 1–100，而 MessageRpcClient 与 MessageApplicationService 的会话/历史读分页最多 50。仅这两项工具改为 schema 和本地校验 1–50；缺省 50、合法 1/50 原样传递，不悄悄截断；51/100 等已经不能完成的请求现在在发起 RPC 前返回 invalid_arguments。好友、我的群、成员列表保留 100，鉴权/身份/查询/期限与 SQL 完全不变。

新增回归检查两类工具边界、缺省、无效类型与大整数，并验证无效参数不调用后端、其他三类工具仍允许 100。构建先在独立目录将原 domain 源码与新测试链接，保存应有失败；再只构建 MCP domain/core/server，使用缓存依赖并单线程，保存原二进制 SHA 与 red/green 日志。构建不会部署或改变 19 个服务，尤其不构建当前仍保存的已拒绝 Gateway 公平调度源码。之后真实隔离 MCP 验证需独立审计，当前没有性能接受结论。

v16 脱敏归档本机 70 文件逐 SHA 通过，archive SHA 488f8ceb9a79614ac0848aa370ddf9abcae8b9c0c19e855572794bfa2b6bbcbc，保留提交合并失败、精确回滚及 CPU 对照。MCP 契约修正不等于私聊尾延迟改善；10k–50k 全功能极致性能、AI 服务/正式 principal 和每功能混合负载验证仍未完成。


### 2026-10-05：MCP 分页修正原版失败/候选通过，真实验证待执行

源码 Git 778ad55708b1806d09c7691b245f870b17861983。将精确原版 domain 源（SHA 1a5ef6126f113c057fc18c2538febb896cff6b96c7b08576467e808ba9b538f5）与新增边界测试在独立目录编译链接，原版明确失败于 schema matches each domain page contract，exit1；不是编译或运行错误。修正后 domain 89 项 PASS，core exit0，core 原测试没有逐项 PASS 输出，不能杜撰检查数量。仅 MCP 单线程缓存构建完成，候选 server SHA dbfef7cc761bc3253d4269038c63e1c347974c73574b5df1e07055fed201d926；原 MCP/core/domain 二进制已逐 SHA 备份，全部 19 实例/配置保持，未部署。

本机生成候选时曾用 Windows 默认 GBK 读取中文文档而失败；部分本机 C++/测试候选保存于 evidence/mcp-page-preparation-encoding-failure-20261005，规范审计后只恢复两项本机 preimage，生成器明确 UTF-8 后成功。该失败发生在打包/上传前，Ubuntu/运行时未改变。生成和恢复工具随本次 Git 保存；保留失败，不掩盖。

待执行真实验证使用已拥有的 principal519870/peer519872/group26/file16，候选原生进程只监听 127.0.0.1:18322，随机 token/私有配置不出目录。八类真实只读 RPC、消息 1/50/缺省、12 非法 limit、未授权/身份注入与其他三类 page100 均验证。strace 由父进程跟踪自己的子进程，-c 只保存 syscall 聚合，绝不附着正式服务或输出参数/凭据；计时受干扰，不能用于容量接受。15s 启动/5s 请求/150s 整体与自己的看护有界，INT/TERM/HUP finally 仅在 PID/cmdline/starttime 全部吻合时停止自己的候选；正式 19 服务/SQL1/1/1/0/0/所有应用保持，不做新 fixture 写入、模型推理、安装或删除。该真实验证此阶段尚未运行。

主性能排查已核实 Message 通道按 target 缓存复用、ZooKeeper Resolve 读本地快照、TCP_NODELAY 已开、遥测为批处理导出。这些机制不支持重复修复“逐消息建连接/实时 ZK 查询/Nagle/同步逐条遥测”的猜测。尚需更细的实际 CPU/RPC 往返证据后实施主要性能修改；全部功能极致性能仍未达到。


### 2026-10-05：MCP 真实边界验证通过；当前主机环境已只读核实

独立回环候选 compiled778/dbfef7 完成 27 请求：八类真实领域 RPC、消息 1/50/缺省六个合法用例、两消息工具 51/100/0/-1/字符串/null 共十二个非法用例、未授权和身份注入检查。好友/群列表/群成员 100 保持有效；已有测试身份519870/peer519872/group26/file16均正确、文件字节校验元数据未变。仅父 strace 跟踪自有子进程，汇总 syscall 数量不含调用参数；futex/epoll 总耗时包含等待且受 ptrace 干扰，不能当独占内核 CPU 或生产 P99。候选按 PID/cmdline/starttime 验证后已停止，全部19/配置/SQL1/1/1/0/0保持，没有模型推理或业务写入。

下一步封存原38dca镜像+仅 MCP 二进制，验证UID1000加载和缺配置预期exit1；之后仅 MCP 更新，环境键值/命令/健康检查/正式 principal/模型/配置原样，其他18保持。无活跃MCP请求和压测时执行，验证现有token的工具声明50/100，不导出token。任何失败精确回滚38dca/8cd229，并验证ENV映射而不是数组排列。当前 image/deployment尚未执行；这项功能修正不表示正式principal1缺失或AI连接已解决，也不表示10k–50k性能已接受。

Windows21:39UTC只读审计：实际运行VMware16.2.4 build20089737、Ubuntu VMX8vCPU/16384MiB，当前vmware.log Monitor Mode=ULM且WHP标记存在，Windows HypervisorPresent=true、VirtualizationBasedSecurityStatus=2。真实VMX SHA726444586dbb79772b4a2127581f11fcd75d9a5087a0c9b033f6a5aed705ad4f。SecurityServicesRunning=[0]，不据此声称HVCI/内存完整性功能正在运行。VMware官方说明ULM使用WHP API： https://blogs.vmware.com/cloud-foundation/2020/05/28/vmware-workstation-now-supports-hyper-v-mode/ ；Microsoft说明状态2表示VBS运行： https://learn.microsoft.com/en-us/windows/security/hardware-security/enable-virtualization-based-protection-of-code-integrity 。此为当前环境因素，没有与CPL0成对比较，不能分配延迟因果百分比或否定软件瓶颈。没有改VBS/启动项/VMX/系统设置、重启或停止任何应用。

虚拟网卡确为e1000，但guest ip route get192.168.220.128 from192.168.220.129结果local/devlo；本地压测及Docker服务链并不以物理虚拟网卡为主要传输路径。因此不盲改网卡。当前有依据的方向是进一步区分原消息组件的SQL同步/网络往返/RPC与线程成本，再选择代码优化；不是把所有高内核时间都归给VMware。主机审计JSON在本机evidence/vmware-host-mode-review-20261005，只有允许的模式/硬件字段，没有完整VMX/log或命令行。

v17将保留MCP原版red、89green/corepass、真实验证/汇总trace、自有进程停止审计、源码/镜像/更新和回滚证据，实际secret扫描且不覆盖v16。所有功能极致性能仍未实现，继续迭代。


### 2026-10-05：MCP已落地；隔离RPC增量成本测量待执行

MCP image bc85c186271873610ff759c008d5f36d49d1a9c331e064eb7fa1121d822485a3，编译778ad55708b1806d09c7691b245f870b17861983，二进制dbfef7cc761bc3253d4269038c63e1c347974c73574b5df1e07055fed201d926，实例bd8182dcd4be5ee219b58b4711fd5e4512a38b19c712ce5b7d0358b2bd36cef7。健康检查、现有token的schema50/100、完整ENV键值/命令/健康配置均通过；仅MCP更新，其他18/全部配置保持。原38dca/8cd229及精确rollback保留；正式principal1与AI问题仍未解决。v17本机65文件SHA通过，archive68a0b3dbbc8efaacfa0b69543778ede430646f658ba9c46dd278e722b4bfce57。

新组件诊断比较相同20ms受控等待/128B synthetic echo的direct6000次与真实gRPC6000次，各300/s20s；再仅父strace -c跟踪自己子进程1500次5s。CQ1/MIN1/MAX2匹配原默认，16驱动线程、3s每次RPC、35s各进程上限，所有计划请求不跳过。记录严格响应身份/内容/数量、迟发、各index对应的caller/handler wall及handler threadCPU/TID、rusage合计CPU与context-switch。只编译自己的文件和缓存proto/gRPC，排除全部其他TinyIMX实现archive和缓存MAX16 MessageServer；不构建正式目标或安装依赖。

不访问SQL/Redis/鉴权/真实用户/AI，不包含receiver/wire/10k在线背景。synthetic成功不是durable ACK或容量接受。rusage包括caller和server，strace计数包括启动关闭、等待时间并受ptrace干扰；不能用模拟结果直接替代真实Message CPU或归因全部延迟。用未跟踪成对计时和CPU测增量，以跟踪计数检验创建机制，保留共享主机顺序对比限制。源码/审计先Git，当前未测量，所有19/config保持；只在cmdline/starttime/PGID匹配时清理自己的新进程组。旧完整19身份压测工具因MCP更新不能直接重放。准备阶段自动审查超时未创建生成器/包/候选源，核验后单次重试；不是安全拒绝，无生产动作。目标未达，继续测量再选择优化。


### 2026-10-05：隔离RPC诊断首轮编译失败已保留，工具修正待测

5cd8b4b20826c8eec4d80fdae8c5b590b19f917f 的新工具在编译阶段失败：匿名命名空间 pb 别名与当前 Protobuf extension_set.h 的全局 pb 冲突。compile.log SHA44c558e859759be3a0729352b870f2b244eea2b1a53006d0303979c5d5153ddc，没有link/测量/容量结果。原失败目录不修改，单独只读归类确认全部19/配置保持、自有probe未运行；不能把编译失败当业务延迟或根因证据。

只把诊断别名改为 probe_message_proto 并全局限定协议命名空间；不修改任何生产实现。新attempt2目录/helper保留同样case和所有检查；编译/链接也使用自己的新进程组、180s期限和cmdline/starttime/PGID清理，INT/TERM/HUP已移至编译前，失败保存明确phase/exit/error及运行状态。全部原Git/日志/工具保留，v18尚未导出，将同时包含首轮失败、只读复核、两次源码迭代及attempt2完成结果。修正/审计先Git，当前尚未得到RPC增量测量；性能目标仍未达，继续测量。


### 2026-10-05：受控RPC机制证据与固定工作线程候选（尚未测量候选）

4b9db7f 的第二次组件诊断编译/链接与全部case完成，正式19容器和配置不变。direct/grpc各6000条合成响应全部身份/字段核对正确，无异常、无负的配对附加时间。相同20ms受控等待：direct调用P99 20.76237ms、均值20.26236，进程0.079237核、16个handler TID；gRPC调用P99 23.594134ms、均值21.92129，逐请求配对附加P99 3.226773ms/均值1.665207，进程0.698700核、484个handler TID。gRPC用户/系统CPU9.282559/4.704915秒，direct1.01423/0.571899秒；自愿切换65165/12004，非自愿420/15。两个进程均包括调用方与处理方，不能把差值直接当成正式Message独占CPU，更不能把23.6ms当真实持久化延迟。没有10k心跳/多跳/SQL/授权/发送接收确认，这只支持RPC开销机制存在。

父级strace 1500条合成调用全部正确，但显著扰动：调用P99 98.571528ms、计划迟到P99 1431.755516ms；681次clone3、16574次futex包括框架启动/停止，futex 77.65%为系统调用累计等待时间占比，绝不是独占CPU比例。不能用此轮traced数据作为性能结论或真实业务根因比例。

下一项为单独新诊断CPP：当前生成的CallbackService接口、固定16业务工作线程、待处理上限512，同一20ms处理逻辑/协议/客户端，A同步-B回调-B回调-A同步各20秒300/s，加父级callback strace5秒。阻塞工作只在自有工作线程，回调快速提交；Finish之后不访问reactor，OnDone删除，取消/期限在执行前检查。提交/完成/拒绝/队列峰值、处理TID、全量逐请求配对与迟到一起保存。尚未得到候选结果，尚不修改生产RPC实现或部署；先测机制再决定是否承担生产迁移的取消/期限/关闭/过载全功能风险。官方参考 https://grpc.io/docs/languages/cpp/callback/ 与 https://grpc.io/docs/languages/cpp/best_practices/ 。

v18首轮导出在写归档/审计之前被“Only bounded evidence files”拒绝：20MiB检查在ELF过滤前，自己的诊断二进制超过边界。保留原helper/原Git/原结果，单独新attempt2导出只把ELF魔数过滤提前，仍禁止二进制/秘密且普通证据上限20MiB不放宽。新的运行目录保存首轮失败分类和大文件类型/大小；修正导出需候选完成才能执行，当前尚未生成v18归档。所有失败与新源代码均纳入Git，性能目标仍未达。


### 2026-10-05：固定线程回调机制候选拒绝；下一步隔离连接往返成本

fb44d14完整ABBA各6000/6000字段正确，callback提交/完成均6000、拒绝0、结束待处理0、队列峰值2/3；全部19/配置保持。同步A1/A2进程CPU0.679041/0.714810核，回调B1/B2 0.870256/0.869075核；附加开销P99同步3.042336/3.324842ms、回调3.368641/3.365442ms，均值同步1.618209/1.709033ms、回调1.766088/1.760129ms；调用P99同步23.423174/23.807896、回调23.771882/23.734689ms。回调TID固定16而同步467/453，但CPU/尾延迟未改善，自愿切换回调91087/91388 vs同步65161/65073。因此仅处理线程数量下降不足以接受优化：拒绝向生产迁移此候选，也不能声称gRPC线程 churn 已解释真实188ms尾延迟。所有候选源码和失败判定保持，停止继续盲调此矩阵。

callback父级strace 1500/1500、clone3 44（先前同步traced681），调用P99 86.969201ms、计划迟到P99 276.326966ms；futex16035和78.40%仍是累计等待占比不是CPU。两次tracer扰动差异不能代替未traced性能；未traced回调CPU上升才是拒绝依据。正式消息同步handler/取消/期限/持久化均未改变。当前头文件明确同步/回调IsCancelled随时安全，CQ异步仅在AsyncNotifyWhenDone标签送达后安全，因此绝不能简单把旧handler塞进CQ并声称语义不变。

v18_attempt2归档100文件 SHAece928ff8e5251c4fd6cdb8b3d5533bd9c1e912e0dca8a708682b1f0ed51f4f8，包含首轮编译失败、修复、直接/gRPC、ABBA候选和首轮导出失败分类。Windows旧extractor已安全完整解包，但只接受v数字清单名而拒绝-attempt2后缀；保留该失败审计/目录，不删除或重新覆盖，只用准确指定清单逐文件复核，100个SHA全部通过，独立验证记录已保存。

只读审查确认MySQL池借用每次Ping且已经在池锁外，失败重连会归还slot，不是旧锁内网络错误。下一项另建诊断CPP，16专属健康连接，现有Message凭据只在进程内读取，地址换为真实MySQL Docker IP；只做SELECT1不读写用户表。固定300/s20秒的Ping+Query-A1、Query-B1、Query-B2、Ping+Query-A2，保存6000条逐请求Ping/Query/CPU/计划迟到与MySQL cgroup计数。native→Docker路径不同于正式container→Docker，且没有真实SQL/事务/10k心跳，Query-only只用于衡量一轮健康检查RTT成本，不是产品政策或故障恢复证明。当前尚未测量，不修改连接验证/重试/SQL/持久化/服务部署；机制收益确认后再设计空闲失效、出错后恢复、事务回收等产品验证。目标仍未达。


### 2026-10-05：减少确认路径SQL往返的原子收件人条件候选（尚未构建/测试/部署）

699b062只读健康连接ABBA四轮各6000条SELECT1正确。A1/A2 Ping平均0.561194/0.568582ms、P99 1.324623/1.354024ms；Ping+Query调用P99 2.368942/2.380542、均值1.149633/1.162640，Query-only P99 1.470426/1.556427、均值0.679460/0.680934。probeCPU A0.206279/0.204724核 vsB0.137156/0.138409；整个MySQL cgroup A0.248183/0.257747 vsB0.166127/0.170207（含后台/启动，不是探针独占）。每轮全部逐请求和资源计数保留，MySQL1/1/1/0/0不变，19服务/配置不变；native到Docker健康SELECT1成本不能当真实业务188ms延迟归因或10k性能通过。v19本地53文件逐SHA验证，归档97240a934d0e064d765d1d7badf0462ec66e3be561b551e6c65e5b9730d8e72e。

不更改连接池健康政策。跳过/缓存Ping会使已有断线测试从Acquire失败变为先拿到失效lease，语义变弱，当前候选保留每次健康检查。也不重复旧dc7083先查后更新单lease候选；新方案在成功Pending确认路径先执行收件人绑定原子UPDATE：PK message_id、to_user_id=认证收件人、status0、from_user_id非0、消息类型在当前枚举1..3、created_at非NULL。当前真实表是数值/日期类型，非NULL日期读取为非空字符串，其余字段原解析允许空值；所有原解析会拒绝的有效性条件在快路径保留，不加入额外的发送者!=收件人限制。影响1行才成功，0行在同一健康lease全记录查询，仍按原解析→不存在→所有权→Confirmed/Read/Failed/异常顺序分类。UPDATE错误不重试、不假报成功；0行后仍合法Pending视为存储不一致，保守失败。批量确认、已读、私信持久化、群消息和RPC取消/故障seam都不改；不降低事务/同步日志持久化。

真数据库回归新增：pool1同一会话SHOW SESSION Com_select前后差值证明成功Pending确认没有SELECT（初值不假设0）；消息类型0/4不更新；Confirmed/Read错收件人仍拒绝；合法和错误收件人并发只有合法者能迁移。原有状态9/Failed、不存在、重复、确认与Read交叉、4线程幂等、连接归还、原子outbox/回滚全部保留。会话计数只读前测曾错误要求初值0，实际mysql CLI初始1→SELECT后2；原失败/原始SHA729ce3a3338f992b9e67c0e0ebb5d8d32a95d5dc2678e814530269184cb166cc保留，独立只读复核差值1通过，不修改原失败。一次跨层命令参数错误发生在SSH/SQL之前，后改固定脚本/结构化SQL，无秘密输出或产品影响。

代码先审计/Git，当前尚未得到新候选构建或性能结果。构建只Message与必要测试、parallel1/缓存SDK，不部署Gateway当前fair源码；验证同步Server默认原源码重新编译，不能误封装缓存MAX16版本。新建两套精确命名独立schema池1/4（只读生产DDL，不拷贝生产行），所有测试写入/故障trigger/记录只在新schema，保存全部失败和schemas不DROP。测试通过后再独立审计镜像/Message替换和完全回退，匹配真实私信控制；目标仍未达，继续迭代。


### 2026-10-05：原子确认84构建通过；红对照记录工具修正，不改业务代码

84cb214e17bdd4590eb5763483e448d4f78b0d0b候选构建通过：MessageApp53、crash28、真实gRPC75，共156条PASS输出，无FAIL；默认同步服务对象不引用SetSyncServerOption，确认未封入缓存MAX16。候选Message SHA5d24b144577e01a5bfc7049b93ebcd65f61e1008da7e8513546ad0e041c55202，新SQLtest SHAe6eb32cf936a8e7e309fd14e3313c40c56999634a1170a2e177c1050f116ec75，原adapter新test红binary SHAbb0e16c38b2b3e3c842fb3b2734623f2f14b7ca16953b3b3892a2f88488f7582；实际编译版本84，与运行正式Message的ddc7不同。全部19/配置保持，尚未部署。

首轮独立MySQL红对照正确返回1，54条独立功能断言PASS，只有“Pending normal path avoids SELECT before update”新行为失败；末尾另外打印“[FAIL] M16-A Transactional Outbox integration tests, failed=1”总汇。工具把两行FAIL当两个实际断言，导致停止在红对照，绿候选没有运行。原failed SHA bb82f8aa89bed78669c8ba8edb3633dd9831e966b9dc83524cd78d478118c0c9、原log SHA d379d78888f1440fa515bcd826ff10546da41f97b712fffcad9ccae15f585265保持。只读复核核对真实创建的schema只有codex_receiver_guard_20261005_red1；旧failed字段列的是3个计划名，不能说尚未创建的green1/4已保存。此错误是测试汇总分类，不能声称业务候选失败或已通过绿验证。

只修复helper，产品和testC++保持84逐diff不变；新版将实际断言和准确总汇分开，要求红对照恰好1个预期失败且总汇failed=1/exit1，绿对照必须完全无FAIL输出和exit0，未放宽任何业务断言。新attempt2目录和3个全新schema red1a2/p1a2/p4a2，保留首轮全部数据不删除/复用。现有生产created_at实际TIMESTAMP NOT NULL，独立DDL前检查已按实际typed TIMESTAMP/DATETIME处理，并非臆测字符串列。helper/docs新Git与编译84区分，使用二进制前只允许docs/evidence_tools差异，若业务/test/CMake变化则拒绝。v20将在真正新红绿完成后冻结全部源码/构建/首轮失败/复核/第二轮结果和会话计数失败/复核；当前新绿结果仍NOT_RUN，整体性能目标未达。


### 2026-10-05：原子收件人更新真实数据库绿验证完成；准备匹配性能对照

163f4df 的新工具完成红绿对照，实际编译代码仍为84cb214e17bdd4590eb5763483e448d4f78b0d0b。原版红对照54条独立PASS，恰好1条避免SELECT的新行为FAIL，准确总汇failed=1/exit1；候选池1 outbox55条PASS、unread snapshot14条PASS，候选池4 outbox54条PASS，共123条绿PASS且无FAIL/exit0。与此前156条单元/真实RPC PASS分开记录，不把红对照或零输出测试伪算进绿数量。三套独立red1a2/p1a2/p4a2 schema和首轮red1都保留；生产SQL配置1/1/1/0/0、全部19服务与配置不变，尚未部署或证明性能改善。v20归档77文件SHA d531f77096f86137af9fc42e8b1da0989e8b0ccdf8f24f09e3a3b4281d0f1589，Windows新目录逐SHA全部验证。

方向依据是正常Pending确认去掉一个健康lease/Ping及完整行GET，不缓存/跳过现有健康检查，不降低持久化或期限。首次成功原子UPDATE由PK、收件人、Pending、非零发送者、有效消息类型和日期绑定；重复、已读、错误身份和异常数据仍完整解析分类。终态/重复路径现在UPDATE零行再GET，可能比原版GET更贵，需要后续重复确认及混合交互负载验证。发送者positive ACK边界不直接包含收件人确认，此修改可能通过降低共享CPU/SQL竞争改善端到端，不能把确认路径省时直接当ACK省时。

本次源代码仅新增封装/部署/同负载运行/分析/精确回滚/安全导出helper，C++与测试仍84未改。镜像只替换Message二进制5d24b144577e01a5bfc7049b93ebcd65f61e1008da7e8513546ad0e041c55202；运行Gateway仍33fc原版1d8/16线程，User仍原38，已修复MCPbc85保留；不能部署当前Gateway拒绝过的fair源码。原版先测guard150A1，10k登录100人/秒、150私信/秒、60秒/9000计划、3秒原期限/15秒心跳；只换Message后guard150B1同参数。全部登录、计划/尝试/positive/wire/SQL确认、无skip/negative/late/disconnect、心跳完全相等，以及原100ms两项P99门槛不改。只读两帧SQLdigest/19cgroup/guest压力计数限定在活动窗口，保存观察开销；后台与时序共享主机限制明确，部分/失败人口不得全负载归因。全部结果包括FAIL保留，不在构建时测试、不因减少线程/查询就接受优化。当前控制尚NOT_RUN；即便私信改善，群组/离线/文件/搜索/MCP/AI/20k至50k/故障及长稳仍未完成。

部署前保存真实inspect私有备份、原image/tag与rollback override，仅Message可重建；环境键值、命令、healthcheck、配置SHA、其他18 ID/image/start以及实际容器内二进制校验。失败或回归按证据恢复原b24并核验健康/二进制c119，保留候选镜像、所有自己的停止探针和数据库行。不删除文件/容器/数据，不清全局缓存，不停任何其他应用，不改VM/主机安全/网络；运行前内存门槛只用于避免失真的负载，保留用户游戏/Python。

本地准备也记入失败链：red-review生成器第一次未闭合三引号导致Python解析失败，在任何写入之前停止；随后的package因新源文件不存在而拒绝，未上传/改业务。修正时一次自动权限审查超时，只读确认没有修改后一次重试完成；这是超时，不是拒绝安全请求，不代表业务故障。原本地失败审计保持。Git提交、编译版本、运行镜像以及功能通过/性能待测分别记录，继续按实测结果迭代。


### 2026-10-05：原子确认机制生效但端到端收益不足，恢复原运行版并转向在线共享开销审查

4240e2c 已保存封装/部署/对照/分析/精确回滚脚本。候选镜像e38099c18b81d67a64a1daddb63171c01c9681cb4e826465f91055dd86b0e4fd仅含Message84cb214/二进制5d24，UID1000加载和受控缺配置退出通过；原版guard150A1先运行后只替换Message执行guard150B1。两轮均10k全部登录、9000计划/发送/positive ACK/实际wire/SQL确认，skip/late/negative/disconnect0、所有worker完成，心跳82754/83102各完全相等。原版P99 175.5ms/计划到ACK177.5、max308.782、活动149.9333/s；候选P99 173.1/174.8、max288.524、活动149.9167/s。两项原100ms门槛都FAIL，退出2原始保存；2.4ms差值在共享主机顺序测量中不足以证明稳定优化，不追加盲目参数矩阵、不声称解决主要瓶颈。

两轮只读活动计数窗口有效58.3712/58.4051秒，原两帧观察350.570/291.202ms，候选393.774/319.268ms，完整开销保存。按MID完整行GET 21922→13154，INSERT8748/8749，GET/INSERT约2.506→1.503，与成功确认少一次GET机制一致；不能把活动计数窗口8749条当9000全窗、背景也不可称独占。Message平均CPU0.905532→0.852731核，系统0.510398→0.446024、用户0.395134→0.406707；MySQL1.430540→1.430536核几乎不变；COMMIT9350/9399次、均值6.944610→7.322538ms，确认UPDATE8751次/8.915323ms→8746次/10.715598ms（新SQLdigest不同，计数细微边界差异）。两GW各约0.71→0.72核、Social0.31465→0.31928、Redis0.58878→0.55795、nginx0.26571→0.26918；全19无自身cgroup限流。查询次数下降是真实机制证据，提交/确认等待和MySQLCPU未改善；所有平均值不是SQLP99，也不能把总时间求和当独占墙时或推导因果百分比。

运行原Messageb24/c119已按精确回滚恢复，当前CIDcbac27adc561012c78a6bfd91a86b2860e89bf7a8a1a285e193993f690485293，健康、pool16、配置2ead、ENV键值/命令/healthcheck/其他18 ID-image-start和1/1/1/0/0都核验。新MCPbc85继续保持。候选源代码84/测试/编译二进制/镜像/SQLschemas/所有消息/raw仍留存，当前GitHEAD不能冒充运行代码；本次未修改产品C++或测试。v21归档118文件 SHA48f7da3c587c3b0f454e0feececbc322e11a44c7d2086561be1b3a34a258120c，Windows新目录119归档条目/118逐文件SHA通过，包括两轮FAIL、部署与恢复，没有配置、环境、ELF或完整服务日志。

下一项先只读完整9000成对台账的发送→ACK、发送→wire、wire与ACK时间差，原直方图P50/P95/P99/P99.9和调度迟到不加额外0.1ms；分析guest CPU/PSI增量，明确观察帧捕获边界和同机客户端/代理/其他应用共享CPU限制。负载生成器已有心跳优先队列，不存在每循环扫描所有10k心跳的旧问题；固定epoll_wait10ms会造成若干发送合并，已有计划到ACK和调度迟到严格保留，不能用改发送节奏隐藏服务不足。只读源快照取实际运行Gateway33fc、Message ddc关联公共连接实现及当前HEAD作为审查上下文；当前Gateway公平调度源码曾拒绝，严禁直接构建部署它，未标注Git的Social/outbox运行等同HEAD也不能臆测。重点检查持续心跳/在线路由以及每私信好友关系校验与共享网络往返，再决定新产品变更。当前全部功能极致性能及20k50k仍未实现。

本轮分析证据限制也保存：回滚前候选服务阶段日志未单独采集，容器重建后不可用，不能捏造候选phase或比较；所有必要完整台账/数据库/CPU/压力/门槛原始结果已保留，原版日志在部署前私有快照中可筛出数值慢样本（阈值且限频，有偏样本非P99）。新版回滚helper仅增强今后的模板，在重建前把本任务当前candidate log保存runtime-private，捕获失败仍保存分类并继续必要回滚，绝不导出全日志；已经执行的旧Git/原回滚记录不修改。曾只读误查resources.jsonl，实际是guest-resources.log，改用真实路径后读取，未更改任何样本/产品。主机其他应用/游戏/Python全部保留，无全局清理或VM安全更改。


### 2026-10-05：完整投递与共同消息计时证明等待分散，先验证在线维护合并机制

9eb6296 的只读成本报告完成。原/候选全部9000发送-ACK-wire按CID/MID和双方身份成对核对，无丢弃样本：发送→wire均值74.6993/72.6494ms，P99 210.1270/210.9084、P99.9 286.9274/296.8753、max324.5814/358.2285，实际投递尾延迟没有改善。2554/2604条实际delivery早于sender ACK属于允许交叉顺序，未把负wire-minus-ACK时间丢掉；该差值均值10.5391/10.1802，P99 73.4677/73.2296。原完整ACK均值64.1773/P50 55.4/P95 130.1/P99 175.5/P99.9 245.6，候选62.4872/54.6/125.3/173.1/240.3，严格原100us上界桶保持。发送调度迟到均值1.71312/1.73115、P99 9.1/9.0、max15.383/12.035ms，不能把近175ms尾延迟都归因于10ms客户端epoll等待。

活动guest忙碌85.6950/85.1040%，system+softirq40.1613/39.1819%，ctxt37885.5/37278.7每秒、guestwide fork/thread310.821/312.832每秒（包含全部应用/观察者）。CPU some压力63.2608/62.3974%，IO some2.0670/2.0806/full1.0182/1.0235%，memory压力接近0；19cgroup总5.42436/5.37003核（不含所有系统路径，不能与guest差值断言loadgen独占）。采样在两帧中读数时刻有350-394ms边界不确定，所有capture值和原stat/PSI保存。这不是内存不足导致本轮失败，不进行全局内存清理、主机程序停止或安全/VM设置更改。

原版488个阈值/限频持久化慢样本全部按MID/身份/单调时间与同一条消息台账匹配，全部计时顺序有效，没有不匹配。这个共同有偏样本发送→ACK均值74.79054，由进入仓储前33.66787、仓储内部23.44657、仓储结束至ACK17.67611ms精确逐消息加和；commit均值11.31337、max110.503ms。仓储前包括client/nginx/Gateway排队、权限/路由与RPC，仓储后包括服务返回/RPC、未读快照和ACK交付，不能直接称某一项独占。此样本不是总体P99，也不把不同人群均值相减得出差异。

Gateway原版慢chat日志327条、candidate299，均失败0；原chat dispatch年龄平均11.2933、work119.8545、permission19.2139、route8.16086、persistRPC82.8792、unread8.87533ms；candidate9.57197/118.8080/17.3080/8.03041/81.7788/11.14788。peer原4/candidate6条慢样本，不是完整交叉路径总体。进一步原327 Gateway与488repo取相同MID交集40条，全部RPCduration>=repository：同一组senderACK均值152.6363，Gatewaydispatch10.9378、权限17.03325、路由8.941075、persistRPC87.349325，其中repo59.627375/RPC之外27.72195、unread10.528375，repo内commit34.307525；40条是两处慢采样交集，不能称主观百分比根因或将27.72当gRPC独占CPU/运输时间。真实尾等待横跨存储与多跳/共享调度，需要改变高频成本而非只调单个SQL参数。

已按运行33fc源码确认：Pong快速返回、4线程异步presence、O(1)session反向索引、本地ZK发现快照、权限一条双向SQL、原子未读一lease/EVAL快照、持久化确认边界之前的sender ACK策略都已存在，不能重复声称修复。在线刷新仍每次健康lease/PING再owner(gateway_id+connection_name)绑定Lua GET/JSON/EXPIRE。10k/15秒约667次维护每秒，再叠加每消息路线和未读Redis调用；下一项只能先隔离机制：独立自有Redis键，原single Lua与有界批量owner条件刷新，相同open-loop维护人口/健康检查/状态结果，验证TTL、旧连接/新所有者、notfound/invalidrecord/wrongtype/取消或停机边界，再比较网络命令数/CPU/墙时/迟到。当前尚未编写/构建/测量生产批量候选，尚不承诺端到端收益，native→Docker组件路径不同于真实container→Docker；不缓存/放宽好友拉黑权限，不跳过健康检查，不延长客户端期限或削弱持久化。

日志当前Gateway/Message/Social均warn，console=true/file=true/async=false/file每条flush=false；User info但活动User仅0.023核。Logger代码同步锁/console endl存在，TINYIMX_LOG确有过滤前ostringstream构造，但正常INFO实际写入在上述服务已关闭，异步日志改造不是已证实主要瓶颈；不能直接调更低级别或根据源码就宣称根因。v22本地44条目/43文件SHA全部验证，归档a44d03d3ac977d267d0ac900ddb38256f0728281e46905439e88f10a4ddb2f47；Gateway数值3文件单独哈希下载核验，仓储与GatewayWindows数值join源工具与结果均单独留存并原样复制至guest新stage，Python执行环境为原Windows任务tools目录，Git forensic copy不在guest执行。

只读解析失败与修正也记录：第一helper错把peer context user_id当recipient，嵌入Python35行AssertionError，第一stage只有审计无成功数值报告，原脚本/stage不覆盖。运行33fc的SubmitMustRunConnectionBusinessTask不赋user/epoch，BusinessRequestContext默认0，HandleGatewayForwardChat使用该helper且ScopedBusinessDispatchContext(connection,0,0)；只读分类sourceSHA4a8fed3bbc7decc120eb287d9022ba0023c7ab3c52d551d019e3fa0c786eb5da、类型SHAabe8fc9ff0181e5a91cd8234a316d9ac13eac894e6413efa9c11b5809b743228。新attempt2保留MID绑定正ACK台账；chat sender/原seq/正epoch，peer内部user=epoch=0/正seq，产品身份校验没有改。分类命令一次自动审查超时未启动，仅一次同意重试成功；这是超时而不是安全拒绝，没有approval阻塞。全部19/config/运行原版及MCP好修复仍保持、整体目标未达，继续有证据地迭代。


### 2026-10-05：隔离在线维护原型的审计与执行前状态

只读预检 `online-maintenance-mechanism-preflight-20261005` 完成于7e93560：实际Redis7.4.11、standalone cluster_enabled0、noeviction、maxmemory0/maxclients10000，连接25/阻塞0/used107636592B。真实native目标172.18.0.13，两个Gateway Redis pool8，运行33fc的owner条件刷新Lua与当前源码一致，SHA12b8fcb43720694347e0a781d9c7ed6adcff43bdb34c37e750cf2c38e26246b1。原池Acquire仍健康PING，没有跳过；两组相同4worker仅是受控逻辑Gateway模型，不把组件等同完整10k心跳。主机可用约5.5GiB，无需清理；其他应用、游戏、Python保持。

本次只添加诊断CPP，使用既有redis_pool_demo编译flags与link精确SHA和项目静态库SHA，仅编译自有object/ELF，不构建或部署任何产品服务。固定自有Redis前缀codex:online-maintenance-probe-20261005:，15类x2模式合法所有者/缺失/旧网关/旧连接/损坏JSON/scalar/null/缺少所有者/零负非法TTL/wrongtype/替换所有者/unicode/已过期，逐项状态+TTL+原字节预计84检查。原Lua完整同体包装，批量每key独立pcall，wrongtype错误只对应该key，其他结果继续；尚非故障网络/产品取消与停机正确性验证。功能通过后才创建10000全新自有键TTL300，不引用生产键、没有DEL/FLUSH/CONFIG。正常所有者EXPIRE120，失败不改记录/TTL，所有夹具自然到期；不能任意删除历史证据或清理内存。

随后single-A1/batch-B1/batch-B2/single-A2，各667/s20秒、13340全部计划槽open-loop，同8worker/两pool8，batch两collector/最多16key/最长5ms合并。两512有界队列溢出即保留失败，不丢样本；要求planned/attempted/issued/completed完全一致、错误0、逐ID/状态1/时间顺序核对。CPU包括collectors，native到Docker与产品container路径不同；Redis cgroup含背景/.5s屏障/结果序列化，不能当独占业务CPU。编译链接180秒、prepare60秒、各case90秒，仅PID-starttime-cmdline-PGID匹配的本任务子进程组可在失败后终止；所有日志/live/partialraw/failure阶段保留。19运行ID/image/start、私有配置SHA逐项保持，SQL未触及，原持久化1/1/1/0/0不变。

执行前结果明确 `NOT_RUN`，不得称生产批量候选或全功能压测通过；命令减少之外，还需CPU、迟到/5ms维护新鲜度与全人口错误证明。如果机制有效再做生产生命周期/所有权交叉测试并审计正确运行基线，不直接构建源码中已拒绝Gateway fair调度。运行仍原Gateway33fc/1d8、Message ddc7/b24/c119/CIDcbac，User38、MCPbc85；源码guarded确认84与当前Git不等于运行版本。v23归档24文件SHA88f9c20c3d930d05323330493a241e3b0a7ee2c48729e8e6079c9b654f2f3290，已本地逐文件验证；v24预置仅完整组件证据才导出，排除秘密配置/env/私有日志/ELF并RAM秘密扫描。审查时在打包前修复漏standard include、临时vector迭代器错误以及RedisConfig.enable必须读取真实配置；无执行或产品变更。曾只读猜错services/gateway路径，真实gateway/由rg文件清单确认，不是产品失败。持续迭代，所有功能10k50k极致要求仍未达到。


### 2026-10-05：在线维护机制有重复成本收益，新增有界缓存API前置测试

c87b9b5实际执行 `online-maintenance-component-probe-20261005`：84项检查全部通过；ABBA原/批/批/原各13340计划、attempted、issued、completed、逐ID结果1完全一致，错误0，全19/config保持。原A1/A2 EVAL与健康lease各13340，batchB1/B2各6622，最大批3；原nativeCPU0.362273/0.368229核、batch0.335281/0.336015（包括两collector）下降约7.45%/8.75%。全Redis cgroup原0.528632/0.600619、batch0.291491/0.361036核，两组对照分别下降0.23714/0.23958核；该cgroup包含背景/.5sbarrier/序列化，不能称独占业务CPU。原enqueue→result P992.611634/2.919340，batch8.001407/8.092809ms；scheduled→result P992.854266/3.255612→8.302184/8.445341，5ms合并增加异步维护新鲜度等待。只有native独立测试键，没有真实10kPong/消息混合压力，不等同完整性能验收，也不外推50k结果。

v24归档87文件/88条目 SHAa7af2aed2df360cd2495d0d4329a83d1e2e7faade9356bd8a8f317d2ce2603a2，全87逐文件SHA本地通过，含53360全部原始请求与语义/CPU/进程/编译审计。全服务/资源/应用仍原样，无内存清理必要。下一阶段不重复容量压测，先把有证据机制实现成缓存API，保证功能衔接再接入Gateway。

只读基线snapshot确认Gateway/相关cache/example相对运行33fc仅BusinessExecutor.cpp有已拒绝6521 fair调度差异；本次仅恢复该源码精确原SHA3b707e2e5a63cd7671a92c977ea75fc7fcaad29de1c90e92e41bcb8f9017cb22，Git父版本/审计preimage仍保留，没有构建部署它。新增 `OnlineStatusRefreshRequest` 与 `RefreshOnlineIfMatchBatch` 最大16，输入顺序返回、逐项原校验（非法项不触及Redis），全非法/空不Acquire，超过16整批invalid且无I/O；合法子集仅一次原健康Acquire/PING和EVAL。Lua包装一次从原single body生成（原SHA12b8...不改），每key pcall隔离wrongtype，原owner网关+连接/GET/JSON/EXPIRE和逐项typed状态、TTL/原字节保留。当前Redisstandalone范围明确，不声称提供RedisCluster跨slot批量能力，不放宽好友/持久化/超时或加入盲重试。

API真实Redis测试CPP与helper分别ownpool1/4，用全新固定prefix codex:online-maintenance-batch-api-20261005:p1/p4，最多120自有键TTL300/专用1秒过期/正常120刷新，显式替换自有夹具，禁止生产键/DEL/FLUSH/CONFIG/SQL。18类原单条与批量逐项状态/TTL/字节对比、wrongtype后有效项、空/1/16/17边界、同用户混合所有者顺序、替换后旧owner不得续期、null/uninit/只关闭本任务池及invalid项、4并发调用100个三项mixed batches/全部lease归还。每检查保存JSON，failure/编译/log/partial原样留存；编译ownAPI.o+test.o/link各180s，cases90s仅本任务PID身份匹配组可终止。结果在提交时明确 `NOT_RUN`，没有全局Redis故障或产品Gateway生命周期测试；不把真实API functional与之前prototype84混称。

接入审查发现原网关kNotFound会验证当前local session并自恢复，必须保留；不能把批量NotFound直接丢弃而制造在线路由丢失。还需执行时epoch+connection当前性、立即Pong、错所有者/下线交叉、queue上限/结果终态、停机排空后销毁pool。拒绝让四个presence调用线程同步等5ms形成max4批，因为50k用户时吞吐会受工作线程数量限制；后续只考虑独立有界异步批量调度及实际生命周期测试。当前API尚未在Gateway使用，运行仍33fc/1d8、Message原ddc/b24/c119、User38、MCPbc85。所有功能极致及20k50k要求仍未达到，继续按证据迭代。


### 2026-10-05：批量缓存API350检查通过，先验证命令无响应的停机边界

cca0e3b实际 `online-maintenance-batch-api-20261005`：ownpool1/4各175检查全部PASS，共350；两组原single18与batch18逐项对比、TTL/原字节/所有权及fault结果保留，size0/1/16/17、wrongtype后的合法项、重复用户旧/正确owner顺序、替换后旧owner、null/uninit/只关闭自有pool、4并发100个mixed3item batch/全lease归还都通过。原运行19身份/config/健康核对完全一致；API自有编译object/ELF留存，没有构建或部署Gateway。v25归档41文件/42条目 SHA35524c79c6a3de63e399515ae229071ceb9e819fde280748a1a356800994cce8，全41逐文件SHA本地通过。此前prototype84与实际API350是不同检查，不能混称全功能或Pong/50k验收。

继续审查异步合批生命周期发现代码仅redisConnectWithTimeout3s，实际安装Hiredis1.3.0 header区分command_timeout并提供redisSetTimeout；同版本官方primary源码 https://github.com/redis/hiredis/blob/v1.3.0/hiredis.c 中redisConnectWithTimeout只设置connect_timeout、redisSetTimeout设置blocking socket读写边界。当前SDK读取原RedisConnection源码与运行33fc精确一致。本次先诊断，不因源码没有调用setter就直接声称实际无限等待，更不把故障停机边界当正常175ms根因。

新诊断CPP只连接任务自有127.0.0.1 OS分配高端口模拟端点，禁止6379/11434，不读取真实凭证或连接productionRedis。AUTH固定public-owned-auth-fixture属于公开测试字串，SELECT1/PING/EVAL return1无生产键。每类healthy先正常+OK/PONG/:1证明协议/客户端路径，随后ownpeer在收到正确command后故意4.25秒无响应，再仅关闭自有peer释放客户端。记录原进程是否在连接3秒之外仍未完成及全部Native/服务端单调时间；预期red4次bound violation，编译前结果明确 `NOT_RUN`，要真实复现后再单独审计修复。共8顺序case，1native child/1serverthread/listener/peer，编译link180s、每case20s安全guard，仅PID身份匹配的自有组可在失败时终止。审查先修正finally关闭次序：释放peer/关闭listener后，必要时终止自有child再等待serverthread，避免异常时阻塞接收让threadjoin抢先失败。没有执行失败或产品改动。

后续若证实缺少command边界，有限I/O超时属于故障正确性前置，不延长客户端3秒期限、不跳过healthyPING、不盲重试。再验证所有原API350及健康AUTH/SELECT/PING/EVAL，保留red/green原始证据与Git后才接入bounded async collector/workers、即时Pong/current epoch/connection/旧会话取消/停机排空。另有kNotFound→current local check→SetOnline的两阶段恢复窗口需自有交叉用例验证，不能在批量中省略自恢复，也不能臆造已复现的真实远端替换故障。所有运行仍GWs33fc/1d8、Message原ddc/b24/c119、User38、MCPbc85，源码executor已恢复运行版、cacheAPI尚未使用；全应用/game/Python保留，无全局清理。完整所有功能10k50k极致目标仍未达到，继续按证据迭代。


### 2026-10-05：原Redis命令无响应缺少边界已复现，修复单次I/O空等上限

f036ee5 `redis-command-timeout-original-probe-20261005`实际完成：AUTH/SELECT/PING/EVAL四healthy正常成功，四无响应全部在4.25秒屏障仍未退出，关闭自有peer后才失败。原Ping4.252310381/Auth4.252681309/Select4.251610326/Eval4.252164753秒，错误均Server closed the connection；AUTH/SELECT连接建立完成但初始化命令最终失败，PING/EVAL原Connect成功。自己的loopback源/peer/command接收与释放单调时刻/InstalledHiredis1.3.0/cachedlibSHA全部保存；没有真实Redis访问、生产故障/配置或键写入。v26归档70文件/71条目 SHA96fdcc172dc16464c3fe99b86b39b62dcf908fe1d4394fb4e869721bffde2d58，本地70逐文件SHA通过。源码判断由实际cachedSDK行为支持，但不能解释正常负载175ms全部尾延迟。

本轮只在成功建立socket后、AUTH/SELECT之前调用redisSetTimeout(context_,原timeval3s)，失败关闭context并返回false。仍保留redisConnectWithTimeout3s、原健康PING/重连/返回值/协议与解析、原客户端/RPC期限，没有延长门槛或重试。Hiredis1.3.0 setter配置blocking socket单次读写空等上限；不是整个命令绝对期限，分段回复drip、发送缓冲阻塞和全局停机deadline尚未实证，不得把3秒直接当全操作上限。官方同版本说明 https://github.com/redis/hiredis/blob/v1.3.0/hiredis.c 。当前仅源码候选、没有产品部署。

新fixedstage编译自有RedisConnection.o置于原cachedlibs前解析class符号，所有原SDK/cached产品ELF不覆盖；同一个已提交surrogate probe源码重建，四healthy和四无响应严格要求idle失败且连接不可用、2.7..4.25秒、peer关闭之前已返回，避免把EOF误算timeout。随后exact350实际APIharness仅固定namespace/stage字符串替换到新自有regressionCPP，原测试源码不变且原/生成SHA保存；编译ownAPI/test/Connection objects后用新prefix codex:redis-command-timeout-api-regression-20261005:p1/p4，pool1/4各175+100并发mixed batch，最多120newownedkeys/TTL300/1expiry/正常120/自有replace。Privateconfig仅RAM，禁止生产key/DEL/FLUSH/CONFIG/SQL，全部19/健康/config保持。所有编译/partial/logs/failure/PID-身份审计保留，执行前green+reg明确NOT_RUN。

异步Gateway合批尚待开发与正确性测试：有界input/ready/outstanding人口、执行时会话epoch+connection、Pong即时、result终态计数、队列deadline与取消、kNotFound安全恢复、关闭后排空及cache/pool销毁顺序；不能根据socketidle setter就声称全局停止保证。所有游戏/Python/其他应用保留，无内存清理必要。Gateway源码executor已经恢复运行33fc，cachebatchAPI350已证实但运行仍未使用；运行GWs1d8/33fc、Message原b24/ddc/c119、User38、MCPbc85，持久化1/1/1/0/0未触及。所有功能10k50k极致目标继续推进，当前未达到。


### 2026-10-05：命令idle修复验证完成，复现在线自恢复的交叉窗口

5063423实际 `redis-command-timeout-fixed-probe-20261005`全部通过：四healthy AUTH/SELECT/PING/EVAL成功；四无响应在ownpeer释放前自行idle失败，Ping3.051162740/Auth3.020139256/Select3.043109987/Eval3.008081565秒，错误err1/Resource temporarily unavailable，连接不可用，不能误算peer EOF导致结束。随后新prefix350原API回归各pool175全部PASS、各100并发mixedbatches、全部lease归还；原/生成harness只stage/prefix替换，原测试源码没有改。所有19身份/config/health保持，尚未部署候选。v27归档99文件/100条目 SHA615a7220afc5b3aae56382ea7d68dd9d90d330a7edafd945b076dbd4bf0fa5b1，全99本地逐文件SHA通过。3秒是单次blockingI/O空等，不是wholecommand/RPC/partialdrip/全局停机绝对期限，TXstall/drip尚未验证，没有正常175ms根因或完整容量宣称。

后续Gateway异步合批审查查实原HandleHeartbeat传ordering_key std::nullopt；原gateway.presence.offline设置user/epoch/kMustRun但没有ordering_key。因此不能假定既有presence四worker会把同一用户刷新/下线严格串行；SubmitSessionBusinessTask仅复制传入optional key，没有隐式排序。缺失记录恢复是Lua返回NotFound→检查local current→SetUserOnline→unconditional SETEX，检查与写入不是同一原子操作。

本次仅新增诊断，使用已350验证的actualcacheAPI.o与finiteidleConnection.o，objects SHA冻结、cache/header source与编译输入SHA核对，原SDK/cachedELF不覆盖。独立namespace codex:online-restore-interleave-probe-20261005:60001..60004四个全新自有键，TTLs120/300。四个确定性交叉case：缺失直接恢复control；localcheck后remote owner先SetOnline、旧SetOnline再写；localcheck后模拟unbind/current=false及ownmissingOfflineCleanup、旧SetOnline再写；原restore先写remote再写的反序control。Expectedred两窗口、healthy两控制，逐个记录typed结果/完整自有record/操作顺序；明确local-current是模型boolean，不操作或声称验证真实TCP/Gateway会话、不估计线上竞态频率或性能。ownSetOfflineIfMatch只对原missing夹具返回NotFound，不执行删除；没有生产key/直接DEL/FLUSH/CONFIG/SQL/login/服务修改。

执行前结果明确NOT_RUN。若复现，应单独审计missing-only原子Lua避免覆盖并发新owner，且给恢复与下线设置共同的user ordering/fence，保留oldconn/epoch取消和kMustRun清理；单独SETNX不能解决“cleanup之后旧恢复重新创建”的本地生命周期窗口。异步刷新不得在独立batch线程直接blind自恢复，应把恢复交回有会话取消和用户排序的presence域，再验证断线/替换/重连/队列deadline/shutdown计数。后续有界input/ready/maxoutstanding/worker collector和即时Pong仍未实现，没有极致全功能性能验收。生产仍GWs33fc/1d8、Message原ddc/b24/c119、User38、MCPbc85，所有apps/game/Python保持，无资源清理或VM/security/NIC修改。继续按证据迭代。


### 2026-10-05：缺失presence恢复保留新owner，刷新与下线共享用户排序

648ca58 actualcacheAPI窗口诊断四个自有键：正常恢复与反序remote写两控制正确；remote先写/oldrestore后写覆盖新owner、cleanup先结束/oldrestore后写重建旧记录两原问题确定性复现。local-current是明确模型布尔量，未验证真实Gateway/TCP频率。v28冻结28文件/29条目 SHAbf1f42fabdfd3773a82bf3093caaaf1cca4194ed68e3846caa7962ae7d48b4c7、本地全28 SHA通过。

本轮11路径修改前审计/原文件备份，以648为parent，添加单key原子SetOnlineIfMissing(SET EX NX Lua)，SetOnlineStatus新增kAlreadyExists尾值3、原Stored0/Invalid1/RedisError2保持，合法login原SetOnline不改；任何既有owner/坏JSON/错类型byte与TTL保留。Gateway仅missing heartbeat自恢复改用新API，失败typed告警；即时Pong保持。Heartbeat取消任务与kMustRun离线cleanup显式使用同一PresenceUserOrderingKey(userID)，保持sessionepoch/connection取消，cleanup解绑后必须执行；原restored33fc执行器调度不改。SETNX解决远端owner窗口，共同用户排序/取消解决本地下线之后重建窗口，两者缺一不可。未来异步batch线程不允许直接missing blindrestore，必须回交同域用户排序和session取消任务。

新增独立actualRedis缓存测试，privatepool1/4、全新固定namespace、最多160键、TTL300/120/expiry1；现有owner/无效记录/错类型bytes与TTL、invalid/null/uninit/ownshutdown、只有一个竞争恢复赢家、两交叉窗口；真实restoredBusinessExecutor+ThreadPool自有编译对象测试active restore→mustRun cleanup、cleanup→queued stale restore cancel及accepted终态计数，local-current仍明确模型，不能当真实Gateway会话验收。只允许explicit ownfixture SetOfflineIfMatch生命周期清理，不涉及其他记录/直接DEL/FLUSH/CONFIG/SQL。原350APIharness仅freshstage/prefix替换并冻结源/生成SHA，复验缓存API扩展影响。全部编译/链接180s、测试90s有ownPID/starttime/PGID/cmdline守护，失败/部分结果保留。

提交时测试NOT_RUN，完整Gateway构建及真实会话交叉回归、异步有界队列生命周期、匹配150/s压力尚待执行；生产19身份/config/health均保持，GWs33fc/1d8、Message原ddc/b24/c119、User38/MCPbc85，durability1/1/1/0/0保持。命令3sec仅perI/O idle，不是wholecmd/partialdrip/全局停机绝对时限；先前4healthy/4stall+350API已绿但未部署。所有apps/game/Python保留，不清理/删文件/改VM/security/NIC，不重跑已拒绝callback/worker32/groupcommitdelay或跳过healthyPing。10k50k所有功能性能尚未达预期，继续凭证据迭代。


### 2026-10-05：deef66c安全恢复136+350全部通过，接入默认关闭的有界在线维护合批

实际online-restore-missing-fence-test-20261005 pool1/4各68、合136全部PASS；original350 APIharness只freshstage/prefix替换各175全部PASS。真实Cache/33fcExecutor/ThreadPool编译自有对象，remote owner写后oldmissing restore保留bytes/TTL、8竞争仅1winner、active oldrestore后mustRun cleanup最终missing、cleanup先active后的queuedstale取消且0IO、排队deadline过期0IO、accepted终态2/2归零。local-current依旧受控模型，不代替完整Gateway真实会话。v29冻结70文件/71条目 SHA88db90de3b379b64facbc8489a4a1bec977e496a675226648a22c9160dec1d36，本地70逐文件SHA全部通过。

本轮11路径预审计后添加headeronly OnlineStatusMaintenance：单collector、4worker/max16/5ms；总pending包含input、ready、执行/callback，不允许独立队列突破现有presence容量；TrySubmit不等Redis。10sec beforeI/Odeadline，epoch/connection current前置及callback前取消。所有accepted只记一个completed/cancel-beforeIO/cancel-beforecallback/deadline-beforeIO/workerexception/completionexception终态；typedRedis结果独立计数，不重复terminal。worker只ownerchecked EXPIRE、不创建记录，NotFound回交原presence userordering/cancelable RefreshUserOnlineIfMatch→missing-onlyNX；cleanup原kMustRun保持；恢复提交继承原心跳received时间防止重置预算。即时Pong保持。

默认关闭，只有exact TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE=1开启；Bootstrap loop结束先Stop维护并join所有worker/callback，再drainpresence/cache，Gateway.Stop/析构幂等补偿。Stop不声称绝对30sec；Redis3sec仍只是I/Oidle，部分响应/全局截止未证明。新增realcache生命周期受控测试包含capacity/inflight/ready上限、shutdownadmission、cancel/deadline/exception隔离、批量关联和计数；独立编译完整GatewayServer+gateway_demo及exact restoredExecutor、新Cache、finiteidleConnection覆盖cached对象，避免rejectedfair旧library，原SDKlibs/ELF不覆盖、不CMakebuildMessage84candidate。

提交前上述新测试及完整Gateway编译NOT_RUN，仍须现场会话回归和匹配150/s压力评估。静态本地准备attempt1使用不存在KickOldConnection作边界而失败，仅已审计header写入；原generator/failure/部分headerSHA保留，按真实PushOfflineMessages边界修正并从preimage确定性生成，未发生guest写/编译/运行失败。单组件ABBA此前nativeCPU-7.45/8.75%、Rediswhole-0.237/0.240cores是真实受控收益，freshness P99升至约8ms；不是privateACK/全功能/50k容量验收。本轮无部署、19身份/config/health不变，Message原ddc/b24/c119 User38 MCPbc85、SQLdurability1/1/1/0/0保持。所有apps保留，不清理删文件，不改VM/security/NIC，不重复已否定盲调参。所有raw/失败/代码Git保存，继续验证及迭代。


### 2026-10-05：真实合批生命周期224与完整网关构建通过，准备镜像/端到端验证

38f41a9 actualmaintenance/cache pool1/4各112合224全部PASS：pending覆盖inflight/input/ready且上限8，overload与shutdown立即拒绝，beforeIO取消/deadline/probeexception、afterIO取消/callbackexception隔离，每accepted恰一终态；四heldworker受控full16两group结果关联和owner保留，typed mismatch10/refreshed26/36completed，backendthrow与cardinality不符不影响后续任务。fullgroup测试collector500ms只是受控排队，不是性能测量，正常capacity用default5ms。完整Gateway/demo自有编译链接PASS，ELF c2894a21cf308ad35ef665c603d17b1a2577c9216aa9e74856772befa99d640f、原cached库/ELF/19configs完全保持；own33fcExecutor/deefCache/506Connection前置，未build/deployMessage84。

v30导出在生成archive/audit前失败：fullGateway link.map33533003bytes超过generic20MiB；ELF33951840正确先排除，不涉及源码/224检查/构建失败。全map/raw仍保存。新v31仅已知该自有map路径允许64MiB，其余nonELF仍20MiB，密钥精确RAM扫描及SHA保留；v30归档不存在，不能声称已下载。失败及解决保留本地audit/online-maintenance-export-v30-failure-before-v31.json。

本轮10路径仅工具/文档预审计：封装only38Gateway ELF到原1d8 base新tag、UID1000 no-net/no-cap只读loader/missingconfig控制；先原10k/150/s60秒9000 baseline，候选部署仅两GW增加exactbatch_enable=1且保存privateinspect/log、原env/rollback指令，其他17/config/worker16/recovery/fanout/SQLdurability1/1/1/0/0保持。回滚先capture候选log，capture失败仍回滚，恢复原1d8及flag缺省；保留全部候选/rows/log，不prune/delete。原matched压力工具精确改GW身份，新run batch150A1/B1/B2/A2，worker5d6bd183及原Messagec119固定，activewindow readonlydigest/wholecgroup+gueststat/PSI、all9000完整attempt/positive/wire/SQL/HB相等gates、每个FAIL保存。源helpers提交前全NOT_RUN。

现场已有功能链用新519880/882/884/886四个synthetic身份，执行前确认socialpair为空，正常friend/private/read/group/file+wire/durable/checksum，仅样本回归非TLS/MCP/AI/offline/fault/50k全验收。后续TTL缺失/旧连接下线/跨GW替换会话测试另审计。3secI/Oidle非wholecmd/全局停机绝对deadline，不从组件少命令推断端到端极致。若端到端不能重复改善则还原并准确分析剩余瓶颈；不盲重复callback/32worker/groupcommitdelay/skiphealthyPing。所有apps/game/Python保留，不清内存删文件/改VM/security/NIC，源迭代Git和原raw完整留存。全10k50k所有功能极致尚未达到，持续推进。


### 2026-10-05：原10k150基线P99仍180.5ms，真实会话交叉验证候选已健康

5dc3cb9已提交镜像/控制工具；sealedimage a8b7d5ea6446a2fdbedac0f3ebbbfb07579155ec19b819959d96eb0262aeb6a9仅38f41Gateway ELF c289到原1d8base，UID1000 loader及expectedmissingconfigexit1通过，全部旧SDK目标保持。v31 source/lifecycle/fullbuild54文件/55条目 SHA00a187fd2dd95d1dcf75e912b733f322d6ef14e274f71804b35612df19182e09全54本地SHA通过，完整map保存，v30大小失败未丢证据。

baseline batch150A1实际完成、privateexit2/FAIL：10klogin、9000attempt/positive/wire/receiverACK及SQLconfirmed全部正确，无negative/skip/late/disconnect，HB82612/82612；P99180.5、scheduledP99181.7、max346.497ms，active8990/60=149.8333/s，仅延迟gates失败。不能把completed状态当性能PASS；两个activewindow readonlydigest/19wholecgroup/gueststat/PSI等raw已保留。候选尚未测性能。

双Gateway候选部署a8已健康，gateway-a ecb08556be0f75ec39fd4b6e2fc1104479b3bc37d21db7455257acc9c136ed56、gateway-b a7fedf6c9293de88f7da6cf7b3b63a22f588b76e818e9b491a85d4e78d1a925f；其他17精确身份/启动/privateconfig不变，env只新增batchEnable1，原worker16/recovery/fanout/pools/SQLdurability保持，privateoldlogs/inspect和原1d8rollback已保存。此轮source提交不再部署或改productionCPP。

7路径预审计新增真实twoGW session actor，existing synthetic519890/519892 readonlySQL精确验证username/status及在线key初始missing；仅自有会话正常login/HB/close和全recordCAS EXPIRE1（每次mutation前独立audit）强制过期，privateRedis密码onlyRAM、无DEL/SQL直接写/其他key/全局设置。验证missingrestore、同GW替换/旧EOF、新owner不受旧cleanup影响、跨GW接管后旧HB/旧close不覆盖新owner、新ownerclose/inflightmaintenance和expiredownsession32HBclose不复活；主动关闭burst只报告clientwrites不伪称serveraccepted，offline watch10samples/1sec明确限制，不估算竞态频率或全负载P99。Probe原始body/owner/PTTL/wire/seq以及失败完整保存，90sec ownPID/starttime/argv/PGID守护，无其他应用停机/清理。

提交前liveactor/现成功能链/候选150性能NOT_RUN，继续功能门控后再测匹配负载。新v32仅明确完成/失败自有stages，冻结所有raw与源工具；既有Messageddc/b24/c119、User38、MCPbc85保持。3secI/Oidle非absolutewholecmd/globalshutdown proof；全10k50k所有功能极致未达到，持续凭端到端CPU/SQL/latency证据迭代，不盲重复旧被否定方向。所有apps/game/Python保留，无任意删除、VM/security/NIC变化。


### 2026-10-05：真实双GW36检查PASS，功能链0操作fixture碰撞修正为只读选择新pair

daf296c真实online-maintenance-session-control实际36检查PASS、18个Pong样本：ownTTL CAS过期后missing恢复原conn，sameGW替换旧EOF/newconn保护，crossGW接管后旧8HB及旧close保留新owner，newownerHB后close最终missing/10samples1sec不重建，第二ownUID TTL过期后32clientHBwrite立即close最终missing/10samples。主动断开burst没有伪称serveraccepted，实际2UID功能交叉样本不等于竞态频率或完整压力P99。全部19/config完全保持，无其他账号缓存/SQL写/全局配置操作。

batchfunc1原功能链在built-inpreflight因已有socialpair记录而停止，operations_completed=0，无login/friend/group/file变更，原失败result/trace/log完整保留。这是fixture选择碰撞，不是业务实现失败或性能退化。只读在reserved519800..519950范围检查exactusername/status、所有双向relation/request和RedisEXISTS0，验证新pairs519800/519803、519801/519804，未执行SQL/Redis写或重置。7路径audit新增freshbatchfunc2/functional attempt2包装，原cross_feature_actor不改且执行前再次同样fixture验证；仅正常公共API创建own新记录/explicitown featuredisband/cancel，所有history/files保持。ownactor300sec PID/starttime/argv/PGID保护，fileRPC子进程继承自有group，最后runtime/config核对。候选38/a8两GW保持，其他17未变化。

新v33明确保存batchfunc1的0操作preflight失败、36真实session与attempt2所有raw、原10k150A1 P99180.5/scheduled181.7FAIL和后来候选控制/rollback有则冻结；不覆盖任何旧结果。提交前attempt2和candidate150均NOT_RUN，功能门控通过后再匹配测量；极致所有功能10k50k未达，不把completedstate/小样本PASS当容量验收。Messageddc/b24/c119、User38/MCPbc85、SQLdurability1/1/1/0/0和所有apps/game/Python保持，无清理/删除/安全/VM/NIC更改，持续准确分析和Git迭代。


### 2026-10-05：候选10k150 ACK P99首次180.5→104.4，继续同窗瓶颈核对

30bfa3d已提交fixture碰撞修正与只读选择；freshbatchfunc2真实49operations/35assertions全部PASS，4actors519800/803/801/804各HB1/1，friendaccept/reject/private幂等wire/history/read/group权限/fanout/file真实bytes/checksum均通过；原batchfunc1零操作已有关系preflightFAIL完整保存，36真实twoGW sessionPASS保持。仍是功能样本，不是全功能50k容量。

actualcandidate batch150B1 completed/privateexit2/FAIL，原同10k15060秒9000负载全部login/attempt/positive/wire/receiverACK/SQLconfirmed，negative/skip/late/disconnect0、HB82196/82196；P99104.4、scheduled106.0、max178.393ms、active8991/60=149.85/s、within100ms8884/9000。baselineA1相同完整gates、HB82612/82612、P99180.5/scheduled181.7/max346.497/within100ms8155，首次ACK尾延迟降42.16%，明显但仍不达100门槛和极致目标；没有把completed当PASS。候选B2/A2尚未执行，不能用单次sharedhost差异证明可重复极致。

本轮8路径预审计只新增分析/保全工具，产品source/CMake/运行19/privateconfig不改。按SHA冻结2窗口19wholecgroup CPU、gueststat/CPU/IO/memoryPSI、SQLdigest ps差分/count→mean-ms/1e9及captureduration/activewindow界限；原100us histogram/完整9000 send→ACK/wire MID/CID/UID/seq严格关联，signedwire-minusACK允许先投递再senderACK的合法负值、不筛掉。缺失/失败窗口明示unqualified，不假造9000分位；mean不是P99或exclusiveRPC CPU，内核waitshare不是CPU，wholeRedis/MySQL包括background，guest压力不独占指向IO/调度。

新增readonly阶段日志采集：baselineGW来自部署前privatefullLog，candidateGW与原Message按A1UTC→当前区间保存privatefullLogs，再仅提取matchedpositiveMID允许numericfields；chatUID/seq/epoch>0、peer内部UID/epoch0必须按实际上下文判别，persistence字段mid/from正确。Gateway>=100ms/rate8及repo>=10ms/rate8偏置样本不当全体P99，不拿不同人群均值相减声称瓶颈占比，零日志不意味着零工作。分析结果提交前NOT_RUN，不凭希望宣称CPU或commit收益。

候选日后回滚时原before-log捕获会丢SIGTERM后drain统计，本轮添加独立wrapper预审计2个ownDocker logs --follow进程到runtime-private再调用原已审计2GWrollback；通过stream保留oldcontainer停止/移除期间真实batchaccepted/terminals/pending/maxbatch日志。日志失败不阻止恢复，只有ownPID/starttime/argv/PGID验证后关闭followers，缺失line只算未证实；原rollback按精确1d8/flag缺省/worker16/other17/config保护，source旧helper不改且所有raw/失败/rows/files保留。v34显式扩展analysis/source/phases/drainrollback和未来B2/A2完整/失败记录，不覆盖旧归档。

当前a8/38候选两GW healthy，Message原ddc/b24/c119 User38 MCPbc85 SQLdurability1/1/1/0/0保持，所有apps/game/Python保留，不删文件清内存/改VM/security/NIC。先分析同窗机制与剩余长尾，再根据首次明显改善决定B2/A2复制验证或下轮具体修复，不能盲重复加worker/callback/groupcommitdelay/跳healthyPing。全10k50k所有功能极致未达到，继续准确分析并Git迭代。


### 2026-10-05：完整10k150 ABBA确认维护批处理收益，仍继续长尾优化

b911b2b的分析/私有日志保全/停机持续stream工具实际执行成功。四轮相同10k登录、150/s×60s=9000原负载，Message原ddc/b24/c119、原worker5d6bd、durability1/1/1/0/0、other17/config/apps保持。每轮9000发送/正ACK/实际wire/receiverACK/SQLconfirmed完整；negative/skip/late/disconnect0，HB82612/82196/82136/81481分别精确相等。counter两快照均在60秒activewindow，完整ledger严格UID/CID/MID/seq关联，未丢弃合法wire先于senderACK负差。

|窗口|ACK P99 ms|实际收取 P99 ms|send→ACK mean ms|Redis核|两GW核|19容器核|CPU PSI some %|
|---|---:|---:|---:|---:|---:|---:|---:|
|原版 A1|180.5|214.744504|50.730026|0.539518|1.370194|5.152189|55.254434|
|候选 B1|104.4|123.064594|38.105161|0.371591|1.234038|4.918230|52.355852|
|候选 B2|110.0|127.127925|36.439514|0.362574|1.231716|4.893547|51.058919|
|原版 A2|153.7|180.573642|44.439333|0.541070|1.343698|5.088737|53.711161|

候选两轮ACK P99均低于两轮原版；对应A1→B1下降42.16%、A2→B2下降28.43%，原版自身存在时段波动，不能将单次42%宣传为全负载稳定收益或对四个P99取均值当总体P99。实际收取同步改善，维护Redis/网关CPU下降重复出现；MessageCPU0.8651/0.8883/0.8803/0.8516，MySQL1.3594/1.4041/1.3953/1.3478未下降，wholecgroup含background不能归为单RPCexclusiveCPU。SQLCOMMIT均值5.4849/4.8350/4.6189/5.2173ms，不是commitP99；guestCPU busy81.53/79.23/78.99/80.58%、约290–296fork/thread每秒以及高CPU排队仍存在；memoryPSI近0，不能把本轮归为缺内存、也不删除应用/缓存。

全部四轮privateexit2/FAIL，100ms两门槛未通过；scheduled P99181.7/106.0/111.6/155.9。仅固定plaintext私聊场景，不是所有功能20k50k/AI/TLS/离线/故障/长稳PASS。真实session36checks/18Pong和functional49operations35assertions保持PASS，原fixture碰撞零操作FAIL保留。已验证源38候选可据真实ABBA条件重新保留，helper要求maxcandidateP99<minbaselineP99、完整所有nonlatencygates、real功能/会话/两drain证据；本sourcecommit仍不改变运行，后续独立预审计精确a8/c289复用，仅2GWs且other17/config/env不变（仅batchflag1）。

两候选GW退出drain实际accepted=terminals分别82179/82207，pending0/maxbatch16/peak23与32/Rediserror0/overload0；A completed82179，B completed82168+cancelIO24+cancelCallback15=82207。batchcalls21748/21741、batchitems82179/82183，合并164362items/43489calls≈3.78items/call，是候选存活期含真实session/functional与两容量轮的统计，不能充作某个60秒窗口的callcount。Missing1/16及Mismatch8/0包含真实过期/takeover测试，不是消息丢失。恢复原GW1d8健康，新CID b1f7b4/913a624，other17/privateconfigs精确保持；候选完整私有日志已在恢复前留存，停机stream全部验证，避免了旧日志捕获缺失。

现有Gateway慢样本>=100ms/rate8与repo>=10ms/rate8是不同偏置人群。A1 chat249/repo488只共享MID23；B1 chat29/repo488交集0；B2 chat47/repo486交集2。Gateway慢样本persistRPC均值79.838/91.007/79.832ms，仅说明已抓到的慢请求中该段突出，不能减去不同仓储人群18.765/16.297/15.433ms而宣称transport占比，更不能用waitshare当CPU。新增strict sameMID/UID/seq/绝对时间配对helper，按每条send→repo start→repo end→ACK分解并检查容差；rpc-minus-repo明确包括应用、响应、传输、调度等，不是纯gRPCCPU。交集不足限制本轮因果分解，下一步针对性补证据或隔离机制，而非盲worker/callback/commitdelay矩阵。配对helper尚NOT_RUN，在新commit后执行并另保存实际结果。

v34实际已导出294文件，SHA218b982f8b164f49fa95ff934c2b646462075556f22eedfb09c3e667a03a0f64，完整四轮raw和failedfixture/functional/source/phase/drain恢复在内；排除privateconfig/env/fullLogs/ELF并RAM扫描，local下载/逐SHA解包待执行，不能提前宣称本机验证。v35新增本source/配对/有效候选保留stage的显式完整或失败守卫，旧归档保持。持续迭代目标未达，不以组件PASS或helpercompleted替代业务延迟验收。


### 2026-10-05：保留ABBA验证候选，隔离验证真正CompletionQueue而非重复callback路线

163d86d提交实际完整ABBA与仍FAIL边界后，strictsameMID配对实际完成：A1仅23交集，ACKmean147.960ms=repo前71.801+repo49.301+repo后26.858；RPC75.151中非repo25.850ms（应用/响应/传输/调度混合，不是pureCPU）。B1交集0，B2仅2，ACK124.811=54.247+54.002+16.563，RPC84.696/非repo30.694、commit15.533。全部从同一条消息直接算出，不减不同人群均值；选中的极少slowbiased请求不能代表9000总体或单一根因。候选retainedstage实际a8/c289 compiled38两GW健康，CID3f19b66/a4159ac，仅batchEnable1差异，other17/config原样。v34本机295entries/294SHA全部验证通过，旧rawfailure/重叠前后/停机证据完整保全。

本轮7路径先审计，仅新增isolatedcompletionqueueprobe工具/文档，不改产品CPP/CMake/任何运行ELF，Messageguarded84仍未部署。根据保留的原Sync/Callback负收益及guest约290–296fork/thread每秒，提出待检验机制：真正AsyncService+CompletionQueue能否比Sync减少框架开销并降低尾延迟。参考官方 https://grpc.io/docs/languages/cpp/async/ ，2CQpollers只处理accept/finish标签、16固定worker做相同20ms睡眠+128byteecho，所有syntheticindex/字段逐条核对。每call双标签生命周期保持到Finishcompletion；未绑定accept的ok=false回收；serverShutdown→workerdrain→cqShutdown→pollerjoin，normalcaseaccepted/workercompleted/finished/对象回收账目严格核对。队列512是inflight+queued总数，不改deadlines/重试/SQL策略。

同既有原型16clients/300/s/20s/6000×ABBA、Sync1CQ MIN1/MAX2精确一致，127.0.0.1:0自分配端口、cached生成proto/grpc库、一台owncompiler，新stage隔离object/ELF，不链接任何productimplementationarchives/no真实DBRedisAI/用户，all19/配置保持且>2GiB/noactivecapacity。输出CPUuser/system/上下文切换和每条caller/handler/handlerCPU/extra/lateness/raw，全失败保留；不使用traced性能或把线程减少当性能结论。CPU是同进程client+server、额外wall含固定workerqueue，不等于真实MessageexclusiveCPU。normal机制不证明production取消/过载/SQLdeadline/故障/持久化/全关停契约，AsyncContext无done通知时不调用IsCancelled；产品路径仍原Sync保持。

CQ机制尚NOT_RUN，只在实测CPU/延迟同时值得时才考虑生产具体实现，并需要领域/生命周期/同负载端到端复核。若无改善停止这个方向但继续其他准确瓶颈，不重复盲加worker或callback路线。v36显式保存本source/probe完整或失败及上一retained/join记录，原归档不覆盖，私有数据/ELF排除和RAMsecret/SHA守卫保持。全功能10k50k极致仍未达到，继续迭代。


### 2026-10-05：真CQ+blockingworker负收益已记录，先验证SQL多往返机制

3469f73真实CQ四轮6000各/24ksynthetics全部正确。Sync A1/A2 CPU0.517045/0.500381核、callerP9922.828491/22.707419ms、matchedextraP992.460499/2.381103ms，423/438handlerTIDs；CQ B1/B2 CPU0.568523/0.581996核、callerP9922.850198/22.954109ms、extra2.543100/2.598802ms，16handlerTIDs，voluntaryctx82863/82905 vs64718/64613。CQ每轮accepted=submitted=completed=finish6000、allocated=deleted6001/oneunboundacceptcanceled/live/pending0/peak7，无漏对象/反例/异常，但CPU更高且尾延迟未改善。拒绝这个CQ+blockingworker方案进入产品，不把固定线程少当性能、不泛化所有异步策略无效。所有raw源码/编译/账目/失败限制保全；v36已352文件SHAe3c1e0c249c3d9466f5330d94677cce0b741e75cf7b920322e1f45f5ced08982，本机下载尚NOT_RUN。

只读SQLpreflight实际8.0.40，durable1/1/1/0/0，原连接已经CLIENT_MULTI_STATEMENTS，原两表无trigger，private有两FK→im_users；原Query只读首结果而Execute会消费多结果，不能直接用原Query拼接SQL而遗留结果/忽略错误。官方多结果契约 https://dev.mysql.com/doc/c-api/8.0/en/c-api-multiple-queries.html 要求逐个结果消费和区分next_result=-1/0/>0。本轮8路径先审计，只新增native成本probe和记录，不改产品driver/API/CMake/运行代码。提出待验证机制：PING+precheck+BEGIN维持，同INSERT/identitySELECT/outboxINSERTSELECT/COMMIT四条SQL一次packet vs四次（含PING每条新消息7→4native请求），无skipPING/commitdelay/弱化ACK。

在单独两张absent且fixed命名codex_sql_rtt_messages_20261005/codex_sql_rtt_outbox_20261005上做真实durableInnoDB提交；原表LIKE复制列/索引/check/charset，LIKE遗漏FK所以只在ownmessage表加两个ownnamed同RESTRICT/CASCADE FK→原已验证synthetic700001..710000用户，原用户/消息/outbox/schema数据不改、normalprojector/relay不会读own表。DDL之前单独审计所有语句/源SHA/原schema/index/无trigger/own名不存在/10000用户名status1，然后3ownDDL分别再写audit；任何失败保留partial，不DROP/DELETE/TRUNCATE/重建。配置密码只读RAM，不写配置/环境/密钥。

16专属native连接同原clientflag/utf8mb4/5sI/O，300/s20s6000×ABBA，24k own消息+24k ownoutbox全部提交保存、全字段/JSON/uniqueMID/CID/计数/事务结束验证，所有raw单条CPU/阶段/迟发及SQLerrno保全。Native-Docker线路不是product路线，processCPU是caller，wholeMySQLcgroup含background/连接setup/只读audit，不作exclusiveCPU推断。业务完整性、领域EventCodec、并发幂等/注入错误/identitytampering/提交不确定恢复还需要产品后续特定回归；component正常成功不等于生产协议/全10k50k。完整比较前NOT_RUN，无preset收益结论。all19/config/currenta8candidate/noactivecapacity/>2GiB/>1GiBdisk守卫，原索引/durability前后保护；一编译器ownELF/noSDK，无任何production archives链接。v37显式source/preflight/probe所有complete或failed/ownDDL记录，原档案保持。继续针对实测瓶颈，所有功能极致尚未达到。


### 2026-10-05：SQL往返实测收益及提交前校验边界

becd组件正常ABBA各6000真实durable own表事务。Single A1/A2 caller mean6.993567/6.965908ms、P9912.320544/12.148644ms、client CPU0.471217/0.467212核、MySQL wholecgroup1.213829/1.212815核；fourSQL batch B1/B2 mean6.014814/6.169501ms、P9910.398923/11.185632ms、client0.411941/0.424788核、MySQL1.048054/1.069916核。PING/precheck/BEGIN保持，native7→4，对照一致24k消息和24koutbox保留，failure0；MySQL cgroup包含背景/连接setup，native线路非RPC，不外推全负载P99。v37实际426文件已封存SHAa769f17f9bc17ec1c84dd0cac56ca1a2c563a2133df9b7a51ff6ac2422b8e66d。

审查发现上一成本probe在combinedCOMMIT后才校验C++记录，正常路径虽正确但不能作为产品API：错误记录必须在outbox/COMMIT前拒绝。拒绝直接移植四SQLbatch，原源码/结果保留。新7路径审计只新增safe组件；同6SQL/PING/precheck两控制均先校验全部record和insert_id再单独outboxINSERT/COMMIT。只把START TRANSACTION+INSERT+identitySELECT3语句单包，native7→5。使用既有两own表，schema/index/FK/24k旧行和无新CIDprefix先核对，不DDL/delete/原消息写。4固定轮300/s20s6000共追加24kown消息/outbox，记录新总数48k，不覆盖旧行。两negative fixture（identity projection故意错recipient、中间SQL1054）均需校验拒绝/rollback后0行/同connection SELECT1可用，测量前counter重置。组件仍不证明全生产EventCodec/幂等竞争/提交不确定/故障恢复，后续产品需逐项回归；本轮未执行前不预设收益。原durability1/1/1/0/0、所有19容器/config/其它应用保持。代码/每条raw/失败/审计新阶段独立Git及v38保全。


### 2026-10-05：安全事务前半段实测通过，产品默认OFF候选

6cb safe组件ABBA各6000：singleA1/A2 mean7.466654/7.338825ms，P9914.603638/13.306077ms，clientCPU0.479838/0.482842，wholeMySQL1.268399/1.285254；safeB1/B2 mean7.101917/6.846974ms，P9912.756773/12.587563ms，client0.464834/0.454095，wholeMySQL1.227017/1.187353。两候选尾延迟及CPU均低于两个控制，但不外推native组件为端到端收益。新增24k消息+24kownoutbox全配对/零失败，合计48k/48k保留。各轮两negative：错误recipient投影及第三SQL1054→提交前拒绝/rollback0行/同连接SELECT1可用，八次PASS；中间失败不是完整故障或提交不确定认证。

新的14路径源审计实现默认OFF TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE=1：只合并START TRANSACTION+INSERT+完整记录SELECT，完整C++原BuildMessagesFromResult与same_identity验证后才原领域EventCodec/outbox/COMMIT。PING/precheck/唯一键竞争/释放lease后recovery、outbox失败rollback、commit outcome ambiguous recovery保持原逻辑。专用driver逐一消费恰3results，记录BEGIN/INSERTpartialack；server statementerror留可回滚事务，client/未知protocol关闭自身无COMMIT session，绝不假成功或盲重试。新增combined phase独立标记，不把组合时间归因单独INSERT。原路径默认OFF保留。

准确核对Git发现84拒绝的guarded ReceiverConfirm实际在Adapter/ApplicationService/Port/Adapter.h；MessageRepository.cpp本身与ddc无差异。先从ddc原Git对象SHA核对恢复这四文件，再仅给Adapter加新persist分支，保留旧实验完整Git历史。未将错误文件名当作恢复依据。所有19运行容器/私有config未改，产品新源码尚未部署或验收；独立真实SQL回归/transportfault/端到端控制后才决定是否保留。原MySQL8.0.40/1/1/1/0/0/index13维持，游戏及其它应用保留。


### 2026-10-05：新候选独立构建与真实SQL回归流程，执行前审计

ed90提交产品默认OFF候选及真实fault测试，运行仍a8Gateway/b24Message不变。新增7路径工具审计。独立构建只新stage的四own对象Driver/MessageRepository/Adapter/ApplicationService和8个测试ELF，用真实缓存flags，own符号在SDK/产品archives之前。借用全部archive逐个SHA前后相等，既有cache/CMake/产品ELF完全不写。原trace/应用/CrashWindow/receiverACK/chatACK五unit，加真实outbox集成/unread聚合/新增batchfault；单编译器180s守卫及unit60s真实SQL90s，所有PID/start/argv/PGID记录且只停本任务子进程。

四个absent ownschema codex_private_batch_20261005_off_p1/off_p4/on_p1/on_p4，DDL之前原SHOWCREATE元数据/FK仅指各schema ownUsers、两own用户10001/10002、计划SHA审计；无生产rowcopy/grant/index/DDL配置变化。实际领域event/并发幂等4thread/pool1回收/outbox失败回滚/接收确认和read对照flag0/1。Pool1附加新driver partialACK、1054/1062、额外result关闭session、包含引号/分号/中文content、正确解析但错type和malformedtime必须outbox之前拒绝。session TEMP shadow只在ownschema，关闭自身连接自然释放；没有DROP/DELETE/TRUNCATE/reset指令，所有durable成功测试行和schema保留。private root配置0600只存runtime-private，不显示/导出。COMMIT真实响应丢失专用case已有但需独立只拦自身连接的transportfault审计后执行，当前NOT_RUN，不能称提交不确定认证通过。本轮执行前BUILD/SQL均NOT_RUN，不伪造结果。v39新增所有stage complete或failed、原源码基线及sourceaudit显式保全。全功能10k50k未验收，端到端比较尚待正确性回归。


### 2026-10-05：独立构建首次工具前置失败保全，精确主对象选择后重试

16b构建在compiler启动前的linkplanning断言失败，private_receiver_ack_tests实际除test main还直接链接ReceiverDeliveryTracker.cpp.o；工具将全部cppobjects数量假定1错误。保留原stage四份link snapshots/实际FAILmarker/runtime19/config一致性；无编译/SQL/schema或产品改动，不伪造PASS。新7路径source工具审计，main改为精确matchingtarget.dir/tests/唯一cpp object，额外tracker object原样保留且与所有借用archives一起SHA前后核对。earlypreflight审计提前到任何link snapshot写入之前，planning异常保留failedmarker，另起attempt2 build/SQLstage，旧helper不覆盖。所有ed90产品源码未改；四ownschemas尚未create，运行服务不变。重试尚未执行时BUILD/SQL仍NOT_RUN，v40明确包括initialfailed及attempt2source/build/SQL complete或failed，不删除失败链条。


### 2026-10-05：四产品对象实际编译成功；无库测试链接工具修正

9be attempt2四个新产品对象Driver/Repository/Adapter/ApplicationService和四个test main实际编译成功；前三个测试ELF已链接，但尚未执行unit。随后private_receiver_ack_tests无libtinyimx archive，寻找首次library的位置StopIteration。private_chat_ack_boundary_tests也仅main无库。是工具链接布局假设失败，非CPP编译或业务失败，全部8objects/3ELF/commands/raw/failed/runtime-before-after保留，真实SQL未开始。新attempt3使用next(iterator,len(link))明确支持无库目标；借用own先前编译对象先要求common/services/tests自9be无任何源码或头变化、每个对象SHA前后相同，再只读复用，不重复编译。仍保留extraTracker缓存object哈希及全部SDK不变，剩余main才编译，所有产物写freshstage。新工具source审计/版本+v41含两次失败及三次stage，产品源码ed90未改、运行无变化；unit/SQL尚需实际执行才可PASS，不删失败或掩盖为产品性能结论。


### 2026-10-05：新增fault测试Config接口编译错误精确修正，复用七个完成ELF

317 attempt3余下六个existingtest和初始已完成对象全部链接成功，总共七个existing ELF。最后新fault main编译明确报Config无Instance，项目Config为普通对象；是新增测试误用API而不是业务编译失败。保存完整compiler原错误和failedstage，不覆盖。新7路径审计仅测试一行改tinyimx::Config config; 无common/services变化。final stage先证明自317仅这一个test源码变化、七个ownELF/四product对象/借用SDK和tracker SHA，再复制7sealed ELF到freshpath（保留原件），仅编译新main并复用实际已有outbox link命令替换main/output。执行五unit后才PASS，四ownschema SQL检查仍独立未运行；无重复整体编译/SDK安装/生产SQL或运行变动。v42明确加finalsource/build/SQL及三失败保全；尚未执行不填结果，所有性能/全功能验收仍OPEN。


### 2026-10-05：真实unit发现恢复旧路径带回非法状态缺口，单因定位修正

503 final新fault main编译和链接成功，trace17PASS，应用unit出现InvalidStateFailsClosed/LookupFailureDoesNotMutate两失败。准确源码/test核对：ddc旧ConfirmReceiver switch没有default，enum99落到repository Confirm而confirm_calls变1；后一断言共享counter被前次非法mutation污染，lookup错误本身的earlyreturn正确。因此是一处缺失default，不是两个不同SQL/查询故障。旧guarded实验helper原有非法状态拒绝，恢复整文件时同时恢复了这个旧缺口。承认并保全真实失败日志，不删测试、不把批SQL当罪因或重启重测掩盖。

新7路径审计仅App switch default返回InvalidRecord，不SQL/lease/retry/API/vtable改变，仍原read-first确认，不启用被拒绝的guardedUPDATE性能策略。只重编译App一对象，三个persistproduct对象+八testmain/所有借用SDK逐个SHA前后相等，旧四个失败阶段及每个completed产物保留，freshconfirm-fixstage重链8ELF。全部五unit后才PASS，四ownschema真实SQL之前仍未执行。v43显式保存fixsource/build/SQL和所有旧fail，原运行b24/全部19/config不变；当前不能声称极致全功能验收或COMMITresponse故障验证通过。


### 2026-10-05：130unit实测PASS，真实SQL第一次仅旧策略断言失败

b32独立重链五unit实测trace17/app53/crash28/receiverACK10/chatACK22共130PASS、failure0，全部借用对象/headers/archive不变。原source恢复错误非法状态缺口已正确修正，仍read-first确认。随后真实SQL OFFpool1执行唯一失败断言“Pending normal path avoids SELECT before update”；其余实际domain幂等/outbox原子回滚/权限/缺失/坏记录/receiver/read/竞争行为全部PASS。旧断言来自84被否定的guardedfastUPDATE，是实现开销约束，与主动保留read-first策略不匹配，不是新batch故障（flagOFF）。保留旧testGit和failedraw，不能以它要求重新部署已拒绝优化。

新7路径审计仅把该策略assert改为OBSERVE真实Com_select before/after（无法测量写unavailable，绝不假0），保留所有业务断言。只编译一个outboxmain/relink，七其他ELF/四product对象和所有借用库同SHA，130unit原始结果基于同sealedcode复用无需重跑。产品common/services未改。SQLv2四新名字codex_private_batch_20261005_v2_off_p1/off_p4/on_p1/on_p4，旧实际已创建off_p1库/成功事务/坏记录和outbox保留；旧另外3仅planned未创建，不伪称四库都存在。新轮所有ownschema先查absent/DDL计划再执行，不重建/清旧库。v44保全真实failedSQL、修正前测试和v2结果，未知COMMITresponse仍NOT_RUN；无新capacity或产品部署，all19/config/原1/1/1/0/0不变，全功能50k验收仍未达成。


### 2026-10-05：真实fault first14PASS，临时selfLIKE1066及缺失failfast修正

dc36真实v2 OFFpool1 domain55PASS/0FAIL（实际SELECT记录保留）及unread聚合14PASS/0FAIL；新driver第1..14检查全部PASS：三results转义/transaction、nested拒绝、1062partialBEGIN、1054partialINSERT/rollback0行、额外结果retire+原PING重连、真实adapter含引号分号中文重复同MID。随后ownsession CREATE TEMPORARY im_private_messages LIKE im_private_messages实际1066 Not unique table/alias；harness没有failfast继续ALTER ENUM，existingowncopy第6行转换1265截断（之前业务回归故意放坏type），继续后续ALTERcreated_at改变了ownv2-offp1复制表，并产生错误fixture的own正常durable行/outbox。配置C++前缀硬守卫仅ownschema，所有运行/privateconfig一致；保存真实私有错误allowlist（密码先扫描不输出）与实际ownschema状态，不能把这些夹具错误说产品batch失败，也不能假说仅TEMP变化。旧own表不修复/删除/重建以保留失败。

新7路径审计只改faulttest，显式10列ownTEMP InnoDB含from/CID UNIQUE和ENUM('2','1')，避开同名selfLIKE；创建或ALTER夹具失败立即return，不能再继续修改普通copy或写业务fixture。第一driverTEMP创建也failfast。C++原Parse/same_identity仍需真实wrongparsedtype与NULLcreated_at提交前拒绝、0消息/outbox不增两case通过。产品source unchanged，130unit、其它七sealedELF、三persist对象/正确App对象同SHA，仅新main编译/relink，four new v3 schema先absent/DDL审计；旧首轮和v2已创建offp1各3表/旧badcopy和durable行都保留，其余planned names不伪称已创建。v45保存所有失败及v3源/构建/SQLstage。完整四控制未结束前SQL仍不能PASS，COMMITresponse故障/端到端10k50k未验收；所有原durability/index/19/config和其它应用保持。


### 2026-10-05：事务批处理真实回归完成与提交后恢复边界

`64bee34` 的四个全新隔离 MySQL 测试库已完成：OFF/ON × pool1/pool4 的领域回归各55项；两个pool1另有未读聚合各14项、驱动及身份验证各21项，合计290项实际SQL检查通过、0失败。加上130项已封存且二进制未变化的单元回归，共420项通过。生产表元数据、19个容器及私有配置未变；持久化参数保持1/1/1/0/0。此前所有预检、编译、断言和临时表构造失败保留，不覆盖结果或删除测试库。v45归档已下载，SHA256 `2d1ec1ed82b90267f26e19ad4bce67068a1b8d165b55e2d32da76adca0f23eff`，842个文件逐一核验。

下一隔离验证仅为原生测试ELF链接 `--wrap=mysql_commit`：真正提交成功之后验证本测试连接的TCP对端并shutdown自己的socket，然后一次返回客户端失败；检查稳定CID恢复、恰好一条真实领域outbox、相同MID重试与pool1连接归还。使用原v3两个自有pool1库，各最多增加一对新消息/事件，新的0600私有日志/config独立保存，旧阶段证据不追加。测试标签明确为“提交后客户端失败”，不将已收到成功响应的包装器模拟声称为实际网络COMMIT响应丢包。没有生产故障钩子、TLS改动、全局断连或SQL删除。

目前该提交后故障检查尚未执行，候选MessageService尚未部署；420项正确性及小型SQL组件成本收益均不能证明端到端P99改善或全部功能50k达标。通过之后先构建明确来源的候选镜像，保持已验证Gateway批处理和相同负载进行端到端对照，再决定保留或回退，避免以减少SQL次数替代性能证据。


### 2026-10-05：提交后恢复8项通过，准备明确来源的完整MessageService镜像

`413c74d` 原生隔离故障回归实际完成：批处理OFF/ON各4项通过、0失败，各观察到且仅观察到一次真实提交成功后的本测试TCP shutdown及客户端失败返回。稳定CID恢复为Reused，重试同MID，真实领域outbox恰好1条；两个自有v3 pool1库各仅新增1消息/1事件（各最终13消息17事件）。生产DDL、持久化1/1/1/0/0、借用构建文件哈希、19容器和配置完全一致。回归ELF SHA `e5ea481995fdb34710138e7bd989947e14bb7b10480bce68cf2be3f0d24a852f`。这8项验证提交后客户端故障与自身断连的恢复，不证明真实COMMIT响应在网络中丢失。

当前性能候选尚未部署。独立完整服务构建将前置已验证ed90驱动/仓库/适配器与b32应用对象，另用实际tinyimx_db/tinyimx_message_grpc/message_service_demo参数编译当前连接池、ServiceImpl、与原DDC相同的demo主程序；全程不重新配置CMake、不覆盖旧缓存ELF或库。ServiceImpl只有默认关闭的存储计时，连接池只有默认关闭的获取计时，相关头文件和主程序与DDC相同；原同步RPC、先读后确认以及健康PING保持。完整链接检查来源并确认故障包装器完全未链接，再由固定b24基础镜像生成新的唯一候选标签，UID1000/no-net/readonly/no-cap loader及missingconfig控制，保留停止探针和所有产物。该步骤仅构建镜像，端到端对照仍NOT_RUN，不能据428项正确性宣称极致性能。


### 2026-10-05：完整事务批处理候选镜像通过，准备同二进制端到端对照

`d37dcfe` 完整自有MessageService编译/链接与唯一镜像封存完成，镜像 `sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060`、ELF `2548733766409d282d30f6ffbbbe47f9c12b5d4fa3cacfdcc3d3e47f72582699`。实际前置ed90驱动/仓库/适配器和b32应用，加当前独立编译连接池/ServiceImpl/demo；linkmap确认七对象来源，批处理符号存在，mysql_commit故障包装器完全缺失。UID1000只读/no-net/no-cap ldd-r与预期missingconfig exit1通过；旧cache ELF/库/对象、19容器与配置保持，仍未部署。

接下来仅在已有业务会话与自有压测worker均不活跃时重建MessageService；详细审计保留原inspect/log/私有proposed Compose。同一镜像OFF0→ON1→OFF0，对照只改变一个flag，原RPC/超时、pool16、trace1、健康PING及SQL持久化1/1/1/0/0保持，已验证Gatewaya8 batch1和其余18服务身份固定。异常自动恢复原b24与flag缺省，日志/所有rows/文件/候选/探针保留，不prune/reset。原版编译与新二进制有共同计时及正确性修复差异，因此主ABBA必须用同ELF开关，避免将重编译副作用归为批处理收益。

OFF/ON先各运行好友→私聊/重复/已读→群权限/扇出/重复→文件授权/取消/字节校验的真实功能链，参与者从原自有synthetic范围只读选择offline且双向关系/请求为空的非相邻pair，原actor再次检查身份再执行。压力用相同worker `5d6bd183`、10k在线150/s60s9000消息：sqlbatch150A1/B1/B2/A2，保留完整attempt/positive/wire/SQL/heartbeat及两个P99门槛、相同窗口digest/cgroup/guestPSI。每次结果先分析；完整性失败立即回退，稳定性能差异才决定保留，不能以SQL命令减少或428项正确性替代尾延迟证据。所有测试在本提交时NOT_RUN；全功能50k极致未达成，继续准确诊断。


### 2026-10-05：同二进制完整 ABBA 的收益与未达标边界

`e1f6243` 同一完整 Message ELF `2548733766409d282d30f6ffbbbe47f9c12b5d4fa3cacfdcc3d3e47f72582699`，只切换 `TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE`，Gatewaya8/batch1/workers16及其余18容器、配置、pool16、健康PING、持久化1/1/1/0/0保持。四轮10k在线、150条/s、60s的原始结果：

|轮次|开关|ACK P99 ms|计划发送至ACK P99 ms|100ms内ACK|真实持久化/线上投递/接收ACK|
|---|---|---:|---:|---:|---:|
|sqlbatch150A1|OFF|156.7|158.0|8345|9000/9000/9000|
|sqlbatch150B1|ON|135.0|135.9|8638|9000/9000/9000|
|sqlbatch150B2|ON|132.3|134.0|8616|9000/9000/9000|
|sqlbatch150A2|OFF|164.5|168.6|8242|9000/9000/9000|

每轮attempt/positive/queued receiverACK/SQL核对9000，negative/skipped/late/disconnect全部0，10k登录和实际heartbeat drain等式通过。四轮private_exit均2：两个延迟门槛全部FAIL。两个ON均优于两个OFF；邻接对照A1→B1下降13.85%、A2→B2下降19.57%，这是共享主机本负载的可重复部分收益，不是全速率/全部功能固定收益，也不平均P99。ON作为后续已验证起点保留，不宣称100ms或50k极致已达成。

OFF/ON真实功能链各49操作、35断言及4heartbeat通过，包括好友接受/拒绝、私聊重复/历史/已读、群权限/静音拒绝/扇出/重复/成员及角色变化、文件授权/取消/实际二进制断点续传与下载checksum。闲时单次延迟不能当每功能P99；原生及单元428项正确性通过，仍不代表完整混合压力/TLS/离线/故障/soak/MCP/AI认证。v48完整原始1185文件已本机逐SHA核验，归档SHA `4e6faf32ee47194fedcdcc873a34de5423ea1bc6e156c044650111cb9e2841a6`；v46/v47没有执行，不补造结果。

窗口实际Message CPU0.910–0.937核、MySQL1.456–1.493核、两GW合计1.292–1.322核，19容器总5.172–5.234核；guest busy83.21–84.26%、system29.88–30.94%，CPU PSI some59.37–60.20%，context switches约3.70–3.74万/s、tasks约305–308/s。memory PSI some最高0.018%、IO some2.13–2.41%，目前无内存瓶颈证据，不清理用户应用或缓存。CPU等待强但不能把这些总体指标归为某个C++函数、主机安全功能或唯一根因。

SQL digest COMMIT平均wall6.25–6.92ms，接收确认UPDATE7.76–8.39ms，包含并发等待并非CPU或总体P99。ON的byMID完整记录SELECT约13150另加LAST_INSERT_ID完整记录SELECT8724，合计21874，OFF约21977：身份验证没有删除，不能只比较byMID单个digest误称查询减半。批处理仅减少BEGIN/INSERT/READ通信7→5，所有完整记录校验仍在outbox与COMMIT之前。权限源码实际一个Social RPC/一个双向关系SELECT；路由Redis+本地discovery snapshot，避免错误假设多UserRPC或每条Zookeeper请求。

观察快照与慢ACK重叠A1/B1/B2/A2仅49/42/6/16，慢ACK诊断计数654/362/383/758；剔除重叠后的纯诊断P99仍156.20/131.91/132.47/167.99ms。这不是官方指标替代或删样本许可，说明快照重叠不足以解释大部分尾延迟。新增fresh分析保留完整failed gates、9000消息配对、直算同MID阶段和偏置边界；稀疏slow样本不代表全体。后续先获取真实CPU函数与同消息等待证据再改代码，不重复盲加worker、弱化持久化/PBKDF2/健康PING或以组件PASS结束任务。分析及保留helper在本source提交时NOT_RUN，运行后独立保存结果。


### 2026-10-05：批处理收益保留，准备最小权限 CPU 采样工具自检

`8fecf51` 分析实际完成，四轮9000 send/ACK/delivery逐条配对。ACK总体mean54.197/49.315/50.013/54.403ms，ledger仅诊断的wireP99176.484/163.750/154.965/204.240ms；ACK官方P99仍156.7/135.0/132.3/164.5，均FAIL。新同MID交集A1/B1/B2/A2只有10/8/14/9条，都是threshold/rate8偏置慢样本。ON匹配样本commit均值70.115/65.524ms，仓储75.382/77.975ms；OFF样本dispatch22.446/23.883ms。此为准确同消息位置线索，不能用人群差异声称整体commit退化、组成P99或唯一根因。全部配对数值、窗口CPU/SQL/observer、完整四轮failedgates保存fresh分析阶段。

`private-batch-message-deployment-retained-on-20261005` 已实际保留同be8/ELF2548的batch1，Message CID `0263dbd9cf204a4f02f10ff02667cbf6100013d4162823184f28d96a4d8ad737`，其他18/config及durable1/1/1/0/0保持。v49实际导出1207文件，归档SHA `dc5e9831ae9c4418d616f6dd740e1ff814f219c0e067cd64f2f8a1654fd3c1f6`；本source提交时本机下载核验待执行，不提前称完成。

真实CPU函数剖析尚缺失。只读系统核对实际kernel6.8.0-138、匹配perf6.8.12、Docker29.8.1，perf_event_paranoid4/kptr1保持。官方 https://docs.kernel.org/admin-guide/perf-security.html 推荐CAP_PERFMON；Docker官方default https://raw.githubusercontent.com/moby/profiles/main/seccomp/default.json 有CAP_PERFMON条件perf_event_open许可。不要从仅CPU高推测全部开销属于gRPC/MySQL/VM主机安全设置。当前新工具只做隔离自检：三个新自有容器UID0无CAP、UID0仅PERFMON、UID1000仅PERFMON，同nativeperf+逐个依赖ELF只读mount，自有futex1线程1秒、cpu-clock:u49Hz平面IP采样；不读取产品PID、宿主PID空间或用户应用。默认seccomp/dropALL/只读/no-net/nnp/128MiB/CPU1/pids64，无SYS_ADMIN/SYS_PTRACE/privileged/unconfined/setcap/sysctl/全盘mount。

record输出binary pipe由guest进程直接保存fresh0700私有目录/0600文件，Dockerlogdrivernone，不采集堆栈、寄存器、内存或内核；报告从stdin只读取自有raw，零CAP/平面符号聚合。既有系统perf/libSHA保留且不下载或安装，所有probe停止后保留。20秒上限只按exact自有CID/name/image/argv停止。真实UID能力以实测为准，若失败保留错误先分析，不擅自扩大权限。自检在本source提交时NOT_RUN；成功仍需后续单独审计实际服务附加与诊断扰动，不能据此宣称生产瓶颈或性能达标。继续优化目标仍未达成。


### 2026-10-05：最小采样能力实测通过，补证同 UID 启动

`b6f3cea` 工具自检实际PASS：UID0无权限对照exit255/0raw，UID0只PERFMON exit0/11948bytes、报告成功；UID1000普通OCI方式只PERFMON exit255/0raw。该方式没有得到可用采样能力，不能假设nonroot cap自动继承。全部三个实际结果及错误摘要哈希保留、19运行身份/配置/sysctl4/1保持。v49本机1207文件逐SHA通过，v50实际导出1227文件SHA `d151c75541b8a5bedb2adbd3e06540996801e32701a7ae536ce7bad7186d5d1a`，本机v50核验尚待执行。

后续专用自有launcher在隔离容器启动瞬间只给PERFMON/SETUID/SETGID/SETPCAP；先清ambient、将所有bounding cap除PERFMON外全部删除，再empty supplementarygroups、切固定UID/GID1000，effective/permitted/inheritable仅PERFMON，ambient仅PERFMON，清keepcaps。严格核验五个cap集合均 `0000004000000000`、resuid/resgid全1000、supplementary0、nnp1/seccomp2，再exec固定nativeperf record。所有启动能力从最终五个集合消失且不可恢复，失败任何一步均在采样前abort。没有SYS_PTRACE/SYS_ADMIN/globalsetcap/sysctl/unconfined或hostPID共享，容器readonly/no-net/defaultseccomp/128MiB/CPU1/pids64保留。这个自检仍仅一线程一秒工具自身，未附加产品服务。

启动器用单独ownstage ELF编译，180s自有compiler PID/start/argv/PGID界限，旧cache/products均不覆盖。采样49Hzuser-onlyflatIP不采栈/regs/memory/kernel，binarypipe私有保存，报告zeroCAP/stdin。所有stoppedprobe和失败raw保留，额外caps只在launcher初始化、没有产品权限变化。新bootstrap本source提交时NOT_RUN；通过后实际targetmaps/符号/采样窗口仍需单独准确审计。全功能极致仍未实现，持续推进真实瓶颈剖析。


### 2026-10-05：启动器被文件权限拒绝，独立修复测试工具

`35ec1da` 自检在OCI exec前失败exit126/Statecreated/Pid0，原诊断ELF由umask077生成mode0700、guest1000所有，隔离UID0仅四个规定caps没有DACoverride，因此对该ELF无执行权限。未执行launcher，也未附加服务；不是内核采样策略失败。原compile/source/probe/private.stderr/failed.json保持，19服务/配置/sysctl不变。所有先前自检正确结果也保持。

新attempt2只在全新0700stage编译同源码，然后审计新ELF绝对路径、SHA、无symlink、原mode及新mode0555；仅给该刚创建自有诊断ELF只读可执行权限，没有任意chmod其他文件或新增DAC能力。旧stage/probe/ELF0700保留，不覆盖。其余UID1000/finalPERFMON五集合/defaultseccomp/no-net/readonly/nnp/128MiB/CPU1/pids64/49Hz1秒自测与独立cap零报告全部保持。新probe名明确attempt2，OCI未执行时记录实际State/Error而非空断言。新自检在本提交时NOT_RUN；先修工具再剖析，不把工具失败解释为产品瓶颈。

v50本机1227文件逐SHA已通过，归档SHA `d151c75541b8a5bedb2adbd3e06540996801e32701a7ae536ce7bad7186d5d1a`。全功能10k50k极致未达，继续。


### 2026-10-05：同 UID 采样启动器通过，准备真实业务 CPU 函数证据

`08bab3c` attempt2实际PASS：新ownELF0555、采样前uid/gid1000、effective/permitted/inheritable/bounding/ambient均PERFMON `0000004000000000`、nnp1/seccomp2，1秒自身record exit0/11804bytes及独立zeroCAP报告成功。第一失败0700/OCI126完整保留。没有产品或19运行身份/配置/sysctl4/1变化。

真实附加前只读检查发现容器PID1为docker-init，不能拿容器State.Pid/PID1当Message/Gateway业务进程。新增helper必须在每个目标namespace中仅匹配唯一准确业务exe，nsPID>1、UID/GID全1000、startticks/ELFSHA和maps/实际rootELF可读，再附加这个PID并保持目标CID/启动时间不变。三个新的专有诊断容器各只共享其准确目标container PID空间、无网络/hostPID/写mount，已通过启动器到sameUID/solePERFMON；49Hz userCPU平面IP45秒，无内核/栈/寄存器/内存。保留完整私有binarypipe，所有能力启动检查，PID/ELF/文件SHA边界。

用原worker/SHA/ramp/HB/deadline及同10k150/s60s9000的独立cpuprofile150，batchON保持。三个recorder在allready/control start+4s之后启动，45s完成，原rawrunner始末仍恰好19业务容器；所有符号报告只在压测worker全部drain之后运行，避免破坏runner身份守卫。报告UID1000/zeroCAP，通过 `/proc/<真实servicePID>/root` symfs读取实际当前产品ELF/DSO，防止nativeperf依赖只读mount或旧基础镜像给出错误符号。128MiB/CPU1/pids64/defaultseccomp/nnp/no-net/readonly/lognone和90秒exactownCID边界保持。工具本身CPU和采样扰动可能影响延迟，因此原失败/指标仍存，这一次专用于函数CPU定位，不当无采样验收或CPU等待来源证明。

本source提交时实际业务采样NOT_RUN，无产品CPP/ELF修改/部署，其他应用继续保留。全功能10k50k極致未达，取得函数证据再选择下一具体代码优化。


### 2026-10-05：真实负载采样部分成功，先恢复既有 Gateway 证据

`2a6495f` 的cpuprofile150实际完成10k登录、9000 attempt/positive/线上delivery/接收ACK及SQL持久化，negative/skipped/late/disconnect均0。记录仅为带采样诊断；100ms内ACK实时4090，扰动很大，不能冒充原未采样ABBA验收。两个Gateway采样器exit0，各raw约87KiB；Message采样器exit255/raw38KiB，stderr仅final五能力证明和19条Ignored openfailure线程警告，observer因此失败，整体helperFAILED，未生成任何CPU符号结论。所有原始pressure及partialraw/失败/容器保持，19身份和配置保持。

Linux6.8官方 https://raw.githubusercontent.com/torvalds/linux/v6.8/tools/perf/util/evsel.c 表明这种warning属于已经忽略的ESRCH（线程消失）； https://raw.githubusercontent.com/torvalds/linux/v6.8/tools/perf/builtin-record.c 对-p本来就打开ignore_missing_thread。因此警告是线程寿命变化证据，但不能直接认定它就是fatalexit原因或靠加ignoreflag解决。当前准确fatalcause仍OPEN，不能开no-inherit漏掉新工作线程来宣称完整MessageCPU，也不扩大安全权限。

现有四轮未采样cgroup中Messageuser0.427–0.434/system0.483–0.506核，两GW各user0.302–0.339/system0.313–0.344核；用户空间采样只能覆盖其中部分，不能解释全部kernelCPU/调度等待。源码Message/Social RPC已经缓存channel/stub，不能假设每请求CreateChannel并做无效改动。

新工具只在freshrecovery阶段解析两个实际成功Gatewayraw，UID1000/zeroCAP报告读取准确targetroot符号、所有parentraw/sourceSHA校验并保留。报告按全部符号行统计samplecount与unknown符号比例，先验证可用性再选择下一代码优化；没有新负载、重启、编译、权限或产品修改。Message失败保持明确false，不将部分报告称全部CPU剖析完成。新recovery在本source提交时NOT_RUN。全功能极致未达，继续准确分析。


### 2026-10-05：捕获符号映射错误，拒绝错误函数归因并修复

`ab11ee5` 从已有raw恢复GWa620/GWb625 userCPU样本，raw完整、无新负载。但报告出现纯私聊没有触发的FriendRequest与nss密码解析等函数，表明符号可信度不合格。根因已核实：实际 `readlink -f /proc/7/root` 为 `/`，Linux6.8官方 https://raw.githubusercontent.com/torvalds/linux/v6.8/tools/perf/util/symbol.c 中symbol__init realpath(symfs)，若等于 `/` 就重设symfs空串。魔法procroot路径被清空，报告静默使用profiler基础镜像的旧Gateway和只读注入的native旧glibc，地址偏移匹配错误。前一source关于procroot准确DSO的意图没有实际成立；所有旧报告保留，但其函数名和据此的业务归因明确不可用，没有据此修改产品或移除权限校验。两Gateway约42–44% `[vdso]+0x768` 只是未解析代码位置，不提前称某个计时函数。

新工具只读当前两个固定Gateway的exe-backed maps，在freshstage/bundle为每个目标逐文件复制实际ELF与sharedlibrary，准确路径白名单、size/SHA/buildID/c289主ELF和原inode身份检查，每复制前独立audit、destinationabsent且withinroot、copy后流式SHA/ELFmagic/所有者核验。仅public可执行文件，无整个root/config/env/应用内存或用户数据；每文件<=1GiB、总<=2GiB，guest>2GiB/disk>3GiB。不覆盖旧stage、产品或cache，bundle0700且ELF排除export。

报告改为真实物理只读目录 `/opt/codex-symbols`，不会canonicalize到 `/`；隔离PID、不再共享业务PID，UID1000/zeroCAP/defaultseccomp/nnp/no-net/readonly/lognone等原边界保持。明确 `overhead,sample,dso,symbol` 四字段与分号分隔，避免长模板符号导致巨大padding；重新解析必须保持全部620/625样本，未知vDSO比例诚实保留。无需重新压测或扩大权限，Messageexit255原因仍OPEN。新物理快照/报告在本source提交时NOT_RUN；先验证映射可靠后再选业务修改。全功能极致继续未达，准确迭代持续。


### 2026-10-05：物理符号快照补全实际运行库目录

`dfe80f6` 首次快照的保守白名单在任何文件复制或报告启动之前拒绝 `/opt/tinyimx/lib/libgcc_s.so.1`。实际只读exe-backed maps共有六文件：c289 Gateway主ELF、项目RPATH目录的libgcc/libstdc++、系统ld-linux/libc/libm；不是未知用户数据。独立失败审计已保存ExecutableWhitelistRejected/no ELF copies/19身份配置保持，旧stage/source/报告不覆盖。

新attempt2只增加准确项目public artifact目录 `/opt/tinyimx/lib/` 的共享库basename白名单，其余文件大小/SHA/buildID/ELFmagic/所有者/绝对freshdest、guestRAM/disk、bundle0700、physicalsymfs、zeroCAP隔离报告与完整620/625样本检查保持。新stage/probe名字独立，加入失败marker确保所有后续错误保存；没有权限扩张、产品修改或新压力。旧错误符号报告依旧不可用于归因。新attempt2本source提交时NOT_RUN，继续先确保正确证据再优化。


### 2026-10-05：物理符号报告成功，离线解析保留全部样本

`9aa93e7` 实际复制两个Gateway各六个public ELF/DSO，12文件全部size/ELFmagic/owner/targetSHA/copySHA/buildID验证，真正c289与运行RPATH库一致；两个隔离UID1000/zeroCAP报告exit0、各426/430KiB生成。parentraw及19身份配置保持。阶段最终FAIL仅为nativeperf输出格式：显式四列外依然自动追加第5列IPC占位 `- -`，严格4列解析因此拒绝，没有丢弃不认识的数据或制造CPU结论。

新工具只读取这些已经成功的报告与哈希清单，明确严格接受5列且IPC只能两个 `-`（不解释为硬件指标），类型解析前四列，并核对headerSamples620/625、Lost0、187/167符号行、总样本exact以及总权重舍入。显式按CPU权重排序后输出热点，避免字段排序影响top；长符号只移除显示padding，完整值保留到freshanalysis/all-symbol-rows，不截断原始证据。没有重新复制、报告容器、采样或压力，所有输入包括12ELF在前后继续SHA保护。新离线解析本source提交时NOT_RUN；取得可信实际函数后才进行下一精准修改。vDSO未解析offset保留未知，Messagecapture仍不完整，所有fullfeature目标未达。


### 2026-10-05：Gateway可信热点与独立时钟成本诊断

物理ELF解析 `private-cpu-symbol-analysis-20261005` 已PASS：GatewayA/B样本620/625、Lost0，vDSO偏移0x768占44.35%/42.08%用户CPU，其余主要send/syscall/recv与同步；CycleClock::Now显式函数占2.26%/0.8%。该分析取代旧procroot错误函数名，用户态平面采样不能代表内核CPU、等待时间或请求P99；带采样压力1306.6ms不能作为无采样验收。Message采样失败255原因仍OPEN。

下一诊断只新建own C程序与隔离UID1000零CAP/无网络/只读/defaultseccomp容器，四轮各模式20k次：实际运行镜像libc的MONOTONIC、REALTIME、直接syscall MONOTONIC以及COARSE对照；输出批均wall/user/system成本，不伪装成单调用P99。仅复制诊断自身AT_SYSINFO_EHDR与自身maps严格对应的公开kernel vDSO ELF，验证1–16页/64KiB/ELF与段节边界后流式保存private目录，readelf/objdump核对0x768指令。没有读取目标进程内存/用户数据，没有时钟源/宿主安全/业务期限修改。COARSE不用于产品计时或压测门槛。本source提交时probe NOT_RUN，需实际代码与测量结果再决定优化；所有功能10k–50k目标仍未达。


诊断source首次应用在所有路径写入之前，被远端严格AST检查拒绝：v58导出目录列表生成缺少一个引号。没有产品代码/源文件/编译/容器变化；空的首source阶段保留并写明确失败审计，不覆盖。修复列表、对两个helper都执行AST检查后，fresh attempt2 source stage再提交。此为诊断打包缺陷，不是业务性能证据。


### 2026-10-05：精确时钟异常成本已实测，继续区分指令原因

`67b43c6` 的隔离诊断实际PASS，容器50c6f7（UID1000，E/P/I/B/A零，nnp1/seccomp2）、320k调用全成功，19服务/config/内核安全设置保持。MONOTONIC四轮批均wall为8253.29/4201.80/3789.06/3743.26ns，REALTIME4925.45/7956.17/6090.02/4452.72ns，直接syscall14819.69/10131.38/8330.24/9832.50ns，COARSE4.57/4.93/4.38/4.53ns。不是单调用P99或带载表现。公开self vDSO8192字节SHA29465e48a8210bdd3f6d54223c3d5349d01503bedf5a3c10bf4a4a02a9264302，0x765为RDTSCP，热点0x768紧接其后的NOP；平面IP采样可能有skid，不能仅凭NOP断言其自身消耗或分配全部请求延迟。TSC路径公开Linux源码使用rdtsc_ordered；https://raw.githubusercontent.com/torvalds/linux/v6.8/arch/x86/include/asm/vdso/gettimeofday.h ，候选序列RDTSC/LFENCE+RDTSC/RDTSCP由特征决定：https://raw.githubusercontent.com/torvalds/linux/v6.8/arch/x86/include/asm/msr.h 。当前WHP/ULM存在，但无成对宿主模式测试，尚不能宣布唯一原因。

按150消息/s，ChatRequestPhaseTrace每条约10次精确时钟，即便4us也只约0.006CPU核；排除仅关闭该诊断就解决42%用户CPU的未经证实推断。RPC期限/入队/活跃/观测也有精确时钟，当前采样无调用链，调用频率归属仍OPEN。后续own诊断CPUID检查TSC/SSE2/RDTSCP后比较三条用户态指令与四API模式，4轮各20k调用，共560k；原public vDSO SHA也核对相同。原始TSC只是成本对照，不替代安全的monotonic期限或性能量测。fresh source/工具/编译/容器受审计，新probe本source提交时NOT_RUN，无产品代码/VM启动/安全/应用变化。v57主机下载及1388manifest哈希通过，v58实际导出1408文件、SHAe6534ef70ff5d329b88157c9640c8f44da4a03c516afc7a3d8985bd610f2a71f。所有功能10k–50k目标继续OPEN。


### 2026-10-05：计时指令对照否定单独禁用RDTSCP方向

`c26fae5` 的指令诊断实际PASS：560k调用全成功，CPUID前置验证，隔离容器f7c763/ELFf2224c79，public vDSO SHA与前probe完全一致。四轮RDTSC批均3395.97–3446.93ns，LFENCE_RDTSC3403.27–3503.22ns，RDTSCP3417.34–3630.61ns；MONOTONIC3493.44–3541.31ns，REALTIME3507.12–3540.98ns，系统调用7225.20–7315.26ns，COARSE4.53–6.15ns。三条指令同样昂贵，不能推荐只禁用RDTSCP/重启/改kernel达到大改善，未做任何此类修改。代码调用频率归属仍缺失，不能用单次成本换算整个请求延迟。

下一诊断只给旧own合成RPC程序加载fresh counterSO，真实clock_gettime和值/result/errno均原样返回；统计4096固定槽×7时钟桶与立即返回代码地址。只保存自身公开代码地址/对应exact ELF函数，不读取任何目标应用内存、完整栈、用户内容或regs。LD_PRELOAD仅在own子进程最小环境生效，产品服务不加载。比较direct计数与grpc正常A1/计数B1/B2/正常A2，每3000条20ms合成echo、16调用worker；overflow/初始化fallback必须0，原binary/DSO SHA前后保护、elf偏移精确符号校验。包括启动/关闭、调用方+服务方，不是实际Gateway调用频率或10k压测。需要该证据后才选择高频调用优化。本source提交时NOT_RUN；allfeatures10k–50k仍OPEN，其他应用继续保留。


### 2026-10-05：RPC时钟调用量已量化，检查本地数据库传输成本

`4a9494b` 的own clockcounter五组全部PASS，共15000合成echo，overflow/fallback均0，original ELF/DSO SHA保持。直接3000条共21002时钟调用，grpcB1/B2共183880/183833（约61.29/61.28次/echo），主要now_impl121543/121566、cppsteady53207/53139；direct cppsteady15002。该全进程计数包括启动/关闭以及client+server，不等于Gateway/Message实测调用量。正常A1/计数B1/B2/正常A2过程CPU0.4411/0.4368/0.4256/0.5044核，简单调用P99 22.606/22.377/22.677/23.511ms；没有凭计数工具较低P99宣布性能收益。RPC会放大精确时钟频率，但无法独自解释真实135ms长尾；不能只关私聊trace、用coarse改期限或重启禁用RDTSCP。

原真实ABBA Message内核0.48–0.51核/MySQL0.575–0.588核、guestCPU PSI约60%，内存PSI很低，下一检查本地MySQL TCP内核路径。只读preflight确定MySQL cc86c/d58a/init2425、backend network；public socket /var/run/mysqld/mysqld.sock mode777/uid999/gid999/inode3932320，durability1/1/1/0/0。新ownCPP仅通过test ELF --wrap_mysql_real_connect强制TCP或socket，getsockname验物理AF、TLS协商数明确保存；原健康PING/SELECT1/utf8/认证选项保持。四fresh隔离容器在同be8镜像、相同CPU1/256MiB/16连接，ABBA各3000次，唯一public socket inode只读bind、privateconfig只读、onlyfresh ownoutput可写；没有生产配置/SQL表/服务重启。该inode诊断bind在MySQL重启后会失效，不能直接当稳健部署方案。先测成本，后续若有效再实现可选产品transport并完整故障/事务/跨功能验证。本source提交时NOT_RUN；所有10k–50k功能极致验收仍OPEN。


### 2026-10-05：Unix对照首次编译失败与实际SDK兼容修复

`6e40e0b` 的firststage仅compile FAIL：Oracle nativeclient不存在mysql_get_socket，原privatecompilelog SHA78fd5d4dd56fe785e9da6508967f7f0a7b67ceb4764ed06a674415e372bad946、failuremarker/原runner保留；尚无新ELF或诊断容器，没有SQL对照结果，19/config不变。修正own diagnostic读取当前SDK MYSQL::net.fd（已有真正PASS的mysql_owned_postcommit_fault使用相同字段），只getsockname而不关闭/故障socket。额外用官方public mysql_get_host_info描述交叉检查TCP/IP或UNIX socket：https://dev.mysql.com/doc/c-api/8.0/en/mysql-get-host-info.html ，不导出原字符串。保持真实AF proof，不改用只检查配置值或替换客户端库。fresh attempt2编译/stage/CID输出均独立，原失败不覆盖；新工具本source提交时NOT_RUN。产品CPP/配置/MySQL持久化/安全/应用与所有目标OPEN保持。


### 2026-10-05：Unix首次启动失败，canonical公开socket挂载预检

`2e31603` own diagnosticCPP实际compile/link成功，ELF/object/555审计均保存。首TCP容器8f16e057/statecreated/PID0/exit128，runc绑定 `/proc/2425/root/var/run/mysqld/mysqld.sock` EINVAL，程序从未执行；stdout空，stderrSHAc0daedc58acca05d083c71c77bcb55e8d7562019a2d84582538d95c0799859a8，无SQL结果。该FAIL是mount路径问题，不是TCP/Unix/MySQL性能或认证测量失败。Docker只读inspect为overlayfs/GraphDriver null，仅mysql-data挂载；没有猜upperdir/rootfs或暴露整个数据库卷。MySQL容器内部readlink-f socket为 `/run/mysqld/mysqld.sock`，因此去除 `/var/run` 绝对symlink后的canonical来源 `/proc/2425/root/run/mysqld/mysqld.sock`。

fresh attempt3先做zeroCAP/no-net/readonly独立stat容器，唯一bind公开canonical socket，必须type/mode/owner/inode与原MySQLstat完全相同才能进入SQL对照。复用attempt2实际成功ELF/object，CPP/flags/allcachedlibs/sourceSHA一致，既不再编译，也不更改555权限；原failed容器、ELF和日志保留。四freshTCP/socket容器与原ABBA准则保持，45sec自身CID超时守卫，全部产品/配置/durability/apps不变。canonical挂载预检与新SQL本source提交时NOT_RUN；仅inodebind诊断方案不声称生产重启韧性或目标验收。


### 2026-10-05：canonical socket仍失败；按实际mount元数据定位来源

`cd2715f` attempt3的no-net/zeroCAP/stat容器6bdffcfb在OCI阶段仍EINVAL，statecreated/PID0/exit128，未执行stat、更无SQL。已推翻“只去掉/var/run symlink即可”的假设，前一版审计原样保留。两次source均通过proc/PID/root进入MySQL的mount namespace。实际只读mountinfo确认MySQL namespace4026533105、SSH4026531841；Linux6.8 `fs/namespace.c::__do_loopback`对不属于caller namespace的来源挂载返回EINVAL，与现象吻合（机制解释，不伪称已内核trace定位）。初次readlink /proc/1/ns/mnt受不同UID限制，改为/proc/self成功；只读失败保留。

来源依据：https://raw.githubusercontent.com/torvalds/linux/v6.8/fs/namespace.c 。MySQL实际根overlay挂载元数据给出upperdir `/var/lib/containerd/io.containerd.snapshotter.v1.overlayfs/snapshots/458/fs`；fresh attempt4仅绑定该实际upperdir下 `/run/mysqld/mysqld.sock` 单一公开socket，不猜GraphDriver路径，不映射数据库卷或整个root/upperdir。先独立stat要求socket777/999/999/inode3932320与原MySQL完全一致，再允许只读4×3000PING+SELECT1。复用attempt2已编译ELF与object，SHA/555/源码/库不变。首次启动失败容器与所有报告保留。新socket验证和SQL对照本提交时NOT_RUN，全部正式服务/配置/宿主应用不变。


### 2026-10-05：公开socket stat通过但native连接失败；区分传输与RPC框架

`d3f229e` attempt4实际upperdir单socket stat预检通过，首TCP16条全TLS连接、3000PING/SELECT1正确/P992.932063ms。Unix容器62a34a29执行后exit4，setup连接失败，无result，stderr/stdout均空；没有Unix查询或ABBA收益。inode/type/owner stat相同不足以确认可连接，不能部署该路径。fresh nativeC仅socket/connect/SO_ERROR/close自有fd并记录OS errno与dev/ino，不读配置、不认证、不发送SQL。Linux6.8 af_unix.c查找依赖d_backing_inode对象而非仅stat数字，作为候选解释，必须等实际errno再分类，不能臆断SSL/account故障。

下一诊断重心是已有61次preciseclock/echo与~0.43core300/s的RPC组件开销。fresh ownCPP沿用实际gRPC1.76/当前缓存proto与syncCQ1MIN1MAX2，native16client，0ms和20ms handler两场景，分别TCP/UnixABBA4×3000全字段synthetic结果。只在自己的ELF链接bind/connect包装器核对物理family与127loopback/固定私有socket路径，保持realcall/errno。所有新容器零CAP/UID1000/nnp/seccomp2/readonly/no-net，仅自己的caseoutput RW；Unix socket只能在fresh且预检不存在的case目录由框架创建/清理。全部正式19容器/配置/SQL durability/宿主应用保持，不把synthetic组件结果当作全功能/10k50k验收。

另一个源代码审查发现是LOG宏先构造ostringstream再进入Logger过滤，禁用日志也计算message表达式；潜在浪费尚未量化，未以此替代真实CPU证据，也未修改产品。应用器白名单第一次克隆漏了underscore helper名，在任何源文件或审计目录写入前被严格白名单拒绝；原applier/包保留，fresh v2由manifest精确白名单通过，已保存local审计。


### 2026-10-05：纠正诊断偏移，按等级延迟构造日志候选

30452a5 的隔离传输诊断实际完成：公开 upperdir Unix socket connect=-1/errno111(ECONNREFUSED)，没有认证或 SQL；overlay 原 socket dev87 与 bind dev2053、inode同3932320，仅stat相同不等于同一可连接对象，禁止部署此路径。RPC8组共24000合成结果全正确，CPU1/no-net own容器里0ms TCP P99 6.328/4.674ms、Unix23.090/30.035；20ms TCP45.902/46.082、Unix65.475/72.784，未发现收益。此实验存在单核配额，不能将它解释为真实部署瓶颈或Unix普遍较差。正式Message/GatewayA/B/MySQL实际cpu.max=max 100000且nr_periods/nr_throttled/throttled_usec均0，没有生产CFS配额限流。v65已保存并校验1577文件，tar SHA f35b78d06d6b2fe4f09b53329403fa5fb2212186d0b5023e5bc37c644916284b。近几轮集中修复诊断工具，没有新的真实产品性能突破；必须停止用组件测试替代业务收益。

源码审计发现LOG宏总先构造ostringstream/计算操作数，Logger随后才过滤。752条源码调用词法清单和唯一函数调用清单只读保存；15条可疑名称实际是纯status转字符串。另发现examples/thread_pool_demo的ok_future.get()确有消费/等待副作用，移出LOG保留原语义。其余操作数为状态/地址/错误/getter/集合大小/只读Stats与诊断时钟，未发现必要业务修改。词法工具不是任意C++纯函数证明，后续新增日志仍需把业务操作放在外部。

本候选修改Logger.h的LogLazy模板与LOG宏，在等级过滤时跳过factory并恰好累计一次filtered；放行后仍调用原Log二次过滤，以处理格式化期间等级改变，源位置在factory外构造，保留输出/Fatal刷新/直接Log API/失败计数。等级、期限、事务、密码验证、数据和正式配置不变。原生回归检查输出/源函数、全部等级、factory异常、等级在factory中改变、未初始化、8线程8000计数、future消费、单次level求值；同ELF复现旧eager与新lazy，0/512B格式8组ABBA各100000条，只比较均值CPU与wall。该source提交时NOT_RUN。原生通过之后仍需真实产品重建、功能回归和同负载10k150端到端对照；不得提前宣布135ms长尾突破，所有功能10k–50k目标仍OPEN。


### 2026-10-05：日志延迟构造原生通过，准备实际服务同源码对照

f51b4f6已提交产品修正；lazy-logging-regression-20261005实际PASS，ELF67b89ed433352d6bca91c9221117ee146f672357c5d6975c78c492b7694e3ba6、CIDf2542200保留。23语义检查、8组ABBA各100000丢弃日志全通过，总31checks。0B eagerCPU29.986/26.923ms、lazy0.850/0.863；512B eager34.398/25.462、lazy0.535/0.875ms。原函数/内容、直接API、Fatal即时flush、等级表达式一次、factory异常、factory中提高等级后的二次过滤、未初始化失败、8线程8000 filtered精确计数、future.get外置全部PASS。正式19/config保持，没有产品部署或业务P99收益证据。单条省约0.26–0.34微秒，实际请求总收益必须结合过滤频率与真实CPU/P99，禁止将几十倍宏加速声称为项目几十倍加速。

现场公开配置显示Gateway/Message日志warn，Info/Debug/Trace确会过滤。实际服务链接缓存含过去已撤销候选，不能只重编译main或用旧archive宣布新优化已生效。本构建准备相同当前源码的eager-control与lazy两个变体：按现有.o.d审计全部日志头依赖，独立重编译每个受影响TU，并显式重编译当前原始MessageServiceServer/MessageApplicationService和两个main；每个受影响archive在自己的fresh目录复制后用唯一原member名替换，不覆盖原SDK/cache/ELF。control通过首位-I私有头目录载入精确30452a5旧LogMacros.h，其他源码/Logger ABI/flags/外部libs/基础image相同，实际-MD依赖核对header来源。Gateway以原a8b7基础image、Message以原be8为基础，各封装两个唯一tag、singlebinary COPY；同组五个原业务unit重编译/重链接运行并要求结果一致，每variant>=130checks。UID1000零CAP/no-net readonly loader/缺配置退出1各验证。编译单进程和内存/磁盘守卫，不停其他应用、不部署/改配置/改数据。source提交时新paired build NOT_RUN；之后必须真实跨功能与10k150匹配对照，没有达到所有10k–50k功能极致验收。


### 2026-10-05：成对构建预检发现测试多对象，逐对象校准后重试

8efa570 first helper在line36链接清单预检失败，尚未创建build stage、编译或镜像。private_receiver_ack_tests实际有test与gateway/ReceiverDeliveryTracker两条.cpp.o，不能沿用单main假设。只读确认全部七目标，其余六单对象、该测试双对象，路径都真实存在；原helper/Git/失败输出保持。fresh attempt2对每一个显式对象从actual link提取源路径、当前SHA/flags独立编译，再逐项替换，保证链接中无旧CMakeFiles显式对象。原cache/archive/member/SHA/资源/自有进程watchdog/同源码两变体/130业务checks/基础镜像/四tag/八停止probe/all19/config守卫保持。本source提交时attempt2 NOT_RUN；无部署/配置/数据/其他应用改动，不将工具修复作性能收益。


### 2026-10-05：44个对照对象和130业务检查通过，修正本地基础镜像引用

3f5b971 attempt2完整重编译44个eager对象、七个ELF并通过130业务unit（trace17/application53/crash28/receiver10/boundary22），随后封装Gateway FAIL。build日志SHA efbc0f6ec2b53e2d7ee404e2824969abd05bf691233ef441efa7219daa139748明确显示FROM sha256:a8b7被BuildKit解析为docker.io/library/sha256仓库，未创建候选镜像。该失败和旧Dockerfile/ELF/archive/log都保留。只读再次核对源和全部借用输入SHA保持。

fresh attempt3引用真实已存在本地tag（Gateway codex-online-maintenance-gateway-v1、Message codex-private-begin-insert-read-batch-v1），build前后均核对a8b7/be8物理ID，不拉取或更换基础镜像。为避免重复已成功编译，先验证attempt2源/flags/archive SHA和44项编译计划与当前完全一致，冻结其自有eager产物SHA再复用，重跑130业务unit。冻结发生在原失败后/复用前，不伪称是原编译前已记录SHA。新stage保存复用审计及fresh image context/Dockerfile，完全不覆盖attempt2目录；eager镜像标记真实build revision3f5，lazy标记本轮head。lazy44对象/七ELF独立编译、130同样unit、四镜像及UID1000加载/缺配置退出守卫继续。source提交时attempt3 NOT_RUN，正式19/config/SQL/apps均保持；仍需真实跨功能与匹配10k150才能评估本次业务收益，所有功能10k–50k极致目标未达标。


### 2026-10-05：四个日志对照服务镜像通过，进入真实链路验证

db6c0ea attempt3 PASS，每variant44TU/130unit，四image/八UID1000loader与受控缺配置probe通过，复用eager所有产物SHA与借用/source输入保持，正式19/config保持。eager Gateway image4a28a73e/ELFf655f900、Message image83db801b/ELF5997e07d；lazy Gateway image0d9d904b/ELF95bbc5af、Message image419aef5e/ELF7b9edcae。完整SHA在summary/completed-images与所有审计记录，eager build3f5b971/lazy db6c0ea，产品除日志宏及示例future外的源码保持相同。诊断输出源码已每秒最多8条且经完整LOG_WARN构造，不支持逐字段stderr导致长尾的猜测；不关闭trace混淆比较。

本轮七文件预审计后准备实际链路工具。只有空闲两GWs+Message切换，所有原env值（含SQL batch1/trace1、maintenance1/worker16）、Cmd/User/WorkingDir、挂载/资源/安全/网络/日志设置逐项相同，其余16 IDs/images/start和全configSHA、durability1/1/1/0/0保持。原始inspect/env/log/Compose config与override均0700private，不导出。原像a8b7/be8/原env回滚保留并在切换验证失败时恢复；只受控recreate3服务，不清理文件/容器/数据。先两组相同正常跨功能actor（独立只读确认的519800–519950非相邻offline空关系对）验证friend/private/unread/group/file链。再10k/150s/60s/9000计划ABBA，login100/s、原offer/timeout/heartbeat/wire/SQL条件完全不变，所有FAIL保存。观察同样58s cgroup/guest/SQLdigest窗口并额外两次公开task-ID集合，无栈/内存/参数/env读取，只帮助区分稳定worker/替换，不当作精确线程创建计数。所有用户应用保留；不会在压力窗口编译源码。本source提交时部署/actor/新pressure NOT_RUN，不提前声称项目P99下降；所有功能10k–50k极致目标仍未完成。

本地生成器首次精确缩进预检FAIL（预计3空格、实际2空格），在endpoint/functional/export/docs写入前被拦截；原generator及audit保留，fresh v2按原源修正并AST通过，尚无guest/runtime影响。公开taskID诊断不读栈/内存/env；不能唯一发现child时省略该项，不用额外权限替代，也不让可选诊断阻止核心对照。


### 2026-10-05：日志两变体真实功能通过，但10k控制在登录阶段失败

f56042f真实部署eager与lazy各完成49操作/35断言，friend→private授权、持久化/幂等/receiver ACK/unread/read、group权限/分页/扇出ACK/owner转移、file跨owner拒绝/取消/续传/字节校验通过。低并发延迟只是逐操作样本，不是全功能容量P99。原生23语义+8计时检查及每variant130业务unit不能代替真实负载验收。

logging150A1在82.50s中止，仅7804登录、1 business_deadline_exceeded；成功登录均值497.83ms/P99上界2314.1ms/max3016.719ms。logging150B1在96.41s中止，仅9190登录、1 auth_timeout。两组消息计划/发送/ACK均0，不能比较ACKP99或给出日志收益百分比，也不继续无效B2/A2。A1网关WARN约9600条主要为private/group离线回放admission拒绝；CPU PSI avg10最高81.13，memory PSI0、guest available约8GiB。回放/认证/心跳的共享资源压力是观察事实，唯一根因尚未得到控制证明。主机随后可用约5GiB且瞬时分页0，这不能反推A1时的分页或资源因果。

已通过原审计rollback恢复a8b7 Gateway/be8 Message和原全部env（SQL batch1/maintenance1/worker16/trace1）；other16/config/durability1/1/1/0/0保持，两组原私有日志在切换前保存。新增一次原sealed image logging150O1控制，固定10k/150/60s/9000计划、login100/s、原期限/HB/真实wire与SQL守卫，区分重编译运行版本与当前共同资源变化；原像对照不能单独隔离全部compiler/cache/cold state/host差异。该提交时original控制/汇总NOT_RUN，不改产品CPP、不清理内存、不宣称突破。全功能10k–50k目标仍未达标。
