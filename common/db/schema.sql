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