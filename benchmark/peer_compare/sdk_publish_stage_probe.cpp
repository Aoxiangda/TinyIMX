// Finite internal-component diagnostic; uses the real application publisher.
#include "services/eventing/rocketmq/RocketMQProducer.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <latch>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <time.h>
#include <sys/resource.h>

namespace mq = tinyimx::eventing::rocketmq_transport;
namespace ev = tinyimx::eventing;
struct Row {
    long long begin_ns{}, end_ns{}, utc_begin_ns{}, utc_end_ns{};
    int status{-1};
    std::string receipt;
};
long long Now(clockid_t clock) {
    timespec t{};
    if (clock_gettime(clock, &t)) throw std::runtime_error("clock_gettime failed");
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}
void Require(bool good, const char* why) { if (!good) throw std::runtime_error(why); }
struct Window { long long begin_ns, end_ns, cpu_ns, utc_before_ns, utc_after_ns; };

int main(int argc, char** argv) try {
    Require(argc == 5, "endpoint topic label output-directory required");
    const std::string endpoint=argv[1], topic=argv[2], label=argv[3], output=argv[4];
    Require(endpoint=="tinyimx-m21-rocketmq-proxy-1:8081", "owned local endpoint only");
    Require(std::regex_match(label,std::regex("pilot|off1|onn1|onn2|off2")), "owned label only");
    Require(topic=="codex_perf841_events_20261024_r1_"+label, "owned topic only");
    Require(output=="/out", "fixed output mount required");
    {std::ifstream maps("/proc/self/maps");std::string line;bool found=false;
     while(std::getline(maps,line))if(line.find("/probe/sdk/librocketmq.so")!=std::string::npos)found=true;
     Require(found,"pinned SDK shadow must actually be mapped");}
    const bool pilot=label=="pilot";
    const unsigned workers=label.starts_with("onn")?16:4;
    const unsigned warm=pilot?8:1000, measured=pilot?32:10000, total=warm+measured;
    mq::RocketMQProducerOptions options;
    options.endpoint=endpoint; options.topic=topic; options.request_timeout_ms=3000;
    mq::RocketMQProducer producer(options);
    Require(producer.Start(), "real production publisher failed to start");
    std::vector<ev::PublishMessage> messages;
    messages.reserve(total);
    for(unsigned i=0;i<total;++i) {
        const auto key="PERF841:"+label+":"+std::to_string(i);
        auto body=key+"|"; body.append(512-body.size(),'x');
        messages.push_back({key,topic,"message.created.v1",key,std::move(body)});
    }
    std::vector<Row> rows(total);
    auto execute=[&](unsigned begin,unsigned end) {
        std::atomic<unsigned> next{begin};
        std::latch ready(workers), start(1);
        std::vector<std::thread> threads;
        for(unsigned j=0;j<workers;++j) threads.emplace_back([&] {
            ready.count_down(); start.wait();
            for(;;) {
                const auto i=next.fetch_add(1,std::memory_order_relaxed);
                if(i>=end)break;
                auto& row=rows[i];
                row.utc_begin_ns=Now(CLOCK_REALTIME); row.begin_ns=Now(CLOCK_MONOTONIC);
                auto result=producer.Publish(messages[i]);
                row.end_ns=Now(CLOCK_MONOTONIC); row.utc_end_ns=Now(CLOCK_REALTIME);
                row.status=static_cast<int>(result.status); row.receipt=std::move(result.broker_message_id);
            }
        });
        ready.wait();
        Window w{}; w.utc_before_ns=Now(CLOCK_REALTIME);w.begin_ns=Now(CLOCK_MONOTONIC);
        const auto cpu=Now(CLOCK_PROCESS_CPUTIME_ID);start.count_down();
        for(auto& t:threads)t.join();
        w.cpu_ns=Now(CLOCK_PROCESS_CPUTIME_ID)-cpu;w.end_ns=Now(CLOCK_MONOTONIC);w.utc_after_ns=Now(CLOCK_REALTIME);
        return w;
    };
    const auto warm_window=execute(0,warm);
    for(unsigned i=0;i<warm;++i) Require(rows[i].status==0&&!rows[i].receipt.empty(),"warmup publication failure");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const auto window=execute(warm,total);
    // Ledger IO and exact statistics are deliberately after the API window.
    std::ofstream file(output+"/api.tsv",std::ios::binary);
    Require(file.good(), "new ledger not writable");
    file<<"index\twarm\tbegin_ns\tend_ns\tutc_begin_ns\tutc_end_ns\tstatus\treceipt\n";
    std::vector<long long> latency;
    unsigned failures=0;
    for(unsigned i=0;i<total;++i) {
        const auto&r=rows[i];
        Require(r.receipt.find_first_of("\t\r\n")==std::string::npos,"unsafe receipt representation");
        file<<i<<'\t'<<(i<warm)<<'\t'<<r.begin_ns<<'\t'<<r.end_ns<<'\t'<<r.utc_begin_ns<<'\t'<<r.utc_end_ns<<'\t'<<r.status<<'\t'<<r.receipt<<'\n';
        if(i>=warm) {latency.push_back(r.end_ns-r.begin_ns);failures+=r.status!=0||r.receipt.empty();}
    }
    file.close();Require(file.good(),"ledger write failed");
    std::sort(latency.begin(),latency.end());
    long double sum=0;for(auto v:latency)sum+=v;
    rusage usage{};Require(getrusage(RUSAGE_SELF,&usage)==0,"resource query failed");
    std::cout<<"PERF841_RESULT {\"label\":\""<<label<<"\",\"workers\":"<<workers
        <<",\"warm\":"<<warm<<",\"measured\":"<<measured<<",\"all_rows\":"<<total
        <<",\"failed\":"<<failures<<",\"window_begin_ns\":"<<window.begin_ns<<",\"window_end_ns\":"<<window.end_ns
        <<",\"utc_before_ns\":"<<window.utc_before_ns<<",\"utc_after_ns\":"<<window.utc_after_ns
        <<",\"process_cpu_ns\":"<<window.cpu_ns<<",\"mean_api_ms\":"<<double(sum/measured/1e6)
        <<",\"p99_api_ms\":"<<latency[static_cast<size_t>(std::ceil(measured*.99))-1]/1e6
        <<",\"completed_per_second\":"<<measured*1e9/double(window.end_ns-window.begin_ns)
        <<",\"process_mean_busy_cores\":"<<double(window.cpu_ns)/(window.end_ns-window.begin_ns)
        <<",\"process_peak_rss_kib\":"<<usage.ru_maxrss<<"}"<<std::endl;
    producer.Stop();
    return failures?1:0;
}catch(const std::exception&e){std::cerr<<"PERF841_FAILED "<<e.what()<<std::endl;return 2;}
