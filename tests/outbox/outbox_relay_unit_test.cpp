#include "services/eventing/EventCodec.h"
#include "services/eventing/EventPublisher.h"
#include "services/outbox/OutboxRelay.h"
#include "services/outbox/OutboxStore.h"
#include "common/config/Config.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace {

class FakeStore final : public tinyimx::outbox::OutboxStore {
public:
    explicit FakeStore(tinyimx::outbox::OutboxRecord record)
        : record_(std::move(record)) {
    }

    tinyimx::outbox::OutboxClaimResult ClaimBatch(
        const std::string& claim_token,
        std::size_t,
        int
    ) override {
        std::lock_guard<std::mutex> lock(mutex_);
        tinyimx::outbox::OutboxClaimResult result;
        result.success = true;
        if (!available_) {
            return result;
        }
        available_ = false;
        record_.locked_by = claim_token;
        result.records.push_back(record_);
        return result;
    }

    tinyimx::outbox::OutboxMutationResult RenewClaim(
        std::uint64_t id,
        const std::string& token,
        int
    ) override {
        return Owned(id, token, "renew");
    }

    tinyimx::outbox::OutboxMutationResult MarkPublished(
        std::uint64_t id,
        const std::string& token
    ) override {
        auto result = Owned(id, token, "published");
        if (result.success) {
            std::lock_guard<std::mutex> lock(mutex_);
            published_ = true;
        }
        return result;
    }

    tinyimx::outbox::OutboxMutationResult MarkRetry(
        std::uint64_t id,
        const std::string& token,
        int retry_after_ms,
        const std::string&
    ) override {
        auto result = Owned(id, token, "retry");
        if (result.success) {
            std::lock_guard<std::mutex> lock(mutex_);
            retry_ = true;
            retry_after_ms_ = retry_after_ms;
        }
        return result;
    }

    tinyimx::outbox::OutboxMutationResult MarkQuarantined(
        std::uint64_t id,
        const std::string& token,
        const std::string&,
        bool publish_attempted
    ) override {
        auto result = Owned(id, token, "quarantine");
        if (result.success) {
            std::lock_guard<std::mutex> lock(mutex_);
            quarantined_ = true;
            quarantine_publish_attempted_ = publish_attempted;
        }
        return result;
    }

    tinyimx::outbox::OutboxMutationResult ReleaseClaim(
        std::uint64_t id,
        const std::string& token
    ) override {
        return Owned(id, token, "release");
    }

    tinyimx::outbox::OutboxCleanupResult CleanupPublished(
        int,
        std::size_t
    ) override {
        tinyimx::outbox::OutboxCleanupResult result;
        result.success = true;
        return result;
    }

    bool published() const { std::lock_guard<std::mutex> lock(mutex_); return published_; }
    bool retry() const { std::lock_guard<std::mutex> lock(mutex_); return retry_; }
    bool quarantined() const { std::lock_guard<std::mutex> lock(mutex_); return quarantined_; }
    bool quarantine_publish_attempted() const { std::lock_guard<std::mutex> lock(mutex_); return quarantine_publish_attempted_; }
    int retry_after_ms() const { std::lock_guard<std::mutex> lock(mutex_); return retry_after_ms_; }

private:
    tinyimx::outbox::OutboxMutationResult Owned(
        std::uint64_t id,
        const std::string& token,
        const std::string& action
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        tinyimx::outbox::OutboxMutationResult result;
        if (id != record_.outbox_id || token != record_.locked_by) {
            result.ownership_lost = true;
            result.message = action + " ownership lost";
            return result;
        }
        result.success = true;
        result.affected_rows = 1;
        return result;
    }

private:
    mutable std::mutex mutex_;
    tinyimx::outbox::OutboxRecord record_;
    bool available_{true};
    bool published_{false};
    bool retry_{false};
    bool quarantined_{false};
    bool quarantine_publish_attempted_{false};
    int retry_after_ms_{0};
};

