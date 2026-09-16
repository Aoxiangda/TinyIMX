#include "services/group/application/GroupApplicationService.h"
#include "services/group/server/GroupServiceServer.h"
#include "services/group/service/GroupServiceImpl.h"
#include "tinyimx/group/v1/group_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakeGroupRepositoryPort final : public tinyimx::group::GroupRepositoryPort {
public:
    FakeGroupRepositoryPort() {
        stored.group_id=9001; stored.name="core-team"; stored.owner_user_id=10001;
        stored.status=tinyimx::group::GroupStatus::kActive;
        stored.join_policy=tinyimx::group::GroupJoinPolicy::kOpen;
        stored.max_members=500; stored.version=1; stored.member_version=1;
        tinyimx::group::GroupMemberView owner;
        owner.group_id=9001; owner.user_id=10001; owner.role=tinyimx::group::GroupRole::kOwner;
        owner.status=tinyimx::group::GroupMemberStatus::kActive; owner.membership_epoch=1;
        members.push_back(owner);
    }

    tinyimx::group::GroupRepositoryMutationResult Success(const std::string& msg) {
        tinyimx::group::GroupRepositoryMutationResult r;
        r.status=tinyimx::group::GroupApplicationStatus::kSucceeded;
        r.outcome=tinyimx::group::GroupMutationOutcome::kApplied;
        r.group=stored; r.message=msg; return r;
    }

    tinyimx::group::GroupRepositoryMutationResult CreateGroup(const tinyimx::group::CreateGroupCommand& c) override {
        stored.name=c.name; stored.description=c.description; stored.avatar_url=c.avatar_url;
        stored.owner_user_id=c.actor_user_id; stored.join_policy=c.join_policy; stored.max_members=c.max_members;
        return Success("created");
    }
    tinyimx::group::GroupRepositoryGetResult GetGroup(std::uint64_t a,std::uint64_t g) override {
        tinyimx::group::GroupRepositoryGetResult r;
        const auto it=FindMember(a);
        if(g!=stored.group_id || it==members.end() || it->status!=tinyimx::group::GroupMemberStatus::kActive){
            r.status=tinyimx::group::GroupApplicationStatus::kPermissionDenied; r.message="not visible"; return r;
        }
        r.status=tinyimx::group::GroupApplicationStatus::kSucceeded; r.group=stored; return r;
    }
    tinyimx::group::GroupRepositoryMutationResult UpdateGroup(const tinyimx::group::UpdateGroupCommand& c) override {
        if(c.expected_version!=stored.version){ auto r=Success("bad"); r.status=tinyimx::group::GroupApplicationStatus::kAborted; return r; }
        if(c.name) stored.name=*c.name; ++stored.version; return Success("updated");
    }
    tinyimx::group::GroupRepositoryMutationResult DisbandGroup(const tinyimx::group::DisbandGroupCommand& c) override {
        if(c.expected_version!=stored.version){ auto r=Success("bad"); r.status=tinyimx::group::GroupApplicationStatus::kAborted; return r; }
        stored.status=tinyimx::group::GroupStatus::kDisbanded; ++stored.version; ++stored.member_version;
        stored.disbanded_by_user_id=c.actor_user_id; return Success("disbanded");
    }
    tinyimx::group::GroupRepositoryMutationResult JoinGroup(const tinyimx::group::JoinGroupCommand& c) override {
        auto it=FindMember(c.actor_user_id);
        if(it==members.end()){ tinyimx::group::GroupMemberView m; m.group_id=stored.group_id; m.user_id=c.actor_user_id; m.role=tinyimx::group::GroupRole::kMember; m.status=tinyimx::group::GroupMemberStatus::kActive; m.membership_epoch=1; members.push_back(m); }
        else { it->status=tinyimx::group::GroupMemberStatus::kActive; ++it->membership_epoch; it->role=tinyimx::group::GroupRole::kMember; }
        ++stored.version; ++stored.member_version; return Success("joined");
    }
    tinyimx::group::GroupRepositoryMutationResult LeaveGroup(const tinyimx::group::LeaveGroupCommand& c) override {
        auto it=FindMember(c.actor_user_id); if(it==members.end() || it->role==tinyimx::group::GroupRole::kOwner){ auto r=Success("bad"); r.status=tinyimx::group::GroupApplicationStatus::kFailedPrecondition; return r; }
        it->status=tinyimx::group::GroupMemberStatus::kLeft; ++stored.version; ++stored.member_version; return Success("left");
    }
    tinyimx::group::GroupRepositoryMutationResult InviteMember(const tinyimx::group::InviteMemberCommand& c) override {
        auto it=FindMember(c.target_user_id);
        if(it==members.end()){ tinyimx::group::GroupMemberView m; m.group_id=stored.group_id; m.user_id=c.target_user_id; m.role=tinyimx::group::GroupRole::kMember; m.status=tinyimx::group::GroupMemberStatus::kActive; m.membership_epoch=1; members.push_back(m); }
        else { it->status=tinyimx::group::GroupMemberStatus::kActive; ++it->membership_epoch; it->role=tinyimx::group::GroupRole::kMember; it->muted_until.clear(); }
        ++stored.version; ++stored.member_version; return Success("invited");
    }
    tinyimx::group::GroupRepositoryMutationResult KickMember(const tinyimx::group::KickMemberCommand& c) override {
        auto it=FindMember(c.target_user_id); if(it==members.end()){ auto r=Success("bad"); r.status=tinyimx::group::GroupApplicationStatus::kNotFound; return r; }
        it->status=tinyimx::group::GroupMemberStatus::kKicked; ++stored.version; ++stored.member_version; return Success("kicked");
    }
    tinyimx::group::GroupRepositoryMutationResult SetMemberRole(const tinyimx::group::SetMemberRoleCommand& c) override {
        auto it=FindMember(c.target_user_id); if(it==members.end()){ auto r=Success("bad"); r.status=tinyimx::group::GroupApplicationStatus::kNotFound; return r; }
        it->role=c.role; ++stored.version; ++stored.member_version; return Success("role");
    }
    tinyimx::group::GroupRepositoryMutationResult SetMemberMute(const tinyimx::group::SetMemberMuteCommand& c) override {
        auto it=FindMember(c.target_user_id); if(it==members.end()){ auto r=Success("bad"); r.status=tinyimx::group::GroupApplicationStatus::kNotFound; return r; }
        it->muted_until=c.muted_until; ++stored.version; ++stored.member_version; return Success("mute");
    }
    tinyimx::group::GroupRepositoryMutationResult TransferOwnership(const tinyimx::group::TransferOwnershipCommand& c) override {
        auto old=FindMember(c.actor_user_id); auto target=FindMember(c.target_user_id);
        if(old==members.end() || target==members.end()){ auto r=Success("bad"); r.status=tinyimx::group::GroupApplicationStatus::kNotFound; return r; }
        old->role=tinyimx::group::GroupRole::kAdmin; target->role=tinyimx::group::GroupRole::kOwner;
        stored.owner_user_id=c.target_user_id; ++stored.version; ++stored.member_version; return Success("transferred");
    }
    tinyimx::group::GroupMemberListResult ListGroupMembers(const tinyimx::group::ListGroupMembersQuery& q) override {
        tinyimx::group::GroupMemberListResult r; r.status=tinyimx::group::GroupApplicationStatus::kSucceeded;
        for(const auto& m:members) if(m.status==tinyimx::group::GroupMemberStatus::kActive && m.user_id>q.after_user_id) r.members.push_back(m);
        std::sort(r.members.begin(),r.members.end(),[](const auto& a,const auto& b){return a.user_id<b.user_id;});
        if(r.members.size()>q.limit){ r.has_more=true; r.members.resize(q.limit); }
        return r;
    }
    tinyimx::group::GroupListResult ListMyGroups(const tinyimx::group::ListMyGroupsQuery& q) override {
        tinyimx::group::GroupListResult r; r.status=tinyimx::group::GroupApplicationStatus::kSucceeded;
        auto it=FindMember(q.actor_user_id); if(it!=members.end() && it->status==tinyimx::group::GroupMemberStatus::kActive) r.groups.push_back(stored); return r;
    }
    tinyimx::group::GroupSendPermissionResult CheckGroupSendPermission(std::uint64_t a,std::uint64_t) override {
        tinyimx::group::GroupSendPermissionResult r; r.status=tinyimx::group::GroupApplicationStatus::kSucceeded; r.member_version=stored.member_version;
        auto it=FindMember(a); if(it==members.end()) { r.message="not member"; return r; }
        r.role=it->role; r.membership_epoch=it->membership_epoch;
        r.allowed=it->status==tinyimx::group::GroupMemberStatus::kActive && it->muted_until.empty() && stored.status==tinyimx::group::GroupStatus::kActive;
        r.message=r.allowed?"allowed":"denied"; return r;
    }

    std::vector<tinyimx::group::GroupMemberView>::iterator FindMember(std::uint64_t id){
        return std::find_if(members.begin(),members.end(),[&](const auto& m){return m.user_id==id;});
    }

    tinyimx::group::GroupView stored;
    std::vector<tinyimx::group::GroupMemberView> members;
};

