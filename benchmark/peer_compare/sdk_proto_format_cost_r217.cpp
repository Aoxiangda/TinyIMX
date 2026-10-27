// Component CPU cost only. No RocketMQ shared library, network, SQL or Redis.
#include "rocketmq/Logger.h"
#include "fmt/format.h"
#include "spdlog/spdlog.h"
#include "LazyLogValue.h"
#include "spdlog/sinks/null_sink.h"
#include "apache/rocketmq/v2/service.pb.h"
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
static_assert(GOOGLE_PROTOBUF_VERSION==3019004,"match candidate SDK protobuf");
static_assert(FMT_VERSION==90000,"match SDK client fmt9-first include order");
namespace {
void Check(bool value,const char*why){if(!value)throw std::runtime_error(why);}
std::int64_t CpuNs(){timespec t{};Check(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t)==0,"CPU clock unavailable");return t.tv_sec*1000000000LL+t.tv_nsec;}
}
int main(int argc,char**argv){try{
 Check(argc==4,"body-file metadata0or1 declared-lazy0or1");
 const bool metadata=std::string(argv[2])=="1",lazy=std::string(argv[3])=="1";
 Check((std::string(argv[2])=="0"||metadata)&&(std::string(argv[3])=="0"||lazy),"invalid mode");
 Check(ROCKETMQ_NAMESPACE::logging::LazyDebugFormattingEnabled()==lazy,"process flag differs");
 std::ifstream input(argv[1],std::ios::binary);Check(bool(input),"body file absent");
 const std::string body((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
 Check(!body.empty()&&body.size()<=8192,"bounded fixture body required");
 apache::rocketmq::v2::ReceiveMessageResponse response;
 auto*message=response.mutable_message();message->set_body(body);
 if(metadata){
  // These header values are representative synthetic fields, not a capture
  // of a real SDK ReceiveMessageResponse. The actual fixture body is separate.
  message->mutable_topic()->set_name("codex_perf041_events_20261019_r153");
  auto*p=message->mutable_system_properties();p->set_tag("message.created.v1");
  p->add_keys("message.created.v1:9000001");p->set_message_id("0123456789ABCDEF0123456789ABCDEF");
  p->set_receipt_handle(std::string(96,'R'));p->set_queue_id(3);p->set_queue_offset(12345);
  p->mutable_born_timestamp()->set_seconds(1700000000);p->mutable_born_timestamp()->set_nanos(123000000);
  p->mutable_store_timestamp()->set_seconds(1700000001);p->set_born_host("172.18.0.1");
  p->set_store_host("172.18.0.2");p->mutable_invisible_duration()->set_seconds(10);p->set_delivery_attempt(1);
 }
 const std::string wire_before=response.SerializeAsString();
 const std::size_t expected_text_bytes=response.DebugString().size();
 std::shared_ptr<spdlog::sinks::null_sink_mt> sink(new spdlog::sinks::null_sink_mt());
 spdlog::logger logger("owned-r217",sink);logger.set_level(spdlog::level::info);
 std::uint64_t calls=0,checksum=0;
 auto emit=[&]{SPDLOG_LOGGER_DEBUG(&logger,"ReceiveMessageStreamReader#OnReadDone: response={}",
  ROCKETMQ_NAMESPACE::logging::MakeLazyLogValue([&]{++calls;auto text=response.DebugString();checksum+=text.size();return text;}));};
 logger.set_level(spdlog::level::debug);emit();
 Check(calls==1&&checksum==expected_text_bytes,"DEBUG must format once in either arm");
 logger.set_level(spdlog::level::info);
 for(unsigned i=0;i<1000;++i)emit();
 calls=0;checksum=0;const unsigned iterations=100000;
 const auto cpu=CpuNs();const auto wall=std::chrono::steady_clock::now();
 for(unsigned i=0;i<iterations;++i)emit();
 const auto wall_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-wall).count();
 const auto cpu_ns=CpuNs()-cpu;
 Check(calls==(lazy?0u:iterations),"disabled DEBUG producer count differs");
 Check(checksum==(lazy?0u:iterations*expected_text_bytes),"format checksum differs");
 Check(response.SerializeAsString()==wire_before,"formatting mutated response");
 std::cout<<"{\"lazy\":"<<lazy<<",\"metadata_synthetic\":"<<metadata<<",\"iterations\":"<<iterations
  <<",\"body_bytes\":"<<body.size()<<",\"wire_bytes\":"<<wire_before.size()<<",\"text_bytes\":"<<expected_text_bytes
  <<",\"format_calls\":"<<calls<<",\"checksum\":"<<checksum<<",\"cpu_ns\":"<<cpu_ns<<",\"wall_ns\":"<<wall_ns
  <<",\"DEBUG_control_calls\":1,\"protobuf_version\":"<<GOOGLE_PROTOBUF_VERSION<<",\"fmt_version\":"<<FMT_VERSION<<"}"<<std::endl;
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
