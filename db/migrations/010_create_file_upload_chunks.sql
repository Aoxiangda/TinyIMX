USE tinyimx;

-- M18-B1: durable chunk manifest. Progress correctness is defined by one row
-- per (upload_id, chunk_index), never by a process-local bitmap or one mutable
-- uploaded_offset. File bytes remain in the storage backend.
CREATE TABLE IF NOT EXISTS im_file_upload_chunks (
    upload_id BIGINT UNSIGNED NOT NULL,
    chunk_index BIGINT UNSIGNED NOT NULL,
    file_id BIGINT UNSIGNED NOT NULL,
    owner_user_id BIGINT UNSIGNED NOT NULL,

    byte_offset BIGINT UNSIGNED NOT NULL,
    chunk_size BIGINT UNSIGNED NOT NULL,
    checksum_algorithm VARCHAR(16) NOT NULL,
    checksum CHAR(64) NOT NULL,
    storage_part_key VARCHAR(512) NOT NULL,

    -- 1=RESERVED: durable intent exists, bytes may need retry/recovery.
    -- 2=STORED: atomic storage write completed before this transition.
    status TINYINT UNSIGNED NOT NULL DEFAULT 1,
    version BIGINT UNSIGNED NOT NULL DEFAULT 1,

    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3),
    stored_at DATETIME(3) NULL,

    PRIMARY KEY (upload_id, chunk_index),
    UNIQUE KEY uk_im_file_upload_chunk_storage_key (storage_part_key),
    KEY idx_im_file_upload_chunk_owner (owner_user_id, upload_id, chunk_index),
    KEY idx_im_file_upload_chunk_file (file_id, chunk_index),
    KEY idx_im_file_upload_chunk_status (upload_id, status, chunk_index),

    CONSTRAINT fk_im_file_upload_chunk_upload
        FOREIGN KEY (upload_id)
        REFERENCES im_file_upload_sessions(upload_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_file_upload_chunk_file
        FOREIGN KEY (file_id)
        REFERENCES im_files(file_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT fk_im_file_upload_chunk_owner
        FOREIGN KEY (owner_user_id)
        REFERENCES im_users(user_id)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT chk_im_file_upload_chunks_size CHECK (chunk_size > 0),
    CONSTRAINT chk_im_file_upload_chunks_checksum_algorithm
        CHECK (checksum_algorithm = 'sha256'),
    CONSTRAINT chk_im_file_upload_chunks_status CHECK (status IN (1, 2)),
    CONSTRAINT chk_im_file_upload_chunks_version CHECK (version >= 1)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;
