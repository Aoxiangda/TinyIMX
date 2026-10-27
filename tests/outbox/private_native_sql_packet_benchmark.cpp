#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/MessageRepository.h"
#include <chrono>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
void Require(bool ok,const char* label) { if(!ok)throw std::runtime_error(label); }
std::map<std::string,std::uint64_t> Counters(tinyimx::MySqlConnectionPool& pool) {
    auto c=pool.Acquire();tinyimx::MySqlQueryResult rows;
    Require(c&&c->Query("SHOW SESSION STATUS WHERE Variable_name IN ('Com_select','Com_insert','Com_commit','Com_rollback')",&rows),"session counters");
    std::map<std::string,std::uint64_t> result;
    for(const auto& row:rows.rows)result[row[0]]=std::stoull(row[1]);
    Require(result.size()==4,"four counters");result["Grouped_commands"]=c->GroupedStatementCommandAttempts();return result;
}
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    try {
        tinyimx::Config config;
        Require(config.LoadFromFile(argv[1])&&config.MySql().database=="codex_perf041_20261018_r90"&&config.MySql().pool_size==1,"isolated r89 pool1");
        Require(tinyimx::Logger::Instance().Init(config.Logger()),"logger");
        tinyimx::MySqlConnectionPool pool;Require(pool.Initialize(config.MySql()),"pool");
        tinyimx::MessageRepository repository(&pool);tinyimx::outbox::OutboxRepository outbox(&pool);
        tinyimx::message::MessageRepositoryAdapter adapter(&repository,&pool,&outbox,config.RocketMQ().message_topic);
        const auto prefix=std::string("p096-r90-")+argv[2]+"-";
        // Twenty repetitions per size/arm; raw times and actual statement counts
        // are retained. No production-capacity or all-message latency claim.
        for(const std::size_t size:{1,2,4,8,16,64}) {
            std::vector<tinyimx::message::PrivateMessagePersistInput> inputs;
            std::vector<double> elapsed;
            const auto before=Counters(pool);
            for(std::size_t iteration=0;iteration<20;++iteration) {
                inputs.clear();
                for(std::size_t i=0;i<size;++i)inputs.push_back({500001,500002,
                    prefix+std::to_string(size)+"-"+std::to_string(iteration)+"-"+std::to_string(i),1,std::string(128,'x')});
                const auto start=std::chrono::steady_clock::now();
                const auto result=adapter.PersistPrivateMessages(inputs);
                elapsed.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
                Require(result.size()==size,"batch result shape");
                for(const auto& item:result)Require(item.Accepted()&&item.outcome==tinyimx::message::PersistPrivateMessageOutcome::kCreated,"every input created");
            }
            const auto after=Counters(pool);
            std::cout<<"NATIVE_SQL_POINT {\"arm\":\""<<argv[2]<<"\",\"batch\":"<<size<<",\"repetitions\":20,\"times_ms\":[";
            for(std::size_t i=0;i<elapsed.size();++i){if(i)std::cout<<',';std::cout<<elapsed[i];}
            std::cout<<"],\"statement_deltas\":{";
            std::size_t j=0;for(const auto& [name,value]:after){if(j++)std::cout<<',';std::cout<<'"'<<name<<"\":"<<value-before.at(name);}
            std::cout<<"}}\n";
        }
        Require(pool.AvailableCount()==1,"lease returned");pool.Shutdown();tinyimx::Logger::Instance().Shutdown();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';tinyimx::Logger::Instance().Shutdown();return 1;}
}
