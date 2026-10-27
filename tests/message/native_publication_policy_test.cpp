#include "services/message/local/NativePublicationPolicy.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using tinyimx::message::local::ReadNativePublicationPolicy;
using nlohmann::json;

int main() try {
    unsigned checks=0;
    auto same=[&](const json& value,std::size_t workers,std::size_t held) {
        const auto out=ReadNativePublicationPolicy(value);
        if(out.workers!=workers||out.max_held!=held)throw std::runtime_error("policy value mismatch");
        ++checks;
    };
    auto reject=[&](const json& value) {
        try { (void)ReadNativePublicationPolicy(value); }
        catch(const std::invalid_argument&) { ++checks; return; }
        throw std::runtime_error("invalid policy accepted");
    };
    same(json::object(),4,64);
    same({{"publication",json::object()}},4,64);
    same({{"outbox_relay",{{"workers",16},{"publish_batch_size",8}}}},4,64);
    same({{"publication",{{"workers",8}}}},8,64);
    same({{"publication",{{"max_held",256}}}},4,256);
    same({{"publication",{{"workers",16},{"max_held",16}}}},16,16);
    for(const auto& value: {json(),json::array(),json(true),json(8),json("workers")})reject(value);
    for(const auto& value: {json(),json::array(),json(true),json(8),json("workers")})reject({{"publication",value}});
    reject({{"publication",{{"worker",8}}}});
    reject({{"publication",{{"workers",8},{"publish_batch_size",8}}}});
    for(const char* key: {"workers","max_held"}) {
        for(const auto& value: {json(),json::array(),json::object(),json(true),json(false),json("4"),json(4.0),json(4.5),json(-1),json(0),json(std::numeric_limits<std::uint64_t>::max())}) {
            reject({{"publication",{{key,value}}}});
        }
    }
    reject({{"publication",{{"workers",17}}}});
    reject({{"publication",{{"max_held",257}}}});
    reject({{"publication",{{"workers",16},{"max_held",15}}}});
    reject({{"publication",{{"max_held",3}}}});
    // Exercise every supported concurrency value and its capacity boundary.
    for(std::size_t workers=1;workers<=16;++workers) {
        same(json::parse(json({{"publication",{{"workers",workers},{"max_held",workers}}}}).dump()),workers,workers);
        if(workers>1)reject({{"publication",{{"workers",workers},{"max_held",workers-1}}}});
    }
    std::cout<<"NATIVE_PUBLICATION_POLICY_PASS checks="<<checks<<"\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr<<"NATIVE_PUBLICATION_POLICY_FAIL "<<e.what()<<"\n";
    return 1;
}
