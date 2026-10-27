#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
static_assert(GOOGLE_PROTOBUF_VERSION==3019004,"use the installed isolated SDK protobuf headers");
namespace {
std::int64_t CpuNs(){timespec t{};if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t))throw std::runtime_error("CPU clock unavailable");return t.tv_sec*1000000000LL+t.tv_nsec;}
}
int main(){try{
    const auto*d=google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName("apache.rocketmq.v2.ReceiveMessageResponse");
    if(!d)throw std::runtime_error("installed SDK response descriptor not registered");
    const auto*prototype=google::protobuf::MessageFactory::generated_factory()->GetPrototype(d);
    if(!prototype)throw std::runtime_error("SDK generated response prototype missing");
    std::unique_ptr<google::protobuf::Message> response(prototype->New());
    const auto*field=d->FindFieldByName("message");if(!field)throw std::runtime_error("SDK response message field missing");
    auto*message=response->GetReflection()->MutableMessage(response.get(),field);
    const auto*body=message->GetDescriptor()->FindFieldByName("body");if(!body)throw std::runtime_error("SDK message body field missing");
    constexpr unsigned iterations=20000;
    volatile bool debug_enabled=false; // Cost-only synthetic guard; no SDK/logger replacement.
    for(unsigned bytes:{128u,1024u,8192u}){
        message->GetReflection()->SetString(message,body,std::string(bytes,'A'));
        for(bool guarded:{false,true,true,false}){
            std::uint64_t checksum=0,calls=0;
            const auto cpu=CpuNs();const auto wall=std::chrono::steady_clock::now();
            for(unsigned i=0;i<iterations;++i){
                if(!guarded||debug_enabled){auto text=response->DebugString();checksum+=text.size();++calls;}
            }
            const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-wall).count();
            const auto cpu_ns=CpuNs()-cpu;
            if(calls!=(guarded?0u:iterations)||(!guarded&&checksum<iterations*bytes))throw std::runtime_error("formatting count mismatch");
            std::cout<<"{\"body_bytes\":"<<bytes<<",\"guarded\":"<<guarded<<",\"iterations\":"<<iterations<<",\"format_calls\":"<<calls<<",\"checksum\":"<<checksum<<",\"cpu_ns\":"<<cpu_ns<<",\"wall_ns\":"<<ns<<"}\n";
        }
    }
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
