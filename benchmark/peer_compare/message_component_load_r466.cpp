// Message-only CPU diagnosis. Not a full TCP IM or capacity benchmark.
#include "tinyimx/message/v1/message_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

namespace v1 = tinyimx::message::v1;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr unsigned messages = 60000, workers = 32, users = 20000;
std::string Body(unsigned i) {
    auto s = "component-r456-body:" + std::to_string(i) + ":";
    s.resize(128, 'x');
    return s;
}

int main(int argc, char** argv) try {
    if (argc != 4) return 2;
    const std::string target = argv[1];
    const std::filesystem::path out = argv[2], marker = argv[3];
    if (out.parent_path() != "/home/jackson7/projects/tinyimx-interview-20261017" ||
        (out.filename() != "perf461-component-load-r1" && out.filename() != "perf466-component-load-r1" && out.filename() != "perf467-component-load-r1" && out.filename() != "perf468-component-load-r1") || std::filesystem::exists(out / "client-records.jsonl") ||
        marker != out / "start-load" || std::filesystem::exists(marker)) return 2;
    std::ofstream records(out / "client-records.jsonl");
    if (!records) throw std::runtime_error("component record open failed");
    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    if (!channel->WaitForConnected(std::chrono::system_clock::now() + 5s))
        throw std::runtime_error("component channel connection failed");
    std::vector<std::unique_ptr<v1::MessageService::Stub>> stubs;
    for (unsigned w = 0; w < workers; ++w) stubs.push_back(v1::MessageService::NewStub(channel));
    std::cout << "READY_COMPONENT456" << std::endl;
    const auto end_wait = Clock::now() + 20s;
    while (!std::filesystem::exists(marker)) {
        if (Clock::now() >= end_wait) throw std::runtime_error("component barrier timeout");
        std::this_thread::sleep_for(10ms);
    }
    std::ifstream in(marker);
    std::int64_t target_ns = 0;
    in >> target_ns;
    if (!in || target_ns <= 0) throw std::runtime_error("invalid monotonic barrier");
    const Clock::time_point start{std::chrono::nanoseconds(target_ns)};
    if (start < Clock::now() || start > Clock::now() + 10s) throw std::runtime_error("barrier outside future bound");
    std::vector<std::uint64_t> mids(messages);
    std::atomic<unsigned> next{0}, created{0}, confirmed{0}, errors{0};
    std::atomic<std::int64_t> max_lag_ns{0};
    std::mutex output;
    std::vector<std::jthread> threads;
    for (unsigned w = 0; w < workers; ++w) threads.emplace_back([&, w] {
        for (;;) {
            const auto i = next.fetch_add(1);
            if (i >= messages) return;
            const auto scheduled = start + std::chrono::milliseconds(i);
            std::this_thread::sleep_until(scheduled);
            const auto lag = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - scheduled).count();
            auto observed = max_lag_ns.load();
            while (observed < lag && !max_lag_ns.compare_exchange_weak(observed, lag)) {}
            const std::uint64_t from = 500001 + (i % users), to = 500001 + ((i % users) ^ 1U);
            const auto cid = "component-r456-" + std::to_string(i);
            v1::PersistPrivateMessageRequest q;
            q.set_from_user_id(from); q.set_to_user_id(to); q.set_client_message_id(cid);
            q.set_message_type(1); q.set_content(Body(i));
            v1::PersistPrivateMessageResponse response;
            grpc::ClientContext context;
            context.set_deadline(std::chrono::system_clock::now() + 5s);
            const auto status = stubs[w]->PersistPrivateMessage(&context, q, &response);
            if (!status.ok() || response.result() != v1::PERSIST_PRIVATE_MESSAGE_RESULT_CREATED ||
                !response.message_id() || response.record().content() != Body(i)) {
                ++errors;
                std::lock_guard hold(output);
                records << nlohmann::json{{"i",i},{"error","persist"},{"grpc",status.error_code()},{"result",response.result()}}.dump() << '\n';
                continue;
            }
            ++created; mids[i] = response.message_id();
            v1::ConfirmReceiverBatchRequest confirmation;
            confirmation.set_receiver_user_id(to); confirmation.add_message_ids(response.message_id());
            v1::MessageMutationResponse mutation;
            grpc::ClientContext ack;
            ack.set_deadline(std::chrono::system_clock::now() + 5s);
            const auto confirmed_status = stubs[w]->ConfirmReceiverBatch(&ack, confirmation, &mutation);
            const bool ok = confirmed_status.ok() && mutation.affected_rows() == 1;
            if (ok) ++confirmed; else ++errors;
            std::lock_guard hold(output);
            records << nlohmann::json{{"i",i},{"mid",response.message_id()},{"cid",cid},{"from",from},{"to",to},{"receiver_confirmed",ok},{"start_lag_ns",lag}}.dump() << '\n';
        }
    });
    threads.clear(); records.flush();
    const std::set<std::uint64_t> unique(mids.begin(), mids.end());
    const bool passed = created == messages && confirmed == messages && errors == 0 &&
                        unique.size() == messages && !unique.contains(0) && records.good();
    nlohmann::json result{{"status",passed?"COMPONENT60000_CREATED_AND_CONFIRMED":"COMPONENT_FAILURE"},
        {"messages",messages},{"created",created.load()},{"confirmed",confirmed.load()},{"errors",errors.load()},
        {"logical_users",users},{"workers",workers},{"target_rate",1000},{"payload_bytes",128},
        {"start_guest_mono_ns",target_ns},{"end_guest_mono_ns",std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()},
        {"max_start_lag_ns",max_lag_ns.load()},{"unique_mids",unique.size()},
        {"full_tcp_im_benchmark",false},{"formal_performance_eligible",false},
        {"scope","Linux component client -> Message RPC persistence + receiver confirmation; MQ and snapshot projector remain. No20K authenticated TCP sessions, Gateway permissions or end-to-end delivery claim."}};
    std::ofstream summary(out / "client-summary.json"); summary << result.dump(2) << '\n';
    std::cout << "COMPONENT456_FINAL " << result.dump() << std::endl;
    return passed ? 0 : 1;
} catch (const std::exception& e) {
    std::cerr << "COMPONENT456_ERROR " << e.what() << std::endl;
    return 1;
}
