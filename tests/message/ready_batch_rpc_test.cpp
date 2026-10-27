#include "services/message/service/MessageReadyBatchService.h"
#include "services/message/application/MessageApplicationService.h"
#include "tests/message/ReadyBatchNoopRepository.h"
#include <grpcpp/grpcpp.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
using namespace tinyimx::message;
namespace pb=tinyimx::message::v1;
void Require(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
template<class F>bool Wait(F predicate){const auto until=std::chrono::steady_clock::now()+2s;
    do{if(predicate())return true;std::this_thread::sleep_for(1ms);}while(std::chrono::steady_clock::now()<until);return false;}
class Repository final:public tinyimx::tests::ReadyBatchNoopRepository {
public:
    std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;
    std::map<std::pair<std::uint64_t,std::string>,MessageView>records;
    std::set<std::uint64_t>events;std::uint64_t next=100;std::vector<std::size_t>batch_sizes;
    bool throw_after_commit=false,bad_count=false;
    void Release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}
    bool Entered(){std::lock_guard lock(mutex);return entered;}
    std::size_t Count(){std::lock_guard lock(mutex);return records.size();}
    MessageRepositoryPersistResult PersistPrivateMessage(std::uint64_t from,std::uint64_t to,const std::string& cid,
        std::uint32_t type,const std::string& body)override{
        return PersistPrivateMessages({{from,to,cid,type,body}}).front();
    }
    std::vector<MessageRepositoryPersistResult> PersistPrivateMessages(const std::vector<PrivateMessagePersistInput>& in)override{
        std::unique_lock lock(mutex);batch_sizes.push_back(in.size());
        if(!in.empty()&&in.front().client_message_id=="hold"){
            entered=true;cv.notify_all();Require(cv.wait_for(lock,5s,[&]{return released;}),"repository gate watchdog");
        }
        if(bad_count)return {};
        auto staged=records;auto staged_events=events;auto sequence=next;
        std::vector<MessageRepositoryPersistResult> out;
        for(const auto& input:in){
            MessageRepositoryPersistResult result;result.status=MessageApplicationStatus::kSucceeded;
            auto key=std::make_pair(input.from_user_id,input.client_message_id);auto found=staged.find(key);
            if(found!=staged.end()){
                result.record=found->second;
                result.outcome=result.record.to_user_id==input.to_user_id&&result.record.content==input.content&&result.record.message_type==input.message_type
                    ?PersistPrivateMessageOutcome::kReused:PersistPrivateMessageOutcome::kIdempotencyConflict;
            }else{
                MessageView v;v.message_id=++sequence;v.from_user_id=input.from_user_id;v.to_user_id=input.to_user_id;
                v.client_message_id=input.client_message_id;v.message_type=input.message_type;v.content=input.content;v.created_at="2026-10-20 00:00:00";
                staged.emplace(key,v);staged_events.insert(v.message_id);result.record=v;result.outcome=PersistPrivateMessageOutcome::kCreated;
            }
            result.message_id=result.record.message_id;out.push_back(std::move(result));
        }
        records.swap(staged);events.swap(staged_events);next=sequence;
        if(throw_after_commit){throw_after_commit=false;throw std::runtime_error("owned uncertain result after commit");}
        return out;
    }
    MessageRepositoryGetResult FindPrivateMessageByClientMessageId(std::uint64_t from,const std::string& cid)override{
        std::lock_guard lock(mutex);MessageRepositoryGetResult result;result.status=MessageApplicationStatus::kSucceeded;
        const auto found=records.find({from,cid});if(found!=records.end()){result.found=true;result.record=found->second;}return result;
    }
    MessageRepositoryGetResult GetPrivateMessage(std::uint64_t mid)override{
        std::lock_guard lock(mutex);MessageRepositoryGetResult result;result.status=MessageApplicationStatus::kSucceeded;
        for(const auto& [key,v]:records)if(v.message_id==mid){result.found=true;result.record=v;break;}return result;
    }
};
struct Threads {
    Repository& repo;std::vector<std::jthread>threads;
    ~Threads(){repo.Release();for(auto& t:threads)if(t.joinable())t.join();}
    template<class F>void Add(F fn){threads.emplace_back(std::move(fn));}
    void Join(){repo.Release();for(auto& t:threads)if(t.joinable())t.join();}
};
struct Server {
    std::unique_ptr<grpc::Server>server;std::unique_ptr<pb::MessageService::Stub>stub;
    explicit Server(grpc::Service* service){
        grpc::ServerBuilder b;int port=0;b.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);b.RegisterService(service);
        server=b.BuildAndStart();Require(server&&port>0,"loopback server failed");
        stub=pb::MessageService::NewStub(grpc::CreateChannel("127.0.0.1:"+std::to_string(port),grpc::InsecureChannelCredentials()));
    }
    ~Server(){server->Shutdown();server->Wait();}
    std::pair<grpc::Status,pb::PersistPrivateMessageResponse> Put(const std::string& cid,const std::string& body="same",std::chrono::milliseconds timeout=4s,std::uint64_t from=1,std::uint64_t to=2){
        grpc::ClientContext c;c.set_deadline(std::chrono::system_clock::now()+timeout);pb::PersistPrivateMessageRequest q;
        q.set_from_user_id(from);q.set_to_user_id(to);q.set_client_message_id(cid);q.set_message_type(1);q.set_content(body);
        pb::PersistPrivateMessageResponse r;auto s=stub->PersistPrivateMessage(&c,q,&r);return {s,r};
    }
};
void CancelAndCapacity(){
    std::atomic<bool>cancel_observed{false};
    Repository repo;MessageApplicationService app(&repo);MessageReadyBatchService service(&app,nullptr,1,2,
        [&](std::string_view cid){if(cid=="cancelled")cancel_observed.store(true,std::memory_order_release);});Server server(&service);
    std::promise<std::pair<grpc::Status,pb::PersistPrivateMessageResponse>> first,second;auto f=first.get_future(),g=second.get_future();Threads threads{repo};
    threads.Add([&]{first.set_value(server.Put("hold"));});Require(Wait([&]{return repo.Entered();}),"running request did not enter");
    threads.Add([&]{second.set_value(server.Put("cancelled","same",100ms));});
    Require(Wait([&]{return service.Stats().accepted==2;}),"callback blocked instead of queueing");
    Require(server.Put("overload").first.error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED,"in-flight budget omitted");
    Require(g.wait_for(1s)==std::future_status::ready&&g.get().first.error_code()==grpc::StatusCode::DEADLINE_EXCEEDED,"queued deadline");
    Require(Wait([&]{return cancel_observed.load(std::memory_order_acquire);}),"server did not observe the queued cancellation");
    Require(f.wait_for(0ms)==std::future_status::timeout,"ACK before durable result");threads.Join();Require(f.get().first.ok(),"running request lost");
    Require(repo.Count()==1,"cancelled queued request mutated repository");
    auto recovered=server.Put("hold");Require(recovered.first.ok()&&recovered.second.result()==pb::PERSIST_PRIVATE_MESSAGE_RESULT_REUSED,"queue did not recover");
    grpc::ClientContext c;pb::GetPrivateMessageRequest q;q.set_message_id(recovered.second.message_id());pb::GetPrivateMessageResponse r;
    Require(server.stub->GetPrivateMessage(&c,q,&r).ok()&&r.record().content()=="same","sync read RPC fallback changed");
    server.server->Shutdown();server.server->Wait();service.Drain();const auto s=service.Stats();
    Require(s.pending==0&&s.completed==s.accepted&&s.overloaded==1,"cancel/drain lifecycle");
    std::cout<<"PASS real callback nonblocking admission, running-inclusive budget, queued deadline, sync fallback and drain\n";
}
void TimeoutAfterRepositoryEntry(){
    std::atomic<bool>cancel_observed{false};
    Repository repo;MessageApplicationService app(&repo);MessageReadyBatchService service(&app,nullptr,1,2,
        [&](std::string_view cid){if(cid=="hold")cancel_observed.store(true,std::memory_order_release);});Server server(&service);
    using Reply=std::pair<grpc::Status,pb::PersistPrivateMessageResponse>;
    std::promise<Reply>reply;auto future=reply.get_future();Threads threads{repo};
    threads.Add([&]{reply.set_value(server.Put("hold","same",100ms));});
    Require(Wait([&]{return repo.Entered();}),"timed request did not enter repository before its deadline");
    Require(future.wait_for(1s)==std::future_status::ready&&future.get().first.error_code()==grpc::StatusCode::DEADLINE_EXCEEDED,"in-repository deadline result");
    Require(Wait([&]{return cancel_observed.load(std::memory_order_acquire);}),"server cancellation after repository entry not observed");
    threads.Join();Require(Wait([&]{return service.Stats().completed==1;}),"accepted mutation did not finish after client timeout");
    Require(repo.Count()==1&&repo.events.size()==1,"uncertain committed outcome not retained exactly once");
    auto retry=server.Put("hold");Require(retry.first.ok()&&retry.second.result()==pb::PERSIST_PRIVATE_MESSAGE_RESULT_REUSED&&repo.Count()==1&&repo.events.size()==1,
        "CID recovery after timed-out commit duplicated or lost a durable result");
    server.server->Shutdown();server.server->Wait();service.Drain();
    std::cout<<"PASS timeout after repository entry remains uncertain; CID retry reuses one message and event\n";
}
void SharedBatchAndConflict(){
    Repository repo;MessageApplicationService app(&repo);MessageReadyBatchService service(&app,nullptr,1,8);Server server(&service);
    using Reply=std::pair<grpc::Status,pb::PersistPrivateMessageResponse>;
    std::vector<Reply> replies(5);Threads threads{repo};threads.Add([&]{replies[0]=server.Put("hold");});
    Require(Wait([&]{return repo.Entered();}),"batch first running");
    for(int i=1;i<5;++i){threads.Add([&,i]{replies[i]=server.Put(i==4?"invalid":"repeat",i==3?"conflict":"same",4s,i==4?0:1);});
        Require(Wait([&]{return service.Stats().accepted==static_cast<std::uint64_t>(i+1);}),"ordered batch callback not returned");}
    threads.Join();
    Require(replies[0].first.ok()&&replies[1].first.ok()&&replies[2].first.ok()&&replies[3].first.ok(),"batch statuses");
    Require(replies[1].second.result()==pb::PERSIST_PRIVATE_MESSAGE_RESULT_CREATED&&replies[2].second.result()==pb::PERSIST_PRIVATE_MESSAGE_RESULT_REUSED&&
            replies[3].second.result()==pb::PERSIST_PRIVATE_MESSAGE_RESULT_IDEMPOTENCY_CONFLICT,"CID outcome order changed");
    Require(replies[1].second.message_id()==replies[2].second.message_id()&&replies[1].second.message_id()==replies[3].second.message_id(),"CID MID changed");
    Require(replies[4].first.error_code()==grpc::StatusCode::INVALID_ARGUMENT&&repo.Count()==2&&repo.events.size()==2,"invalid item or duplicate emitted extra event");
    const auto limit=ConfiguredPrivateReadyBatchLimit();
    const bool keyed=ConfiguredPrivateReadyKeyedScheduler();
    const auto expected_sizes=(limit==1||keyed)?std::vector<std::size_t>({1,1,1,1}):std::vector<std::size_t>({1,3});
    Require(repo.batch_sizes==expected_sizes,"configured preparation limit not passed through application with invalid item excluded");
    server.server->Shutdown();server.server->Wait();service.Drain();auto s=service.Stats();
    // With keyed64 the invalid sender0 and one ready sender1 input share a
    // batch; the remaining sender1 CIDs stay sequential across later batches.
    const auto expected_single=limit==1?5u:(keyed?3u:1u);
    Require(s.accepted==5&&s.completed==5&&s.batch_sizes[1]==expected_single&&
        s.batch_sizes[2]==(limit==64&&keyed?1u:0u)&&s.batch_sizes[4]==(limit==64&&!keyed?1u:0u)&&s.pending==0,"ready batch histogram mismatch");
    std::cout<<"PASS one ready batch, per-item invalid mapping, created/reused/conflict, immutable CID and one event\n";
}
void UncertainCompletion(){
    Repository repo;repo.throw_after_commit=true;MessageApplicationService app(&repo);MessageReadyBatchService service(&app,nullptr,1);Server server(&service);
    Require(server.Put("uncertain").first.error_code()==grpc::StatusCode::UNAVAILABLE,"uncertain result falsely successful");
    Require(repo.Count()==1&&repo.events.size()==1,"coordinator automatically retried uncertain write");
    auto retry=server.Put("uncertain");Require(retry.first.ok()&&retry.second.result()==pb::PERSIST_PRIVATE_MESSAGE_RESULT_REUSED&&repo.Count()==1&&repo.events.size()==1,"CID recovery after uncertain result");
    server.server->Shutdown();server.server->Wait();service.Drain();Require(service.Stats().execute_failures==1,"uncertain failure accounting");
    std::cout<<"PASS uncertain execution is not retried; client retry reuses durable CID\n";
}
void ApplicationBounds(){
    Repository repo;MessageApplicationService app(&repo);
    auto out=app.PersistPrivateMessages({{1,2,"valid",1,"body"},{1,1,"self",1,"body"},{1,2,"",1,"body"}});
    Require(out.size()==3&&out[0].Accepted()&&out[1].status==MessageApplicationStatus::kInvalidArgument&&out[2].status==MessageApplicationStatus::kInvalidArgument&&repo.Count()==1,"application validation changed");
    const auto calls=repo.batch_sizes.size();out=app.PersistPrivateMessages(std::vector<PrivateMessagePersistInput>(65));
    Require(out.size()==65&&repo.batch_sizes.size()==calls,"oversized batch entered repository");
    repo.bad_count=true;out=app.PersistPrivateMessages({{1,2,"missing",1,"body"}});
    Require(out.size()==1&&out[0].status==MessageApplicationStatus::kStorageError,"malformed repository batch falsely successful");
    std::cout<<"PASS shared application validation,64 bound and result-count failure\n";
}
void SenderLaneIsolation(){
    if(!ConfiguredPrivateReadyKeyedScheduler())return;
    Repository repo;MessageApplicationService app(&repo);MessageReadyBatchService service(&app,nullptr,2,4);Server server(&service);
    using Reply=std::pair<grpc::Status,pb::PersistPrivateMessageResponse>;
    std::promise<Reply>first,same,other;Threads threads{repo};
    threads.Add([&]{first.set_value(server.Put("hold"));});Require(Wait([&]{return repo.Entered();}),"first sender not blocked");
    threads.Add([&]{same.set_value(server.Put("next"));});Require(Wait([&]{return service.Stats().accepted>=2;}),"same sender not admitted");
    threads.Add([&]{other.set_value(server.Put("unrelated","same",4s,3,4));});
    auto ready=other.get_future();Require(ready.wait_for(2s)==std::future_status::ready&&ready.get().first.ok(),"unrelated modulo-colliding sender waited behind SQL gate");
    Require(repo.Count()==1,"same sender bypassed its unfinished predecessor");
    threads.Join();Require(first.get_future().get().first.ok()&&same.get_future().get().first.ok(),"accepted sender failed after gate release");
    server.server->Shutdown();server.server->Wait();service.Drain();
    Require(service.Stats().accepted==3&&service.Stats().completed==3&&repo.Count()==3,"keyed RPC lost or replayed an accepted input");
    std::cout<<"PASS real RPC, unrelated colliding sender progresses while same sender retains FIFO\n";
}
}
int main(){try{ApplicationBounds();CancelAndCapacity();TimeoutAfterRepositoryEntry();SharedBatchAndConflict();UncertainCompletion();SenderLaneIsolation();return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
