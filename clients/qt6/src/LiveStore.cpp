#include "LiveStore.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QUuid>
#include <QRegularExpression>
#include <QNetworkProxy>
#include <QCryptographicHash>

#include <algorithm>
namespace {
QString key(const QJsonValue &v){const auto n=Timx::positiveId(v);return n?QString::number(n):QString{};}
QString stamp(){return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));}
QString textContent(const QJsonValue &v){
    const auto text=v.toString();const auto d=QJsonDocument::fromJson(text.toUtf8());
    return d.isObject()?d.object().value(QStringLiteral("text")).toString():text;
}
QString reason(const QJsonObject &b){return b.value(QStringLiteral("reason")).toString(b.value(QStringLiteral("message")).toString(QStringLiteral("请求未成功")));}
}
LiveStore::LiveStore(QObject *parent):QObject(parent),conversations_(this),messages_(this),contacts_(this),requests_(this),groups_(this),transfers_(this){
    clock_.start();socket_.setReadBufferSize(2*1024*1024);
    // This is a raw TCP protocol to a LAN gateway. A system HTTP proxy cannot carry it.
    socket_.setProxy(QNetworkProxy::NoProxy);
    connect(&socket_,&QTcpSocket::connected,this,[this]{
        socket_.setSocketOption(QAbstractSocket::LowDelayOption,1);state_=QStringLiteral("authenticating");emit sessionChanged();
        rpc(1001,{{QStringLiteral("username"),name_},{QStringLiteral("password"),password_}});password_.fill(QChar(0));password_.clear();
    });
    connect(&socket_,&QTcpSocket::readyRead,this,[this]{input_+=socket_.readAll();if(input_.size()>2*1024*1024){fail(QStringLiteral("响应缓冲超过限制"));return;}drain();});
    connect(&socket_,&QTcpSocket::disconnected,this,[this]{if(!suppressDisconnect_&&(authenticated_||loginBusy_))fail(QStringLiteral("服务器连接已断开；重新登录后可重试待确认消息"));});
    connect(&socket_,&QTcpSocket::errorOccurred,this,[this](QAbstractSocket::SocketError){if(!suppressDisconnect_&&(authenticated_||loginBusy_))fail(socket_.errorString());});
    timer_.setInterval(250);connect(&timer_,&QTimer::timeout,this,&LiveStore::tick);timer_.start();
}
LiveStore::~LiveStore(){
    // QTcpSocket destruction can emit disconnected. Disable callbacks while every
    // state member is still alive, before C++ destroys members in reverse order.
    disconnect(&socket_,nullptr,this,nullptr);
    disconnect(&timer_,nullptr,this,nullptr);
    disconnect(&accountLock_,nullptr,this,nullptr);
    timer_.stop();socket_.abort();if(accountLock_.isAttached())accountLock_.detach();
}
QVariantMap LiveStore::current()const{return conversations_.get(selected_);}
QString LiveStore::currentKey()const{return current().value(QStringLiteral("id")).toString();}
void LiveStore::setDraft(const QString &text){if(currentKey().isEmpty()||draft()==text)return;drafts_[currentKey()]=text;emit draftChanged();}
void LiveStore::notify(const QString &text){notice_=text;emit noticeChanged();}
void LiveStore::resetModels(){
    conversations_.replace({});messages_.replace({});contacts_.replace({});requests_.replace({});groups_.replace({});transfers_.replace({});
    history_.clear();drafts_.clear();names_.clear();delivered_.clear();deliveredOrder_.clear();selected_=-1;emit selectionChanged();emit draftChanged();
}
void LiveStore::logout(){fail(QStringLiteral("已退出登录"));self_=0;name_.clear();resetModels();emit sessionChanged();}
void LiveStore::login(const QString &address,const QString &username,const QString &password){
    if(loginBusy_)return;
    const QRegularExpression re(QStringLiteral("^([A-Za-z0-9.-]+):([0-9]{1,5})$"));const auto match=re.match(address.trimmed());
    const int port=match.captured(2).toInt();
    if(!match.hasMatch()||port<1||port>65535||username.trimmed().isEmpty()||password.isEmpty()){
        error_=QStringLiteral("请输入用户名、密码和有效的 主机:端口");emit sessionChanged();return;
    }
    // Reconnecting the same account preserves uncertain CIDs. Switching accounts clears all private state.
    const bool same=name_==username.trimmed()&&endpoint_==address.trimmed()&&self_>0;
    fail({});if(!same){self_=0;resetModels();}
    name_=username.trimmed();endpoint_=address.trimmed();password_=password;error_.clear();loginBusy_=true;
    const auto lockIdentity=(endpoint_.toCaseFolded()+QChar(u'|')+name_.toCaseFolded()).toUtf8();
    const auto lockName=QStringLiteral("TinyIMXAccount_")+QString::fromLatin1(QCryptographicHash::hash(lockIdentity,QCryptographicHash::Sha256).toHex());
    accountLock_.setNativeKey(lockName);
    if(!accountLock_.create(1)){fail(QStringLiteral("此账号已在本机另一个客户端登录或正在登录，请先退出原窗口"),QStringLiteral("local_account_in_use"));return;}
    state_=QStringLiteral("connecting");started_=clock_.elapsed();emit sessionChanged();
    socket_.connectToHost(match.captured(1),quint16(port));
}
void LiveStore::fail(const QString &message,const QString &state){
    suppressDisconnect_=true;socket_.abort();suppressDisconnect_=false;
    if(accountLock_.isAttached())accountLock_.detach();
    password_.fill(QChar(0));password_.clear();input_.clear();
    const auto requests=pending_;pending_.clear();
    for(const auto &p:requests)if(p.type==2001)setMessageState(p.key,p.cid,QStringLiteral("uncertain"));
    authenticated_=false;loginBusy_=false;state_=state;error_=message;emit sessionChanged();
    if(!message.isEmpty())notify(message);
}
quint32 LiveStore::rpc(quint16 type,const QJsonObject &body,const QString &peer,const QString &cid){
    if(socket_.state()!=QAbstractSocket::ConnectedState||pending_.size()>=128){notify(QStringLiteral("连接不可用或请求过多"));return 0;}
    if(type!=1001&&!authenticated_)return 0;
    do{++seq_;}while(seq_==0||pending_.contains(seq_));
    const auto bytes=Timx::encode(type,seq_,body);if(bytes.isEmpty())return 0;
    pending_.insert(seq_,{type,peer,cid,clock_.elapsed()});
    if(socket_.write(bytes)!=bytes.size()){pending_.remove(seq_);fail(QStringLiteral("发送缓冲写入失败"));return 0;}
    return seq_;
}
void LiveStore::tick(){
    const auto now=clock_.elapsed();
    if(loginBusy_&&now-started_>15000){fail(QStringLiteral("连接或登录超时"));return;}
    for(auto it=pending_.begin();it!=pending_.end();){
        if(now-it->start<15000){++it;continue;}
        const auto p=it.value();it=pending_.erase(it);
        if(p.type==1001||p.type==9001){fail(QStringLiteral("登录或心跳响应超时"));return;}
        if(p.type==2001)setMessageState(p.key,p.cid,QStringLiteral("uncertain"));
        notify(QStringLiteral("请求超时；发送结果可能已入库，请使用原消息重试"));
    }
    if(!authenticated_)return;
    if(now-lastHeartbeat_>=5000){
        bool waiting=false;for(const auto &p:pending_)if(p.type==9001)waiting=true;
        if(!waiting){rpc(9001,{});lastHeartbeat_=now;}
    }
    if(now-lastRefresh_>=15000){refresh();}
}
void LiveStore::refresh(){
    if(!authenticated_)return;
    lastRefresh_=clock_.elapsed();
    // Do not pile up refreshes if a previous page is still in flight.
    for(const auto type:{2009,2013,2007}){
        bool pending=false;for(const auto &p:pending_)if(p.type==type)pending=true;
        if(!pending)rpc(quint16(type),{{QStringLiteral("limit"),type==2007?50:100}});
    }
}
int LiveStore::ensureConversation(const QString &peer){
    if(peer.isEmpty()||peer==accountId())return -1;
    for(int i=0;i<conversations_.rowCount();++i)if(conversations_.get(i).value(QStringLiteral("id")).toString()==peer)return i;
    const auto name=names_.value(peer,QStringLiteral("用户 ")+peer);
    conversations_.append({{QStringLiteral("id"),peer},{QStringLiteral("name"),name},{QStringLiteral("initial"),name.left(1)},
        {QStringLiteral("color"),QStringLiteral("#86a49b")},{QStringLiteral("subtitle"),QStringLiteral("用户 ID：")+peer+QStringLiteral(" · 在线状态未订阅")},
        {QStringLiteral("online"),false},{QStringLiteral("kind"),QStringLiteral("private")},{QStringLiteral("preview"),QString{}},
        {QStringLiteral("unread"),0},{QStringLiteral("time"),QString{}},{QStringLiteral("pinned"),false}});
    return conversations_.rowCount()-1;
}
void LiveStore::syncCurrent(){messages_.replace(history_.value(currentKey()));emit selectionChanged();}
void LiveStore::selectConversation(int index){
    if(index<0||index>=conversations_.rowCount())return;
    selected_=index;syncCurrent();emit draftChanged();fetchHistory(currentKey());if(viewActive_)markRead();
}
void LiveStore::openConversation(const QString &peer){bool ok=false;const auto id=peer.toLongLong(&ok);if(!ok||id<=0)return;const auto i=ensureConversation(peer);if(i>=0)selectConversation(i);}
void LiveStore::setViewActive(bool value){viewActive_=value;if(value)markRead();}
void LiveStore::fetchHistory(const QString &peer,qint64 before){
    if(!authenticated_||peer.isEmpty())return;
    for(const auto &p:pending_)if(p.type==2005&&p.key==peer)return;
    QJsonObject b{{QStringLiteral("peer_user_id"),peer.toLongLong()},{QStringLiteral("limit"),50}};
    if(before>0)b.insert(QStringLiteral("before_message_id"),before);
    rpc(2005,b,peer);
}
void LiveStore::loadEarlier(){qint64 before=0;for(const auto &r:history_.value(currentKey())){const auto n=r.value(QStringLiteral("mid")).toString().toLongLong();if(n>0&&(before==0||n<before))before=n;}fetchHistory(currentKey(),before);}
bool LiveStore::sendMessage(const QString &text){
    const auto peer=currentKey();if(!authenticated_||peer.isEmpty()){notify(QStringLiteral("请先登录并选择一个好友会话"));return false;}
    if(text.trimmed().isEmpty()||text.toUtf8().size()>16000){notify(QStringLiteral("消息为空或超过 16000 字节"));return false;}
    QVariantMap row{{QStringLiteral("author"),name_},{QStringLiteral("text"),text},{QStringLiteral("own"),true},
        {QStringLiteral("time"),stamp()},{QStringLiteral("kind"),QStringLiteral("text")},{QStringLiteral("status"),QStringLiteral("sending")},
        {QStringLiteral("cid"),QUuid::createUuid().toString(QUuid::WithoutBraces)},{QStringLiteral("mid"),QString{}}};
    history_[peer].append(row);syncCurrent();
    if(!sendRow(peer,row)){setMessageState(peer,row.value(QStringLiteral("cid")).toString(),QStringLiteral("uncertain"));return false;}return true;
}
bool LiveStore::sendRow(const QString &peer,const QVariantMap &row){
    const auto cid=row.value(QStringLiteral("cid")).toString();
    for(const auto &p:pending_)if(p.type==2001&&p.cid==cid)return false;
    return rpc(2001,{{QStringLiteral("to"),peer.toLongLong()},{QStringLiteral("text"),row.value(QStringLiteral("text")).toString()},
        {QStringLiteral("client_message_id"),cid}},peer,cid)>0;
}
void LiveStore::retryMessage(int index){
    const auto row=messages_.get(index);const auto status=row.value(QStringLiteral("status")).toString();
    if(!authenticated_||(status!=QStringLiteral("failed")&&status!=QStringLiteral("uncertain"))||row.value(QStringLiteral("cid")).toString().isEmpty())return;
    if(sendRow(currentKey(),row))setMessageState(currentKey(),row.value(QStringLiteral("cid")).toString(),QStringLiteral("sending"));
}
void LiveStore::setMessageState(const QString &peer,const QString &cid,const QString &state,const QString &mid){
    auto &rows=history_[peer];for(auto &r:rows)if(r.value(QStringLiteral("cid")).toString()==cid&&!cid.isEmpty()){
        r[QStringLiteral("status")]=state;if(!mid.isEmpty())r[QStringLiteral("mid")]=mid;
    }
    if(!mid.isEmpty()){
        bool canonical=false;for(const auto &r:rows)if(r.value(QStringLiteral("cid")).toString()==cid)canonical=true;
        if(canonical){
            for(auto it=rows.begin();it!=rows.end();){
                if(it->value(QStringLiteral("mid")).toString()==mid&&it->value(QStringLiteral("cid")).toString()!=cid)it=rows.erase(it);
                else ++it;
            }
        }
    }
    if(peer==currentKey())syncCurrent();
}
void LiveStore::markRead(){
    const auto peer=currentKey();if(!authenticated_||!viewActive_||peer.isEmpty())return;
    for(const auto &p:pending_)if(p.type==2003&&p.key==peer)return;
    rpc(2003,{{QStringLiteral("peer_user_id"),peer.toLongLong()}},peer);
}
void LiveStore::addFriend(const QString &identity,const QString &note){
    bool ok=false;const auto id=identity.trimmed().toLongLong(&ok);
    if(!authenticated_||!ok||id<=0||id==self_||note.toUtf8().size()>255){notify(QStringLiteral("请填写有效的其他用户 ID，申请说明不超过 255 字节"));return;}
    rpc(2011,{{QStringLiteral("to_user_id"),id},{QStringLiteral("request_message"),note}});
}
void LiveStore::acceptRequest(int index,bool accept){
    const auto row=requests_.get(index);if(row.value(QStringLiteral("state")).toString()!=QStringLiteral("pending"))return;
    const auto id=row.value(QStringLiteral("id")).toString();for(const auto &p:pending_)if((p.type==2015||p.type==2017)&&p.key==id)return;
    rpc(accept?2015:2017,{{QStringLiteral("request_id"),id.toLongLong()}},id);
}
void LiveStore::drain(){
    for(int i=0;i<64;++i){Timx::Frame f;QString error;const auto result=Timx::take(input_,f,error);
        if(result==Timx::Decode::More)return;
        if(result==Timx::Decode::Invalid){fail(error);return;}
        handle(f);if(socket_.state()!=QAbstractSocket::ConnectedState)return;
    }if(!input_.isEmpty())QTimer::singleShot(0,this,&LiveStore::drain);
}
void LiveStore::handle(const Timx::Frame &f){
    const auto &b=f.body;
    if(f.type==9999&&f.seq==0){const auto r=reason(b);fail(r==QStringLiteral("login_replaced")?QStringLiteral("账号已在其他客户端登录"):r,r);emit event(QStringLiteral("session_error"),b);return;}
    if(f.type==2019){
        const auto mid=key(b.value(QStringLiteral("message_id"))),peer=key(b.value(QStringLiteral("from")));
        if(!authenticated_||mid.isEmpty()||peer.isEmpty()||Timx::positiveId(b.value(QStringLiteral("to")))!=self_||!b.value(QStringLiteral("text")).isString()){
            fail(QStringLiteral("消息投递身份不匹配"));return;
        }
        const int ci=ensureConversation(peer);if(ci<0){fail(QStringLiteral("无效投递会话"));return;}
        bool known=delivered_.contains(mid);for(const auto &r:history_.value(peer))if(r.value(QStringLiteral("mid")).toString()==mid)known=true;
        if(!known){
            const auto text=b.value(QStringLiteral("text")).toString();
            history_[peer].append({{QStringLiteral("author"),names_.value(peer,QStringLiteral("用户 ")+peer)},
                {QStringLiteral("text"),text},{QStringLiteral("own"),false},{QStringLiteral("time"),stamp()},
                {QStringLiteral("kind"),QStringLiteral("text")},{QStringLiteral("status"),QStringLiteral("received")},
                {QStringLiteral("mid"),mid},{QStringLiteral("cid"),QString{}}});
            const auto unread=conversations_.get(ci).value(QStringLiteral("unread")).toInt()+1;
            conversations_.update(ci,{{QStringLiteral("preview"),text},{QStringLiteral("time"),stamp()},{QStringLiteral("unread"),unread}});
            if(peer==currentKey())syncCurrent();
        }
        if(!delivered_.contains(mid)){delivered_.insert(mid);deliveredOrder_.append(mid);if(deliveredOrder_.size()>5000)delivered_.remove(deliveredOrder_.takeFirst());}
        // A delivery attempt ACK is not a read receipt. Always ACK duplicates with their own seq.
        socket_.write(Timx::encode(2020,f.seq,{{QStringLiteral("message_id"),mid.toLongLong()}}));
        if(viewActive_&&peer==currentKey())markRead();
        emit event(QStringLiteral("delivery"),b);return;
    }
    if(!pending_.contains(f.seq)){emit event(QStringLiteral("late_response"),{{QStringLiteral("type"),f.type},{QStringLiteral("seq"),qint64(f.seq)}});return;}
    const auto p=pending_.take(f.seq);
    if(f.type!=9999&&f.type!=(p.type==9001?9001:p.type+1)){fail(QStringLiteral("响应类型与请求不匹配"));return;}
    if(p.type==9001){emit event(QStringLiteral("heartbeat"),b);return;}
    const bool success=f.type!=9999&&b.value(QStringLiteral("success")).toBool();
    if(!success){
        if(p.type==1001){fail(reason(b));}
        else{if(p.type==2001)setMessageState(p.key,p.cid,QStringLiteral("failed"));notify(reason(b));}
        emit event(QStringLiteral("rpc_failure"),{{QStringLiteral("request_type"),p.type},{QStringLiteral("reason"),reason(b)}});return;
    }
    if(p.type==1001){
        const auto id=Timx::positiveId(b.value(QStringLiteral("user_id")));if(id<=0||(self_>0&&id!=self_)){fail(QStringLiteral("登录响应用户 ID 无效"));return;}
        self_=id;authenticated_=true;loginBusy_=false;state_=QStringLiteral("online");error_.clear();lastHeartbeat_=clock_.elapsed();
        emit sessionChanged();rpc(2021,{});refresh();if(!currentKey().isEmpty())fetchHistory(currentKey());emit event(QStringLiteral("login"),b);return;
    }
    if(b.contains(QStringLiteral("user_id"))&&Timx::positiveId(b.value(QStringLiteral("user_id")))!=self_){fail(QStringLiteral("响应账号身份不匹配"));return;}
    if(p.type==2021){
        const auto profile=b.value(QStringLiteral("profile")).toObject();
        if(profile.contains(QStringLiteral("user_id"))&&Timx::positiveId(profile.value(QStringLiteral("user_id")))!=self_){fail(QStringLiteral("资料身份不匹配"));return;}
        // Keep the login username stable for reconnect; nickname is stored separately in names_.
        names_[accountId()]=profile.value(QStringLiteral("nickname")).toString(name_);emit sessionChanged();
    }else if(p.type==2009){
        QList<QVariantMap> rows;
        for(const auto &v:b.value(QStringLiteral("friends")).toArray()){
            const auto o=v.toObject();const auto peer=key(o.value(QStringLiteral("friend_user_id")));if(peer.isEmpty()||peer==accountId())continue;
            auto name=o.value(QStringLiteral("nickname")).toString();if(name.isEmpty())name=o.value(QStringLiteral("username")).toString(QStringLiteral("用户 ")+peer);
            names_[peer]=name;const int ci=ensureConversation(peer);
            if(ci>=0)conversations_.update(ci,{{QStringLiteral("name"),name},{QStringLiteral("initial"),name.left(1)}});
            rows.append({{QStringLiteral("id"),peer},{QStringLiteral("name"),name},{QStringLiteral("initial"),name.left(1)},
                {QStringLiteral("color"),QStringLiteral("#86a49b")},{QStringLiteral("online"),false},{QStringLiteral("subtitle"),QStringLiteral("ID ")+peer}});
        }contacts_.replace(rows);if(b.value(QStringLiteral("has_more")).toBool())notify(QStringLiteral("联系人仅加载前 100 位，更多分页尚待接入"));
        if(selected_<0&&conversations_.rowCount()>0)selectConversation(0);
        emit selectionChanged();
    }else if(p.type==2007){
        for(const auto &v:b.value(QStringLiteral("conversations")).toArray()){
            const auto o=v.toObject();const auto peer=key(o.value(QStringLiteral("peer_user_id")));const int ci=ensureConversation(peer);if(ci<0)continue;
            conversations_.update(ci,{{QStringLiteral("preview"),textContent(o.value(QStringLiteral("last_content")))},
                {QStringLiteral("unread"),o.value(QStringLiteral("unread_count")).toInt()},{QStringLiteral("time"),o.value(QStringLiteral("last_created_at")).toString()}});
        }if(selected_<0&&conversations_.rowCount()>0)selectConversation(0);
    }else if(p.type==2013){
        QList<QVariantMap> rows;for(const auto &v:b.value(QStringLiteral("requests")).toArray()){
            const auto o=v.toObject();const auto id=key(o.value(QStringLiteral("request_id")));if(id.isEmpty())continue;
            auto name=o.value(QStringLiteral("from_nickname")).toString();if(name.isEmpty())name=o.value(QStringLiteral("from_username")).toString();
            const auto status=o.value(QStringLiteral("request_status")).toInt();
            rows.append({{QStringLiteral("id"),id},{QStringLiteral("name"),name},{QStringLiteral("note"),o.value(QStringLiteral("request_message")).toString()},
                {QStringLiteral("state"),status==0?QStringLiteral("pending"):status==1?QStringLiteral("accepted"):QStringLiteral("rejected")}});
        }requests_.replace(rows);
    }else if(p.type==2011||p.type==2015||p.type==2017){notify(p.type==2011?QStringLiteral("好友申请已由服务器确认"):QStringLiteral("好友申请处理成功"));refresh();
    }else if(p.type==2001){
        const auto mid=key(b.value(QStringLiteral("message_id")));
        if(!b.value(QStringLiteral("stored_persistent")).toBool()||mid.isEmpty()||b.value(QStringLiteral("client_message_id")).toString()!=p.cid||
            Timx::positiveId(b.value(QStringLiteral("from")))!=self_||key(b.value(QStringLiteral("to")))!=p.key){
            setMessageState(p.key,p.cid,QStringLiteral("uncertain"));fail(QStringLiteral("消息入库确认身份或 CID 不匹配"));return;
        }
        setMessageState(p.key,p.cid,QStringLiteral("stored"),mid);
        const int ci=ensureConversation(p.key);const auto rows=history_.value(p.key);for(const auto &r:rows)if(r.value(QStringLiteral("cid")).toString()==p.cid)
            conversations_.update(ci,{{QStringLiteral("preview"),r.value(QStringLiteral("text"))},{QStringLiteral("time"),stamp()}});
    }else if(p.type==2003){
        if(key(b.value(QStringLiteral("peer_user_id")))!=p.key){fail(QStringLiteral("已读响应会话不匹配"));return;}
        const int ci=ensureConversation(p.key);if(ci>=0)conversations_.update(ci,{{QStringLiteral("unread"),0}});
    }else if(p.type==2005){
        auto &rows=history_[p.key];
        for(const auto &v:b.value(QStringLiteral("messages")).toArray()){
            const auto o=v.toObject();const auto mid=key(o.value(QStringLiteral("message_id")));const auto from=key(o.value(QStringLiteral("from"))),to=key(o.value(QStringLiteral("to")));
            if(mid.isEmpty()||!((from==accountId()&&to==p.key)||(from==p.key&&to==accountId())))continue;
            const auto content=QJsonDocument::fromJson(o.value(QStringLiteral("content")).toString().toUtf8()).object();
            const auto cid=content.value(QStringLiteral("client_message_id")).toString();bool found=false;
            for(auto &r:rows)if(r.value(QStringLiteral("mid")).toString()==mid||(!cid.isEmpty()&&r.value(QStringLiteral("cid")).toString()==cid)){
                r[QStringLiteral("mid")]=mid;if(r.value(QStringLiteral("own")).toBool())r[QStringLiteral("status")]=o.value(QStringLiteral("delivery_status")).toInt()==2?QStringLiteral("read"):QStringLiteral("stored");found=true;break;
            }
            if(!found)rows.append({{QStringLiteral("author"),from==accountId()?name_:names_.value(p.key,QStringLiteral("用户 ")+p.key)},
                {QStringLiteral("text"),textContent(o.value(QStringLiteral("content")))},{QStringLiteral("own"),from==accountId()},
                {QStringLiteral("time"),o.value(QStringLiteral("created_at")).toString()},{QStringLiteral("kind"),QStringLiteral("text")},
                {QStringLiteral("status"),from==accountId()?(o.value(QStringLiteral("delivery_status")).toInt()==2?QStringLiteral("read"):QStringLiteral("stored")):QStringLiteral("received")},
                {QStringLiteral("mid"),mid},{QStringLiteral("cid"),cid}});
        }
        std::stable_sort(rows.begin(),rows.end(),[](const QVariantMap &a,const QVariantMap &b){const auto x=a.value(QStringLiteral("mid")).toString().toLongLong(),y=b.value(QStringLiteral("mid")).toString().toLongLong();if(!x)return false;if(!y)return true;return x<y;});
        if(currentKey()==p.key)syncCurrent();
    }
    emit event(QStringLiteral("response"),{{QStringLiteral("request_type"),p.type},{QStringLiteral("body"),b}});
}
