USE tinyimx;

-- M18-A1: File metadata is durable business truth. Large file bytes do not
-- enter the chat-message/MySQL payload path; storage_key is server-generated.
CREATE TABLE IF NOT EXISTS im_files (
    file_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    owner_user_id BIGINT UNSIGNED NOT NULL,

    file_name VARCHAR(255) NOT NULL,
    content_type VARCHAR(128) NOT NULL DEFAULT 'application/octet-stream',
    total_size BIGINT UNSIGNED NOT NULL,

    checksum_algorithm VARCHAR(16) NOT NULL,
    expected_checksum CHAR(64) NOT NULL,
    verified_checksum CHAR(64) NULL,

    storage_backend VARCHAR(32) NOT NULL DEFAULT 'local_fs',
    storage_key VARCHAR(512) NULL,

    status TINYINT UNSIGNED NOT NULL DEFAULT 1,
    version BIGINT UNSIGNED NOT NULL DEFAULT 1,

    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3),
    available_at DATETIME(3) NULL,
    expires_at DATETIME(3) NULL,

    PRIMARY KEY (file_id),
    UNIQUE KEY uk_im_files_storage_key (storage_backend, storage_key),
    KEY idx_im_files_owner_status (owner_user_id, status, file_id),
    KEY idx_im_files_status_id (status, file_id),

    CONSTRAINT fk_im_files_owner
        FOREIGN KEY (owner_user_id)
        REFERENCES im_users(user_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT chk_im_files_total_size CHECK (total_size > 0),
    CONSTRAINT chk_im_files_status CHECK (status IN (1, 2, 3, 4, 5, 6, 7)),
    CONSTRAINT chk_im_files_version CHECK (version >= 1),
    CONSTRAINT chk_im_files_checksum_algorithm CHECK (checksum_algorithm = 'sha256')
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;

-- Durable upload protocol state. client_upload_id is the business idempotency
-- key for response-loss retry; Packet.seq must never replace it.
CREATE TABLE IF NOT EXISTS im_file_upload_sessions (
    upload_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    file_id BIGINT UNSIGNED NOT NULL,
    owner_user_id BIGINT UNSIGNED NOT NULL,

    client_upload_id VARCHAR(64) NOT NULL,
    request_fingerprint CHAR(64) NOT NULL,

    total_size BIGINT UNSIGNED NOT NULL,
    chunk_size BIGINT UNSIGNED NOT NULL,

    status TINYINT UNSIGNED NOT NULL DEFAULT 1,
    version BIGINT UNSIGNED NOT NULL DEFAULT 1,

    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3),
    expires_at DATETIME(3) NOT NULL,
    completed_at DATETIME(3) NULL,

    PRIMARY KEY (upload_id),
    UNIQUE KEY uk_im_file_upload_client (owner_user_id, client_upload_id),
    UNIQUE KEY uk_im_file_upload_file (file_id),
    KEY idx_im_file_upload_owner_status (owner_user_id, status, upload_id),
    KEY idx_im_file_upload_expiry (status, expires_at, upload_id),

    CONSTRAINT fk_im_file_upload_file
        FOREIGN KEY (file_id)
        REFERENCES im_files(file_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_file_upload_owner
        FOREIGN KEY (owner_user_id)
        REFERENCES im_users(user_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT chk_im_file_upload_total_size CHECK (total_size > 0),
    CONSTRAINT chk_im_file_upload_chunk_size CHECK (chunk_size > 0),
    CONSTRAINT chk_im_file_upload_status CHECK (status IN (1, 2, 3, 4, 5)),
    CONSTRAINT chk_im_file_upload_version CHECK (version >= 1)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;
