#include "services/group/application/GroupEventFactory.h"
#include "services/group/repository/GroupRepositoryAdapter.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Check(bool value,const char* label) {
    if (!value) throw std::runtime_error(label);
    std::cout << "PASS " << label << '\n';
}
}
int main() {
    try {
        using namespace tinyimx::group;
        GroupView group;group.group_id=7001;group.owner_user_id=10001;group.version=3;group.member_version=4;
        GroupMemberView member;member.group_id=7001;member.user_id=10002;member.membership_epoch=2;
        const std::vector<GroupOutboxEventSpec> events={
            GroupEventFactory::GroupCreated(group),GroupEventFactory::GroupUpdated(group),GroupEventFactory::GroupDisbanded(group),
            GroupEventFactory::MemberJoined(group,member,10001,false),GroupEventFactory::MemberJoined(group,member,10001,true),
            GroupEventFactory::MemberLeft(group,member,10002),GroupEventFactory::MemberKicked(group,member,10001),
            GroupEventFactory::MemberRoleChanged(group,member,10001),GroupEventFactory::MemberMuteChanged(group,member,10001),
            GroupEventFactory::OwnershipTransferred(group,member,10001)};
        const std::string topic="codex_perf118_group_domain";
        for (const auto& original:events) {
            Check(original.topic=="tinyimx-message-events","legacy factory default remains compatible");
            const auto routed=GroupEventFactory::WithTopic(original,topic);
            Check(routed.topic==topic&&original.topic=="tinyimx-message-events","deployment routing is explicit and leaves original value unchanged");
            Check(routed.event.schema_version==original.event.schema_version&&routed.event.event_id==original.event.event_id&&
                  routed.event.event_type==original.event.event_type&&routed.event.aggregate_type==original.event.aggregate_type&&
                  routed.event.aggregate_id==original.event.aggregate_id&&routed.event.producer_service==original.event.producer_service&&
                  routed.event.occurred_at==original.event.occurred_at&&routed.event.payload==original.event.payload&&
                  routed.tag==original.tag&&routed.message_key==original.message_key,"routing preserves complete event identity payload and dedup key");
        }
        for (const auto& topic_ok:std::vector<std::string>{"a",std::string(128,'x'),"aZ09_-"})
            Check(GroupEventFactory::IsValidTopic(topic_ok),"supported topic boundary accepted");
        for (const auto& bad:std::vector<std::string>{"",std::string(129,'x'),"a b","a/b","a.b","a'",std::string("a\0b",3),"主题"}) {
            Check(!GroupEventFactory::IsValidTopic(bad),"invalid routing topic rejected");
            bool rejected=false;
            try {(void)GroupEventFactory::WithTopic(events.front(),bad);}catch(const std::invalid_argument&){rejected=true;}
            Check(rejected,"factory refuses invalid routing before persistence");
            rejected=false;
            try {GroupRepositoryAdapter adapter(nullptr,nullptr,nullptr,nullptr,bad);}catch(const std::invalid_argument&){rejected=true;}
            Check(rejected,"adapter validates configuration at construction before transactions");
        }
        GroupRepositoryAdapter legacy(nullptr,nullptr,nullptr,nullptr);
        Check(true,"existing four-argument adapter construction remains compatible");
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
