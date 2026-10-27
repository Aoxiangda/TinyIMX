#include "services/projection/unread/UnreadProjector.h"

#include "common/logging/LogMacros.h"
#include "services/cache/UnreadCountCache.h"
#include "services/eventing/EventCodec.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace tinyimx::projection::unread {
namespace {

bool HasKey(
    const std::vector<std::string>& keys,
    const std::string& expected
) {
    return std::find(keys.begin(), keys.end(), expected) != keys.end();
}

}  // namespace

UnreadProjector::UnreadProjector(
    eventing::EventConsumer* consumer,
    UnreadProjectionSource* reader,
    UnreadCountCache* cache,
    UnreadProjectorOptions options
)
    : consumer_(consumer),
      reader_(reader),
      cache_(cache),
      options_(std::move(options)) {
}

UnreadProjector::~UnreadProjector() {
    Shutdown();
}

bool UnreadProjector::ValidateOptions(std::string* error) const {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (consumer_ == nullptr || reader_ == nullptr || cache_ == nullptr) {
        return fail("unread projector requires consumer, reader and cache");
    }
    if (options_.topic.empty()) {
        return fail("unread projector topic is empty");
    }
    if (options_.batch_size == 0 || options_.batch_size > 256) {
        return fail("unread projector batch_size is invalid");
    }
    if (options_.batch_snapshot_reads && options_.mode != UnreadProjectorMode::kWriter) {
        return fail("unread snapshot batching requires authoritative writer mode");
    }
    if (options_.worker_threads == 0 || options_.worker_threads > 16 ||
        options_.batch_size * options_.worker_threads > 1024) {
        return fail("unread projector workers must be 1-16 and held events <=1024");
    }
    if (options_.invisible_duration_ms <= 0 ||
        options_.receive_error_backoff_ms <= 0) {
        return fail("unread projector timing configuration is invalid");
    }
    return true;
}

bool UnreadProjector::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return true;
    }

    std::string error;
    if (!ValidateOptions(&error)) {
        running_.store(false);
        last_error_ = std::move(error);
        return false;
    }

    try {
        threads_.reserve(options_.worker_threads);
        for (std::size_t i = 0; i < options_.worker_threads; ++i) {
            threads_.emplace_back(&UnreadProjector::RunLoop, this);
        }
    } catch (const std::exception& e) {
        running_.store(false);
        lifecycle_cv_.notify_all();
        for (auto& thread : threads_) {
            if (thread.joinable()) thread.join();
        }
        threads_.clear();
        last_error_ = std::string("unread projector thread start failed: ") + e.what();
        return false;
    }

    LOG_INFO(
        "unread projector started"
        << ", mode="
        << (options_.mode == UnreadProjectorMode::kWriter ? "writer" : "shadow")
        << ", topic=" << options_.topic
        << ", batch_size=" << options_.batch_size
        << ", worker_threads=" << options_.worker_threads
        << ", parallel_batch_ack=" << options_.parallel_batch_ack
        << ", batch_snapshot_reads=" << options_.batch_snapshot_reads
    );
    return true;
}

void UnreadProjector::Shutdown() {
    const bool was_running = running_.exchange(false);
    lifecycle_cv_.notify_all();
    for (auto& thread : threads_) {
        if (thread.joinable()) thread.join();
    }
    threads_.clear();
    if (was_running) {
        LOG_INFO("unread projector stopped");
    }
}