class FakePublisher final : public tinyimx::eventing::EventPublisher {
public:
    explicit FakePublisher(tinyimx::eventing::PublishStatus status)
        : status_(status) {
    }

    tinyimx::eventing::PublishResult Publish(
        const tinyimx::eventing::PublishMessage& message
    ) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++calls_;
        last_event_id_ = message.event_id;
        tinyimx::eventing::PublishResult result;
        result.status = status_;
        result.broker_message_id = status_ == tinyimx::eventing::PublishStatus::kPublished
            ? "broker-1" : "";
        result.message = status_ == tinyimx::eventing::PublishStatus::kPublished
            ? "ok" : "injected publish failure";
        return result;
    }

    int calls() const { std::lock_guard<std::mutex> lock(mutex_); return calls_; }
    std::string last_event_id() const { std::lock_guard<std::mutex> lock(mutex_); return last_event_id_; }

private:
    tinyimx::eventing::PublishStatus status_;
    int calls_{0};
    std::string last_event_id_;
    mutable std::mutex mutex_;
};

int g_failed = 0;

void Expect(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cerr << "[FAIL] " << name << '\n';
        ++g_failed;
    }
}

tinyimx::outbox::OutboxRecord MakeRecord(bool valid_payload = true, std::uint64_t id = 1) {
    tinyimx::eventing::DomainEvent event;
    event.schema_version = 1;
    event.event_id = "message.created.v1:" + std::to_string(99000 + id);
    event.event_type = "message.created.v1";
    event.aggregate_type = "private_message";
    event.aggregate_id = std::to_string(99000 + id);
    event.producer_service = "message-service";
    event.occurred_at = "2026-09-09T00:00:00.000Z";
    event.payload = {
        {"message_id", 99000 + id},
        {"from_user_id", 10001},
        {"to_user_id", 10002},
    };
    const auto encoded = tinyimx::eventing::EventCodec::Encode(event);

    tinyimx::outbox::OutboxRecord record;
    record.outbox_id = id;
    record.event_id = event.event_id;
    record.event_type = event.event_type;
    record.schema_version = event.schema_version;
    record.aggregate_type = event.aggregate_type;
    record.aggregate_id = event.aggregate_id;
    record.producer_service = event.producer_service;
    record.topic = "tinyimx-message-events";
    record.tag = event.event_type;
    record.message_key = event.event_id;
    record.payload = valid_payload ? encoded.encoded : "{broken-json";
    return record;
}

tinyimx::outbox::OutboxRelayOptions Options() {
    tinyimx::outbox::OutboxRelayOptions options;
    options.instance_id = "relay-test";
    options.batch_size = 1;
    options.worker_threads = 1;
    options.max_inflight = 1;
    options.poll_interval_ms = 5;
    options.lease_ms = 3000;
    options.lease_renew_interval_ms = 500;
    options.retry_base_ms = 20;
    options.retry_max_ms = 100;
    options.cleanup_interval_ms = 10000;
    return options;
}

bool WaitUntil(const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return condition();
}

void TestPublished() {
    FakeStore store(MakeRecord());
    FakePublisher publisher(tinyimx::eventing::PublishStatus::kPublished);
    tinyimx::outbox::OutboxRelay relay(&store, &publisher, Options());
    Expect(relay.Start(), "relay starts for publish case");
    Expect(WaitUntil([&] { return store.published(); }), "published result marks durable outbox Published");
    Expect(publisher.calls() == 1, "published case invokes publisher once");
    Expect(relay.ShutdownGraceful(1000), "published case drains gracefully");
}

void TestRetry() {
    FakeStore store(MakeRecord());
    FakePublisher publisher(tinyimx::eventing::PublishStatus::kRetryableFailure);
    tinyimx::outbox::OutboxRelay relay(&store, &publisher, Options());
    Expect(relay.Start(), "relay starts for retry case");
    Expect(WaitUntil([&] { return store.retry(); }), "retryable send schedules durable retry");
    Expect(store.retry_after_ms() >= 1 && store.retry_after_ms() <= 100, "retry delay stays bounded");
    Expect(relay.ShutdownGraceful(1000), "retry case drains gracefully");
}

