#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/message/local/NativeActorReferences.h"
#include "services/message/local/MySqlLegacyIdentity.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace tinyimx;
using namespace tinyimx::message;
using namespace tinyimx::message::local;
namespace {
void Check(bool ok,const char* text) {
    if(!ok)throw std::runtime_error(text);
    std::cout<<"PASS "<<text<<std::endl;
}
nlohmann::json Snapshot(LocalPrivateStore& store) {
    const auto one=store.FindPrivateMessageByClientMessageId(960001,"upgrade-cid");
    const auto two=store.FindPrivateMessageByClientMessageId(960001,"upgrade-two");
    const auto pending=store.CountPending(960002);
    const auto unread=store.ReadUnreadSnapshot(960002,960001);
    const auto events=store.ListPendingEvents(16);
    const auto actors=store.ListMessageActors();
    Check(one.Found()&&two.Found()&&pending.Succeeded()&&unread.Succeeded()&&
          events.Succeeded()&&actors.Succeeded(),"complete upgrade state readable");
    nlohmann::json serialized=nlohmann::json::array();
    for(const auto&e:events.events)serialized.push_back({e.event_id,e.topic,e.tag,e.message_key,e.body});
    return {{"first_mid",one.record.message_id},{"second_mid",two.record.message_id},
            {"first_body",one.record.content},{"second_body",two.record.content},
            {"pending",pending.count},{"private_unread",unread.private_unread},
            {"total_unread",unread.total_unread},{"events",serialized},{"actors",actors.actors}};
}
}
int main(int argc,char**argv) {
    try {
        if(argc!=4)return 2;
        Config cfg;Check(cfg.LoadFromFile(argv[1])&&cfg.MySql().database=="codex_perf146_20261020_r5","exact isolated native upgrade schema");
        const std::filesystem::path root=argv[2];const std::string mode=argv[3];
        Check(root.is_absolute()&&root.string()=="/home/jackson7/projects/tinyimx-interview-20261017/fk-free-qualification-r146/upgrade-r5"&&std::filesystem::canonical(root)==root,"owned retained physical upgrade path");
        Check(mode=="seed"||mode=="verify"||mode=="reject","explicit upgrade mode");
        Check(Logger::Instance().Init(cfg.Logger()),"upgrade logger");
        MySqlConnectionPool pool;auto sql=cfg.MySql();sql.pool_size=4;Check(pool.Initialize(sql),"real SQL upgrade pool");
        auto oracle=std::make_shared<MySqlLegacyIdentity>(&pool);LocalPrivateStoreOptions options;
        options.directory=(root/"owner").string();options.storage_identity="native-fk-upgrade-r146";
        options.first_message_id=7290000000000000ULL;options.initial_user_ids={960001,960002};
        options.message_events_topic="codex_perf146_upgrade_events";options.block_cache_bytes=8*1024*1024;
        options.identity_policy=oracle->Policy();options.resolve_identity=[oracle](auto from,auto to,const auto& cid){return oracle->Resolve(from,to,cid);};
        std::unique_ptr<NativeActorReferences> refs;
        options.before_new_message_commit=[&](auto from,auto to){return refs->EnsureActors(from,to);};
        if(mode=="reject") {
            bool rejected=false;
            try { refs=std::make_unique<NativeActorReferences>(&pool,options); }
            catch(const std::exception&e) { rejected=std::string(e.what()).find("FK")!=std::string::npos; }
            Check(rejected,"incompatible writer/schema combination rejects startup");
            refs.reset();pool.Shutdown();Logger::Instance().Shutdown();return 0;
        }
        refs=std::make_unique<NativeActorReferences>(&pool,options);
        LocalPrivateStore store(options);refs->ReconcileBeforeServing(store);
        if(mode=="seed") {
            const auto one=store.PersistPrivateMessage(960001,960002,"UpGrade-CID",1,"accepted before FK migration");
            const auto two=store.PersistPrivateMessage(960001,960002,"upgrade-two",1,"second durable message");
            Check(one.Accepted()&&two.Accepted(),"legacy writer accepts two durable messages");
            Check(store.ConfirmReceiver(one.message_id,960002).Succeeded(),"legacy receiver confirmation committed");
            Check(store.MarkDialogRead(960002,960001).Succeeded(),"legacy read state and event committed");
            std::ofstream(root/"seed-state.json")<<Snapshot(store).dump(2)<<'\n';
        } else {
            nlohmann::json expected;std::ifstream(root/"seed-state.json")>>expected;
            Check(Snapshot(store)==expected,"MIDs content confirmation unread events and actors survive in-place upgrade");
            const auto retry=store.PersistPrivateMessage(960001,960002,"upgrade-cid",1,"accepted before FK migration");
            Check(retry.outcome==PersistPrivateMessageOutcome::kIdempotencyConflict&&retry.message_id==expected["first_mid"].get<std::uint64_t>(),"collation alias retains old MID and existing exact-request conflict contract");
            const auto exact=store.PersistPrivateMessage(960001,960002,"UpGrade-CID",1,"accepted before FK migration");
            Check(exact.outcome==PersistPrivateMessageOutcome::kReused&&exact.message_id==retry.message_id,"exact original CID retry is reused after migration");
            const auto next=store.PersistPrivateMessage(960001,960002,"upgrade-next",1,"accepted by new FK-free writer");
            Check(next.Accepted()&&next.message_id>expected["second_mid"].get<std::uint64_t>(),"new writer appends without ID reuse");
        }
        Check(store.Shutdown(),"upgrade engine closes before owner lease release");
        refs.reset();pool.Shutdown();Logger::Instance().Shutdown();return 0;
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
