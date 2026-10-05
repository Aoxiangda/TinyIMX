// Diagnostic only: healthy TCP versus Unix socket connections; PING+SELECT1, no user data.
#include "common/db/MySqlConnection.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>
#include <sys/resource.h>
#include <time.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
// Wrapper is linked ONLY into this diagnostic ELF, never any product binary.
static std::atomic<int> tcp_connects{0}, unix_connects{0}, bad_family{0}, tls_connects{0};
extern "C" MYSQL* __real_mysql_real_connect(MYSQL*,const char*,const char*,const char*,const char*,unsigned int,const char*,unsigned long);
extern "C" MYSQL* __wrap_mysql_real_connect(MYSQL* handle,const char* host,const char* user,const char* password,const char* database,unsigned int port,const char* socket,unsigned long flags) {
    const char* mode=std::getenv("TINYIMX_READONLY_TRANSPORT_MODE");
    if(!mode || (std::strcmp(mode,"tcp") && std::strcmp(mode,"unix")))return nullptr;
    const bool local=std::strcmp(mode,"unix")==0;
    unsigned int protocol=local?MYSQL_PROTOCOL_SOCKET:MYSQL_PROTOCOL_TCP;
    if(mysql_options(handle,MYSQL_OPT_PROTOCOL,&protocol))return nullptr;
    MYSQL* result=__real_mysql_real_connect(handle,local?"localhost":host,user,password,database,local?0:port,local?"/opt/codex/mysql.sock":socket,flags);
    const int saved=errno;
    if(result) {
        sockaddr_storage address{};socklen_t size=sizeof(address);
        if(::getsockname(result->net.fd,reinterpret_cast<sockaddr*>(&address),&size))++bad_family;
        else if(address.ss_family==AF_UNIX && local)++unix_connects;
        else if(address.ss_family==AF_INET && !local)++tcp_connects;
        else ++bad_family;
        // Exact installed Oracle SDK also used by the verified owned fault fixture.
        // Public connection description independently checks the transport type.
        const char* description=mysql_get_host_info(result);
        if(!description || !std::strstr(description,local?"UNIX socket":"TCP/IP"))++bad_family;
        if(mysql_get_ssl_cipher(result)!=nullptr)++tls_connects;
    }
    errno=saved;return result;
}

namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
constexpr int kWorkers = 16;
struct Row {
    std::int64_t caller{}, ping{}, query{}, cpu{}, late{};
    bool ok{};
};
std::int64_t Cpu() {
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value)) return -1;
    return value.tv_sec * 1000000000LL + value.tv_nsec;
}
std::int64_t Us(timeval value) { return value.tv_sec * 1000000LL + value.tv_usec; }
Json Distribution(std::vector<std::int64_t> values) {
    if (values.empty()) return nullptr;
    std::sort(values.begin(), values.end());
    long double total = 0; for (auto value : values) total += value;
    auto p = [&](int n) { return values[(values.size()*n+99)/100-1]/1000000.0; };
    return Json{{"count", values.size()}, {"mean_ms", static_cast<double>(total/values.size()/1000000)},
                {"p50_ms", p(50)}, {"p95_ms", p(95)}, {"p99_ms", p(99)}, {"max_ms", values.back()/1000000.0}};
}
}
int main(int argc, char** argv) {
    try {
        ::umask(0077);if (::getuid()!=1000 || ::geteuid()!=1000 || argc != 7) return 2;
        const std::string mode = argv[1];
        const int rate = std::stoi(argv[2]), seconds = std::stoi(argv[3]);
        if ((mode != "ping-query" && mode != "query") || rate < 1 || rate > 500 || seconds < 1 || seconds > 20) return 2;
        std::ifstream input(argv[5]); Json source; input >> source;
        const auto& mysql = source.at("mysql");
        tinyimx::MySqlConfig config;
        config.enable = mysql.at("enable").get<bool>();
        config.host = argv[4]; config.port = mysql.at("port").get<int>();
        config.database = mysql.at("database").get<std::string>();
        config.user = mysql.at("user").get<std::string>();
        config.password = mysql.at("password").get<std::string>();
        if (!config.enable) return 3;
        std::vector<std::unique_ptr<tinyimx::MySqlConnection>> connections;
        for (int i = 0; i < kWorkers; ++i) {
            auto connection = std::make_unique<tinyimx::MySqlConnection>();
            if (!connection->Connect(config)) return 4;
            connections.push_back(std::move(connection));
        }
        config.password.clear(); source.clear();
        const std::size_t count = static_cast<std::size_t>(rate)*seconds;
        std::vector<Row> rows(count);
        std::atomic<std::size_t> next{0}; std::atomic<int> exceptions{0};
        std::barrier ready(kWorkers+1); Clock::time_point origin;
        std::vector<std::thread> workers;
        for (int i = 0; i < kWorkers; ++i) workers.emplace_back([&, i] {
            auto& connection = *connections[i]; ready.arrive_and_wait();
            for (;;) {
                const auto index = next.fetch_add(1); if (index >= count) break;
                try {
                    const auto due = origin + std::chrono::nanoseconds(index*1000000000LL/rate);
                    std::this_thread::sleep_until(due);
                    auto& row = rows[index];
                    const auto started = Clock::now(); const auto cpu = Cpu();
                    row.late = std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::nanoseconds>(started-due).count());
                    bool healthy = true;
                    if (mode == "ping-query") {
                        auto before = Clock::now(); healthy = connection.Ping();
                        row.ping = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-before).count();
                    }
                    tinyimx::MySqlQueryResult result; auto before = Clock::now();
                    const bool query_ok = healthy && connection.Query("SELECT 1 AS probe_value", &result);
                    row.query = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-before).count();
                    row.ok = query_ok && result.fields.size() == 1 && result.fields[0] == "probe_value" &&
                             result.rows.size() == 1 && result.rows[0].size() == 1 && result.rows[0][0] == "1";
                    const auto end_cpu = Cpu(); row.cpu = cpu >= 0 && end_cpu >= cpu ? end_cpu-cpu : -1;
                    row.caller = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-started).count();
                } catch (...) { ++exceptions; }
            }
        });
        rusage before{}, after{}; getrusage(RUSAGE_SELF, &before); origin = Clock::now(); ready.arrive_and_wait();
        for (auto& worker : workers) worker.join();
        const auto end = Clock::now(); getrusage(RUSAGE_SELF, &after);
        const double elapsed = std::chrono::duration<double>(end-origin).count();
        std::vector<std::int64_t> caller, ping, query, cpu, late; Json raw = Json::array(); int valid = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& row = rows[i]; valid += row.ok;
            caller.push_back(row.caller); ping.push_back(row.ping); query.push_back(row.query);
            if (row.cpu >= 0) cpu.push_back(row.cpu); late.push_back(row.late);
            raw.push_back({i, row.ok, row.caller, row.ping, row.query, row.cpu, row.late});
        }
        const auto user = Us(after.ru_utime)-Us(before.ru_utime), system = Us(after.ru_stime)-Us(before.ru_stime);
        Json output{{"status", valid == static_cast<int>(count) && !exceptions ? "READONLY_CONNECTION_COMPONENT_COMPLETE" : "FAIL"},
                    {"mode", mode}, {"planned", count}, {"select_one_correct", valid}, {"exceptions", exceptions.load()},
                    {"workers", kWorkers}, {"rate_per_second", rate}, {"nominal_seconds", seconds}, {"elapsed_seconds", elapsed},
                    {"transport_mode", std::getenv("TINYIMX_READONLY_TRANSPORT_MODE")},
                    {"connect_families", {{"tcp",tcp_connects.load()},{"unix",unix_connects.load()},{"bad",bad_family.load()},{"tls",tls_connects.load()}}},
                    {"ping_calls", mode == "ping-query" ? count : 0}, {"caller_wall", Distribution(caller)},
                    {"ping_wall", Distribution(ping)}, {"query_wall", Distribution(query)}, {"thread_cpu", Distribution(cpu)},
                    {"scheduled_lateness", Distribution(late)}, {"process_user_cpu_seconds", user/1000000.0},
                    {"process_system_cpu_seconds", system/1000000.0}, {"process_mean_cpu_cores", (user+system)/1000000.0/elapsed},
                    {"voluntary_context_switches", after.ru_nvcsw-before.ru_nvcsw}, {"involuntary_context_switches", after.ru_nivcsw-before.ru_nivcsw},
                    {"raw_columns", {"index", "select_one_ok", "caller_ns", "ping_ns", "query_ns", "thread_cpu_ns", "scheduled_late_ns"}},
                    {"raw", std::move(raw)}, {"performance_acceptance", false},
                    {"limits", "Healthy fresh exclusive16 connections, native-to-Docker path, SELECT1 only. Both transports preserve PING. Ownwrapper forces physicalsocketfamily; TLS negotiated status explicit. No production transport/config change or restart-resilience proof. No10khold/domainSQL/commit; ownprocessCPU excludes MySQL/background; startup connection and close outside rusage. Sharedhost sequential variation and clocks may perturb."}};
        std::ofstream result(argv[6]); result << output.dump() << '\n'; result.close();
        return result && output["status"] == "READONLY_CONNECTION_COMPONENT_COMPLETE" ? 0 : 1;
    } catch (...) { return 5; } // Never print configuration or exception text containing credentials.
}
