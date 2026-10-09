-- TinyIMX: add the retention-range index without changing cleanup semantics.
-- One-time DDL. Use apply_index.py for definition-aware idempotence and identity checks.
-- Keep old migrations immutable. No USE: target database must be explicitly bound.
-- No row deletion, no delivery_status mutation, no durability/isolation change.
ALTER TABLE im_event_outbox
    ADD INDEX idx_im_event_outbox_cleanup (status, published_at, outbox_id),
    ALGORITHM=INPLACE,
    LOCK=NONE;
