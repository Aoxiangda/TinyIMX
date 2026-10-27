#include "services/message/local/LocalEventRelay.h"
#include "services/message/local/NativePublicationPolicy.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
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
template<class F>bool Wait(F f){const auto end=std::chrono::steady_clock::now()+10s;while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(1ms);}return f();}
LocalPrivateStoreOptions StoreOptions(const std::filesystem::path& root,const std::string& name){
    LocalPrivateStoreOptions o;o.directory=(root/name).string();o.storage_identity="progress-batch-test:"+root.filename().string()+':'+name;
    o.first_message_id=7100000000000000ULL;o.message_events_topic="codex_perf529_events";o.initial_user_ids={500001,500002};
    o.block_cache_bytes=1024*1024;o.exact_user_locking=true;return o;
}
LocalEventRelayOptions RelayOptions(std::size_t batch){
    LocalEventRelayOptions o;o.workers=1;o.max_held=64;o.progress_batch_size=batch;o.scan_page=64;
    o.nonblocking_health=true;o.max_attempts=3;o.retry_base=1ms;o.retry_max=4ms;o.poll_interval=1ms;o.sync_interval=10ms;return o;
}
std::vector<std::string> Fill(LocalPrivateStore& s,unsigned count){
    std::vector<std::string> ids;
    for(unsigned i=0;i<count;++i){auto r=s.PersistPrivateMessage(500001,500002,"batch-"+std::to_string(i),1,"immutable-body:"+std::to_string(i));
        if(!r.Accepted())throw std::runtime_error("fill failed");ids.push_back("message.created.v1:"+std::to_string(r.message_id));}
    return ids;
}
class Publisher final:public EventPublisher{
public:
    std::mutex mutex;std::condition_variable cv;std::map<std::string,unsigned> attempts;std::map<std::string,std::string> bodies;
    std::string transient,permanent,always_retry;bool blocked=false,entered=false,released=false;unsigned gate=0,arrivals=0;std::filesystem::path journal;
    PublishResult Publish(const PublishMessage& message)override{
        std::unique_lock lock(mutex);auto attempt=++attempts[message.event_id];
        if(gate&&!released){++arrivals;cv.notify_all();cv.wait(lock,[&]{return released;});}
        if(blocked&&!entered){entered=true;cv.notify_all();cv.wait(lock,[&]{return released;});}
        if(message.event_id==permanent)return{PublishStatus::kPermanentFailure,"","permanent"};
        if(message.event_id==always_retry||(message.event_id==transient&&attempt<3))return{PublishStatus::kRetryableFailure,"","retry"};
        if(!journal.empty()){
            const auto line=message.event_id+'\t'+message.body+'\n';int fd=open(journal.c_str(),O_CREAT|O_APPEND|O_WRONLY,0600);
            if(fd<0)throw std::runtime_error("journal open");std::size_t offset=0;bool good=true;
            while(offset<line.size()){auto n=write(fd,line.data()+offset,line.size()-offset);if(n<=0){good=false;break;}offset+=n;}
            good=good&&fsync(fd)==0;close(fd);if(!good)throw std::runtime_error("journal fsync");
        }
        auto [at,inserted]=bodies.emplace(message.event_id,message.body);
        if(!inserted&&at->second!=message.body)throw std::runtime_error("envelope changed");
        return{PublishStatus::kPublished,"journal:"+message.event_id,""};
    }
    bool Entered(){std::lock_guard lock(mutex);return entered;}
    unsigned Arrivals(){std::lock_guard lock(mutex);return arrivals;}
    void Release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}
};
void Policy(){
    Check(ReadNativePublicationPolicy(nlohmann::json::object()).progress_batch_size==1,"default progress remains single event");
    for(unsigned n:{1U,8U,16U})Check(ReadNativePublicationPolicy({{"publication",{{"progress_batch_size",n}}}}).progress_batch_size==n,"explicit bounded progress batch parses");
    for(const auto& value:{nlohmann::json(0),nlohmann::json(-1),nlohmann::json(17),nlohmann::json(true),nlohmann::json(1.5),nlohmann::json("8")}){
        bool rejected=false;try{(void)ReadNativePublicationPolicy({{"publication",{{"progress_batch_size",value}}}});}catch(const std::invalid_argument&){rejected=true;}
        Check(rejected,"invalid progress batch rejected before startup");
    }
    bool rejected=false;try{(void)ReadNativePublicationPolicy({{"publication",{{"workers",1},{"max_held",4},{"progress_batch_size",8}}}});}catch(const std::invalid_argument&){rejected=true;}
    Check(rejected,"progress batch must fit held ownership bound");
}
void Contracts(const std::filesystem::path& root){
    Policy();
    for(std::size_t batch:{1U,8U}){
        {
            LocalPrivateStore store(StoreOptions(root,"success-"+std::to_string(batch)));Fill(store,33);Publisher publisher;
            LocalEventRelay relay(&store,&publisher,RelayOptions(batch));Check(relay.Start(),"success relay starts");
            Check(Wait([&]{return store.Stats().pending_events==0;})&&relay.Shutdown(),"all successful markers drain and final WAL sync succeeds");
            Check(relay.Stats().published==33&&publisher.bodies.size()==33&&relay.Stats().held==0,"exactly33 stable events; no leaked ownership");
            Check(store.Stats().publication_batches==(batch==1?33:5),"already queued markers use33 writes at1 or5 writes at8");
            Check(store.ReadUnreadSnapshot(500002,500001).total_unread==33,"progress batching leaves unread truth unchanged");
        }
        {
            LocalPrivateStore store(StoreOptions(root,"retry-"+std::to_string(batch)));auto ids=Fill(store,16);Publisher publisher;
            publisher.transient=ids[0];publisher.permanent=ids[1];publisher.always_retry=ids[2];
            LocalEventRelay relay(&store,&publisher,RelayOptions(batch));Check(relay.Start(),"retry relay starts");
            Check(Wait([&]{return store.Stats().pending_events==0;})&&relay.Shutdown(),"mixed success/retry/permanent batches drain");
            auto stats=relay.Stats();Check(stats.published==14&&stats.quarantined==2&&stats.retryable_failures==5&&stats.permanent_failures==1,"mixed outcomes are never marked as successful together");
            Check(store.ListFailedEvents(16).events.size()==2&&publisher.bodies.size()==14,"only two failed IDs remain quarantined");
        }
        {
            LocalPrivateStore store(StoreOptions(root,"stop-"+std::to_string(batch)));Fill(store,16);Publisher publisher;publisher.blocked=true;
            LocalEventRelay relay(&store,&publisher,RelayOptions(batch));Check(relay.Start()&&Wait([&]{return publisher.Entered();}),"one publication blocked inside an acquired batch");
            auto stopping=std::async(std::launch::async,[&]{return relay.Shutdown();});
            const bool fenced=Wait([&]{return relay.Stats().stopping;});
            const bool waiting=stopping.wait_for(20ms)==std::future_status::timeout;
            publisher.Release();const bool stopped=stopping.get();
            Check(fenced&&waiting&&stopped&&!relay.Healthy(),"shutdown waits for current call and fences health");
            Check(store.Stats().pending_events==15&&publisher.bodies.size()==1&&relay.Stats().held==0,"shutdown leaves all acquired unattempted events durable");
            Publisher next;LocalEventRelay recovered(&store,&next,RelayOptions(batch));
            Check(recovered.Start()&&Wait([&]{return store.Stats().pending_events==0;})&&recovered.Shutdown()&&next.bodies.size()==15,"next owner resumes all remaining events exactly once");
        }
    }
    {
        LocalPrivateStore store(StoreOptions(root,"concurrent-four-workers"));Fill(store,512);Publisher publisher;publisher.gate=4;
        auto options=RelayOptions(8);options.workers=4;LocalEventRelay relay(&store,&publisher,options);
        Check(relay.Start()&&Wait([&]{return publisher.Arrivals()==4;}),"four workers enter independent in-flight batches");
        const bool bounded=relay.Stats().active==4&&relay.Stats().held<=64;
        publisher.Release();
        Check(bounded&&Wait([&]{return store.Stats().pending_events==0;})&&relay.Shutdown(),"concurrent batches drain within held and active bounds");
        Check(relay.Stats().published==512&&publisher.bodies.size()==512&&relay.Stats().held==0,"four workers publish all512 events without stale-page duplicates");
        Check(std::all_of(publisher.attempts.begin(),publisher.attempts.end(),[](const auto& value){return value.second==1;}),"every concurrent event has one actual publisher invocation");
    }
    {
        LocalPrivateStore store(StoreOptions(root,"fatal-partial"));Fill(store,16);Publisher publisher;auto options=RelayOptions(8);unsigned successes=0;
        options.after_success_before_progress=[&](const auto&){if(++successes==3)throw std::runtime_error("owned post-ACK failure");};
        LocalEventRelay relay(&store,&publisher,options);Check(relay.Start()&&Wait([&]{return relay.Stats().failed;}),"partial batch post-ACK failure fences owner");
        Check(!relay.Shutdown()&&store.Stats().pending_events==16&&publisher.bodies.size()==3,"partial successful batch remains replayable after fatal exception");
        Publisher next;LocalEventRelay recovered(&store,&next,RelayOptions(8));
        Check(recovered.Start()&&Wait([&]{return store.Stats().pending_events==0;})&&recovered.Shutdown()&&next.bodies.size()==16,"replacement owner recovers complete batch with stable identities");
        for(const auto&[id,body]:publisher.bodies)Check(next.bodies.at(id)==body,"replayed accepted event has identical envelope bytes");
    }
    std::cout<<"RELAY_PROGRESS_BATCH_CONTRACTS_PASS checks="<<checks<<std::endl;
}
}
int main(int argc,char**argv)try{
    if(argc!=3)return 2;std::filesystem::path root=argv[1];std::string mode=argv[2];
    if(!root.is_absolute()||std::filesystem::canonical(root)!=root||root.filename().string().find("perf529-")==std::string::npos)return 2;
    if(mode=="contracts"){Contracts(root);return 0;}
    if(mode!="crash"&&mode!="verify")return 2;
    LocalPrivateStore store(StoreOptions(root,"crash-store"));Publisher publisher;publisher.journal=root/"publisher-contract-journal.tsv";auto options=RelayOptions(8);
    if(mode=="crash"){
        Fill(store,16);unsigned seen=0;options.after_success_before_progress=[&](const auto&){if(++seen==3){std::cout<<"PROGRESS_BATCH_REAL_PROCESS_CRASH_READY"<<std::endl;raise(SIGSTOP);}};
        LocalEventRelay relay(&store,&publisher,options);if(!relay.Start())return 3;std::this_thread::sleep_for(30s);return 4;
    }
    Check(store.Stats().pending_events==16,"SIGKILL before batch progress preserves16 pending events");
    LocalEventRelay relay(&store,&publisher,options);Check(relay.Start()&&Wait([&]{return store.Stats().pending_events==0;})&&relay.Shutdown(),"real process restart publishes and syncs all16 events");
    std::map<std::string,std::vector<std::string>> observed;std::ifstream input(publisher.journal);
    for(std::string line;std::getline(input,line);){auto split=line.find('\t');if(split==std::string::npos)throw std::runtime_error("journal malformed");observed[line.substr(0,split)].push_back(line.substr(split+1));}
    unsigned total=0,duplicates=0;for(const auto&[id,records]:observed){total+=records.size();if(records.size()==2){++duplicates;Check(records[0]==records[1],"process-death replay has identical envelope bytes");}else Check(records.size()==1,"unattempted event has one journal acceptance");}
    Check(observed.size()==16&&total==19&&duplicates==3,"exactly3 stable replay duplicates and no lost/extra event");
    Check(store.ReadUnreadSnapshot(500002,500001).total_unread==16,"process-death replay never duplicates message or unread truth");
    std::cout<<"RELAY_PROGRESS_BATCH_PROCESS_RECOVERY_PASS checks="<<checks<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