bool UnreadProjector::RebuildUser(
    std::uint64_t receiver_user_id,
    std::string* error_message
) {
    if (reader_ == nullptr || cache_ == nullptr || receiver_user_id == 0) {
        if (error_message != nullptr) {
            *error_message = "unread projector rebuild rejected invalid input";
        }
        return false;
    }

    std::lock_guard<std::mutex> receiver_lock(
        receiver_mutexes_[receiver_user_id % receiver_mutexes_.size()]);
    const auto snapshot = reader_->LoadUserSnapshot(receiver_user_id);
    if (!snapshot.success) {
        if (error_message != nullptr) {
            *error_message = snapshot.message;
        }
        return false;
    }

    const auto replaced = cache_->ReplaceUserUnreadProjection(
        receiver_user_id,
        snapshot.value.peer_counts,
        snapshot.value.total_unread
    );
    if (!replaced.Succeeded()) {
        if (error_message != nullptr) {
            *error_message = replaced.error_message;
        }
        return false;
    }

    LOG_INFO(
        "unread projector rebuilt user projection"
        << ", receiver=" << receiver_user_id
        << ", peers=" << snapshot.value.peer_counts.size()
        << ", total_unread=" << snapshot.value.total_unread
    );
    return true;
}

bool UnreadProjector::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

UnreadProjectorStats UnreadProjector::Stats() const noexcept {
    UnreadProjectorStats stats;

    stats.received_total =
        received_total_.load(std::memory_order_relaxed);

    stats.reconciled_total =
        reconciled_total_.load(std::memory_order_relaxed);

    stats.shadow_match_total =
        shadow_match_total_.load(std::memory_order_relaxed);

    stats.shadow_mismatch_total =
        shadow_mismatch_total_.load(std::memory_order_relaxed);

    stats.process_failure_total =
        process_failure_total_.load(std::memory_order_relaxed);

    stats.receive_failure_total =
        receive_failure_total_.load(std::memory_order_relaxed);

    stats.retryable_failure_total =
        retryable_failure_total_.load(std::memory_order_relaxed);

    stats.poison_failure_total =
        poison_failure_total_.load(std::memory_order_relaxed);

    stats.ack_failure_total =
        ack_failure_total_.load(std::memory_order_relaxed);
    stats.snapshot_batch_calls = snapshot_batch_calls_.load(std::memory_order_relaxed);
    stats.snapshot_batch_dialogs = snapshot_batch_dialogs_.load(std::memory_order_relaxed);

    return stats;
}

const std::string& UnreadProjector::LastError() const noexcept {
    return last_error_;
}

bool UnreadProjector::ExtractAffectedDialog(
    const eventing::ConsumedEvent& consumed,
    AffectedDialog* dialog,
    std::string* error
) const {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (dialog == nullptr) {
        return fail("unread projector missing output dialog");
    }
    if (consumed.topic != options_.topic) {
        return fail("unread projector rejected unexpected topic");
    }

    const auto decoded = eventing::EventCodec::Decode(consumed.body);
    if (!decoded.success) {
        return fail(decoded.message);
    }
    const auto& event = decoded.event;
    if (event.schema_version != 1) {
        return fail("unread projector rejected unsupported schema version");
    }
    if (consumed.tag != event.event_type) {
        return fail("unread projector rejected tag/event_type mismatch");
    }
    if (!HasKey(consumed.keys, event.event_id)) {
        return fail("unread projector rejected missing event_id message key");
    }

    try {
        if (event.event_type == "message.created.v1") {
            dialog->receiver_user_id =
                event.payload.at("to_user_id").get<std::uint64_t>();
            dialog->peer_user_id =
                event.payload.at("from_user_id").get<std::uint64_t>();
        } else if (event.event_type == "dialog.read_advanced.v1") {
            dialog->receiver_user_id =
                event.payload.at("reader_user_id").get<std::uint64_t>();
            dialog->peer_user_id =
                event.payload.at("peer_user_id").get<std::uint64_t>();
        } else {
            return fail("unread projector rejected unsupported event_type");
        }
    } catch (const std::exception& e) {
        return fail(std::string("unread projector invalid event payload: ") + e.what());
    }

    if (dialog->receiver_user_id == 0 || dialog->peer_user_id == 0 ||
        dialog->receiver_user_id == dialog->peer_user_id) {
        return fail("unread projector extracted invalid dialog identity");
    }
    return true;
}