void TestInvalidQuarantine() {
    FakeStore store(MakeRecord(false));
    FakePublisher publisher(tinyimx::eventing::PublishStatus::kPublished);
    tinyimx::outbox::OutboxRelay relay(&store, &publisher, Options());
    Expect(relay.Start(), "relay starts for quarantine case");
    Expect(WaitUntil([&] { return store.quarantined(); }), "invalid durable event is quarantined");
    Expect(publisher.calls() == 0, "invalid durable event never reaches RocketMQ publisher");
    Expect(!store.quarantine_publish_attempted(), "validation quarantine does not increment publish attempt");
    Expect(relay.ShutdownGraceful(1000), "quarantine case drains gracefully");
}

void TestDeterministicBackoff() {
    const int a = tinyimx::outbox::OutboxRelay::ComputeRetryDelayForTest(
        "event-x", 4, 200, 30000
    );
    const int b = tinyimx::outbox::OutboxRelay::ComputeRetryDelayForTest(
        "event-x", 4, 200, 30000
    );
    const int capped = tinyimx::outbox::OutboxRelay::ComputeRetryDelayForTest(
        "event-x", 30, 200, 30000
    );
    Expect(a == b, "retry jitter is deterministic for event/attempt");
    Expect(a > 0 && a <= 30000, "retry jitter stays in configured bound");
    Expect(capped > 0 && capped <= 30000, "exponential retry remains capped");
}

class BacklogStore : public tinyimx::outbox::OutboxStore {
public:
    explicit BacklogStore(std::uint64_t count) : count_(count) {}
    tinyimx::outbox::OutboxClaimResult ClaimBatch(const std::string& token,std::size_t limit,int) override {
        std::lock_guard lock(mutex_); ++claims_;
        tinyimx::outbox::OutboxClaimResult result;result.success=true;
        while(next_<=count_ && result.records.size()<limit) {
            auto r=MakeRecord(true,next_++);r.locked_by=token;owned_[r.outbox_id]=token;
            order_.push_back(r.outbox_id);result.records.push_back(std::move(r));
        }
        peak_=std::max(peak_,owned_.size());return result;
    }
    tinyimx::outbox::OutboxMutationResult RenewClaim(std::uint64_t id,const std::string&t,int)override{return owned(id,t,false);}
    tinyimx::outbox::OutboxMutationResult MarkPublished(std::uint64_t id,const std::string&t)override{return owned(id,t,true);}
    tinyimx::outbox::OutboxMutationResult MarkRetry(std::uint64_t id,const std::string&t,int,const std::string&)override{return owned(id,t,true);}
    tinyimx::outbox::OutboxMutationResult MarkQuarantined(std::uint64_t id,const std::string&t,const std::string&,bool)override{return owned(id,t,true);}
    tinyimx::outbox::OutboxMutationResult ReleaseClaim(std::uint64_t id,const std::string&t)override{return owned(id,t,true);}
    tinyimx::outbox::OutboxCleanupResult CleanupPublished(int,std::size_t)override{
        tinyimx::outbox::OutboxCleanupResult r;r.success=true;return r;
    }
    std::size_t finished()const{std::lock_guard lock(mutex_);return finished_;}
    std::size_t peak()const{std::lock_guard lock(mutex_);return peak_;}
    std::size_t claims()const{std::lock_guard lock(mutex_);return claims_;}
    std::vector<std::uint64_t> order()const{std::lock_guard lock(mutex_);return order_;}
private:
    tinyimx::outbox::OutboxMutationResult owned(std::uint64_t id,const std::string&t,bool done){
        std::lock_guard lock(mutex_);tinyimx::outbox::OutboxMutationResult r;
        auto it=owned_.find(id);if(it==owned_.end()||it->second!=t){r.ownership_lost=true;return r;}
        r.success=true;r.affected_rows=1;if(done){owned_.erase(it);++finished_;}return r;
    }
    mutable std::mutex mutex_;std::uint64_t count_,next_=1;
    std::size_t claims_=0,finished_=0,peak_=0;
    std::unordered_map<std::uint64_t,std::string> owned_;std::vector<std::uint64_t> order_;
};
class DelayedSuccessPublisher final:public tinyimx::eventing::EventPublisher {
public:
    tinyimx::eventing::PublishResult Publish(const tinyimx::eventing::PublishMessage&)override{
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        tinyimx::eventing::PublishResult r;r.status=tinyimx::eventing::PublishStatus::kPublished;return r;
    }
};
void TestBacklogRefillWithoutIdleDelay(){
    BacklogStore store(24);DelayedSuccessPublisher publisher;auto options=Options();
    options.batch_size=2;options.worker_threads=2;options.max_inflight=4;
    options.poll_interval_ms=30000;
    tinyimx::outbox::OutboxRelay relay(&store,&publisher,options);
    Expect(relay.Start(),"backlog relay starts with long idle poll interval");
    Expect(WaitUntil([&]{return store.finished()==24;}),"backlog refills on progress without sleeping for idle interval");
    Expect(relay.ShutdownGraceful(1000),"backlog refill shuts down while capacity wait is active");
    Expect(store.peak()<=4,"bounded refill never exceeds max_inflight");
    auto order=store.order();std::vector<std::uint64_t> expected;for(std::uint64_t i=1;i<=24;i++)expected.push_back(i);
    Expect(order==expected,"all backlog IDs claimed once in original order");
    Expect(relay.Stats().published_total==24,"all backlog successes marked published");
}
void TestEmptyQueueKeepsBackoff(){
    BacklogStore store(0);DelayedSuccessPublisher publisher;auto options=Options();
    options.batch_size=2;options.worker_threads=2;options.max_inflight=4;options.poll_interval_ms=100;
    tinyimx::outbox::OutboxRelay relay(&store,&publisher,options);
    Expect(relay.Start(),"empty relay starts");std::this_thread::sleep_for(std::chrono::milliseconds(250));
    Expect(relay.ShutdownGraceful(1000),"empty backoff wakes for shutdown");
    Expect(store.claims()>=1&&store.claims()<=10,"empty queue does not spin after adaptive refill");
}

