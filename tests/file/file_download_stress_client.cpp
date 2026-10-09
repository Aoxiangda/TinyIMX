#include "tinyimx/file/v1/file_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <openssl/evp.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
std::string Sha256Hex(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE]{}; unsigned int size=0;
    if (EVP_Digest(input.data(), input.size(), digest, &size, EVP_sha256(), nullptr) != 1) return {};
    std::ostringstream out; out<<std::hex<<std::setfill('0');
    for(unsigned int i=0;i<size;++i) out<<std::setw(2)<<static_cast<unsigned int>(digest[i]);
    return out.str();
}
std::string ReadAll(const std::filesystem::path& path){ std::ifstream in(path,std::ios::binary); return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()}; }
std::map<std::string,std::string> LoadState(const std::filesystem::path& path){ std::map<std::string,std::string>s; std::ifstream in(path); std::string line; while(std::getline(in,line)){auto p=line.find('='); if(p!=std::string::npos)s[line.substr(0,p)]=line.substr(p+1);} return s; }
std::uint64_t U64(const std::map<std::string,std::string>&s,const std::string&k){try{return std::stoull(s.at(k));}catch(...){return 0;}}
double Percentile(std::vector<double> v,double p){ if(v.empty()) return 0; std::sort(v.begin(),v.end()); const auto i=static_cast<std::size_t>(std::ceil(p*v.size()))-1; return v[std::min(i,v.size()-1)]; }
}
int main(int argc,char*argv[]){
    if(argc<6){std::cerr<<"usage: file_download_stress_client <target> <state_dir> <threads> <ops_per_thread> <range_size>\n";return 64;}
    const std::string target=argv[1]; const std::filesystem::path dir=argv[2];
    const int threads=std::stoi(argv[3]), ops=std::stoi(argv[4]); const std::uint64_t range_size=std::stoull(argv[5]);
    if(threads<=0||threads>64||ops<=0||ops>10000||range_size==0||range_size>1024ULL*1024ULL)return 65;
    const auto state=LoadState(dir/"state.env"); const auto actor=U64(state,"actor"), file_id=U64(state,"file_id"), total=U64(state,"total_size");
    const std::string sha=state.count("sha256")?state.at("sha256"):""; const std::string expected=ReadAll(dir/"expected.bin");
    if(actor==0||file_id==0||total==0||expected.size()!=total||sha.size()!=64)return 66;
    std::atomic<std::uint64_t> failures{0}, bytes{0}, max_response{0}; std::mutex latency_mutex; std::vector<double> latencies;
    const auto wall_start=std::chrono::steady_clock::now();
    std::vector<std::thread> workers; workers.reserve(threads);
    for(int t=0;t<threads;++t) workers.emplace_back([&,t]{
        auto stub=tinyimx::file::v1::FileService::NewStub(grpc::CreateChannel(target,grpc::InsecureChannelCredentials()));
        std::vector<double> local; local.reserve(ops);
        for(int i=0;i<ops;++i){
            const std::uint64_t slots=std::max<std::uint64_t>(1,(total+range_size-1)/range_size);
            const std::uint64_t slot=(static_cast<std::uint64_t>(t)*131ULL+static_cast<std::uint64_t>(i)*17ULL)%slots;
            const std::uint64_t offset=std::min(slot*range_size,total-1);
            tinyimx::file::v1::ReadFileRangeRequest req; req.set_actor_user_id(actor);req.set_file_id(file_id);req.set_offset(offset);req.set_length(range_size);req.set_if_match_sha256(sha);
            grpc::ClientContext ctx; ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(5)); tinyimx::file::v1::ReadFileRangeResponse res;
            const auto start=std::chrono::steady_clock::now(); const auto status=stub->ReadFileRange(&ctx,req,&res); const auto end=std::chrono::steady_clock::now();
            local.push_back(std::chrono::duration<double,std::milli>(end-start).count());
            const std::string want=expected.substr(static_cast<std::size_t>(offset),res.data().size());
            if(!status.ok()||res.offset()!=offset||res.data().empty()||res.data()!=want||res.range_sha256()!=Sha256Hex(res.data())){++failures;continue;}
            const auto response_bytes = static_cast<std::uint64_t>(res.data().size());
            bytes.fetch_add(response_bytes, std::memory_order_relaxed);
            std::uint64_t cur=max_response.load(std::memory_order_relaxed);
            while(cur<response_bytes && !max_response.compare_exchange_weak(
                cur, response_bytes, std::memory_order_relaxed)){}
        }
        std::lock_guard<std::mutex> lock(latency_mutex); latencies.insert(latencies.end(),local.begin(),local.end());
    });
    for(auto& w:workers)w.join(); const auto wall_end=std::chrono::steady_clock::now();
    const double seconds=std::chrono::duration<double>(wall_end-wall_start).count(); const double mibps=seconds>0?(bytes.load()/1048576.0)/seconds:0;
    const double p50=Percentile(latencies,.50),p95=Percentile(latencies,.95),p99=Percentile(latencies,.99);
    std::cout<<std::fixed<<std::setprecision(3)
             <<"M18_C2_STRESS threads="<<threads<<" ops_per_thread="<<ops<<" total_ops="<<(threads*ops)
             <<" failures="<<failures.load()<<" bytes="<<bytes.load()<<" mibps="<<mibps
             <<" p50_ms="<<p50<<" p95_ms="<<p95<<" p99_ms="<<p99<<" max_response="<<max_response.load()<<'\n';
    if(failures.load()!=0||max_response.load()>1024ULL*1024ULL)return 1;
    return 0;
}
