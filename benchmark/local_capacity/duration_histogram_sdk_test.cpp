#include "common/observability/DurationHistogramViews.h"
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/metric_reader.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <numeric>
#include <string>
#include <vector>

namespace sdk=opentelemetry::sdk::metrics;
namespace nostd=opentelemetry::nostd;
class Reader final:public sdk::MetricReader {
public:
 sdk::AggregationTemporality GetAggregationTemporality(sdk::InstrumentType) const noexcept override {return sdk::AggregationTemporality::kCumulative;}
private:
 bool OnForceFlush(std::chrono::microseconds) noexcept override {return true;}
 bool OnShutDown(std::chrono::microseconds) noexcept override {return true;}
};
int main(int argc,char** argv) {
 const bool legacy=argc==2&&std::string(argv[1])=="--legacy";if(argc!=1&&!legacy)return 2;
 int failures=0,checks=0;auto check=[&](bool ok,const std::string& label){++checks;failures+=!ok;std::cout<<(ok?"[PASS] ":"[FAIL] ")<<label<<'\n';};
 auto reader=std::make_shared<Reader>();
 sdk::MeterProvider provider(legacy?std::make_unique<sdk::ViewRegistry>():tinyimx::observability::MakeDurationHistogramViews());
 provider.AddMetricReader(reader);auto meter=provider.GetMeter("tinyimx.observability","m20");
 const std::vector<double> values={0.006,0.025,0.099,0.100,0.101,0.133,1.5,12.0};
 std::vector<nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>> histograms;
 for(const char* name:{"rpc.client.call.duration","rpc.server.call.duration","tinyimx.thread_pool.task.duration"}){
  auto h=meter->CreateDoubleHistogram(name,"","s");for(double v:values)h->Record(v,opentelemetry::context::Context{});histograms.push_back(std::move(h));
 }
 auto unrelated=meter->CreateDoubleHistogram("unrelated.latency","","s");for(double v:values)unrelated->Record(v,opentelemetry::context::Context{});
 auto other=provider.GetMeter("other.instrumentation","m20");auto otherhist=other->CreateDoubleHistogram("rpc.client.call.duration","","s");for(double v:values)otherhist->Record(v,opentelemetry::context::Context{});
 auto counter=meter->CreateUInt64Counter("unrelated.counter","","{task}");counter->Add(3,opentelemetry::context::Context{});
 std::map<std::string,sdk::HistogramPointData> points;int counter_seen=0;
 const bool collected=reader->Collect([&](sdk::ResourceMetrics& data){
  for(const auto& scope:data.scope_metric_data_)for(const auto& m:scope.metric_data_){
   for(const auto& p:m.point_data_attr_){
    if(nostd::holds_alternative<sdk::HistogramPointData>(p.point_data))points[std::string(scope.scope_->GetName())+":"+m.instrument_descriptor.name_]=nostd::get<sdk::HistogramPointData>(p.point_data);
    else if(m.instrument_descriptor.name_=="unrelated.counter"&&nostd::holds_alternative<sdk::SumPointData>(p.point_data))counter_seen+=nostd::get<int64_t>(nostd::get<sdk::SumPointData>(p.point_data).value_)==3;
   }
  }
  return true;
 });
 check(collected,"real SDK collection succeeds");check(points.size()==5,"three owned and two independent histograms collected once");check(counter_seen==1,"unrelated counter retains sum aggregation");
 for(const char* name:{"rpc.client.call.duration","rpc.server.call.duration","tinyimx.thread_pool.task.duration"}){
  auto it=points.find(std::string("tinyimx.observability:")+name);check(it!=points.end(),std::string(name)+" present");if(it==points.end())continue;const auto& p=it->second;
  check(p.count_==8&&std::accumulate(p.counts_.begin(),p.counts_.end(),uint64_t{0})==8,std::string(name)+" conserves counts including overflow");
  check(std::abs(nostd::get<double>(p.sum_)-13.964)<1e-9,std::string(name)+" preserves seconds sum");
  auto boundary=std::find(p.boundaries_.begin(),p.boundaries_.end(),0.1);check(boundary!=p.boundaries_.end(),std::string(name)+" has exact100ms boundary");
  if(boundary!=p.boundaries_.end())check(std::accumulate(p.counts_.begin(),p.counts_.begin()+(boundary-p.boundaries_.begin())+1,uint64_t{0})==4,std::string(name)+" separates100ms from101ms with inclusive boundary");
  check(p.counts_.back()==1,std::string(name)+" retains12second overflow");
 }
 for(const char* key:{"tinyimx.observability:unrelated.latency","other.instrumentation:rpc.client.call.duration"}){
  auto it=points.find(key);check(it!=points.end()&&std::find(it->second.boundaries_.begin(),it->second.boundaries_.end(),5.0)!=it->second.boundaries_.end()&&std::find(it->second.boundaries_.begin(),it->second.boundaries_.end(),0.1)==it->second.boundaries_.end(),std::string(key)+" keeps SDK default outside exact selector");
 }
 provider.Shutdown();std::cout<<"DURATION_HISTOGRAM_REAL_SDK_CHECKS="<<checks<<" FAILURES="<<failures<<" LEGACY="<<legacy<<'\n';return failures?1:0;
}
