# TinyIMX M16 Closeout Manifest

## Module

M16 — Transactional Outbox, RocketMQ Event Backbone, and Reliability Closure

## Final Architecture

MessageApplicationService
→ MySQL transaction (message state + event outbox)
→ OutboxRelay
→ RocketMQ
→ UnreadProjector
→ Redis read model

## Delivery Semantics

RocketMQ and OutboxRelay use at-least-once delivery semantics.

A broker publish may be replayed if publication succeeds but the
corresponding Outbox Published transition is not durably recorded.

TinyIMX does not claim system-level Exactly-Once transport.

Application-visible unread state is made effectively-once through stable
event identity, idempotent processing, and authoritative MySQL snapshot
reconciliation.

## Outbox State Model

- Pending: 0
- Published: 1
- Retry: 2
- Quarantined: 3

Quarantined is a terminal operator-visible state and is never silently
treated as Published.

## Production RocketMQ Contract

Topic:

    tinyimx-message-events

Unread projector consumer group:

    tinyimx-unread-projector-v1

Retry policy:

    retryQueueNums = 1
    retryMaxTimes = 16

The production SimpleConsumer runtime reports max_attempts=17.

## Ownership

MySQL and MessageService own durable message state.

MessageService writes the corresponding Outbox event in the same MySQL
transaction as the message/read-state mutation.

OutboxRelay owns event publication.

RocketMQ owns event transport.

When unread_projection.owner=projector, UnreadProjector owns the final
Redis unread projection. Gateway unread mutation remains available only
behind its explicit ownership gate.

## Reliability Closure

M16 acceptance covered:

- rollback when Outbox insertion fails;
- crash after claim before publish;
- publish success before MarkPublished;
- Redis apply before ACK;
- retryable MQ dependency failure and recovery;
- Redis dependency outage and redelivery;
- permanent poison-message bounded retry and DLQ.

## Final Regression

Final release-candidate evidence:

- fresh configure: PASS;
- fresh full build with one build job: PASS;
- ordinary CTest: 25/25 PASS;
- M16 MySQL/Redis integration: 3/3 PASS;
- M12 final regression: PASS=19, FAIL=0;
- M13 final acceptance: PASS=16, FAIL=0;
- retained M14/M15 acceptance: PASS;
- M15 architecture/repository hard gate: PASS.

One build job is used for the final validation VM because parallel heavy
linking caused host-memory pressure. The same source tree completed fully
with one build job.

## Retained-Regression Backlog Recovery

Retained regressions generated durable events while OutboxRelay was not
running.

Before recovery:

- Pending/Retry: 595
- Quarantined: 2

Normal M16 recovery was performed without deleting or manually rewriting
Outbox records.

Relay:

- claimed: 595
- published: 595
- retry: 0
- newly quarantined: 0
- ownership lost: 0

Projector:

- received: 595
- reconciled: 595
- process failures: 0
- ACK failures: 0

After recovery:

- Pending/Retry: 0
- Quarantined: 2

## Test Boundary

Deterministic M16 tests run in ordinary CTest.

MySQL/Redis integration tests use CTest labels and shared-resource locks.

Live RocketMQ transport and destructive reliability scenarios remain
explicit acceptance workflows and are intentionally excluded from ordinary
CTest.

## Deferred Item

The local RocketMQ client may report:

    Unknown metric address scheme

This warning does not affect publication, consumption, ACK, retry, or
projection correctness and is deferred to M20 observability.

## Freeze Rule

M16-C2/C3 destructive fault scenarios are not rerun during final freeze
unless later source changes alter their reliability semantics.

At M16 freeze, active Pending/Retry Outbox backlog must be zero.