UnreadProcessResult UnreadProjector::ProcessEvent(
    const eventing::ConsumedEvent& consumed
) {
    AffectedDialog dialog;

    std::string extraction_error;

    if (!ExtractAffectedDialog(
            consumed,
            &dialog,
            &extraction_error)) {

        /*
         * The message itself cannot be interpreted by this projector:
         *
         * - malformed event envelope
         * - unsupported schema/event type
         * - tag/key mismatch
         * - invalid payload
         * - invalid dialog identity
         *
         * Retrying against healthy MySQL/Redis cannot repair the message.
         */
        return {
            UnreadProcessDisposition::kPermanentMessageFailure,
            extraction_error
        };
    }

    std::lock_guard<std::mutex> receiver_lock(
        receiver_mutexes_[dialog.receiver_user_id % receiver_mutexes_.size()]);
    const auto durable = reader_->LoadDialogSnapshot(
        dialog.receiver_user_id,
        dialog.peer_user_id
    );
    return ApplyDurableSnapshot(dialog, durable);
}

std::vector<UnreadProcessResult> UnreadProjector::ProcessBatch(
    const std::vector<eventing::ConsumedEvent>& events
) {
    std::vector<UnreadProcessResult> results(events.size());
    std::vector<std::pair<std::uint64_t, std::uint64_t>> dialogs;
    std::vector<std::size_t> indices, stripes;
    for (std::size_t i = 0; i < events.size(); ++i) {
        AffectedDialog dialog;
        std::string error;
        if (!ExtractAffectedDialog(events[i], &dialog, &error)) {
            results[i] = {UnreadProcessDisposition::kPermanentMessageFailure, error};
            continue;
        }
        indices.push_back(i);
        dialogs.emplace_back(dialog.receiver_user_id, dialog.peer_user_id);
        stripes.push_back(dialog.receiver_user_id % receiver_mutexes_.size());
    }
    if (dialogs.empty()) return results;
    // Hold each receiver stripe once, in a common order across all workers.
    // The fence includes SQL read and cache apply, also against RebuildUser.
    std::sort(stripes.begin(), stripes.end());
    stripes.erase(std::unique(stripes.begin(), stripes.end()), stripes.end());
    std::vector<std::unique_lock<std::mutex>> locks;
    locks.reserve(stripes.size());
    for (auto stripe : stripes) locks.emplace_back(receiver_mutexes_[stripe]);
    std::vector<UnreadProjectionReadResult<DialogUnreadSnapshot>> snapshots;
    std::string failure;
    snapshot_batch_calls_.fetch_add(1, std::memory_order_relaxed);
    snapshot_batch_dialogs_.fetch_add(dialogs.size(), std::memory_order_relaxed);
    try {
        snapshots = reader_->LoadDialogSnapshots(dialogs);
        if (snapshots.size() != dialogs.size())
            failure = "unread projection batch returned invalid result count";
    } catch (const std::exception& e) {
        failure = std::string("unread projection batch read threw: ") + e.what();
    } catch (...) {
        failure = "unread projection batch read threw unknown exception";
    }
    for (std::size_t i = 0; i < indices.size(); ++i) {
        auto& result = results[indices[i]];
        if (!failure.empty()) {
            result = {UnreadProcessDisposition::kRetryableDependencyFailure, failure};
            continue;
        }
        try {
            result = ApplyDurableSnapshot({dialogs[i].first, dialogs[i].second}, snapshots[i]);
        } catch (const std::exception& e) {
            result = {UnreadProcessDisposition::kRetryableDependencyFailure,
                      std::string("unread projection cache apply threw: ") + e.what()};
        } catch (...) {
            result = {UnreadProcessDisposition::kRetryableDependencyFailure,
                      "unread projection cache apply threw unknown exception"};
        }
    }
    return results;
}

