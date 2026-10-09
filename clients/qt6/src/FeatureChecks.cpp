#include "LiveStore.h"
#include "ClientChecks.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QCryptographicHash>
#include <stdexcept>
#include <functional>
namespace {
bool wait(const std::function<bool()> &p,int ms=15000){QElapsedTimer t;t.start();while(!p()&&t.elapsed()<ms){QEventLoop loop;QTimer::singleShot(50,&loop,&QEventLoop::quit);loop.exec();}return p();}
bool stored(LiveStore &s,const QString &text){for(const auto &r:s.messages()->rows())if(r.value(QStringLiteral("text")).toString()==text&&r.value(QStringLiteral("status")).toString()==QStringLiteral("stored"))return true;return false;}
int count(LiveStore &s,const QString &text){int n=0;for(const auto &r:s.messages()->rows())if(r.value(QStringLiteral("text")).toString()==text)++n;return n;}
}
int runFeatureChecks(const QString &profile,const QString &directory){
    if(QFileInfo::exists(directory)||!QDir().mkpath(directory))return 2;
    QJsonArray checks,events;QString error;LiveStore a,b,c;LiveStore* clients[]{&a,&b,&c};
    auto require=[&](const QString &name,bool ok){QJsonObject row{{QStringLiteral("name"),name},{QStringLiteral("pass"),ok}};checks.append(row);QFile f(directory+QStringLiteral("/progress.jsonl"));if(f.open(QIODevice::WriteOnly|QIODevice::Append))f.write(QJsonDocument(row).toJson(QJsonDocument::Compact)+QByteArray("\n"));if(!ok)throw std::runtime_error(name.toUtf8().constData());};
    for(int i=0;i<3;++i)clients[i]->setFileWorkspace(directory+QStringLiteral("/client-%1").arg(i));
    QHash<int,int> responses[3],failures[3];
    for(int i=0;i<3;++i)QObject::connect(clients[i],&LiveStore::event,clients[i],[&,i](const QString &kind,const QJsonObject &body){
        if(kind==QStringLiteral("response"))++responses[i][body.value(QStringLiteral("request_type")).toInt()];
        if(kind==QStringLiteral("rpc_failure"))++failures[i][body.value(QStringLiteral("request_type")).toInt()];
        events.append(QJsonObject{{QStringLiteral("client"),i},{QStringLiteral("kind"),kind},{QStringLiteral("body"),body}});
    });
    try{
        QFile f(profile);require(QStringLiteral("owned_profile_readable"),f.open(QIODevice::ReadOnly));auto config=QJsonDocument::fromJson(f.readAll()).object();const auto accounts=config.value(QStringLiteral("accounts")).toArray();require(QStringLiteral("three_fresh_owned_accounts"),accounts.size()==3);
        const auto endpoint=config.value(QStringLiteral("endpoint")).toString(),password=config.value(QStringLiteral("password")).toString();
        for(int i=0;i<3;++i)clients[i]->login(endpoint,accounts[i].toObject().value(QStringLiteral("username")).toString(),password);
        require(QStringLiteral("three_accounts_authenticated"),wait([&]{return a.authenticated()&&b.authenticated()&&c.authenticated();}));
        require(QStringLiteral("three_file_sessions_authenticated"),wait([&]{return a.fileReady()&&b.fileReady()&&c.fileReady();}));
        const auto token=QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);const auto name=QStringLiteral("Qt功能验证-")+token;
        a.createGroup(name,QStringLiteral("仅使用新建测试账号；保留群组和消息作为证据"));QString gid;
        require(QStringLiteral("create_group_durable_and_listed"),wait([&]{for(const auto &r:a.groups()->rows())if(r.value(QStringLiteral("name")).toString()==name){gid=r.value(QStringLiteral("id")).toString();return true;}return false;}));
        a.selectGroup(gid);require(QStringLiteral("owner_and_member_identity"),wait([&]{return a.groupInfo().value(QStringLiteral("owner_user_id")).toString()==a.accountId()&&a.groupMembers()->rowCount()==1;}));
        a.mutateGroup(QStringLiteral("invite"),gid,b.accountId());require(QStringLiteral("invite_member"),wait([&]{return a.groupMembers()->rowCount()==2;}));
        b.refresh();b.openGroup(gid);a.openGroup(gid);c.openGroup(gid);
        const auto beforeJoin=QStringLiteral("入群前不公开-")+token;a.sendMessage(beforeJoin);require(QStringLiteral("prejoin_snapshot_message_stored"),wait([&]{return stored(a,beforeJoin)&&count(b,beforeJoin)==1;}));
        const auto denied=failures[2][2049];c.sendMessage(QStringLiteral("未入群应拒绝-")+token);require(QStringLiteral("nonmember_send_rejected"),wait([&]{return failures[2][2049]>denied;}));
        c.mutateGroup(QStringLiteral("join"),gid);require(QStringLiteral("open_join_group"),wait([&]{return c.groupMembers()->rowCount()==3;}));a.selectGroup(gid);b.selectGroup(gid);
        const auto text=QStringLiteral("真实群聊 · ")+token;a.sendMessage(text);require(QStringLiteral("group_persist_and_two_recipient_deliveries"),wait([&]{return stored(a,text)&&count(b,text)==1&&count(c,text)==1;}));
        require(QStringLiteral("group_private_ids_do_not_collide"),a.current().value(QStringLiteral("id")).toString()==QStringLiteral("g:")+gid);
        a.mutateGroup(QStringLiteral("admin"),gid,b.accountId());b.selectGroup(gid);require(QStringLiteral("administrator_role_confirmed"),wait([&]{b.selectGroup(gid);return b.groupInfo().value(QStringLiteral("role")).toString()==QStringLiteral("admin");}));
        a.mutateGroup(QStringLiteral("mute"),gid,c.accountId());require(QStringLiteral("muted_member_state"),wait([&]{c.selectGroup(gid);return c.groupMuted();}));require(QStringLiteral("muted_composer_refuses_send"),!c.sendMessage(QStringLiteral("禁言期间不发送")));
        a.mutateGroup(QStringLiteral("unmute"),gid,c.accountId());require(QStringLiteral("unmute_member"),wait([&]{c.selectGroup(gid);return !c.groupMuted();}));
        c.sendMessage(QStringLiteral("解除禁言群聊-")+token);require(QStringLiteral("send_after_unmute"),wait([&]{return stored(c,QStringLiteral("解除禁言群聊-")+token);}));
        a.mutateGroup(QStringLiteral("update"),gid,name+QStringLiteral("-已更新"),QStringLiteral("真实资料更新"));require(QStringLiteral("versioned_group_update"),wait([&]{return a.groupInfo().value(QStringLiteral("name")).toString()==name+QStringLiteral("-已更新");}));
        c.logout();c.login(endpoint,accounts[2].toObject().value(QStringLiteral("username")).toString(),password);
        require(QStringLiteral("group_recipient_relogin"),wait([&]{return c.authenticated()&&c.fileReady();}));c.openGroup(gid);
        require(QStringLiteral("durable_group_history_after_relogin"),wait([&]{return count(c,text)==1;}));
        require(QStringLiteral("late_join_cannot_read_original_nonrecipient_history"),count(c,beforeJoin)==0);
        a.openConversation(b.accountId());b.openConversation(a.accountId());a.askAI(QStringLiteral("用一句中文解释什么是消息幂等，不超过40字。"),false);
        a.sendMessage(QStringLiteral("AI期间私聊-")+token);require(QStringLiteral("private_messaging_during_ai"),wait([&]{return count(b,QStringLiteral("AI期间私聊-")+token)==1;}));
        require(QStringLiteral("real_ollama_stream_completion"),wait([&]{return a.aiState()!=QStringLiteral("thinking");},120000)&&a.aiState()==QStringLiteral("done")&&!a.aiAnswer().isEmpty());
        a.askAI(QStringLiteral("请详细解释 C++ 的内存管理。"));a.cancelAI();require(QStringLiteral("ai_generation_canceled"),a.aiState()==QStringLiteral("canceled"));
        a.configureAI(QStringLiteral("http://127.0.0.1:11434"),QStringLiteral("tinyimx-nonexistent-test-model"));a.askAI(QStringLiteral("测试错误处理"));require(QStringLiteral("missing_model_reports_error"),wait([&]{return a.aiState()==QStringLiteral("error");}));a.configureAI(QStringLiteral("http://127.0.0.1:11434"),QStringLiteral("qwen3:0.6b"));
        a.openGroup(gid);a.selectGroup(gid);a.mutateGroup(QStringLiteral("transfer"),gid,b.accountId());require(QStringLiteral("ownership_transfer"),wait([&]{return a.groupInfo().value(QStringLiteral("owner_user_id")).toString()==b.accountId();}));b.selectGroup(gid);
        b.mutateGroup(QStringLiteral("kick"),gid,c.accountId());require(QStringLiteral("kick_member"),wait([&]{return b.groupMembers()->rowCount()==2;}));const auto kickDenied=failures[2][2049];c.sendMessage(QStringLiteral("移出后拒绝-")+token);require(QStringLiteral("removed_member_send_rejected"),wait([&]{return failures[2][2049]>kickDenied;}));
        // Real native file bytes: source and downloads stay inside this owned evidence directory.
        QByteArray bytes(2*1024*1024,Qt::Uninitialized);for(int i=0;i<bytes.size();++i)bytes[i]=char(i%251);
        const auto source=directory+QStringLiteral("/owned-fixture.bin");QFile sourceFile(source);require(QStringLiteral("file_fixture_created_new_only"),sourceFile.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&sourceFile.write(bytes)==bytes.size());sourceFile.close();
        a.openConversation(b.accountId());b.openConversation(a.accountId());a.uploadFile(QUrl::fromLocalFile(source));
        require(QStringLiteral("upload_makes_progress"),wait([&]{return a.transfers()->rowCount()>0&&a.transfers()->get(0).value(QStringLiteral("progress")).toDouble()>0;}));
        a.transferAction(0,QStringLiteral("pause"));require(QStringLiteral("upload_paused"),a.transfers()->get(0).value(QStringLiteral("state")).toString()==QStringLiteral("paused"));a.transferAction(0,QStringLiteral("resume"));
        require(QStringLiteral("upload_resume_finalize_and_share"),wait([&]{return a.transfers()->get(0).value(QStringLiteral("state")).toString()==QStringLiteral("completed");},45000));
        int attachment=-1;require(QStringLiteral("private_attachment_received"),wait([&]{for(int i=0;i<b.messages()->rowCount();++i)if(b.messages()->get(i).value(QStringLiteral("kind")).toString()==QStringLiteral("file")&&b.messages()->get(i).value(QStringLiteral("text")).toString()==QStringLiteral("owned-fixture.bin")){attachment=i;return true;}return false;}));
        b.downloadMessage(attachment);require(QStringLiteral("download_makes_progress"),wait([&]{return b.transfers()->rowCount()>0&&b.transfers()->get(0).value(QStringLiteral("progress")).toDouble()>0;}));
        b.transferAction(0,QStringLiteral("pause"));require(QStringLiteral("download_paused"),b.transfers()->get(0).value(QStringLiteral("state")).toString()==QStringLiteral("paused"));b.transferAction(0,QStringLiteral("resume"));
        require(QStringLiteral("download_resume_complete"),wait([&]{return b.transfers()->get(0).value(QStringLiteral("state")).toString()==QStringLiteral("completed");},45000));
        QFile downloaded(b.transferPath(0));require(QStringLiteral("native_download_exact_bytes_and_sha256"),downloaded.open(QIODevice::ReadOnly)&&downloaded.readAll()==bytes);
        a.uploadFile(QUrl::fromLocalFile(source));require(QStringLiteral("cancel_upload_started"),wait([&]{return a.transfers()->rowCount()==2&&!a.transfers()->get(1).value(QStringLiteral("upload_id")).toString().isEmpty();}));a.transferAction(1,QStringLiteral("cancel"));require(QStringLiteral("native_upload_canceled"),wait([&]{return a.transfers()->get(1).value(QStringLiteral("state")).toString()==QStringLiteral("canceled");}));
        b.logout();b.login(endpoint,accounts[1].toObject().value(QStringLiteral("username")).toString(),password);require(QStringLiteral("file_task_journal_restored"),wait([&]{return b.fileReady()&&b.transfers()->rowCount()==1&&b.transfers()->get(0).value(QStringLiteral("state")).toString()==QStringLiteral("completed");}));b.openConversation(a.accountId());
        require(QStringLiteral("private_file_card_restored_from_durable_history"),wait([&]{for(const auto &row:b.messages()->rows())if(row.value(QStringLiteral("kind")).toString()==QStringLiteral("file")&&row.value(QStringLiteral("text")).toString()==QStringLiteral("owned-fixture.bin"))return true;return false;}));b.openGroup(gid);b.selectGroup(gid);
        a.openGroup(gid);a.uploadFile(QUrl::fromLocalFile(source));require(QStringLiteral("group_file_upload_and_share"),wait([&]{return a.transfers()->rowCount()==3&&a.transfers()->get(2).value(QStringLiteral("state")).toString()==QStringLiteral("completed");},45000));
        int groupAttachment=-1;require(QStringLiteral("group_file_card_received"),wait([&]{for(int i=0;i<b.messages()->rowCount();++i)if(b.messages()->get(i).value(QStringLiteral("kind")).toString()==QStringLiteral("file")){groupAttachment=i;return true;}return false;}));b.downloadMessage(groupAttachment);
        require(QStringLiteral("group_authorized_file_download"),wait([&]{return b.transfers()->rowCount()==2&&b.transfers()->get(1).value(QStringLiteral("state")).toString()==QStringLiteral("completed");},45000));
        bool canceledBeforeId=false;
        const auto cancelConnection=QObject::connect(&a,&LiveStore::event,&a,[&](const QString &kind,const QJsonObject &body){
            if(kind!=QStringLiteral("upload_begin_sent"))return;
            for(int i=0;i<a.transfers()->rowCount();++i)if(a.transfers()->get(i).value(QStringLiteral("id")).toString()==body.value(QStringLiteral("id")).toString()){
                canceledBeforeId=a.transfers()->get(i).value(QStringLiteral("upload_id")).toString().isEmpty();a.transferAction(i,QStringLiteral("cancel"));break;
            }
        });
        a.uploadFile(QUrl::fromLocalFile(source));
        require(QStringLiteral("cancel_during_begin_waits_for_server_ack"),wait([&]{return a.transfers()->rowCount()==4&&a.transfers()->get(3).value(QStringLiteral("state")).toString()==QStringLiteral("canceled");},45000)&&canceledBeforeId&&!a.transfers()->get(3).value(QStringLiteral("upload_id")).toString().isEmpty());QObject::disconnect(cancelConnection);
        b.downloadMessage(groupAttachment);
        require(QStringLiteral("owned_download_for_corruption_check"),wait([&]{return b.transfers()->rowCount()==3&&b.transfers()->get(2).value(QStringLiteral("progress")).toDouble()>0;}));b.transferAction(2,QStringLiteral("pause"));
        require(QStringLiteral("owned_download_paused_before_tamper"),b.transfers()->get(2).value(QStringLiteral("state")).toString()==QStringLiteral("paused"));
        const auto damaged=b.transferPath(2)+QStringLiteral(".part");QFile damagedFile(damaged);require(QStringLiteral("tamper_only_owned_test_partial"),damagedFile.open(QIODevice::ReadWrite)&&damagedFile.size()>0&&damagedFile.write(QByteArray(1,char(0xff)))==1&&damagedFile.flush());damagedFile.close();b.transferAction(2,QStringLiteral("resume"));
        require(QStringLiteral("corrupted_partial_fails_full_sha256"),wait([&]{return b.transfers()->get(2).value(QStringLiteral("state")).toString()==QStringLiteral("failed");},45000)&&b.transfers()->get(2).value(QStringLiteral("restart_required")).toBool());b.transferAction(2,QStringLiteral("retry"));
        require(QStringLiteral("corrupted_download_retry_new_task_preserves_evidence"),wait([&]{return b.transfers()->rowCount()==4&&b.transfers()->get(3).value(QStringLiteral("state")).toString()==QStringLiteral("completed");},45000)&&QFileInfo::exists(damaged)&&b.transfers()->get(2).value(QStringLiteral("state")).toString()==QStringLiteral("failed"));
        QFile retried(b.transferPath(3));require(QStringLiteral("retried_download_exact_original_bytes"),retried.open(QIODevice::ReadOnly)&&retried.readAll()==bytes);
        bool safePreviews=true;for(const auto &row:b.conversations()->rows())if(row.value(QStringLiteral("preview")).toString().contains(QStringLiteral("\"capability\"")))safePreviews=false;require(QStringLiteral("conversation_file_previews_hide_authorization_payload"),safePreviews);
        b.askAI(QStringLiteral("用一句中文概括当前会话，不超过40字。"),true);require(QStringLiteral("real_ai_with_loaded_context_for_native_capture"),wait([&]{return b.aiState()!=QStringLiteral("thinking");},120000)&&b.aiState()==QStringLiteral("done")&&!b.aiAnswer().isEmpty());
        // Native capture uses the authenticated LiveStore, not design data.
        QQmlApplicationEngine engine;engine.rootContext()->setContextProperty(QStringLiteral("demo"),&b);engine.rootContext()->setContextProperty(QStringLiteral("liveSession"),&b);engine.rootContext()->setContextProperty(QStringLiteral("liveMode"),true);engine.rootContext()->setContextProperty(QStringLiteral("launchUsername"),QString{});engine.rootContext()->setContextProperty(QStringLiteral("launchEndpoint"),endpoint);engine.loadFromModule(QStringLiteral("TinyIMX.Client"),QStringLiteral("Main"));
        require(QStringLiteral("native_live_window_created"),!engine.rootObjects().isEmpty());auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());require(QStringLiteral("native_quick_window"),window!=nullptr);
        for(const auto &page:{QStringLiteral("groups"),QStringLiteral("ai"),QStringLiteral("files"),QStringLiteral("messages")}){window->setProperty("page",page);wait([]{return false;},500);require(QStringLiteral("native_capture_")+page,window->grabWindow().save(directory+QChar(u'/')+page+QStringLiteral(".png")));}
        a.mutateGroup(QStringLiteral("leave"),gid);const auto left=responses[0][2033];require(QStringLiteral("leave_group"),wait([&]{return responses[0][2033]>left;}));b.selectGroup(gid);require(QStringLiteral("remaining_owner_member"),wait([&]{return b.groupMembers()->rowCount()==1;}));const auto disband=responses[1][2029];b.mutateGroup(QStringLiteral("disband"),gid);require(QStringLiteral("versioned_disband_group"),wait([&]{return responses[1][2029]>disband;}));
        a.logout();require(QStringLiteral("logout_clears_private_ai_context"),a.aiAnswer().isEmpty()&&a.groups()->rowCount()==0&&!a.authenticated());
    }catch(const std::exception &e){error=QString::fromUtf8(e.what());}
    QFile out(directory+QStringLiteral("/report.json"));if(!out.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 2;out.write(QJsonDocument(QJsonObject{{QStringLiteral("status"),error.isEmpty()?QStringLiteral("PASS"):QStringLiteral("FAIL")},{QStringLiteral("error"),error},{QStringLiteral("checks"),checks},{QStringLiteral("events"),events},{QStringLiteral("pressure"),false}}).toJson());return error.isEmpty()?0:1;
}
