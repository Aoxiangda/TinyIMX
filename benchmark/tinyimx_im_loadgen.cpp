#include "common/net/Buffer.h"
#include "common/protocol/Packet.h"
#include "common/protocol/ProtocolCodec.h"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

constexpr tinyimx::MessageType kChatDelivery =
    static_cast<tinyimx::MessageType>(2019);
constexpr tinyimx::MessageType kChatDeliveryAck =
    static_cast<tinyimx::MessageType>(2020);
constexpr tinyimx::MessageType kGroupMessageDelivery =
    static_cast<tinyimx::MessageType>(2051);
constexpr tinyimx::MessageType kGroupMessageDeliveryAck =
    static_cast<tinyimx::MessageType>(2052);

struct Config {
    std::string host{"127.0.0.1"};
    std::uint16_t port{9000};
    std::size_t connections{10};
    std::uint64_t user_id_base{200000};
    std::string username_prefix{"m21bench"};
    std::string password{"123456"};
    std::string mode{"hold"};
    std::string peer_mode{"ring"};
    std::size_t hotspot_user_index{0};
    int duration_seconds{20};
    double total_rate{10.0};
    double ramp_per_second{100.0};
    std::size_t payload_bytes{128};
    int heartbeat_seconds{30};
    int drain_seconds{5};
    std::size_t max_outstanding{1};
};

bool ParseSize(const std::string& s, std::size_t* out) {
    try { std::size_t p=0; auto v=std::stoull(s,&p); if(p!=s.size()) return false; *out=v; return true; }
    catch (...) { return false; }
}
bool ParseU64(const std::string& s, std::uint64_t* out) {
    try { std::size_t p=0; auto v=std::stoull(s,&p); if(p!=s.size()) return false; *out=v; return true; }
    catch (...) { return false; }
}
bool ParseInt(const std::string& s, int* out) {
    try { std::size_t p=0; auto v=std::stol(s,&p); if(p!=s.size()) return false; *out=static_cast<int>(v); return true; }
    catch (...) { return false; }
}
bool ParseDouble(const std::string& s, double* out) {
    try { std::size_t p=0; auto v=std::stod(s,&p); if(p!=s.size()) return false; *out=v; return true; }
    catch (...) { return false; }
}

void Usage(const char* a0) {
    std::cerr
        << "usage: " << a0 << " [options]\n"
        << "  --host <host> --port <port>\n"
        << "  --connections <N> --user-id-base <id>\n"
        << "  --username-prefix <prefix> --password <password>\n"
        << "  --mode <hold|private>\n"
        << "  --peer-mode <ring|hotspot> [--hotspot-user-index <zero-based>]\n"
        << "  --duration <seconds> --rate <msg/s>\n"
        << "  --ramp-per-sec <connections/s> --payload-bytes <bytes>\n"
        << "  --heartbeat-seconds <seconds> --drain-seconds <seconds>\n"
        << "  --max-outstanding <N>\n";
}

