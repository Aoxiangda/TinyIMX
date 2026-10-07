#include "DemoStore.h"
#include <QTimer>
#include <QUuid>
#include <QDateTime>
#include <QFileInfo>
#include <QDebug>
#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

int RowModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : rows_.size(); }
QVariant RowModel::data(const QModelIndex &index, int role) const {
    return index.isValid() && index.row() < rows_.size() && role == Qt::UserRole + 1
        ? QVariant(rows_.at(index.row())) : QVariant{};
}
QHash<int,QByteArray> RowModel::roleNames() const { return {{Qt::UserRole + 1, QByteArrayLiteral("item")}}; }
QVariantMap RowModel::get(int index) const { return index >= 0 && index < rows_.size() ? rows_[index] : QVariantMap{}; }
void RowModel::replace(QList<QVariantMap> rows) { beginResetModel(); rows_ = std::move(rows); endResetModel(); emit countChanged(); }
void RowModel::append(const QVariantMap &row) { const auto n = rows_.size(); beginInsertRows({},n,n); rows_.append(row); endInsertRows(); emit countChanged(); }
void RowModel::update(int index, const QVariantMap &fields) {
    if (index < 0 || index >= rows_.size()) return;
    for (auto it = fields.begin(); it != fields.end(); ++it) rows_[index][it.key()] = it.value();
    emit dataChanged(this->index(index), this->index(index), {Qt::UserRole + 1});
}
DemoStore::DemoStore(QObject *parent) : QObject(parent) { seed(); }
static QVariantMap person(const QString &id, const QString &name, const QString &initial,
                          const QString &color, const QString &subtitle, bool online = true) {
    return {{QStringLiteral("id"),id},{QStringLiteral("name"),name},{QStringLiteral("initial"),initial},
            {QStringLiteral("color"),color},{QStringLiteral("subtitle"),subtitle},{QStringLiteral("online"),online}};
}
static QVariantMap message(const QString &author, const QString &text, bool own, const QString &time,
                           const QString &kind = QStringLiteral("text")) {
    return {{QStringLiteral("author"),author},{QStringLiteral("text"),text},{QStringLiteral("own"),own},
            {QStringLiteral("time"),time},{QStringLiteral("kind"),kind},{QStringLiteral("status"),QStringLiteral("stored")},
            {QStringLiteral("cid"),QUuid::createUuid().toString(QUuid::WithoutBraces)}};
}
void DemoStore::seed() {
    auto group = person(QStringLiteral("g-1001"),QStringLiteral("产品研发小组"),QStringLiteral("研"),QStringLiteral("#6474df"),QStringLiteral("12 位成员 · 项目协作"));
    group.insert(QStringLiteral("kind"),QStringLiteral("group")); group.insert(QStringLiteral("preview"),QStringLiteral("林知夏：客户端的设计稿已经准备好了"));
    group.insert(QStringLiteral("unread"),3); group.insert(QStringLiteral("time"),QStringLiteral("10:42")); group.insert(QStringLiteral("pinned"),true);
    QList<QVariantMap> c{group};
    const QStringList names{QStringLiteral("林知夏"),QStringLiteral("陈序"),QStringLiteral("后端开发交流"),QStringLiteral("周予安"),QStringLiteral("设计讨论组")};
    const QStringList previews{QStringLiteral("发你一份更新后的接口说明"),QStringLiteral("好，下午一起看一下"),QStringLiteral("陆川：欢迎分享你的项目"),QStringLiteral("收到，辛苦啦"),QStringLiteral("今天的讨论记录已整理")};
    const QStringList colors{QStringLiteral("#cda881"),QStringLiteral("#86a49b"),QStringLiteral("#8c88c8"),QStringLiteral("#7797bc"),QStringLiteral("#9aaba7")};
    for (int i=0;i<names.size();++i) {
        auto row=person(QString::number(10002+i),names[i],names[i].left(1),colors[i],i==2||i==4?QStringLiteral("群聊"):QStringLiteral("项目联系人"),i!=3);
        row.insert(QStringLiteral("kind"),i==2||i==4?QStringLiteral("group"):QStringLiteral("private"));
        row.insert(QStringLiteral("preview"),previews[i]);row.insert(QStringLiteral("unread"),i==0?2:0);
        row.insert(QStringLiteral("time"),i<2?QStringLiteral("10:38"):QStringLiteral("昨天"));row.insert(QStringLiteral("pinned"),false);c.append(row);
    }
    conversations_.replace(c);
    history_[0]={message(QStringLiteral("系统"),QStringLiteral("今天 · 10 月 7 日"),false,{},QStringLiteral("separator")),
                 message(QStringLiteral("林知夏"),QStringLiteral("早上好！客户端的设计稿已经准备好了，今天我们把聊天和群管理的交互一起过一遍。"),false,QStringLiteral("10:32")),
                 message(QStringLiteral("我"),QStringLiteral("好的。发送状态需要区分“已保存”和“已读”，文件传输也要支持暂停和继续。"),true,QStringLiteral("10:34")),
                 message(QStringLiteral("陈序"),QStringLiteral("我整理了接口说明，大家可以先看这份。"),false,QStringLiteral("10:36")),
                 message(QStringLiteral("陈序"),QStringLiteral("TinyIMX 接口说明.pdf"),false,QStringLiteral("10:36"),QStringLiteral("file")),
                 message(QStringLiteral("林知夏"),QStringLiteral("右侧可以直接查看成员和群资料，不用离开当前会话。你们觉得这个流程怎么样？"),false,QStringLiteral("10:42"))};
    for(int i=1;i<c.size();++i) history_[i]={message(c[i].value(QStringLiteral("name")).toString(),previews[i-1],false,QStringLiteral("10:38")),message(QStringLiteral("我"),QStringLiteral("收到，我们在这里继续沟通。"),true,QStringLiteral("10:40"))};
    messages_.replace(history_[0]);
    QList<QVariantMap> people;
    for(int i=1;i<c.size();++i) if(c[i].value(QStringLiteral("kind")).toString()==QStringLiteral("private"))people.append(c[i]);
    people.append(person(QStringLiteral("10008"),QStringLiteral("陆川"),QStringLiteral("陆"),QStringLiteral("#a09baa"),QStringLiteral("C++ 开发")));
    contacts_.replace(people);
    requests_.replace({{{QStringLiteral("name"),QStringLiteral("沈星河")},{QStringLiteral("note"),QStringLiteral("你好，在开发交流组看到了你的项目。")},{QStringLiteral("state"),QStringLiteral("pending")},{QStringLiteral("id"),QStringLiteral("req-1")}},
                      {{QStringLiteral("name"),QStringLiteral("许宁")},{QStringLiteral("note"),QStringLiteral("我是测试同学，想和你交流客户端体验。")},{QStringLiteral("state"),QStringLiteral("pending")},{QStringLiteral("id"),QStringLiteral("req-2")}}});
    groups_.replace({{{QStringLiteral("name"),QStringLiteral("产品研发小组")},{QStringLiteral("initial"),QStringLiteral("研")},{QStringLiteral("color"),QStringLiteral("#6474df")},{QStringLiteral("members"),12},{QStringLiteral("role"),QStringLiteral("群主")},{QStringLiteral("description"),QStringLiteral("一起把产品做好。接口、设计、开发与测试在这里协作。")},{QStringLiteral("state"),QStringLiteral("active")},{QStringLiteral("version"),1}},
                     {{QStringLiteral("name"),QStringLiteral("后端开发交流")},{QStringLiteral("initial"),QStringLiteral("后")},{QStringLiteral("color"),QStringLiteral("#8c88c8")},{QStringLiteral("members"),48},{QStringLiteral("role"),QStringLiteral("成员")},{QStringLiteral("description"),QStringLiteral("C++ / 网络编程 / 分布式系统")},{QStringLiteral("state"),QStringLiteral("active")},{QStringLiteral("version"),1}}});
    transfers_.replace({{{QStringLiteral("name"),QStringLiteral("TinyIMX 接口说明.pdf")},{QStringLiteral("size"),QStringLiteral("2.4 MB")},{QStringLiteral("target"),QStringLiteral("产品研发小组")},{QStringLiteral("progress"),1.0},{QStringLiteral("state"),QStringLiteral("completed")},{QStringLiteral("direction"),QStringLiteral("下载")}},
                        {{QStringLiteral("name"),QStringLiteral("客户端交互设计.fig")},{QStringLiteral("size"),QStringLiteral("18.6 MB")},{QStringLiteral("target"),QStringLiteral("林知夏")},{QStringLiteral("progress"),0.64},{QStringLiteral("state"),QStringLiteral("paused")},{QStringLiteral("direction"),QStringLiteral("上传")}},
                        {{QStringLiteral("name"),QStringLiteral("项目文档.zip")},{QStringLiteral("size"),QStringLiteral("8.2 MB")},{QStringLiteral("target"),QStringLiteral("产品研发小组")},{QStringLiteral("progress"),0.28},{QStringLiteral("state"),QStringLiteral("failed")},{QStringLiteral("direction"),QStringLiteral("下载")}}});
}
QVariantMap DemoStore::current() const { return conversations_.get(selected_); }
void DemoStore::setDraft(const QString &value) { if(drafts_.value(selected_)==value)return;drafts_[selected_]=value;emit draftChanged(); }
void DemoStore::snapshot() { history_[selected_] = messages_.rows(); }
void DemoStore::selectConversation(int index) {
    if(index<0||index>=conversations_.rowCount())return;
    snapshot();selected_=index;messages_.replace(history_.value(index));markRead();emit selectionChanged();emit draftChanged();emit groupChanged();
}
void DemoStore::markRead() { conversations_.update(selected_,{{QStringLiteral("unread"),0}}); }
void DemoStore::setOnline(bool value) { if(online_==value)return;online_=value;emit onlineChanged();notify(value?QStringLiteral("已切回在线演示状态"):QStringLiteral("已切到离线演示状态；消息不会自动发送")); }
bool DemoStore::sendMessage(const QString &text) {
    if(text.trimmed().isEmpty())return false;
    if(text.toUtf8().size()>8192){notify(QStringLiteral("消息过长，请拆分后发送（界面限制 8 KiB）"));return false;}
    if(groupMuted()){notify(QStringLiteral("你已被禁言，暂时无法发送群消息"));return false;}
    if(selected_==0&&groups_.get(0).value(QStringLiteral("state")).toString()!=QStringLiteral("active")){notify(QStringLiteral("该群已解散或已退出，历史记录保留，不能继续发送"));return false;}
    auto row=message(QStringLiteral("我"),text.trimmed(),true,QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")));
    row.insert(QStringLiteral("status"),online_?QStringLiteral("stored"):QStringLiteral("failed"));
    messages_.append(row);snapshot();conversations_.update(selected_,{{QStringLiteral("preview"),text.trimmed()},{QStringLiteral("time"),row.value(QStringLiteral("time"))}});
    if(!online_)notify(QStringLiteral("发送失败：当前离线。恢复在线后可手动重试同一条消息。"));
    return true;
}
void DemoStore::retryMessage(int index) {
    if(messages_.get(index).value(QStringLiteral("status")).toString()!=QStringLiteral("failed"))return;
    if(!online_){notify(QStringLiteral("网络仍不可用，请先恢复在线状态"));return;}
    if(groupMuted()){notify(QStringLiteral("当前已被禁言，不能重试"));return;}
    if(selected_==0&&groups_.get(0).value(QStringLiteral("state")).toString()!=QStringLiteral("active"))return;
    // Update the existing row, preserving its business CID. Do not append a duplicate.
    messages_.update(index,{{QStringLiteral("status"),QStringLiteral("stored")}});snapshot();notify(QStringLiteral("演示重试成功；保留原消息标识，没有新增重复消息"));
}
void DemoStore::acceptRequest(int index,bool accept) {
    auto row=requests_.get(index);if(row.value(QStringLiteral("state")).toString()!=QStringLiteral("pending"))return;
    requests_.update(index,{{QStringLiteral("state"),accept?QStringLiteral("accepted"):QStringLiteral("rejected")}});
    if(accept)contacts_.append(person(row.value(QStringLiteral("id")).toString(),row.value(QStringLiteral("name")).toString(),row.value(QStringLiteral("name")).toString().left(1),QStringLiteral("#9e9dc2"),QStringLiteral("新联系人")));
    notify(accept?QStringLiteral("已在演示列表中添加联系人"):QStringLiteral("已拒绝这条演示申请"));
}
void DemoStore::addFriend(const QString &identity,const QString &note) {
    if(identity.trimmed().isEmpty()){notify(QStringLiteral("请输入用户 ID"));return;}
    requests_.append({{QStringLiteral("name"),identity.trimmed()},{QStringLiteral("note"),note},{QStringLiteral("state"),QStringLiteral("outgoing")},{QStringLiteral("id"),QUuid::createUuid().toString(QUuid::WithoutBraces)}});
    notify(QStringLiteral("演示申请已创建；等待对方接受"));
}
void DemoStore::createGroup(const QString &name,const QString &description) {
    if(name.trimmed().isEmpty()||name.size()>60){notify(QStringLiteral("群名称需要 1–60 个字符"));return;}
    groups_.append({{QStringLiteral("name"),name.trimmed()},{QStringLiteral("description"),description},{QStringLiteral("initial"),name.trimmed().left(1)},{QStringLiteral("color"),QStringLiteral("#6474df")},{QStringLiteral("members"),1},{QStringLiteral("role"),QStringLiteral("群主")},{QStringLiteral("state"),QStringLiteral("active")},{QStringLiteral("version"),1}});
    auto row=person(QUuid::createUuid().toString(QUuid::WithoutBraces),name.trimmed(),name.trimmed().left(1),QStringLiteral("#6474df"),QStringLiteral("1 位成员 · 新建群聊"));
    row.insert(QStringLiteral("kind"),QStringLiteral("group"));row.insert(QStringLiteral("preview"),QStringLiteral("你创建了这个群聊"));row.insert(QStringLiteral("unread"),0);row.insert(QStringLiteral("time"),QStringLiteral("刚刚"));row.insert(QStringLiteral("pinned"),false);
    conversations_.append(row);history_[conversations_.rowCount()-1]={message(QStringLiteral("系统"),QStringLiteral("群聊已创建，可以邀请成员加入"),false,{},QStringLiteral("separator"))};notify(QStringLiteral("新群聊已加入演示列表"));
}
void DemoStore::groupAction(const QString &action) {
    if(action==QStringLiteral("mute")){groupMuted_=!groupMuted_;emit groupChanged();notify(groupMuted_?QStringLiteral("已启用禁言预览"):QStringLiteral("已解除禁言预览"));}
    else if(action==QStringLiteral("invite")){notify(QStringLiteral("演示邀请已记录；真实接入后需等待服务端确认"));}
    else if(action==QStringLiteral("role")){notify(QStringLiteral("演示角色调整已记录；真实权限由服务端判定"));}
    else if(action==QStringLiteral("transfer")){groups_.update(0,{{QStringLiteral("role"),QStringLiteral("管理员")},{QStringLiteral("version"),groups_.get(0).value(QStringLiteral("version")).toInt()+1}});notify(QStringLiteral("演示群主已转让给林知夏"));}
    else if(action==QStringLiteral("disband")){groups_.update(0,{{QStringLiteral("state"),QStringLiteral("disbanded")}});notify(QStringLiteral("演示群已解散；历史信息保留"));}
    else if(action==QStringLiteral("leave")){groups_.update(0,{{QStringLiteral("state"),QStringLiteral("left")}});notify(QStringLiteral("已退出演示群；历史信息保留"));}
}
void DemoStore::transferAction(int index,const QString &action) {
    auto row=transfers_.get(index);if(row.isEmpty())return;
    const auto state=row.value(QStringLiteral("state")).toString();
    if(action==QStringLiteral("pause")&&state==QStringLiteral("transferring"))transfers_.update(index,{{QStringLiteral("state"),QStringLiteral("paused")}});
    else if(action==QStringLiteral("cancel")&&state!=QStringLiteral("completed"))transfers_.update(index,{{QStringLiteral("state"),QStringLiteral("canceled")}});
    else if(action==QStringLiteral("resume")&&(state==QStringLiteral("paused")||state==QStringLiteral("failed"))){
        if(!online_){notify(QStringLiteral("当前离线，无法继续传输"));return;}
        transfers_.update(index,{{QStringLiteral("state"),QStringLiteral("transferring")}});
        auto timer=new QTimer(this);timer->setInterval(350);
        connect(timer,&QTimer::timeout,this,[this,index,timer]{
            auto current=transfers_.get(index);if(current.value(QStringLiteral("state")).toString()!=QStringLiteral("transferring")){timer->stop();timer->deleteLater();return;}
            if(!online_){transfers_.update(index,{{QStringLiteral("state"),QStringLiteral("paused")}});timer->stop();timer->deleteLater();return;}
            const double p=std::min(1.0,current.value(QStringLiteral("progress")).toDouble()+0.035);
            transfers_.update(index,{{QStringLiteral("progress"),p},{QStringLiteral("state"),p>=1?QStringLiteral("completed"):QStringLiteral("transferring")}});
            if(p>=1){timer->stop();timer->deleteLater();}
        });timer->start();
    }
}
void DemoStore::chooseFile() { notify(QStringLiteral("文件选择入口已设计；请在文件中心体验暂停、继续与取消。当前原型不读取或上传本机文件。")); }
void DemoStore::askAI(const QString &prompt) {
    if(prompt.trimmed().isEmpty()||aiState_==QStringLiteral("thinking"))return;
    if(!online_){aiState_=QStringLiteral("error");aiAnswer_=QStringLiteral("当前离线，请恢复连接后重试。输入仍然保留。") ;emit aiChanged();return;}
    aiState_=QStringLiteral("thinking");aiAnswer_.clear();const int generation=++aiGeneration_;emit aiChanged();
    QTimer::singleShot(1600,this,[this,generation,prompt]{
        if(generation!=aiGeneration_)return;
        if(!online_){aiState_=QStringLiteral("error");aiAnswer_=QStringLiteral("连接已中断，请恢复后重试。");}
        else {aiState_=QStringLiteral("done");aiAnswer_=QStringLiteral("【本地示例回复】\n\n你的问题：%1\n\n建议先梳理目标，再把任务拆成可验证的步骤：\n1. 确认当前需求与接口边界。\n2. 检查失败、重试和权限变化后的交互。\n3. 保存结论与待办，方便下一次继续。\n\n真实 Ollama / Agent 服务尚未接入，本回复用于验证界面状态。").arg(prompt);}
        emit aiChanged();
    });
}
void DemoStore::cancelAI() { ++aiGeneration_;aiState_=QStringLiteral("canceled");aiAnswer_=QStringLiteral("已停止生成，输入内容保留，可以重新提交。");emit aiChanged(); }
void DemoStore::notify(const QString &text) { notice_=text;emit noticeChanged(); }
void DemoStore::loadEarlier() { notify(QStringLiteral("已到达本地示例记录的开头；真实接入将按 before_message_id 分页读取。")); }
int DemoStore::runSelfTest(const QString &reportPath) {
    int checks=0,failed=0;QJsonArray results;auto check=[&](bool ok,const char *name){++checks;results.append(QJsonObject{{QStringLiteral("name"),QString::fromLatin1(name)},{QStringLiteral("pass"),ok}});if(!ok){++failed;qWarning()<<name;}};
    check(conversations_.rowCount()==6,"initial conversations");check(!sendMessage(QString{}),"empty message rejected");
    selectConversation(1);const int before=messages_.rowCount();setOnline(false);check(sendMessage(QStringLiteral("保留 CID 的离线消息")),"offline message retained");
    const int n=messages_.rowCount()-1;const auto cid=messages_.get(n).value(QStringLiteral("cid"));check(messages_.get(n).value(QStringLiteral("status")).toString()==QStringLiteral("failed"),"offline not sent");
    retryMessage(n);check(messages_.get(n).value(QStringLiteral("status")).toString()==QStringLiteral("failed"),"retry while offline remains failed");
    setOnline(true);retryMessage(n);check(messages_.rowCount()==before+1&&messages_.get(n).value(QStringLiteral("cid"))==cid,"retry does not duplicate or change cid");
    check(messages_.get(n).value(QStringLiteral("status")).toString()==QStringLiteral("stored"),"retry status stored not read");
    selectConversation(0);groupAction(QStringLiteral("mute"));const int count=messages_.rowCount();check(!sendMessage(QStringLiteral("禁言"))&&messages_.rowCount()==count,"muted group denied");groupAction(QStringLiteral("mute"));
    const int people=contacts_.rowCount();acceptRequest(0,true);acceptRequest(0,true);check(contacts_.rowCount()==people+1,"accept request idempotent");acceptRequest(1,false);check(contacts_.rowCount()==people+1,"reject no new friend");
    const int groups=groups_.rowCount();createGroup(QString{},QString{});check(groups_.rowCount()==groups,"empty group rejected");createGroup(QStringLiteral("新群"),QStringLiteral("说明"));check(groups_.rowCount()==groups+1&&conversations_.rowCount()==7,"created group appears");
    transferAction(1,QStringLiteral("cancel"));const auto progress=transfers_.get(1).value(QStringLiteral("progress"));transferAction(1,QStringLiteral("resume"));check(transfers_.get(1).value(QStringLiteral("state")).toString()==QStringLiteral("canceled")&&transfers_.get(1).value(QStringLiteral("progress"))==progress,"canceled transfer cannot resume");
    askAI(QStringLiteral("测试"));check(aiState_==QStringLiteral("thinking"),"ai thinking");cancelAI();check(aiState_==QStringLiteral("canceled"),"ai canceled");
    selectConversation(1);check(messages_.get(messages_.rowCount()-1).value(QStringLiteral("cid"))==cid,"conversation switch retains history");
    setDraft(QStringLiteral("未发送的草稿"));selectConversation(0);check(draft().isEmpty(),"draft scoped to conversation");selectConversation(1);check(draft()==QStringLiteral("未发送的草稿"),"draft survives conversation switch");
    selectConversation(0);groupAction(QStringLiteral("disband"));check(!sendMessage(QStringLiteral("解散后禁止发送")),"disbanded group denies send");
    if(!reportPath.isEmpty()) {
        QFile f(reportPath);if(QFileInfo::exists(reportPath)||!f.open(QIODevice::WriteOnly))return 2;
        f.write(QJsonDocument(QJsonObject{{QStringLiteral("status"),failed?QStringLiteral("FAIL"):QStringLiteral("PASS")},{QStringLiteral("checks"),checks},{QStringLiteral("failures"),failed},{QStringLiteral("results"),results},{QStringLiteral("backend_requests"),0}}).toJson());
    }
    qInfo().noquote()<<QStringLiteral("DESKTOP_STATE_CONTRACT checks=%1 failures=%2").arg(checks).arg(failed);return failed?1:0;
}
