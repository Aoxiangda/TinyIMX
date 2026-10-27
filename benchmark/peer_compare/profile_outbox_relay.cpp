// Measurement-only wrappers around the unchanged production relay entry point.
// No payload/credential capture and no I/O in measured calls. Flush after shutdown.
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/eventing/rocketmq/RocketMQProducer.h"
#include "services/outbox/OutboxRelay.h"
#include "services/outbox/OutboxRepository.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <vector>

namespace profile {
using Clock=std::chrono::steady_clock;
struct Samples {std::mutex mutex;std::vector<double> ms;std::uint64_t dropped=0;};
std::array<Samples,9> samples;
std::array<std::atomic<std::uint64_t>,65> batch_sizes{};
struct Scope {
 int index;Clock::time_point start=Clock::now();
 ~Scope(){double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();auto&s=samples[index];std::lock_guard lock(s.mutex);if(s.ms.size()<200000)s.ms.push_back(ms);else s.dropped++;}
};
void save(){
 const std::array<const char*,9> names={"claim_batch","renew_claim","mark_published","mark_retry","mark_quarantined","release_claim","cleanup_published","rocketmq_publish","mark_published_batch"};
 nlohmann::json output={{"issue_id","PERF-028"},{"scope","Actual repository and SDK API timing wrappers, no I/O during measured calls. Parent trial defines workload; flush after production main shutdown."}};
 for(int i=0;i<9;i++){auto&s=samples[i];std::sort(s.ms.begin(),s.ms.end());double sum=0;for(double v:s.ms)sum+=v;
  nlohmann::json row={{"count",s.ms.size()},{"dropped",s.dropped},{"sum_ms",sum}};
  if(!s.ms.empty()){row["mean_ms"]=sum/s.ms.size();row["p50_ms"]=s.ms[static_cast<std::size_t>(std::ceil(s.ms.size()*.5))-1];row["p99_ms"]=s.ms[static_cast<std::size_t>(std::ceil(s.ms.size()*.99))-1];row["max_ms"]=s.ms.back();}
  output[names[i]]=row;
 }
 output["published_batch_size_histogram"]=nlohmann::json::object();for(std::size_t i=1;i<batch_sizes.size();i++)if(batch_sizes[i].load())output["published_batch_size_histogram"][std::to_string(i)]=batch_sizes[i].load();
 std::ofstream stream("/perf/publish-profile.json");stream<<output.dump(2)<<'\n';
}
}
namespace tinyimx::outbox {
class ProfiledOutboxRepository final:public OutboxStore {
 OutboxRepository real;
public:
 explicit ProfiledOutboxRepository(MySqlConnectionPool*pool):real(pool){}
 OutboxClaimResult ClaimBatch(const std::string&t,std::size_t n,int lease)override{profile::Scope s{0};return real.ClaimBatch(t,n,lease);}
 OutboxMutationResult RenewClaim(std::uint64_t id,const std::string&t,int lease)override{profile::Scope s{1};return real.RenewClaim(id,t,lease);}
 OutboxMutationResult MarkPublished(std::uint64_t id,const std::string&t)override{profile::Scope s{2};return real.MarkPublished(id,t);}
 OutboxMutationResult MarkRetry(std::uint64_t id,const std::string&t,int after,const std::string&e)override{profile::Scope s{3};return real.MarkRetry(id,t,after,e);}
 OutboxMutationResult MarkQuarantined(std::uint64_t id,const std::string&t,const std::string&e,bool attempted)override{profile::Scope s{4};return real.MarkQuarantined(id,t,e,attempted);}
 OutboxMutationResult ReleaseClaim(std::uint64_t id,const std::string&t)override{profile::Scope s{5};return real.ReleaseClaim(id,t);}
 OutboxCleanupResult CleanupPublished(int hours,std::size_t n)override{profile::Scope s{6};return real.CleanupPublished(hours,n);}
#ifdef TINYIMX_PROFILE_BATCH_API
 OutboxBatchMutationResult MarkPublishedBatch(const std::vector<OutboxRecord>&records)override{profile::Scope s{8};if(records.size()<profile::batch_sizes.size())profile::batch_sizes[records.size()]++;return real.MarkPublishedBatch(records);}
#endif
};
}
namespace tinyimx::eventing::rocketmq_transport {
class ProfiledRocketMQProducer final:public EventPublisher {
 RocketMQProducer real;
public:
 explicit ProfiledRocketMQProducer(RocketMQProducerOptions options):real(std::move(options)){}
 bool Start(){return real.Start();}void Stop()noexcept{real.Stop();}
 const std::string&LastError()const noexcept{return real.LastError();}
 PublishResult Publish(const PublishMessage&m)override{profile::Scope s{7};return real.Publish(m);}
};
}
#define OutboxRepository ProfiledOutboxRepository
#define RocketMQProducer ProfiledRocketMQProducer
#define main production_relay_main
#include "examples/outbox_relay_demo.cpp"
#undef main
#undef RocketMQProducer
#undef OutboxRepository
int main(int argc,char**argv){for(auto&s:profile::samples)s.ms.reserve(200000);int code=production_relay_main(argc,argv);profile::save();return code;}
