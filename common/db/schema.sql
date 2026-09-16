CREATE DATABASE IF NOT EXISTS tinyimx
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

USE tinyimx;

CREATE TABLE IF NOT EXISTS im_users (
    user_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    username VARCHAR(64) NOT NULL,
    nickname VARCHAR(64) NOT NULL DEFAULT '',

    avatar_url VARCHAR(255) NOT NULL DEFAULT '',

    password_salt VARCHAR(64) NOT NULL DEFAULT '',
    password_hash VARCHAR(128) NOT NULL DEFAULT '',
    status TINYINT UNSIGNED NOT NULL DEFAULT 1,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    last_login_at DATETIME NULL,

    PRIMARY KEY (user_id),
    UNIQUE KEY uk_im_users_username (username),
    KEY idx_im_users_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS im_user_relations (
    user_id BIGINT UNSIGNED NOT NULL,
    peer_user_id BIGINT UNSIGNED NOT NULL,

    relation_status TINYINT UNSIGNED NOT NULL,

    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (
        user_id,
        peer_user_id
    ),

    KEY idx_im_user_relations_peer_user_id (
        peer_user_id
    ),

    KEY idx_im_user_relations_status (
        relation_status
    ),

    CONSTRAINT fk_im_user_relations_user
        FOREIGN KEY (user_id)
        REFERENCES im_users(user_id)
        ON DELETE CASCADE
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_user_relations_peer
        FOREIGN KEY (peer_user_id)
        REFERENCES im_users(user_id)
        ON DELETE CASCADE
        ON UPDATE CASCADE,

    CONSTRAINT chk_im_user_relations_status
        CHECK (relation_status IN (1, 2))
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS im_friend_requests (
    request_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,

    from_user_id BIGINT UNSIGNED NOT NULL,
    to_user_id BIGINT UNSIGNED NOT NULL,

    request_message VARCHAR(255) NOT NULL DEFAULT '',
    request_status TINYINT UNSIGNED NOT NULL DEFAULT 0,

    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    handled_at DATETIME NULL,

    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (request_id),

    UNIQUE KEY uk_im_friend_requests_direction (
        from_user_id,
        to_user_id
    ),

    KEY idx_im_friend_requests_receiver_status_created (
        to_user_id,
        request_status,
        created_at,
        request_id
    ),

    KEY idx_im_friend_requests_sender_status_created (
        from_user_id,
        request_status,
        created_at,
        request_id
    ),

    CONSTRAINT fk_im_friend_requests_from_user
        FOREIGN KEY (from_user_id)
        REFERENCES im_users(user_id)
        ON DELETE CASCADE
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_friend_requests_to_user
        FOREIGN KEY (to_user_id)
        REFERENCES im_users(user_id)
        ON DELETE CASCADE
        ON UPDATE CASCADE,

    CONSTRAINT chk_im_friend_requests_status
        CHECK (request_status IN (0, 1, 2, 3))
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS im_private_messages (
    message_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    client_message_id VARCHAR(64) NULL,

    from_user_id BIGINT UNSIGNED NOT NULL,
    to_user_id BIGINT UNSIGNED NOT NULL,

    message_type TINYINT UNSIGNED NOT NULL DEFAULT 1,
    content TEXT NOT NULL,

    delivery_status TINYINT UNSIGNED NOT NULL DEFAULT 0,

    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    delivered_at DATETIME NULL,
    read_at DATETIME NULL,

    PRIMARY KEY (message_id),

    UNIQUE KEY uk_im_private_messages_client_msg (
        from_user_id,
        client_message_id
    ),

    KEY idx_im_private_messages_to_status_id (
        to_user_id,
        delivery_status,
        message_id
    ),

    KEY idx_im_private_messages_from_id (
        from_user_id,
        message_id
    ),

    KEY idx_im_private_messages_dialog_id (
        from_user_id,
        to_user_id,
        message_id
    ),

    CONSTRAINT fk_im_private_messages_from_user
        FOREIGN KEY (from_user_id)
        REFERENCES im_users(user_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_private_messages_to_user
        FOREIGN KEY (to_user_id)
        REFERENCES im_users(user_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
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
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- M17-A: authoritative group domain. MySQL remains the durable source of truth.
CREATE TABLE IF NOT EXISTS im_groups (
    group_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    name VARCHAR(128) NOT NULL,
    description VARCHAR(512) NOT NULL DEFAULT '',
    avatar_url VARCHAR(512) NOT NULL DEFAULT '',

    owner_user_id BIGINT UNSIGNED NOT NULL,
    status TINYINT UNSIGNED NOT NULL DEFAULT 1,
    join_policy TINYINT UNSIGNED NOT NULL DEFAULT 1,
    max_members INT UNSIGNED NOT NULL DEFAULT 500,

    version BIGINT UNSIGNED NOT NULL DEFAULT 1,
    member_version BIGINT UNSIGNED NOT NULL DEFAULT 1,

    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3),
    disbanded_at DATETIME(3) NULL,
    disbanded_by_user_id BIGINT UNSIGNED NULL,

    PRIMARY KEY (group_id),
    KEY idx_im_groups_owner_status (owner_user_id, status, group_id),
    KEY idx_im_groups_status_id (status, group_id),

    CONSTRAINT fk_im_groups_owner
        FOREIGN KEY (owner_user_id)
        REFERENCES im_users(user_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_groups_disbanded_by
        FOREIGN KEY (disbanded_by_user_id)
        REFERENCES im_users(user_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT chk_im_groups_status
        CHECK (status IN (1, 2)),
    CONSTRAINT chk_im_groups_join_policy
        CHECK (join_policy IN (1, 2)),
    CONSTRAINT chk_im_groups_max_members
        CHECK (max_members BETWEEN 2 AND 5000),
    CONSTRAINT chk_im_groups_version
        CHECK (version >= 1),
    CONSTRAINT chk_im_groups_member_version
        CHECK (member_version >= 1)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS im_group_members (
    group_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,

    role TINYINT UNSIGNED NOT NULL DEFAULT 3,
    status TINYINT UNSIGNED NOT NULL DEFAULT 1,
    membership_epoch BIGINT UNSIGNED NOT NULL DEFAULT 1,
    muted_until DATETIME(3) NULL,

    joined_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    left_at DATETIME(3) NULL,
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3),

    PRIMARY KEY (group_id, user_id),
    KEY idx_im_group_members_user_status (user_id, status, group_id),
    KEY idx_im_group_members_group_status_role (group_id, status, role, user_id),

    CONSTRAINT fk_im_group_members_group
        FOREIGN KEY (group_id)
        REFERENCES im_groups(group_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_group_members_user
        FOREIGN KEY (user_id)
        REFERENCES im_users(user_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT chk_im_group_members_role
        CHECK (role IN (1, 2, 3)),
    CONSTRAINT chk_im_group_members_status
        CHECK (status IN (1, 2, 3)),
    CONSTRAINT chk_im_group_members_epoch
        CHECK (membership_epoch >= 1)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS im_group_membership_history (
    group_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    membership_epoch BIGINT UNSIGNED NOT NULL,

    role_at_join TINYINT UNSIGNED NOT NULL,
    joined_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    ended_at DATETIME(3) NULL,
    end_reason TINYINT UNSIGNED NULL,
    ended_by_user_id BIGINT UNSIGNED NULL,

    PRIMARY KEY (group_id, user_id, membership_epoch),
    KEY idx_im_group_membership_history_user (user_id, group_id, membership_epoch),

    CONSTRAINT fk_im_group_membership_history_group
        FOREIGN KEY (group_id)
        REFERENCES im_groups(group_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_group_membership_history_user
        FOREIGN KEY (user_id)
        REFERENCES im_users(user_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_group_membership_history_ended_by
        FOREIGN KEY (ended_by_user_id)
        REFERENCES im_users(user_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT chk_im_group_membership_history_role
        CHECK (role_at_join IN (1, 2, 3)),
    CONSTRAINT chk_im_group_membership_history_end_reason
        CHECK (end_reason IS NULL OR end_reason IN (1, 2, 3)),
    CONSTRAINT chk_im_group_membership_history_epoch
        CHECK (membership_epoch >= 1)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;

-- Durable business idempotency record. A1 uses it for group lifecycle
-- mutations; A2 extends the same primitive to membership mutations.
CREATE TABLE IF NOT EXISTS im_group_operation_dedup (
    actor_user_id BIGINT UNSIGNED NOT NULL,
    client_operation_id VARCHAR(64) NOT NULL,

    operation_type VARCHAR(64) NOT NULL,
    request_fingerprint CHAR(64) NOT NULL,

    group_id BIGINT UNSIGNED NOT NULL,
    result_version BIGINT UNSIGNED NOT NULL,
    result_member_version BIGINT UNSIGNED NOT NULL,
    result_membership_epoch BIGINT UNSIGNED NULL,

    completed_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),

    PRIMARY KEY (actor_user_id, client_operation_id),
    KEY idx_im_group_operation_group (group_id, completed_at),

    CONSTRAINT fk_im_group_operation_actor
        FOREIGN KEY (actor_user_id)
        REFERENCES im_users(user_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_group_operation_group
        FOREIGN KEY (group_id)
        REFERENCES im_groups(group_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT chk_im_group_operation_result_version
        CHECK (result_version >= 1),
    CONSTRAINT chk_im_group_operation_result_member_version
        CHECK (result_member_version >= 1)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;