bool ParseArgs(int argc, char* argv[], Config* c) {
    for(int i=1;i<argc;++i) {
        std::string k=argv[i];
        auto next=[&]()->std::optional<std::string>{ if(i+1>=argc) return std::nullopt; return std::string(argv[++i]); };
        if(k=="-h"||k=="--help") { Usage(argv[0]); std::exit(0); }
        else if(k=="--host") { auto v=next(); if(!v) return false; c->host=*v; }
        else if(k=="--port") { auto v=next(); int x=0; if(!v||!ParseInt(*v,&x)||x<=0||x>65535) return false; c->port=static_cast<std::uint16_t>(x); }
        else if(k=="--connections") { auto v=next(); if(!v||!ParseSize(*v,&c->connections)) return false; }
        else if(k=="--user-id-base") { auto v=next(); if(!v||!ParseU64(*v,&c->user_id_base)) return false; }
        else if(k=="--username-prefix") { auto v=next(); if(!v) return false; c->username_prefix=*v; }
        else if(k=="--password") { auto v=next(); if(!v) return false; c->password=*v; }
        else if(k=="--mode") { auto v=next(); if(!v) return false; c->mode=*v; }
        else if(k=="--peer-mode") { auto v=next(); if(!v) return false; c->peer_mode=*v; }
        else if(k=="--hotspot-user-index") { auto v=next(); if(!v||!ParseSize(*v,&c->hotspot_user_index)) return false; }
        else if(k=="--duration") { auto v=next(); if(!v||!ParseInt(*v,&c->duration_seconds)) return false; }
        else if(k=="--rate") { auto v=next(); if(!v||!ParseDouble(*v,&c->total_rate)) return false; }
        else if(k=="--ramp-per-sec") { auto v=next(); if(!v||!ParseDouble(*v,&c->ramp_per_second)) return false; }
        else if(k=="--payload-bytes") { auto v=next(); if(!v||!ParseSize(*v,&c->payload_bytes)) return false; }
        else if(k=="--heartbeat-seconds") { auto v=next(); if(!v||!ParseInt(*v,&c->heartbeat_seconds)) return false; }
        else if(k=="--drain-seconds") { auto v=next(); if(!v||!ParseInt(*v,&c->drain_seconds)) return false; }
        else if(k=="--max-outstanding") { auto v=next(); if(!v||!ParseSize(*v,&c->max_outstanding)) return false; }
        else return false;
    }
    if(c->connections==0||c->connections>20000||c->user_id_base==0) return false;
    if(c->mode!="hold"&&c->mode!="private") return false;
    if(c->peer_mode!="ring"&&c->peer_mode!="hotspot") return false;
    if(c->hotspot_user_index>=c->connections) return false;
    if(c->duration_seconds<=0||c->duration_seconds>86400) return false;
    if(c->mode=="private"&&c->total_rate<=0) return false;
    if(c->ramp_per_second<=0||c->payload_bytes==0||c->payload_bytes>65536) return false;
    if(c->heartbeat_seconds<=0||c->drain_seconds<0||c->drain_seconds>300) return false;
    if(c->max_outstanding==0||c->max_outstanding>128) return false;
    return true;
}

std::string Username(const Config& c,std::size_t i) {
    std::ostringstream o; o<<c.username_prefix<<std::setw(6)<<std::setfill('0')<<(i+1); return o.str();
}
std::uint64_t UserId(const Config& c,std::size_t i) { return c.user_id_base+i+1; }
std::uint64_t PeerUserId(const Config& c,std::size_t i) {
    if(c.peer_mode=="hotspot") {
        if(i!=c.hotspot_user_index) return UserId(c,c.hotspot_user_index);
        return UserId(c,(c.hotspot_user_index+1)%c.connections);
    }
    return UserId(c,(i+1)%c.connections);
}
std::uint64_t EpochMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

class Histogram {
public:
    static constexpr std::uint64_t kBucketUs=100,kMaxUs=10'000'000;
    static constexpr std::size_t kCount=kMaxUs/kBucketUs+1;
    Histogram():b_(kCount,0){}
    void Record(std::chrono::microseconds e){ std::uint64_t u=e.count()<0?std::uint64_t{0}:static_cast<std::uint64_t>(e.count()); u=std::min(u,kMaxUs); ++b_[u/kBucketUs]; ++n_; sum_+=u; min_=std::min(min_,u); max_=std::max(max_,u); }
    std::uint64_t Count()const{return n_;}
    double AvgMs()const{return n_?static_cast<double>(sum_)/n_/1000.0:0;}
    double MinMs()const{return n_?static_cast<double>(min_)/1000.0:0;}
    double MaxMs()const{return n_?static_cast<double>(max_)/1000.0:0;}
    double P(double p)const{ if(!n_) return 0; auto want=static_cast<std::uint64_t>(std::ceil(p*n_)); std::uint64_t x=0; for(std::size_t i=0;i<b_.size();++i){x+=b_[i];if(x>=want)return i*kBucketUs/1000.0;} return kMaxUs/1000.0; }
private:
    std::vector<std::uint64_t>b_; std::uint64_t n_{0},sum_{0},min_{std::numeric_limits<std::uint64_t>::max()},max_{0};
};

