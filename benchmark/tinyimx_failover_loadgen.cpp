#include "common/net/Buffer.h"
#include "common/protocol/Packet.h"
#include "common/protocol/ProtocolCodec.h"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

constexpr tinyimx::MessageType kChatDelivery =
    static_cast<tinyimx::MessageType>(2019);
constexpr tinyimx::MessageType kChatDeliveryAck =
    static_cast<tinyimx::MessageType>(2020);
constexpr tinyimx::MessageType kGroupMessageDelivery =
    static_cast<tinyimx::MessageType>(2051);
constexpr tinyimx::MessageType kGroupMessageDeliveryAck =
    static_cast<tinyimx::MessageType>(2052);

struct Config {
    std::string host{"127.0.0.1"};
    std::uint16_t port{9000};
    std::size_t connections{1000};
    std::uint64_t user_id_base{500000};
    std::string username_prefix{"m21b500000_"};
    std::string password{"123456"};
    std::string mode{"hold"};
    std::string peer_mode{"ring"};
    std::size_t hotspot_user_index{0};
    int duration_seconds{180};
    double total_rate{200.0};
    double ramp_per_second{100.0};
    std::size_t payload_bytes{128};
    int heartbeat_seconds{30};
    int drain_seconds{10};
    std::size_t max_outstanding{1};

    bool reconnect_on_disconnect{true};
    double reconnect_rate{300.0};
    int reconnect_base_ms{100};
    int reconnect_max_ms{3000};
    int reconnect_max_attempts{20};
    std::string recovery_events_file{};
};

