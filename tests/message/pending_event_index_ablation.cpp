#include "services/message/local/LocalPrivateStore.h"
#include <nlohmann/json.hpp>
#include <algorithm>
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
namespace {void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}}
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;
        const std::filesystem::path root=argv[1];const std::string variant=argv[2];
        Check(root.is_absolute()&&root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf155-pending-index-")&&std::filesystem::canonical(root)==root,"owned canonical fixture");
        Check(variant=="off"||variant=="on","exact variant");
        const char* flag=std::getenv("TINYIMX_NATIVE_PENDING_EVENT_INDEX_ENABLE");
        const char* empty=std::getenv("TINYIMX_NATIVE_EMPTY_EVENT_SCAN_GUARD_ENABLE");
        Check(flag&&std::string(flag)==(variant=="on"?"1":"0")&&empty&&std::string(empty)=="0","one switch; empty optimization disabled");
        LocalPrivateStoreOptions options;options.directory=(root/"owner").string();
        options.storage_identity="pending-index:"+root.filename().string();options.initial_user_ids={500001,500002,500003,500004};
        options.exact_user_locking=true;options.first_message_id=7500000000000000ULL;
        options.message_events_topic="codex_perf155_index_events";options.block_cache_bytes=8*1024*1024;
        std::atomic<bool> hold{false};std::atomic<std::uint64_t> paused_mid{0};
        std::promise<void> entered,release;auto released=release.get_future().share();
        LocalPrivateStore store(options,[&](auto point,auto mid){
            if(hold&&point==LocalPrivateStore::CommitPoint::kBeforeWrite){paused_mid=mid;entered.set_value();released.wait();}
        });
        Check(store.Stats().pending_event_index_active==(variant=="on"),"actual index selection");
        constexpr unsigned seeds=4000,queries=2000;
        for(unsigned i=0;i<seeds;++i)Check(store.PersistPrivateMessage(500001,500002,"seed-"+std::to_string(i),1,"same pending workload").Accepted(),"seed commit");
        std::vector<std::string> all;std::string cursor;PendingLocalEvent retained;
        for(;;){
            const auto page=store.ListPendingEventsAfter(cursor,256);Check(page.Succeeded(),"seed page");if(page.events.empty())break;
            for(const auto& e:page.events){all.push_back(e.event_id);retained=e;cursor=e.event_id;}
        }
        Check(all.size()==seeds&&std::is_sorted(all.begin(),all.end()),"complete sorted corpus");all.pop_back();
        for(std::size_t i=0;i<all.size();i+=256){
            const std::vector<std::string> ids(all.begin()+i,all.begin()+std::min(all.size(),i+256));
            Check(store.MarkEventsPublished(ids).Succeeded(),"publish seed entries");
        }
        Check(store.SyncPublicationProgress().Succeeded()&&store.Stats().pending_events==1,"one real event remains after deleted history");
        const auto before=store.Stats();const auto wall=std::chrono::steady_clock::now();const auto cpu=std::clock();
        for(unsigned i=0;i<queries;++i){
            const auto page=store.ListPendingEvents(8);
            Check(page.Succeeded()&&page.events.size()==1&&page.events[0].event_id==retained.event_id&&page.events[0].body==retained.body,"exact remaining envelope");
        }
        const double cpu_ms=1000.*(std::clock()-cpu)/CLOCKS_PER_SEC;
        const double wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-wall).count();
        const auto after=store.Stats();const auto scans=after.event_scans_started-before.event_scans_started,indexed=after.pending_index_queries-before.pending_index_queries;
        Check(variant=="on"?(indexed==queries&&scans==0):(indexed==0&&scans==queries),"actual path counters");
        Check(store.ListPendingEventsAfter(retained.event_id,8).events.empty(),"exclusive cursor");
        Check(!store.ListPendingEventsAfter(std::string(192,'x'),8).Succeeded(),"cursor validation retained");
        Check(store.MarkEventFailed(retained.event_id,"controlled").Succeeded()&&store.ListPendingEvents(8).events.empty(),"quarantine removes key");
        Check(store.RequeueFailedEvents({retained.event_id}).Succeeded(),"requeue");
        auto page=store.ListPendingEvents(8);Check(page.Succeeded()&&page.events.size()==1&&page.events[0].body==retained.body,"requeue exact body");
        Check(store.MarkEventsPublished({retained.event_id}).Succeeded(),"finish remaining event");

        // A later unrelated actor commits while an earlier reserved MID waits.
        hold=true;auto writer=std::async(std::launch::async,[&]{return store.PersistPrivateMessage(500001,500002,"paused",1,"earlier reservation");});
        struct ReleaseOnError{std::promise<void>& p;bool done{false};~ReleaseOnError(){if(!done)try{p.set_value();}catch(...){}}} unblock{release};
        Check(entered.get_future().wait_for(std::chrono::seconds(3))==std::future_status::ready&&store.Stats().pending_events==1,"prepared reservation");
        page=store.ListPendingEvents(8);Check(page.Succeeded()&&page.events.empty(),"prepared key is not committed");
        const auto guessed="message.created.v1:"+std::to_string(paused_mid.load());
        const auto premature=store.MarkEventsPublished({guessed});Check(premature.Succeeded()&&premature.affected_rows==0,"missing durable key cannot erase a future entry");
        hold=false;const auto later=store.PersistPrivateMessage(500003,500004,"later",1,"later commit first");Check(later.Accepted(),"unrelated write commits");
        page=store.ListPendingEvents(8);Check(page.Succeeded()&&page.events.size()==1&&page.events[0].event_id=="message.created.v1:"+std::to_string(later.message_id),"later commit visible before earlier reservation");
        release.set_value();unblock.done=true;const auto earlier=writer.get();Check(earlier.Accepted()&&earlier.message_id==paused_mid.load(),"earlier finishes");
        page=store.ListPendingEvents(8);Check(page.Succeeded()&&page.events.size()==2&&page.events[0].event_id==guessed,"earlier commit not missed after later key was read");
        Check(store.Shutdown(),"shutdown with two durable events");Check(!store.ListPendingEvents(8).Succeeded(),"closed fence");

        // A lower new budget must not strand a previously valid backlog.
        auto small=options;small.max_pending_events=1;
        {
            LocalPrivateStore reopened(small);Check(!reopened.Stats().pending_event_index_active,"bounded fallback");
            page=reopened.ListPendingEvents(8);Check(page.Succeeded()&&page.events.size()==2,"old backlog drainable");
            Check(!reopened.PersistPrivateMessage(500001,500002,"over-limit",1,"reject").Accepted(),"admission budget unchanged");
            std::vector<std::string> ids;for(const auto& e:page.events)ids.push_back(e.event_id);Check(reopened.MarkEventsPublished(ids).Succeeded(),"drain fallback");
            Check(reopened.PersistPrivateMessage(500001,500002,"after-drain",1,"admit").Accepted(),"admission recovers");Check(reopened.Shutdown(),"fallback shutdown");
        }
        {
            LocalPrivateStore reopened(options);page=reopened.ListPendingEvents(8);
            Check(reopened.Stats().pending_event_index_active==(variant=="on")&&page.Succeeded()&&page.events.size()==1,"startup restores remaining E keys");
            Check(reopened.MarkEventsPublished({page.events[0].event_id}).Succeeded()&&reopened.ListPendingEvents(8).events.empty(),"recovered event completes");Check(reopened.Shutdown(),"recovered shutdown");
        }
        std::cout<<nlohmann::json{{"status","PENDING_EVENT_INDEX_MECHANISM_CONCURRENCY_REQUEUE_REOPEN_AND_BUDGET_PASS"},{"variant",variant},
            {"seed_messages",seeds},{"retained_pending",1},{"queries",queries},{"iterator_scans",scans},{"indexed_queries",indexed},
            {"process_cpu_ms",cpu_ms},{"wall_ms",wall_ms},{"formal_performance",false}}.dump()<<std::endl;
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
