#include "services/message/local/LocalEventRelay.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <barrier>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <time.h>
using namespace tinyimx::message;
using namespace tinyimx::message::local;
using namespace tinyimx::eventing;
namespace {
using Clock=std::chrono::steady_clock;
double CpuSeconds(){timespec ts{};if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&ts))throw std::runtime_error("process CPU clock failed");return ts.tv_sec+ts.tv_nsec/1e9;}
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
class ImmediatePublisher final:public EventPublisher {
public:
    std::atomic<unsigned> published{0},duplicates{0};
    std::array<std::atomic<unsigned char>,20000> seen{};
    PublishResult Publish(const PublishMessage& message)override{
        constexpr std::string_view prefix="message.created.v1:";
        const std::string_view id=message.event_id;std::uint64_t mid=0;
        if(!id.starts_with(prefix))return{PublishStatus::kPermanentFailure,"","invalid fixture event"};
        const auto tail=id.substr(prefix.size());const auto parsed=std::from_chars(tail.data(),tail.data()+tail.size(),mid);
        if(parsed.ec!=std::errc{}||parsed.ptr!=tail.data()+tail.size()||mid<7570000000000000ULL||mid>=7570000000020000ULL)
            return{PublishStatus::kPermanentFailure,"","fixture event out of range"};
        if(seen[mid-7570000000000000ULL].exchange(1))++duplicates;
        ++published;return{PublishStatus::kPublished,"isolated-no-network",""};
    }
};
long long P99(std::vector<long long> v){Require(!v.empty(),"missing latency samples");std::sort(v.begin(),v.end());return v[(v.size()*99+99)/100-1];}
}
// This intentionally removes SQL, network, authentication and real MQ to isolate
// local write/confirmation/publication-progress contention. Never report its
// throughput as complete IM capacity or compare it to a full peer protocol.
int main(int argc,char**argv)try{
    if(argc!=2)return 2;const std::filesystem::path root=argv[1];
    Require(root.is_absolute()&&root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf757-")&&std::filesystem::canonical(root)==root,"owned physical ablation root required");
    constexpr unsigned users=20000,messages=20000,writers=16;
    LocalPrivateStoreOptions options;options.directory=(root/"owner").string();options.storage_identity="perf757-"+root.filename().string();
    options.first_message_id=7570000000000000ULL;options.message_events_topic="codex_perf757_local_contention";
    options.block_cache_bytes=128*1024*1024;options.exact_user_locking=true;
    for(unsigned i=0;i<users;++i)options.initial_user_ids.push_back(500001+i);
    LocalPrivateStore store(options);ImmediatePublisher publisher;LocalEventRelayOptions relay_options;
    relay_options.workers=4;relay_options.max_held=64;relay_options.progress_batch_size=1;relay_options.nonblocking_health=true;
    LocalEventRelay relay(&store,&publisher,relay_options);Require(relay.Start(),"relay starts");
    std::barrier ready(writers+1);std::atomic<unsigned> rejected{0},confirmed{0};
    std::vector<std::vector<long long>> persist(writers),confirmation(writers);
    std::vector<std::thread> threads;std::atomic<bool> sampling{true};std::atomic<std::uint64_t> peak_pending{0};
    std::jthread sampler([&](std::stop_token stop){while(sampling&&!stop.stop_requested()){auto n=store.Stats().pending_events;auto old=peak_pending.load();while(n>old&&!peak_pending.compare_exchange_weak(old,n)){}std::this_thread::sleep_for(std::chrono::milliseconds(10));}});
    for(unsigned worker=0;worker<writers;++worker)threads.emplace_back([&,worker]{
        persist[worker].reserve(messages/writers+1);confirmation[worker].reserve(messages/writers+1);ready.arrive_and_wait();
        for(unsigned i=worker;i<messages;i+=writers){
            const std::uint64_t from=500001+i,to=500001+(i^1U);const auto cid="local-ablation-"+std::to_string(i);
            auto t=Clock::now();auto result=store.PersistPrivateMessage(from,to,cid,1,std::string(128,'x'));
            persist[worker].push_back(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-t).count());
            if(!result.Accepted()){++rejected;continue;}
            t=Clock::now();auto ack=store.ConfirmReceiver(result.message_id,to);
            confirmation[worker].push_back(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-t).count());
            if(ack.Succeeded()&&ack.affected_rows==1)++confirmed;else ++rejected;
        }
    });
    const auto started=Clock::now();const auto cpu_start=CpuSeconds();ready.arrive_and_wait();
    for(auto& thread:threads)thread.join();const auto foreground_end=Clock::now();
    const auto deadline=Clock::now()+std::chrono::seconds(60);
    while(store.Stats().pending_events&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool drained=store.Stats().pending_events==0;const bool healthy=relay.Shutdown();
    const auto ended=Clock::now();const auto cpu_end=CpuSeconds();sampling=false;sampler.join();
    Require(!rejected&&confirmed==messages&&publisher.published==messages&&!publisher.duplicates&&drained&&healthy,"all raw messages confirmed and all publication metadata drained once");
    Require(std::all_of(publisher.seen.begin(),publisher.seen.end(),[](const auto& n){return n.load()==1;}),"every original event published exactly once");
    const auto stats=store.Stats();Require(stats.message_commits==messages&&stats.pending_events==0&&store.ListFailedEvents(256).events.empty(),"complete native counters and no failed event");
    auto actors=store.ListMessageActors();Require(actors.Succeeded()&&actors.actors.size()==users,"all20000 fixture actors participated");
    std::vector<long long> p,c;for(auto& v:persist)p.insert(p.end(),v.begin(),v.end());for(auto& v:confirmation)c.insert(c.end(),v.begin(),v.end());
    const auto seconds=std::chrono::duration<double>(ended-started).count();
    std::cout<<"{\"scope\":\"isolated_local_engine_contention_not_full_IM_QPS\",\"messages\":"<<messages
      <<",\"users\":"<<users<<",\"writers\":"<<writers<<",\"publish_workers\":4,\"event_progress_shards\":"<<(store.EventProgressShardsEnabled()?"true":"false")
      <<",\"full_drain_seconds\":"<<seconds<<",\"foreground_seconds\":"<<std::chrono::duration<double>(foreground_end-started).count()
      <<",\"local_complete_ops_s\":"<<messages/seconds<<",\"process_CPU_seconds\":"<<cpu_end-cpu_start
      <<",\"process_busy_cores\":"<<(cpu_end-cpu_start)/seconds<<",\"peak_pending_events\":"<<peak_pending
      <<",\"persist_P99_us\":"<<P99(p)<<",\"confirm_P99_us\":"<<P99(c)<<",\"engine_WAL_syncs\":"<<stats.engine_wal_syncs
      <<",\"confirmed\":"<<confirmed<<",\"published\":"<<publisher.published<<",\"rejected\":"<<rejected<<",\"pending_events\":0}"<<std::endl;
    Require(store.Shutdown(),"final store shutdown");return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
