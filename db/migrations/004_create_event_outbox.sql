USE tinyimx;

CREATE TABLE IF NOT EXISTS im_event_outbox (
    outbox_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,

    event_id VARCHAR(191) NOT NULL,
    event_type VARCHAR(96) NOT NULL,
    schema_version INT UNSIGNED NOT NULL DEFAULT 1,

    aggregate_type VARCHAR(64) NOT NULL,
    aggregate_id VARCHAR(128) NOT NULL,
    producer_service VARCHAR(64) NOT NULL,

    topic VARCHAR(128) NOT NULL,
    tag VARCHAR(64) NOT NULL DEFAULT '',
    message_key VARCHAR(191) NOT NULL,
    payload JSON NOT NULL,

    status TINYINT UNSIGNED NOT NULL DEFAULT 0,
    attempt_count INT UNSIGNED NOT NULL DEFAULT 0,
    next_attempt_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),

    locked_by VARCHAR(128) NULL,
    locked_until DATETIME(3) NULL,
    last_error VARCHAR(1024) NOT NULL DEFAULT '',

    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    published_at DATETIME(3) NULL,
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3),

    PRIMARY KEY (outbox_id),

    UNIQUE KEY uk_im_event_outbox_event_id (event_id),

    KEY idx_im_event_outbox_publish_scan (
        status,
        next_attempt_at,
        locked_until,
        outbox_id
    ),

    KEY idx_im_event_outbox_aggregate (
        aggregate_type,
        aggregate_id,
        outbox_id
    ),

    CONSTRAINT chk_im_event_outbox_status
        CHECK (status IN (0, 1, 2, 3))
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;
