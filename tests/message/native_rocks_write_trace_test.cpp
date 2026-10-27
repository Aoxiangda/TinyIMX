#include "services/message/local/NativeRocksWriteTrace.h"
#include <rocksdb/db.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using Trace=tinyimx::message::local::NativePersistencePhaseTrace;
using RocksTrace=tinyimx::message::local::NativeRocksWriteTrace;
namespace {
std::vector<Trace::Snapshot> saved;
void Capture(const Trace::Snapshot& value)noexcept{saved.push_back(value);}
void Check(bool value,const char* message){
    if(!value)throw std::runtime_error(message);
    std::cout<<"PASS "<<message<<'\n';
}
}
int main(int argc,char**argv){try{
    Check(argc==3,"explicit owned directory and pipelined0/1 required");
    const std::filesystem::path root(argv[1]);
    const std::string pipelined(argv[2]);
    Check(pipelined=="0"||pipelined=="1","exact pipeline value");
    const auto relative=root.lexically_relative("/mnt/tinyimx-benchmark");
    Check(root.is_absolute()&&!relative.empty()&&*relative.begin()!=".."&&
        root.filename()=="rocks-profile-d"+pipelined&&root.parent_path().filename()=="tests"&&
        std::filesystem::canonical(root.parent_path())==root.parent_path()&&!std::filesystem::exists(root),
        "fresh canonical private test path");
    saved.reserve(8);
    rocksdb::Options options;options.create_if_missing=true;
    options.compression=rocksdb::kNoCompression;
    options.enable_pipelined_write=pipelined=="1";
    options.max_background_jobs=1;options.write_buffer_size=1024*1024;
    std::unique_ptr<rocksdb::DB> db;
    Check(rocksdb::DB::Open(options,root.string(),&db).ok(),"actual pinned RocksDB opens owned test data");
    rocksdb::WriteOptions write;write.sync=true;write.disableWAL=false;
    const auto original=rocksdb::GetPerfLevel();
    rocksdb::SetPerfLevel(rocksdb::PerfLevel::kEnableCount);
    auto* counters=rocksdb::get_perf_context();
    const auto before=counters->write_wal_time;
    {
        RocksTrace no_request(true);
        Check(rocksdb::GetPerfLevel()==rocksdb::PerfLevel::kEnableCount,
            "no active request leaves the caller perf level untouched");
        Check(db->Put(write,"outside","real sync WAL").ok(),"outside request still writes normally");
    }
    Check(counters->write_wal_time==before,"inactive request did not secretly enable timing");
    {
        Trace trace(1,2,true,Capture);
        {
            RocksTrace scope(true);
            Check(rocksdb::GetPerfLevel()==rocksdb::PerfLevel::kEnableTime,"active scope enables actual mutex and write timing");
            Check(db->Put(write,"success","real durable success").ok(),"profiled write uses actual synchronous WAL");
        }
        Check(rocksdb::GetPerfLevel()==rocksdb::PerfLevel::kEnableCount,"successful scope restores prior caller level");
        trace.Finish(0,0,1);
    }
    Check(saved.size()==1&&saved.back().rocks_write_calls==1&&saved.back().rocks_ns[Trace::RocksWal]>0&&
        static_cast<std::uint64_t>(saved.back().rocks_ns[Trace::RocksWal])==counters->write_wal_time-before,
        "snapshot records only actual WAL counter delta without resetting earlier counters");
    rocksdb::SetPerfLevel(rocksdb::PerfLevel::kEnableTimeExceptForMutex);
    const auto committed=counters->write_wal_time;
    {
        Trace trace(3,4,true,Capture);
        bool seen=false;
        try{
            RocksTrace scope(true);
            Check(db->Put(write,"uncertain","committed before exception").ok(),"exception fixture commits real owned data");
            throw std::runtime_error("sentinel after durable write");
        }catch(const std::runtime_error& error){seen=std::string(error.what())=="sentinel after durable write";}
        Check(seen&&rocksdb::GetPerfLevel()==rocksdb::PerfLevel::kEnableTimeExceptForMutex,
            "exception preserves exact error and restores caller instrumentation");
        trace.Finish(6,-1,2);
    }
    Check(saved.size()==2&&saved.back().rocks_write_calls==1&&
        static_cast<std::uint64_t>(saved.back().rocks_ns[Trace::RocksWal])==counters->write_wal_time-committed,
        "uncertain result still retains its actual engine measurements");
    {
        Trace trace(5,6,true,Capture);
        {RocksTrace disabled(false);Check(db->Put(write,"disabled","caller instrumented only").ok(),"disabled tracer preserves write");}
        Check(rocksdb::GetPerfLevel()==rocksdb::PerfLevel::kEnableTimeExceptForMutex,"disabled tracer respects an already instrumented caller");
        trace.Finish(0,0,3);
    }
    Check(saved.back().rocks_write_calls==0&&saved.back().rocks_ns[Trace::RocksWal]==-1,
        "disabled tracer does not label external counter changes as collected measurements");
    rocksdb::SetPerfLevel(original);
    Check(db->Close().ok(),"owned real engine closes cleanly");
    std::cout<<"ACTUAL_ROCKS_PERF_CONTEXT_LEVEL_DELTA_EXCEPTION_CONTRACTS_PASS pipeline="<<pipelined<<'\n';
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
