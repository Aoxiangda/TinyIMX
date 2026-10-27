// PERF080: real frozen Redis Lua APIs, controlled order of two cache writers.
// SQL truth is a fixture value (4), not an actual database or timing replay.
#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "services/cache/UnreadCountCache.h"
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    using namespace tinyimx;
    if(argc!=3||std::string(argv[2])!="--owned-isolated-redis")return 2;
    Config config;
    if(!config.LoadFromFile(argv[1])||config.Redis().host!="127.0.0.1"||
       config.Redis().port!=16392||config.Redis().db!=0||config.Redis().pool_size!=1)return 2;
    RedisConnectionPool pool;
    if(!pool.Initialize(config.Redis()))return 2;
    UnreadCountCache cache(&pool,"tinyimx:codex:perf080:r54:");
    auto require=[](bool ok){if(!ok)throw std::runtime_error("owned interleaving invariant failed");};
    unsigned double_writer_failures=0,single_writer_passes=0,gateway_first_passes=0;
    for(std::uint64_t i=0;i<100;++i) {
        const auto user=810000+i*10,peer=user+1,mid=900000+i*10;
        // The projector snapshot already includes the newly committed MID.
        require(cache.SetUnreadSnapshot(user,peer,4,4).Succeeded());
        const auto late=cache.EnsurePrivateUnreadProjection(mid,user,peer,true,true);
        require(late.Succeeded()&&late.incremented);
        if(cache.GetPrivateUnread(user,peer).count==5&&cache.GetTotalUnread(user).count==5)
            ++double_writer_failures;
        // A gateway in projector-owner mode only reads these two counters.
        const auto single_user=user+2,single_peer=user+3;
        require(cache.SetUnreadSnapshot(single_user,single_peer,4,4).Succeeded());
        for(int read=0;read<8;++read) {
            require(cache.GetPrivateUnread(single_user,single_peer).count==4);
            require(cache.GetTotalUnread(single_user).count==4);
        }
        ++single_writer_passes;
        // Reverse order can mask the invalid double-writer configuration.
        const auto first_user=user+4,first_peer=user+5;
        require(cache.SetUnreadSnapshot(first_user,first_peer,3,3).Succeeded());
        require(cache.EnsurePrivateUnreadProjection(mid+1,first_user,first_peer,true,true).Succeeded());
        require(cache.SetUnreadSnapshot(first_user,first_peer,4,4).Succeeded());
        require(cache.GetPrivateUnread(first_user,first_peer).count==4&&cache.GetTotalUnread(first_user).count==4);
        ++gateway_first_passes;
    }
    std::cout<<"RESULT double_writer_wrong="<<double_writer_failures
             <<" single_writer_correct="<<single_writer_passes
             <<" reverse_order_masks_bug="<<gateway_first_passes<<std::endl;
    pool.Shutdown();
    return double_writer_failures==100&&single_writer_passes==100&&gateway_first_passes==100?0:1;
}
