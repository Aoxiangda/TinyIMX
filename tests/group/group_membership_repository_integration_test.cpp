#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/group/application/GroupPermissionPolicy.h"
#include "services/group/repository/GroupRepositoryAdapter.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/GroupRepository.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t kOwner = 10001;
constexpr std::uint64_t kUser2 = 10002;
constexpr std::uint64_t kUser3 = 10003;
constexpr std::uint64_t kUser4 = 10004;
constexpr std::uint64_t kUser5 = 10005;
constexpr std::uint64_t kUser6 = 10006;
int g_failed = 0;

void Expect(bool condition, const std::string& name) {
    if (condition) std::cout << "[PASS] " << name << '\n';
    else { std::cerr << "[FAIL] " << name << '\n'; ++g_failed; }
}

std::string UniqueId(const std::string& prefix) {
    const auto nanos=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return prefix+std::to_string(static_cast<long long>(nanos));
}

std::uint64_t CountRows(tinyimx::MySqlConnectionPool* pool,const std::string& table,const std::string& where_clause) {
    auto c=pool->Acquire(); if(!c) return 0;
    tinyimx::MySqlQueryResult q;
    if(!c->Query("SELECT COUNT(*) FROM "+table+" WHERE "+where_clause,&q) || q.rows.size()!=1 || q.rows[0].size()!=1) return 0;
    return static_cast<std::uint64_t>(std::stoull(q.rows[0][0]));
}

std::string Scalar(tinyimx::MySqlConnectionPool* pool,const std::string& sql) {
    auto c=pool->Acquire(); if(!c) return {};
    tinyimx::MySqlQueryResult q; if(!c->Query(sql,&q) || q.rows.size()!=1 || q.rows[0].size()!=1) return {};
    return q.rows[0][0];
}

void EnsureUsers(tinyimx::MySqlConnectionPool* pool) {
    auto c=pool->Acquire(); if(!c) return;
    for(std::uint64_t id=kOwner; id<=kUser6; ++id) {
        c->Execute("INSERT IGNORE INTO im_users (user_id, username, nickname, status) VALUES ("+
                   std::to_string(id)+", 'm17a2-user-"+std::to_string(id)+"', 'M17A2', 1)");
    }
}

void CleanupGroup(tinyimx::MySqlConnectionPool* pool,std::uint64_t gid) {
    if (!gid) {
        return;
    }
    auto c = pool->Acquire();
    if (!c) {
        return;
    }
    c->Execute("DELETE FROM im_event_outbox WHERE aggregate_type='group' AND aggregate_id='"+std::to_string(gid)+"'");
    c->Execute("DELETE FROM im_group_operation_dedup WHERE group_id="+std::to_string(gid));
    c->Execute("DELETE FROM im_group_membership_history WHERE group_id="+std::to_string(gid));
    c->Execute("DELETE FROM im_group_members WHERE group_id="+std::to_string(gid));
    c->Execute("DELETE FROM im_groups WHERE group_id="+std::to_string(gid));
}

std::uint64_t CreateOpenGroup(tinyimx::group::GroupRepositoryAdapter* adapter,std::uint32_t max_members,const std::string& name) {
    tinyimx::group::CreateGroupCommand c;
    c.actor_user_id=kOwner; c.client_operation_id=UniqueId("m17a2-create-"); c.name=name;
    c.join_policy=tinyimx::group::GroupJoinPolicy::kOpen; c.max_members=max_members;
    const auto r=adapter->CreateGroup(c);
    return r.Accepted() && r.group ? r.group->group_id : 0;
}

}  // namespace

