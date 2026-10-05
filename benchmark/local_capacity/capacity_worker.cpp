// Local capacity worker: normal hold/private traffic, NOT a failover/full-feature runner.
// Reuses TinyIMX's real Packet/ProtocolCodec/Buffer. No service code is modified.
#include "common/protocol/ProtocolCodec.h"
#include "benchmark/local_capacity/OfferedSchedule.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using Clock = std::chrono::steady_clock;
using TP = Clock::time_point;
using Tree = boost::property_tree::ptree;
using tinyimx::MessageType;
using tinyimx::Packet;
namespace fs = std::filesystem;
static std::uint64_t ns(TP t) { return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count(); }
static double ms(TP a, TP b) { return std::chrono::duration<double,std::milli>(b-a).count(); }
static std::string quote(const std::string& s) {
    std::ostringstream o; o<<'"';
    for(unsigned char c:s) { if(c=='"'||c=='\\') o<<'\\'<<char(c); else if(c<32) o<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<int(c)<<std::dec; else o<<char(c); }
    return o.str()+'"';
}
static std::uint64_t number(const std::string& s) {
    if(s.empty()||!std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';})) throw std::runtime_error("INVALID_UNSIGNED");
    std::size_t p=0; auto x=std::stoull(s,&p); if(p!=s.size())throw std::runtime_error("INVALID_UNSIGNED"); return x;
}
static unsigned small_number(const std::string& s){auto v=number(s);if(v>std::numeric_limits<unsigned>::max())throw std::runtime_error("INTEGER_OVERFLOW");return static_cast<unsigned>(v);}
static bool token(const std::string& s) { return !s.empty()&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';}); }
static Tree parse(const std::string& s) {
    std::istringstream in(s); Tree t; boost::property_tree::read_json(in,t);
    std::set<std::string> seen; for(const auto& x:t) if(x.first.empty()||!seen.insert(x.first).second)throw std::runtime_error("DUPLICATE_OR_NONOBJECT_JSON");
    return t;
}
static std::string field(const Tree& t,const std::string& k) { auto x=t.get_child_optional(k); if(!x||!x->empty())throw std::runtime_error("MISSING_OR_NONSCALAR_FIELD:"+k);return x->data(); }
static std::uint64_t uintfield(const Tree& t,const std::string& k){return number(field(t,k));}
static void atomic(const fs::path& p,const std::string& s) {
    auto tmp=p; tmp+=".tmp"; {std::ofstream o(tmp,std::ios::trunc);o.exceptions(std::ios::badbit|std::ios::failbit);o<<s<<'\n';}fs::rename(tmp,p);
}
struct Histogram {
    // Fixed finite bins, plus an explicit overflow bin. Raw max is never clamped.
    static constexpr std::uint64_t step_us=100, cap_us=60'000'000;
    std::map<std::uint64_t,std::uint64_t> bins;
    std::uint64_t count=0, maximum=0; long double sum=0;
    void add(double value_ms) { if(!std::isfinite(value_ms)||value_ms<0)throw std::runtime_error("BAD_LATENCY");auto u=static_cast<std::uint64_t>(std::ceil(value_ms*1000));auto b=u>cap_us?cap_us+step_us:((u+step_us-1)/step_us)*step_us;++bins[b];++count;sum+=u;maximum=std::max(maximum,u); }
    std::string json()const {std::ostringstream o;o<<"{\"step_us\":"<<step_us<<",\"cap_us\":"<<cap_us<<",\"count\":"<<count<<",\"sum_us\":"<<std::fixed<<std::setprecision(0)<<sum<<",\"max_us\":"<<maximum<<",\"bins\":[";bool first=true;for(auto [b,n]:bins){if(!first)o<<',';first=false;o<<'['<<b<<','<<n<<']';}return o.str()+"]}";}
};
struct Config {
    std::string host="127.0.0.1", source="127.0.0.2",prefix="m21b500000_",run, mode="private";
    fs::path out, control;
    std::uint64_t base=500000, total=10000,offset=0,count=10000;
    unsigned worker=0,port=9000,duration=300,hb=15,drain=10,verify=90;
    double rate=100,ramp=100;
    std::string password;
    bool login_trace{false};
};
static void usage() {std::cout<<"tinyimx_capacity_worker --out DIR --control DIR --run-id TOKEN [--connections N --total-users N --offset N --user-id-base N --username-prefix TOKEN --source-ip IPv4 --host IPv4 --port N --worker-id N --mode hold|private --rate N --ramp-per-sec N --duration N --heartbeat-seconds N --drain-seconds N]\nPassword: TINYIMX_BENCH_PASSWORD environment. Protocol: plaintext TCP; no reconnect, TLS, group publish, or 50k claim.\n";}
static Config args(int argc,char**argv) {
    Config c;const char*pw=std::getenv("TINYIMX_BENCH_PASSWORD");c.password=pw?pw:"123456";
    const char*lt=std::getenv("TINYIMX_LOGIN_CLIENT_TRACE_ENABLE");c.login_trace=lt&&lt[0]=='1'&&lt[1]=='\0';
    for(int i=1;i<argc;++i){std::string k=argv[i];if(k=="--help"||k=="-h"){usage();std::exit(0);}if(i+1>=argc)throw std::runtime_error("MISSING_OPTION_VALUE");std::string v=argv[++i];
      if(k=="--out")c.out=v;else if(k=="--control")c.control=v;else if(k=="--run-id")c.run=v;else if(k=="--host")c.host=v;else if(k=="--source-ip")c.source=v;else if(k=="--username-prefix")c.prefix=v;else if(k=="--mode")c.mode=v;
      else if(k=="--connections")c.count=number(v);else if(k=="--total-users")c.total=number(v);else if(k=="--offset")c.offset=number(v);else if(k=="--user-id-base")c.base=number(v);
      else if(k=="--worker-id")c.worker=small_number(v);else if(k=="--port")c.port=small_number(v);else if(k=="--duration")c.duration=small_number(v);else if(k=="--heartbeat-seconds")c.hb=small_number(v);else if(k=="--drain-seconds")c.drain=small_number(v);else if(k=="--verify-timeout")c.verify=small_number(v);
      else if(k=="--rate"||k=="--ramp-per-sec"){std::size_t pos=0;double d=std::stod(v,&pos);if(pos!=v.size()||!std::isfinite(d)||d<0||d>100000)throw std::runtime_error("INVALID_RATE");if(k=="--rate")c.rate=d;else c.ramp=d;}
      else throw std::runtime_error("UNKNOWN_OPTION:"+k);
    }
    if(c.count<1||c.count>10000||c.total<1||c.total>50000||c.offset>c.total||c.count>c.total-c.offset||c.base==0||c.base>1000000000000ULL||c.port==0||c.port>65535||c.duration<1||c.duration>7200||c.hb<1||c.hb>60||c.drain<1||c.drain>60||c.verify<5||c.verify>300||c.ramp<=0||c.worker>63||!token(c.prefix)||c.prefix.size()>40||!token(c.run)||c.run.size()>20||c.out.empty()||c.control.empty()||(c.mode!="hold"&&c.mode!="private")||(c.mode=="private"&&c.rate<=0))throw std::runtime_error("INVALID_CONFIG");
    in_addr a{};if(::inet_pton(AF_INET,c.host.c_str(),&a)!=1||::inet_pton(AF_INET,c.source.c_str(),&a)!=1)throw std::runtime_error("IPV4_REQUIRED");
    if(!fs::is_directory(c.out)||!fs::is_directory(c.control)||(fs::exists(c.out/"final.json")||fs::exists(c.out/"ledger.tsv")))throw std::runtime_error("OUTPUT_DIRECTORY_GUARD");return c;
}
struct Conn {
    int fd=-1; int state=0; // 0 unused 1 connecting 2 login 3 online 4 closed
    tinyimx::Buffer input{1024}; std::deque<std::string> output; std::size_t output_offset=0,queued=0;
    std::uint32_t seq=1,login_seq=0,chat_seq=0,hb_seq=0;
    std::uint64_t uid=0,to=0,serial=0;std::string cid; TP login_start{},scheduled{},enqueued{},hb_sent{};
};
class Worker {
    Config c;std::vector<Conn> conns;int ep=-1; sockaddr_in target{},src{};tinyimx::ProtocolCodec codec{1024*1024};
    std::priority_queue<std::pair<TP,std::size_t>,std::vector<std::pair<TP,std::size_t>>,std::greater<std::pair<TP,std::size_t>>> heartbeats;
    std::ofstream ledger;std::map<std::string,std::uint64_t> m;
    void failure_detail(const Conn& x,const Packet& p,const Tree& t,const char* kind) {
        // Allowlist only response diagnostics; never write a raw body or credentials.
        auto safe=[&](const char* key){auto v=t.get<std::string>(key,"");
            if(!c.password.empty()){std::size_t at=0;while((at=v.find(c.password,at))!=std::string::npos){v.replace(at,c.password.size(),"[redacted]");at+=10;}}
            if(v.size()>512)v.resize(512);return quote(v);};
        std::ofstream o(c.out/"failure-responses.jsonl",std::ios::app);
        o<<"{\"kind\":"<<quote(kind)<<",\"monotonic_ns\":"<<ns(Clock::now())
         <<",\"expected_user_id\":"<<x.uid<<",\"expected_sequence\":"<<(x.state==2?x.login_seq:x.chat_seq)
         <<",\"response_sequence\":"<<p.seq<<",\"response_type\":"<<static_cast<unsigned>(p.type)
         <<",\"client_message_id\":"<<quote(x.cid)<<",\"success\":"<<safe("success")
         <<",\"observed_user_id\":"<<safe("user_id")<<",\"reason\":"<<safe("reason")
         <<",\"message\":"<<safe("message")<<",\"stored_persistent\":"<<safe("stored_persistent")<<"}\n";
        o.flush();if(!o)throw std::runtime_error("FAILURE_EVIDENCE_WRITE_FAILED");
    }
    Histogram login_latency,ack_latency,schedule_latency,ready_lag;
    std::uint64_t next_index=0,planned=0,online=0;std::size_t rr=0;
    TP began{},next_open{},last_control{},last_progress{},start{},end{},audit_at{};
    bool ready=false,started=false,audit_ready=false,normal_release=false,end_accounted=false;
    bool stop_heartbeats=false,heartbeat_drained=false;
    TP setup_deadline{};std::string terminal="RUNNING";
    std::uint64_t uid(std::size_t i)const{return c.base+c.offset+i+1;}
    std::uint64_t peer(std::size_t i)const{return c.base+(c.offset+i+1)%c.total+1;}
    std::string username(std::size_t i)const{std::ostringstream o;o<<c.prefix<<std::setw(6)<<std::setfill('0')<<(c.offset+i+1);return o.str();}
    std::uint32_t seq(Conn& x){auto v=x.seq++;if(v==0)throw std::runtime_error("SEQ_EXHAUSTED");return v;}
    void event(const char* kind,const Conn& x,std::uint64_t mid=0,std::uint32_t d=0) {
        ledger<<kind<<'\t'<<x.uid<<'\t'<<x.to<<'\t'<<mid<<'\t'<<d<<'\t'<<x.cid<<'\t'<<ns(Clock::now())<<'\n';
        if(!ledger)throw std::runtime_error("LEDGER_WRITE_FAILED");
    }
    void interest(std::size_t i){auto&x=conns[i];if(x.fd<0)return;epoll_event e{};e.data.u32=static_cast<unsigned>(i);e.events=EPOLLIN|EPOLLRDHUP|EPOLLERR|EPOLLHUP;if(x.state==1||!x.output.empty())e.events|=EPOLLOUT;if(::epoll_ctl(ep,EPOLL_CTL_MOD,x.fd,&e)<0)throw std::runtime_error("EPOLL_MOD");}
    void queue(std::size_t i,MessageType type,std::uint32_t d,const std::string& body){auto&x=conns[i];Packet p;p.type=type;p.seq=d;p.body=body;tinyimx::Buffer b;std::string err;if(!codec.Encode(p,&b,&err))throw std::runtime_error("ENCODE");auto s=b.RetrieveAllAsString();if(x.queued+s.size()>262144)throw std::runtime_error("CLIENT_OUTPUT_LIMIT");x.queued+=s.size();x.output.push_back(std::move(s));interest(i);}
    void close(std::size_t i){auto&x=conns[i];if(x.fd<0)return;if(x.state==3){--online;++m["disconnects"];}::epoll_ctl(ep,EPOLL_CTL_DEL,x.fd,nullptr);::close(x.fd);x.fd=-1;x.state=4;}
    void connected(std::size_t i){auto&x=conns[i];x.state=2;++m["connected"];x.login_start=Clock::now();x.login_seq=seq(x);queue(i,MessageType::kLoginRequest,x.login_seq,"{\"username\":"+quote(username(i))+",\"password\":"+quote(c.password)+"}");if(c.login_trace&&x.uid%16==0)ledger<<"login_sent\t"<<x.uid<<'\t'<<x.to<<"\t0\t"<<x.login_seq<<"\t\t"<<ns(x.login_start)<<'\n';}
    void open(std::size_t i){auto&x=conns[i];x.uid=uid(i);x.to=peer(i);x.fd=::socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(x.fd<0)throw std::runtime_error("SOCKET");int one=1;
        if(::setsockopt(x.fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one))<0)throw std::runtime_error("TCP_NODELAY");
#ifdef IP_BIND_ADDRESS_NO_PORT
        if(::setsockopt(x.fd,IPPROTO_IP,IP_BIND_ADDRESS_NO_PORT,&one,sizeof(one))<0)throw std::runtime_error("IP_BIND_ADDRESS_NO_PORT");
#endif
        if(::bind(x.fd,reinterpret_cast<sockaddr*>(&src),sizeof(src))<0)throw std::runtime_error("SOURCE_BIND_ERRNO_"+std::to_string(errno));
        int rc=::connect(x.fd,reinterpret_cast<sockaddr*>(&target),sizeof(target));if(rc<0&&errno!=EINPROGRESS)throw std::runtime_error("CONNECT_ERRNO_"+std::to_string(errno));x.state=1;
        epoll_event e{};e.data.u32=i;e.events=EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLERR|EPOLLHUP;if(::epoll_ctl(ep,EPOLL_CTL_ADD,x.fd,&e)<0)throw std::runtime_error("EPOLL_ADD");if(rc==0)connected(i);
    }
    void packet(std::size_t i,const Packet&p){auto&x=conns[i];auto t=parse(p.body);auto now=Clock::now();
      if(p.type==MessageType::kLoginResponse){if(x.state!=2||p.seq!=x.login_seq||t.get<std::string>("success","")!="true"||t.get<std::uint64_t>("user_id",0)!=x.uid){failure_detail(x,p,t,"login");throw std::runtime_error("LOGIN_IDENTITY_OR_FAILURE");}x.state=3;++online;++m["login_ok"];login_latency.add(ms(x.login_start,now));if(c.login_trace&&x.uid%16==0)ledger<<"login_ack\t"<<x.uid<<'\t'<<x.to<<"\t0\t"<<p.seq<<"\t\t"<<ns(now)<<'\n';heartbeats.push({now+std::chrono::seconds(c.hb),i});return;}
      if(p.type==MessageType::kHeartbeat){if(x.state!=3||p.seq!=x.hb_seq||x.hb_seq==0||field(t,"pong")!="true")throw std::runtime_error("HEARTBEAT_IDENTITY");x.hb_seq=0;++m["heartbeat_ack"];return;}
      if(x.state!=3)throw std::runtime_error("BUSINESS_BEFORE_LOGIN");
      if(p.type==MessageType::kChatAck){if(x.chat_seq==0||p.seq!=x.chat_seq)throw std::runtime_error("UNKNOWN_CHAT_ACK");
        if(field(t,"success")!="true"){failure_detail(x,p,t,"private_ack");++m["chat_ack_fail"];event("fail",x,0,p.seq);x.chat_seq=0;return;}
        auto mid=uintfield(t,"message_id");if(mid==0||field(t,"client_message_id")!=x.cid||uintfield(t,"from")!=x.uid||uintfield(t,"to")!=x.to||field(t,"stored_persistent")!="true")throw std::runtime_error("ACK_IDENTITY");
        ++m["chat_ack_ok"];if(now<end)++m["ack_in_active_window"];double latency=ms(x.enqueued,now);ack_latency.add(latency);schedule_latency.add(ms(x.scheduled,now));if(latency<=100)++m["ack_within_100ms"];event("ack",x,mid,p.seq);x.chat_seq=0;return;
      }
      if(p.type==MessageType::kChatDelivery){auto mid=uintfield(t,"message_id");auto from=uintfield(t,"from");if(!mid||!p.seq||uintfield(t,"to")!=x.uid)throw std::runtime_error("DELIVERY_IDENTITY");auto cid=t.get<std::string>("client_message_id","");if(!cid.empty()&&!token(cid))throw std::runtime_error("DELIVERY_CID_FORMAT");
        ledger<<"delivery\t"<<from<<'\t'<<x.uid<<'\t'<<mid<<'\t'<<p.seq<<'\t'<<cid<<'\t'<<ns(now)<<'\n';++m["wire_deliveries"];
        queue(i,MessageType::kChatDeliveryAck,p.seq,"{\"message_id\":"+std::to_string(mid)+"}");++m["receiver_ack_queued"];return;
      }
      if(p.type==MessageType::kGroupMessageDelivery){auto mid=uintfield(t,"message_id");if(!mid||!p.seq)throw std::runtime_error("GROUP_DELIVERY_IDENTITY");queue(i,MessageType::kGroupMessageDeliveryAck,p.seq,"{\"message_id\":"+std::to_string(mid)+"}");++m["group_deliveries_observed_not_a_group_test"];return;}
      throw std::runtime_error("UNEXPECTED_PACKET_TYPE_"+std::to_string(static_cast<unsigned>(p.type)));
    }
    void read(std::size_t i){auto&x=conns[i];char b[16384];bool eof=false;std::size_t budget=0;while(budget<262144){auto n=::recv(x.fd,b,sizeof(b),0);if(n>0){x.input.Append(b,n);budget+=n;if(x.input.ReadableBytes()>2*1024*1024)throw std::runtime_error("CLIENT_INPUT_LIMIT");continue;}if(n==0){eof=true;break;}if(errno==EINTR)continue;if(errno==EAGAIN||errno==EWOULDBLOCK)break;throw std::runtime_error("RECV_ERRNO_"+std::to_string(errno));}
        auto r=codec.Decode(&x.input);if(r.status!=tinyimx::DecodeStatus::kOk&&r.status!=tinyimx::DecodeStatus::kNeedMoreData)throw std::runtime_error("DECODE");for(auto&p:r.packets)packet(i,p);if(eof){close(i);throw std::runtime_error("UNEXPECTED_DISCONNECT");}}
    void flush(std::size_t i){auto&x=conns[i];std::size_t budget=0;while(!x.output.empty()&&budget<262144){auto&s=x.output.front();auto n=::send(x.fd,s.data()+x.output_offset,s.size()-x.output_offset,MSG_NOSIGNAL);if(n>0){x.output_offset+=n;x.queued-=n;budget+=n;if(x.output_offset==s.size()){x.output.pop_front();x.output_offset=0;}continue;}if(n<0&&errno==EINTR)continue;if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;throw std::runtime_error("SEND_ERRNO_"+std::to_string(errno));}interest(i);}
    void pump(int wait_ms){epoll_event es[512];int n=::epoll_wait(ep,es,512,wait_ms);if(n<0){if(errno==EINTR)return;throw std::runtime_error("EPOLL_WAIT");}for(int k=0;k<n;++k){std::size_t i=es[k].data.u32;if(i>=conns.size())throw std::runtime_error("EPOLL_INDEX");auto&x=conns[i];auto ev=es[k].events;
        if(x.state==1&&(ev&(EPOLLOUT|EPOLLERR|EPOLLHUP))){int e=0;socklen_t l=sizeof(e);if(::getsockopt(x.fd,SOL_SOCKET,SO_ERROR,&e,&l)<0||e)throw std::runtime_error("CONNECT_COMPLETION");connected(i);}
        if(ev&EPOLLIN)read(i);if(ev&EPOLLOUT)flush(i);if(ev&(EPOLLERR|EPOLLHUP|EPOLLRDHUP)){close(i);throw std::runtime_error("SOCKET_CLOSED");}}
    }
    void heartbeat(TP now){if(stop_heartbeats)return;while(!heartbeats.empty()&&heartbeats.top().first<=now){auto [_,i]=heartbeats.top();heartbeats.pop();auto&x=conns[i];if(x.state!=3)continue;if(x.hb_seq){if(ms(x.hb_sent,now)>2.0*c.hb*1000)throw std::runtime_error("HEARTBEAT_UNRESOLVED");heartbeats.push({now+std::chrono::seconds(1),i});continue;}x.hb_seq=seq(x);x.hb_sent=now;queue(i,MessageType::kHeartbeat,x.hb_seq,"{}");++m["heartbeat_sent"];heartbeats.push({now+std::chrono::seconds(c.hb),i});}}
    void send_due(TP now){if(c.mode!="private")return;const auto limit=tinyimx::capacity::OfferedRequestCount(c.rate,c.duration);std::size_t burst=0;while(burst++<256){if(planned>=limit)break;TP due=start+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(planned/c.rate));if(due>=end||due>now)break;++planned;++m["planned_requests"];ready_lag.add(ms(due,now));std::size_t i=rr++%conns.size();auto&x=conns[i];if(x.state!=3||x.chat_seq){++m["skipped_scheduled_requests"];continue;}x.chat_seq=seq(x);x.scheduled=due;x.enqueued=Clock::now();x.cid="l"+c.run+"-w"+std::to_string(c.worker)+"-u"+std::to_string(x.uid)+"-n"+std::to_string(++x.serial);if(x.cid.size()>64)throw std::runtime_error("CID_LENGTH");
        queue(i,MessageType::kChatMessage,x.chat_seq,"{\"client_message_id\":"+quote(x.cid)+",\"to\":"+std::to_string(x.to)+",\"text\":"+quote(std::string(128,'x'))+"}");++m["send_attempts"];if(now>=end)++m["late_offered_requests"];event("send",x,0,x.chat_seq);}}
    std::string result()const{std::ostringstream o;o<<"{\"schema\":\"tinyimx-capacity-worker-v1\",\"run_id\":"<<quote(c.run)<<",\"worker_id\":"<<c.worker<<",\"offset\":"<<c.offset<<",\"connections\":"<<c.count<<",\"total_users\":"<<c.total<<",\"mode\":"<<quote(c.mode)<<",\"status\":"<<quote(terminal)<<",\"online_now\":"<<online<<",\"duration_seconds\":"<<c.duration<<",\"rate\":"<<c.rate<<",\"source_ip\":"<<quote(c.source)<<",\"start_steady_ns\":"<<(started?ns(start):0)<<",\"snapshot_steady_ns\":"<<ns(Clock::now())<<",\"inflight\":";std::size_t inflight=0;for(auto&x:conns)inflight+=(x.chat_seq!=0);o<<inflight<<",\"metrics\":{";bool first=true;for(auto&[k,v]:m){if(!first)o<<',';first=false;o<<quote(k)<<':'<<v;}o<<"},\"ack_histogram\":"<<ack_latency.json()<<",\"scheduled_to_ack_histogram\":"<<schedule_latency.json()<<",\"schedule_lag_histogram\":"<<ready_lag.json()<<",\"login_histogram\":"<<login_latency.json()<<'}';return o.str();}
public:
    explicit Worker(Config cfg):c(std::move(cfg)),conns(c.count){for(auto k:{"connected","login_ok","disconnects","heartbeat_sent","heartbeat_ack","planned_requests","skipped_scheduled_requests","late_offered_requests","send_attempts","chat_ack_ok","chat_ack_fail","ack_in_active_window","ack_within_100ms","wire_deliveries","receiver_ack_queued","group_deliveries_observed_not_a_group_test"})m[k]=0;}
    ~Worker(){for(auto&x:conns)if(x.fd>=0)::close(x.fd);if(ep>=0)::close(ep);}
    int run(){try{ledger.open(c.out/"ledger.tsv",std::ios::trunc);if(!ledger)throw std::runtime_error("LEDGER_OPEN");ep=::epoll_create1(EPOLL_CLOEXEC);if(ep<0)throw std::runtime_error("EPOLL_CREATE");target.sin_family=src.sin_family=AF_INET;target.sin_port=htons(c.port);::inet_pton(AF_INET,c.host.c_str(),&target.sin_addr);::inet_pton(AF_INET,c.source.c_str(),&src.sin_addr);
       began=Clock::now();next_open=began;setup_deadline=began+std::chrono::seconds(static_cast<int>(c.count/c.ramp*2+90));
       while(true){auto now=Clock::now();heartbeat(now); // also while ramping/barrier/auditing
         if(!ready){std::size_t opens=0;while(next_index<c.count&&now>=next_open&&opens++<128){open(next_index++);next_open=began+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(next_index/c.ramp));}if(now>setup_deadline)throw std::runtime_error("SETUP_TIMEOUT");if(online==c.count){ready=true;atomic(c.out/"ready.json",result());}}
         if(ms(last_control,now)>=100){last_control=now;if(fs::exists(c.control/"abort"))throw std::runtime_error("COORDINATOR_ABORT");if(ready&&!started&&fs::exists(c.control/"start_ns")){std::ifstream f(c.control/"start_ns");std::string s;f>>s;auto target_ns=number(s);start=TP(std::chrono::nanoseconds(target_ns));if(start<now)throw std::runtime_error("START_BARRIER_LATE");if(ms(now,start)>10000)throw std::runtime_error("START_BARRIER_TOO_FAR");end=start+std::chrono::seconds(c.duration);started=true;}
            if(ready&&!started&&now>setup_deadline+std::chrono::seconds(90))throw std::runtime_error("BARRIER_TIMEOUT");}
         // Preserve every absolute-time offered slot, including a final slot
         // whose wakeup crosses end. Late offers retain scheduled timestamps;
         // their ACKs do not count in the original active throughput window.
         // Catchup remains bounded in send_due and the drain deadline is fixed.
         if(started&&now>=start&&!audit_ready){send_due(now);if(now>=end+std::chrono::seconds(c.drain)){if(!end_accounted){end_accounted=true;const auto expected=tinyimx::capacity::OfferedRequestCount(c.rate,c.duration);if(c.mode=="private"&&expected>planned){m["planned_requests"]+=expected-planned;m["skipped_scheduled_requests"]+=expected-planned;planned=expected;}}audit_ready=true;audit_at=now;ledger.flush();terminal="AUDIT_READY";atomic(c.out/"audit-ready.json",result());}}
         if(audit_ready){
           if(fs::exists(c.control/"quiesce_heartbeats"))stop_heartbeats=true;
           if(stop_heartbeats&&!heartbeat_drained&&m["heartbeat_sent"]==m["heartbeat_ack"]){heartbeat_drained=true;atomic(c.out/"heartbeat-drained.json",result());}
           if(fs::exists(c.control/"release")){normal_release=true;break;}
           if(now>audit_at+std::chrono::seconds(c.verify))throw std::runtime_error("AUDIT_TIMEOUT");
         }
         if(ms(last_progress,now)>1000){last_progress=now;ledger.flush();atomic(c.out/"live.json",result());}
         pump(10);
       }
       terminal=normal_release?"COMPLETED":"INCOMPLETE";ledger.flush();atomic(c.out/"final.json",result());return normal_release?0:2;
    }catch(const std::exception&e){terminal="FAILED:"+std::string(e.what());ledger.flush();try{atomic(c.out/"final.json",result());}catch(...){}std::cerr<<"FIRST_FAILURE="<<terminal<<'\n';return 2;}}
};
int main(int argc,char**argv){try{auto c=args(argc,argv);return Worker(std::move(c)).run();}catch(const std::exception&e){std::cerr<<"FIRST_FAILURE="<<e.what()<<'\n';return 64;}}
