// Owned native owner -> real MQ -> real snapshot RPC -> real Redis projector.
// Finite fault qualification; fixture identity registry, no production cutover.
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/message/local/LocalEventRelay.h"
#include "services/message/local/LocalUnreadProjectionSource.h"
#include "services/projection/unread/UnreadProjector.h"
#include "services/projection/unread/UnreadSnapshotRpc.h"
#include "services/cache/UnreadCountCache.h"
#include "services/eventing/EventCodec.h"
#include "services/eventing/rocketmq/RocketMQProducer.h"
#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"
#include "services/eventing/rocketmq/OptionalWireAckBatch.h"
#include <rocketmq/Logger.h>
#include <grpcpp/grpcpp.h>
#include <dlfcn.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace mq=ROCKETMQ_NAMESPACE;
namespace fs=std::filesystem;
using namespace tinyimx;
using namespace tinyimx::message;
using namespace tinyimx::message::local;
using namespace tinyimx::projection::unread;
using namespace tinyimx::eventing;
using namespace tinyimx::eventing::rocketmq_transport;
using namespace std::chrono_literals;
constexpr std::uint64_t sender=500001,receiver=500002;
void Require(bool good,const char* why){if(!good)throw std::runtime_error(why);}
template<class F> void Wait(F&& predicate,const char* why){const auto until=std::chrono::steady_clock::now()+110s;while(!predicate()){Require(std::chrono::steady_clock::now()<until,why);std::this_thread::sleep_for(20ms);}}
struct ServerGuard{std::unique_ptr<grpc::Server> server;~ServerGuard(){Stop();}void Stop(){if(server){server->Shutdown();server->Wait();server.reset();}}};
struct Catalog{
 std::mutex mutex;std::map<std::string,std::string> published;std::set<std::string> confirmed;
 std::map<std::string,unsigned> deliveries;std::atomic<bool> fatal{false};std::string error;
 void Fail(const std::exception&e){std::lock_guard lock(mutex);error=e.what();fatal=true;}
 void CheckFatal(){std::lock_guard lock(mutex);if(fatal)throw std::runtime_error(error);}
};
class TracedPublisher final:public EventPublisher{
 LocalPrivateStore&store;RocketMQProducer&publisher;Catalog&catalog;
public:
 TracedPublisher(LocalPrivateStore&s,RocketMQProducer&p,Catalog&c):store(s),publisher(p),catalog(c){}
 PublishResult Publish(const PublishMessage&m)override{
  try{
   auto pending=store.ListPendingEvents(64);Require(pending.Succeeded(),"native outbox read failed");
   auto found=std::find_if(pending.events.begin(),pending.events.end(),[&](const auto&e){return e.event_id==m.event_id;});
   Require(found!=pending.events.end()&&found->body==m.body&&found->topic==m.topic&&found->tag==m.tag&&found->message_key==m.message_key,"relay did not publish exact durable native event");
   auto decoded=EventCodec::Decode(m.body);Require(decoded.success&&decoded.event.event_id==m.event_id,"native envelope identity changed");
   {std::lock_guard lock(catalog.mutex);Require(catalog.published.emplace(m.event_id,m.body).second,"relay unexpectedly resubmitted an event");}
   auto result=publisher.Publish(m);Require(result.Published(),"real native event publish failed");return result;
  }catch(const std::exception&e){catalog.Fail(e);throw;}
 }
};
class FaultConsumer final:public EventConsumer{
 RocketMQSimpleConsumer&transport;LocalPrivateStore&store;UnreadCountCache&cache;Catalog&catalog;
 bool server_batch,first=true;std::size_t failed=0,first_size=0,corrupt_index=0;
 std::atomic<unsigned> calls{0};
public:
 FaultConsumer(RocketMQSimpleConsumer&t,LocalPrivateStore&s,UnreadCountCache&c,Catalog&x,bool batch):transport(t),store(s),cache(c),catalog(x),server_batch(batch){}
 EventReceiveResult Receive(std::size_t n,int invisible)override{
  try{
   auto result=transport.Receive(n,invisible);Require(result.success,"actual MQ receive failed");
   std::lock_guard lock(catalog.mutex);
   for(const auto&e:result.events){auto decoded=EventCodec::Decode(e.body);Require(decoded.success,"received envelope invalid");const auto&id=decoded.event.event_id;
    Require(catalog.published.count(id)==1&&catalog.published.at(id)==e.body,"received body differs from published native outbox");
    Require(std::find(e.keys.begin(),e.keys.end(),id)!=e.keys.end()&&e.tag==decoded.event.event_type,"native event key or tag changed");
    Require(!catalog.confirmed.count(id),"successfully ACKed native event redelivered");++catalog.deliveries[id];}
   return result;
  }catch(const std::exception&e){catalog.Fail(e);throw;}
 }
 EventAckResult Ack(const ConsumedEvent&)override{throw std::runtime_error("parallel projector called sequential ACK");}
 std::vector<EventAckResult> AckBatch(const std::vector<ConsumedEvent>& input)override{
  try{
   const auto truth=store.ReadUnreadSnapshot(receiver,sender);auto a=cache.GetPrivateUnread(receiver,sender),b=cache.GetTotalUnread(receiver);
   Require(truth.Succeeded()&&a.Completed()&&b.Completed()&&a.count==static_cast<std::int64_t>(truth.private_unread)&&b.count==static_cast<std::int64_t>(truth.total_unread),"ACK occurred before correct real Redis projection");
   auto events=input;const bool inject=first;
   if(inject){first=false;first_size=events.size();Require(first_size>=2,"real mixed batch not formed");corrupt_index=first_size/2;
    auto original=std::static_pointer_cast<const mq::Message>(events[corrupt_index].native_handle);Require(bool(original),"native receipt missing");auto broken=std::make_shared<mq::Message>(*original);broken->mutableExtension().receipt_handle="owned-malformed-middle-receipt";events[corrupt_index].native_handle=std::static_pointer_cast<const void>(broken);}
   auto results=transport.AckBatch(events);Require(results.size()==input.size(),"ACK results lost positions");
   {std::lock_guard lock(catalog.mutex);
    for(std::size_t i=0;i<results.size();++i){const auto decoded=EventCodec::Decode(input[i].body);Require(decoded.success,"ACK input envelope invalid");
     if(inject&&(server_batch||i==corrupt_index)){Require(!results[i].success,"corrupted middle receipt outcome falsely successful");++failed;continue;}
     Require(results[i].success,"valid receipt unexpectedly failed");Require(catalog.confirmed.insert(decoded.event.event_id).second,"same event ACK completion repeated");}}
   ++calls;
   if(inject){
    Require(failed==(server_batch?first_size:1),"middle receipt failure isolation differs");
    auto read=store.MarkDialogRead(receiver,sender);Require(read.Succeeded()&&read.affected_rows==32,"native read transition did not commit all32 messages");
    auto zero=store.ReadUnreadSnapshot(receiver,sender);Require(zero.Succeeded()&&zero.private_unread==0&&zero.total_unread==0,"native authoritative read snapshot not zero");
   }
   return results;
  }catch(const std::exception&e){catalog.Fail(e);throw;}
 }
 std::size_t Failed()const{return failed;}std::size_t FirstSize()const{return first_size;}std::size_t CorruptIndex()const{return corrupt_index;}unsigned Calls()const{return calls;}
};

