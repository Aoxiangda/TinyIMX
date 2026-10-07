#pragma once
#include "DemoStore.h"
#include "TimxCodec.h"
#include <QTcpSocket>
#include <QSharedMemory>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>

class LiveStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(RowModel* conversations READ conversations CONSTANT)
    Q_PROPERTY(RowModel* messages READ messages CONSTANT)
    Q_PROPERTY(RowModel* contacts READ contacts CONSTANT)
    Q_PROPERTY(RowModel* requests READ requests CONSTANT)
    Q_PROPERTY(RowModel* groups READ groups CONSTANT)
    Q_PROPERTY(RowModel* transfers READ transfers CONSTANT)
    Q_PROPERTY(QVariantMap current READ current NOTIFY selectionChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY draftChanged)
    Q_PROPERTY(bool online READ authenticated NOTIFY sessionChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY sessionChanged)
    Q_PROPERTY(bool loginBusy READ loginBusy NOTIFY sessionChanged)
    Q_PROPERTY(QString loginError READ loginError NOTIFY sessionChanged)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY sessionChanged)
    Q_PROPERTY(QString accountId READ accountId NOTIFY sessionChanged)
    Q_PROPERTY(QString accountName READ accountName NOTIFY sessionChanged)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY sessionChanged)
    Q_PROPERTY(bool groupMuted READ groupMuted CONSTANT)
    Q_PROPERTY(QString aiState READ aiState CONSTANT)
    Q_PROPERTY(QString aiAnswer READ aiAnswer CONSTANT)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
public:
    explicit LiveStore(QObject *parent=nullptr);
    ~LiveStore() override;
    RowModel *conversations(){return &conversations_;} RowModel *messages(){return &messages_;}
    RowModel *contacts(){return &contacts_;} RowModel *requests(){return &requests_;}
    RowModel *groups(){return &groups_;} RowModel *transfers(){return &transfers_;}
    QVariantMap current()const;int selectedIndex()const{return selected_;}
    QString draft()const{return drafts_.value(currentKey());}void setDraft(const QString &text);
    bool authenticated()const{return authenticated_;}bool loginBusy()const{return loginBusy_;}
    QString loginError()const{return error_;}QString connectionState()const{return state_;}
    QString accountId()const{return QString::number(self_);}QString accountName()const{return name_;}
    QString endpoint()const{return endpoint_;}bool groupMuted()const{return false;}
    QString aiState()const{return QStringLiteral("unavailable");}QString aiAnswer()const{return {};}
    QString notice()const{return notice_;}
    Q_INVOKABLE void login(const QString &endpoint,const QString &username,const QString &password);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectConversation(int index);
    Q_INVOKABLE void openConversation(const QString &peer);
    Q_INVOKABLE bool sendMessage(const QString &text);
    Q_INVOKABLE void retryMessage(int index);
    Q_INVOKABLE void markRead();
    Q_INVOKABLE void setViewActive(bool value);
    Q_INVOKABLE void loadEarlier();
    Q_INVOKABLE void addFriend(const QString &identity,const QString &note);
    Q_INVOKABLE void acceptRequest(int index,bool accept);
    Q_INVOKABLE void createGroup(const QString &,const QString &){unsupported();}
    Q_INVOKABLE void groupAction(const QString &){unsupported();}
    Q_INVOKABLE void transferAction(int,const QString &){unsupported();}
    Q_INVOKABLE void chooseFile(){unsupported();}Q_INVOKABLE void askAI(const QString &){unsupported();}
    Q_INVOKABLE void cancelAI(){unsupported();}Q_INVOKABLE void notify(const QString &text);
signals:
    void sessionChanged();void selectionChanged();void draftChanged();void noticeChanged();
    void event(const QString &kind,const QJsonObject &body);
private:
    struct Pending { quint16 type;QString key,cid; qint64 start; };
    RowModel conversations_,messages_,contacts_,requests_,groups_,transfers_;
    QTcpSocket socket_;QSharedMemory accountLock_;QTimer timer_;QElapsedTimer clock_;QByteArray input_;
    QHash<quint32,Pending> pending_;quint32 seq_{0};
    QHash<QString,QList<QVariantMap>> history_;QHash<QString,QString> drafts_;
    QHash<QString,QString> names_;QSet<QString> delivered_;QStringList deliveredOrder_;
    int selected_{-1};qint64 self_{0},started_{0},lastHeartbeat_{0},lastRefresh_{0};
    bool authenticated_{false},loginBusy_{false},viewActive_{false},suppressDisconnect_{false};
    QString name_,endpoint_,password_,state_{QStringLiteral("disconnected")},error_,notice_;
    QString currentKey()const;int ensureConversation(const QString &peer);
    void drain();void handle(const Timx::Frame &frame);void tick();
    void resetModels();void fail(const QString &message,const QString &state=QStringLiteral("disconnected"));
    quint32 rpc(quint16 type,const QJsonObject &body,const QString &key={},const QString &cid={});
    void fetchHistory(const QString &peer,qint64 before=0);void syncCurrent();
    void setMessageState(const QString &key,const QString &cid,const QString &state,const QString &mid={});
    bool sendRow(const QString &key,const QVariantMap &row);
    void unsupported(){notify(QStringLiteral("当前真实客户端已接入登录、好友与私聊；此模块仍在接入中。"));}
};
