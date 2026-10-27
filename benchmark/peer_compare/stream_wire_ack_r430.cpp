// Finite concurrent transport qualification. No IM capacity or latency claim.
#include <rocketmq/Configuration.h>
#include <rocketmq/Producer.h>
#include <rocketmq/SimpleConsumer.h>
#include <rocketmq/Logger.h>
#include "TinyIMXBatchAckApi.h"
#include <nlohmann/json.hpp>
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

namespace mq = ROCKETMQ_NAMESPACE;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
constexpr unsigned kWarmup = 128, kSteady = 1024, kMessages = kWarmup + kSteady, kRate = 1000, kWorkers = 4;
void Require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
std::string Body(unsigned i) {
    auto s = std::string("perf430:") + std::to_string(i) + ":";
    s.resize(128, 'x'); return s;
}

int main(int argc, char** argv) try {
    if (argc != 6 || std::string(argv[2]) != "--owned-stream430") return 2;
    const std::string cohort = argv[3], mode = argv[5];
    if (cohort != "off1" && cohort != "onn1" && cohort != "onn2" && cohort != "off2") return 2;
    Require(mode == "--validate" || mode == "--run", "invalid mode");
    nlohmann::json config; std::ifstream file(argv[1]); file >> config;
    const auto topic = config.at("rocketmq").at("message_topic").get<std::string>();
    const auto group = config.at("unread_projection").at("consumer_group").get<std::string>();
    Require(topic == "codex_perf430_events_20261021_r1_" + cohort &&
            group == "codex_perf430_consumer_20261021_r1_" + cohort, "unowned namespace");
    const auto& rc = config.at("rocketmq");
    Require(!rc.value("tls", false) && rc.value("access_key", "").empty() && rc.value("access_secret", "").empty(), "fixture requires existing no-TLS/no-auth template");
    Require(config.at("unread_projection").at("enable") == false, "transport-only fixture");
    if (mode == "--validate") { std::cout << "STREAM430_CONFIG_VALIDATED_NO_CLIENT\n"; return 0; }
    using Getter = const mq::TinyIMXBatchAckApiV1* (*)() noexcept;
    auto getter = reinterpret_cast<Getter>(dlsym(RTLD_DEFAULT, "tinyimx_rocketmq_batch_ack_api_v1"));
    Dl_info info{};
    Require(getter && dladdr(reinterpret_cast<void*>(getter), &info) &&
            std::filesystem::canonical(info.dli_fname) == std::filesystem::canonical(argv[4]), "wrong SDK loaded");
    const auto* api = getter();
    Require(api && api->magic == mq::kTinyIMXBatchAckMagic && api->version == 1 &&
            api->structure_bytes == sizeof(*api) && api->gnu_cxx11_abi == _GLIBCXX_USE_CXX11_ABI &&
            api->maximum_entries == 256 && api->consumer_bytes == sizeof(mq::SimpleConsumer) &&
            api->message_pointer_bytes == sizeof(mq::MessageConstSharedPtr) && api->ack_batch && api->read_stats, "SDK ABI differs");
    const auto* flag = std::getenv("TINYIMX_ROCKETMQ_LAZY_DEBUG_ENABLE");
    Require(flag && std::string(flag) == "0", "logging factor must stay OFF");
    mq::getLogger().setLevel(mq::Level::Info); mq::getLogger().setConsoleLevel(mq::Level::Warn);
    auto cfg = mq::Configuration::newBuilder().withEndpoints(rc.at("endpoint").get<std::string>())
        .withRequestTimeout(10000ms).withSsl(false).build();
    auto consumer = mq::SimpleConsumer::newBuilder().withGroup(group).withConfiguration(cfg)
        .subscribe(topic, mq::FilterExpression("perf430.stream.v1")).withAwaitDuration(5000ms).build();
    auto producer = mq::Producer::newBuilder().withConfiguration(cfg).withTopics({topic}).build();
    mq::TinyIMXBatchAckStatsV1 initial{}, first{}, last{}; api->read_stats(&initial);
    std::mutex phase_mutex; std::condition_variable_any phase_cv; bool warmed = false; Clock::time_point steady_began;
    std::array<std::string, kMessages> sent_ids, received_ids;
    std::array<unsigned, kMessages> deliveries{}, completions{};
    std::array<double, kMessages> send_lateness_ms{};
    std::atomic<unsigned> next{0}; std::atomic<bool> failed{false};
    std::mutex error_mutex; std::string sender_error;
    const auto began = Clock::now() + 100ms;
    std::vector<std::jthread> workers;
    for (unsigned worker = 0; worker < kWorkers; ++worker) workers.emplace_back([&](std::stop_token stop) {
        try {
            for (;;) {
                const auto i = next.fetch_add(1); if (i >= kMessages || failed) break;
                auto due = began + std::chrono::microseconds(1000000ULL * i / kRate);
                if (i >= kWarmup) {
                    std::unique_lock lock(phase_mutex);
                    if (!phase_cv.wait(lock, stop, [&] { return warmed; })) break;
                    due = steady_began + std::chrono::microseconds(1000000ULL * (i-kWarmup) / kRate);
                }
                std::this_thread::sleep_until(due);
                send_lateness_ms[i] = std::chrono::duration<double, std::milli>(Clock::now() - due).count();
                auto message = mq::Message::newBuilder().withTopic(topic).withTag("perf430.stream.v1")
                    .withKeys({"perf430-" + cohort + "-" + std::to_string(i)}).withBody(Body(i)).build();
                std::error_code ec; const auto receipt = producer.send(std::move(message), ec);
                Require(!ec, "stream send failed"); sent_ids[i] = receipt.message_id;
                Require(!sent_ids[i].empty(), "empty published ID");
            }
        } catch (const std::exception& e) { std::lock_guard lock(error_mutex); sender_error = e.what(); failed = true; }
    });
    std::map<std::size_t, unsigned> histogram;
    unsigned complete = 0, redelivered = 0, intentionally_unacked = kMessages;
    std::uint64_t received = 0, batch_calls = 0;
    const auto deadline = Clock::now() + 100s;
    while (complete < kMessages && Clock::now() < deadline && !failed) {
        std::error_code ec; std::vector<mq::MessageConstSharedPtr> messages;
        consumer.receive(16, 10000ms, ec, messages); Require(!ec, "stream receive failed");
        if (messages.empty()) continue;
        Require(messages.size() <= 16, "receive exceeded requested limit");
        ++histogram[messages.size()]; received += messages.size();
        std::vector<unsigned> indices;
        for (const auto& message : messages) {
            Require(message && message->topic() == topic && message->tag() == "perf430.stream.v1", "foreign message");
            const auto& body = message->body(); Require(body.starts_with("perf430:"), "invalid stream body");
            const auto end = body.find(':', 8); Require(end != std::string::npos, "invalid stream index");
            const auto index = static_cast<unsigned>(std::stoul(body.substr(8, end - 8)));
            Require(index < kMessages && body == Body(index), "body mismatch");
            const auto key = "perf430-" + cohort + "-" + std::to_string(index);
            Require(std::find(message->keys().begin(), message->keys().end(), key) != message->keys().end(), "key mismatch");
            Require(!completions[index], "successfully ACKed message redelivered");
            Require(received_ids[index].empty() || received_ids[index] == message->id(), "redelivery ID changed");
            received_ids[index] = message->id(); ++deliveries[index]; indices.push_back(index);
        }
        const bool inject = intentionally_unacked == kMessages && indices.front() >= kWarmup;
        if (inject) { intentionally_unacked = indices.front(); messages.front().reset(); }
        std::vector<std::error_code> errors;
        Require(api->ack_batch(&consumer, &messages, &errors) == 0 && errors.size() == messages.size(), "ACK result positions lost");
        ++batch_calls;
        for (std::size_t j = 0; j < errors.size(); ++j) {
            if (inject && !j) { Require(bool(errors[j]), "missing handle falsely ACKed"); continue; }
            Require(!errors[j], "valid stream ACK failed");
            Require(++completions[indices[j]] == 1, "duplicate completion"); ++complete;
            if (indices[j] == intentionally_unacked) ++redelivered;
        }
        if (complete == kWarmup && !warmed) {
            api->read_stats(&first);
            Require(first.submitted_entries-initial.submitted_entries == kWarmup &&
                    first.rpc_submissions-initial.rpc_submissions == first.rpc_terminals-initial.rpc_terminals, "warmup did not drain");
            histogram.clear(); received = 0; batch_calls = 0;
            { std::lock_guard lock(phase_mutex); steady_began = Clock::now()+100ms; warmed = true; }
            phase_cv.notify_all();
        }
    }
    workers.clear();  // Join all producers before checking their result arrays.
    Require(!failed, sender_error.c_str());
    Require(complete == kMessages && redelivered == 1 && received == kSteady + 1, "stream did not recover exactly one skipped ACK");
    for (unsigned i = 0; i < kMessages; ++i) {
        Require(sent_ids[i] == received_ids[i] && completions[i] == 1 &&
                deliveries[i] == (i == intentionally_unacked ? 2U : 1U), "published/received identity or recovery mismatch");
    }
    std::this_thread::sleep_for(11s);
    std::error_code ec; std::vector<mq::MessageConstSharedPtr> empty;
    consumer.receive(1, 10000ms, ec, empty); Require(!ec && empty.empty(), "ACKed data returned after invisibility period");
    api->read_stats(&last);
    Require(last.submitted_entries - first.submitted_entries == kSteady &&
            last.rpc_submissions - first.rpc_submissions == last.rpc_terminals - first.rpc_terminals, "SDK final entries or terminals differ");
    nlohmann::json hist = nlohmann::json::object();
    for (const auto& [n, count] : histogram) hist[std::to_string(n)] = count;
    std::vector<double> lateness(send_lateness_ms.begin()+kWarmup,send_lateness_ms.end());
    std::sort(lateness.begin(),lateness.end());
    const double late_mean=std::accumulate(lateness.begin(),lateness.end(),0.0)/kSteady;
    const double late_p99=lateness[(kSteady*99+99)/100-1], late_max=lateness.back();
    nlohmann::json result = {{"cohort",cohort},{"messages",kSteady},{"received",received},{"completed",complete-kWarmup},
        {"warmup_messages",kWarmup},{"warmup_drained_before_steady",warmed},
        {"total_messages",kMessages},{"total_sdk_entries",last.submitted_entries-initial.submitted_entries},
        {"total_sdk_rpc_submissions",last.rpc_submissions-initial.rpc_submissions},
        {"send_schedule_qualified",late_mean<=5 && late_p99<=10 && late_max<=25},
        {"redelivered",redelivered},{"batch_calls",batch_calls},{"receive_batch_histogram",hist},
        {"receive_mean_batch",double(received)/batch_calls},{"sdk_entries",last.submitted_entries-first.submitted_entries},
        {"sdk_rpc_submissions",last.rpc_submissions-first.rpc_submissions},{"sdk_rpc_terminals",last.rpc_terminals-first.rpc_terminals},
        {"planned_rate_per_s",kRate},{"producer_workers",kWorkers},{"payload_bytes",128},
        {"send_start_lateness_ms_max",late_max},{"send_start_lateness_ms_p99",late_p99},
        {"send_start_lateness_ms_mean",late_mean},
        {"prefilled_before_receive",false},{"artificial_processing_delay",false},{"formal_performance",false},
        {"scope","Concurrent SDK transport only; no Gateway, SQL/native authority, Redis projector or Windows load client. Batch formation and exact receipt recovery only."}};
    std::cout << "STREAM430_TRANSPORT_PASS " << result.dump() << std::endl;
    return 0;
} catch (const std::exception& e) { std::cerr << "STREAM430_FAIL " << e.what() << std::endl; return 1; }
