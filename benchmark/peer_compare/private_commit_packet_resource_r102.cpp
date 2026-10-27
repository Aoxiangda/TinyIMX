#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/MessageRepository.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <iostream>
#include <stdexcept>

int main(int argc,char**argv) {
    using namespace tinyimx;
    using namespace tinyimx::message;
    try {
        if(argc!=4)return 2;
        const std::string label=argv[2];
        const bool enabled=std::string(argv[3])=="1";
        if(label!="a-off"&&label!="b-on"&&label!="c-on"&&label!="d-off")return 2;
        Config cfg;
        if(!cfg.LoadFromFile(argv[1])||cfg.MySql().database!="codex_perf102_20261020_r1"||cfg.MySql().pool_size!=1)
            throw std::runtime_error("owned schema/pool guard");
        if(!Logger::Instance().Init(cfg.Logger()))throw std::runtime_error("logger init");
        MySqlConnectionPool pool;
        if(!pool.Initialize(cfg.MySql()))throw std::runtime_error("pool init");
        MessageRepository messages(&pool);outbox::OutboxRepository events(&pool);
        MessageRepositoryAdapter adapter(&messages,&pool,&events,"codex_perf102_events_20261020_r1");
        const auto start_id=std::chrono::steady_clock::now().time_since_epoch().count();
        const std::string prefix="p102-"+label+'-'+std::to_string(start_id)+'-';
        constexpr unsigned count=500;
        nlohmann::json rows=nlohmann::json::array();
        for(unsigned i=0;i<count;++i) {
            const auto cid=prefix+std::to_string(i);
            timespec cpu_before{},cpu_after{};
            if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu_before))throw std::runtime_error("CPU clock unavailable");
            const auto begin=std::chrono::steady_clock::now();
            const auto result=adapter.PersistPrivateMessage(500001,500002,cid,1,"owned component command boundary benchmark");
            const auto end=std::chrono::steady_clock::now();
            if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu_after))throw std::runtime_error("CPU clock unavailable");
            if(!result.Accepted()||result.outcome!=PersistPrivateMessageOutcome::kCreated||result.record.client_message_id!=cid)
                throw std::runtime_error("unexpected durable result");
            const auto wall=std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count();
            const auto cpu=(cpu_after.tv_sec-cpu_before.tv_sec)*1000000000LL+cpu_after.tv_nsec-cpu_before.tv_nsec;
            rows.push_back({{"index",i},{"mid",result.message_id},{"wall_ns",wall},{"thread_cpu_ns",cpu}});
        }
        std::uint64_t commands=0;
        {auto lease=pool.Acquire();if(!lease)throw std::runtime_error("lease return failed");commands=lease->InsertCommitCommandAttempts();}
        if(commands!=(enabled?count:0))throw std::runtime_error("combined command count does not match requested arm");
        if(pool.Size()!=1||pool.AvailableCount()!=1)throw std::runtime_error("lease leak");
        pool.Shutdown();Logger::Instance().Shutdown();
        std::cout<<"PACKET_RESOURCE "<<nlohmann::json({{"label",label},{"enabled",enabled},{"completed",count},
            {"combined_command_attempts",commands},{"samples",rows},
            {"scope","Sequential repository-only owned MySQL fixture; no RPC/Gateway/MQ projection; not full IM throughput or end-to-end latency."}}).dump()<<'\n';
        return 0;
    } catch(const std::exception&e) {
        std::cerr<<e.what()<<'\n';Logger::Instance().Shutdown();return 1;
    }
}
