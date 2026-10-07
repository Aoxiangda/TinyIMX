#include "LiveStore.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>
#include <algorithm>
QVariantMap LiveStore::formatMessage(QVariantMap row)const{
    const auto raw=row.value(QStringLiteral("text")).toString();const auto a=QJsonDocument::fromJson(raw.toUtf8()).object();
    if(a.value(QStringLiteral("tinyimx_file")).toInt()==1&&a.value(QStringLiteral("file_id")).toString().toLongLong()>0&&a.value(QStringLiteral("sha256")).toString().size()==64&&a.value(QStringLiteral("capability")).toString().size()<=4096){
        row[QStringLiteral("raw")]=raw;row[QStringLiteral("attachment")]=a.toVariantMap();row[QStringLiteral("kind")]=QStringLiteral("file");row[QStringLiteral("text")]=a.value(QStringLiteral("name")).toString();
    }return row;
}
QString LiveStore::previewText(const QString &text)const{
    const auto row=formatMessage({{QStringLiteral("text"),text}});
    return row.value(QStringLiteral("kind")).toString()==QStringLiteral("file")?QStringLiteral("[文件] ")+row.value(QStringLiteral("text")).toString():text;
}
void LiveStore::downloadMessage(int index){const auto row=messages_.get(index);files_->download(QJsonObject::fromVariantMap(row.value(QStringLiteral("attachment")).toMap()));}
void LiveStore::startFileSession(){
    const QUrl address(QStringLiteral("http://")+endpoint_);const auto fileEndpoint=QStringLiteral("http://")+address.host()+QStringLiteral(":18082");
    QNetworkRequest request(QUrl(fileEndpoint+QStringLiteral("/desktop")));request.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));request.setTransferTimeout(15000);
    const auto payload=QJsonDocument(QJsonObject{{QStringLiteral("op"),QStringLiteral("login")},{QStringLiteral("args"),QJsonObject{{QStringLiteral("username"),name_},{QStringLiteral("password"),password_}}}}).toJson(QJsonDocument::Compact);
    auto reply=http_.post(request,payload);password_.fill(QChar(0));password_.clear();const auto account=self_;const auto connection=connectionGeneration_;
    connect(reply,&QNetworkReply::finished,this,[this,reply,fileEndpoint,account,connection]{
        const auto body=QJsonDocument::fromJson(reply->readAll()).object();const auto token=body.value(QStringLiteral("token")).toString();reply->deleteLater();
        if(!authenticated_||self_!=account||connectionGeneration_!=connection)return;
        if(reply->error()!=QNetworkReply::NoError||!body.value(QStringLiteral("success")).toBool()||body.value(QStringLiteral("user_id")).toString()!=accountId()||token.isEmpty()){
            notify(QStringLiteral("聊天已连接，文件服务认证失败：")+body.value(QStringLiteral("error")).toString(reply->errorString()));emit event(QStringLiteral("file_session_failed"),{});return;
        }
        files_->connectSession(fileEndpoint,accountId(),token);emit sessionChanged();emit event(QStringLiteral("file_session_ready"),{});
        if(currentKey().startsWith(QStringLiteral("g:")))fetchGroupHistory(currentKey().mid(2),0);
    });
}
void LiveStore::fetchGroupHistory(const QString &gid,qint64 before){
    if(!fileReady()||gid.toLongLong()<=0||groupHistoryPending_.contains(gid))return;
    groupHistoryPending_.insert(gid);
    QJsonObject args{{QStringLiteral("group_id"),gid},{QStringLiteral("limit"),50}};if(before>0)args.insert(QStringLiteral("before_message_id"),QString::number(before));
    files_->request(QStringLiteral("group_history"),args,[this,gid](bool ok,const QJsonObject &b){groupHistoryPending_.remove(gid);if(!ok){notify(b.value(QStringLiteral("error")).toString());emit event(QStringLiteral("group_history_failed"),{});return;}
        const auto k=QStringLiteral("g:")+gid;auto &rows=history_[k];
        for(const auto &v:b.value(QStringLiteral("messages")).toArray()){
            const auto o=v.toObject();const auto mid=o.value(QStringLiteral("message_id")).toString(),from=o.value(QStringLiteral("from_user_id")).toString(),cid=o.value(QStringLiteral("client_message_id")).toString();
            if(o.value(QStringLiteral("group_id")).toString()!=gid||mid.toLongLong()<=0||from.toLongLong()<=0)continue;
            bool found=false;for(auto &r:rows)if(r.value(QStringLiteral("mid")).toString()==mid||(!cid.isEmpty()&&r.value(QStringLiteral("cid")).toString()==cid)){r[QStringLiteral("mid")]=mid;if(from==accountId())r[QStringLiteral("status")]=QStringLiteral("stored");found=true;break;}
            if(!found)rows.append(formatMessage({{QStringLiteral("mid"),mid},{QStringLiteral("cid"),cid},{QStringLiteral("author"),names_.value(from,QStringLiteral("用户 ")+from)},
                {QStringLiteral("text"),o.value(QStringLiteral("content")).toString()},{QStringLiteral("own"),from==accountId()},{QStringLiteral("kind"),QStringLiteral("text")},
                {QStringLiteral("time"),o.value(QStringLiteral("created_at")).toString()},{QStringLiteral("status"),from==accountId()?QStringLiteral("stored"):QStringLiteral("received")}}));
        }
        std::stable_sort(rows.begin(),rows.end(),[](const QVariantMap&a,const QVariantMap&b){const auto x=a.value(QStringLiteral("mid")).toString().toLongLong(),y=b.value(QStringLiteral("mid")).toString().toLongLong();return x>0&&(y==0||x<y);});
        if(currentKey()==k)syncCurrent();
        emit event(QStringLiteral("group_history"),{{QStringLiteral("group_id"),gid},{QStringLiteral("count"),b.value(QStringLiteral("messages")).toArray().size()}});
    });
}