UnreadProcessResult UnreadProjector::ApplyDurableSnapshot(
    const AffectedDialog& dialog,
    const UnreadProjectionReadResult<DialogUnreadSnapshot>& durable
) {

    if (!durable.success) {
        /*
         * The event is valid, but authoritative durable state cannot
         * currently be read. This is dependency failure, not poison.
         */
        return {
            UnreadProcessDisposition::kRetryableDependencyFailure,
            durable.message
        };
    }
    if (durable.value.receiver_user_id != dialog.receiver_user_id ||
        durable.value.peer_user_id != dialog.peer_user_id ||
        durable.value.private_unread < 0 ||
        durable.value.total_unread < durable.value.private_unread) {
        return {UnreadProcessDisposition::kRetryableDependencyFailure,
                "unread projection snapshot identity/count mismatch"};
    }

    if (options_.mode == UnreadProjectorMode::kShadow) {
        const auto private_cached =
            cache_->GetPrivateUnread(
                dialog.receiver_user_id,
                dialog.peer_user_id
            );

        const auto total_cached =
            cache_->GetTotalUnread(
                dialog.receiver_user_id
            );

        if (!private_cached.Completed() ||
            !total_cached.Completed()) {

            return {
                UnreadProcessDisposition::kRetryableDependencyFailure,
                "unread projector shadow read failed"
            };
        }

        const bool matches =
            private_cached.count ==
                durable.value.private_unread &&
            total_cached.count ==
                durable.value.total_unread;

        if (matches) {
            shadow_match_total_.fetch_add(
                1,
                std::memory_order_relaxed
            );
        } else {
            shadow_mismatch_total_.fetch_add(
                1,
                std::memory_order_relaxed
            );

            LOG_WARN(
                "unread projector shadow mismatch"
                << ", receiver="
                << dialog.receiver_user_id
                << ", peer="
                << dialog.peer_user_id
                << ", redis_private="
                << private_cached.count
                << ", mysql_private="
                << durable.value.private_unread
                << ", redis_total="
                << total_cached.count
                << ", mysql_total="
                << durable.value.total_unread
            );
        }

        return {
            UnreadProcessDisposition::kSucceeded,
            ""
        };
    }

    const auto applied =
        cache_->SetUnreadSnapshot(
            dialog.receiver_user_id,
            dialog.peer_user_id,
            durable.value.private_unread,
            durable.value.total_unread
        );

    if (!applied.Succeeded()) {
        /*
         * Redis projection failure is retryable. The authoritative MySQL
         * snapshot remains intact and the message MUST remain unacked.
         */
        return {
            UnreadProcessDisposition::kRetryableDependencyFailure,
            applied.error_message
        };
    }

    reconciled_total_.fetch_add(
        1,
        std::memory_order_relaxed
    );

    return {
        UnreadProcessDisposition::kSucceeded,
        ""
    };
}

