#include "services/message/application/MessageEventFactory.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;
void Check(bool ok, const char* text) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << text << '\n';
    failures += !ok;
}
template<class F> bool Invalid(F&& operation) {
    try { operation(); } catch (const std::invalid_argument&) { return true; }
    return false;
}
}
int main() {
    using namespace tinyimx::message;
    const std::string topic = "codex-perf041-topic-r1";
    MessageView p{};p.message_id=123;p.from_user_id=10;p.to_user_id=20;p.message_type=1;
    p.content="body-must-not-enter-event";p.client_message_id="cid-must-not-enter-event";
    GroupMessageView g{};g.message_id=456;g.group_id=30;g.from_user_id=10;g.message_type=1;
    g.membership_epoch=2;g.member_version=3;g.authorized_role=1;
    const auto private_event=MessageEventFactory::MessageCreated(p,topic);
    const auto group_event=MessageEventFactory::GroupMessageCreated(g,topic);
    const auto read_event=MessageEventFactory::DialogReadAdvanced(20,10,1,topic);
    Check(private_event.topic==topic&&group_event.topic==topic&&read_event.topic==topic,"private/group/read use explicit configured topic");
    Check(private_event.event.event_id=="message.created.v1:123"&&group_event.event.event_id=="group_message.created.v1:456","private/group stable IDs unchanged");
    Check(read_event.event.event_type=="dialog.read_advanced.v1"&&read_event.event.aggregate_id=="20:10","read event schema and dialog identity retained");
    Check(!private_event.event.payload.contains("content")&&!private_event.event.payload.contains("client_message_id"),"topic does not leak body or CID into event contract");
    Check(private_event.message_key==private_event.event.event_id&&group_event.message_key==group_event.event.event_id&&read_event.message_key==read_event.event.event_id,"all message keys remain event IDs");
    Check(MessageEventFactory::MessageCreated(p).topic==MessageEventFactory::kDefaultTopic&&MessageEventFactory::GroupMessageCreated(g).topic==MessageEventFactory::kDefaultTopic&&MessageEventFactory::DialogReadAdvanced(20,10,1).topic==MessageEventFactory::kDefaultTopic,"legacy callers retain default topic for all event types");
    Check(MessageEventFactory::IsValidTopic(std::string(128,'a'))&&!MessageEventFactory::IsValidTopic(std::string(129,'a')),"topic length boundary enforced");
    for(const std::string bad: {std::string{},std::string{"bad.topic"},std::string{"bad topic"},std::string{"bad/topic"},std::string{"bad\nname"},std::string(129,'x')}) {
        Check(!MessageEventFactory::IsValidTopic(bad),"invalid topic rejected by startup validation");
        Check(Invalid([&]{(void)MessageEventFactory::MessageCreated(p,bad);})&&Invalid([&]{(void)MessageEventFactory::GroupMessageCreated(g,bad);})&&Invalid([&]{(void)MessageEventFactory::DialogReadAdvanced(20,10,1,bad);}),"invalid topic cannot create any event type");
        Check(Invalid([&]{MessageRepositoryAdapter adapter(nullptr,nullptr,nullptr,bad);}),"adapter refuses invalid topic before any database call");
    }
    return failures ? 1 : 0;
}
