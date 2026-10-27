#include "services/message/local/NativePersistencePhaseTrace.h"
#include "services/message/application/MessageApplicationTypes.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using Trace=tinyimx::message::local::NativePersistencePhaseTrace;
namespace {
std::int64_t wall=0,cpu=0;unsigned wall_calls=0,cpu_calls=0;
std::vector<Trace::Snapshot> snapshots;
std::int64_t Wall()noexcept{++wall_calls;return wall;}
std::int64_t Cpu()noexcept{++cpu_calls;return cpu;}
void Capture(const Trace::Snapshot&s)noexcept{snapshots.push_back(s);}
void Check(bool good,const char* text){if(!good)throw std::runtime_error(text);std::cout<<"PASS "<<text<<'\n';}
void Advance(int w,int c){wall+=w;cpu+=c;}
}
int main(){try{
    snapshots.reserve(16);
    {
        Trace off(1,2,false,Capture,Wall,Cpu);int executed=0;
        auto lease=off.Measure(Trace::Identity,[&]{++executed;return std::make_unique<int>(7);});
        Trace::MeasureCurrent(Trace::ReferenceSql,[&]{++executed;});
        try{off.Measure(Trace::EngineSync,[]{throw std::runtime_error("sentinel");});}
        catch(const std::runtime_error&e){Check(std::string(e.what())=="sentinel","disabled diagnostics preserve exact application exception");}
        off.Finish(0,0,101);
        Check(executed==2&&*lease==7,"disabled diagnostics preserve move-only lease and void callable exactly once");
    }
    Check(wall_calls==0&&cpu_calls==0&&snapshots.empty(),"disabled diagnostics perform no clocks, sink or active phase collection");
    {
        Trace parent(3,4,true,Capture,Wall,Cpu);
        parent.Measure(Trace::Identity,[&]{
            Trace::MeasureCurrent(Trace::IdentityAcquirePing,[]{Advance(8,1);});
            Trace::MeasureCurrent(Trace::IdentityQuery,[]{Advance(12,3);});
        });
        {
            Trace disabled_child(9,10,false,Capture,Wall,Cpu);
            Trace::MeasureCurrent(Trace::LocalLock,[]{Advance(3,0);});
        }
        {
            Trace child(5,6,true,Capture,Wall,Cpu);child.Allocated(202);
            child.Measure(Trace::References,[]{
                auto wait=Trace::StartCurrent();Advance(5,0);Trace::RecordCurrent(Trace::ReferenceMutex,wait);
                Trace::FreshActors(2);
                Trace::MeasureCurrent(Trace::ReferenceAcquirePing,[]{Advance(7,1);});
                Trace::MeasureCurrent(Trace::ReferenceSql,[]{Advance(11,2);});
            });
            try{child.Measure(Trace::EngineSync,[]{Advance(4,1);throw std::runtime_error("write uncertain");});}
            catch(const std::runtime_error&){}
            child.Finish(6,-1,0);
        }
        Trace::FreshActors(0);
        Trace::MeasureCurrent(Trace::MidAllocate,[]{Advance(2,1);});
        parent.Finish(0,0,303);parent.Finish(6,-1,0);
    }
    Check(snapshots.size()==2,"nested diagnostics emit each completed request exactly once");
    const auto&a=snapshots[0];const auto&b=snapshots[1];
    Check(a.mid==202&&a.reply_mid==0&&a.threw&&a.status==6,"failed write keeps allocated MID without inventing successful reply");
    Check(a.fresh_actors==2&&a.wall_us[Trace::References]==23&&a.wall_us[Trace::ReferenceMutex]==5&&a.wall_us[Trace::ReferenceSql]==11,"cold registration retains inclusive total and separate mutex/SQL children");
    Check(a.phase_cpu_us[Trace::References]==3&&a.phase_cpu_us[Trace::ReferenceMutex]==0,"wall wait remains distinct from consumed thread CPU");
    Check(b.from==3&&b.to==4&&b.mid==303&&b.reply_mid==303&&!b.threw&&b.fresh_actors==0,"nested child restores parent identity and warm classification");
    Check(b.wall_us[Trace::Identity]==20&&b.wall_us[Trace::IdentityAcquirePing]==8&&b.wall_us[Trace::IdentityQuery]==12,"inclusive identity cost contains its children rather than duplicating them");
    Check(b.wall_us[Trace::References]==-1&&b.wall_us[Trace::MidAllocate]==2&&b.wall_us[Trace::LocalLock]==3,"disabled child leaves parent active and enabled child cannot contaminate parent phases");
    {
        Trace unwinding(7,8,true,Capture,Wall,Cpu);
        try{unwinding.Measure(Trace::Identity,[]{throw std::runtime_error("identity failure");});}
        catch(const std::runtime_error&e){Check(std::string(e.what())=="identity failure","enabled diagnostic preserves application exception");}
    }
    Check(snapshots.size()==3&&snapshots.back().threw&&snapshots.back().status==-1,"uncompleted scope records failure rather than a successful request");
    const auto clocks=wall_calls+cpu_calls;
    Trace::MeasureCurrent(Trace::EngineSync,[]{Advance(9,1);});
    Check(wall_calls+cpu_calls==clocks,"completed and unwound scopes leave no dangling thread-local collector");
    {
        Trace detailed(11,12,true,Capture,Wall,Cpu);
        detailed.Measure(Trace::EventAdmission,[]{
            Trace::MeasureCurrent(Trace::RelayHealth,[]{
                auto wait=Trace::StartCurrent();Advance(50,0);Trace::RecordCurrent(Trace::RelayHealthMutex,wait);Advance(5,2);
            });Advance(3,1);
        });
        detailed.Measure(Trace::StateRead,[]{Advance(7,1);});detailed.Measure(Trace::BatchBuild,[]{Advance(11,2);});detailed.Finish(0,0,404);
    }
    const auto&d=snapshots.back();
    Check(d.wall_us[Trace::EventAdmission]==58&&d.wall_us[Trace::RelayHealth]==55&&d.wall_us[Trace::RelayHealthMutex]==50,"admission contains healthy lookup and its mutex wait without losing boundaries");
    Check(d.finished_us-d.started_us==d.wall_us[Trace::EventAdmission]+d.wall_us[Trace::StateRead]+d.wall_us[Trace::BatchBuild]&&d.phase_cpu_us[Trace::RelayHealthMutex]==0,"exclusive disjoint stages close wall total while nested lock waiting consumes no synthetic CPU");
    using Result=tinyimx::message::MessageRepositoryPersistResult;
    using Status=tinyimx::message::MessageApplicationStatus;
    using Outcome=tinyimx::message::PersistPrivateMessageOutcome;
    std::vector<Result> results(4);
    for(auto&r:results){r.status=Status::kSucceeded;r.message_id=808;r.outcome=Outcome::kReused;}
    results[0].outcome=Outcome::kCreated;
    results[2].outcome=Outcome::kIdempotencyConflict;
    results[3].status=Status::kInvalidArgument;results[3].message_id=0;
    {
        Trace batch(21,22,true,Capture,Wall,Cpu);batch.BatchInputs(4);
        batch.Measure(Trace::Identity,[]{Advance(10,2);});
        batch.Measure(Trace::LocalLock,[]{Advance(900,0);});
        batch.Measure(Trace::References,[]{Trace::FreshActors(2);Advance(120000,3);});
        batch.Measure(Trace::References,[]{Trace::FreshActors(0);Advance(2000,1);});
        batch.Measure(Trace::EngineSync,[]{Advance(400,2);});batch.FinishBatch(results);
    }
    const auto batch=snapshots.back();
    Check(batch.batch_inputs==4&&batch.batch_results==4&&batch.batch_created==1&&batch.batch_failed==2&&
        batch.batch_mids[0]==808&&batch.batch_mids[1]==808&&batch.batch_mids[2]==808&&batch.batch_mids[3]==0,
        "batch metadata preserves duplicate identity, conflict and invalid result without inventing members");
    Check(batch.fresh_actors==2&&batch.wall_us[Trace::References]==122000&&batch.wall_us[Trace::EngineSync]==400&&
        batch.phase_cpu_us[Trace::References]==4&&batch.status==static_cast<int>(Status::kInvalidArgument),
        "whole-batch SQL preparation remains distinct from engine sync and result failures");
    const auto before_clocks=wall_calls+cpu_calls;const auto before_scopes=snapshots.size();
    {Trace off(1,2,false,Capture,Wall,Cpu);off.BatchInputs(4);off.FinishBatch(results);}
    Check(wall_calls+cpu_calls==before_clocks&&snapshots.size()==before_scopes,
        "disabled batch diagnostics perform no clock or sink calls");
    return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
