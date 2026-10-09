#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/repository/GroupRepository.h"
#include "services/group/repository/GroupRepositoryAdapter.h"
#include "services/group/repository/GroupActorSnapshotControl.h"
#include "services/group/application/GroupPermissionPolicy.h"
#include "services/outbox/OutboxRepository.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace t {
using J=nlohmann::json; using Clock=std::chrono::steady_clock;
namespace fs=std::filesystem;
fs::path out; J checks=J::array();
void save(const std::string& name,const J& x) {
    const auto p=out/(name+".tmp");
    {std::ofstream f(p);f<<x.dump(2)<<'\n';f.flush();if(!f)throw std::runtime_error("OwnEvidenceWrite");}
    fs::rename(p,out/name);
}
void check(const std::string& n,bool pass) {
    checks.push_back({{"name",n},{"pass",pass}});save("checks.json",checks);
    if(!pass)throw std::runtime_error("OwnCheck:"+n);
}
template<class R> J group(const R& r) {
    return J::array({r.group_id,r.name,r.description,r.avatar_url,r.owner_user_id,
        static_cast<int>(r.status),static_cast<int>(r.join_policy),r.max_members,
        r.version,r.member_version,r.created_at,r.updated_at,r.disbanded_at,r.disbanded_by_user_id});
}
template<class R> J member(const R& r) {
    return J::array({r.group_id,r.user_id,static_cast<int>(r.role),static_cast<int>(r.status),
        r.membership_epoch,r.muted_until,r.joined_at,r.left_at,r.updated_at});
}
J result(const tinyimx::GroupFindResult& r) {
    return {{"status",static_cast<int>(r.status)},{"found",r.found},{"record",group(r.record)},{"message",r.message}};
}
J result(const tinyimx::GroupMemberFindResult& r) {
    return {{"status",static_cast<int>(r.status)},{"found",r.found},{"record",member(r.record)},{"message",r.message}};
}
J api(tinyimx::group::GroupRepositoryAdapter& a,int operation,std::uint64_t uid,std::uint64_t gid,
      std::uint32_t limit=50,std::uint64_t after=0) {
    if(operation==0) {
        const auto r=a.GetGroup(uid,gid);
        return {{"status",static_cast<int>(r.status)},{"group",r.group?group(*r.group):J(nullptr)},{"message",r.message}};
    }
    if(operation==1) {
        tinyimx::group::ListGroupMembersQuery q;q.actor_user_id=uid;q.group_id=gid;q.limit=limit;q.after_user_id=after;
        const auto r=a.ListGroupMembers(q);J rows=J::array();for(const auto& v:r.members)rows.push_back(member(v));
        return {{"status",static_cast<int>(r.status)},{"members",rows},{"has_more",r.has_more},{"message",r.message}};
    }
    if(operation==2) {
        const auto r=a.CheckGroupSendPermission(uid,gid);
        return {{"status",static_cast<int>(r.status)},{"allowed",r.allowed},{"role",static_cast<int>(r.role)},
            {"epoch",r.membership_epoch},{"member_version",r.member_version},{"message",r.message}};
    }
    const auto r=a.PrepareGroupMessageSend(uid,gid);
    return {{"status",static_cast<int>(r.status)},{"allowed",r.allowed},{"role",static_cast<int>(r.role)},
        {"epoch",r.membership_epoch},{"member_version",r.member_version},{"recipients",r.recipient_user_ids},{"message",r.message}};
}
J timing(std::vector<double> v) {
    const double mean=std::accumulate(v.begin(),v.end(),0.0)/v.size();std::sort(v.begin(),v.end());
    return {{"samples",v.size()},{"mean_ms",mean},{"p50_ms",v[static_cast<std::size_t>(std::ceil(.5*v.size()))-1]},
        {"p99_ms",v[static_cast<std::size_t>(std::ceil(.99*v.size()))-1]},{"max_ms",v.back()}};
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=7)throw std::runtime_error("OwnArguments");
        t::out=argv[4];
        const std::string allowed="/home/jackson7/projects/TinyIMX_publish/.local/codex/group-actor-snapshot-build-20261006/";
        if(t::out.string().rfind(allowed,0)!=0||!t::fs::is_directory(t::out))throw std::runtime_error("OwnEvidencePath");
        const bool enabled=tinyimx::group::GroupActorSnapshotEnabled();
        t::check("strict-rollout-control",enabled==(std::string(argv[6])=="1"));
        tinyimx::Config config;if(!config.LoadFromFile(argv[1]))throw std::runtime_error("OwnConfigLoad");
        auto mysql=config.MySql();if(!mysql.enable)throw std::runtime_error("OwnMysqlDisabled");
        mysql.host=argv[3];mysql.pool_size=std::stoi(argv[2]);
        if(mysql.pool_size!=1&&mysql.pool_size!=4)throw std::runtime_error("OwnPoolSize");
        tinyimx::MySqlConnectionPool pool;t::check("own-pool-initialize",pool.Initialize(mysql));
        tinyimx::GroupRepository repo(&pool);tinyimx::outbox::OutboxRepository outbox(&pool);
        tinyimx::group::GroupPermissionPolicy permission;
        tinyimx::group::GroupRepositoryAdapter adapter(&repo,&pool,&outbox,&permission);
        std::ifstream f(argv[5]);t::J pre;f>>pre;const auto& cases=pre.at("cases");t::check("bounded-real-corpus",cases.size()==149);
        t::J direct=t::J::array(),behavior=t::J::array();
        std::uint64_t active_gid=0,active_uid=0;
        for(std::size_t i=0;i<cases.size();++i) {
            const auto& c=cases[i];const auto gid=c.at("group_id").get<std::uint64_t>(),uid=c.at("user_id").get<std::uint64_t>();
            auto conn=pool.Acquire();t::check("snapshot-begin-"+std::to_string(i),conn&&conn->BeginConsistentReadTransaction());
            const auto g=repo.FindGroupByIdOnConnection(conn.operator->(),gid,false);
            const auto m=repo.FindMemberOnConnection(conn.operator->(),gid,uid,false);
            const auto joined=repo.FindGroupAndMemberOnConnection(conn.operator->(),gid,uid);
            t::check("full-group-and-member-equivalence-"+std::to_string(i),t::result(g)==t::result(joined.group)&&t::result(m)==t::result(joined.member));
            direct.push_back({{"group_id",gid},{"uid",uid},{"group",t::result(joined.group)},{"member",t::result(joined.member)}});
            t::check("snapshot-commit-"+std::to_string(i),conn->Commit());conn.Reset();
            for(int op=0;op<4;++op)behavior.push_back({{"case",i},{"operation",op},{"response",t::api(adapter,op,uid,gid)}});
            if(!active_gid && c.value("group_status",0)==1 && c.value("exists",false)&&c.value("role",0)==1) {active_gid=gid;active_uid=uid;}
        }
        t::check("active-positive-fixture",active_gid>0&&active_uid>0);
        for(const auto& ids:std::vector<std::pair<std::uint64_t,std::uint64_t>>{{0,active_uid},{active_gid,0},{0,0}}) {
            auto conn=pool.Acquire();const auto g=repo.FindGroupByIdOnConnection(conn.operator->(),ids.first,false);
            const auto m=repo.FindMemberOnConnection(conn.operator->(),ids.first,ids.second,false);
            const auto j=repo.FindGroupAndMemberOnConnection(conn.operator->(),ids.first,ids.second);
            t::check("invalid-argument-original-equivalence-"+std::to_string(ids.first)+"-"+std::to_string(ids.second),t::result(g)==t::result(j.group)&&t::result(m)==t::result(j.member));
        }
        const auto null_result=repo.FindGroupAndMemberOnConnection(nullptr,active_gid,active_uid);
        t::check("null-connection-typed-invalid",null_result.group.status==tinyimx::GroupRepositoryStatus::kInvalidArgument&&null_result.member.status==tinyimx::GroupRepositoryStatus::kInvalidArgument);
        tinyimx::MySqlConnection closed;const auto closed_result=repo.FindGroupAndMemberOnConnection(&closed,active_gid,active_uid);
        t::check("closed-connection-storage-error",closed_result.group.status==tinyimx::GroupRepositoryStatus::kStorageError&&closed_result.member.status==tinyimx::GroupRepositoryStatus::kStorageError);
        for(const auto limit:std::vector<std::uint32_t>{1,20,100,0,101})
            for(const auto after:std::vector<std::uint64_t>{0,active_uid,99999999})
                behavior.push_back({{"limit",limit},{"after",after},{"response",t::api(adapter,1,active_uid,active_gid,limit,after)}});
        t::save("full-direct-corpus.json",direct);t::save("adapter-behavior.json",behavior);
        t::J perf=t::J::array();
        for(int op=0;op<4;++op) {
            t::J raw=t::J::array();std::vector<double> times;
            const auto expected=t::api(adapter,op,active_uid,active_gid);
            t::check("positive-interface-"+std::to_string(op),expected.at("status")==0);
            for(int warm=0;warm<5;++warm)t::check("warm-equivalence-"+std::to_string(op)+"-"+std::to_string(warm),t::api(adapter,op,active_uid,active_gid)==expected);
            for(int j=0;j<100;++j) {
                const auto start=t::Clock::now();const auto response=t::api(adapter,op,active_uid,active_gid);
                const double ms=std::chrono::duration<double,std::milli>(t::Clock::now()-start).count();
                if(response!=expected)throw std::runtime_error("OwnPerformanceResponseMismatch");
                times.push_back(ms);raw.push_back({{"index",j},{"ms",ms}});
            }
            perf.push_back({{"operation",op},{"metrics",t::timing(times)},{"raw",raw},{"full_response",expected}});
        }
        t::save("performance.json",perf);
        t::check("all-leases-returned",pool.AvailableCount()==static_cast<std::size_t>(mysql.pool_size));
        pool.Shutdown();t::check("own-shutdown-no-lease",!pool.Acquire(std::chrono::milliseconds{1}));
        t::save("result.json",{{"status","GROUP_ACTOR_NATIVE_READONLY_PASS"},{"checks",t::checks.size()},{"cases",cases.size()},
            {"adapter_behavior_rows",behavior.size()},{"pool_size",mysql.pool_size},{"enabled",enabled},{"performance",perf},
            {"production_data_writes",false},{"limits","One native persistent account/read corpus, closed-loop400samples/case; notGateway,10k50kcapacity,populationP99,fault orracefrequencyproof"}});
        std::cout<<"GROUP_ACTOR_NATIVE_READONLY_PASS checks="<<t::checks.size()<<'\n';return 0;
    }catch(const std::exception& e){
        if(!t::out.empty()&&t::fs::is_directory(t::out))t::save("failed.json",{{"status","FAIL"},{"type","OwnNativeException"},{"message",e.what()},{"checks",t::checks}});
        std::cerr<<e.what()<<'\n';return 1;
    }
}
