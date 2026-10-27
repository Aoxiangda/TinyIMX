// Independent read-only audit, including persisted-but-unjournalled messages.
#include <rocksdb/db.h>
#include <nlohmann/json.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
using Json=nlohmann::json;
namespace fs=std::filesystem;
static void Require(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
static std::string Hex(std::uint64_t n){
    std::string out(16,'0');for(int i=15;i>=0;--i){out[i]="0123456789abcdef"[n&15];n>>=4;}return out;
}
static std::uint64_t Number(const std::string& v){
    Require(v.size()==8,"invalid numeric value");std::uint64_t n=0;for(unsigned char c:v)n=(n<<8)|c;return n;
}

static std::string Payload(const std::string& cid,std::size_t size,const std::string& pattern){
    Require(size>cid.size(),"payload size must include CID delimiter");
    std::string out=cid+"|";out.reserve(size);
    if(pattern=="repeat"){out.append(size-out.size(),'x');return out;}
    Require(pattern=="xorshift64","explicit payload pattern");
    std::uint64_t state=14695981039346656037ULL;
    for(unsigned char c:cid)state=(state^c)*1099511628211ULL;
    if(!state)state=1;
    constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    while(out.size()<size){state^=state>>12;state^=state<<25;state^=state>>27;
        out.push_back(chars[(state*2685821657736338717ULL)>>58]);}
    return out;
}

struct NoLog:rocksdb::Logger{void Logv(const char*,va_list)override{}};
struct Expected{std::uint64_t from,to;std::string cid;bool found{false};};
int main(int argc,char**argv){try{
    if(argc==5&&std::string(argv[1])=="--payload-vector"){std::cout<<Payload(argv[2],std::stoull(argv[3]),argv[4]);return 0;}
    Require(argc==12,"owned-db expected-jsonl known-count actual-persisted run users planned SQL-derived-weights payload-bytes expected-pending-events");
    const auto dbpath=fs::canonical(argv[1]),inputpath=fs::canonical(argv[2]);
    const auto rel=dbpath.lexically_relative("/mnt/tinyimx-benchmark");
    Require(!rel.empty()&&*rel.begin()!=".."&&dbpath.filename()=="owner"&&inputpath.parent_path()==dbpath.parent_path(),"exact owned closed paths");
    const auto known=std::stoull(argv[3]),actual=std::stoull(argv[4]);
    const std::string run(argv[5]);const auto users=std::stoull(argv[6]),planned=std::stoull(argv[7]);
    const auto payload=std::stoull(argv[9]),expectedEvents=std::stoull(argv[10]);
    const std::string pattern(argv[11]);Require(pattern=="repeat"||pattern=="xorshift64","declared payload corpus");
    Require((users==20000||users==40000||users==50000||users==60000||users==64000)&&planned<=8000000&&0<known&&known<=actual&&actual<=planned&&run.size()<40,"explicit bounded partial denominator");
    Require((payload==128||payload==256||payload==1024||payload==4096||payload==16384)&&expectedEvents<=actual,"explicit payload and durable pending budget");
    std::ifstream weightInput(argv[8]);Json weightJson;weightInput>>weightJson;
    Require(weightJson.at("version")=="8.0.40"&&weightJson.at("collation")=="utf8mb4_unicode_ci","same actual SQL collation authority");
    std::array<std::string,128> weights;
    for(const auto& item:weightJson.at("weights").items()){
        const auto code=std::stoul(item.key());const auto value=item.value().get<std::string>();
        Require(code<128&&value.size()==4&&value.find_first_not_of("0123456789ABCDEF")==std::string::npos,"actual SQL character weight");
        weights[code]=value;
    }
    std::unordered_map<std::uint64_t,Expected> expected;
    std::ifstream input(inputpath);Require(input.good(),"expected input readable");std::string line;
    while(std::getline(input,line)){
        auto j=Json::parse(line);auto mid=j.at("mid").get<std::uint64_t>();
        Require(mid>0&&j.at("delivery_mid").get<std::uint64_t>()==mid&&j.at("ack_ns").get<std::int64_t>()>0&&j.at("delivery_ns").get<std::int64_t>()>0,"known completion contract");
        Require(expected.emplace(mid,Expected{j.at("from").get<std::uint64_t>(),j.at("to").get<std::uint64_t>(),j.at("cid").get<std::string>()}).second,"duplicate known MID");
    }
    Require(input.eof()&&expected.size()==known,"exact known-completion count");
    rocksdb::Options options;options.create_if_missing=false;options.info_log=std::make_shared<NoLog>();
    std::unique_ptr<rocksdb::DB> db;
    Require(rocksdb::DB::OpenForReadOnly(options,dbpath.string(),&db).ok(),"read-only real engine opens");
    rocksdb::ReadOptions read;read.verify_checksums=true;
    std::vector<bool> seen(planned);std::vector<std::uint64_t> received(users),pendingByUser(users);
    std::size_t messages=0,pending=0,events=0,cids=0,histories=0,confirmed=0,extraConfirmed=0,knownUnconfirmed=0;
    Json extra=Json::array();
    std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(read));
    for(it->SeekToFirst();it->Valid();it->Next()){
        const auto key=it->key().ToString();
        if(key.rfind("P/",0)==0)++pending;
        if(key.rfind("E/",0)==0)++events;
        if(key.rfind("I/",0)==0)++cids;
        if(key.rfind("H/",0)==0)++histories;
        if(key.rfind("M/",0)!=0)continue;
        auto j=Json::parse(it->value().ToString());const auto mid=j.at("message_id").get<std::uint64_t>();
        const auto from=j.at("from").get<std::uint64_t>(),to=j.at("to").get<std::uint64_t>();
        const auto cid=j.at("client_message_id").get<std::string>();
        Require(key=="M/"+Hex(mid),"MID key and record mismatch");
        const auto prefix="w"+run+"-u"+std::to_string(from)+"-n";
        Require(cid.rfind(prefix,0)==0&&cid.size()>prefix.size(),"persisted CID outside actual declared run");
        const auto suffix=cid.substr(prefix.size());
        Require(suffix.find_first_not_of("0123456789")==std::string::npos,"CID index syntax");
        const auto n=std::stoull(suffix);
        Require(n<planned&&!seen[n]&&from==500001+n%users&&to==500001+((from-500001)^1)&&cid==prefix+std::to_string(n),"actual schedule identity or duplicate CID index");
        seen[n]=true;++received[to-500001];
        const auto state=j.at("state").get<unsigned>();Require(state<=1,"unexpected read/state mutation in write-only cohort");
        const bool done=state==1;confirmed+=done;pendingByUser[to-500001]+=!done;
        Require((done&&j.at("confirmed_at").get<std::string>().size()==19)||(!done&&j.at("confirmed_at").get<std::string>().empty()),"confirmation state/timestamp disagree");
        Require(j.at("type").get<unsigned>()==1,"message type");
        const auto body=Json::parse(j.at("content").get<std::string>());
        Require(cid.size()<payload&&body.at("from").get<std::uint64_t>()==from&&body.at("to").get<std::uint64_t>()==to&&
            body.at("text").get<std::string>()==Payload(cid,payload,pattern),"exact persisted binary payload/schedule mismatch");
        std::string value;
        std::string canonical;
        for(unsigned char c:cid){Require(c<128&&!weights[c].empty(),"unqualified CID alphabet");canonical+=weights[c];}
        Require(db->Get(read,"K/"+Hex(mid),&value).ok()&&value==canonical,"reverse CID weight differs from actual SQL authority");
        Require(db->Get(read,"I/"+Hex(from)+'/'+canonical,&value).ok()&&Number(value)==mid,"CID index does not identify persisted MID");
        Require(db->Get(read,"H/"+Hex(std::min(from,to))+'/'+Hex(std::max(from,to))+'/'+Hex(mid),&value).ok()&&value.empty(),"history index does not identify persisted MID");
        auto pendingStatus=db->Get(read,"P/"+Hex(to)+'/'+Hex(mid),&value);
        Require(done?pendingStatus.IsNotFound():pendingStatus.ok(),"receiver pending index disagrees with confirmation");
        const auto found=expected.find(mid);
        if(found!=expected.end()){
            auto& e=found->second;
            Require(!e.found&&from==e.from&&to==e.to&&cid==e.cid,"known completed identity differs");
            e.found=true;knownUnconfirmed+=!done;
        }else{
            extraConfirmed+=done;
            extra.push_back(Json{{"index",n},{"mid",mid},{"receiver_confirmed",done}});
        }
        ++messages;
    }
    Require(it->status().ok(),"checksum/iterator error");
    Require(messages==actual&&cids==messages&&histories==messages&&events==expectedEvents,"actual declared persisted/index/publication counts");
    std::uint64_t sumPending=0;
    for(unsigned i=0;i<users;++i){
        const auto uid=500001+i,peer=500001+(i^1);std::string value;
        for(const auto& pair:std::vector<std::pair<std::string,std::uint64_t>>{
            {"Q/"+Hex(uid),pendingByUser[i]},{"N/"+Hex(uid),received[i]},
            {"D/"+Hex(uid)+'/'+Hex(peer),received[i]}}){
            const auto status=db->Get(read,pair.first,&value);
            Require((status.IsNotFound()&&pair.second==0)||(status.ok()&&Number(value)==pair.second),"receiver pending/unread counter mismatch");
        }
        sumPending+=pendingByUser[i];
    }
    Require(pending==sumPending&&confirmed+pending==messages,"pending set and state accounting");
    for(const auto& [mid,e]:expected)Require(e.found,"missing persisted known-completed message");
    std::cout<<Json{{"status",knownUnconfirmed?"KNOWN_COMPLETED_RECEIVER_CONFIRMATION_GAP_FOUND":"ALL_KNOWN_COMPLETIONS_DURABLE_AND_ALL_ACTUAL_EXTRAS_CLASSIFIED"},
        {"known_client_complete",known},{"actual_persisted",messages},{"known_complete_unconfirmed",knownUnconfirmed},
        {"persisted_outside_completed_journal",messages-known},{"extras_receiver_confirmed",extraConfirmed},
        {"all_receiver_confirmed",confirmed},{"receiver_pending",pending},{"pending_events",events},
        {"all_CID_history_unread_and_pending_indexes_match",true},{"payload_pattern",pattern},{"payload_bytes",payload},{"exact_expected_pending_events",expectedEvents},{"received_by_user",received},{"read_only",true},
        {"unjournalled_persisted_rows",extra}}.dump()<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"Read-only partial audit failed: "<<e.what()<<'\n';return 1;}}
