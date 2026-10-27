#include "services/message/local/LocalEventRelay.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
using namespace tinyimx::message;
using namespace tinyimx::message::local;
using namespace tinyimx::eventing;
using namespace std::chrono_literals;
namespace {
unsigned checks=0;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);++checks;std::cout<<"PASS "<<why<<'\n';}
template<class F>bool Wait(F f){auto end=std::chrono::steady_clock::now()+10s;while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(1ms);}return f();}
struct Gate{
    std::mutex mutex;std::condition_variable ready;bool entered=false,released=false;
    void Block(){std::unique_lock lock(mutex);entered=true;ready.notify_all();ready.wait(lock,[&]{return released;});}
    bool Entered(){std::lock_guard lock(mutex);return entered;}
    void Release(){std::lock_guard lock(mutex);released=true;ready.notify_all();}
};
struct ReleaseOnExit{Gate& gate;~ReleaseOnExit(){gate.Release();}};
LocalPrivateStoreOptions StoreOptions(const std::filesystem::path& root,const std::string& name){
    LocalPrivateStoreOptions o;o.directory=(root/name).string();o.storage_identity="perf783:"+root.filename().string()+':'+name;
    o.first_message_id=7830000000000000ULL;o.initial_user_ids={500001,500002};
    o.message_events_topic="codex_perf783_events";o.block_cache_bytes=1024*1024;o.exact_user_locking=true;return o;
}
LocalEventRelayOptions Options(){
    LocalEventRelayOptions o;o.workers=1;o.max_held=8;o.scan_page=16;o.progress_batch_size=1;
    o.nonblocking_health=true;o.max_attempts=3;o.retry_base=1ms;o.retry_max=4ms;o.poll_interval=1ms;o.sync_interval=20ms;return o;
}
std::vector<std::string> Fill(LocalPrivateStore& store,unsigned n){
    std::vector<std::string> ids;
    for(unsigned i=0;i<n;++i){auto r=store.PersistPrivateMessage(500001,500002,"detached-"+std::to_string(i),1,"body-"+std::to_string(i));
        if(!r.Accepted())throw std::runtime_error("fixture commit failed");ids.push_back("message.created.v1:"+std::to_string(r.message_id));}
    return ids;
}
class Publisher final:public EventPublisher{
public:
    std::mutex mutex;std::map<std::string,unsigned> attempts;std::map<std::string,std::string> bodies;
    std::string transient,permanent,always_retry;std::filesystem::path journal;
    PublishResult Publish(const PublishMessage& m)override{
        std::lock_guard lock(mutex);auto attempt=++attempts[m.event_id];
        if(m.event_id==permanent)return{PublishStatus::kPermanentFailure,"",""};
        if(m.event_id==always_retry||(m.event_id==transient&&attempt<3))return{PublishStatus::kRetryableFailure,"",""};
        auto[it,inserted]=bodies.emplace(m.event_id,m.body);
        if(!inserted&&it->second!=m.body)throw std::runtime_error("changed replay body");
        if(!journal.empty()){
            const auto line=m.event_id+'\t'+m.body+'\n';int fd=open(journal.c_str(),O_CREAT|O_APPEND|O_WRONLY,0600);
            if(fd<0)throw std::runtime_error("journal open");std::size_t pos=0;bool good=true;
            while(pos<line.size()){auto n=write(fd,line.data()+pos,line.size()-pos);if(n<=0){good=false;break;}pos+=n;}
            good=good&&fsync(fd)==0;close(fd);if(!good)throw std::runtime_error("journal sync");
        }
        return{PublishStatus::kPublished,"owned:"+m.event_id,""};
    }
    std::size_t Count(){std::lock_guard lock(mutex);return bodies.size();}
};
void Contracts(const std::filesystem::path& root){
    for(bool detached:{false,true}){
        setenv("TINYIMX_NATIVE_RELAY_DETACHED_PROGRESS_ENABLE",detached?"1":"0",1);
        LocalPrivateStore store(StoreOptions(root,detached?"detached":"inline"));Fill(store,24);Publisher publisher;
        Gate gate;auto options=Options();std::atomic<unsigned> hook_calls{0};
        options.after_success_before_progress=[&](const auto&){if(hook_calls.fetch_add(1)==0)gate.Block();};
        LocalEventRelay relay(&store,&publisher,options);ReleaseOnExit release{gate};
        Check(relay.Start()&&Wait([&]{return gate.Entered();}),"progress barrier entered after actual publication");
        if(detached)Check(Wait([&]{return publisher.Count()==8;}),"publisher continues while progress is blocked until exact held bound");
        else{std::this_thread::sleep_for(30ms);Check(publisher.Count()==1,"inline reference sender cannot advance while progress is blocked");}
        const auto accepted=publisher.Count();
        Check(store.Stats().pending_events==24&&relay.Stats().held<=8,"unmarked publications retain durable events and bounded held ownership");
        std::this_thread::sleep_for(30ms);
        Check(publisher.Count()==accepted&&std::all_of(publisher.attempts.begin(),publisher.attempts.end(),[](const auto& v){return v.second==1;}),"blocked progress causes neither stale-page duplicate publishes nor unbounded work");
        auto stop=std::async(std::launch::async,[&]{return relay.Shutdown();});
        const bool fenced=Wait([&]{return relay.Stats().stopping;});
        const bool waited=stop.wait_for(30ms)==std::future_status::timeout;
        gate.Release();const bool stopped=stop.get();
        Check(fenced&&waited,"shutdown fences admission and waits for blocked progress");
        Check(stopped&&relay.Stats().held==0&&!relay.Healthy(),"shutdown drains successful progress and joins its writer");
        Check(store.Stats().pending_events==24-accepted&&relay.Stats().published==accepted,"shutdown leaves only unattempted messages pending");
        Check(!detached||store.Stats().publication_batches<=3,"writer coalesces already completed calls without waiting to gather");
        Publisher next;LocalEventRelay resumed(&store,&next,Options());
        Check(resumed.Start()&&Wait([&]{return store.Stats().pending_events==0;})&&resumed.Shutdown(),"next owner resumes every unattempted durable event");
        Check(next.Count()+accepted==24&&store.ReadUnreadSnapshot(500002,500001).total_unread==24,"handoff changes neither message truth nor unread totals");
    }
    setenv("TINYIMX_NATIVE_RELAY_DETACHED_PROGRESS_ENABLE","1",1);
    {
        LocalPrivateStore store(StoreOptions(root,"mixed"));auto ids=Fill(store,257);Publisher publisher;
        publisher.transient=ids[0];publisher.permanent=ids[1];publisher.always_retry=ids[2];
        auto options=Options();options.workers=4;options.max_held=16;LocalEventRelay relay(&store,&publisher,options);
        Check(relay.Start()&&Wait([&]{return store.Stats().pending_events==0;})&&relay.Shutdown(),"four senders and independent progress writer drain mixed results");
        auto s=relay.Stats();Check(s.published==255&&s.quarantined==2&&s.retryable_failures==5&&s.permanent_failures==1&&s.held==0,"retries and permanent failures are never included in success progress");
        for(const auto& [id,n]:publisher.attempts)Check(n==((id==ids[0]||id==ids[2])?3:1),"concurrent ownership prevents duplicate successful publisher calls");
        Check(store.ListFailedEvents(16).events.size()==2&&store.ReadUnreadSnapshot(500002,500001).total_unread==257,"quarantine is exact and does not remove durable message truth");
    }
    {
        LocalPrivateStore store(StoreOptions(root,"exception"));Fill(store,64);Publisher publisher;auto options=Options();
        options.after_success_before_progress=[](const auto&){throw std::bad_alloc();};
        LocalEventRelay relay(&store,&publisher,options);
        Check(relay.Start()&&Wait([&]{return relay.Stats().failed;}),"progress exception fences service health");
        Check(!relay.Shutdown()&&relay.Stats().held==0&&store.Stats().pending_events==64,"fatal progress never acknowledges or discards an unmarked event");
        Publisher next;LocalEventRelay resumed(&store,&next,Options());
        Check(resumed.Start()&&Wait([&]{return store.Stats().pending_events==0;})&&resumed.Shutdown()&&next.Count()==64,"replacement owner recovers complete stable event set");
        for(const auto&[id,body]:publisher.bodies)Check(next.bodies.at(id)==body,"failure replay preserves full event envelope");
    }
    {
        auto opt=StoreOptions(root,"write-rejected");
        {
            LocalPrivateStore store(opt);Fill(store,16);Publisher publisher;auto options=Options();
            options.after_success_before_progress=[&](const auto&){store.Shutdown();};
            LocalEventRelay relay(&store,&publisher,options);
            Check(relay.Start()&&Wait([&]{return relay.Stats().failed;})&&!relay.Shutdown()&&relay.Stats().held==0,"rejected progress write fences rather than silently dropping accepted events");
        }
        LocalPrivateStore reopened(opt);Check(reopened.Stats().pending_events==16,"failed progress write leaves full durable pending set after reopen");
        Publisher next;LocalEventRelay resumed(&reopened,&next,Options());
        Check(resumed.Start()&&Wait([&]{return reopened.Stats().pending_events==0;})&&resumed.Shutdown()&&next.Count()==16,"failed progress write is recoverable by a new owner");
    }
    std::cout<<"DETACHED_PROGRESS_CONTRACTS_PASS checks="<<checks<<std::endl;
}
}
int main(int argc,char**argv)try{
    if(argc!=3)return 2;std::filesystem::path root=argv[1];std::string mode=argv[2];
    if(!root.is_absolute()||std::filesystem::canonical(root)!=root||!root.filename().string().starts_with("perf783-"))return 2;
    if(mode=="contracts"){Contracts(root);return 0;}
    if(mode!="crash"&&mode!="verify")return 2;
    setenv("TINYIMX_NATIVE_RELAY_DETACHED_PROGRESS_ENABLE","1",1);
    LocalPrivateStore store(StoreOptions(root,"crash"));Publisher publisher;publisher.journal=root/"publisher-journal.tsv";auto options=Options();
    if(mode=="crash"){
        Fill(store,16);std::atomic<bool> first{true};
        options.after_success_before_progress=[&](const auto&){if(first.exchange(false)){std::cout<<"DETACHED_PROGRESS_CRASH_READY"<<std::endl;raise(SIGSTOP);}};
        LocalEventRelay relay(&store,&publisher,options);if(!relay.Start())return 3;std::this_thread::sleep_for(30s);return 4;
    }
    std::map<std::string,std::vector<std::string>> before;{
        std::ifstream in(publisher.journal);for(std::string line;std::getline(in,line);){auto at=line.find('\t');if(at==std::string::npos)throw std::runtime_error("bad journal");before[line.substr(0,at)].push_back(line.substr(at+1));}
    }
    Check(before.size()>=1&&before.size()<=8&&store.Stats().pending_events==16,"real process death preserves all events before blocked progress");
    LocalEventRelay relay(&store,&publisher,options);
    Check(relay.Start()&&Wait([&]{return store.Stats().pending_events==0;})&&relay.Shutdown()&&publisher.Count()==16,"restart drains all16 exact events after real SIGKILL");
    for(const auto&[id,bodies]:before)Check(bodies.size()==1&&publisher.bodies.at(id)==bodies.front(),"broker-accepted replay carries identical event identity and body");
    Check(store.ReadUnreadSnapshot(500002,500001).total_unread==16&&relay.Stats().held==0,"recovery never duplicates unread truth or leaks ownership");
    std::cout<<"DETACHED_PROGRESS_PROCESS_RECOVERY_PASS checks="<<checks<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
