USE tinyimx;

-- RocketMQ 5.x topic resources reject dots. M16-A had already persisted
-- publication intent using tinyimx.message.events, so migrate only events
-- that are still eligible for publication. Published history is immutable;
-- quarantined rows stay quarantined for explicit operator review.
UPDATE im_event_outbox
SET topic = 'tinyimx-message-events',
    updated_at = CURRENT_TIMESTAMP(3)
WHERE topic = 'tinyimx.message.events'
  AND status IN (0, 2);
