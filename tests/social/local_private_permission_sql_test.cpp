#include "common/config/Config.h"
#include "common/db/MySqlConnection.h"
#include "services/repository/FriendRepository.h"
#include "services/social/repository/FriendRepositoryAdapter.h"
#include "services/rpc/LocalPrivateChatPermissionClient.h"
#include "services/rpc/SocialRpcClient.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include "services/social/service/SocialServiceImpl.h"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace tinyimx;using namespace std::chrono_literals;
void Require(bool ok,const char* name){if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<std::endl;}
rpc::RpcCallOptions Budget(std::chrono::milliseconds time=2s){rpc::RpcCallOptions out;out.remaining_timeout=time;return out;}
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)return 2;
        Config config;Require(config.LoadFromFile(argv[1]),"explicit owned config loaded");
        Require(config.MySql().enable&&config.MySql().database=="codex_perf041_20261018_r94","only newly allocated r94 schema permitted");
        MySqlConnection observer;Require(observer.Connect(config.MySql()),"observer connected");
        MySqlConnectionPool pool1,pool2;Require(pool1.Initialize(config.MySql())&&pool2.Initialize(config.MySql()),"two independent local compositions connected");
        FriendRepository repo1(&pool1),repo2(&pool2);
        social::FriendRepositoryAdapter adapter1(&repo1),adapter2(&repo2);
        social::FriendApplicationService app1(&adapter1),app2(&adapter2);
        social::SocialServiceImpl service(&app1);
        grpc::ServerBuilder builder;int port=0;
        builder.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);builder.RegisterService(&service);
        auto server=builder.BuildAndStart();Require(server&&port,"actual Social RPC endpoint started");
        struct StopServer {grpc::Server* server;~StopServer(){server->Shutdown();server->Wait();}} stop{server.get()};
        rpc::SocialRpcClient remote(std::make_shared<rpc::StaticServiceEndpointProvider>("127.0.0.1:"+std::to_string(port)));
        rpc::LocalPrivateChatPermissionClient local1(&app1,1,4),local2(&app2,1,4);
        const rpc::CheckPrivateChatPermissionRpcRequest request{500001,500002};
        auto check=[&](rpc::ChatPermissionRpcOutcome expected,const char* label){
            auto a=remote.CheckPrivateChatPermission(request,Budget());
            auto b=local1.CheckPrivateChatPermission(request,Budget());auto c=local2.CheckPrivateChatPermission(request,Budget());
            Require(a.ok()&&b.ok()&&c.ok()&&a.value->outcome==expected&&b.value->outcome==expected&&c.value->outcome==expected&&a.value->message==b.value->message&&a.value->message==c.value->message,label);
        };
        auto sql=[&](const std::string& s){Require(observer.Execute(s),"owned relation mutation committed");};
        auto reset=[&]{sql("INSERT INTO im_user_relations(user_id,peer_user_id,relation_status) VALUES(500001,500002,1),(500002,500001,1) ON DUPLICATE KEY UPDATE relation_status=VALUES(relation_status)");};
        reset();check(rpc::ChatPermissionRpcOutcome::kAllowed,"RPC/local1/local2 agree on mutual friendship");
        sql("UPDATE im_user_relations SET relation_status=2 WHERE user_id=500001 AND peer_user_id=500002");
        check(rpc::ChatPermissionRpcOutcome::kBlockedBySelf,"self block is immediately authoritative across instances");
        reset();sql("UPDATE im_user_relations SET relation_status=2 WHERE user_id=500002 AND peer_user_id=500001");
        check(rpc::ChatPermissionRpcOutcome::kBlockedByPeer,"peer block is immediately authoritative across instances");
        reset();sql("DELETE FROM im_user_relations WHERE user_id=500001 AND peer_user_id=500002");
        check(rpc::ChatPermissionRpcOutcome::kNotFriend,"one-sided relationship removal rejects both local instances");
        reset();check(rpc::ChatPermissionRpcOutcome::kAllowed,"unblock/re-add after earlier denial is visible without TTL");
        for(int i=0;i<20;++i){sql("UPDATE im_user_relations SET relation_status="+std::to_string(i%2?1:2)+" WHERE user_id=500002 AND peer_user_id=500001");check(i%2?rpc::ChatPermissionRpcOutcome::kAllowed:rpc::ChatPermissionRpcOutcome::kBlockedByPeer,"repeated committed revocation/re-enable never uses stale allow");}
        pool1.Shutdown();
        auto failed=local1.CheckPrivateChatPermission(request,Budget());
        Require(failed.ok()&&!failed.value->Allowed()&&failed.value->outcome==rpc::ChatPermissionRpcOutcome::kStorageError,"local DB dependency failure fails closed");
        Require(local2.CheckPrivateChatPermission(request,Budget()).value->Allowed(),"independent instance still queries its own healthy pool");
        Require(pool1.Initialize(config.MySql()),"local pool recovered");check(rpc::ChatPermissionRpcOutcome::kAllowed,"recovery rereads authoritative relationship");
        sql("LOCK TABLES im_user_relations WRITE");
        auto timeout=local1.CheckPrivateChatPermission(request,Budget(30ms));
        Require(!timeout.ok()&&timeout.status.code==rpc::RpcErrorCode::kDeadlineExceeded,"real blocked MySQL SELECT returns deadline without permission success");
        auto drain=std::async(std::launch::async,[&]{local1.Drain();});
        const bool waited=drain.wait_for(20ms)==std::future_status::timeout;
        sql("UNLOCK TABLES");
        Require(waited&&drain.wait_for(2s)==std::future_status::ready,"drain joins actual blocked read before releasing its application");drain.get();
        Require(local1.Stats().expired_after_execution>=1,"real late SQL result discarded safely");
        local2.Drain();
        MySqlQueryResult q;Require(observer.Query("SELECT (SELECT COUNT(*) FROM im_private_messages),(SELECT COUNT(*) FROM im_event_outbox)",&q)&&q.rows.size()==1&&q.rows[0]==std::vector<std::string>{"0","0"},"permission resource tests create no messages or Outbox events");
        std::cout<<"REAL_LOCAL_PERMISSION_AND_RPC_SQL_CONTRACTS_PASS"<<std::endl;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
    return 0;
}
