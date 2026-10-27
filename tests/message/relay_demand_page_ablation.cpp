#include "services/message/local/LocalEventRelay.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
using namespace tinyimx::message;
using namespace tinyimx::message::local;
using namespace tinyimx::eventing;
namespace {
void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class F>bool Wait(F&& f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return f();}
class ControlledPublisher final:public EventPublisher{
public:
    std::mutex mutex;std::condition_variable cv;unsigned permits{0},completed{0};bool released{false};std::map<std::string,unsigned> calls;std::vector<std::string> order;std::map<std::string,std::string> bodies;
    PublishResult Publish(const PublishMessage& m)override{
        std::unique_lock lock(mutex);cv.wait(lock,[&]{return released||completed<permits;});
        ++completed;++calls[m.event_id];order.push_back(m.event_id);bodies[m.event_id]=m.body;return{PublishStatus::kPublished,"own:"+m.event_id,""};
    }
    void Grant(unsigned n){std::lock_guard lock(mutex);permits=n;cv.notify_all();}
    void Release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}
    unsigned Count(){std::lock_guard lock(mutex);return completed;}
};
}
int main(int argc,char** argv){try{
    if(argc!=3)return 2;const std::filesystem::path root=argv[1];const std::string variant=argv[2];const bool enabled=variant=="on";
    Check(root.is_absolute()&&root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf173-demand-page-")&&std::filesystem::canonical(root)==root,"owned canonical fixture");
    Check(variant=="off"||enabled,"variant");
    const char* demand=std::getenv("TINYIMX_NATIVE_RELAY_DEMAND_PAGE_ENABLE");const char* held=std::getenv("TINYIMX_NATIVE_RELAY_HELD_FILTER_ENABLE");const char* index=std::getenv("TINYIMX_NATIVE_PENDING_EVENT_INDEX_ENABLE");
    Check(demand&&std::string(demand)==(enabled?"1":"0")&&held&&std::string(held)=="0"&&index&&std::string(index)=="1","single demand factor; held0/index1 fixed");
    LocalPrivateStoreOptions opt;opt.directory=(root/"owner").string();opt.storage_identity="demand-page:"+root.filename().string();opt.message_events_topic="codex_perf173_demand_events";opt.initial_user_ids={500001,500002};opt.first_message_id=7700000000000000ULL;opt.block_cache_bytes=8*1024*1024;
    constexpr unsigned seeds=400,turns=160;ControlledPublisher publisher;
    std::uint64_t decoded=0,unused=0,enqueued=0,pages=0;double cpu_ms=0,wall_ms=0;
    {
        LocalPrivateStore source(opt);
        for(unsigned i=0;i<seeds;++i)Check(source.PersistPrivateMessage(500001,500002,"demand-"+std::to_string(i),1,"exact body \" \\ 中文 "+std::string(128,'x')).Accepted(),"seed durable event");
        LocalEventRelayOptions ro;ro.workers=1;ro.max_held=8;ro.scan_page=64;ro.poll_interval=std::chrono::milliseconds(2);ro.sync_interval=std::chrono::milliseconds(20);ro.nonblocking_health=true;
        LocalEventRelay relay(&source,&publisher,ro);struct ReleaseOnError{ControlledPublisher& p;~ReleaseOnError(){p.Release();}} release{publisher};
        Check(relay.Stats().demand_page_enabled==enabled&&!relay.Stats().held_filter_enabled,"actual feature flags");
        const auto before=source.Stats();const auto cpu=std::clock();const auto wall=std::chrono::steady_clock::now();Check(relay.Start(),"relay start");
        Check(Wait([&]{return relay.Stats().enqueued_events==8&&relay.Stats().held==8;}),"initial admission fills exactly eight slots");
        for(unsigned n=1;n<=turns;++n){
            publisher.Grant(n);Check(Wait([&]{const auto s=relay.Stats();return s.published==n&&s.enqueued_events==8+n&&s.held==8;}),"one released slot refills without losing or bypassing backlog");
        }
        cpu_ms=1000.*(std::clock()-cpu)/CLOCKS_PER_SEC;wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-wall).count();
        const auto s=relay.Stats();decoded=source.Stats().event_decode_attempts-before.event_decode_attempts;unused=s.page_records_unvisited;enqueued=s.enqueued_events;pages=s.pending_pages;
        Check(enqueued==turns+8&&source.Stats().pending_events==seeds-turns,"no pending event was acknowledged by reading it");
        // Start() validates one real pending envelope before starting threads.
        // Attribute that common startup check separately from poller pages.
        Check(decoded==s.decoded_page_records+1,"all decoded work attributed to pages plus one startup validation");
        if(enabled)Check(decoded==enqueued+1&&unused==0,"demand read decodes exactly accepted slots, no unvisited tail");
        else Check(decoded>=turns*32&&unused==decoded-enqueued-1,"baseline overfetch measured and fully accounted");
        auto stop=std::async(std::launch::async,[&]{return relay.Shutdown();});const auto waiting=stop.wait_for(std::chrono::milliseconds(40));publisher.Release();const bool stopped=stop.get();
        Check(waiting==std::future_status::timeout&&stopped,"shutdown waits for actual active publication");
        Check(publisher.Count()==turns+1&&source.Stats().pending_events==seeds-turns-1,"unattempted queued and unread backlog remains durable");
        Check(source.Shutdown(),"source shutdown");
    }
    {
        LocalPrivateStore reopened(opt);LocalEventRelayOptions ro;ro.workers=1;ro.max_held=8;ro.scan_page=64;ro.poll_interval=std::chrono::milliseconds(2);ro.nonblocking_health=true;
        LocalEventRelay relay(&reopened,&publisher,ro);Check(relay.Start()&&Wait([&]{return publisher.Count()==seeds&&reopened.Stats().pending_events==0;}),"reopen drains every pending event");Check(relay.Shutdown()&&reopened.Shutdown(),"clean final shutdown");
    }
    {std::lock_guard lock(publisher.mutex);Check(publisher.calls.size()==seeds&&std::is_sorted(publisher.order.begin(),publisher.order.end()),"single worker event order retained across restart");for(const auto& [id,n]:publisher.calls)Check(n==1&&!publisher.bodies.at(id).empty(),"one exact nonempty body per event, no duplicates");}
    std::cout<<nlohmann::json{{"status","DEMAND_PAGE_REAL_RELAY_OVERFETCH_ORDER_FULL_QUEUE_SHUTDOWN_RESTART_PASS"},{"variant",variant},{"published_during_measurement",turns},{"enqueued",enqueued},{"decoded",decoded},{"startup_validation_decodes",1},{"unvisited",unused},{"pages",pages},{"cpu_ms",cpu_ms},{"wall_ms",wall_ms}}.dump()<<'\n';return 0;
}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
