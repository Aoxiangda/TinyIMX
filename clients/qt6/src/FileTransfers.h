#pragma once
#include "DemoStore.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QNetworkReply>
#include <QJsonObject>
#include <QUrl>
#include <functional>
#include <memory>
class FileTransfers final:public QObject {
    Q_OBJECT
public:
    explicit FileTransfers(RowModel* model,QObject* parent=nullptr);
    ~FileTransfers() override;
    void setWorkspace(const QString &directory){workspace_=directory;}
    void connectSession(const QString &endpoint,const QString &actor,const QString &token);
    void disconnectSession();
    bool ready()const{return !token_.isEmpty();}
    void upload(const QUrl &url,const QString &conversation);
    void download(const QJsonObject &attachment);
    void action(int index,const QString &action);
    void request(const QString &op,const QJsonObject &args,const std::function<void(bool,const QJsonObject&)> &done);
    QString outputPath(int index)const;
signals:
    void notice(const QString &text);
    void attachmentReady(const QString &conversation,const QString &text);
    void changed();
    void event(const QString &kind,const QJsonObject &body);
private:
    struct Task {QVariantMap row;quint64 generation{0};bool busy{false};QPointer<QNetworkReply> reply;};
    RowModel *model_;QNetworkAccessManager http_;QString endpoint_,actor_,token_,workspace_,journal_,downloads_;quint64 sessionGeneration_{0};bool journalWritable_{true};
    QList<std::shared_ptr<Task>> tasks_;
    void sync();void save();void load();void fail(const std::shared_ptr<Task> &task,const QString &message);
    void hashUpload(const std::shared_ptr<Task> &task,bool begin);
    void uploadStep(const std::shared_ptr<Task> &task);
    void downloadStep(const std::shared_ptr<Task> &task);
    void verifyDownload(const std::shared_ptr<Task> &task);
    void completeUpload(const std::shared_ptr<Task> &task);
    void cancelUpload(const std::shared_ptr<Task> &task);
    void taskRequest(const std::shared_ptr<Task> &task,const QString &op,const QJsonObject &args,const std::function<void(const QJsonObject&)> &done);
};
