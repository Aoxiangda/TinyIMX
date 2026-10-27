#include "services/message/local/NativeSstWritebackPolicy.h"
#include <rocksdb/db.h>
#include <rocksdb/write_batch.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void Check(bool value,const char* message){
    if(!value)throw std::runtime_error(message);
    std::cout<<"PASS "<<message<<'\n';
}
std::string Key(unsigned n){return "message/"+std::to_string(1000000+n);}
std::string Value(unsigned n){
    std::string result(8192,' ');
    for(unsigned i=0;i<result.size();++i)result[i]=static_cast<char>((i*37+n*17)%251);
    return result;
}
}
int main(int argc,char**argv){try{
    Check(argc==3,"explicit owned test directory and policy0/1 required");
    const std::filesystem::path root(argv[1]);const std::string mode(argv[2]);
    Check(mode=="0"||mode=="1","exact policy value");
    const auto relative=root.lexically_relative("/mnt/tinyimx-benchmark");
    Check(root.is_absolute()&&!relative.empty()&&*relative.begin()!=".."&&
        root.filename()=="sst-writeback-d"+mode&&root.parent_path().filename()=="tests"&&
        std::filesystem::canonical(root.parent_path())==root.parent_path()&&!std::filesystem::exists(root),
        "fresh canonical private test directory");
    Check(::setenv("TINYIMX_NATIVE_SST_SMOOTH_WRITEBACK_ENABLE",mode.c_str(),1)==0,"select actual candidate policy");
    rocksdb::Options options;options.create_if_missing=true;
    options.compression=rocksdb::kNoCompression;options.enable_pipelined_write=true;
    options.write_buffer_size=1024*1024;options.max_write_buffer_number=2;
    options.max_background_jobs=2;
    tinyimx::message::local::ApplyNativeSstWritebackPolicy(options);
    std::unique_ptr<rocksdb::DB> db;
    Check(rocksdb::DB::Open(options,root.string(),&db).ok(),"real pinned engine opens");
    const auto actual=db->GetOptions();
    Check(actual.bytes_per_sync==(mode=="1"?512*1024:0)&&actual.wal_bytes_per_sync==0&&
        actual.compression==rocksdb::kNoCompression&&actual.enable_pipelined_write,
        "actual engine changes only SST range writeback cadence");
    rocksdb::WriteOptions write;write.sync=true;write.disableWAL=false;
    constexpr unsigned records=6144;
    for(unsigned first=0;first<records;first+=64){
        rocksdb::WriteBatch batch;
        for(unsigned n=first;n<first+64;++n)batch.Put(Key(n),Value(n));
        Check(db->Write(write,&batch).ok(),"synchronous WAL batch survives SST turnover");
    }
    rocksdb::FlushOptions flush;flush.wait=true;
    Check(db->Flush(flush).ok(),"all immutable memtables flush with candidate enabled");
    Check(db->CompactRange(rocksdb::CompactRangeOptions{},nullptr,nullptr).ok(),"real SST compaction completes");
    std::uint64_t size=0;Check(db->GetIntProperty("rocksdb.total-sst-files-size",&size)&&size>0,
        "test actually generated persisted SST files");
    Check(db->Close().ok(),"compacted engine closes");db.reset();
    options.create_if_missing=false;
    Check(rocksdb::DB::Open(options,root.string(),&db).ok(),"same-format engine reopens without replaying application traffic");
    std::string value;
    for(unsigned n=0;n<records;++n)
        if(!db->Get(rocksdb::ReadOptions{},Key(n),&value).ok()||value!=Value(n))
            throw std::runtime_error("persisted exact binary payload mismatch at "+std::to_string(n));
    auto iterator=std::unique_ptr<rocksdb::Iterator>(db->NewIterator(rocksdb::ReadOptions{}));
    unsigned count=0;for(iterator->SeekToFirst();iterator->Valid();iterator->Next())++count;
    Check(iterator->status().ok()&&count==records,"all6144 records and exact payloads survive flush compaction and reopen");
    iterator.reset();Check(db->Close().ok(),"reopened owned engine closes cleanly");
    std::cout<<"ACTUAL_SST_WRITEBACK_SYNC_WAL_COMPACTION_REOPEN_PASS policy="<<mode<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
