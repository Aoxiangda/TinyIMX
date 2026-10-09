-- Query-aligned nonunique indexes; no row, protocol or durability changes.
-- Run through benchmark/local_capacity/apply_private_indexes.py for an audit,
-- definition checks, old/new query parity and execution-plan evidence.
-- One-time DDL against an explicitly bound database. No USE or implicit fallback.
ALTER TABLE im_private_messages
    ADD INDEX idx_im_private_messages_unread_dialog
        (to_user_id, from_user_id, delivery_status),
    ADD INDEX idx_im_private_messages_pending_recipient
        (delivery_status, to_user_id),
    ALGORITHM=INPLACE,
    LOCK=NONE;
