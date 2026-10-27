#include "services/message/server/MessageRpcPollerPolicy.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
}
int main(int argc,char**argv) {
    try {
        using namespace tinyimx::message;
        unsigned checks=0;
        for(const char* value:{static_cast<const char*>(nullptr),"","0","1","8","16","032","64x","-1"," 64"}) {
            const auto p=ParseMessageRpcPollerRetention(value);
            Check(!p.enabled&&p.queues==1&&p.min_pollers==1&&p.max_pollers==2,"invalid retention must preserve default profile");++checks;
        }
        for(const int maximum:{32,64}) {
            const auto value=std::to_string(maximum);
            const auto p=ParseMessageRpcPollerRetention(value.c_str());
            Check(p.enabled&&p.queues==1&&p.min_pollers==1&&p.max_pollers==maximum,"retention must change only idle maximum");++checks;
        }
        const auto legacy=ParseMessageRpcPollerPolicy("1");
        Check(legacy.enabled&&legacy.queues==1&&legacy.min_pollers==4&&legacy.max_pollers==8,"legacy enabled profile unchanged");++checks;
        const auto off=ParseMessageRpcPollerPolicy(nullptr);
        Check(!off.enabled&&off.queues==1&&off.min_pollers==1&&off.max_pollers==2,"legacy default unchanged");++checks;
        Check(argc==4,"expected enabled/min/max arguments required");
        const auto actual=ConfiguredMessageRpcPollerPolicy();
        Check(actual.enabled==bool(std::stoi(argv[1]))&&actual.queues==1&&actual.min_pollers==std::stoi(argv[2])&&actual.max_pollers==std::stoi(argv[3]),"actual environment policy differs");++checks;
        std::cout<<"{\"status\":\"PASS\",\"checks\":"<<checks<<",\"actual_enabled\":"<<int(actual.enabled)<<",\"actual_min\":"<<actual.min_pollers<<",\"actual_max\":"<<actual.max_pollers<<"}\n";
        return 0;
    }catch(const std::exception&e) {
        std::cerr<<e.what()<<'\n';return 2;
    }
}
