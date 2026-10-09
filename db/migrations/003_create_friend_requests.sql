USE tinyimx;

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
        ON DELETE CASCADE,

    CONSTRAINT fk_im_friend_requests_to_user
        FOREIGN KEY (to_user_id)
        REFERENCES im_users(user_id)
        ON DELETE CASCADE,

    CONSTRAINT chk_im_friend_requests_status
        CHECK (request_status IN (0, 1, 2, 3))
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;