// Standalone with the pinned SDK headers; no active library or broker mutation.
#include "rocketmq/Logger.h"
#ifdef TINYIMX_TEST_FMT9_FIRST
#include "fmt/format.h"
#endif
#include "spdlog/spdlog.h"
#include "LazyLogValue.h"
#include "spdlog/async_logger.h"
#include "spdlog/details/thread_pool.h"
#include "spdlog/sinks/ostream_sink.h"
#include <atomic>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
namespace rl=ROCKETMQ_NAMESPACE::logging;
namespace {
void Check(bool yes,const char*name){if(!yes)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
std::string Produce(std::atomic<unsigned>&count){++count;return "owned debug body";}
}
int main(int argc,char**argv){try{
    if(argc!=2||(std::string(argv[1])!="0"&&std::string(argv[1])!="1"))return 2;
    const bool lazy=std::string(argv[1])=="1";
    Check(rl::LazyDebugFormattingEnabled()==lazy,"effective process flag matches declared arm");
#ifdef TINYIMX_TEST_FMT9_FIRST
    static_assert(FMT_VERSION==90000,"pinned SDK external fmt first");
#else
    static_assert(FMT_VERSION==80101,"pinned SDK spdlog bundled fmt first");
#endif
    std::ostringstream output;
    std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink(new spdlog::sinks::ostream_sink_mt(output));
    spdlog::logger logger("owned-lazy-unit",sink);logger.set_pattern("%v");logger.set_level(spdlog::level::info);
    std::atomic<unsigned> count(0);
#if SPDLOG_ACTIVE_LEVEL > SPDLOG_LEVEL_DEBUG
    SPDLOG_LOGGER_DEBUG(&logger,"{}",rl::MakeLazyLogValue([&]{return Produce(count);}));
    Check(count==0&&output.str().empty(),"compiled-out debug never constructs or evaluates either arm");
    std::cout<<"LAZY_SDK_COMPILED_OUT_CONTRACT_PASS\n";return 0;
#else
    std::atomic<unsigned> eager(0);
    SPDLOG_LOGGER_DEBUG(&logger,"{}",Produce(eager));
    Check(eager==1&&output.str().empty(),"original eager argument executes at disabled INFO level");
    SPDLOG_LOGGER_DEBUG(&logger,"{}",rl::MakeLazyLogValue([&]{return Produce(count);}));
    Check(count==(lazy?0u:1u)&&output.str().empty(),"opt-in lazy mode avoids disabled formatting while OFF keeps original evaluation");
    const unsigned before=count.load();logger.set_level(spdlog::level::debug);
    SPDLOG_LOGGER_DEBUG(&logger,"payload={}",rl::MakeLazyLogValue([&]{return Produce(count);}));
    Check(count==before+1&&output.str()=="payload=owned debug body\n","enabled debug formats exactly once with identical text");
    output.str("");output.clear();
    SPDLOG_LOGGER_DEBUG(&logger,"{:>20.5}",rl::MakeLazyLogValue([&]{return Produce(count);}));
    Check(output.str()==std::string(15,' ')+"owned\n","custom formatter preserves width and precision");
    output.str("");output.clear();logger.set_level(spdlog::level::info);logger.enable_backtrace(8);
    const unsigned trace_before=count.load();
    SPDLOG_LOGGER_DEBUG(&logger,"trace={}",rl::MakeLazyLogValue([&]{return Produce(count);}));
    Check(count==trace_before+1&&output.str().empty(),"disabled debug still captures backtrace through the logger");
    logger.dump_backtrace();
    Check(output.str().find("trace=owned debug body")!=std::string::npos,"backtrace retains original debug payload");
    logger.disable_backtrace();output.str("");output.clear();
    const unsigned concurrent_before=count.load();std::vector<std::thread> workers;
    for(int t=0;t<4;++t)workers.push_back(std::thread([&]{for(int i=0;i<100;++i)SPDLOG_LOGGER_DEBUG(&logger,"{}",rl::MakeLazyLogValue([&]{return Produce(count);}));}));
    for(std::size_t i=0;i<workers.size();++i)workers[i].join();
    Check(count==concurrent_before+(lazy?0u:400u)&&output.str().empty(),"concurrent disabled callbacks retain zero lazy producer calls");
    logger.set_level(spdlog::level::debug);std::string expected;
    {
        std::string ephemeral(4096,'z');expected=ephemeral;
        SPDLOG_LOGGER_DEBUG(&logger,"{}",rl::MakeLazyLogValue([&]{++count;return ephemeral;}));
        ephemeral.assign("changed");
    }
    Check(output.str()==expected+"\n","temporary formatted storage lives until synchronous sink consumption");
    output.str("");output.clear();
    {
        std::shared_ptr<spdlog::details::thread_pool> pool(new spdlog::details::thread_pool(32,1));
        std::shared_ptr<spdlog::async_logger> async(new spdlog::async_logger("owned-async-unit",sink,pool,spdlog::async_overflow_policy::block));
        async->set_pattern("%v");async->set_level(spdlog::level::debug);
        {
            std::string ephemeral="async original";
            SPDLOG_LOGGER_DEBUG(async.get(),"{}",rl::MakeLazyLogValue([&]{++count;return ephemeral;}));
            ephemeral="async mutated";
        }
        async->flush();pool.reset(); // joins the FIFO worker after accepted records
    }
    Check(output.str()=="async original\n","async logger owns formatted bytes before captured input expires");
    std::cout<<"LAZY_SDK_REAL_SPDLOG_LEVEL_BACKTRACE_CONCURRENCY_LIFETIME_PASS fmt="<<FMT_VERSION<<" lazy="<<lazy<<'\n';return 0;
#endif
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