bool ParseSize(const std::string& s, std::size_t* out) {
    try {
        std::size_t p = 0;
        auto v = std::stoull(s, &p);
        if (p != s.size()) return false;
        *out = static_cast<std::size_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseU64(const std::string& s, std::uint64_t* out) {
    try {
        std::size_t p = 0;
        auto v = std::stoull(s, &p);
        if (p != s.size()) return false;
        *out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseInt(const std::string& s, int* out) {
    try {
        std::size_t p = 0;
        long v = std::stol(s, &p);
        if (p != s.size()) return false;
        *out = static_cast<int>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseDouble(const std::string& s, double* out) {
    try {
        std::size_t p = 0;
        double v = std::stod(s, &p);
        if (p != s.size()) return false;
        *out = v;
        return true;
    } catch (...) {
        return false;
    }
}

void Usage(const char* argv0) {
    std::cerr
        << "usage: " << argv0 << " [options]\n"
        << "  --host <host> --port <port>\n"
        << "  --connections <N> --user-id-base <id>\n"
        << "  --username-prefix <prefix> --password <password>\n"
        << "  --mode <hold|private> --peer-mode <ring|hotspot>\n"
        << "  --duration <seconds> --rate <msg/s> --ramp-per-sec <connections/s>\n"
        << "  --payload-bytes <bytes> --heartbeat-seconds <seconds>\n"
        << "  --drain-seconds <seconds> --max-outstanding <N>\n"
        << "  --reconnect-on-disconnect <0|1>\n"
        << "  --reconnect-rate <connections/s>\n"
        << "  --reconnect-base-ms <ms> --reconnect-max-ms <ms>\n"
        << "  --reconnect-max-attempts <N>\n"
        << "  --recovery-events-file <path>\n";
}

bool ParseArgs(int argc, char* argv[], Config* c) {
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        auto next = [&]() -> std::optional<std::string> {
            if (i + 1 >= argc) return std::nullopt;
            return std::string(argv[++i]);
        };

        if (key == "-h" || key == "--help") {
            Usage(argv[0]);
            std::exit(0);
        } else if (key == "--host") {
            auto v = next(); if (!v) return false; c->host = *v;
        } else if (key == "--port") {
            auto v = next(); int x = 0;
            if (!v || !ParseInt(*v, &x) || x <= 0 || x > 65535) return false;
            c->port = static_cast<std::uint16_t>(x);
        } else if (key == "--connections") {
            auto v = next(); if (!v || !ParseSize(*v, &c->connections)) return false;
        } else if (key == "--user-id-base") {
            auto v = next(); if (!v || !ParseU64(*v, &c->user_id_base)) return false;
        } else if (key == "--username-prefix") {
            auto v = next(); if (!v) return false; c->username_prefix = *v;
        } else if (key == "--password") {
            auto v = next(); if (!v) return false; c->password = *v;
        } else if (key == "--mode") {
            auto v = next(); if (!v) return false; c->mode = *v;
        } else if (key == "--peer-mode") {
            auto v = next(); if (!v) return false; c->peer_mode = *v;
        } else if (key == "--hotspot-user-index") {
            auto v = next(); if (!v || !ParseSize(*v, &c->hotspot_user_index)) return false;
        } else if (key == "--duration") {
            auto v = next(); if (!v || !ParseInt(*v, &c->duration_seconds)) return false;
        } else if (key == "--rate") {
            auto v = next(); if (!v || !ParseDouble(*v, &c->total_rate)) return false;
        } else if (key == "--ramp-per-sec") {
            auto v = next(); if (!v || !ParseDouble(*v, &c->ramp_per_second)) return false;
        } else if (key == "--payload-bytes") {
            auto v = next(); if (!v || !ParseSize(*v, &c->payload_bytes)) return false;
        } else if (key == "--heartbeat-seconds") {
            auto v = next(); if (!v || !ParseInt(*v, &c->heartbeat_seconds)) return false;
        } else if (key == "--drain-seconds") {
            auto v = next(); if (!v || !ParseInt(*v, &c->drain_seconds)) return false;
        } else if (key == "--max-outstanding") {
            auto v = next(); if (!v || !ParseSize(*v, &c->max_outstanding)) return false;
        } else if (key == "--reconnect-on-disconnect") {
            auto v = next(); int x = 0;
            if (!v || !ParseInt(*v, &x) || (x != 0 && x != 1)) return false;
            c->reconnect_on_disconnect = (x == 1);
        } else if (key == "--reconnect-rate") {
            auto v = next(); if (!v || !ParseDouble(*v, &c->reconnect_rate)) return false;
        } else if (key == "--reconnect-base-ms") {
            auto v = next(); if (!v || !ParseInt(*v, &c->reconnect_base_ms)) return false;
        } else if (key == "--reconnect-max-ms") {
            auto v = next(); if (!v || !ParseInt(*v, &c->reconnect_max_ms)) return false;
        } else if (key == "--reconnect-max-attempts") {
            auto v = next(); if (!v || !ParseInt(*v, &c->reconnect_max_attempts)) return false;
        } else if (key == "--recovery-events-file") {
            auto v = next(); if (!v) return false; c->recovery_events_file = *v;
        } else {
            return false;
        }
    }

    if (c->connections == 0 || c->connections > 20000 || c->user_id_base == 0) return false;
    if (c->mode != "hold" && c->mode != "private") return false;
    if (c->peer_mode != "ring" && c->peer_mode != "hotspot") return false;
    if (c->hotspot_user_index >= c->connections) return false;
    if (c->duration_seconds <= 0 || c->duration_seconds > 86400) return false;
    if (c->mode == "private" && c->total_rate <= 0.0) return false;
    if (c->ramp_per_second <= 0.0 || c->reconnect_rate <= 0.0) return false;
    if (c->payload_bytes == 0 || c->payload_bytes > 65536) return false;
    if (c->heartbeat_seconds <= 0 || c->drain_seconds < 0 || c->drain_seconds > 300) return false;
    if (c->max_outstanding == 0 || c->max_outstanding > 128) return false;
    if (c->reconnect_base_ms <= 0 || c->reconnect_max_ms < c->reconnect_base_ms) return false;
    if (c->reconnect_max_attempts <= 0 || c->reconnect_max_attempts > 1000) return false;
    return true;
}

std::string Username(const Config& c, std::size_t i) {
    std::ostringstream out;
    out << c.username_prefix << std::setw(6) << std::setfill('0') << (i + 1);
    return out.str();
}

std::uint64_t UserId(const Config& c, std::size_t i) {
    return c.user_id_base + i + 1;
}

std::uint64_t PeerUserId(const Config& c, std::size_t i) {
    if (c.peer_mode == "hotspot") {
        if (i != c.hotspot_user_index) return UserId(c, c.hotspot_user_index);
        return UserId(c, (c.hotspot_user_index + 1) % c.connections);
    }
    return UserId(c, (i + 1) % c.connections);
}

std::uint64_t EpochMillis() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

std::uint64_t Mix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31U);
}

class Histogram {
public:
    static constexpr std::uint64_t kBucketUs = 100;
    static constexpr std::uint64_t kMaxUs = 120'000'000;
    static constexpr std::size_t kCount = kMaxUs / kBucketUs + 1;

    Histogram() : buckets_(kCount, 0) {}

    void Record(std::chrono::microseconds elapsed) {
        std::uint64_t us = elapsed.count() < 0
            ? std::uint64_t{0}
            : static_cast<std::uint64_t>(elapsed.count());
        us = std::min(us, kMaxUs);
        ++buckets_[us / kBucketUs];
        ++count_;
        sum_us_ += us;
        min_us_ = std::min(min_us_, us);
        max_us_ = std::max(max_us_, us);
    }

    std::uint64_t Count() const { return count_; }
    double AvgMs() const { return count_ ? static_cast<double>(sum_us_) / count_ / 1000.0 : 0.0; }
    double MinMs() const { return count_ ? static_cast<double>(min_us_) / 1000.0 : 0.0; }
    double MaxMs() const { return count_ ? static_cast<double>(max_us_) / 1000.0 : 0.0; }

    double P(double p) const {
        if (!count_) return 0.0;
        const auto want = static_cast<std::uint64_t>(std::ceil(p * count_));
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < buckets_.size(); ++i) {
            seen += buckets_[i];
            if (seen >= want) return i * kBucketUs / 1000.0;
        }
        return kMaxUs / 1000.0;
    }

private:
    std::vector<std::uint64_t> buckets_;
    std::uint64_t count_{0};
    std::uint64_t sum_us_{0};
    std::uint64_t min_us_{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t max_us_{0};
};

struct Metrics {
    std::uint64_t connect_attempts{0};
    std::uint64_t connected{0};
    std::uint64_t connect_fail{0};
    std::uint64_t login_ok{0};
    std::uint64_t login_fail{0};
    std::uint64_t login_response_fail{0};
    std::uint64_t login_unresolved_fail{0};
    std::uint64_t deadline_rejections{0};

    std::uint64_t disconnects{0};
    std::uint64_t disconnect_events{0};
    std::uint64_t affected_clients{0};
    std::uint64_t recovered_clients{0};
    std::uint64_t reconnect_attempts{0};
    std::uint64_t reconnect_connect_fail{0};
    std::uint64_t reconnect_connected{0};
    std::uint64_t reconnect_exhausted{0};
    std::uint64_t reauth_attempts{0};
    std::uint64_t reauth_ok{0};
    std::uint64_t reauth_fail{0};

    std::uint64_t protocol_errors{0};
    std::uint64_t server_errors{0};
    std::uint64_t overload_rejections{0};
    std::uint64_t heartbeat_sent{0};
    std::uint64_t heartbeat_ack{0};

    std::uint64_t send_attempts{0};
    std::uint64_t message_retry_attempts{0};
    std::uint64_t chat_ack_ok{0};
    std::uint64_t chat_ack_fail{0};
    std::uint64_t receiver_delivery{0};
    std::uint64_t receiver_delivery_unique{0};
    std::uint64_t receiver_delivery_duplicates{0};
    std::uint64_t receiver_ack_sent{0};
    std::uint64_t group_delivery{0};
    std::uint64_t group_ack_sent{0};
    std::uint64_t loadgen_backpressure_skips{0};
};

enum class State {
    kUnused,
    kConnecting,
    kLoginPending,
    kOnline,
    kClosed,
};

struct Pending {
    std::string client_message_id;
    std::uint64_t to{0};
    TimePoint first_sent{};
};

struct Conn {
    int fd{-1};
    State state{State::kUnused};
    tinyimx::Buffer input;
    std::deque<std::string> out;
    std::size_t out_offset{0};
    std::unordered_map<std::uint32_t, Pending> pending;
    std::unordered_map<std::string, Pending> retry_pending;
    std::unordered_set<std::uint64_t> seen_deliveries;

    std::uint32_t seq{1};
    std::uint64_t uid{0};
    std::uint64_t peer{0};
    std::string username;
    std::uint64_t logical{0};

    bool login_resolved{false};
    bool counted_disconnect{false};
    bool ever_online{false};
    bool recovery_pending{false};
    bool recovered_once{false};
    int reconnect_attempt_no{0};

    TimePoint login_started{};
    TimePoint next_heartbeat{};
    TimePoint first_disconnect{};
    TimePoint next_reconnect{};
};

class FailoverLoadGen {
public:
    explicit FailoverLoadGen(Config config)
        : config_(std::move(config)),
          conns_(config_.connections),
          events_(4096),
          payload_(config_.payload_bytes, 'x'),
          run_id_(EpochMillis()) {}

    ~FailoverLoadGen() {
        for (auto& c : conns_) {
            if (c.fd >= 0) ::close(c.fd);
        }
        if (epoll_fd_ >= 0) ::close(epoll_fd_);
    }

    int Run() {
        if (!Resolve()) return 64;
        epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0) return 64;

        if (!config_.recovery_events_file.empty()) {
            recovery_events_.open(config_.recovery_events_file, std::ios::out | std::ios::trunc);
            if (!recovery_events_) {
                std::cerr << "failed to open recovery events file: "
                          << config_.recovery_events_file << '\n';
                return 64;
            }
            recovery_events_ << "event,user_id,elapsed_ms,attempt\n";
            // The failover runner consumes this file while LoadGen is alive.
            // Make the CSV header immediately visible to the observer.
            recovery_events_.flush();
        }

        start_ = Clock::now();
        next_connect_ = start_;
        next_reconnect_global_ = start_;
        const double ramp_seconds =
            static_cast<double>(config_.connections) / config_.ramp_per_second;
        setup_deadline_ = start_ + std::chrono::seconds(
            static_cast<int>(std::max(60.0, ramp_seconds * 2.0 + 30.0))
        );

        while (!finished_) {
            const auto now = Clock::now();
            if (!test_started_) {
                OpenDue(now);
                EvaluateSetup(now);
            } else {
                ReconnectDue(now);
                Heartbeat(now);
                if (!draining_) {
                    SendDue(now);
                    if (now >= test_end_) {
                        draining_ = true;
                        drain_end_ = now + std::chrono::seconds(config_.drain_seconds);
                    }
                } else if (now >= drain_end_) {
                    finished_ = true;
                    break;
                }
            }

            const int ready = ::epoll_wait(
                epoll_fd_, events_.data(), static_cast<int>(events_.size()), 10
            );
            if (ready < 0) {
                if (errno == EINTR) continue;
                return 65;
            }
            for (int i = 0; i < ready; ++i) {
                const auto index = events_[i].data.u32;
                if (index < conns_.size()) {
                    HandleEvent(index, events_[i].events);
                } else {
                    ++metrics_.protocol_errors;
                }
            }
        }

        result_time_ = Clock::now();

        /*
         * Capture the steady-state online population BEFORE the benchmark
         * performs its controlled shutdown.
         *
         * online_at_end is intentionally zero after CloseAll(false), so it
         * must never be used as a failover-health gate.
         */
        std::size_t online_before_close = 0;
        for (const auto& c : conns_) {
            if (c.state == State::kOnline) {
                ++online_before_close;
            }
        }

        CloseAll(false);
        PrintResult(online_before_close);
        return setup_failed_ ? 2 : 0;
    }

private:
    bool Resolve() {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        const auto service = std::to_string(config_.port);
        if (::getaddrinfo(config_.host.c_str(), service.c_str(), &hints, &result) != 0 || result == nullptr) {
            return false;
        }
        bool ok = false;
        for (auto* p = result; p != nullptr; p = p->ai_next) {
            if (p->ai_family == AF_INET) {
                std::memcpy(&target_, p->ai_addr, sizeof(target_));
                ok = true;
                break;
            }
        }
        ::freeaddrinfo(result);
        return ok;
    }

    static bool SetNonblocking(int fd) {
        const int flags = ::fcntl(fd, F_GETFL, 0);
        return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    std::uint32_t NextSeq(Conn& c) {
        for (;;) {
            const auto seq = c.seq++;
            if (seq != 0) return seq;
        }
    }

    double SinceTestStartMs(TimePoint now) const {
        if (!test_started_ || test_start_ == TimePoint{}) return 0.0;
        return std::chrono::duration<double, std::milli>(now - test_start_).count();
    }

    void WriteRecoveryEvent(
        const char* event,
        const Conn& c,
        TimePoint now,
        int attempt
    ) {
        if (!recovery_events_) return;
        recovery_events_ << event << ',' << c.uid << ','
                         << std::fixed << std::setprecision(3)
                         << SinceTestStartMs(now) << ',' << attempt << '\n';

        /*
         * recovery-events.csv is a live coordination channel between
         * FailoverLoadGen and tinyimx_capstone_failover.sh.
         *
         * std::ofstream is buffered. Without an explicit flush the runner can
         * observe a stale recovered-client count even though the in-process
         * metrics have already reached full recovery. flush() is sufficient:
         * the runner only needs visibility through the page cache; fsync()
         * durability is not required for benchmark coordination.
         */
        recovery_events_.flush();
    }

    int BackoffMs(const Conn& c) const {
        int cap = config_.reconnect_base_ms;
        const int exponent = std::max(0, std::min(c.reconnect_attempt_no, 20));
        for (int i = 0; i < exponent && cap < config_.reconnect_max_ms; ++i) {
            if (cap > config_.reconnect_max_ms / 2) {
                cap = config_.reconnect_max_ms;
                break;
            }
            cap *= 2;
        }
        cap = std::min(cap, config_.reconnect_max_ms);
        const auto mixed = Mix64(run_id_ ^ c.uid ^ static_cast<std::uint64_t>(c.reconnect_attempt_no + 1));
        return 1 + static_cast<int>(mixed % static_cast<std::uint64_t>(std::max(1, cap)));
    }

    void ResetTransport(Conn& c) {
        c.input.RetrieveAll();
        c.out.clear();
        c.out_offset = 0;
    }

    void StashPending(Conn& c) {
        for (const auto& [seq, pending] : c.pending) {
            (void)seq;
            c.retry_pending[pending.client_message_id] = pending;
        }
        c.pending.clear();
    }

    void ScheduleReconnect(std::size_t i, TimePoint now) {
        auto& c = conns_[i];
        if (!config_.reconnect_on_disconnect || !c.ever_online || draining_) return;
        if (c.reconnect_attempt_no >= config_.reconnect_max_attempts) {
            ++metrics_.reconnect_exhausted;
            return;
        }
        c.recovery_pending = true;
        c.next_reconnect = now + std::chrono::milliseconds(BackoffMs(c));
    }

    bool OpenSocket(std::size_t i, bool reconnect) {
        auto& c = conns_[i];
        ResetTransport(c);
        c.login_resolved = false;

        const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (reconnect) {
            ++metrics_.reconnect_attempts;
            ++c.reconnect_attempt_no;
        } else {
            ++metrics_.connect_attempts;
        }

        if (fd < 0 || !SetNonblocking(fd)) {
            if (fd >= 0) ::close(fd);
            if (reconnect) {
                ++metrics_.reconnect_connect_fail;
                ScheduleReconnect(i, Clock::now());
            } else {
                ++metrics_.connect_fail;
                c.state = State::kClosed;
            }
            return false;
        }

        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));

        c.fd = fd;
        c.uid = UserId(config_, i);
        c.peer = PeerUserId(config_, i);
        c.username = Username(config_, i);

        const int rc = ::connect(
            fd,
            reinterpret_cast<sockaddr*>(&target_),
            sizeof(target_)
        );
        c.state = (rc == 0 ? State::kLoginPending : State::kConnecting);
        if (rc != 0 && errno != EINPROGRESS) {
            ::close(fd);
            c.fd = -1;
            c.state = State::kClosed;
            if (reconnect) {
                ++metrics_.reconnect_connect_fail;
                ScheduleReconnect(i, Clock::now());
            } else {
                ++metrics_.connect_fail;
            }
            return false;
        }

        epoll_event event{};
        event.data.u32 = static_cast<std::uint32_t>(i);
        event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) != 0) {
            ::close(fd);
            c.fd = -1;
            c.state = State::kClosed;
            if (reconnect) {
                ++metrics_.reconnect_connect_fail;
                ScheduleReconnect(i, Clock::now());
            } else {
                ++metrics_.connect_fail;
            }
            return false;
        }

        if (rc == 0) Connected(i, reconnect);
        return true;
    }

    void OpenDue(TimePoint now) {
        const auto interval = std::chrono::duration<double>(1.0 / config_.ramp_per_second);
        std::size_t burst = 0;
        while (created_ < config_.connections && now >= next_connect_ && burst < 256) {
            OpenSocket(created_++, false);
            ++burst;
            next_connect_ += std::chrono::duration_cast<Clock::duration>(interval);
        }
    }

    void ReconnectDue(TimePoint now) {
        if (!config_.reconnect_on_disconnect || draining_) return;
        const auto interval = std::chrono::duration<double>(1.0 / config_.reconnect_rate);
        std::size_t burst = 0;

        while (now >= next_reconnect_global_ && burst < 256) {
            std::optional<std::size_t> selected;
            for (std::size_t attempt = 0; attempt < conns_.size(); ++attempt) {
                const std::size_t i = (reconnect_cursor_ + attempt) % conns_.size();
                const auto& c = conns_[i];
                if (c.fd < 0 && c.recovery_pending &&
                    c.reconnect_attempt_no < config_.reconnect_max_attempts &&
                    c.next_reconnect != TimePoint{} && now >= c.next_reconnect) {
                    selected = i;
                    reconnect_cursor_ = (i + 1) % conns_.size();
                    break;
                }
            }

            if (!selected.has_value()) break;
            OpenSocket(*selected, true);
            ++burst;
            next_reconnect_global_ += std::chrono::duration_cast<Clock::duration>(interval);
        }

        if (next_reconnect_global_ < now - std::chrono::seconds(1)) {
            next_reconnect_global_ = now;
        }
    }

    void EvaluateSetup(TimePoint now) {
        const auto terminal = metrics_.connect_fail + metrics_.login_ok + metrics_.login_fail;
        if (created_ == config_.connections && terminal >= config_.connections) {
            if (metrics_.login_ok != config_.connections) {
                setup_failed_ = true;
                finished_ = true;
                return;
            }
            test_started_ = true;
            test_start_ = now;
            test_end_ = now + std::chrono::seconds(config_.duration_seconds);
            next_send_ = now;
            next_reconnect_global_ = now;
            std::cout << "TINYIMX_FAILOVER_LOADGEN_READY connections="
                      << config_.connections
                      << " mode=" << config_.mode
                      << " reconnect_rate=" << config_.reconnect_rate
                      << std::endl;
            return;
        }
        if (now >= setup_deadline_) {
            setup_failed_ = true;
            finished_ = true;
        }
    }

    bool Queue(std::size_t i, const tinyimx::Packet& packet) {
        auto& c = conns_[i];
        if (c.fd < 0 || c.state == State::kClosed) return false;
        tinyimx::Buffer encoded;
        std::string error;
        if (!codec_.Encode(packet, &encoded, &error)) {
            ++metrics_.protocol_errors;
            return false;
        }
        c.out.push_back(encoded.RetrieveAllAsString());
        Interest(i);
        return true;
    }

    void Interest(std::size_t i) {
        auto& c = conns_[i];
        if (c.fd < 0) return;
        epoll_event event{};
        event.data.u32 = static_cast<std::uint32_t>(i);
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
        if (c.state == State::kConnecting || !c.out.empty()) event.events |= EPOLLOUT;
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, c.fd, &event);
    }

    void Connected(std::size_t i, bool reconnect) {
        auto& c = conns_[i];
        c.state = State::kLoginPending;
        c.login_started = Clock::now();
        c.login_resolved = false;
        if (reconnect) {
            ++metrics_.reconnect_connected;
            ++metrics_.reauth_attempts;
        } else {
            ++metrics_.connected;
        }

        tinyimx::Packet packet;
        packet.type = tinyimx::MessageType::kLoginRequest;
        packet.seq = NextSeq(c);
        packet.body = Json{{"username", c.username}, {"password", config_.password}}.dump();
        if (!Queue(i, packet)) {
            if (reconnect) {
                c.login_resolved = true;
                ++metrics_.reauth_fail;
                Close(i, true);
            } else {
                c.login_resolved = true;
                ++metrics_.login_fail;
                ++metrics_.login_unresolved_fail;
                Close(i, false);
            }
        }
    }

    void HandleConnectFailure(std::size_t i) {
        auto& c = conns_[i];
        const bool reconnect = c.recovery_pending && c.ever_online;
        if (c.fd >= 0) {
            ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, c.fd, nullptr);
            ::close(c.fd);
            c.fd = -1;
        }
        c.state = State::kClosed;
        ResetTransport(c);
        if (reconnect) {
            ++metrics_.reconnect_connect_fail;
            ScheduleReconnect(i, Clock::now());
        } else {
            ++metrics_.connect_fail;
        }
    }

    void HandleEvent(std::size_t i, std::uint32_t events) {
        auto& c = conns_[i];
        if (c.fd < 0) return;

        if ((events & EPOLLOUT) && c.state == State::kConnecting) {
            int error = 0;
            socklen_t length = sizeof(error);
            if (::getsockopt(c.fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0) {
                HandleConnectFailure(i);
                return;
            }
            Connected(i, c.recovery_pending && c.ever_online);
        }
        if ((events & EPOLLOUT) && c.fd >= 0) Flush(i);
        if ((events & EPOLLIN) && c.fd >= 0) Read(i);
        if ((events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) && c.fd >= 0) {
            Close(i, test_started_);
        }
    }

    void Flush(std::size_t i) {
        auto& c = conns_[i];
        while (c.fd >= 0 && !c.out.empty()) {
            auto& frame = c.out.front();
            const auto* data = frame.data() + c.out_offset;
            const auto remaining = frame.size() - c.out_offset;
            const ssize_t written = ::send(c.fd, data, remaining, MSG_NOSIGNAL);
            if (written > 0) {
                c.out_offset += static_cast<std::size_t>(written);
                if (c.out_offset == frame.size()) {
                    c.out.pop_front();
                    c.out_offset = 0;
                }
                continue;
            }
            if (written < 0 && errno == EINTR) continue;
            if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            Close(i, test_started_);
            return;
        }
        Interest(i);
    }

    void Read(std::size_t i) {
        auto& c = conns_[i];
        char buffer[16384];
        for (;;) {
            const ssize_t n = ::recv(c.fd, buffer, sizeof(buffer), 0);
            if (n > 0) {
                c.input.Append(buffer, static_cast<std::size_t>(n));
                continue;
            }
            if (n == 0) {
                Close(i, test_started_);
                return;
            }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            Close(i, test_started_);
            return;
        }

        while (c.fd >= 0) {
            auto decoded = codec_.Decode(&c.input);
            if (decoded.status == tinyimx::DecodeStatus::kNeedMoreData) break;
            if (decoded.status != tinyimx::DecodeStatus::kOk) {
                ++metrics_.protocol_errors;
                Close(i, test_started_);
                return;
            }
            if (decoded.packets.empty()) break;
            for (const auto& packet : decoded.packets) {
                HandlePacket(i, packet);
                if (c.fd < 0) return;
            }
        }
    }

    void HandlePacket(std::size_t i, const tinyimx::Packet& packet) {
        if (packet.type == tinyimx::MessageType::kLoginResponse) {
            Login(i, packet);
            return;
        }
        if (packet.type == tinyimx::MessageType::kChatAck) {
            ChatAck(i, packet);
            return;
        }
        if (packet.type == kChatDelivery) {
            Delivery(i, packet, false);
            return;
        }
        if (packet.type == kGroupMessageDelivery) {
            Delivery(i, packet, true);
            return;
        }
        if (packet.type == tinyimx::MessageType::kHeartbeat) {
            try {
                if (Json::parse(packet.body).value("pong", false)) {
                    ++metrics_.heartbeat_ack;
                } else {
                    ++metrics_.protocol_errors;
                }
            } catch (...) {
                ++metrics_.protocol_errors;
            }
            return;
        }
        if (packet.type == tinyimx::MessageType::kError) {
            ++metrics_.server_errors;
            DetectOverload(packet.body);
            return;
        }
        ++metrics_.protocol_errors;
    }

    void RetryPending(std::size_t i, TimePoint now) {
        auto& c = conns_[i];
        if (c.retry_pending.empty()) return;

        std::vector<Pending> pending;
        pending.reserve(c.retry_pending.size());
        for (const auto& [client_id, item] : c.retry_pending) {
            (void)client_id;
            pending.push_back(item);
        }
        c.retry_pending.clear();

        for (const auto& item : pending) {
            const auto seq = NextSeq(c);
            tinyimx::Packet packet;
            packet.type = tinyimx::MessageType::kChatMessage;
            packet.seq = seq;
            packet.body = Json{
                {"client_message_id", item.client_message_id},
                {"to", item.to},
                {"text", payload_}
            }.dump();
            if (Queue(i, packet)) {
                c.pending.emplace(seq, item);
                ++metrics_.message_retry_attempts;
            } else {
                c.retry_pending[item.client_message_id] = item;
            }
        }
        (void)now;
    }

    void Login(std::size_t i, const tinyimx::Packet& packet) {
        auto& c = conns_[i];
        if (c.login_resolved) {
            ++metrics_.protocol_errors;
            return;
        }

        const auto now = Clock::now();
        const bool recovery_login = c.recovery_pending && c.ever_online;
        if (c.login_started != TimePoint{}) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - c.login_started);
            if (recovery_login) reauth_latency_.Record(elapsed);
            else login_latency_.Record(elapsed);
        }

        bool success = false;
        try {
            const auto body = Json::parse(packet.body);
            success = body.value("success", false) && body.value("user_id", 0ULL) == c.uid;
            if (!success) {
                const auto reason = body.value("reason", std::string{});
                if (reason == "business_deadline_exceeded") ++metrics_.deadline_rejections;
                if (reason.find("overload") != std::string::npos ||
                    reason == "business_runtime_overloaded" ||
                    reason == "business_executor_overloaded") {
                    ++metrics_.overload_rejections;
                }
            }
        } catch (...) {
            success = false;
        }

        c.login_resolved = true;
        if (!success) {
            if (recovery_login) {
                ++metrics_.reauth_fail;
                Close(i, true);
            } else {
                ++metrics_.login_fail;
                ++metrics_.login_response_fail;
                Close(i, false);
            }
            return;
        }

        c.state = State::kOnline;
        c.next_heartbeat = now + std::chrono::seconds(config_.heartbeat_seconds);
        Interest(i);

        if (!c.ever_online) {
            c.ever_online = true;
            ++metrics_.login_ok;
            return;
        }

        if (recovery_login) {
            ++metrics_.reauth_ok;
            if (!c.recovered_once) {
                c.recovered_once = true;
                ++metrics_.recovered_clients;
            }
            if (c.first_disconnect != TimePoint{}) {
                recovery_latency_.Record(
                    std::chrono::duration_cast<std::chrono::microseconds>(now - c.first_disconnect)
                );
            }
            WriteRecoveryEvent("recovered", c, now, c.reconnect_attempt_no);
            c.recovery_pending = false;
            last_recovery_time_ = now;
            RetryPending(i, now);
        }
    }

    void ChatAck(std::size_t i, const tinyimx::Packet& packet) {
        auto& c = conns_[i];
        const auto it = c.pending.find(packet.seq);
        if (it == c.pending.end()) {
            ++metrics_.protocol_errors;
            return;
        }
        const Pending pending = it->second;
        c.pending.erase(it);

        try {
            const auto body = Json::parse(packet.body);
            if (!body.value("success", false)) {
                ++metrics_.chat_ack_fail;
                DetectOverload(packet.body);
                return;
            }
            if (body.value("client_message_id", std::string{}) != pending.client_message_id ||
                body.value("message_id", 0ULL) == 0 ||
                body.value("from", 0ULL) != c.uid ||
                body.value("to", 0ULL) != pending.to ||
                !body.value("stored_persistent", false)) {
                ++metrics_.chat_ack_fail;
                ++metrics_.protocol_errors;
                return;
            }
            ++metrics_.chat_ack_ok;
            message_latency_.Record(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now() - pending.first_sent
                )
            );
        } catch (...) {
            ++metrics_.chat_ack_fail;
            ++metrics_.protocol_errors;
        }
    }

    void Delivery(std::size_t i, const tinyimx::Packet& packet, bool group) {
        auto& c = conns_[i];
        if (packet.seq == 0) {
            ++metrics_.protocol_errors;
            return;
        }

        std::uint64_t message_id = 0;
        std::uint64_t to = c.uid;
        try {
            const auto body = Json::parse(packet.body);
            message_id = body.value("message_id", 0ULL);
            if (!group) to = body.value("to", 0ULL);
            if (message_id == 0 || (!group && to != c.uid)) {
                ++metrics_.protocol_errors;
                return;
            }
        } catch (...) {
            ++metrics_.protocol_errors;
            return;
        }

        tinyimx::Packet ack;
        ack.type = group ? kGroupMessageDeliveryAck : kChatDeliveryAck;
        ack.seq = packet.seq;
        ack.body = Json{{"message_id", message_id}}.dump();

        if (group) {
            ++metrics_.group_delivery;
        } else {
            ++metrics_.receiver_delivery;
            if (c.seen_deliveries.insert(message_id).second) {
                ++metrics_.receiver_delivery_unique;
            } else {
                ++metrics_.receiver_delivery_duplicates;
            }
        }

        if (Queue(i, ack)) {
            if (group) ++metrics_.group_ack_sent;
            else ++metrics_.receiver_ack_sent;
        } else {
            ++metrics_.protocol_errors;
        }
    }

    void DetectOverload(const std::string& body) {
        try {
            const auto reason = Json::parse(body).value("reason", std::string{});
            if (reason.find("overload") != std::string::npos ||
                reason == "business_runtime_overloaded" ||
                reason == "business_executor_overloaded") {
                ++metrics_.overload_rejections;
            }
        } catch (...) {
        }
    }

    void Heartbeat(TimePoint now) {
        for (std::size_t i = 0; i < conns_.size(); ++i) {
            auto& c = conns_[i];
            if (c.state != State::kOnline || now < c.next_heartbeat) continue;
            tinyimx::Packet packet;
            packet.type = tinyimx::MessageType::kHeartbeat;
            packet.seq = NextSeq(c);
            packet.body = "{}";
            if (Queue(i, packet)) ++metrics_.heartbeat_sent;
            c.next_heartbeat = now + std::chrono::seconds(config_.heartbeat_seconds);
        }
    }

    void SendDue(TimePoint now) {
        if (config_.mode != "private") return;
        const auto interval = std::chrono::duration<double>(1.0 / config_.total_rate);
        std::size_t burst = 0;

        while (now >= next_send_ && burst < 4096) {
            std::optional<std::size_t> selected;
            for (std::size_t attempt = 0; attempt < conns_.size(); ++attempt) {
                const std::size_t i = (send_cursor_ + attempt) % conns_.size();
                if (conns_[i].state == State::kOnline &&
                    conns_[i].pending.size() < config_.max_outstanding) {
                    selected = i;
                    send_cursor_ = (i + 1) % conns_.size();
                    break;
                }
            }

            if (!selected.has_value()) {
                ++metrics_.loadgen_backpressure_skips;
                next_send_ = now + std::chrono::duration_cast<Clock::duration>(interval);
                break;
            }

            SendOne(*selected, now);
            ++burst;
            next_send_ += std::chrono::duration_cast<Clock::duration>(interval);
        }
    }

    void SendOne(std::size_t i, TimePoint now) {
        auto& c = conns_[i];
        const auto seq = NextSeq(c);
        ++c.logical;
        std::ostringstream id;
        id << 'b' << run_id_ << "-u" << c.uid << "-n" << c.logical;
        const auto client_id = id.str();
        if (client_id.size() > 64) {
            ++metrics_.protocol_errors;
            return;
        }

        tinyimx::Packet packet;
        packet.type = tinyimx::MessageType::kChatMessage;
        packet.seq = seq;
        packet.body = Json{
            {"client_message_id", client_id},
            {"to", c.peer},
            {"text", payload_}
        }.dump();

        if (!Queue(i, packet)) {
            ++metrics_.chat_ack_fail;
            return;
        }
        c.pending.emplace(seq, Pending{client_id, c.peer, now});
        ++metrics_.send_attempts;
    }

    void Close(std::size_t i, bool count_disconnect) {
        auto& c = conns_[i];
        if (c.fd < 0) return;
        const auto now = Clock::now();
        const bool was_online = c.state == State::kOnline;
        const bool was_reauth = c.recovery_pending && c.ever_online;

        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, c.fd, nullptr);
        ::close(c.fd);
        c.fd = -1;
        ResetTransport(c);

        if (!c.login_resolved && c.state != State::kConnecting && c.state != State::kUnused) {
            c.login_resolved = true;
            if (was_reauth) {
                ++metrics_.reauth_fail;
            } else if (!c.ever_online) {
                ++metrics_.login_fail;
                ++metrics_.login_unresolved_fail;
            }
        }

        if (count_disconnect) {
            ++metrics_.disconnect_events;
            if (!c.counted_disconnect && c.ever_online) {
                c.counted_disconnect = true;
                ++metrics_.disconnects;
                ++metrics_.affected_clients;
                c.first_disconnect = now;
                first_disconnect_time_ =
                    first_disconnect_time_ == TimePoint{}
                        ? now
                        : std::min(first_disconnect_time_, now);
                WriteRecoveryEvent("affected", c, now, 0);
            }
        }

        if (c.ever_online) StashPending(c);
        c.state = State::kClosed;

        if (count_disconnect && c.ever_online && config_.reconnect_on_disconnect) {
            c.recovery_pending = true;
            ScheduleReconnect(i, now);
        } else if (!was_online && was_reauth && config_.reconnect_on_disconnect) {
            ScheduleReconnect(i, now);
        }
    }

    void CloseAll(bool count_disconnect) {
        for (std::size_t i = 0; i < conns_.size(); ++i) Close(i, count_disconnect);
    }

    void PrintResult(std::size_t online_before_close) const {
        std::uint64_t inflight = 0;
        std::size_t online = 0;
        std::size_t recovery_pending = 0;
        for (const auto& c : conns_) {
            inflight += c.pending.size() + c.retry_pending.size();
            if (c.state == State::kOnline) ++online;
            if (c.recovery_pending) ++recovery_pending;
        }

        double active_seconds = 0.0;
        if (test_started_) {
            auto end = draining_ ? std::min(result_time_, test_end_) : result_time_;
            if (end > test_start_) {
                active_seconds = std::chrono::duration<double>(end - test_start_).count();
            }
        }
        if (active_seconds <= 0.0 && test_started_) active_seconds = config_.duration_seconds;

        const double throughput = active_seconds > 0.0
            ? static_cast<double>(metrics_.chat_ack_ok) / active_seconds
            : 0.0;
        const double success_rate = metrics_.send_attempts > 0
            ? 100.0 * static_cast<double>(metrics_.chat_ack_ok) /
                static_cast<double>(metrics_.send_attempts)
            : (config_.mode == "hold" ? 100.0 : 0.0);
        const double setup_ms = test_started_
            ? std::chrono::duration<double, std::milli>(test_start_ - start_).count()
            : std::chrono::duration<double, std::milli>(result_time_ - start_).count();

        const double first_disconnect_ms =
            first_disconnect_time_ == TimePoint{} ? 0.0 : SinceTestStartMs(first_disconnect_time_);
        const double last_recovery_ms =
            last_recovery_time_ == TimePoint{} ? 0.0 : SinceTestStartMs(last_recovery_time_);
        const double recovery_window_ms =
            first_disconnect_time_ != TimePoint{} && last_recovery_time_ != TimePoint{}
                ? std::chrono::duration<double, std::milli>(
                      last_recovery_time_ - first_disconnect_time_
                  ).count()
                : 0.0;

        Json result{
            {"mode", config_.mode},
            {"peer_mode", config_.peer_mode},
            {"connections", config_.connections},
            {"run_id", run_id_},
            {"setup_failed", setup_failed_},
            {"setup_ms", setup_ms},
            {"connected", metrics_.connected},
            {"connect_fail", metrics_.connect_fail},
            {"login_ok", metrics_.login_ok},
            {"login_fail", metrics_.login_fail},
            {"login_response_fail", metrics_.login_response_fail},
            {"login_unresolved_fail", metrics_.login_unresolved_fail},
            {"deadline_rejections", metrics_.deadline_rejections},
            {"login_p50_ms", login_latency_.P(.50)},
            {"login_p95_ms", login_latency_.P(.95)},
            {"login_p99_ms", login_latency_.P(.99)},
            {"online_before_close", online_before_close},
            {"online_at_end", online},
            {"disconnects", metrics_.disconnects},
            {"disconnect_events", metrics_.disconnect_events},
            {"affected_clients", metrics_.affected_clients},
            {"recovered_clients", metrics_.recovered_clients},
            {"recovery_pending_at_end", recovery_pending},
            {"unrecovered_clients", metrics_.affected_clients >= metrics_.recovered_clients
                ? metrics_.affected_clients - metrics_.recovered_clients : 0},
            {"reconnect_attempts", metrics_.reconnect_attempts},
            {"reconnect_connect_fail", metrics_.reconnect_connect_fail},
            {"reconnect_connected", metrics_.reconnect_connected},
            {"reconnect_exhausted", metrics_.reconnect_exhausted},
            {"reauth_attempts", metrics_.reauth_attempts},
            {"reauth_ok", metrics_.reauth_ok},
            {"reauth_fail", metrics_.reauth_fail},
            {"reauth_p50_ms", reauth_latency_.P(.50)},
            {"reauth_p95_ms", reauth_latency_.P(.95)},
            {"reauth_p99_ms", reauth_latency_.P(.99)},
            {"recovery_p50_ms", recovery_latency_.P(.50)},
            {"recovery_p95_ms", recovery_latency_.P(.95)},
            {"recovery_p99_ms", recovery_latency_.P(.99)},
            {"recovery_max_ms", recovery_latency_.MaxMs()},
            {"first_disconnect_after_test_ms", first_disconnect_ms},
            {"last_recovery_after_test_ms", last_recovery_ms},
            {"recovery_window_ms", recovery_window_ms},
            {"protocol_errors", metrics_.protocol_errors},
            {"server_errors", metrics_.server_errors},
            {"overload_rejections", metrics_.overload_rejections},
            {"heartbeat_sent", metrics_.heartbeat_sent},
            {"heartbeat_ack", metrics_.heartbeat_ack},
            {"send_attempts", metrics_.send_attempts},
            {"message_retry_attempts", metrics_.message_retry_attempts},
            {"chat_ack_ok", metrics_.chat_ack_ok},
            {"chat_ack_fail", metrics_.chat_ack_fail},
            {"receiver_delivery", metrics_.receiver_delivery},
            {"receiver_delivery_unique", metrics_.receiver_delivery_unique},
            {"receiver_delivery_duplicates", metrics_.receiver_delivery_duplicates},
            {"receiver_ack_sent", metrics_.receiver_ack_sent},
            {"group_delivery", metrics_.group_delivery},
            {"group_ack_sent", metrics_.group_ack_sent},
            {"inflight_at_end", inflight},
            {"loadgen_backpressure_skips", metrics_.loadgen_backpressure_skips},
            {"duration_s", active_seconds},
            {"target_rate_msg_s", config_.mode == "private" ? config_.total_rate : 0.0},
            {"throughput_msg_s", throughput},
            {"success_rate", success_rate},
            {"p50_ms", message_latency_.P(.50)},
            {"p95_ms", message_latency_.P(.95)},
            {"p99_ms", message_latency_.P(.99)},
            {"latency_max_ms", message_latency_.MaxMs()},
        };

        std::cout << "TINYIMX_FAILOVER_LOADGEN_RESULT " << result.dump() << '\n';
    }

