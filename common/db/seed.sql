USE tinyimx;

INSERT INTO im_users (
    user_id,
    username,
    nickname,
    password_hash,
    status
) VALUES
    (10001, 'user10001', 'User 10001', 'demo-password-hash', 1),
    (10002, 'user10002', 'User 10002', 'demo-password-hash', 1)
ON DUPLICATE KEY UPDATE
    nickname = VALUES(nickname),
    status = VALUES(status);

INSERT INTO im_user_relations (
    user_id,
    peer_user_id,
    relation_status
) VALUES
    (10001, 10002, 1),
    (10002, 10001, 1)
ON DUPLICATE KEY UPDATE
    relation_status = VALUES(relation_status);