bool Expect(bool condition,const char* name){ if(condition){std::cout<<"[PASS] "<<name<<'\n'; return true;} std::cerr<<"[FAIL] "<<name<<'\n'; return false; }

bool TestRealGrpcA1A2() {
    FakeGroupRepositoryPort repository;
    tinyimx::group::GroupApplicationService application(&repository);
    tinyimx::group::GroupServiceImpl service_impl(&application);
    tinyimx::group::GroupServiceServer server(&service_impl);
    if(!server.Start("127.0.0.1:0")) return Expect(false,"GroupService.RealGrpcServerStart");
    auto channel=grpc::CreateChannel(server.BoundTarget(),grpc::InsecureChannelCredentials());
    auto stub=tinyimx::group::v1::GroupService::NewStub(channel);
    bool ok=true;

    tinyimx::group::v1::CreateGroupRequest create; create.set_actor_user_id(10001); create.set_client_operation_id("m17-a2-create"); create.set_name("core-team"); create.set_join_policy(tinyimx::group::v1::GROUP_JOIN_POLICY_OPEN); create.set_max_members(500);
    grpc::ClientContext c1; tinyimx::group::v1::GroupMutationResponse r1; auto s1=stub->CreateGroup(&c1,create,&r1);
    ok=Expect(s1.ok() && r1.group().group_id()==9001,"GroupService.CreateGroupGrpcSuccess")&&ok;

    tinyimx::group::v1::JoinGroupRequest join; join.set_actor_user_id(10002); join.set_client_operation_id("join-2"); join.set_group_id(9001);
    grpc::ClientContext c2; tinyimx::group::v1::GroupMutationResponse r2; auto s2=stub->JoinGroup(&c2,join,&r2);
    ok=Expect(s2.ok() && r2.group().member_version()==2,"GroupService.JoinGroupGrpcSuccess")&&ok;

    tinyimx::group::v1::SetMemberRoleRequest role; role.set_actor_user_id(10001); role.set_client_operation_id("role-2"); role.set_group_id(9001); role.set_target_user_id(10002); role.set_role(tinyimx::group::v1::GROUP_ROLE_ADMIN);
    grpc::ClientContext c3; tinyimx::group::v1::GroupMutationResponse r3; auto s3=stub->SetMemberRole(&c3,role,&r3);
    ok=Expect(s3.ok(),"GroupService.SetMemberRoleGrpcSuccess")&&ok;

    tinyimx::group::v1::SetMemberMuteRequest mute; mute.set_actor_user_id(10001); mute.set_client_operation_id("mute-2"); mute.set_group_id(9001); mute.set_target_user_id(10002); mute.set_muted_until("2099-01-01T00:00:00.000Z");
    grpc::ClientContext c4; tinyimx::group::v1::GroupMutationResponse r4; auto s4=stub->SetMemberMute(&c4,mute,&r4);
    ok=Expect(s4.ok(),"GroupService.SetMemberMuteGrpcSuccess")&&ok;

    tinyimx::group::v1::CheckGroupSendPermissionRequest check; check.set_actor_user_id(10002); check.set_group_id(9001);
    grpc::ClientContext c5; tinyimx::group::v1::CheckGroupSendPermissionResponse r5; auto s5=stub->CheckGroupSendPermission(&c5,check,&r5);
    ok=Expect(s5.ok() && !r5.allowed() && r5.role()==tinyimx::group::v1::GROUP_ROLE_ADMIN,"GroupService.CheckSendPermissionMapping")&&ok;

    tinyimx::group::v1::ListGroupMembersRequest list; list.set_actor_user_id(10001); list.set_group_id(9001); list.set_limit(50);
    grpc::ClientContext c6; tinyimx::group::v1::ListGroupMembersResponse r6; auto s6=stub->ListGroupMembers(&c6,list,&r6);
    ok=Expect(s6.ok() && r6.members_size()==2,"GroupService.ListMembersGrpcSuccess")&&ok;

    tinyimx::group::v1::TransferOwnershipRequest transfer; transfer.set_actor_user_id(10001); transfer.set_client_operation_id("transfer-2"); transfer.set_group_id(9001); transfer.set_target_user_id(10002);
    grpc::ClientContext c7; tinyimx::group::v1::GroupMutationResponse r7; auto s7=stub->TransferOwnership(&c7,transfer,&r7);
    ok=Expect(s7.ok() && r7.group().owner_user_id()==10002,"GroupService.TransferOwnershipGrpcSuccess")&&ok;

    tinyimx::group::v1::ListMyGroupsRequest my; my.set_actor_user_id(10002); my.set_limit(50);
    grpc::ClientContext c8; tinyimx::group::v1::ListMyGroupsResponse r8; auto s8=stub->ListMyGroups(&c8,my,&r8);
    ok=Expect(s8.ok() && r8.groups_size()==1,"GroupService.ListMyGroupsGrpcSuccess")&&ok;

    server.Shutdown(); server.Wait(); return ok;
}

}  // namespace

int main(){
    std::cout<<"========== TinyIMX M17-A2 GroupService Integration Tests ==========\n";
    const bool ok=TestRealGrpcA1A2();
    std::cout<<"===================================================================\n";
    return ok?0:1;
}