struct Metrics {
    std::uint64_t connect_attempts{0},connected{0},connect_fail{0},login_ok{0},login_fail{0},disconnects{0};
    std::uint64_t login_response_fail{0},login_unresolved_fail{0},deadline_rejections{0};
    std::uint64_t protocol_errors{0},server_errors{0},overload_rejections{0};
    std::uint64_t heartbeat_sent{0},heartbeat_ack{0};
    std::uint64_t send_attempts{0},chat_ack_ok{0},chat_ack_fail{0},receiver_delivery{0},receiver_ack_sent{0};
    std::uint64_t group_delivery{0},group_ack_sent{0},loadgen_backpressure_skips{0};
};
enum class State{kUnused,kConnecting,kLoginPending,kOnline,kClosed};
struct Pending { std::string cid; std::uint64_t to{0}; TimePoint t{}; };
struct Conn {
    int fd{-1}; State state{State::kUnused}; tinyimx::Buffer input; std::deque<std::string> out; std::size_t out_off{0};
    std::unordered_map<std::uint32_t,Pending> pending; std::uint32_t seq{1}; std::uint64_t uid{0},peer{0}; std::string username;
    bool login_resolved{false},counted_disconnect{false}; TimePoint login_started{},next_hb{}; std::uint64_t logical{0};
};

