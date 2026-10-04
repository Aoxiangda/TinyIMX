#include "services/message/repository/PrivatePersistenceTrace.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
using Trace=tinyimx::message::PrivatePersistenceTrace;
namespace {
std::int64_t ticks=0;unsigned clocks=0,emitted=0,failed=0;Trace::Snapshot captured;
std::int64_t Now() noexcept{++clocks;return ticks;}
void Sink(const Trace::Snapshot& s) noexcept{captured=s;++emitted;}
void Check(bool ok,const char* label){std::cout<<(ok?"[PASS] ":"[FAIL] ")<<label<<'\n';failed+=!ok;}
}
int main(){
    static_assert(std::is_trivially_copyable_v<Trace::Snapshot>,"No message/string buffers in exported diagnostic");
    Check(!Trace::EnvironmentEnabled(),"Diagnostic defaults OFF in unconfigured process");
    tinyimx::message::MessageRepositoryPersistResult result;
    {
        Trace trace(1,2,result,false,Sink,Now,0);
        Check(trace.Measure(Trace::Phase::Precheck,[]{return 7;})==7,"Disabled path returns original value");
    }
    Check(clocks==0&&emitted==0,"Disabled path invokes neither clock nor sink");
    result.status=tinyimx::message::MessageApplicationStatus::kSucceeded;
    {
        Trace trace(11,22,result,true,Sink,Now,0);
        trace.Measure(Trace::Phase::Precheck,[]{ticks+=10;});
        trace.Measure(Trace::Phase::Precheck,[]{ticks+=20;});
        auto p=trace.Measure(Trace::Phase::Acquire,[]{ticks+=12;return std::make_unique<int>(42);});
        Check(p&&*p==42,"Move-only acquisition result preserves ownership");
        result.message_id=77;result.outcome=tinyimx::message::PersistPrivateMessageOutcome::kReused;
        result.message="private-body-never-exported";
    }
    Check(emitted==1&&captured.from==11&&captured.to==22&&captured.message_id==77,"Numeric identity and final result captured");
    Check(captured.total_us==42&&captured.phase_us[0]==30&&captured.phase_us[1]==12,"Repeated phase accumulates elapsed time");
    Check(captured.phase_us[2]==-1&&!captured.threw&&captured.status==0&&captured.outcome==1,"Unexecuted phase and exact outcome remain distinguishable");
    {
        Trace trace(1,2,result,true,Sink,Now,10000);
        trace.Measure(Trace::Phase::Commit,[]{ticks+=1;return true;});
    }
    Check(emitted==1,"Fast successful call is not logged");
    result.status=tinyimx::message::MessageApplicationStatus::kStorageError;
    try {
        Trace trace(1,2,result,true,Sink,Now,10000);
        trace.Measure(Trace::Phase::Insert,[]{ticks+=13;throw std::runtime_error("original-exception");});
    }catch(const std::runtime_error& e){Check(std::string(e.what())=="original-exception","Original exception propagates unchanged");}
    Check(emitted==2&&captured.threw&&captured.phase_us[3]==13&&captured.status==6,"Throwing phase retains elapsed failure evidence");
    tinyimx::message::PersistenceLogLimiter limiter;
    unsigned allowed=0;for(unsigned i=0;i<100;++i)allowed+=limiter.Admit(10);
    Check(allowed==8,"Default numeric logger bounded to eight per second");
    Check(limiter.Admit(11)&&!limiter.Admit(10),"Next bucket resumes and stale bucket rejected");
    return failed?1:0;
}
