-- Experimental status-leading index only. 012's paired candidate was rejected.
-- Run solely through the audited helper outside owned load; preserve all rows.
ALTER TABLE im_private_messages
  ADD INDEX idx_im_private_messages_pending_recipient (delivery_status, to_user_id),
  ALGORITHM=INPLACE, LOCK=NONE;
