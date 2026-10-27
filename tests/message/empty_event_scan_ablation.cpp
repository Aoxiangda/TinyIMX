#include "services/message/local/LocalPrivateStore.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace tinyimx::message;
using namespace tinyimx::message::local;
namespace {
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
}
int main(int argc,char**argv){
    try{
        if(argc!=3)return 2;
        const std::filesystem::path root=argv[1];const std::string variant=argv[2];
        Check(root.is_absolute()&&root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf149-empty-scan-")&&std::filesystem::canonical(root)==root,"owned canonical fixture");
        Check(variant=="off"||variant=="on","exact ablation variant");
        const char* flag=std::getenv("TINYIMX_NATIVE_EMPTY_EVENT_SCAN_GUARD_ENABLE");
        Check(flag&&std::string(flag)==(variant=="on"?"1":"0"),"explicit same-binary switch");
        LocalPrivateStoreOptions options;options.directory=(root/"owner").string();options.storage_identity="empty-scan-ablation:"+root.filename().string();
        options.initial_user_ids={500001,500002};options.first_message_id=7300000000000000ULL;
        options.message_events_topic="codex_perf149_scan_events";options.block_cache_bytes=8*1024*1024;
        std::atomic<bool> hold{false};std::promise<void> entered,release;
        auto released=release.get_future().share();
        LocalPrivateStore store(options,[&](auto point,auto){if(hold&&point==LocalPrivateStore::CommitPoint::kBeforeWrite){entered.set_value();released.wait();}});
        for(unsigned i=0;i<500;++i)Check(store.PersistPrivateMessage(500001,500002,"seed-"+std::to_string(i),1,"scan source").Accepted(),"seed committed");
        while(store.Stats().pending_events){
            const auto page=store.ListPendingEvents(256);Check(page.Succeeded()&&!page.events.empty(),"seed page visible");
            std::vector<std::string> ids;for(const auto&e:page.events)ids.push_back(e.event_id);
            Check(store.MarkEventsPublished(ids).Succeeded(),"published progress committed");
        }
        Check(store.SyncPublicationProgress().Succeeded(),"progress synchronized before empty scans");
        const auto before=store.Stats();const auto wall=std::chrono::steady_clock::now();const auto cpu=std::clock();
        for(unsigned i=0;i<2000;++i){const auto p=store.ListPendingEvents(8);Check(p.Succeeded()&&p.events.empty(),"empty scan equivalence");}
        const double cpu_ms=1000.*(std::clock()-cpu)/CLOCKS_PER_SEC;
        const double wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-wall).count();
        const auto after=store.Stats();const auto scans=after.event_scans_started-before.event_scans_started;
        const auto skipped=after.empty_event_scans_skipped-before.empty_event_scans_skipped;
        Check(variant=="on"?(scans==0&&skipped==2000):(scans==2000&&skipped==0),"actual iterator ablation counters");
        Check(!store.ListPendingEventsAfter(std::string(192,'x'),8).Succeeded(),"empty shortcut still validates cursor length");
        const auto one=store.PersistPrivateMessage(500001,500002,"after-empty",1,"new event");
        auto page=store.ListPendingEvents(8);Check(one.Accepted()&&page.Succeeded()&&page.events.size()==1,"new commit visible after empty fast path");
        const auto event=page.events.front();
        Check(store.MarkEventFailed(event.event_id,"controlled").Succeeded()&&store.ListPendingEvents(8).events.empty(),"quarantine removes pending event");
        Check(store.RequeueFailedEvents({event.event_id}).Succeeded(),"explicit requeue accepted");
        page=store.ListPendingEvents(8);Check(page.Succeeded()&&page.events.size()==1&&page.events.front().body==event.body,"requeue retains exact envelope and wakes queue");
        Check(store.MarkEventsPublished({event.event_id}).Succeeded(),"requeued event completes");
        // A reservation precedes its engine write. It is an upper bound, not a
        // claim that an event is already visible: it must not use the zero path.
        hold=true;
        auto writer=std::async(std::launch::async,[&]{return store.PersistPrivateMessage(500001,500002,"inflight",1,"after reservation");});
        struct ReleaseOnError{std::promise<void>&p;bool done{false};~ReleaseOnError(){if(!done)try{p.set_value();}catch(...){}}} unblock{release};
        const bool ready=entered.get_future().wait_for(std::chrono::seconds(3))==std::future_status::ready;
        const auto pending=store.Stats().pending_events;const auto old_scans=store.Stats().event_scans_started;
        const auto not_yet_visible=store.ListPendingEvents(8);const auto new_scans=store.Stats().event_scans_started;
        release.set_value();unblock.done=true;const auto written=writer.get();hold=false;
        Check(ready&&pending==1&&not_yet_visible.Succeeded()&&not_yet_visible.events.empty()&&new_scans==old_scans+1,"in-flight reservation still takes actual snapshot path");
        Check(written.Accepted()&&store.ListPendingEvents(8).events.size()==1,"reserved commit is not missed");
        Check(store.Shutdown(),"normal shutdown");
        Check(!store.ListPendingEvents(8).Succeeded(),"empty guard never bypasses closed-store fence");
        std::cout<<nlohmann::json{{"status","EMPTY_SCAN_MECHANISM_AND_LIFECYCLE_PASS"},{"variant",variant},
            {"seed_messages",500},{"empty_queries",2000},{"actual_iterator_scans",scans},{"skipped",skipped},
            {"empty_query_process_cpu_ms",cpu_ms},{"empty_query_wall_ms",wall_ms},{"formal_performance",false}}.dump()<<std::endl;
        return 0;
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
