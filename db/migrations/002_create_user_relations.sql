USE tinyimx;

CREATE TABLE IF NOT EXISTS im_user_relations (
    user_id BIGINT UNSIGNED NOT NULL,
    peer_user_id BIGINT UNSIGNED NOT NULL,
    relation_status TINYINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (user_id, peer_user_id),
    KEY idx_im_user_relations_peer_user_id (peer_user_id),
    KEY idx_im_user_relations_status (relation_status),

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
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO im_users (
    user_id,
    username,
    nickname,
    status
) VALUES
    (10003, 'user10003', 'User 10003', 1),
    (10004, 'user10004', 'User 10004', 1)
ON DUPLICATE KEY UPDATE
    nickname = VALUES(nickname),
    status = VALUES(status);

INSERT INTO im_user_relations (
    user_id,
    peer_user_id,
    relation_status
) VALUES
    (10001, 10002, 1),
    (10002, 10001, 1),
    (10001, 10003, 2)
ON DUPLICATE KEY UPDATE
    relation_status = VALUES(relation_status);

DELETE FROM im_user_relations
WHERE (user_id = 10001 AND peer_user_id = 10004)
   OR (user_id = 10004 AND peer_user_id = 10001)
   OR (user_id = 10003 AND peer_user_id = 10001);