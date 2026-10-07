#include "FileTransfers.h"
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QUuid>
#include <QTimer>
#include <QDesktopServices>
#include <QRegularExpression>
namespace {
QString str(const QJsonValue &v){return v.isString()?v.toString():QString::number(v.toInteger());}
QString uid(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
QString sha(const QByteArray &data){return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());}
QString safeName(QString name){name=QFileInfo(name).fileName();name.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}._ -]")),QStringLiteral("_"));return name.isEmpty()?QStringLiteral("file.bin"):name.left(100);}
}
FileTransfers::FileTransfers(RowModel *model,QObject *parent):QObject(parent),model_(model){http_.setProxy(QNetworkProxy::NoProxy);}
FileTransfers::~FileTransfers(){disconnectSession();for(auto reply:http_.findChildren<QNetworkReply*>())disconnect(reply,nullptr,this,nullptr);}
void FileTransfers::connectSession(const QString &endpoint,const QString &actor,const QString &token){
    disconnectSession();endpoint_=endpoint;actor_=actor;token_=token;
    const auto scope=sha((endpoint+QChar(u'|')+actor).toUtf8());
    const auto root=workspace_.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation):workspace_;
    const auto directory=root+QStringLiteral("/transfers/")+scope;QDir().mkpath(directory);journal_=directory+QStringLiteral("/tasks.json");
    downloads_=workspace_.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)+QStringLiteral("/TinyIMX"):workspace_+QStringLiteral("/downloads");QDir().mkpath(downloads_);load();emit changed();
}
void FileTransfers::disconnectSession(){
    if(ready())request(QStringLiteral("logout"),{},[](bool,const QJsonObject&){});
    ++sessionGeneration_;for(const auto &task:tasks_){++task->generation;if(task->reply){disconnect(task->reply,nullptr,this,nullptr);task->reply->abort();task->reply->deleteLater();task->reply=nullptr;}task->busy=false;if(task->row.value(QStringLiteral("state")).toString()==QStringLiteral("transferring"))task->row[QStringLiteral("state")]=QStringLiteral("paused");}
    save();journal_.clear();actor_.clear();endpoint_.clear();token_.clear();tasks_.clear();model_->replace({});emit changed();
}
void FileTransfers::sync(){QList<QVariantMap> rows;for(const auto &t:tasks_)rows.append(t->row);model_->replace(rows);save();}
void FileTransfers::save(){if(journal_.isEmpty()||!journalWritable_)return;QJsonArray rows;for(const auto &t:tasks_)rows.append(QJsonObject::fromVariantMap(t->row));QSaveFile f(journal_);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(rows).toJson());if(!f.commit())emit notice(QStringLiteral("传输任务保存失败"));}}
void FileTransfers::load(){
    tasks_.clear();journalWritable_=true;QFile f(journal_);
    if(f.exists()){
        QJsonParseError error;
        if(!f.open(QIODevice::ReadOnly)||f.size()>2*1024*1024){journalWritable_=false;emit notice(QStringLiteral("任务记录无法读取；原记录已保留"));return;}
        const auto document=QJsonDocument::fromJson(f.readAll(),&error);
        if(error.error!=QJsonParseError::NoError||!document.isArray()||document.array().size()>100){journalWritable_=false;emit notice(QStringLiteral("任务记录格式异常；原记录已保留"));return;}
        for(const auto &v:document.array()){
            auto task=std::make_shared<Task>();task->row=v.toObject().toVariantMap();
            const auto id=task->row.value(QStringLiteral("id")).toString(),path=task->row.value(QStringLiteral("path")).toString();
            const bool download=task->row.value(QStringLiteral("direction")).toString()==QStringLiteral("download");
            if(task->row.value(QStringLiteral("actor")).toString()!=actor_||QUuid(id).isNull()||(download&&(QDir::cleanPath(QFileInfo(path).absolutePath())!=QDir::cleanPath(downloads_)||!QFileInfo(path).fileName().startsWith(id+QChar(u'-'))))){
                tasks_.clear();journalWritable_=false;emit notice(QStringLiteral("任务身份或保存路径异常；原记录已保留"));return;
            }
            if(task->row.value(QStringLiteral("state")).toString()==QStringLiteral("transferring"))task->row[QStringLiteral("state")]=QStringLiteral("paused");
            if(task->row.value(QStringLiteral("state")).toString()==QStringLiteral("canceling")){task->row[QStringLiteral("state")]=QStringLiteral("failed");task->row[QStringLiteral("error")]=QStringLiteral("取消尚未确认；重试将继续请求取消");}
            task->row[QStringLiteral("begin_pending")]=false;
            tasks_.append(task);
        }
    }
    sync();
}
void FileTransfers::request(const QString &op,const QJsonObject &args,const std::function<void(bool,const QJsonObject&)> &done){
    if(!ready()){done(false,{{QStringLiteral("error"),QStringLiteral("文件服务尚未认证，请重新登录")}});return;}
    QNetworkRequest request(QUrl(endpoint_+QStringLiteral("/desktop")));request.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));request.setRawHeader(QByteArray("Authorization"),QByteArray("Bearer ")+token_.toUtf8());request.setTransferTimeout(15000);
    auto reply=http_.post(request,QJsonDocument(QJsonObject{{QStringLiteral("op"),op},{QStringLiteral("args"),args}}).toJson(QJsonDocument::Compact));const auto generation=sessionGeneration_;
    connect(reply,&QNetworkReply::finished,this,[this,reply,generation,done]{auto b=QJsonDocument::fromJson(reply->readAll()).object();const bool ok=reply->error()==QNetworkReply::NoError&&b.value(QStringLiteral("success")).toBool();if(!ok&&!b.contains(QStringLiteral("error")))b.insert(QStringLiteral("error"),reply->errorString());reply->deleteLater();if(generation==sessionGeneration_)done(ok,b);});
}
void FileTransfers::taskRequest(const std::shared_ptr<Task> &task,const QString &op,const QJsonObject &args,const std::function<void(const QJsonObject&)> &done){
    task->busy=true;const auto generation=task->generation;request(op,args,[this,task,generation,done](bool ok,const QJsonObject &b){if(generation!=task->generation)return;task->busy=false;if(!ok){fail(task,b.value(QStringLiteral("error")).toString());return;}done(b);});
}
void FileTransfers::fail(const std::shared_ptr<Task> &task,const QString &message){task->busy=false;task->row[QStringLiteral("state")]=QStringLiteral("failed");task->row[QStringLiteral("error")]=message;sync();emit notice(message);emit event(QStringLiteral("transfer_failed"),{{QStringLiteral("id"),task->row.value(QStringLiteral("id")).toString()},{QStringLiteral("error"),message}});}
void FileTransfers::upload(const QUrl &url,const QString &conversation){
    const QFileInfo info(url.toLocalFile());if(!ready()||!url.isLocalFile()||!info.isFile()||info.isSymLink()||info.size()<=0||info.size()>64*1024*1024||tasks_.size()>=100){emit notice(QStringLiteral("请选择 1 字节至 64 MiB 的普通文件，并确认文件服务已连接"));return;}
    auto task=std::make_shared<Task>();task->row={{QStringLiteral("id"),uid()},{QStringLiteral("actor"),actor_},{QStringLiteral("name"),safeName(info.fileName())},{QStringLiteral("path"),info.absoluteFilePath()},
        {QStringLiteral("total"),QString::number(info.size())},{QStringLiteral("size"),QString::number(info.size()/1024.0,'f',1)+QStringLiteral(" KiB")},{QStringLiteral("direction"),QStringLiteral("upload")},
        {QStringLiteral("conversation"),conversation},{QStringLiteral("target"),conversation.isEmpty()?QStringLiteral("个人文件"):conversation},{QStringLiteral("state"),QStringLiteral("transferring")},{QStringLiteral("progress"),0.0},{QStringLiteral("offset"),QStringLiteral("0")},{QStringLiteral("error"),QString{}}};tasks_.append(task);sync();hashUpload(task,true);
}
void FileTransfers::hashUpload(const std::shared_ptr<Task> &task,bool begin){
    auto file=std::make_shared<QFile>(task->row.value(QStringLiteral("path")).toString());if(!file->open(QIODevice::ReadOnly)||file->size()!=task->row.value(QStringLiteral("total")).toString().toLongLong()){fail(task,QStringLiteral("源文件不可读或大小已改变"));return;}
    auto hash=std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);auto timer=new QTimer(this);timer->setInterval(0);const auto generation=task->generation,session=sessionGeneration_;task->busy=true;
    connect(timer,&QTimer::timeout,this,[this,task,file,hash,timer,generation,session,begin]{
        if(session!=sessionGeneration_||generation!=task->generation){timer->stop();timer->deleteLater();return;}
        const auto bytes=file->read(256*1024);if(bytes.isEmpty()&&!file->atEnd()){timer->stop();timer->deleteLater();fail(task,QStringLiteral("读取源文件失败"));return;}hash->addData(bytes);if(!file->atEnd())return;timer->stop();timer->deleteLater();task->busy=false;
        const auto digest=QString::fromLatin1(hash->result().toHex());if(!begin&&digest!=task->row.value(QStringLiteral("sha256")).toString()){fail(task,QStringLiteral("源文件哈希已改变，不能继续原上传"));return;}task->row[QStringLiteral("sha256")]=digest;
        if(begin){task->row[QStringLiteral("begin_pending")]=true;task->busy=true;sync();request(QStringLiteral("begin"),{{QStringLiteral("client_upload_id"),task->row.value(QStringLiteral("id")).toString()},{QStringLiteral("file_name"),task->row.value(QStringLiteral("name")).toString()},
            {QStringLiteral("content_type"),QStringLiteral("application/octet-stream")},{QStringLiteral("total_size"),task->row.value(QStringLiteral("total")).toString()},{QStringLiteral("checksum_algorithm"),QStringLiteral("sha256")},{QStringLiteral("expected_checksum"),digest},{QStringLiteral("preferred_chunk_size"),QStringLiteral("262144")}},[this,task,generation](bool ok,const QJsonObject &b){
                task->row[QStringLiteral("begin_pending")]=false;
                if(!ok){if(generation==task->generation||task->row.value(QStringLiteral("cancel_requested")).toBool())fail(task,b.value(QStringLiteral("error")).toString());return;}
                // Begin is idempotent by task ID. Keep the durable ID even when pause/cancel
                // advanced the task generation while the server created its session.
                const auto s=b.value(QStringLiteral("session")).toObject();task->row[QStringLiteral("upload_id")]=str(s.value(QStringLiteral("upload_id")));task->row[QStringLiteral("file_id")]=str(s.value(QStringLiteral("file_id")));task->row[QStringLiteral("chunk_size")]=str(s.value(QStringLiteral("chunk_size")));sync();
                if(task->row.value(QStringLiteral("cancel_requested")).toBool()){cancelUpload(task);return;}
                if(generation==task->generation){task->busy=false;uploadStep(task);}
            });emit event(QStringLiteral("upload_begin_sent"),{{QStringLiteral("id"),task->row.value(QStringLiteral("id")).toString()}});
        }else if(task->row.value(QStringLiteral("cancel_requested")).toBool())cancelUpload(task);else uploadStep(task);
    });timer->start();
}
void FileTransfers::uploadStep(const std::shared_ptr<Task> &task){
    if(task->row.value(QStringLiteral("state")).toString()!=QStringLiteral("transferring")||task->busy)return;
    taskRequest(task,QStringLiteral("progress"),{{QStringLiteral("upload_id"),task->row.value(QStringLiteral("upload_id")).toString()}},[this,task](const QJsonObject &b){
        const auto progress=b.value(QStringLiteral("progress")).toObject();const auto size=task->row.value(QStringLiteral("total")).toString().toLongLong(),chunk=task->row.value(QStringLiteral("chunk_size")).toString().toLongLong();
        if(chunk<=0||chunk>262144){fail(task,QStringLiteral("文件服务返回无效的分块大小"));return;}
        const auto expected=str(progress.value(QStringLiteral("expected_chunk_count"))).toLongLong(),stored=str(progress.value(QStringLiteral("stored_chunk_count"))).toLongLong();task->row[QStringLiteral("progress")]=expected?double(stored)/expected:0.0;sync();
        if(progress.value(QStringLiteral("ready_to_finalize")).toBool()){taskRequest(task,QStringLiteral("finalize"),{{QStringLiteral("upload_id"),task->row.value(QStringLiteral("upload_id")).toString()}},[this,task](const QJsonObject &result){
            if(result.value(QStringLiteral("file")).toObject().value(QStringLiteral("status")).toString()!=QStringLiteral("FILE_STATUS_AVAILABLE")||result.value(QStringLiteral("verified_checksum")).toString()!=task->row.value(QStringLiteral("sha256")).toString()){fail(task,QStringLiteral("文件终验未通过，不能发布"));return;}completeUpload(task);
        });return;}
        const auto missing=progress.value(QStringLiteral("missing_ranges")).toArray();if(missing.isEmpty()){fail(task,QStringLiteral("服务端缺块进度不一致"));return;}
        const auto index=str(missing.first().toObject().value(QStringLiteral("start_index"))).toLongLong();const auto offset=index*chunk;QFile file(task->row.value(QStringLiteral("path")).toString());if(!file.open(QIODevice::ReadOnly)||file.size()!=size||!file.seek(offset)){fail(task,QStringLiteral("源文件已改变或不可读"));return;}const auto bytes=file.read(qMin(chunk,size-offset));if(bytes.size()!=qMin(chunk,size-offset)){fail(task,QStringLiteral("源文件读取不完整"));return;}
        taskRequest(task,QStringLiteral("chunk"),{{QStringLiteral("upload_id"),task->row.value(QStringLiteral("upload_id")).toString()},{QStringLiteral("chunk_index"),QString::number(index)},{QStringLiteral("byte_offset"),QString::number(offset)},
            {QStringLiteral("data"),QString::fromLatin1(bytes.toBase64())},{QStringLiteral("checksum_algorithm"),QStringLiteral("sha256")},{QStringLiteral("checksum"),sha(bytes)}},[this,task](const QJsonObject&){QTimer::singleShot(0,this,[this,task]{uploadStep(task);});});
    });
}
void FileTransfers::completeUpload(const std::shared_ptr<Task> &task){
    const auto conversation=task->row.value(QStringLiteral("conversation")).toString();if(conversation.isEmpty()){task->row[QStringLiteral("state")]=QStringLiteral("completed");task->row[QStringLiteral("progress")]=1.0;sync();emit event(QStringLiteral("upload_completed"),{{QStringLiteral("file_id"),task->row.value(QStringLiteral("file_id")).toString()}});return;}
    QJsonObject args{{QStringLiteral("file_id"),task->row.value(QStringLiteral("file_id")).toString()}};args.insert(conversation.startsWith(QStringLiteral("g:"))?QStringLiteral("group_id"):QStringLiteral("target_user_id"),conversation.startsWith(QStringLiteral("g:"))?conversation.mid(2):conversation);
    taskRequest(task,QStringLiteral("share"),args,[this,task,conversation](const QJsonObject &b){const auto info=b.value(QStringLiteral("info")).toObject();QJsonObject attachment{{QStringLiteral("tinyimx_file"),1},{QStringLiteral("file_id"),task->row.value(QStringLiteral("file_id")).toString()},{QStringLiteral("name"),info.value(QStringLiteral("file_name"))},{QStringLiteral("size"),str(info.value(QStringLiteral("total_size")))},{QStringLiteral("sha256"),info.value(QStringLiteral("verified_checksum"))},{QStringLiteral("capability"),b.value(QStringLiteral("capability"))}};
        task->row[QStringLiteral("state")]=QStringLiteral("completed");task->row[QStringLiteral("progress")]=1.0;task->row[QStringLiteral("attachment")]=attachment.toVariantMap();sync();emit attachmentReady(conversation,QString::fromUtf8(QJsonDocument(attachment).toJson(QJsonDocument::Compact)));emit event(QStringLiteral("upload_completed"),{{QStringLiteral("file_id"),task->row.value(QStringLiteral("file_id")).toString()}});
    });
}
void FileTransfers::download(const QJsonObject &attachment){
    if(!ready()||tasks_.size()>=100||attachment.value(QStringLiteral("tinyimx_file")).toInt()!=1){emit notice(QStringLiteral("文件服务未连接或附件无效"));return;}
    const auto id=uid();auto task=std::make_shared<Task>();task->row={{QStringLiteral("id"),id},{QStringLiteral("actor"),actor_},{QStringLiteral("name"),safeName(attachment.value(QStringLiteral("name")).toString())},
        {QStringLiteral("file_id"),attachment.value(QStringLiteral("file_id")).toString()},{QStringLiteral("capability"),attachment.value(QStringLiteral("capability")).toString()},
        {QStringLiteral("direction"),QStringLiteral("download")},{QStringLiteral("target"),QStringLiteral("已授权的分享文件")},{QStringLiteral("state"),QStringLiteral("transferring")},{QStringLiteral("progress"),0.0},{QStringLiteral("offset"),QStringLiteral("0")},
        {QStringLiteral("path"),downloads_+QChar(u'/')+id+QChar(u'-')+safeName(attachment.value(QStringLiteral("name")).toString())}};
    const auto part=task->row.value(QStringLiteral("path")).toString()+QStringLiteral(".part");QFile file(part);if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)){emit notice(QStringLiteral("无法创建新的下载文件"));return;}file.close();tasks_.append(task);sync();
    taskRequest(task,QStringLiteral("download_info"),{{QStringLiteral("file_id"),task->row.value(QStringLiteral("file_id")).toString()},{QStringLiteral("capability"),task->row.value(QStringLiteral("capability")).toString()}},[this,task](const QJsonObject &b){const auto info=b.value(QStringLiteral("info")).toObject();task->row[QStringLiteral("total")]=str(info.value(QStringLiteral("total_size")));task->row[QStringLiteral("sha256")]=info.value(QStringLiteral("verified_checksum")).toString();task->row[QStringLiteral("size")]=QString::number(str(info.value(QStringLiteral("total_size"))).toLongLong()/1024.0,'f',1)+QStringLiteral(" KiB");sync();downloadStep(task);});
}
void FileTransfers::downloadStep(const std::shared_ptr<Task> &task){
    if(task->row.value(QStringLiteral("state")).toString()!=QStringLiteral("transferring")||task->busy)return;
    const auto offset=task->row.value(QStringLiteral("offset")).toString().toLongLong(),size=task->row.value(QStringLiteral("total")).toString().toLongLong();
    if(offset==size){verifyDownload(task);return;}
    if(offset<0||offset>size||size<=0||size>64*1024*1024){fail(task,QStringLiteral("下载大小或偏移无效"));return;}
    taskRequest(task,QStringLiteral("read_range"),{{QStringLiteral("file_id"),task->row.value(QStringLiteral("file_id")).toString()},{QStringLiteral("capability"),task->row.value(QStringLiteral("capability")).toString()},
        {QStringLiteral("offset"),QString::number(offset)},{QStringLiteral("length"),QString::number(qMin<qint64>(65536,size-offset))},{QStringLiteral("if_match_sha256"),task->row.value(QStringLiteral("sha256")).toString()}},[this,task,offset,size](const QJsonObject &b){const auto bytes=QByteArray::fromBase64(b.value(QStringLiteral("data")).toString().toLatin1());const auto next=str(b.value(QStringLiteral("next_offset"))).toLongLong();
        if(bytes.isEmpty()||bytes.size()>65536||next!=offset+bytes.size()||str(b.value(QStringLiteral("offset"))).toLongLong()!=offset||sha(bytes)!=b.value(QStringLiteral("range_sha256")).toString()){fail(task,QStringLiteral("下载分块身份、偏移或哈希错误"));return;}
        const auto part=task->row.value(QStringLiteral("path")).toString()+QStringLiteral(".part");QFile file(part);if(QFileInfo(part).isSymLink()||!file.open(QIODevice::ReadWrite)||file.size()!=offset||!file.seek(offset)||file.write(bytes)!=bytes.size()||!file.flush()){fail(task,QStringLiteral("写入下载分块失败"));return;}file.close();task->row[QStringLiteral("offset")]=QString::number(next);task->row[QStringLiteral("progress")]=double(next)/size;sync();QTimer::singleShot(0,this,[this,task]{downloadStep(task);});
    });
}
void FileTransfers::verifyDownload(const std::shared_ptr<Task> &task){
    const auto part=task->row.value(QStringLiteral("path")).toString()+QStringLiteral(".part");
    auto file=std::make_shared<QFile>(part);const auto size=task->row.value(QStringLiteral("total")).toString().toLongLong();
    if(QFileInfo(part).isSymLink()||!file->open(QIODevice::ReadOnly)||file->size()!=size){fail(task,QStringLiteral("下载文件长度或保存路径错误"));return;}
    auto hash=std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);auto timer=new QTimer(this);timer->setInterval(0);task->busy=true;
    const auto generation=task->generation,session=sessionGeneration_;
    connect(timer,&QTimer::timeout,this,[this,task,file,hash,timer,generation,session,part]{
        if(generation!=task->generation||session!=sessionGeneration_){timer->stop();timer->deleteLater();return;}
        const auto bytes=file->read(256*1024);if(bytes.isEmpty()&&!file->atEnd()){timer->stop();timer->deleteLater();fail(task,QStringLiteral("下载文件读取失败"));return;}hash->addData(bytes);if(!file->atEnd())return;
        timer->stop();timer->deleteLater();file->close();task->busy=false;
        if(QString::fromLatin1(hash->result().toHex())!=task->row.value(QStringLiteral("sha256")).toString()){
            task->row[QStringLiteral("restart_required")]=true;fail(task,QStringLiteral("下载文件整体验证失败；重试会创建新任务并保留失败文件"));return;
        }
        if(!QFile::rename(part,task->row.value(QStringLiteral("path")).toString())){fail(task,QStringLiteral("无法发布下载文件；已有同名文件不会覆盖"));return;}
        task->row[QStringLiteral("state")]=QStringLiteral("completed");task->row[QStringLiteral("progress")]=1.0;sync();emit event(QStringLiteral("download_completed"),{{QStringLiteral("file_id"),task->row.value(QStringLiteral("file_id")).toString()}});
    });timer->start();
}
void FileTransfers::action(int index,const QString &action){
    if(index<0||index>=tasks_.size())return;
    const auto task=tasks_[index];const auto state=task->row.value(QStringLiteral("state")).toString();
    if(action==QStringLiteral("open")&&state==QStringLiteral("completed")){QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(task->row.value(QStringLiteral("path")).toString()).absolutePath()));return;}
    if(action==QStringLiteral("pause")&&state==QStringLiteral("transferring")){++task->generation;task->busy=false;task->row[QStringLiteral("state")]=QStringLiteral("paused");sync();return;}
    if(action==QStringLiteral("cancel")&&state!=QStringLiteral("completed")&&state!=QStringLiteral("canceled")&&state!=QStringLiteral("canceling")){
        ++task->generation;task->busy=false;task->row[QStringLiteral("state")]=QStringLiteral("paused");sync();
        if(task->row.value(QStringLiteral("direction")).toString()==QStringLiteral("upload")){
            task->row[QStringLiteral("cancel_requested")]=true;
            if(!task->row.value(QStringLiteral("upload_id")).toString().isEmpty())cancelUpload(task);
            else if(task->row.value(QStringLiteral("begin_pending")).toBool()){task->row[QStringLiteral("state")]=QStringLiteral("canceling");sync();}
            else{task->row[QStringLiteral("cancel_requested")]=false;task->row[QStringLiteral("state")]=QStringLiteral("canceled");sync();}
        }else{task->row[QStringLiteral("state")]=QStringLiteral("canceled");sync();}return;
    }
    if((action==QStringLiteral("resume")||action==QStringLiteral("retry"))&&(state==QStringLiteral("paused")||state==QStringLiteral("failed"))&&ready()){
        if(task->row.value(QStringLiteral("restart_required")).toBool()){
            download({{QStringLiteral("tinyimx_file"),1},{QStringLiteral("name"),task->row.value(QStringLiteral("name")).toString()},{QStringLiteral("file_id"),task->row.value(QStringLiteral("file_id")).toString()},{QStringLiteral("capability"),task->row.value(QStringLiteral("capability")).toString()}});return;
        }
        ++task->generation;task->busy=false;task->row[QStringLiteral("state")]=QStringLiteral("transferring");task->row[QStringLiteral("error")]=QString{};sync();
        if(task->row.value(QStringLiteral("cancel_requested")).toBool()&&!task->row.value(QStringLiteral("upload_id")).toString().isEmpty()){cancelUpload(task);return;}
        if(task->row.value(QStringLiteral("direction")).toString()==QStringLiteral("upload"))hashUpload(task,task->row.value(QStringLiteral("upload_id")).toString().isEmpty());else downloadStep(task);
    }
}
QString FileTransfers::outputPath(int index)const{return index>=0&&index<tasks_.size()?tasks_[index]->row.value(QStringLiteral("path")).toString():QString{};}
void FileTransfers::cancelUpload(const std::shared_ptr<Task> &task){
    task->row[QStringLiteral("state")]=QStringLiteral("canceling");sync();
    taskRequest(task,QStringLiteral("cancel"),{{QStringLiteral("upload_id"),task->row.value(QStringLiteral("upload_id")).toString()}},[this,task](const QJsonObject&){task->row[QStringLiteral("state")]=QStringLiteral("canceled");task->row[QStringLiteral("cancel_requested")]=false;sync();});
}
