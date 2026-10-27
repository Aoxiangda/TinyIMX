#include "services/message/local/LocalEventRelay.h"
#include <nlohmann/json.hpp>
#include <atomic>
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
#include <unordered_set>
using namespace tinyimx::message;
using namespace tinyimx::message::local;
using namespace tinyimx::eventing;
namespace {
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
template<class F>bool Wait(F&& f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return f();}
LocalPrivateStoreOptions Options(const std::filesystem::path& root,const char* name){
    LocalPrivateStoreOptions o;o.directory=(root/name).string();o.storage_identity="held-filter:"+root.filename().string()+name;
    o.initial_user_ids={500001,500002};o.first_message_id=7600000000000000ULL;o.message_events_topic="codex_perf168_held_events";o.block_cache_bytes=8*1024*1024;return o;
}
void Add(LocalPrivateStore& store,unsigned i){Check(store.PersistPrivateMessage(500001,500002,"held-"+std::to_string(i),1,"quoted \"payload\" \\ and unicode 蜜蜂 "+std::string(128,'x')).Accepted(),"seed commit");}
class BlockPublisher final:public EventPublisher{
public:
    std::mutex mutex;std::condition_variable cv;bool entered{false},released{false};std::map<std::string,unsigned> calls;std::map<std::string,std::string> bodies;
    PublishResult Publish(const PublishMessage& m)override{
        std::unique_lock lock(mutex);if(!entered){entered=true;cv.notify_all();cv.wait(lock,[&]{return released;});}
        ++calls[m.event_id];bodies[m.event_id]=m.body;return{PublishStatus::kPublished,"own:"+m.event_id,""};
    }
    void Release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}
    bool Entered(){std::lock_guard lock(mutex);return entered;}
    std::size_t Count(){std::lock_guard lock(mutex);return calls.size();}
};
}
int main(int argc,char** argv){try{
    if(argc!=3)return 2;const std::filesystem::path root=argv[1];const std::string variant=argv[2];
    Check(root.is_absolute()&&root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf168-held-filter-")&&std::filesystem::canonical(root)==root,"owned canonical fixture");
    Check(variant=="off"||variant=="on","variant");const bool enabled=variant=="on";
    const char* flag=std::getenv("TINYIMX_NATIVE_RELAY_HELD_FILTER_ENABLE");Check(flag&&std::string(flag)==(enabled?"1":"0"),"actual relay flag");
    LocalPrivateStore store(Options(root,"pages"));for(unsigned i=0;i<64;++i)Add(store,i);
    const auto full=store.ListPendingEvents(128);Check(full.Succeeded()&&full.events.size()==64&&full.examined_events==64,"complete baseline page");
    std::unordered_set<std::string> held;for(unsigned i=0;i<56;++i)held.insert(full.events[i].event_id);
    auto page=store.ListPendingEventsAfterExcluding("",32,held);
    Check(page.Succeeded()&&page.events.empty()&&page.examined_events==32&&page.last_examined_event_id==full.events[31].event_id,"entirely held page advances with no fabricated envelope");
    page=store.ListPendingEventsAfterExcluding(page.last_examined_event_id,32,held);
    Check(page.Succeeded()&&page.events.size()==8&&page.examined_events==32&&page.last_examined_event_id==full.events[63].event_id,"bounded second page does not starve new events");
    for(unsigned i=0;i<8;++i)Check(page.events[i].body==full.events[56+i].body&&page.events[i].event_id==full.events[56+i].event_id,"exact event body and order");
    Check(store.ListPendingEventsAfterExcluding(page.last_examined_event_id,32,held).examined_events==0,"exclusive final cursor");
    Check(!store.ListPendingEventsAfterExcluding("",0,held).Succeeded()&&!store.ListPendingEventsAfterExcluding("",257,held).Succeeded()&&!store.ListPendingEventsAfterExcluding(std::string(192,'z'),8,held).Succeeded(),"same argument guards");
    auto too_many=held;for(unsigned i=0;i<257;++i)too_many.insert("unknown"+std::to_string(i));Check(!store.ListPendingEventsAfterExcluding("",8,too_many).Succeeded(),"bounded ownership set");
    constexpr unsigned queries=300;const auto before=store.Stats();const auto cpu=std::clock();const auto wall=std::chrono::steady_clock::now();
    for(unsigned i=0;i<queries;++i){const auto q=enabled?store.ListPendingEventsAfterExcluding("",64,held):store.ListPendingEvents(64);Check(q.Succeeded()&&q.examined_events==64&&q.events.size()==(enabled?8:64),"matched workload");}
    const double cpu_ms=1000.*(std::clock()-cpu)/CLOCKS_PER_SEC,wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-wall).count();const auto after=store.Stats();
    Check(after.event_decode_attempts-before.event_decode_attempts==queries*(enabled?8:64)&&after.held_events_skipped-before.held_events_skipped==(enabled?queries*56:0),"actual decoded and skipped work counts");
    const auto id=full.events[0].event_id;Check(store.MarkEventFailed(id,"owned").Succeeded()&&store.RequeueFailedEvents({id}).Succeeded(),"quarantine and requeue");
    Check(store.ListPendingEventsAfterExcluding("",1,held).events.empty(),"still held after requeue");held.erase(id);page=store.ListPendingEventsAfterExcluding("",1,held);Check(page.events.size()==1&&page.events[0].body==full.events[0].body,"released requeue exact body");
    Check(store.Shutdown()&&!store.ListPendingEventsAfterExcluding("",8,held).Succeeded(),"close fence");
    {LocalPrivateStore reopened(Options(root,"pages"));Check(reopened.ListPendingEvents(128).events.size()==64,"exclusion is not persisted; reopen recovers every event");Check(reopened.Shutdown(),"reopen shutdown");}
    auto options=Options(root,"relay");BlockPublisher publisher;
    {
        LocalPrivateStore source(options);Add(source,0);Add(source,1);LocalEventRelayOptions ro;ro.workers=1;ro.max_held=8;ro.scan_page=2;ro.poll_interval=std::chrono::milliseconds(2);ro.sync_interval=std::chrono::milliseconds(20);
        LocalEventRelay relay(&source,&publisher,ro);Check(relay.Stats().held_filter_enabled==enabled&&relay.Start(),"real relay selection/start");
        struct Release{BlockPublisher& p;~Release(){p.Release();}} release{publisher};
        Check(Wait([&]{return publisher.Entered()&&relay.Stats().held==2;}),"publisher owns the first held page");
        Check(Wait([&]{const auto s=source.Stats();return enabled?s.held_events_skipped>=20:s.event_decode_attempts>=20;}),"real poller uses selected decode path");
        Add(source,2);Check(Wait([&]{return relay.Stats().held>=3;}),"new event after all-held page reaches relay without releasing old work");
        for(unsigned i=3;i<24;++i)Add(source,i);Check(Wait([&]{return relay.Stats().held==8;}),"bounded ownership fills without losing unqueued remainder");
        auto stop=std::async(std::launch::async,[&]{return relay.Shutdown();});Check(stop.wait_for(std::chrono::milliseconds(40))==std::future_status::timeout,"shutdown waits for active publisher");
        publisher.Release();Check(stop.get(),"shutdown completes with durable queued backlog");Check(source.Stats().pending_events==23&&publisher.Count()==1,"active success only; unattempted durable work retained");Check(source.Shutdown(),"source shutdown");
    }
    {
        LocalPrivateStore reopened(options);LocalEventRelayOptions ro;ro.workers=2;ro.max_held=8;ro.scan_page=2;ro.poll_interval=std::chrono::milliseconds(2);
        LocalEventRelay relay(&reopened,&publisher,ro);Check(relay.Start()&&Wait([&]{return publisher.Count()==24&&reopened.Stats().pending_events==0;}),"restart publishes complete remaining backlog");Check(relay.Shutdown()&&reopened.Shutdown(),"final drain");
    }
    {std::lock_guard lock(publisher.mutex);for(const auto& [id,count]:publisher.calls)Check(count==1&&!publisher.bodies.at(id).empty(),"no duplicate publication or empty placeholder");}
    std::cout<<nlohmann::json{{"status","HELD_FILTER_PAGINATION_BODY_REQUEUE_CONCURRENT_OWNERSHIP_SHUTDOWN_RESTART_PASS"},{"variant",variant},{"queries",queries},{"cpu_ms",cpu_ms},{"wall_ms",wall_ms},{"decode_attempts",after.event_decode_attempts-before.event_decode_attempts},{"held_skipped",after.held_events_skipped-before.held_events_skipped},{"index_active",after.pending_event_index_active}}.dump()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
