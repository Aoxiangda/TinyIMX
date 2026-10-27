#include "services/message/local/LocalPrivateStore.h"
#include <atomic>
#include <barrier>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace tinyimx::message;
using namespace tinyimx::message::local;
namespace {
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);std::cout<<"PASS "<<text<<'\n';}
LocalPrivateStoreOptions Options(const std::filesystem::path& root){
    LocalPrivateStoreOptions o;o.directory=(root/"owner").string();o.storage_identity="perf754-event-state:"+root.filename().string();
    o.first_message_id=7540000000000000ULL;o.message_events_topic="codex_perf754_event_state";
    o.initial_user_ids={500001,500002};o.block_cache_bytes=8*1024*1024;o.exact_user_locking=true;return o;
}
std::vector<std::string> Fill(LocalPrivateStore& s,unsigned n,unsigned base){
    std::vector<std::string> ids;
    for(unsigned i=0;i<n;++i){auto result=s.PersistPrivateMessage(500001,500002,"event-state-"+std::to_string(base+i),1,"owned event state");
        Check(result.Accepted(),"message and event committed before state race");ids.push_back("message.created.v1:"+std::to_string(result.message_id));}
    return ids;
}
template<class Fn>void Race(unsigned workers,Fn fn){
    std::barrier start(static_cast<std::ptrdiff_t>(workers));std::vector<std::thread> threads;
    for(unsigned i=0;i<workers;++i)threads.emplace_back([&,i]{start.arrive_and_wait();fn(i);});
    for(auto& thread:threads)thread.join();
}
}
int main(int argc,char**argv)try{
    if(argc!=3||std::string(argv[2])!="contracts")return 2;
    const std::filesystem::path root=argv[1];
    Check(root.is_absolute()&&(root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf754-")||root.string().starts_with("/mnt/tinyimx-benchmark/perf943-contracts-"))&&std::filesystem::canonical(root)==root,"owned physical event-state test root");
    auto options=Options(root);unsigned messages=0;
    {
        LocalPrivateStore s(options);const char* flag=std::getenv("TINYIMX_NATIVE_EVENT_PROGRESS_SHARDS_ENABLE");
        const bool expected=flag&&flag[0]=='1'&&flag[1]=='\0';
        Check(s.EventProgressShardsEnabled()==expected,"actual store samples declared global or striped event-state policy");
        auto ids=Fill(s,64,messages);messages+=64;std::atomic<std::uint64_t> removed{0};std::atomic<unsigned> bad{0};
        Race(8,[&](unsigned worker){
            for(unsigned repeat=0;repeat<4;++repeat)for(unsigned i=worker;i<ids.size();i+=8){
                auto r=s.MarkEventsPublished({ids[i],ids[(i+1)%ids.size()],ids[i]});
                if(!r.Succeeded())++bad;removed+=r.affected_rows;
            }
        });
        Check(!bad&&removed==64&&s.Stats().pending_events==0&&s.ListPendingEvents(256).events.empty(),"overlapping and duplicate publication batches remove each event exactly once");
        ids=Fill(s,32,messages);messages+=32;removed=0;
        Race(8,[&](unsigned worker){
            for(unsigned i=worker/2;i<ids.size();i+=4){
                auto r=worker%2?s.MarkEventFailed(ids[i],"owned-race"):s.MarkEventsPublished({ids[i]});
                if(!r.Succeeded())++bad;removed+=r.affected_rows;
            }
        });
        Check(!bad&&removed==32&&s.Stats().pending_events==0,"publication versus quarantine has one winning transition for each identical event");
        auto failed=s.ListFailedEvents(256);Check(failed.Succeeded(),"quarantined state remains readable after race");
        std::vector<std::string> failed_ids;for(const auto& event:failed.events)failed_ids.push_back(event.event.event_id);
        auto requeued=s.RequeueFailedEvents(failed_ids);
        Check(requeued.Succeeded()&&requeued.affected_rows==failed_ids.size()&&s.Stats().pending_events==failed_ids.size(),"requeue restores exactly the quarantined subset without duplicating pending budget");
        auto final=s.MarkEventsPublished(ids);
        Check(final.Succeeded()&&final.affected_rows==failed_ids.size()&&s.Stats().pending_events==0&&s.ListFailedEvents(256).events.empty(),"replayed subset publishes exactly once across all event-state methods");
        ids=Fill(s,32,messages);messages+=32;
        for(const auto& id:ids)Check(s.MarkEventFailed(id,"owned-requeue-race").affected_rows==1,"seed failed state for overlapping requeue race");
        removed=0;std::atomic<unsigned> requeue_count{0};
        Race(2,[&](unsigned worker){
            if(worker){auto r=s.RequeueFailedEvents(ids);if(!r.Succeeded())++bad;requeue_count=r.affected_rows;}
            else{auto r=s.MarkEventsPublished(ids);if(!r.Succeeded())++bad;removed=r.affected_rows;}
        });
        Check(!bad&&requeue_count==32&&(removed==0||removed==32)&&s.Stats().pending_events==32-removed&&s.ListFailedEvents(256).events.empty(),"overlapping multi-key requeue/publication batches keep an atomic linearizable result");
        Check(s.MarkEventsPublished(ids).Succeeded()&&s.Stats().pending_events==0,"remaining requeued events drain without deadlock or counter underflow");
        ids=Fill(s,1,messages);++messages;
        const auto invalid=s.MarkEventsPublished({ids.front(),std::string("bad\0id",6)});
        Check(!invalid.Succeeded()&&invalid.status==MessageApplicationStatus::kInvalidArgument&&s.Stats().pending_events==1,"invalid batch leaves its valid event intact in both lock policies");
        Check(s.MarkEventsPublished(ids).affected_rows==1&&s.SyncPublicationProgress().Succeeded(),"final publication progress synchronizes");
        Check(s.CountPending(500002).count==messages&&s.ReadUnreadSnapshot(500002,500001).total_unread==messages,"publication metadata races never alter durable messages, receiver state or unread truth");
        Check(s.Shutdown(),"owner shuts down cleanly after all transition races");
    }
    {
        LocalPrivateStore reopened(options);
        Check(reopened.Stats().pending_events==0&&reopened.ListFailedEvents(256).events.empty()&&reopened.CountPending(500002).count==messages,"reopen retains empty outbox and all original messages after sync");
    }
    std::cout<<"NATIVE_EVENT_PROGRESS_STATE_CONCURRENCY_PASS messages="<<messages<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
