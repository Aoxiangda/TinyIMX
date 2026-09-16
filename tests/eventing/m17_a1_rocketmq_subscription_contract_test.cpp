#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"
#include "services/projection/unread/UnreadProjector.h"

#include <iostream>
#include <string>

namespace {

int Fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using tinyimx::eventing::rocketmq_transport::RocketMQSimpleConsumerOptions;

    RocketMQSimpleConsumerOptions defaults;
    if (defaults.filter_expression != "*") {
        return Fail("generic RocketMQ consumer lost backward-compatible all-tag default");
    }

    const std::string unread_filter =
        tinyimx::projection::unread::kUnreadProjectionTagFilter;
    if (unread_filter != "message.created.v1||dialog.read_advanced.v1") {
        return Fail("unread projector tag filter changed unexpectedly");
    }
    if (unread_filter.find("group.") != std::string::npos ||
        unread_filter.find('*') != std::string::npos) {
        return Fail("unread projector filter would admit M17 Group domain events");
    }

    RocketMQSimpleConsumerOptions unread_options;
    unread_options.filter_expression = unread_filter;
    if (unread_options.filter_expression != unread_filter) {
        return Fail("RocketMQ consumer options did not retain explicit tag filter");
    }

    std::cout << "PASS: M17-A1 RocketMQ subscription compatibility contract\n";
    return 0;
}
