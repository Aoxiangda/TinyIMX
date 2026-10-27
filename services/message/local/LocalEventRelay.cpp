#include "services/message/local/LocalEventRelay.h"
#include "services/message/local/NativePersistencePhaseTrace.h"
#include "services/message/local/NativePublicationPhaseTrace.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
namespace tinyimx::message::local {
class LocalEventRelay::Impl {
public:
    using Clock=std::chrono::steady_clock;
    struct Retry {unsigned attempts{0};Clock::time_point due;};
    LocalPrivateStore* store;
    eventing::EventPublisher* publisher;
    LocalEventRelayOptions options;
    std::shared_ptr<LocalEventSignal> signal;
    std::mutex lifecycle;
    mutable std::mutex mutex;
    std::condition_variable work;
    std::condition_variable progress_ready;
    std::deque<PendingLocalEvent> queue;
    // A success still owns its held slot until its local progress write has
    // finished. This queue therefore shares max_held with queued/in-flight
    // publications; it cannot grow independently of admission.
    std::deque<PendingLocalEvent> completed;
    std::unordered_set<std::string> held;
    std::unordered_map<std::string,Retry> retries;
    std::vector<std::thread> workers;
    std::thread poller;
    std::thread progress_writer;
    LocalEventRelayStats stats;
    std::atomic<bool> healthy{false};
    bool ever_started{false},owns_store{false},detached_progress{false},progress_stopping{false};
    bool dirty_progress_sync{false},capability_take{false};
    Impl(LocalPrivateStore*s,eventing::EventPublisher*p,LocalEventRelayOptions o):store(s),publisher(p),options(std::move(o)){
        if(!s||!p||options.workers<1||options.workers>16||options.max_held<options.workers||options.max_held>256||
            !options.scan_page||options.scan_page>256||options.max_retry_entries<options.max_held||options.max_retry_entries>1000000||
            !options.progress_batch_size||options.progress_batch_size>16||options.progress_batch_size>options.max_held||
            !options.max_attempts||options.max_attempts>64||options.retry_base.count()<1||options.retry_max<options.retry_base||options.retry_max.count()>30000||
            options.poll_interval.count()<1||options.poll_interval.count()>1000||options.sync_interval.count()<1||options.sync_interval.count()>5000)
            throw std::invalid_argument("invalid bounded native event relay options");
        signal=store->EventSignal();
        const char* filter=std::getenv("TINYIMX_NATIVE_RELAY_HELD_FILTER_ENABLE");
        stats.held_filter_enabled=filter&&filter[0]=='1'&&filter[1]=='\0';
        const char* demand=std::getenv("TINYIMX_NATIVE_RELAY_DEMAND_PAGE_ENABLE");
        stats.demand_page_enabled=demand&&demand[0]=='1'&&demand[1]=='\0';
        const char* detached=std::getenv("TINYIMX_NATIVE_RELAY_DETACHED_PROGRESS_ENABLE");
        detached_progress=detached&&detached[0]=='1'&&detached[1]=='\0';
        const char* dirty=std::getenv("TINYIMX_NATIVE_PUBLICATION_DIRTY_SYNC_ENABLE");
        dirty_progress_sync=dirty&&dirty[0]=='1'&&dirty[1]=='\0';
        const char* take=std::getenv("TINYIMX_NATIVE_RELAY_CAPABILITY_TAKE_ENABLE");
        capability_take=take&&take[0]=='1'&&take[1]=='\0';
        if(capability_take)LOG_INFO("native relay transport bounded take, enabled=1, progress_batch_size="
            <<options.progress_batch_size<<", max_held="<<options.max_held<<", detached_progress="<<detached_progress);
    }
    void Abort(const char* reason)noexcept{
        try{
            {
                std::lock_guard lock(mutex);
                healthy.store(false,std::memory_order_release);
                stats.failed=true;stats.running=false;stats.stopping=true;
                // An allocation failure in optional text must never bypass
                // the fence, wakeups or fatal callback.
                try{if(options.before_error_assignment)options.before_error_assignment();stats.error=reason;}
                catch(...){stats.error.clear();}
                for(const auto&e:queue)held.erase(e.event_id);queue.clear();
                for(const auto&e:completed)held.erase(e.event_id);completed.clear();
            }
            work.notify_all();progress_ready.notify_all();signal->Notify();
            if(options.on_fatal)try{options.on_fatal();}catch(...){}
        }catch(...){}
    }
    bool Stopping()const{std::lock_guard lock(mutex);return stats.stopping;}
    void Quarantine(const PendingLocalEvent& event,const char* reason){
        auto result=store->MarkEventFailed(event.event_id,reason);
        if(!result.Succeeded()||result.affected_rows!=1){Abort("quarantine_write_failed");return;}
        std::lock_guard lock(mutex);++stats.quarantined;retries.erase(event.event_id);
    }
    void ApplyResult(const PendingLocalEvent& event,const eventing::PublishResult& result,
                     std::vector<std::string>& published){
        if(result.Published()){
            if(!detached_progress&&options.after_success_before_progress)options.after_success_before_progress(event);
            published.push_back(event.event_id);return;
        }
        if(result.status==eventing::PublishStatus::kPermanentFailure){
            {std::lock_guard lock(mutex);++stats.permanent_failures;}
            Quarantine(event,"publisher_permanent_failure");return;
        }
        bool exhausted=false,full=false;
        {
            std::lock_guard lock(mutex);++stats.retryable_failures;
            auto found=retries.find(event.event_id);
            if(found==retries.end()){
                full=retries.size()>=options.max_retry_entries;
                if(!full)found=retries.emplace(event.event_id,Retry{}).first;
            }
            if(!full){
                exhausted=++found->second.attempts>=options.max_attempts;
                const auto scale=1ULL<<std::min(found->second.attempts-1,20U);
                const auto delay=std::min<std::uint64_t>(options.retry_max.count(),options.retry_base.count()*scale);
                found->second.due=Clock::now()+std::chrono::milliseconds(delay);
            }
        }
        if(full){Abort("retry_tracking_capacity_reached");return;}
        if(exhausted)Quarantine(event,"publisher_retry_limit");
    }
    bool Supported(const PendingLocalEvent& event)const{
        return event.topic==store->MessageEventsTopic()&&
            (event.tag=="message.created.v1"||event.tag=="dialog.read_advanced.v1");
    }
    void Process(const PendingLocalEvent& event,std::vector<std::string>& published){
        if(!Supported(event)){Quarantine(event,"unsupported_native_event");return;}
        eventing::PublishResult result;
        try{
            NativePublicationPhaseTrace trace(NativePublicationPhaseTrace::Publish,event.event_id,1);
            result=publisher->Publish({event.event_id,event.topic,event.tag,event.message_key,event.body});
            trace.Stop();trace.Result(static_cast<int>(result.status),1);
        }
        catch(...){result.status=eventing::PublishStatus::kRetryableFailure;}
        ApplyResult(event,result,published);
    }
    void ProcessBatch(const std::vector<PendingLocalEvent>& events,
                      std::vector<std::string>& published){
        std::vector<const PendingLocalEvent*> selected;
        std::vector<eventing::PublishMessage> messages;
        selected.reserve(events.size());messages.reserve(events.size());
        for(const auto& event:events){
            if(Stopping())return;
            if(!Supported(event)){Quarantine(event,"unsupported_native_event");continue;}
            selected.push_back(&event);
            messages.push_back({event.event_id,event.topic,event.tag,event.message_key,event.body});
        }
        if(messages.empty()||Stopping())return;
        std::vector<eventing::PublishResult> results;
        try{
            NativePublicationPhaseTrace trace(NativePublicationPhaseTrace::Publish,selected.front()->event_id,messages.size());
            results=publisher->PublishBatch(messages);
            trace.Stop();
            if(NativePublicationPhaseTrace::Enabled()){
                auto status=eventing::PublishStatus::kRetryableFailure;
                if(results.size()==messages.size()){
                    const auto failed=std::find_if(results.begin(),results.end(),
                        [](const auto& result){return !result.Published();});
                    status=failed==results.end()?eventing::PublishStatus::kPublished:failed->status;
                }
                // A malformed/partial result is not a successful batch span.
                // Units represent submitted inputs, never a malformed reply size.
                trace.Result(static_cast<int>(status),messages.size());
            }
        }catch(...){results.clear();}
        if(results.size()!=selected.size()){
            // A malformed response or thrown call confirms nothing. The same
            // event IDs remain durable and retain their bounded retry state.
            results.assign(selected.size(),eventing::PublishResult{});
        }
        // An in-flight call owns every submitted event through its progress
        // marker. Shutdown drains this call, and submits no subsequent group.
        for(std::size_t i=0;i<selected.size();++i)
            ApplyResult(*selected[i],results[i],published);
    }
    void Worker()noexcept{
        std::vector<PendingLocalEvent> events;
        std::vector<std::string> published;
        try{events.reserve(options.progress_batch_size);published.reserve(options.progress_batch_size);}
        catch(...){Abort("event_worker_allocation_failed");return;}
        for(;;){
            events.clear();published.clear();
            bool transferred=false;
            std::size_t take=options.progress_batch_size,transport_capability=0;
            const bool independent_take=capability_take&&detached_progress;
            if(independent_take){
                transport_capability=publisher->MaxBatchMessages();
                // Progress batching is independent of transport batching.
                // Do not reserve sixteen scalar RPCs in one worker before
                // returning any successful ID to the bounded progress writer.
                take=transport_capability>1&&transport_capability<=32
                    ?std::min(take,transport_capability):1;
            }
            {
                std::unique_lock lock(mutex);work.wait(lock,[&]{return stats.stopping||!queue.empty();});
                if(stats.stopping)return;
                // No gather delay and no additional unbounded ownership. Held
                // IDs remain pinned through the final marker write, preventing
                // the poller from admitting a stale E/ snapshot a second time.
                while(events.size()<take&&!queue.empty()){
                    events.push_back(std::move(queue.front()));queue.pop_front();
                }
                ++stats.active;
            }
            try{
                // Reuse the pre-existing ready/progress batch limit. No new
                // gather timer, queue, held slot or admission budget is added.
                const auto capability=independent_take?transport_capability:publisher->MaxBatchMessages();
                if(capability>1&&capability<=32&&events.size()>1&&events.size()<=capability){
                    ProcessBatch(events,published);
                }else{
                    for(const auto& event:events){
                        // Finish a running scalar call, then leave acquired
                        // but unattempted events durable, as before.
                        if(Stopping())break;
                        Process(event,published);
                    }
                }
                bool failed;
                {std::lock_guard lock(mutex);failed=stats.failed;}
                if(!published.empty()&&!failed&&detached_progress){
                    {
                        std::lock_guard lock(mutex);
                        if(!stats.failed){
                            for(auto& event:events)
                                if(std::find(published.begin(),published.end(),event.event_id)!=published.end()){
                                    completed.push_back(std::move(event));
                                    // Do not depend on a moved-from string
                                    // having an empty value on every library.
                                    event.event_id.clear();
                                }
                            transferred=true;
                        }
                    }
                    progress_ready.notify_one();
                }else if(!published.empty()&&!failed){
                    const auto marked=[&]{
                        NativePublicationPhaseTrace trace(NativePublicationPhaseTrace::ProgressWrite,published.front(),published.size());
                        auto value=store->MarkEventsPublished(published);trace.Stop();
                        trace.Result(static_cast<int>(value.status),value.affected_rows);return value;
                    }();
                    if(!marked.Succeeded()||marked.affected_rows!=published.size())Abort("publication_progress_failed");
                    else{
                        std::lock_guard lock(mutex);stats.published+=published.size();
                        for(const auto& id:published)retries.erase(id);
                    }
                }
            }catch(...){Abort("event_worker_exception");}
            NativePublicationPhaseTrace completion(NativePublicationPhaseTrace::CompletionMutex,{},0);
            {
                std::lock_guard lock(mutex);
                completion.Stop();completion.Result(0,events.size());
                // Successfully moved events have an empty ID. Their original
                // held ownership now belongs to the progress writer, which
                // releases it only after the atomic progress write succeeds.
                for(const auto& event:events)
                    if(!transferred||!event.event_id.empty())held.erase(event.event_id);
                --stats.active;
            }
            completion.Emit();
            signal->Notify();
        }
    }
    void WriteProgress()noexcept{
        std::vector<PendingLocalEvent> events;
        std::vector<std::string> ids;
        try{events.reserve(options.max_held);ids.reserve(options.max_held);}
        catch(...){Abort("progress_writer_allocation_failed");return;}
        for(;;){
            events.clear();ids.clear();
            try{
                {
                    std::unique_lock lock(mutex);
                    progress_ready.wait(lock,[&]{return stats.failed||progress_stopping||!completed.empty();});
                    if(stats.failed)return;
                    if(completed.empty()){if(progress_stopping)return;continue;}
                    // Drain only already completed calls. There is no batching
                    // timer and no waiting for another publication to finish.
                    while(!completed.empty()){
                        events.push_back(std::move(completed.front()));completed.pop_front();
                    }
                }
                for(const auto& event:events){
                    if(options.after_success_before_progress)options.after_success_before_progress(event);
                    ids.push_back(event.event_id);
                }
                bool failed;
                {std::lock_guard lock(mutex);failed=stats.failed;}
                if(!failed){
                    NativePublicationPhaseTrace trace(NativePublicationPhaseTrace::ProgressWrite,ids.front(),ids.size());
                    const auto marked=store->MarkEventsPublished(ids);trace.Stop();
                    trace.Result(static_cast<int>(marked.status),marked.affected_rows);
                    if(!marked.Succeeded()||marked.affected_rows!=ids.size())Abort("publication_progress_failed");
                    else{
                        std::lock_guard lock(mutex);stats.published+=ids.size();
                        for(const auto& id:ids)retries.erase(id);
                    }
                }
            }catch(...){Abort("progress_writer_exception");}
            {
                std::lock_guard lock(mutex);
                for(const auto& event:events)held.erase(event.event_id);
            }
            signal->Notify();
        }
    }
    void Poll()noexcept{
        try{
            std::string cursor;auto observed=signal->Version();auto next_sync=Clock::now()+options.sync_interval;unsigned immediate_pages=0;
            while(!Stopping()){
                auto now=Clock::now();
                if(now>=next_sync){
                    const auto synced=[&]{
                        NativePublicationPhaseTrace trace(NativePublicationPhaseTrace::ProgressSync);
                        auto value=dirty_progress_sync?store->SyncPublicationProgressIfDirty():store->SyncPublicationProgress();
                        trace.Stop();trace.Result(static_cast<int>(value.status),dirty_progress_sync?value.affected_rows:1);return value;
                    }();
                    {std::lock_guard lock(mutex);if(synced.Succeeded()){if(!dirty_progress_sync||synced.affected_rows)++stats.progress_syncs;}else ++stats.sync_failures;}
                    if(!synced.Succeeded()){Abort("periodic_progress_sync_failed");break;}
                    next_sync=now+options.sync_interval;
                }
                bool room=false,scanned=false,wrapped=false,read_failed=false;std::size_t page_size=0,requested_page=options.scan_page;
                auto scan=NativePublicationPhaseTrace::Deferred(NativePublicationPhaseTrace::Scan);
                NativePublicationPhaseTrace poll_lock(NativePublicationPhaseTrace::PollMutex);
                {
                    // Read the bounded page and acquire held ownership under
                    // the same lock as worker completion. Otherwise an E/ item
                    // can enter the page while held, get published and released,
                    // then be queued again from that now-stale snapshot.
                    std::lock_guard lock(mutex);
                    poll_lock.Stop();poll_lock.Result(0,1);
                    if(!stats.stopping&&held.size()<options.max_held){
                        if(options.before_pending_scan)options.before_pending_scan();
                        // Both ownership and free admission slots are stable under
                        // this lock. Do not decode a full page merely to discard
                        // its unvisited tail when only one queue slot is free.
                        if(stats.demand_page_enabled)requested_page=std::min(options.scan_page,options.max_held-held.size());
                        scanned=true;scan.Start();const auto page=stats.held_filter_enabled
                            ?store->ListPendingEventsAfterExcluding(cursor,requested_page,held)
                            :store->ListPendingEventsAfter(cursor,requested_page);
                        scan.Stop();scan.Result(static_cast<int>(page.status),page.examined_events);
                        ++stats.pending_pages;stats.decoded_page_records+=page.events.size();
                        read_failed=!page.Succeeded();page_size=page.examined_events;
                        if(!read_failed){
                            if(!page.examined_events){wrapped=!cursor.empty();cursor.clear();}
                            else {
                                bool consumed_page=true;
                                std::size_t visited=0;
                                for(const auto& event:page.events){
                                    if(held.size()>=options.max_held){stats.page_records_unvisited+=page.events.size()-visited;consumed_page=false;break;}
                                    ++visited;
                                    cursor=event.event_id;
                                    if(held.contains(event.event_id))continue;
                                    const auto retry=retries.find(event.event_id);
                                    if(retry!=retries.end()&&retry->second.due>Clock::now())continue;
                                    held.insert(event.event_id);queue.push_back(event);++stats.enqueued_events;stats.held_peak=std::max(stats.held_peak,held.size());
                                }
                                // Advance across excluded held keys as well, including
                                // an entirely held page. Never jump past unqueued work
                                // when admission filled halfway through this page.
                                if(consumed_page)cursor=page.last_examined_event_id;
                            }
                        }
                    }
                    room=held.size()<options.max_held;
                }
                poll_lock.Emit();scan.Emit();
                if(read_failed){Abort("pending_event_read_failed");break;}
                work.notify_all();
                if(scanned&&room&&(wrapped||page_size==requested_page)&&++immediate_pages<4)continue;
                immediate_pages=0;auto deadline=std::min(next_sync,Clock::now()+options.poll_interval);
                // A bounded poll tick checks due entries while scanning pages;
                // never walk the entire retry backlog merely to choose a wakeup.
                observed=signal->WaitUntil(observed,deadline);
            }
        }catch(...){Abort("event_poller_exception");}
    }
    bool StartThreads(){
        {
            std::lock_guard lock(mutex);ever_started=true;stats.running=true;
            healthy.store(true,std::memory_order_release);
        }
        try{
            if(detached_progress)progress_writer=std::thread([this]{WriteProgress();});
            for(std::size_t i=0;i<options.workers;++i)workers.emplace_back([this]{Worker();});
            poller=std::thread([this]{Poll();});return true;
        }catch(...){Abort("event_thread_start_failed");StopThreads();return false;}
    }
    bool StopThreads(){
        {
            std::lock_guard lock(mutex);stats.stopping=true;stats.running=false;
            healthy.store(false,std::memory_order_release);
            for(const auto& event:queue)held.erase(event.event_id);queue.clear();
        }
        work.notify_all();signal->Notify();if(poller.joinable())poller.join();for(auto& worker:workers)if(worker.joinable())worker.join();
        // Normal stop finishes all current publishes before closing the
        // progress queue. A fatal stop leaves every unmarked E/ record durable
        // for the next owner; the final WAL sync still follows all joins.
        {std::lock_guard lock(mutex);progress_stopping=true;}
        progress_ready.notify_all();if(progress_writer.joinable())progress_writer.join();
        const auto synced=store->SyncPublicationProgress();
        {
            std::lock_guard lock(mutex);
            if(synced.Succeeded())++stats.progress_syncs;else{++stats.sync_failures;stats.failed=true;stats.error="final_progress_sync_failed";}
        }
        return synced.Succeeded()&&!Stats().failed;
    }
    LocalEventRelayStats Stats()const{
        const auto wait=NativePersistencePhaseTrace::StartCurrent();std::lock_guard lock(mutex);
        NativePersistencePhaseTrace::RecordCurrent(NativePersistencePhaseTrace::RelayHealthMutex,wait);
        auto out=stats;out.queued=queue.size();out.held=held.size();out.retry_entries=retries.size();return out;
    }
    bool Healthy()const{
        if(options.nonblocking_health)return healthy.load(std::memory_order_acquire);
        const auto s=Stats();return s.running&&!s.failed&&!s.stopping;
    }
};
LocalEventRelay::LocalEventRelay(LocalPrivateStore*s,eventing::EventPublisher*p,LocalEventRelayOptions o):impl_(std::make_unique<Impl>(s,p,std::move(o))){}
LocalEventRelay::~LocalEventRelay(){Shutdown();}
bool LocalEventRelay::Start(){
    auto&s=*impl_;std::lock_guard life(s.lifecycle);
    if(s.ever_started||!s.store->AcquireRelayOwnership())return false;
    s.owns_store=true;
    const auto state=s.store->ListPendingEvents(1);
    if(!state.Succeeded()||!s.StartThreads()){
        s.store->ReleaseRelayOwnership();s.owns_store=false;return false;
    }
    return true;
}
bool LocalEventRelay::Shutdown(){
    auto&s=*impl_;std::lock_guard life(s.lifecycle);
    if(!s.owns_store)return !s.Stats().failed;
    const bool good=s.StopThreads();s.store->ReleaseRelayOwnership();s.owns_store=false;return good;
}
bool LocalEventRelay::Healthy()const{return NativePersistencePhaseTrace::MeasureCurrent(NativePersistencePhaseTrace::RelayHealth,[&]{return impl_->Healthy();});}
LocalEventRelayStats LocalEventRelay::Stats()const{return impl_->Stats();}
}