int main(int argc,char**argv)try{
 if(argc!=6||std::string(argv[2])!="--owned-main243")return 2;
 const std::string cohort=argv[3],mode=argv[5];if(cohort!="off1"&&cohort!="on1"&&cohort!="on2"&&cohort!="off2")return 2;
 if(mode!="--validate"&&mode!="--run")return 2;Config cfg;
 Require(cfg.LoadFromFile(argv[1]),"owned config rejected");
 const auto parent=fs::canonical(fs::path(argv[1]).parent_path());const std::string root_name=parent.parent_path().filename().string();const std::string prefix="perf243-native-projection-";Require(root_name.starts_with(prefix),"unowned native root");
 const std::string run=root_name.substr(prefix.size());Require(run=="r1"||run=="r2"||run=="r3"||run=="r4","unowned native run");
 Require(parent==fs::path("/home/jackson7/projects/tinyimx-interview-20261017")/(prefix+run)/cohort,"unowned native directory");
 Require(cfg.RocketMQ().message_topic=="codex_perf243_events_20261021_"+run+"_"+cohort&&cfg.UnreadProjection().consumer_group=="codex_perf243_consumer_20261021_"+run+"_"+cohort,"unowned MQ scope");
 Require(cfg.Redis().enable&&cfg.Redis().host.starts_with("172.18.")&&cfg.Redis().db==0&&cfg.Redis().port==6379&&!cfg.MySql().enable,"only dedicated Redis and native authority allowed");
 if(mode=="--validate"){std::cout<<"MAIN243_CONFIG_VALIDATED_NO_CLIENT\n";return 0;}
 const bool server_batch=cohort=="on1"||cohort=="on2";const char*flag=std::getenv("TINYIMX_ROCKETMQ_WIRE_ACK_BATCH_ENABLE");Require(flag&&std::string(flag)=="1","client batching must stay ON");
 using Getter=const mq::TinyIMXBatchAckApiV1*(*)()noexcept;const auto getter=reinterpret_cast<Getter>(dlsym(RTLD_DEFAULT,"tinyimx_rocketmq_batch_ack_api_v1"));Dl_info info{};
 Require(getter&&detail::CompatibleWireAckApi(getter())&&dladdr(reinterpret_cast<void*>(getter),&info)&&fs::canonical(info.dli_fname)==fs::canonical(argv[4]),"wrong SDK capability or library");const auto*api=getter();
 mq::getLogger().setLevel(mq::Level::Warn);mq::getLogger().setConsoleLevel(mq::Level::Warn);Require(Logger::Instance().Init(cfg.Logger()),"logger start failed");
 LocalPrivateStoreOptions so;so.directory=(parent/"native-store").string();Require(!fs::exists(so.directory),"never replay existing native fixture");so.storage_identity="owned-proj243-"+run+"-"+cohort;so.message_events_topic=cfg.RocketMQ().message_topic;so.first_message_id=7092430000000000ULL;so.initial_user_ids={sender,receiver};so.block_cache_bytes=8*1024*1024;so.exact_user_locking=true;so.identity_policy="fixture-raw-bytes-v1";
 LocalPrivateStore store(so);std::set<std::uint64_t> mids;
 for(unsigned i=0;i<32;++i){auto p=store.PersistPrivateMessage(sender,receiver,"owned-proj243-"+run+"-"+cohort+"-"+std::to_string(i),1,"native-body:"+std::to_string(i));Require(p.Accepted()&&p.outcome==PersistPrivateMessageOutcome::kCreated&&mids.insert(p.message_id).second,"native message/outbox persist failed");}
 auto initial=store.ReadUnreadSnapshot(receiver,sender);Require(initial.Succeeded()&&initial.private_unread==32&&initial.total_unread==32,"native unread truth must start32");
 LocalUnreadProjectionSource source(&store);UnreadSnapshotRpcService service(&source);grpc::ServerBuilder builder;int port=0;builder.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);builder.RegisterService(&service);ServerGuard server{builder.BuildAndStart()};Require(server.server&&port>0,"real native snapshot RPC failed");
 RedisConnectionPool redis;Require(redis.Initialize(cfg.Redis()),"private Redis pool failed");UnreadCountCache cache(&redis,"owned:perf243:"+run+":"+cohort+":");
 RocketMQSimpleConsumerOptions co;co.endpoint=cfg.RocketMQ().endpoint;co.topic=cfg.RocketMQ().message_topic;co.consumer_group=cfg.UnreadProjection().consumer_group;co.filter_expression=kUnreadProjectionTagFilter;co.await_duration_ms=5000;co.request_timeout_ms=10000;co.tls=cfg.RocketMQ().tls;co.access_key=cfg.RocketMQ().access_key;co.access_secret=cfg.RocketMQ().access_secret;
 RocketMQSimpleConsumer consumer(co);Require(consumer.Start(),"real consumer failed");RocketMQProducerOptions po;po.endpoint=co.endpoint;po.topic=co.topic;po.request_timeout_ms=10000;po.tls=co.tls;po.access_key=co.access_key;po.access_secret=co.access_secret;RocketMQProducer producer(po);Require(producer.Start(),"real producer failed");
 Catalog catalog;TracedPublisher traced(store,producer,catalog);LocalEventRelayOptions ro;ro.workers=1;ro.max_held=32;ro.scan_page=64;ro.sync_interval=20ms;LocalEventRelay relay(&store,&traced,ro);Require(relay.Start(),"actual native relay failed");
 Wait([&]{catalog.CheckFatal();return relay.Stats().published==32&&store.Stats().pending_events==0;},"initial native events did not publish");
 UnreadSnapshotRpcClient reader("127.0.0.1:"+std::to_string(port),2000ms,false);auto remote=reader.LoadDialogSnapshot(receiver,sender);Require(remote.success&&remote.value.private_unread==32,"real RPC does not reflect native authority");
 FaultConsumer fault(consumer,store,cache,catalog,server_batch);UnreadProjectorOptions options;options.mode=UnreadProjectorMode::kWriter;options.topic=co.topic;options.batch_size=16;options.worker_threads=1;options.parallel_batch_ack=true;options.batch_snapshot_reads=false;options.invisible_duration_ms=10000;options.receive_error_backoff_ms=100;
 mq::TinyIMXBatchAckStatsV1 before{},after{};api->read_stats(&before);UnreadProjector projector(&fault,&reader,&cache,options);Require(projector.Start(),"real native RPC projector failed");
 Wait([&]{catalog.CheckFatal();std::lock_guard lock(catalog.mutex);return catalog.confirmed.size()==33;},"faulted native events did not fully recover");
 projector.Shutdown();catalog.CheckFatal();Require(relay.Shutdown(),"relay shutdown did not drain and sync");const auto ps=projector.Stats();const auto rs=relay.Stats();api->read_stats(&after);
 const auto failures=server_batch?fault.FirstSize():1;Require(fault.Failed()==failures&&ps.ack_failure_total==failures,"projection ACK failure stats differ");
 Require(ps.received_total==33+failures&&ps.reconciled_total==33+failures&&ps.process_failure_total==0&&ps.receive_failure_total==0&&ps.poison_failure_total==0&&ps.retryable_failure_total==0,"projector lost/rejected native recovery events");
 Require(rs.published==33&&rs.quarantined==0&&rs.held==0&&!rs.failed&&store.Stats().pending_events==0,"native relay final state inconsistent");
 auto a=cache.GetPrivateUnread(receiver,sender),total=cache.GetTotalUnread(receiver);Require(a.NotFound()&&total.NotFound()&&a.count==0&&total.count==0,"stale redelivery resurrected unread counters or failed zero-key deletion");
 Require(after.submitted_entries-before.submitted_entries==ps.received_total&&after.rpc_submissions-before.rpc_submissions==after.rpc_terminals-before.rpc_terminals,"SDK entries or terminal callbacks differ");
 std::map<std::string,unsigned> deliveries;{std::lock_guard lock(catalog.mutex);Require(catalog.published.size()==33&&catalog.confirmed.size()==33,"unique durable publication/confirmation count differs");deliveries=catalog.deliveries;}
 Require(deliveries.size()==33&&std::count_if(deliveries.begin(),deliveries.end(),[](const auto&v){return v.second==2;})==static_cast<long>(failures)&&std::none_of(deliveries.begin(),deliveries.end(),[](const auto&v){return v.second>2;}),"actual MQ redelivery count differs");
 consumer.Stop();auto stopped=consumer.AckBatch(std::vector<ConsumedEvent>(2));Require(stopped.size()==2&&!stopped[0].success&&!stopped[1].success,"stopped transport accepted ACK");producer.Stop();server.Stop();Require(!reader.LoadDialogSnapshot(receiver,sender).success,"stopped native RPC became successful empty read");redis.Shutdown();Require(store.Shutdown(),"native store shutdown failed");
 LocalPrivateStore reopened(so);auto persisted=reopened.ListHistory(receiver,sender,0,64);auto zero=reopened.ReadUnreadSnapshot(receiver,sender);Require(persisted.Succeeded()&&persisted.records.size()==32&&zero.Succeeded()&&zero.private_unread==0&&zero.total_unread==0&&reopened.Stats().pending_events==0&&reopened.ListFailedEvents(64).events.empty(),"closed/reopened native state differs");std::set<std::uint64_t> restored;for(const auto&m:persisted.records)restored.insert(m.message_id);Require(restored==mids&&reopened.Shutdown(),"reopened native IDs changed");
 nlohmann::json result={{"run",run},{"cohort",cohort},{"wire",1},{"native_messages",32},{"published_native_events",33},{"confirmed_native_events",33},{"first_batch_size",fault.FirstSize()},{"corrupted_index",fault.CorruptIndex()},{"initial_failed",failures},{"projector_received",ps.received_total},{"projector_reconciled",ps.reconciled_total},{"projector_ack_failures",ps.ack_failure_total},{"sdk_rpc_submissions",after.rpc_submissions-before.rpc_submissions},{"sdk_entries",after.submitted_entries-before.submitted_entries},{"cache_private_missing",a.NotFound()},{"cache_total_missing",total.NotFound()},{"cache_private",a.count},{"cache_total",total.count},{"native_reopened_messages",persisted.records.size()},{"native_reopened_unread",zero.total_unread},{"read_advanced_during_ACK_failure",true},{"real_relay_RPC_projector_Redis",true}};
 std::cout<<"MAIN243_NATIVE_PROJECTION_PASS "<<result.dump()<<std::endl;Logger::Instance().Shutdown();return 0;
}catch(const std::exception&e){std::cerr<<"MAIN243_FAIL "<<e.what()<<std::endl;return 1;}