private:
    Config config_;
    int epoll_fd_{-1};
    sockaddr_in target_{};
    tinyimx::ProtocolCodec codec_;
    std::vector<Conn> conns_;
    std::vector<epoll_event> events_;
    std::string payload_;
    std::uint64_t run_id_{0};
    Metrics metrics_;
    Histogram login_latency_;
    Histogram reauth_latency_;
    Histogram recovery_latency_;
    Histogram message_latency_;
    std::ofstream recovery_events_;

    std::size_t created_{0};
    std::size_t send_cursor_{0};
    std::size_t reconnect_cursor_{0};
    bool test_started_{false};
    bool setup_failed_{false};
    bool draining_{false};
    bool finished_{false};

    TimePoint start_{};
    TimePoint next_connect_{};
    TimePoint next_reconnect_global_{};
    TimePoint setup_deadline_{};
    TimePoint test_start_{};
    TimePoint test_end_{};
    TimePoint next_send_{};
    TimePoint drain_end_{};
    TimePoint result_time_{};
    TimePoint first_disconnect_time_{};
    TimePoint last_recovery_time_{};
};

}  // namespace

int main(int argc, char* argv[]) {
    Config config;
    if (!ParseArgs(argc, argv, &config)) {
        Usage(argv[0]);
        return 64;
    }
    return FailoverLoadGen(std::move(config)).Run();
}
