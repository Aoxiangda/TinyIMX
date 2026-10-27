#pragma once
#include "services/message/local/LocalPrivateStore.h"
#include "services/eventing/EventPublisher.h"
#include <chrono>
#include <functional>
#include <memory>
namespace tinyimx::message::local {
struct LocalEventRelayOptions {
    std::size_t workers{4},max_held{64},scan_page{128},max_retry_entries{65536};
    // Opt-in candidate: opportunistically take already queued events and commit
    // only their successful publication markers together. Never wait to fill.
    std::size_t progress_batch_size{1};
    unsigned max_attempts{16};
    std::chrono::milliseconds retry_base{20},retry_max{5000},poll_interval{20},sync_interval{100};
    // Candidate: read lifecycle health without joining the scan/queue lock.
    bool nonblocking_health{false};
    // Nonblocking notification only; never destroy/stop the relay in this call.
    std::function<void()> on_fatal;
    // Test seam after actual publisher success, before the progress write.
    // With detached progress this runs on the progress writer, not the sender.
    std::function<void(const PendingLocalEvent&)> after_success_before_progress;
    // Test seam under the poll/ownership lock, immediately before store scan.
    std::function<void()> before_pending_scan;
    // Test seam for failure while building optional fatal diagnostics.
    std::function<void()> before_error_assignment;
};
struct LocalEventRelayStats {
    std::uint64_t published{0},retryable_failures{0},permanent_failures{0},quarantined{0},progress_syncs{0},sync_failures{0};
    std::size_t queued{0},active{0},held{0},held_peak{0},retry_entries{0};
    bool running{false},stopping{false},failed{false};
    bool held_filter_enabled{false};
    bool demand_page_enabled{false};
    std::uint64_t pending_pages{0}, decoded_page_records{0}, page_records_unvisited{0}, enqueued_events{0};
    std::string error;
};
class LocalEventRelay final {
public:
    // Store and publisher outlive this owner. Publisher calls must have their
    // own bounded request timeout. Shutdown completes current calls, leaves
    // queued/unattempted events durable, joins workers, then syncs progress.
    // TINYIMX_NATIVE_RELAY_DETACHED_PROGRESS_ENABLE=1 selects a bounded progress
    // writer. Successful publications retain their held slots until marked;
    // shutdown drains that writer after joining publishers and before sync.
    LocalEventRelay(LocalPrivateStore* store,eventing::EventPublisher* publisher,LocalEventRelayOptions options={});
    ~LocalEventRelay();
    LocalEventRelay(const LocalEventRelay&)=delete;
    LocalEventRelay&operator=(const LocalEventRelay&)=delete;
    bool Start();
    bool Shutdown();
    bool Healthy()const;
    LocalEventRelayStats Stats()const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
