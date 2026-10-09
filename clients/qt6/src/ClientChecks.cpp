#include "ClientChecks.h"
#include "LiveStore.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QDir>
#include <QDateTime>
#include <QtEndian>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <functional>
#include <stdexcept>
#include <memory>
namespace {
bool waitFor(const std::function<bool()> &predicate,int timeout=15000){
    QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<timeout){QEventLoop loop;QTimer::singleShot(10,&loop,&QEventLoop::quit);loop.exec();}return predicate();
}
void save(const QString &path,const QJsonObject &result){QFile f(path);if(f.exists()||!f.open(QIODevice::WriteOnly|QIODevice::NewOnly))throw std::runtime_error("report exists or cannot be written");f.write(QJsonDocument(result).toJson());}
int textCount(LiveStore &s,const QString &text){int n=0;for(const auto &r:s.messages()->rows())if(r.value(QStringLiteral("text")).toString()==text)++n;return n;}
bool hasStored(LiveStore &s,const QString &text){for(const auto &r:s.messages()->rows())if(r.value(QStringLiteral("text")).toString()==text&&!r.value(QStringLiteral("mid")).toString().isEmpty()&&(r.value(QStringLiteral("status")).toString()==QStringLiteral("stored")||r.value(QStringLiteral("status")).toString()==QStringLiteral("read")))return true;return false;}
}
int runProtocolChecks(const QString &report){
    QJsonArray tests;int failures=0;auto check=[&](const QString &name,bool ok){tests.append(QJsonObject{{QStringLiteral("name"),name},{QStringLiteral("pass"),ok}});if(!ok)++failures;};
    const qint64 huge=9007199254740993LL;
    const auto encoded=Timx::encode(1002,42,{{QStringLiteral("success"),true},{QStringLiteral("user_id"),huge}});
    Timx::Frame f;QString error;QByteArray buffer;
    bool fragments=true;for(qsizetype i=0;i<encoded.size();++i){buffer+=encoded.mid(i,1);const auto r=Timx::take(buffer,f,error);if(r!=(i==encoded.size()-1?Timx::Decode::FrameReady:Timx::Decode::More))fragments=false;}
    check(QStringLiteral("byte_by_byte_fragmentation"),fragments&&buffer.isEmpty());
    check(QStringLiteral("network_header_and_sequence"),f.type==1002&&f.seq==42);
    check(QStringLiteral("64_bit_ID_above_JS_precision"),Timx::positiveId(f.body.value(QStringLiteral("user_id")))==huge);
    check(QStringLiteral("reject_fractional_ID"),Timx::positiveId(QJsonValue(1.5))==0);
    check(QStringLiteral("reject_string_ID"),Timx::positiveId(QJsonValue(QStringLiteral("12")))==0);
    check(QStringLiteral("reject_negative_ID"),Timx::positiveId(QJsonValue(-1))==0);
    buffer=encoded+Timx::encode(9001,43,{});const bool a=Timx::take(buffer,f,error)==Timx::Decode::FrameReady&&f.seq==42;
    check(QStringLiteral("coalesced_frames"),a&&Timx::take(buffer,f,error)==Timx::Decode::FrameReady&&f.seq==43&&buffer.isEmpty());
    for(const auto offset:{0,4,8,10}){buffer=encoded;buffer[offset]=char(0xff);check(QStringLiteral("reject_bad_header_%1").arg(offset),Timx::take(buffer,f,error)==Timx::Decode::Invalid);}
    buffer=encoded; qToBigEndian<quint32>(1024*1024+1,reinterpret_cast<uchar*>(buffer.data())+16);
    check(QStringLiteral("reject_oversized_body_before_allocation"),Timx::take(buffer,f,error)==Timx::Decode::Invalid);
    buffer=encoded.left(20)+QByteArray("[]");qToBigEndian<quint32>(2,reinterpret_cast<uchar*>(buffer.data())+16);
    check(QStringLiteral("reject_non_object_json"),Timx::take(buffer,f,error)==Timx::Decode::Invalid);
    buffer=encoded;buffer[20]='!';check(QStringLiteral("reject_corrupt_json"),Timx::take(buffer,f,error)==Timx::Decode::Invalid);
    buffer=Timx::encode(3002,1,{});check(QStringLiteral("reject_internal_gateway_type"),Timx::take(buffer,f,error)==Timx::Decode::Invalid);
    try{save(report,{{QStringLiteral("status"),failures?QStringLiteral("FAIL"):QStringLiteral("PASS")},{QStringLiteral("failures"),failures},{QStringLiteral("checks"),tests}});}catch(...){return 2;}return failures?1:0;
}
int runLiveChecks(const QString &profile,const QString &directory){
    if(QFileInfo::exists(directory)||!QDir().mkpath(directory))return 2;
    QJsonArray checks,events;QString failure;QJsonObject config;int step=0;
    LiveStore a,b,c;LiveStore *clients[]{&a,&b,&c};
    for(int i=0;i<3;++i)QObject::connect(clients[i],&LiveStore::sessionChanged,clients[i],[&,i]{events.append(QJsonObject{{QStringLiteral("client"),i},{QStringLiteral("kind"),QStringLiteral("session_state")},{QStringLiteral("state"),clients[i]->connectionState()},{QStringLiteral("error"),clients[i]->loginError()},{QStringLiteral("account_id"),clients[i]->accountId()}});});
    for(int i=0;i<3;++i)QObject::connect(clients[i],&LiveStore::event,clients[i],[&,i](const QString &kind,const QJsonObject &body){events.append(QJsonObject{{QStringLiteral("client"),i},{QStringLiteral("kind"),kind},{QStringLiteral("body"),body},{QStringLiteral("utc"),QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});});
    auto require=[&](const QString &name,bool ok){QFile progress(directory+QStringLiteral("/progress.jsonl"));if(progress.open(QIODevice::WriteOnly|QIODevice::Append))progress.write(QJsonDocument(QJsonObject{{QStringLiteral("check"),name},{QStringLiteral("pass"),ok}}).toJson(QJsonDocument::Compact)+QByteArray("\n"));checks.append(QJsonObject{{QStringLiteral("name"),name},{QStringLiteral("pass"),ok},{QStringLiteral("step"),++step}});if(!ok)throw std::runtime_error(name.toUtf8().constData());};
    try{
        QFile file(profile);require(QStringLiteral("profile_readable"),file.open(QIODevice::ReadOnly));config=QJsonDocument::fromJson(file.readAll()).object();
        const auto accounts=config.value(QStringLiteral("accounts")).toArray();const auto endpoint=config.value(QStringLiteral("endpoint")).toString();
        require(QStringLiteral("exactly_three_owned_accounts"),accounts.size()==3);
        const auto password=config.value(QStringLiteral("password")).toString();
        LiveStore bad;bad.login(endpoint,accounts[0].toObject().value(QStringLiteral("username")).toString(),QStringLiteral("deliberately-wrong-password"));
        require(QStringLiteral("invalid_password_rejected"),waitFor([&]{return !bad.loginBusy();})&&!bad.authenticated()&&!bad.loginError().isEmpty());
        for(int i=0;i<3;++i)clients[i]->login(endpoint,accounts[i].toObject().value(QStringLiteral("username")).toString(),password);
        require(QStringLiteral("three_simultaneous_authenticated_TCP_sessions"),waitFor([&]{return a.authenticated()&&b.authenticated()&&c.authenticated();}));
        for(int i=0;i<3;++i)require(QStringLiteral("identity_%1").arg(i),clients[i]->accountId()==accounts[i].toObject().value(QStringLiteral("user_id")).toString());
        for(int i=0;i<3;++i)for(int j=i+1;j<3;++j){
            auto &from=*clients[i];auto &to=*clients[j];
            from.refresh();to.refresh();waitFor([]{return false;},300);
            bool already=false;for(const auto &r:from.contacts()->rows())if(r.value(QStringLiteral("id")).toString()==to.accountId())already=true;
            if(already){require(QStringLiteral("existing_owned_friendship_%1_%2").arg(i).arg(j),true);continue;}
            from.addFriend(to.accountId(),QStringLiteral("Windows Qt native three-client verification"));
            int found=-1;
            require(QStringLiteral("friend_request_%1_%2").arg(i).arg(j),waitFor([&]{to.refresh();for(int k=0;k<to.requests()->rowCount();++k)if(to.requests()->get(k).value(QStringLiteral("state")).toString()==QStringLiteral("pending")){found=k;return true;}return false;}));
            to.acceptRequest(found,true);
            require(QStringLiteral("mutual_friendship_%1_%2").arg(i).arg(j),waitFor([&]{from.refresh();to.refresh();bool x=false,y=false;for(const auto &r:from.contacts()->rows())if(r.value(QStringLiteral("id")).toString()==to.accountId())x=true;for(const auto &r:to.contacts()->rows())if(r.value(QStringLiteral("id")).toString()==from.accountId())y=true;return x&&y;}));
        }
        const auto token=QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        auto exchange=[&](LiveStore &from,LiveStore &to,const QString &label){
            from.openConversation(to.accountId());to.openConversation(from.accountId());from.setViewActive(true);to.setViewActive(true);
            const auto text=QStringLiteral("Qt真实消息 %1 · %2 · 🙂\n第二行").arg(label,token);
            require(label+QStringLiteral("_submit"),from.sendMessage(text));
            require(label+QStringLiteral("_stored_and_received_once"),waitFor([&]{return hasStored(from,text)&&textCount(to,text)==1;})&&textCount(from,text)==1);
        };
        exchange(a,b,QStringLiteral("A_to_B"));exchange(b,a,QStringLiteral("B_to_A"));exchange(a,c,QStringLiteral("A_to_C"));exchange(c,b,QStringLiteral("C_to_B"));
        const auto peer=b.accountId();b.logout();a.openConversation(peer);const auto offline=QStringLiteral("offline-replay-")+token;
        require(QStringLiteral("offline_send_submitted"),a.sendMessage(offline));require(QStringLiteral("offline_send_persisted"),waitFor([&]{return hasStored(a,offline);}));
        b.login(endpoint,accounts[1].toObject().value(QStringLiteral("username")).toString(),password);
        require(QStringLiteral("recipient_relogin"),waitFor([&]{return b.authenticated();}));b.openConversation(a.accountId());b.setViewActive(true);
        require(QStringLiteral("offline_delivery_and_history_deduplicated"),waitFor([&]{return textCount(b,offline)==1;}));
        a.openConversation(peer);a.loadEarlier();require(QStringLiteral("sender_history_reconciled"),waitFor([&]{return textCount(a,offline)==1&&hasStored(a,offline);}));
        const auto n=events.size();require(QStringLiteral("heartbeat_response_received"),waitFor([&]{for(int i=n;i<events.size();++i)if(events[i].toObject().value(QStringLiteral("kind")).toString()==QStringLiteral("heartbeat"))return true;return false;},8000));
        // Capture the actual live C++ models through the same native QML used by the user.
        for(int i=0;i<3;++i){
            QQmlApplicationEngine engine;auto context=engine.rootContext();context->setContextProperty(QStringLiteral("demo"),clients[i]);context->setContextProperty(QStringLiteral("liveSession"),clients[i]);
            context->setContextProperty(QStringLiteral("liveMode"),true);context->setContextProperty(QStringLiteral("launchUsername"),QString{});context->setContextProperty(QStringLiteral("launchEndpoint"),endpoint);
            engine.loadFromModule(QStringLiteral("TinyIMX.Client"),QStringLiteral("Main"));require(QStringLiteral("native_live_QML_%1").arg(i),!engine.rootObjects().isEmpty());
            auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());waitFor([]{return false;},600);
            require(QStringLiteral("native_live_capture_%1").arg(i),window&&!window->grabWindow().isNull()&&window->grabWindow().save(directory+QStringLiteral("/client-%1.png").arg(i)));
        }
        LiveStore replacement;replacement.login(endpoint,accounts[0].toObject().value(QStringLiteral("username")).toString(),password);
        require(QStringLiteral("same_host_duplicate_account_blocked_before_network_login"),!replacement.authenticated()&&!replacement.loginBusy()&&replacement.connectionState()==QStringLiteral("local_account_in_use")&&a.authenticated());
        replacement.logout();
    }catch(const std::exception &e){failure=QString::fromUtf8(e.what());}
    a.logout();b.logout();c.logout();
    try{
        save(directory+QStringLiteral("/events.json"),{{QStringLiteral("events"),events}});
        save(directory+QStringLiteral("/summary.json"),{{QStringLiteral("status"),failure.isEmpty()?QStringLiteral("PASS"):QStringLiteral("FAIL")},
            {QStringLiteral("failure"),failure},{QStringLiteral("checks"),checks},{QStringLiteral("pressure"),false},
            {QStringLiteral("server"),config.value(QStringLiteral("endpoint"))},{QStringLiteral("accounts"),config.value(QStringLiteral("accounts"))},
            {QStringLiteral("test_topology"),QStringLiteral("3 independent LiveStore TCP sessions in native Windows Qt process; subsequent manual launch uses 3 independent GUI processes")}});
    }catch(...){return 2;}return failure.isEmpty()?0:1;
}
bool startWindowChecks(LiveStore *store,QQuickWindow *window,const QString &profile,int accountIndex,const QString &directory){
    if(!window||accountIndex<0||accountIndex>2||QFileInfo::exists(directory)||!QDir().mkpath(directory))return false;
    QFile file(profile);if(!file.open(QIODevice::ReadOnly))return false;
    const auto config=QJsonDocument::fromJson(file.readAll()).object();const auto accounts=config.value(QStringLiteral("accounts")).toArray();if(accounts.size()!=3)return false;
    const auto own=accounts[accountIndex].toObject();const auto token=config.value(QStringLiteral("scenario")).toString();if(token.isEmpty())return false;
    struct State {QElapsedTimer clock;QJsonArray events;QSet<QString> received,stored;bool sent=false,finishing=false;};
    auto state=std::make_shared<State>();state->clock.start();
    QObject::connect(store,&LiveStore::event,store,[state,token](const QString &kind,const QJsonObject &body){
        state->events.append(QJsonObject{{QStringLiteral("kind"),kind},{QStringLiteral("body"),body}});
        if(kind==QStringLiteral("delivery")&&body.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("crossprocess-")+token))state->received.insert(QString::number(Timx::positiveId(body.value(QStringLiteral("message_id")))));
        if(kind==QStringLiteral("response")&&body.value(QStringLiteral("request_type")).toInt()==2001)state->stored.insert(QString::number(Timx::positiveId(body.value(QStringLiteral("body")).toObject().value(QStringLiteral("message_id")))));
    });
    auto timer=new QTimer(store);timer->setInterval(200);
    QObject::connect(timer,&QTimer::timeout,store,[=]{
        if(state->finishing)return;
        if(!state->sent&&store->authenticated()&&store->contacts()->rowCount()==2&&store->accountId()==own.value(QStringLiteral("user_id")).toString()){
            state->sent=true;
            for(int i=0;i<3;++i)if(i!=accountIndex){const auto peer=accounts[i].toObject().value(QStringLiteral("user_id")).toString();store->openConversation(peer);store->sendMessage(QStringLiteral("crossprocess-%1 %2 -> %3 中文🙂").arg(token,store->accountId(),peer));}
        }
        const bool ok=state->sent&&state->stored.size()==2&&state->received.size()==2;
        if(ok||state->clock.elapsed()>30000){
            state->finishing=true;timer->stop();
            QTimer::singleShot(600,store,[=]{
                bool captured=window->grabWindow().save(directory+QStringLiteral("/window.png"));
                try{
                    save(directory+QStringLiteral("/events.json"),{{QStringLiteral("events"),state->events}});
                    save(directory+QStringLiteral("/summary.json"),{{QStringLiteral("status"),ok&&captured?QStringLiteral("PASS"):QStringLiteral("FAIL")},
                        {QStringLiteral("pid"),QCoreApplication::applicationPid()},{QStringLiteral("account"),own},
                        {QStringLiteral("stored_messages"),state->stored.size()},{QStringLiteral("received_messages"),state->received.size()},
                        {QStringLiteral("capture"),captured},{QStringLiteral("pressure"),false},{QStringLiteral("topology"),QStringLiteral("independent native GUI process")}});
                }catch(...){QCoreApplication::exit(2);return;}
                QCoreApplication::exit(ok&&captured?0:1);
            });
        }
    });
    timer->start();QTimer::singleShot(0,store,[=]{store->login(config.value(QStringLiteral("endpoint")).toString(),own.value(QStringLiteral("username")).toString(),config.value(QStringLiteral("password")).toString());});return true;
}
