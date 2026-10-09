#include "LiveStore.h"
#include <QJsonArray>
#include <QDateTime>
#include <QUuid>
#include <QTimeZone>

namespace {
QString id(const QJsonValue &v){const auto n=Timx::positiveId(v);return n?QString::number(n):QString{};}
QString uuid(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
}
QVariantMap LiveStore::groupRow(const QJsonObject &b)const{
    auto row=b.toVariantMap();const auto gid=id(b.value(QStringLiteral("group_id")));
    row[QStringLiteral("id")]=gid;row[QStringLiteral("version")]=QString::number(b.value(QStringLiteral("version")).toInteger());
    row[QStringLiteral("owner_user_id")]=id(b.value(QStringLiteral("owner_user_id")));
    row[QStringLiteral("initial")]=b.value(QStringLiteral("name")).toString().left(1);
    row[QStringLiteral("color")]=QStringLiteral("#6474df");return row;
}
int LiveStore::ensureGroupConversation(const QString &gid,const QString &name){
    if(gid.toLongLong()<=0)return -1;
    const auto k=QStringLiteral("g:")+gid;
    for(int i=0;i<conversations_.rowCount();++i)if(conversations_.get(i).value(QStringLiteral("id")).toString()==k){
        if(!name.isEmpty())conversations_.update(i,{{QStringLiteral("name"),name},{QStringLiteral("initial"),name.left(1)}});
        return i;
    }
    const auto label=name.isEmpty()?QStringLiteral("群组 ")+gid:name;
    conversations_.append({{QStringLiteral("id"),k},{QStringLiteral("name"),label},{QStringLiteral("initial"),label.left(1)},
        {QStringLiteral("color"),QStringLiteral("#6474df")},{QStringLiteral("subtitle"),QStringLiteral("群 ID：")+gid},
        {QStringLiteral("online"),false},{QStringLiteral("kind"),QStringLiteral("group")},{QStringLiteral("preview"),QString{}},
        {QStringLiteral("unread"),0},{QStringLiteral("time"),QString{}},{QStringLiteral("pinned"),false}});
    return conversations_.rowCount()-1;
}
void LiveStore::createGroup(const QString &name,const QString &description){
    if(!authenticated_||name.trimmed().isEmpty()||name.toUtf8().size()>128||description.toUtf8().size()>512){notify(QStringLiteral("群名称或介绍无效"));return;}
    rpc(2023,{{QStringLiteral("client_operation_id"),uuid()},{QStringLiteral("name"),name.trimmed()},
        {QStringLiteral("description"),description},{QStringLiteral("join_policy"),QStringLiteral("open")},{QStringLiteral("max_members"),100}});
}
void LiveStore::selectGroup(const QString &gid){
    if(!authenticated_||gid.toLongLong()<=0)return;
    if(groupInfo_.value(QStringLiteral("id")).toString()!=gid){groupInfo_={{QStringLiteral("id"),gid}};groupMembers_.replace({});emit groupChanged();}
    for(const auto type:{2025,2045}){
        bool waiting=false;for(const auto &p:pending_)if(p.type==type&&p.key==gid)waiting=true;
        if(!waiting)rpc(quint16(type),{{QStringLiteral("group_id"),gid.toLongLong()},{QStringLiteral("limit"),100}},gid);
    }
}
void LiveStore::openGroup(const QString &gid){const auto ci=ensureGroupConversation(gid);if(ci>=0)selectConversation(ci);}
bool LiveStore::groupMuted()const{
    if(!currentKey().startsWith(QStringLiteral("g:"))||groupInfo_.value(QStringLiteral("id")).toString()!=currentKey().mid(2))return false;
    const auto until=groupInfo_.value(QStringLiteral("muted_until")).toString();
    auto timestamp=until;timestamp.replace(QChar(u' '),QChar(u'T'));if(!timestamp.endsWith(QChar(u'Z')))timestamp+=QChar(u'Z');
    const auto when=QDateTime::fromString(timestamp,Qt::ISODateWithMs);
    return when.isValid()&&when>QDateTime::currentDateTimeUtc();
}
void LiveStore::mutateGroup(const QString &action,const QString &gid,const QString &target,const QString &value){
    if(!authenticated_||gid.toLongLong()<=0)return;
    const QHash<QString,quint16> types{{QStringLiteral("update"),2027},{QStringLiteral("disband"),2029},{QStringLiteral("join"),2031},
        {QStringLiteral("leave"),2033},{QStringLiteral("invite"),2035},{QStringLiteral("kick"),2037},{QStringLiteral("admin"),2039},
        {QStringLiteral("member"),2039},{QStringLiteral("mute"),2041},{QStringLiteral("unmute"),2041},{QStringLiteral("transfer"),2043}};
    if(!types.contains(action))return;
    QJsonObject b{{QStringLiteral("group_id"),gid.toLongLong()},{QStringLiteral("client_operation_id"),uuid()}};
    if(action==QStringLiteral("update")||action==QStringLiteral("disband")){
        if(gid!=groupInfo_.value(QStringLiteral("id")).toString()||groupInfo_.value(QStringLiteral("version")).toString().toLongLong()<=0){notify(QStringLiteral("请先刷新群资料，再提交修改"));selectGroup(gid);return;}
        b.insert(QStringLiteral("expected_version"),groupInfo_.value(QStringLiteral("version")).toString().toLongLong());
        if(action==QStringLiteral("update")){
            if(target.trimmed().isEmpty()||target.toUtf8().size()>128||value.toUtf8().size()>512){notify(QStringLiteral("群资料无效"));return;}
            b.insert(QStringLiteral("name"),target.trimmed());b.insert(QStringLiteral("description"),value);
        }
    }else if(action!=QStringLiteral("join")&&action!=QStringLiteral("leave")){
        if(target.toLongLong()<=0){notify(QStringLiteral("请输入有效的成员 ID"));return;}
        b.insert(QStringLiteral("target_user_id"),target.toLongLong());
        if(action==QStringLiteral("admin")||action==QStringLiteral("member"))b.insert(QStringLiteral("role"),action);
        if(action==QStringLiteral("mute")||action==QStringLiteral("unmute"))b.insert(QStringLiteral("muted_until"),action==QStringLiteral("unmute")?QString{}:QDateTime::currentDateTimeUtc().addSecs(600).toString(Qt::ISODateWithMs));
    }
    // Prevent double-clicks; retries after uncertainty must not imply a success.
    for(const auto &p:pending_)if(p.type==types.value(action)&&p.key==gid)return;
    rpc(types.value(action),b,gid);
}
void LiveStore::handleGroupResponse(const Pending &p,const QJsonObject &b){
    if(p.type==2049){
        const auto mid=id(b.value(QStringLiteral("message_id")));
        if(mid.isEmpty()||id(b.value(QStringLiteral("group_id")))!=p.key.mid(2)||b.value(QStringLiteral("client_message_id")).toString()!=p.cid){setMessageState(p.key,p.cid,QStringLiteral("uncertain"));fail(QStringLiteral("群消息确认身份不匹配"));return;}
        setMessageState(p.key,p.cid,QStringLiteral("stored"),mid);
        const int ci=ensureGroupConversation(p.key.mid(2));for(const auto &row:history_.value(p.key))if(row.value(QStringLiteral("cid")).toString()==p.cid)conversations_.update(ci,{{QStringLiteral("preview"),previewText(row.value(QStringLiteral("raw"),row.value(QStringLiteral("text"))).toString())},{QStringLiteral("time"),row.value(QStringLiteral("time"))}});
        return;
    }
    if(p.type==2047){
        QList<QVariantMap> rows;for(const auto &v:b.value(QStringLiteral("groups")).toArray()){
            const auto row=groupRow(v.toObject());if(row.value(QStringLiteral("id")).toString().isEmpty())continue;rows.append(row);
            ensureGroupConversation(row.value(QStringLiteral("id")).toString(),row.value(QStringLiteral("name")).toString());
        }
        if(p.key.isEmpty())groups_.replace(rows);else for(const auto &row:rows)groups_.append(row);
        if(b.value(QStringLiteral("has_more")).toBool()&&!rows.isEmpty())rpc(2047,{{QStringLiteral("after_group_id"),rows.last().value(QStringLiteral("id")).toString().toLongLong()},{QStringLiteral("limit"),100}},QStringLiteral("page"));
    }else if(p.type==2045){
        if(groupInfo_.value(QStringLiteral("id")).toString()!=p.key.section(QChar(u'|'),0,0))return;
        QList<QVariantMap> rows;for(const auto &v:b.value(QStringLiteral("members")).toArray()){
            const auto o=v.toObject();auto row=o.toVariantMap();row[QStringLiteral("id")]=id(o.value(QStringLiteral("user_id")));rows.append(row);
            if(row.value(QStringLiteral("id")).toString()==accountId()){groupInfo_[QStringLiteral("role")]=row.value(QStringLiteral("role"));groupInfo_[QStringLiteral("muted_until")]=row.value(QStringLiteral("muted_until"));}
        }
        if(!p.key.contains(QChar(u'|')))groupMembers_.replace(rows);else for(const auto &row:rows)groupMembers_.append(row);
        if(b.value(QStringLiteral("has_more")).toBool()&&!rows.isEmpty())rpc(2045,{{QStringLiteral("group_id"),p.key.section(QChar(u'|'),0,0).toLongLong()},{QStringLiteral("after_user_id"),rows.last().value(QStringLiteral("id")).toString().toLongLong()},{QStringLiteral("limit"),100}},p.key.section(QChar(u'|'),0,0)+QStringLiteral("|page"));
        emit groupChanged();
    }else if(p.type==2025){
        if(groupInfo_.value(QStringLiteral("id")).toString()!=p.key)return;
        const auto old=groupInfo_;groupInfo_=groupRow(b.value(QStringLiteral("group")).toObject());
        groupInfo_[QStringLiteral("role")]=old.value(QStringLiteral("role"));groupInfo_[QStringLiteral("muted_until")]=old.value(QStringLiteral("muted_until"));emit groupChanged();
    }else{
        const auto group=b.value(QStringLiteral("group")).toObject();const auto gid=id(group.value(QStringLiteral("group_id")));
        notify(QStringLiteral("群组操作已由服务器确认"));refresh();if(!gid.isEmpty())selectGroup(gid);else if(!p.key.isEmpty()&&p.type!=2029&&p.type!=2033)selectGroup(p.key);
        if(p.type==2029||p.type==2033){groupInfo_.clear();groupMembers_.replace({});emit groupChanged();}
    }
}
void LiveStore::handleGroupDelivery(const Timx::Frame &f){
    const auto &b=f.body;const auto gid=id(b.value(QStringLiteral("group_id"))),mid=id(b.value(QStringLiteral("message_id"))),from=id(b.value(QStringLiteral("from_user_id")));
    if(!authenticated_||gid.isEmpty()||mid.isEmpty()||from.isEmpty()||!b.value(QStringLiteral("content")).isString()){fail(QStringLiteral("无效的群消息投递"));return;}
    const auto k=QStringLiteral("g:")+gid;const auto dedup=QStringLiteral("group:")+mid;const int ci=ensureGroupConversation(gid);
    bool known=delivered_.contains(dedup);for(const auto &r:history_.value(k))if(r.value(QStringLiteral("mid")).toString()==mid)known=true;
    if(!known){const auto text=b.value(QStringLiteral("content")).toString();history_[k].append(formatMessage({{QStringLiteral("author"),names_.value(from,QStringLiteral("用户 ")+from)},
        {QStringLiteral("text"),text},{QStringLiteral("own"),from==accountId()},{QStringLiteral("time"),b.value(QStringLiteral("created_at")).toString()},
        {QStringLiteral("kind"),QStringLiteral("text")},{QStringLiteral("status"),QStringLiteral("received")},{QStringLiteral("mid"),mid},{QStringLiteral("cid"),QString{}}}));
        conversations_.update(ci,{{QStringLiteral("preview"),previewText(text)},{QStringLiteral("unread"),viewActive_&&k==currentKey()?0:conversations_.get(ci).value(QStringLiteral("unread")).toInt()+1}});if(k==currentKey())syncCurrent();
    }
    if(!delivered_.contains(dedup)){delivered_.insert(dedup);deliveredOrder_.append(dedup);if(deliveredOrder_.size()>5000)delivered_.remove(deliveredOrder_.takeFirst());}
    socket_.write(Timx::encode(2052,f.seq,{{QStringLiteral("message_id"),mid.toLongLong()}}));emit event(QStringLiteral("group_delivery"),b);
}