class RecordingBatchStore final:public BacklogStore {
public:
    RecordingBatchStore(std::uint64_t count,bool fail=false):BacklogStore(count),fail_(fail){}
    tinyimx::outbox::OutboxBatchMutationResult MarkPublishedBatch(const std::vector<tinyimx::outbox::OutboxRecord>&records)override{
        {std::lock_guard lock(batch_mutex_);sizes_.push_back(records.size());for(const auto&r:records)ids_.push_back(r.outbox_id);}
        if(fail_){tinyimx::outbox::OutboxBatchMutationResult r;r.message="injected batch storage failure";return r;}
        return tinyimx::outbox::OutboxStore::MarkPublishedBatch(records);
    }
    auto sizes()const{std::lock_guard lock(batch_mutex_);return sizes_;}
    auto ids()const{std::lock_guard lock(batch_mutex_);return ids_;}
private:mutable std::mutex batch_mutex_;bool fail_;std::vector<std::size_t> sizes_;std::vector<std::uint64_t> ids_;
};
class AlternatingPublisher final:public tinyimx::eventing::EventPublisher {
public:
    tinyimx::eventing::PublishResult Publish(const tinyimx::eventing::PublishMessage&)override{
        tinyimx::eventing::PublishResult r;r.status=(++calls_%2==0)?tinyimx::eventing::PublishStatus::kPublished:tinyimx::eventing::PublishStatus::kRetryableFailure;return r;
    }
private:int calls_=0;
};
void TestPublishBatchBoundaries(){
    RecordingBatchStore store(10);FakePublisher publisher(tinyimx::eventing::PublishStatus::kPublished);auto o=Options();o.batch_size=10;o.max_inflight=10;o.publish_batch_size=4;
    tinyimx::outbox::OutboxRelay relay(&store,&publisher,o);Expect(relay.Start(),"bounded publication batches start");
    Expect(WaitUntil([&]{return store.finished()==10;}),"publication tail batch drains without fill wait");Expect(relay.ShutdownGraceful(1000),"batched publication drains gracefully");
    Expect(store.sizes()==std::vector<std::size_t>{4,4,2},"publication SQL batches have bounded 4/4/2 sizes");
    Expect(relay.Stats().published_total==10&&relay.Stats().inflight==0,"batch outcomes count every completed event");
}
void TestPublishBatchOnlyBrokerSuccess(){
    RecordingBatchStore store(8);AlternatingPublisher publisher;auto o=Options();o.batch_size=8;o.max_inflight=8;o.publish_batch_size=8;
    tinyimx::outbox::OutboxRelay relay(&store,&publisher,o);Expect(relay.Start(),"mixed-result batch starts");
    Expect(WaitUntil([&]{return store.finished()==8;}),"mixed send outcomes finish safely");Expect(relay.ShutdownGraceful(1000),"mixed-result batch drains");
    Expect(store.ids()==std::vector<std::uint64_t>{2,4,6,8},"only broker-confirmed events enter published-state batch");
    Expect(relay.Stats().published_total==4&&relay.Stats().retry_total==4,"failed MQ publishes retain retry outcomes");
}
void TestPublishBatchDatabaseFailure(){
    RecordingBatchStore store(4,true);FakePublisher publisher(tinyimx::eventing::PublishStatus::kPublished);auto o=Options();o.batch_size=4;o.max_inflight=4;o.publish_batch_size=4;
    tinyimx::outbox::OutboxRelay relay(&store,&publisher,o);Expect(relay.Start(),"batch storage failure case starts");
    Expect(WaitUntil([&]{return relay.Stats().publish_failure_total==4;}),"batch database failure counts all unmarked events");Expect(relay.ShutdownGraceful(1000),"batch error releases local inflight lifecycle");
    Expect(store.finished()==0&&relay.Stats().published_total==0,"broker success without database mark never claims durable Published");
}
void TestPublishBatchInvalidOptions(){
    BacklogStore store(0);FakePublisher publisher(tinyimx::eventing::PublishStatus::kPublished);
    for(std::size_t n:{0U,2U,65U}){auto o=Options();o.publish_batch_size=n;tinyimx::outbox::OutboxRelay relay(&store,&publisher,o);Expect(!relay.Start(),"invalid publication batch bound is rejected");}
}
void TestPublishBatchConfig(){
    tinyimx::Config defaults;Expect(defaults.LoadFromString("{}")&&defaults.OutboxRelay().publish_batch_size==1,"publication batch defaults preserve legacy single-event writes");
    const std::string prefix=R"({"mysql":{"enable":true},"rocketmq":{"enable":true},"outbox_relay":{"enable":true,"publish_batch_size":)";
    tinyimx::Config enabled;Expect(enabled.LoadFromString(prefix+"8}}")&&enabled.OutboxRelay().publish_batch_size==8,"publication batch size parses through real config");
    for(auto value:{"0","65"}){tinyimx::Config invalid;Expect(!invalid.LoadFromString(prefix+value+"}}"),"out-of-range publication batch configuration rejected");}
}

}  // namespace

int main() {
    std::cout << "========== TinyIMX M16-B Outbox Relay Unit Tests ==========\n";
    TestPublished();
    TestRetry();
    TestInvalidQuarantine();
    TestDeterministicBackoff();
    TestBacklogRefillWithoutIdleDelay();
    TestEmptyQueueKeepsBackoff();
    TestPublishBatchBoundaries();
    TestPublishBatchOnlyBrokerSuccess();
    TestPublishBatchDatabaseFailure();
    TestPublishBatchInvalidOptions();
    TestPublishBatchConfig();
    std::cout << "===========================================================\n";
    if (g_failed == 0) {
        std::cout << "[PASS] M16-B Outbox Relay unit tests\n";
    }
    return g_failed == 0 ? 0 : 1;
}
