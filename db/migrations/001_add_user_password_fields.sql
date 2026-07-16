ALTER TABLE im_users
ADD COLUMN password_salt VARCHAR(64) NOT NULL DEFAULT "" AFTER username;

ALTER TABLE im_users
ADD COLUMN password_hash VARCHAR(128) NOT NULL DEFAULT "" AFTER password_salt;