int main(int argc,char* argv[]) {
    std::string config_path="config/gateway-a.local.json"; if(argc>=2) config_path=argv[1];
    tinyimx::Config config; if(!config.LoadFromFile(config_path)){std::cerr<<"[FAIL] config load\n"; return 1;}
    if(!tinyimx::Logger::Instance().Init(config.Logger())) return 1;
    tinyimx::MySqlConnectionPool pool; if(!pool.Initialize(config.MySql())) return 1;
    EnsureUsers(&pool);

    tinyimx::GroupRepository repo(&pool);
    tinyimx::outbox::OutboxRepository outbox(&pool);
    tinyimx::group::GroupPermissionPolicy permission;
    tinyimx::group::GroupRepositoryAdapter adapter(&repo,&pool,&outbox,&permission);

    std::cout<<"========== TinyIMX M17-A2 Membership Repository Integration ==========\n";
    const std::uint64_t gid=CreateOpenGroup(&adapter,10,UniqueId("m17a2-main-"));
    Expect(gid!=0,"create OPEN group for A2 membership tests");

    tinyimx::group::JoinGroupCommand join2{kUser2,UniqueId("join2-"),gid};
    const auto joined=adapter.JoinGroup(join2);
    Expect(joined.Accepted() && joined.group && joined.group->member_version==2,"OPEN JoinGroup commits membership mutation");
    Expect(CountRows(&pool,"im_group_members","group_id="+std::to_string(gid)+" AND user_id="+std::to_string(kUser2)+" AND status=1 AND role=3 AND membership_epoch=1")==1,"JoinGroup creates MEMBER epoch=1");
    const auto join_reused=adapter.JoinGroup(join2);
    Expect(join_reused.Accepted() && join_reused.outcome==tinyimx::group::GroupMutationOutcome::kReused,"JoinGroup response-loss retry is REUSED");

    tinyimx::group::LeaveGroupCommand leave2{kUser2,UniqueId("leave2-"),gid};
    const auto left=adapter.LeaveGroup(leave2);
    Expect(left.Accepted(),"LeaveGroup commits durable LEFT transition");
    Expect(CountRows(&pool,"im_group_membership_history","group_id="+std::to_string(gid)+" AND user_id="+std::to_string(kUser2)+" AND membership_epoch=1 AND end_reason=1 AND ended_at IS NOT NULL")==1,"LeaveGroup closes membership history epoch=1");

    tinyimx::group::JoinGroupCommand rejoin2{kUser2,UniqueId("rejoin2-"),gid};
    const auto rejoined=adapter.JoinGroup(rejoin2);
    Expect(rejoined.Accepted(),"LEFT member can rejoin OPEN group");
    Expect(CountRows(&pool,"im_group_members","group_id="+std::to_string(gid)+" AND user_id="+std::to_string(kUser2)+" AND status=1 AND role=3 AND membership_epoch=2")==1,"rejoin increments membership_epoch");

    tinyimx::group::KickMemberCommand kick2{kOwner,UniqueId("kick2-"),gid,kUser2};
    const auto kicked=adapter.KickMember(kick2);
    Expect(kicked.Accepted(),"Owner can kick active MEMBER");
    tinyimx::group::JoinGroupCommand illegal_join{kUser2,UniqueId("illegal-rejoin-"),gid};
    Expect(adapter.JoinGroup(illegal_join).status==tinyimx::group::GroupApplicationStatus::kFailedPrecondition,"KICKED member cannot self-rejoin OPEN group");

    tinyimx::group::InviteMemberCommand invite2{kOwner,UniqueId("invite2-"),gid,kUser2};
    const auto invited=adapter.InviteMember(invite2);
    Expect(invited.Accepted(),"Owner can re-invite KICKED member");
    Expect(CountRows(&pool,"im_group_members","group_id="+std::to_string(gid)+" AND user_id="+std::to_string(kUser2)+" AND status=1 AND role=3 AND membership_epoch=3")==1,"re-invite resets role to MEMBER and increments epoch");

    tinyimx::group::SetMemberRoleCommand promote2{kOwner,UniqueId("promote2-"),gid,kUser2,tinyimx::group::GroupRole::kAdmin};
    Expect(adapter.SetMemberRole(promote2).Accepted(),"Owner can promote MEMBER to ADMIN");
    Expect(Scalar(&pool,"SELECT role FROM im_group_members WHERE group_id="+std::to_string(gid)+" AND user_id="+std::to_string(kUser2))=="2","role mutation persisted ADMIN");

    tinyimx::group::InviteMemberCommand invite3{kOwner,UniqueId("invite3-"),gid,kUser3};
    Expect(adapter.InviteMember(invite3).Accepted(),"Owner invites second MEMBER");
    tinyimx::group::KickMemberCommand admin_kick{kUser2,UniqueId("admin-kick-"),gid,kUser3};
    Expect(adapter.KickMember(admin_kick).Accepted(),"ADMIN can kick MEMBER");
    tinyimx::group::InviteMemberCommand reinvite3{kOwner,UniqueId("reinvite3-"),gid,kUser3};
    Expect(adapter.InviteMember(reinvite3).Accepted(),"Owner re-invites kicked user3");
    tinyimx::group::SetMemberRoleCommand promote3{kOwner,UniqueId("promote3-"),gid,kUser3,tinyimx::group::GroupRole::kAdmin};
    Expect(adapter.SetMemberRole(promote3).Accepted(),"Owner promotes user3 to ADMIN");
    tinyimx::group::KickMemberCommand admin_kick_admin{kUser2,UniqueId("admin-kick-admin-"),gid,kUser3};
    Expect(adapter.KickMember(admin_kick_admin).status==tinyimx::group::GroupApplicationStatus::kPermissionDenied,"ADMIN cannot kick ADMIN");

    tinyimx::group::SetMemberMuteCommand mute2{kOwner,UniqueId("mute2-"),gid,kUser2,"2099-01-01 00:00:00.000"};
    Expect(adapter.SetMemberMute(mute2).Accepted(),"Owner can mute ADMIN");
    const auto denied_send=adapter.CheckGroupSendPermission(kUser2,gid);
    Expect(denied_send.Succeeded() && !denied_send.allowed && denied_send.role==tinyimx::group::GroupRole::kAdmin,"active mute denies group send permission");
    tinyimx::group::SetMemberMuteCommand unmute2{kOwner,UniqueId("unmute2-"),gid,kUser2,""};
    Expect(adapter.SetMemberMute(unmute2).Accepted(),"Owner can unmute ADMIN");
    const auto allowed_send=adapter.CheckGroupSendPermission(kUser2,gid);
    Expect(allowed_send.Succeeded() && allowed_send.allowed && allowed_send.membership_epoch==3,"unmuted active ADMIN can send with epoch snapshot");

    const auto listed=adapter.ListGroupMembers({kOwner,gid,0,2});
    Expect(listed.Succeeded() && listed.members.size()==2 && listed.has_more,"ListGroupMembers uses keyset page + has_more");
    const auto listed2=adapter.ListGroupMembers({kOwner,gid,listed.members.back().user_id,2});
    Expect(listed2.Succeeded() && !listed2.members.empty(),"ListGroupMembers continues with after_user_id cursor");
    const auto mygroups=adapter.ListMyGroups({kUser2,0,50});
    bool found_group=false; for(const auto& g:mygroups.groups) if(g.group_id==gid) found_group=true;
    Expect(mygroups.Succeeded() && found_group,"ListMyGroups returns active membership groups");

    tinyimx::group::TransferOwnershipCommand transfer{kOwner,UniqueId("transfer-"),gid,kUser2};
    const auto transferred=adapter.TransferOwnership(transfer);
    Expect(transferred.Accepted() && transferred.group && transferred.group->owner_user_id==kUser2,"TransferOwnership atomically changes owner");
    Expect(Scalar(&pool,"SELECT role FROM im_group_members WHERE group_id="+std::to_string(gid)+" AND user_id="+std::to_string(kOwner))=="2" &&
           Scalar(&pool,"SELECT role FROM im_group_members WHERE group_id="+std::to_string(gid)+" AND user_id="+std::to_string(kUser2))=="1","TransferOwnership updates both roles atomically");
    tinyimx::group::LeaveGroupCommand old_owner_leave{kOwner,UniqueId("old-owner-leave-"),gid};
    Expect(adapter.LeaveGroup(old_owner_leave).Accepted(),"previous owner can leave after transfer");
    tinyimx::group::LeaveGroupCommand current_owner_leave{kUser2,UniqueId("owner-leave-"),gid};
    Expect(adapter.LeaveGroup(current_owner_leave).status==tinyimx::group::GroupApplicationStatus::kFailedPrecondition,"current owner cannot leave without transfer");

    // Outbox failure must roll back membership, history, versions, and operation record.
    const auto before_failure=adapter.GetGroup(kUser2,gid);
    tinyimx::group::InviteMemberCommand rollback_invite{kUser2,UniqueId("rollback-invite-"),gid,kUser5};
    outbox.SetForceInsertFailureForTest(true);
    const auto rollback_result=adapter.InviteMember(rollback_invite);
    outbox.SetForceInsertFailureForTest(false);
    Expect(!rollback_result.Completed(),"forced Outbox failure rejects InviteMember");
    Expect(CountRows(&pool,"im_group_members","group_id="+std::to_string(gid)+" AND user_id="+std::to_string(kUser5))==0,"Outbox failure rolls back membership row");
    Expect(CountRows(&pool,"im_group_operation_dedup","actor_user_id="+std::to_string(kUser2)+" AND client_operation_id='"+rollback_invite.client_operation_id+"'")==0,"Outbox failure rolls back membership operation dedup");
    const auto after_failure=adapter.GetGroup(kUser2,gid);
    Expect(before_failure.Found() && after_failure.Found() && before_failure.group->version==after_failure.group->version,"Outbox failure rolls back group versions");

    // Concurrent duplicate InviteMember: exactly one APPLIED, all others REUSED.
    const std::string concurrent_op=UniqueId("concurrent-invite-");
    tinyimx::group::InviteMemberCommand concurrent_invite{kUser2,concurrent_op,gid,kUser4};
    constexpr std::size_t kThreads=4;
    std::vector<tinyimx::group::GroupRepositoryMutationResult> results(kThreads);
    std::vector<std::thread> threads;
    for(std::size_t i=0;i<kThreads;++i) threads.emplace_back([&,i]{results[i]=adapter.InviteMember(concurrent_invite);});
    for(auto& t:threads) t.join();
    std::size_t applied=0,reused=0; for(const auto& r:results){if(r.Completed()&&r.outcome==tinyimx::group::GroupMutationOutcome::kApplied)++applied; if(r.Completed()&&r.outcome==tinyimx::group::GroupMutationOutcome::kReused)++reused;}
    Expect(applied==1 && reused==kThreads-1,"concurrent duplicate InviteMember converges to one mutation");
    Expect(CountRows(&pool,"im_group_membership_history","group_id="+std::to_string(gid)+" AND user_id="+std::to_string(kUser4))==1,"concurrent duplicate creates one membership epoch/history row");

    // Capacity race: max=2 means owner + exactly one of two concurrent joins.
    const std::uint64_t capacity_gid=CreateOpenGroup(&adapter,2,UniqueId("m17a2-capacity-"));
    tinyimx::group::JoinGroupCommand join5{kUser5,UniqueId("cap-join5-"),capacity_gid};
    tinyimx::group::JoinGroupCommand join6{kUser6,UniqueId("cap-join6-"),capacity_gid};
    tinyimx::group::GroupRepositoryMutationResult r5,r6;
    std::thread t5([&]{r5=adapter.JoinGroup(join5);}); std::thread t6([&]{r6=adapter.JoinGroup(join6);}); t5.join(); t6.join();
    const int accepted=(r5.Accepted()?1:0)+(r6.Accepted()?1:0);
    const int exhausted=(r5.status==tinyimx::group::GroupApplicationStatus::kResourceExhausted?1:0)+(r6.status==tinyimx::group::GroupApplicationStatus::kResourceExhausted?1:0);
    Expect(accepted==1 && exhausted==1,"group-row locking prevents concurrent max_members overflow");
    Expect(CountRows(&pool,"im_group_members","group_id="+std::to_string(capacity_gid)+" AND status=1")==2,"capacity race leaves exactly max_members active rows");

    // M17-B2 recipient snapshot isolation: authorization/member-version reads and
    // recipient enumeration must observe one point-in-time view even if the
    // server/session default transaction isolation is changed by deployment.
    const std::uint64_t snapshot_gid=CreateOpenGroup(&adapter,10,UniqueId("m17b2-snapshot-"));
    tinyimx::group::JoinGroupCommand snapshot_join2{kUser2,UniqueId("snapshot-join2-"),snapshot_gid};
    Expect(snapshot_gid!=0 && adapter.JoinGroup(snapshot_join2).Accepted(),
           "M17-B2 snapshot fixture has one recipient before concurrent join");
    auto snapshot_connection=pool.Acquire();
    bool snapshot_started=snapshot_connection && snapshot_connection->BeginConsistentReadTransaction();
    Expect(snapshot_started,"M17-B2 explicit consistent-read transaction starts");
    if(snapshot_started) {
        tinyimx::MySqlQueryResult before_rows;
        const std::string recipient_count_sql=
            "SELECT COUNT(*) FROM im_group_members WHERE group_id="+std::to_string(snapshot_gid)+
            " AND status=1 AND user_id<>"+std::to_string(kOwner);
        const bool before_ok=snapshot_connection->Query(recipient_count_sql,&before_rows) &&
            before_rows.rows.size()==1 && before_rows.rows[0].size()==1 && before_rows.rows[0][0]=="1";
        Expect(before_ok,"M17-B2 consistent snapshot initially sees original recipient set");

        tinyimx::group::JoinGroupCommand concurrent_snapshot_join{kUser3,UniqueId("snapshot-join3-"),snapshot_gid};
        const auto joined_after_snapshot=adapter.JoinGroup(concurrent_snapshot_join);
        Expect(joined_after_snapshot.Accepted(),"concurrent member join commits outside frozen snapshot");

        tinyimx::MySqlQueryResult frozen_rows;
        const bool frozen_ok=snapshot_connection->Query(recipient_count_sql,&frozen_rows) &&
            frozen_rows.rows.size()==1 && frozen_rows.rows[0].size()==1 && frozen_rows.rows[0][0]=="1";
        Expect(frozen_ok,"M17-B2 frozen recipient snapshot excludes join-after-snapshot member");
        Expect(snapshot_connection->Commit(),"M17-B2 consistent-read transaction commits read-only");
        Expect(CountRows(&pool,"im_group_members","group_id="+std::to_string(snapshot_gid)+
                         " AND status=1 AND user_id<>"+std::to_string(kOwner))==2,
               "fresh read observes join-after-snapshot member after snapshot closes");
    }

    CleanupGroup(&pool,gid); CleanupGroup(&pool,capacity_gid); CleanupGroup(&pool,snapshot_gid);
    pool.Shutdown(); tinyimx::Logger::Instance().Shutdown();
    std::cout<<"========================================================================\n";
    if(g_failed==0){std::cout<<"[PASS] M17-A2 Membership Repository integration tests\n"; return 0;}
    std::cerr<<"[FAIL] M17-A2 Membership Repository integration tests, failed="<<g_failed<<'\n'; return 1;
}