void UnreadProjector::RunLoop() {
    while (running_.load(std::memory_order_acquire)) {
        const auto received = consumer_->Receive(
            options_.batch_size,
            options_.invisible_duration_ms
        );

        if (!received.success) {
            /*
             * Preserve process_failure_total as the historical aggregate
             * while exposing receive failures separately for C3.
             */
            process_failure_total_.fetch_add(
                1,
                std::memory_order_relaxed
            );

            receive_failure_total_.fetch_add(
                1,
                std::memory_order_relaxed
            );

            LOG_WARN(
                "unread projector receive failed"
                << ", failure_type=transport_receive"
                << ", error=" << received.message
            );

            std::unique_lock<std::mutex>
                lock(lifecycle_mutex_);

            lifecycle_cv_.wait_for(
                lock,
                std::chrono::milliseconds(
                    options_.receive_error_backoff_ms
                ),
                [&] {
                    return !running_.load(
                        std::memory_order_acquire
                    );
                }
            );

            continue;
        }

        // Once a batch is accepted, finish all its cache/ACK outcomes even if
        // shutdown begins during SQL. Before acceptance leave it unacked.
        const bool batch_snapshots = options_.batch_snapshot_reads;
        if (batch_snapshots && !running_.load(std::memory_order_acquire)) break;
        std::vector<UnreadProcessResult> batch_results;
        if (batch_snapshots) batch_results = ProcessBatch(received.events);
        std::vector<eventing::ConsumedEvent> ackable;
        if (options_.parallel_batch_ack) ackable.reserve(received.events.size());
        for (std::size_t event_index = 0; event_index < received.events.size(); ++event_index) {
            const auto& event = received.events[event_index];
            if (!batch_snapshots && !running_.load(
                    std::memory_order_acquire)) {
                break;
            }

            received_total_.fetch_add(
                1,
                std::memory_order_relaxed
            );

            UnreadProcessResult processed;
            try {
                processed = batch_snapshots ? batch_results[event_index] : ProcessEvent(event);
            } catch (const std::exception& e) {
                processed = {UnreadProcessDisposition::kRetryableDependencyFailure,
                             std::string("projection processing threw: ") + e.what()};
            } catch (...) {
                processed = {UnreadProcessDisposition::kRetryableDependencyFailure,
                             "projection processing threw unknown exception"};
            }

            if (!processed.Succeeded()) {
                /*
                 * Compatibility aggregate.
                 */
                process_failure_total_.fetch_add(
                    1,
                    std::memory_order_relaxed
                );

                if (
                    processed.disposition ==
                    UnreadProcessDisposition::
                        kRetryableDependencyFailure
                ) {
                    retryable_failure_total_.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );

                    /*
                     * NO ACK.
                     *
                     * MySQL/Redis/network dependency recovery may make the
                     * same event process successfully later.
                     */
                    LOG_ERROR(
                        "unread projector retryable dependency failure; "
                        "message left unacked"
                        << ", failure_type=retryable_dependency"
                        << ", broker_message_id="
                        << event.broker_message_id
                        << ", error="
                        << processed.error
                    );
                } else {
                    poison_failure_total_.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );

                    /*
                     * NO ACK.
                     *
                     * The message itself is permanently invalid for this
                     * consumer. RocketMQ consumer-group bounded retry owns
                     * transport retry exhaustion and DLQ transition.
                     */
                    LOG_ERROR(
                        "unread projector poison message rejected; "
                        "message left unacked"
                        << ", failure_type=permanent_message"
                        << ", broker_message_id="
                        << event.broker_message_id
                        << ", error="
                        << processed.error
                    );
                }

                continue;
            }

            /*
             * ACK only after durable read + projection processing has
             * succeeded. C2 crash-window semantics remain unchanged.
             */
            if (options_.parallel_batch_ack) {
                ackable.push_back(event);
                continue;
            }
            const auto ack = consumer_->Ack(event);

            if (!ack.success) {
                ack_failure_total_.fetch_add(
                    1,
                    std::memory_order_relaxed
                );

                LOG_WARN(
                    "unread projector ACK failed; "
                    "duplicate delivery remains safe"
                    << ", broker_message_id="
                    << event.broker_message_id
                    << ", error="
                    << ack.message
                );
            }
        }
        // Even if Shutdown begins while processing this batch, completed
        // projections are acknowledged and every submitted callback is joined
        // before the worker exits. Failed/unprocessed events remain unacked.
        if (!ackable.empty()) {
            std::vector<eventing::EventAckResult> results;
            try {
                results = consumer_->AckBatch(ackable);
            } catch (...) {
                // Unknown ACK outcome is a retry, never a reported success.
                ack_failure_total_.fetch_add(ackable.size(), std::memory_order_relaxed);
                LOG_WARN("unread projector ACK batch threw; redelivery remains safe");
                continue;
            }
            if (results.size() != ackable.size()) {
                ack_failure_total_.fetch_add(ackable.size(), std::memory_order_relaxed);
                LOG_WARN("unread projector ACK batch returned invalid result count");
                continue;
            }
            for (std::size_t i = 0; i < results.size(); ++i) {
                if (results[i].success) continue;
                ack_failure_total_.fetch_add(1, std::memory_order_relaxed);
                LOG_WARN("unread projector batch ACK failed; duplicate delivery remains safe"
                         << ", broker_message_id=" << ackable[i].broker_message_id
                         << ", error=" << results[i].message);
            }
        }
    }
}

}  // namespace tinyimx::projection::unread
