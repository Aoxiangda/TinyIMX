#include "common/config/Config.h"
#include "common/db/LogicalReferences.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/repository/MessageRepository.h"
#include "services/repository/GroupRepository.h"
#include "services/repository/FileRepository.h"
#include "services/message/local/NativeActorReferences.h"
#include "services/message/local/MySqlLegacyIdentity.h"

#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace tinyimx;
using db::LogicalReference;
namespace {
void Check(bool ok, const char* text) {
    if (!ok) throw std::runtime_error(text);
    std::cout << "PASS " << text << std::endl;
}
std::string Scalar(MySqlConnection* c, const std::string& sql) {
    MySqlQueryResult rows;
    Check(c && c->Query(sql,&rows) && rows.rows.size()==1 && rows.rows[0].size()==1,
          "bounded fixture observation");
    return rows.rows[0][0];
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=3) return 2;
        Config cfg;
        Check(cfg.LoadFromFile(argv[1]) &&
              cfg.MySql().database=="codex_perf146_20261020_r1", "exact isolated FK-free schema");
        const std::filesystem::path physical=argv[2];
        Check(physical.is_absolute() && physical.string().starts_with(
            "/home/jackson7/projects/tinyimx-interview-20261017/fk-free-qualification-r146/"),
            "owned native fixture path");
        Check(Logger::Instance().Init(cfg.Logger()),"fixture logger");
        MySqlConnectionPool pool;auto sqlcfg=cfg.MySql();sqlcfg.pool_size=6;
        Check(pool.Initialize(sqlcfg),"real bounded SQL pool");
        auto c=pool.Acquire();
        Check(c && Scalar(c.operator->(),"SELECT COUNT(*) FROM information_schema.referential_constraints WHERE CONSTRAINT_SCHEMA=DATABASE()") == "0", "zero physical foreign keys");
        Check(c->Execute("INSERT INTO im_users(user_id,username,nickname,status)VALUES(960001,'fkfree_a','A',1),(960002,'fkfree_b','B',1),(960003,'fkfree_c','C',1)"), "immutable parent fixtures");
        Check(!db::ReferencesExist(c.operator->(),{LogicalReference::User(999999999)}),"missing logical reference rejected");
        Check(!db::ReferencesExist(c.operator->(),{LogicalReference::User(0)}),"zero logical reference rejected");
        MessageRepository messages(&pool);GroupRepository groups(&pool);FileRepository files(&pool);
        Check(!messages.SavePrivateMessage(960001,999999999,"bad",DeliveryStatus::kPending,PrivateMessageType::kText,"bad_single").Succeeded(),"ordinary private write rejects absent receiver");
        Check(c->BeginTransaction(),"caller owns multi-table transaction");
        const auto gid=groups.InsertGroupOnConnection(c.operator->(),960001,"FK free","","",1,100);
        Check(gid.Succeeded() && gid.group_id,"valid group accepted without FK");
        Check(!groups.InsertGroupOnConnection(c.operator->(),999999999,"bad","","",1,100).Succeeded(),"group rejects absent owner");
        Check(!groups.InsertMemberOnConnection(c.operator->(),gid.group_id,999999999,3,1).Succeeded(),"member rejects absent user");
        Check(!groups.InsertMemberOnConnection(c.operator->(),999999999,960002,3,1).Succeeded(),"member rejects absent group");
        Check(groups.InsertOwnerMemberOnConnection(c.operator->(),gid.group_id,960001).Succeeded(),"owner member inserted on same transaction");
        Check(groups.InsertMembershipHistoryOnConnection(c.operator->(),gid.group_id,960001,1,1).Succeeded(),"membership history inserted on same transaction");
        Check(!groups.UpdateGroupOwnerAndVersionsOnConnection(c.operator->(),gid.group_id,999999999,2,2).Succeeded(),"owner transfer rejects missing identity");
        Check(!groups.DisbandGroupOnConnection(c.operator->(),gid.group_id,999999999,2,2).Succeeded(),"disband rejects missing actor");
        Check(!groups.CloseMembershipHistoryOnConnection(c.operator->(),gid.group_id,960001,1,1,999999999).Succeeded(),"history close rejects missing actor");
        const auto gm=messages.SaveGroupMessageOnConnection(c.operator->(),gid.group_id,960001,"group_ok",1,"body",1,1,1);
        Check(gm.Succeeded(),"group message accepted");
        Check(!messages.SaveGroupMessageOnConnection(c.operator->(),999999999,960001,"group_bad",1,"body",1,1,1).Succeeded(),"message rejects absent group");
        Check(!messages.InsertGroupMessageDeliveriesOnConnection(c.operator->(),gm.message_id,gid.group_id,{960002,999999999}).Succeeded(),"delivery rejects whole mixed-recipient batch before writes");
        Check(Scalar(c.operator->(),"SELECT COUNT(*) FROM im_group_message_deliveries")=="0","invalid delivery batch leaves no partial rows");
        Check(!messages.InsertGroupMessageDeliveriesOnConnection(c.operator->(),gm.message_id,gid.group_id+1,{960002}).Succeeded(),"delivery rejects mismatched message/group");
        Check(messages.InsertGroupMessageDeliveriesOnConnection(c.operator->(),gm.message_id,gid.group_id,{960002}).Succeeded(),"valid group delivery inserted");
        const std::string hash(64,'a');
        Check(!files.InsertFileOnConnection(c.operator->(),999999999,"bad","text/plain",128,"sha256",hash).Succeeded(),"file rejects absent owner");
        const auto file=files.InsertFileOnConnection(c.operator->(),960001,"good","text/plain",128,"sha256",hash);
        Check(file.Succeeded(),"valid file accepted");
        Check(!files.InsertUploadSessionIdempotentOnConnection(c.operator->(),file.file_id,960002,"bad_upload",hash,128,128).Succeeded(),"upload rejects file-owner mismatch");
        const auto upload=files.InsertUploadSessionIdempotentOnConnection(c.operator->(),file.file_id,960001,"good_upload",hash,128,128);
        Check(upload.Succeeded(),"valid upload accepted");
        Check(!files.InsertChunkIdempotentOnConnection(c.operator->(),upload.upload_id,0,file.file_id+1,960001,0,128,"sha256",hash,"bad-part").Succeeded(),"chunk rejects upload-file mismatch");
        Check(files.InsertChunkIdempotentOnConnection(c.operator->(),upload.upload_id,0,file.file_id,960001,0,128,"sha256",hash,"good-part").Succeeded(),"valid upload chunk accepted");
        PrivateMessageRecord good;good.from_user_id=960001;good.to_user_id=960002;good.client_message_id="bulk_ok";good.message_type=1;good.content="body";good.delivery_status=0;
        auto bad=good;bad.client_message_id="bulk_bad";bad.to_user_id=999999999;
        Check(!messages.SaveAndFindPrivateMessagesBulkOnConnection(c.operator->(),{good,bad}).Succeeded(),"bulk SQL path rejects entire invalid actor set");
        Check(!messages.SaveAndFindPrivateMessagesOnConnection(c.operator->(),{good,bad}).Succeeded(),"statement SQL path rejects entire invalid actor set");
        Check(Scalar(c.operator->(),"SELECT COUNT(*) FROM im_private_messages")=="0","invalid private batches have zero partial writes");
        Check(c->Rollback(),"caller rollback remains authoritative");
        Check(Scalar(c.operator->(),"SELECT COUNT(*) FROM im_groups") == "0" &&
              Scalar(c.operator->(),"SELECT COUNT(*) FROM im_files") == "0", "rollback removes parent and all same-transaction children");
        const auto accepted=messages.SavePrivateMessage(960001,960002,"valid",DeliveryStatus::kPending,PrivateMessageType::kText,"valid_single");
        Check(accepted.Succeeded(),"ordinary valid private write accepted");
        Check(c->Execute("UPDATE im_users SET status=2 WHERE user_id=960002"),"account lifecycle retains disabled parent identity");
        Check(db::ReferencesExist(c.operator->(),{LogicalReference::User(960002)}) &&
              Scalar(c.operator->(),"SELECT COUNT(*) FROM im_private_messages")=="1", "disabling parent preserves historical references without cascade");
        // Native mode requires empty SQL private history. Roll back a native
        // fixture transaction rather than erasing any accepted SQL message.
        c.Reset();pool.Shutdown();Logger::Instance().Shutdown();
        std::cout << "FK_FREE_REPOSITORY_CONTRACTS_PASS" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << std::endl;return 1;
    }
}
