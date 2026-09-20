USE tinyimx;

-- M17-B2: one durable fanout identity per (group message, recipient).
CREATE TABLE IF NOT EXISTS im_group_message_deliveries (
    message_id BIGINT UNSIGNED NOT NULL,
    recipient_user_id BIGINT UNSIGNED NOT NULL,
    group_id BIGINT UNSIGNED NOT NULL,

    -- 1=PENDING, 2=DEFERRED_OFFLINE, 3=DELIVERED.
    delivery_status TINYINT UNSIGNED NOT NULL DEFAULT 1,
    attempt_count INT UNSIGNED NOT NULL DEFAULT 0,

    last_gateway_id VARCHAR(128) NULL,
    lease_owner VARCHAR(128) NULL,
    lease_token VARCHAR(128) NULL,
    lease_until DATETIME(3) NULL,
    next_retry_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    last_error_code VARCHAR(128) NULL,

    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3),
    delivered_at DATETIME(3) NULL,

    PRIMARY KEY (message_id, recipient_user_id),
    KEY idx_im_group_delivery_due (
        delivery_status,
        next_retry_at,
        lease_until,
        message_id,
        recipient_user_id
    ),
    KEY idx_im_group_delivery_recipient (
        recipient_user_id,
        delivery_status,
        message_id
    ),
    KEY idx_im_group_delivery_group (
        group_id,
        message_id,
        recipient_user_id
    ),

    CONSTRAINT fk_im_group_delivery_message
        FOREIGN KEY (message_id)
        REFERENCES im_group_messages(message_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_group_delivery_group
        FOREIGN KEY (group_id)
        REFERENCES im_groups(group_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_group_delivery_recipient
        FOREIGN KEY (recipient_user_id)
        REFERENCES im_users(user_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT chk_im_group_delivery_status
        CHECK (delivery_status IN (1, 2, 3))
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;
