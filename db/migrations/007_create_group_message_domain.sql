USE tinyimx;

-- M17-B1: authoritative durable group-message identity.
CREATE TABLE IF NOT EXISTS im_group_messages (
    message_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    client_message_id VARCHAR(64) NOT NULL,
    group_id BIGINT UNSIGNED NOT NULL,
    from_user_id BIGINT UNSIGNED NOT NULL,
    message_type TINYINT UNSIGNED NOT NULL DEFAULT 1,
    content TEXT NOT NULL,
    membership_epoch BIGINT UNSIGNED NOT NULL,
    member_version BIGINT UNSIGNED NOT NULL,
    authorized_role TINYINT UNSIGNED NOT NULL,
    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),

    PRIMARY KEY (message_id),
    UNIQUE KEY uk_im_group_messages_client_msg (
        from_user_id,
        client_message_id
    ),
    KEY idx_im_group_messages_group_id (
        group_id,
        message_id
    ),
    KEY idx_im_group_messages_sender_id (
        from_user_id,
        message_id
    ),

    CONSTRAINT fk_im_group_messages_group
        FOREIGN KEY (group_id)
        REFERENCES im_groups(group_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_group_messages_sender
        FOREIGN KEY (from_user_id)
        REFERENCES im_users(user_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT chk_im_group_messages_type
        CHECK (message_type IN (1, 2, 3)),

    CONSTRAINT chk_im_group_messages_role
        CHECK (authorized_role IN (1, 2, 3)),

    CONSTRAINT chk_im_group_messages_membership_epoch
        CHECK (membership_epoch > 0),

    CONSTRAINT chk_im_group_messages_member_version
        CHECK (member_version > 0)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;
