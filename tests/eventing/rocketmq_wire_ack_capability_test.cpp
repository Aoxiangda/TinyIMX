#include "services/eventing/rocketmq/OptionalWireAckBatch.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace mq = ROCKETMQ_NAMESPACE;
namespace d = tinyimx::eventing::rocketmq_transport::detail;
namespace {
unsigned checks = 0, calls = 0;
int behavior = 0;
void Require(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
    ++checks;
}
// ABI fixture only: ignores its consumer argument; never constructs a client.
int FakeAck(mq::SimpleConsumer*, const std::vector<mq::MessageConstSharedPtr>* input,
            std::vector<std::error_code>* errors) {
    ++calls;
    errors->assign(input->size(), {});
    if (behavior == 1) return 2;
    if (behavior == 2) { errors->pop_back(); return 0; }
    if (behavior == 3) { errors->push_back({}); return 0; }
    if (behavior == 4) throw std::runtime_error("fixture submission exception");
    if (behavior == 5) (*errors)[2] = std::make_error_code(std::errc::permission_denied);
    return 0;
}
void FakeStats(mq::TinyIMXBatchAckStatsV1*) {}
bool AllFailed(const std::vector<std::error_code>& errors, std::size_t size) {
    return errors.size() == size && std::all_of(errors.begin(), errors.end(),
        [](const auto& e) { return bool(e); });
}
}

int main() try {
    Require(d::ParseWireAckMode(nullptr) == d::WireAckMode::Disabled, "default OFF");
    Require(d::ParseWireAckMode("0") == d::WireAckMode::Disabled, "explicit OFF");
    Require(d::ParseWireAckMode("1") == d::WireAckMode::Enabled, "explicit ON");
    for (const auto* value : {"", "true", "2", " 1", "1 ", "01"})
        Require(d::ParseWireAckMode(value) == d::WireAckMode::Invalid, "invalid flag accepted");

    mq::TinyIMXBatchAckApiV1 good{mq::kTinyIMXBatchAckMagic, 1, sizeof(good),
        _GLIBCXX_USE_CXX11_ABI, 256, sizeof(mq::SimpleConsumer),
        sizeof(mq::MessageConstSharedPtr), FakeAck, FakeStats};
    Require(d::CompatibleWireAckApi(&good), "compatible ABI rejected");
    Require(!d::CompatibleWireAckApi(nullptr), "missing capability accepted");
    for (int field = 0; field < 9; ++field) {
        auto bad = good;
        switch (field) {
        case 0: ++bad.magic; break;
        case 1: ++bad.version; break;
        case 2: --bad.structure_bytes; break;
        case 3: bad.gnu_cxx11_abi = 1 - bad.gnu_cxx11_abi; break;
        case 4: --bad.maximum_entries; break;
        case 5: ++bad.consumer_bytes; break;
        case 6: ++bad.message_pointer_bytes; break;
        case 7: bad.ack_batch = nullptr; break;
        case 8: bad.read_stats = nullptr; break;
        }
        Require(!d::CompatibleWireAckApi(&bad), "incompatible ABI accepted");
    }
    auto message = mq::Message::newBuilder().withTopic("owned-fixture").withBody("body").build();
    std::vector<mq::MessageConstSharedPtr> handles(8,
        mq::MessageConstSharedPtr(std::move(message)));
    Require(d::InvokeWireAckBatch(&good, nullptr, {}).empty() && calls == 0, "empty submitted");
    Require(AllFailed(d::InvokeWireAckBatch(&good, nullptr,
        std::vector<mq::MessageConstSharedPtr>(257)), 257) && calls == 0, "oversized submitted");
    Require(AllFailed(d::InvokeWireAckBatch(nullptr, nullptr, handles), 8) && calls == 0,
        "missing capability silently used");
    auto errors = d::InvokeWireAckBatch(&good, nullptr, handles);
    Require(errors.size() == 8 && std::none_of(errors.begin(), errors.end(),
        [](const auto& e) { return bool(e); }) && calls == 1, "valid ABI call failed");
    for (behavior = 1; behavior <= 4; ++behavior) {
        const auto before = calls;
        Require(AllFailed(d::InvokeWireAckBatch(&good, nullptr, handles), 8) && calls == before + 1,
            "bad API result accepted or silently retried");
    }
    behavior = 5;
    errors = d::InvokeWireAckBatch(&good, nullptr, handles);
    Require(errors[2] == std::errc::permission_denied, "per-entry status lost");
    Require(std::count_if(errors.begin(), errors.end(), [](const auto& e) { return !e; }) == 7,
        "one failure corrupted unrelated results");
    behavior = 0;
    handles[3].reset();
    errors = d::InvokeWireAckBatch(&good, nullptr, handles);
    Require(errors[3] == std::errc::invalid_argument, "null input falsely ACKed");
    Require(std::count_if(errors.begin(), errors.end(), [](const auto& e) { return !e; }) == 7,
        "null input corrupted other positions");
    std::cout << "WIRE_ACK_CAPABILITY_CONTRACT_PASS checks=" << checks << " no_network=true\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "WIRE_ACK_CAPABILITY_CONTRACT_FAIL " << error.what() << '\n';
    return 1;
}
