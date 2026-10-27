-- Deploy the FK-free application writers first and quiesce all writers.
-- Invoke in the intended schema with @tinyimx_confirm_quiesced=1.
-- No rows or indexes are deleted. DDL auto-commits; after interruption rerun.
-- Do not disable foreign_key_checks. Preflight rejects existing orphan data.
-- Native stores keep their physical ownership, journal and actor registries.
DELIMITER $$
DROP PROCEDURE IF EXISTS tinyimx_remove_business_foreign_keys$$
CREATE PROCEDURE tinyimx_remove_business_foreign_keys()
BEGIN
  DECLARE finished BOOL DEFAULT FALSE;
  DECLARE invalid_ref BOOL DEFAULT FALSE;
  DECLARE table_to_alter VARCHAR(64);
  DECLARE key_to_remove VARCHAR(64);
  DECLARE ddl_lock BOOL DEFAULT FALSE;
  DECLARE constraints_cursor CURSOR FOR
    SELECT TABLE_NAME,CONSTRAINT_NAME FROM information_schema.referential_constraints
      WHERE CONSTRAINT_SCHEMA=DATABASE() AND TABLE_NAME IN ('im_file_upload_chunks','im_file_upload_sessions','im_files','im_friend_requests','im_group_members','im_group_membership_history','im_group_message_deliveries','im_group_messages','im_group_operation_dedup','im_groups','im_native_private_actor_refs','im_private_messages','im_user_relations')
      ORDER BY TABLE_NAME,CONSTRAINT_NAME;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET finished=TRUE;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    IF ddl_lock THEN DO RELEASE_LOCK(CONCAT('timx:fkfree:',DATABASE())); END IF;
    RESIGNAL;
  END;
  IF DATABASE() IS NULL OR COALESCE(@tinyimx_confirm_quiesced,0)<>1 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='explicit schema and quiesced-writer confirmation required';
  END IF;
  SELECT GET_LOCK(CONCAT('timx:fkfree:',DATABASE()),0)=1 INTO ddl_lock;
  IF NOT COALESCE(ddl_lock,FALSE) THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='logical-reference migration already running'; END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_user_relations') THEN
    SELECT EXISTS(SELECT 1 FROM `im_user_relations` child LEFT JOIN `im_users` parent
      ON child.`user_id`=parent.`user_id`
      WHERE child.`user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_user_relations_user'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_user_relations') THEN
    SELECT EXISTS(SELECT 1 FROM `im_user_relations` child LEFT JOIN `im_users` parent
      ON child.`peer_user_id`=parent.`user_id`
      WHERE child.`peer_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_user_relations_peer'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_friend_requests') THEN
    SELECT EXISTS(SELECT 1 FROM `im_friend_requests` child LEFT JOIN `im_users` parent
      ON child.`from_user_id`=parent.`user_id`
      WHERE child.`from_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_friend_requests_from_user'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_friend_requests') THEN
    SELECT EXISTS(SELECT 1 FROM `im_friend_requests` child LEFT JOIN `im_users` parent
      ON child.`to_user_id`=parent.`user_id`
      WHERE child.`to_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_friend_requests_to_user'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_private_messages') THEN
    SELECT EXISTS(SELECT 1 FROM `im_private_messages` child LEFT JOIN `im_users` parent
      ON child.`from_user_id`=parent.`user_id`
      WHERE child.`from_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_private_messages_from_user'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_private_messages') THEN
    SELECT EXISTS(SELECT 1 FROM `im_private_messages` child LEFT JOIN `im_users` parent
      ON child.`to_user_id`=parent.`user_id`
      WHERE child.`to_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_private_messages_to_user'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_groups') THEN
    SELECT EXISTS(SELECT 1 FROM `im_groups` child LEFT JOIN `im_users` parent
      ON child.`owner_user_id`=parent.`user_id`
      WHERE child.`owner_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_groups_owner'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_groups') THEN
    SELECT EXISTS(SELECT 1 FROM `im_groups` child LEFT JOIN `im_users` parent
      ON child.`disbanded_by_user_id`=parent.`user_id`
      WHERE child.`disbanded_by_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_groups_disbanded_by'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_members') THEN
    SELECT EXISTS(SELECT 1 FROM `im_group_members` child LEFT JOIN `im_groups` parent
      ON child.`group_id`=parent.`group_id`
      WHERE child.`group_id` IS NOT NULL AND parent.`group_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_group_members_group'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_members') THEN
    SELECT EXISTS(SELECT 1 FROM `im_group_members` child LEFT JOIN `im_users` parent
      ON child.`user_id`=parent.`user_id`
      WHERE child.`user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_group_members_user'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_membership_history') THEN
    SELECT EXISTS(SELECT 1 FROM `im_group_membership_history` child LEFT JOIN `im_groups` parent
      ON child.`group_id`=parent.`group_id`
      WHERE child.`group_id` IS NOT NULL AND parent.`group_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_group_membership_history_group'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_membership_history') THEN
    SELECT EXISTS(SELECT 1 FROM `im_group_membership_history` child LEFT JOIN `im_users` parent
      ON child.`user_id`=parent.`user_id`
      WHERE child.`user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_group_membership_history_user'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_membership_history') THEN
    SELECT EXISTS(SELECT 1 FROM `im_group_membership_history` child LEFT JOIN `im_users` parent
      ON child.`ended_by_user_id`=parent.`user_id`
      WHERE child.`ended_by_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_group_membership_history_ended_by'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_operation_dedup') THEN
    SELECT EXISTS(SELECT 1 FROM `im_group_operation_dedup` child LEFT JOIN `im_users` parent
      ON child.`actor_user_id`=parent.`user_id`
      WHERE child.`actor_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_group_operation_actor'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_operation_dedup') THEN
    SELECT EXISTS(SELECT 1 FROM `im_group_operation_dedup` child LEFT JOIN `im_groups` parent
      ON child.`group_id`=parent.`group_id`
      WHERE child.`group_id` IS NOT NULL AND parent.`group_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_group_operation_group'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_messages') THEN
    SELECT EXISTS(SELECT 1 FROM `im_group_messages` child LEFT JOIN `im_groups` parent
      ON child.`group_id`=parent.`group_id`
      WHERE child.`group_id` IS NOT NULL AND parent.`group_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_group_messages_group'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_messages') THEN
    SELECT EXISTS(SELECT 1 FROM `im_group_messages` child LEFT JOIN `im_users` parent
      ON child.`from_user_id`=parent.`user_id`
      WHERE child.`from_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_group_messages_sender'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_message_deliveries') THEN
    SELECT EXISTS(SELECT 1 FROM `im_group_message_deliveries` child LEFT JOIN `im_group_messages` parent
      ON child.`message_id`=parent.`message_id`
      WHERE child.`message_id` IS NOT NULL AND parent.`message_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_group_delivery_message'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_message_deliveries') THEN
    SELECT EXISTS(SELECT 1 FROM `im_group_message_deliveries` child LEFT JOIN `im_groups` parent
      ON child.`group_id`=parent.`group_id`
      WHERE child.`group_id` IS NOT NULL AND parent.`group_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_group_delivery_group'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_message_deliveries') THEN
    SELECT EXISTS(SELECT 1 FROM `im_group_message_deliveries` child LEFT JOIN `im_users` parent
      ON child.`recipient_user_id`=parent.`user_id`
      WHERE child.`recipient_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_group_delivery_recipient'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_files') THEN
    SELECT EXISTS(SELECT 1 FROM `im_files` child LEFT JOIN `im_users` parent
      ON child.`owner_user_id`=parent.`user_id`
      WHERE child.`owner_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_files_owner'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_file_upload_sessions') THEN
    SELECT EXISTS(SELECT 1 FROM `im_file_upload_sessions` child LEFT JOIN `im_files` parent
      ON child.`file_id`=parent.`file_id`
      WHERE child.`file_id` IS NOT NULL AND parent.`file_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_file_upload_file'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_file_upload_sessions') THEN
    SELECT EXISTS(SELECT 1 FROM `im_file_upload_sessions` child LEFT JOIN `im_users` parent
      ON child.`owner_user_id`=parent.`user_id`
      WHERE child.`owner_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_file_upload_owner'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_file_upload_chunks') THEN
    SELECT EXISTS(SELECT 1 FROM `im_file_upload_chunks` child LEFT JOIN `im_file_upload_sessions` parent
      ON child.`upload_id`=parent.`upload_id`
      WHERE child.`upload_id` IS NOT NULL AND parent.`upload_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_file_upload_chunk_upload'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_file_upload_chunks') THEN
    SELECT EXISTS(SELECT 1 FROM `im_file_upload_chunks` child LEFT JOIN `im_files` parent
      ON child.`file_id`=parent.`file_id`
      WHERE child.`file_id` IS NOT NULL AND parent.`file_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_file_upload_chunk_file'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_file_upload_chunks') THEN
    SELECT EXISTS(SELECT 1 FROM `im_file_upload_chunks` child LEFT JOIN `im_users` parent
      ON child.`owner_user_id`=parent.`user_id`
      WHERE child.`owner_user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_im_file_upload_chunk_owner'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_native_private_actor_refs') THEN
    SELECT EXISTS(SELECT 1 FROM `im_native_private_actor_refs` child LEFT JOIN `im_native_private_owner` parent
      ON child.`owner_slot`=parent.`owner_slot`
      WHERE child.`owner_slot` IS NOT NULL AND parent.`owner_slot` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_native_actor_owner'; END IF;
  END IF;
  IF EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_native_private_actor_refs') THEN
    SELECT EXISTS(SELECT 1 FROM `im_native_private_actor_refs` child LEFT JOIN `im_users` parent
      ON child.`user_id`=parent.`user_id`
      WHERE child.`user_id` IS NOT NULL AND parent.`user_id` IS NULL LIMIT 1) INTO invalid_ref;
    IF invalid_ref THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='orphan logical reference: fk_native_actor_user'; END IF;
  END IF;

  OPEN constraints_cursor;
  removal_loop: LOOP
    FETCH constraints_cursor INTO table_to_alter,key_to_remove;
    IF finished THEN LEAVE removal_loop; END IF;
    SET @tinyimx_drop_fk=CONCAT('ALTER TABLE `',REPLACE(table_to_alter,'`','``'),
      '` DROP FOREIGN KEY `',REPLACE(key_to_remove,'`','``'),'`');
    PREPARE tinyimx_fk_ddl FROM @tinyimx_drop_fk;
    EXECUTE tinyimx_fk_ddl;
    DEALLOCATE PREPARE tinyimx_fk_ddl;
  END LOOP;
  CLOSE constraints_cursor;
  IF EXISTS(SELECT 1 FROM information_schema.referential_constraints
      WHERE CONSTRAINT_SCHEMA=DATABASE() AND TABLE_NAME IN ('im_file_upload_chunks','im_file_upload_sessions','im_files','im_friend_requests','im_group_members','im_group_membership_history','im_group_message_deliveries','im_group_messages','im_group_operation_dedup','im_groups','im_native_private_actor_refs','im_private_messages','im_user_relations')) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='business foreign keys remain';
  END IF;
  DO RELEASE_LOCK(CONCAT('timx:fkfree:',DATABASE()));
END$$
CALL tinyimx_remove_business_foreign_keys()$$
DROP PROCEDURE tinyimx_remove_business_foreign_keys$$
DELIMITER ;