class LoadGen {
public:
    explicit LoadGen(Config c):c_(std::move(c)),conns_(c_.connections),events_(1024),payload_(c_.payload_bytes,'x'),run_id_(EpochMillis()){}
    ~LoadGen(){for(auto&c:conns_)if(c.fd>=0)::close(c.fd);if(ep_>=0)::close(ep_);}
    int Run(){
        if(!Resolve())return 64; ep_=::epoll_create1(EPOLL_CLOEXEC); if(ep_<0)return 64;
        start_=Clock::now(); next_connect_=start_; double ramp_s=static_cast<double>(c_.connections)/c_.ramp_per_second;
        setup_deadline_=start_+std::chrono::seconds(static_cast<int>(std::max(60.0,ramp_s*2+30)));
        while(!finished_){ auto now=Clock::now(); if(!test_started_){OpenDue(now);EvaluateSetup(now);}else{Heartbeat(now);if(!draining_){SendDue(now);if(now>=test_end_){draining_=true;drain_end_=now+std::chrono::seconds(c_.drain_seconds);}}else if(now>=drain_end_){finished_=true;break;}}
            int n=::epoll_wait(ep_,events_.data(),events_.size(),10); if(n<0){if(errno==EINTR)continue;return 65;} for(int i=0;i<n;++i){auto idx=events_[i].data.u32;if(idx<conns_.size())HandleEvent(idx,events_[i].events);else ++m_.protocol_errors;}
        }
        result_time_=Clock::now(); CloseAll(false); Print(); return setup_failed_?2:0;
    }
private:
    bool Resolve(){addrinfo h{};h.ai_family=AF_INET;h.ai_socktype=SOCK_STREAM;addrinfo*r=nullptr;auto svc=std::to_string(c_.port);if(::getaddrinfo(c_.host.c_str(),svc.c_str(),&h,&r)!=0||!r)return false;bool ok=false;for(auto*p=r;p;p=p->ai_next)if(p->ai_family==AF_INET){std::memcpy(&target_,p->ai_addr,sizeof(target_));ok=true;break;}::freeaddrinfo(r);return ok;}
    static bool Nonblock(int fd){int f=::fcntl(fd,F_GETFL,0);return f>=0&&::fcntl(fd,F_SETFL,f|O_NONBLOCK)==0;}
    std::uint32_t NextSeq(Conn&c){for(;;){auto s=c.seq++;if(s)return s;}}
    bool Open(std::size_t i){auto&c=conns_[i];int fd=::socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);++m_.connect_attempts;if(fd<0||!Nonblock(fd)){if(fd>=0)::close(fd);++m_.connect_fail;c.state=State::kClosed;return false;}int one=1;::setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));::setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE,&one,sizeof(one));c.fd=fd;c.uid=UserId(c_,i);c.peer=PeerUserId(c_,i);c.username=Username(c_,i);int rc=::connect(fd,reinterpret_cast<sockaddr*>(&target_),sizeof(target_));c.state=(rc==0?State::kLoginPending:State::kConnecting);if(rc!=0&&errno!=EINPROGRESS){::close(fd);c.fd=-1;c.state=State::kClosed;++m_.connect_fail;return false;}epoll_event e{};e.data.u32=i;e.events=EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLERR|EPOLLHUP;if(::epoll_ctl(ep_,EPOLL_CTL_ADD,fd,&e)!=0){::close(fd);c.fd=-1;c.state=State::kClosed;++m_.connect_fail;return false;}if(rc==0)Connected(i);return true;}
    void OpenDue(TimePoint now){auto interval=std::chrono::duration<double>(1.0/c_.ramp_per_second);std::size_t burst=0;while(created_<c_.connections&&now>=next_connect_&&burst<256){Open(created_++);++burst;next_connect_+=std::chrono::duration_cast<Clock::duration>(interval);}}
    void EvaluateSetup(TimePoint now){auto terminal=m_.connect_fail+m_.login_ok+m_.login_fail;if(created_==c_.connections&&terminal>=c_.connections){if(m_.login_ok!=c_.connections){setup_failed_=true;finished_=true;return;}test_started_=true;test_start_=now;test_end_=now+std::chrono::seconds(c_.duration_seconds);next_send_=now;std::cout<<"TINYIMX_LOADGEN_READY connections="<<c_.connections<<" mode="<<c_.mode<<" peer_mode="<<c_.peer_mode<<std::endl;return;}if(now>=setup_deadline_){setup_failed_=true;finished_=true;}}
    bool Queue(std::size_t i,const tinyimx::Packet&p){auto&c=conns_[i];if(c.fd<0||c.state==State::kClosed)return false;tinyimx::Buffer b;std::string err;if(!codec_.Encode(p,&b,&err)){++m_.protocol_errors;return false;}c.out.push_back(b.RetrieveAllAsString());Interest(i);return true;}
    void Interest(std::size_t i){auto&c=conns_[i];if(c.fd<0)return;epoll_event e{};e.data.u32=i;e.events=EPOLLIN|EPOLLRDHUP|EPOLLERR|EPOLLHUP;if(c.state==State::kConnecting||!c.out.empty())e.events|=EPOLLOUT;::epoll_ctl(ep_,EPOLL_CTL_MOD,c.fd,&e);}
    void Connected(std::size_t i){auto&c=conns_[i];c.state=State::kLoginPending;++m_.connected;c.login_started=Clock::now();tinyimx::Packet p;p.type=tinyimx::MessageType::kLoginRequest;p.seq=NextSeq(c);p.body=Json{{"username",c.username},{"password",c_.password}}.dump();if(!Queue(i,p)){c.login_resolved=true;++m_.login_fail;++m_.login_unresolved_fail;Close(i,false);}}
    void HandleEvent(std::size_t i,std::uint32_t ev){auto&c=conns_[i];if(c.fd<0)return;if((ev&EPOLLOUT)&&c.state==State::kConnecting){int err=0;socklen_t l=sizeof(err);if(::getsockopt(c.fd,SOL_SOCKET,SO_ERROR,&err,&l)!=0||err){++m_.connect_fail;Close(i,false);return;}Connected(i);}if((ev&EPOLLOUT)&&c.fd>=0)Flush(i);if((ev&EPOLLIN)&&c.fd>=0)Read(i);if((ev&(EPOLLHUP|EPOLLRDHUP))&&c.fd>=0)Close(i,test_started_);}
    void Flush(std::size_t i){auto&c=conns_[i];while(c.fd>=0&&!c.out.empty()){auto&f=c.out.front();ssize_t n=::send(c.fd,f.data()+c.out_off,f.size()-c.out_off,MSG_NOSIGNAL);if(n>0){c.out_off+=n;if(c.out_off==f.size()){c.out.pop_front();c.out_off=0;}continue;}if(n<0&&errno==EINTR)continue;if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;Close(i,test_started_);return;}Interest(i);}
    void Read(std::size_t i){auto&c=conns_[i];char buf[16384];for(;;){ssize_t n=::recv(c.fd,buf,sizeof(buf),0);if(n>0){c.input.Append(buf,n);continue;}if(n==0){Close(i,test_started_);return;}if(errno==EINTR)continue;if(errno==EAGAIN||errno==EWOULDBLOCK)break;Close(i,test_started_);return;}while(c.fd>=0){auto r=codec_.Decode(&c.input);if(r.status==tinyimx::DecodeStatus::kNeedMoreData)break;if(r.status!=tinyimx::DecodeStatus::kOk){++m_.protocol_errors;Close(i,test_started_);return;}if(r.packets.empty())break;for(auto&p:r.packets){Packet(i,p);if(c.fd<0)return;}}}
    void Packet(std::size_t i,const tinyimx::Packet&p){if(p.type==tinyimx::MessageType::kLoginResponse){Login(i,p);return;}if(p.type==tinyimx::MessageType::kChatAck){ChatAck(i,p);return;}if(p.type==kChatDelivery){Delivery(i,p,false);return;}if(p.type==kGroupMessageDelivery){Delivery(i,p,true);return;}if(p.type==tinyimx::MessageType::kHeartbeat){try{if(Json::parse(p.body).value("pong",false))++m_.heartbeat_ack;else ++m_.protocol_errors;}catch(...){++m_.protocol_errors;}return;}if(p.type==tinyimx::MessageType::kError){++m_.server_errors;Overload(p.body);return;}++m_.protocol_errors;}
    void Login(std::size_t i,const tinyimx::Packet&p){auto&c=conns_[i];if(c.login_resolved){++m_.protocol_errors;return;}if(c.login_started!=TimePoint{})login_lat_.Record(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-c.login_started));try{auto j=Json::parse(p.body);if(!j.value("success",false)||j.value("user_id",0ULL)!=c.uid){const auto reason=j.value("reason",std::string{});if(reason=="business_deadline_exceeded")++m_.deadline_rejections;if(reason.find("overload")!=std::string::npos||reason=="business_runtime_overloaded"||reason=="business_executor_overloaded")++m_.overload_rejections;c.login_resolved=true;++m_.login_fail;++m_.login_response_fail;Close(i,false);return;}}catch(...){c.login_resolved=true;++m_.login_fail;++m_.login_response_fail;Close(i,false);return;}c.login_resolved=true;c.state=State::kOnline;c.next_hb=Clock::now()+std::chrono::seconds(c_.heartbeat_seconds);++m_.login_ok;Interest(i);}
    void ChatAck(std::size_t i,const tinyimx::Packet&p){auto&c=conns_[i];auto it=c.pending.find(p.seq);if(it==c.pending.end()){++m_.protocol_errors;return;}auto q=it->second;c.pending.erase(it);try{auto j=Json::parse(p.body);if(!j.value("success",false)){++m_.chat_ack_fail;Overload(p.body);return;}if(j.value("client_message_id",std::string{})!=q.cid||j.value("message_id",0ULL)==0||j.value("from",0ULL)!=c.uid||j.value("to",0ULL)!=q.to||!j.value("stored_persistent",false)){++m_.chat_ack_fail;++m_.protocol_errors;return;}++m_.chat_ack_ok;lat_.Record(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-q.t));}catch(...){++m_.chat_ack_fail;++m_.protocol_errors;}}
    void Delivery(std::size_t i,const tinyimx::Packet&p,bool group){auto&c=conns_[i];if(!p.seq){++m_.protocol_errors;return;}std::uint64_t mid=0,to=c.uid;try{auto j=Json::parse(p.body);mid=j.value("message_id",0ULL);if(!group)to=j.value("to",0ULL);if(!mid||(!group&&to!=c.uid)){++m_.protocol_errors;return;}}catch(...){++m_.protocol_errors;return;}tinyimx::Packet ack;ack.type=group?kGroupMessageDeliveryAck:kChatDeliveryAck;ack.seq=p.seq;ack.body=Json{{"message_id",mid}}.dump();if(group)++m_.group_delivery;else ++m_.receiver_delivery;if(Queue(i,ack)){if(group)++m_.group_ack_sent;else ++m_.receiver_ack_sent;}else ++m_.protocol_errors;}
    void Overload(const std::string&s){try{auto r=Json::parse(s).value("reason",std::string{});if(r.find("overload")!=std::string::npos||r=="business_runtime_overloaded"||r=="business_executor_overloaded")++m_.overload_rejections;}catch(...){}}
    void Heartbeat(TimePoint now){for(std::size_t i=0;i<conns_.size();++i){auto&c=conns_[i];if(c.state!=State::kOnline||now<c.next_hb)continue;tinyimx::Packet p;p.type=tinyimx::MessageType::kHeartbeat;p.seq=NextSeq(c);p.body="{}";if(Queue(i,p))++m_.heartbeat_sent;c.next_hb=now+std::chrono::seconds(c_.heartbeat_seconds);}}
    void SendDue(TimePoint now){if(c_.mode!="private")return;auto intv=std::chrono::duration<double>(1.0/c_.total_rate);std::size_t burst=0;while(now>=next_send_&&burst<4096){std::optional<std::size_t>sel;for(std::size_t a=0;a<conns_.size();++a){auto i=(rr_+a)%conns_.size();if(conns_[i].state==State::kOnline&&conns_[i].pending.size()<c_.max_outstanding){sel=i;rr_=(i+1)%conns_.size();break;}}if(!sel){++m_.loadgen_backpressure_skips;next_send_=now+std::chrono::duration_cast<Clock::duration>(intv);break;}SendOne(*sel,now);++burst;next_send_+=std::chrono::duration_cast<Clock::duration>(intv);}}
    void SendOne(std::size_t i,TimePoint now){auto&c=conns_[i];auto s=NextSeq(c);++c.logical;std::ostringstream os;os<<'b'<<run_id_<<"-u"<<c.uid<<"-n"<<c.logical;auto cid=os.str();if(cid.size()>64){++m_.protocol_errors;return;}tinyimx::Packet p;p.type=tinyimx::MessageType::kChatMessage;p.seq=s;p.body=Json{{"client_message_id",cid},{"to",c.peer},{"text",payload_}}.dump();if(!Queue(i,p)){++m_.chat_ack_fail;return;}c.pending.emplace(s,Pending{cid,c.peer,now});++m_.send_attempts;}
    void Close(std::size_t i,bool count){auto&c=conns_[i];if(c.fd<0)return;::epoll_ctl(ep_,EPOLL_CTL_DEL,c.fd,nullptr);::close(c.fd);c.fd=-1;if(!c.login_resolved&&c.state!=State::kConnecting&&c.state!=State::kUnused){c.login_resolved=true;++m_.login_fail;++m_.login_unresolved_fail;}if(count&&!c.counted_disconnect){c.counted_disconnect=true;++m_.disconnects;}c.state=State::kClosed;}
    void CloseAll(bool count){for(std::size_t i=0;i<conns_.size();++i)Close(i,count);}
    void Print()const{std::uint64_t inflight=0;std::size_t online=0;for(auto&c:conns_){inflight+=c.pending.size();if(c.state==State::kOnline)++online;}double active=0;if(test_started_){auto e=draining_?std::min(result_time_,test_end_):result_time_;if(e>test_start_)active=std::chrono::duration<double>(e-test_start_).count();}if(active<=0&&test_started_)active=c_.duration_seconds;double tput=active>0?m_.chat_ack_ok/active:0;double success=m_.send_attempts?100.0*m_.chat_ack_ok/m_.send_attempts:(c_.mode=="hold"?100.0:0.0);double setup=test_started_?std::chrono::duration<double,std::milli>(test_start_-start_).count():std::chrono::duration<double,std::milli>(result_time_-start_).count();Json j{{"mode",c_.mode},{"peer_mode",c_.peer_mode},{"hotspot_user_index",c_.hotspot_user_index},{"connections",c_.connections},{"run_id",run_id_},{"setup_failed",setup_failed_},{"setup_ms",setup},{"connected",m_.connected},{"connect_fail",m_.connect_fail},{"login_ok",m_.login_ok},{"login_fail",m_.login_fail},{"login_response_fail",m_.login_response_fail},{"login_unresolved_fail",m_.login_unresolved_fail},{"deadline_rejections",m_.deadline_rejections},{"login_response_count",login_lat_.Count()},{"login_latency_min_ms",login_lat_.MinMs()},{"login_latency_avg_ms",login_lat_.AvgMs()},{"login_p50_ms",login_lat_.P(.50)},{"login_p95_ms",login_lat_.P(.95)},{"login_p99_ms",login_lat_.P(.99)},{"login_latency_max_ms",login_lat_.MaxMs()},{"online_at_end",online},{"disconnects",m_.disconnects},{"protocol_errors",m_.protocol_errors},{"server_errors",m_.server_errors},{"overload_rejections",m_.overload_rejections},{"heartbeat_sent",m_.heartbeat_sent},{"heartbeat_ack",m_.heartbeat_ack},{"send_attempts",m_.send_attempts},{"chat_ack_ok",m_.chat_ack_ok},{"chat_ack_fail",m_.chat_ack_fail},{"receiver_delivery",m_.receiver_delivery},{"receiver_ack_sent",m_.receiver_ack_sent},{"group_delivery",m_.group_delivery},{"group_ack_sent",m_.group_ack_sent},{"inflight_at_end",inflight},{"loadgen_backpressure_skips",m_.loadgen_backpressure_skips},{"duration_s",active},{"target_rate_msg_s",c_.mode=="private"?c_.total_rate:0.0},{"throughput_msg_s",tput},{"success_rate",success},{"latency_count",lat_.Count()},{"latency_min_ms",lat_.MinMs()},{"latency_avg_ms",lat_.AvgMs()},{"p50_ms",lat_.P(.50)},{"p95_ms",lat_.P(.95)},{"p99_ms",lat_.P(.99)},{"latency_max_ms",lat_.MaxMs()}};std::cout<<"TINYIMX_LOADGEN_RESULT "<<j.dump()<<'\n';}
private:
    Config c_; int ep_{-1}; sockaddr_in target_{}; tinyimx::ProtocolCodec codec_; std::vector<Conn>conns_; std::vector<epoll_event>events_; std::string payload_; std::uint64_t run_id_{0}; Metrics m_; Histogram login_lat_,lat_;
    std::size_t created_{0},rr_{0}; bool test_started_{false},setup_failed_{false},draining_{false},finished_{false}; TimePoint start_{},next_connect_{},setup_deadline_{},test_start_{},test_end_{},next_send_{},drain_end_{},result_time_{};
};
}

int main(int argc,char*argv[]) { Config c; if(!ParseArgs(argc,argv,&c)){Usage(argv[0]);return 64;} return LoadGen(std::move(c)).Run(); }
