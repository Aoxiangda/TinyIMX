#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct StressConfig {
    std::string host{"127.0.0.1"};
    uint16_t port{9000};
    int client_count{10};
    int messages_per_client{100};
    int message_size{64};
};

struct StressResult {
    std::atomic<int> connected_clients{0};
    std::atomic<int> failed_clients{0};

    std::atomic<int> total_requests{0};
    std::atomic<int> success_requests{0};
    std::atomic<int> failed_requests{0};
};

void PrintUsage(const char* program) {
    std::cout << "Usage:\n"
              << "  " << program
              << " [host] [port] [client_count] [messages_per_client] [message_size]\n\n"
              << "Example:\n"
              << "  " << program << " 127.0.0.1 9000 20 100 64\n";
}

bool ParseInt(const char* text, int* value) {
    if (text == nullptr || value == nullptr) {
        return false;
    }

    try {
        *value = std::stoi(text);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseConfig(int argc, char* argv[], StressConfig* config) {
    if (config == nullptr) {
        return false;
    }

    if (argc >= 2) {
        config->host = argv[1];
    }

    if (argc >= 3) {
        int port = 0;
        if (!ParseInt(argv[2], &port) || port <= 0 || port > 65535) {
            return false;
        }
        config->port = static_cast<uint16_t>(port);
    }

    if (argc >= 4) {
        if (!ParseInt(argv[3], &config->client_count) ||
            config->client_count <= 0) {
            return false;
        }
    }

    if (argc >= 5) {
        if (!ParseInt(argv[4], &config->messages_per_client) ||
            config->messages_per_client <= 0) {
            return false;
        }
    }

    if (argc >= 6) {
        if (!ParseInt(argv[5], &config->message_size) ||
            config->message_size <= 0) {
            return false;
        }
    }

    return true;
}

std::string BuildPayload(int client_id,
                         int message_id,
                         int message_size) {
    std::ostringstream oss;
    oss << "client=" << client_id
        << ",message=" << message_id
        << ",";

    std::string payload = oss.str();

    if (static_cast<int>(payload.size()) < message_size) {
        payload.append(
            static_cast<std::size_t>(message_size - payload.size()),
            'x'
        );
    }

    payload.push_back('\n');
    return payload;
}

bool SetSocketTimeout(int fd, int seconds) {
    timeval timeout {};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    if (::setsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            static_cast<socklen_t>(sizeof(timeout))
        ) != 0) {
        return false;
    }

    if (::setsockopt(
            fd,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            static_cast<socklen_t>(sizeof(timeout))
        ) != 0) {
        return false;
    }

    return true;
}

bool SetTcpNoDelay(int fd) {
    int value = 1;

    return ::setsockopt(
               fd,
               IPPROTO_TCP,
               TCP_NODELAY,
               &value,
               static_cast<socklen_t>(sizeof(value))
           ) == 0;
}

int ConnectToServer(const std::string& host, uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    SetSocketTimeout(fd, 5);
    SetTcpNoDelay(fd);

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        ::close(fd);
        return -1;
    }

    if (::connect(
            fd,
            reinterpret_cast<sockaddr*>(&address),
            static_cast<socklen_t>(sizeof(address))
        ) != 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

bool SendAll(int fd, const std::string& data) {
    std::size_t sent_total = 0;

    while (sent_total < data.size()) {
        const ssize_t n = ::send(
            fd,
            data.data() + sent_total,
            data.size() - sent_total,
            MSG_NOSIGNAL
        );

        if (n > 0) {
            sent_total += static_cast<std::size_t>(n);
            continue;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }

        return false;
    }

    return true;
}

bool RecvExact(int fd, std::size_t expected_size, std::string* output) {
    if (output == nullptr) {
        return false;
    }

    output->clear();
    output->resize(expected_size);

    std::size_t received_total = 0;

    while (received_total < expected_size) {
        const ssize_t n = ::recv(
            fd,
            output->data() + received_total,
            expected_size - received_total,
            0
        );

        if (n > 0) {
            received_total += static_cast<std::size_t>(n);
            continue;
        }

        if (n == 0) {
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        return false;
    }

    return true;
}

void RunClient(int client_id,
               const StressConfig& config,
               StressResult* result) {
    const int fd = ConnectToServer(config.host, config.port);
    if (fd < 0) {
        result->failed_clients.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    result->connected_clients.fetch_add(1, std::memory_order_relaxed);

    for (int i = 0; i < config.messages_per_client; ++i) {
        const std::string payload =
            BuildPayload(client_id, i, config.message_size);

        result->total_requests.fetch_add(1, std::memory_order_relaxed);

        if (!SendAll(fd, payload)) {
            result->failed_requests.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        std::string response;
        if (!RecvExact(fd, payload.size(), &response)) {
            result->failed_requests.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        if (response != payload) {
            result->failed_requests.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        result->success_requests.fetch_add(1, std::memory_order_relaxed);
    }

    ::close(fd);
}

}  // namespace

int main(int argc, char* argv[]) {
    StressConfig config;

    if (!ParseConfig(argc, argv, &config)) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::cout << "========== TCP Echo Stress Client ==========\n";
    std::cout << "host = " << config.host << '\n';
    std::cout << "port = " << config.port << '\n';
    std::cout << "client_count = " << config.client_count << '\n';
    std::cout << "messages_per_client = "
              << config.messages_per_client << '\n';
    std::cout << "message_size = " << config.message_size << '\n';

    StressResult result;

    const auto start_time = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(config.client_count));

    for (int i = 0; i < config.client_count; ++i) {
        threads.emplace_back([i, &config, &result]() {
            RunClient(i, config, &result);
        });
    }

    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    const auto end_time = std::chrono::steady_clock::now();

    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(
            end_time - start_time
        ).count();

    const int total_requests =
        result.total_requests.load(std::memory_order_relaxed);

    const int success_requests =
        result.success_requests.load(std::memory_order_relaxed);

    const int failed_requests =
        result.failed_requests.load(std::memory_order_relaxed);

    const double qps =
        elapsed_ms > 0.0
            ? static_cast<double>(success_requests) / (elapsed_ms / 1000.0)
            : 0.0;

    std::cout << "--------------------------------------------\n";
    std::cout << "connected_clients = "
              << result.connected_clients.load() << '\n';

    std::cout << "failed_clients = "
              << result.failed_clients.load() << '\n';

    std::cout << "total_requests = " << total_requests << '\n';
    std::cout << "success_requests = " << success_requests << '\n';
    std::cout << "failed_requests = " << failed_requests << '\n';

    std::cout << "elapsed_ms = " << elapsed_ms << '\n';
    std::cout << "qps = " << qps << '\n';
    std::cout << "============================================\n";

    if (result.failed_clients.load() != 0 || failed_requests != 0) {
        return 1;
    }

    return 0;
}