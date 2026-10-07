#include "DemoStore.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTimer>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDateTime>
#include <QDebug>
#include <QFont>
#include <QFileInfo>
#include <memory>
#include <QMutex>
#include <QMutexLocker>

static QString captureLog;
static QMutex captureMutex;
static int captureWarnings=0;
static void writeQtMessage(QtMsgType type,const QMessageLogContext &context,const QString &message) {
    QMutexLocker lock(&captureMutex);
    if(type==QtWarningMsg||type==QtCriticalMsg||type==QtFatalMsg)++captureWarnings;
    QFile file(captureLog);if(file.open(QIODevice::WriteOnly|QIODevice::Append)){
        file.write(QStringLiteral("%1\t%2\t%3:%4\t%5\n").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)).arg(int(type)).arg(QString::fromUtf8(context.file?context.file:"")).arg(context.line).arg(message).toUtf8());
    }
}

int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);
    app.setApplicationName(QStringLiteral("TinyIMX Desktop"));app.setOrganizationName(QStringLiteral("TinyIMX"));
    QFont font(QStringLiteral("Microsoft YaHei UI"));font.setPixelSize(14);app.setFont(font);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    DemoStore store;
    if(app.arguments().contains(QStringLiteral("--self-test"))){const int report=app.arguments().indexOf(QStringLiteral("--self-test-report"));return store.runSelfTest(report>=0&&report+1<app.arguments().size()?app.arguments().at(report+1):QString{});}
    const int capture=app.arguments().indexOf(QStringLiteral("--capture"));
    QString directory;
    if(capture>=0){
        if(capture+1>=app.arguments().size())return 2;
        directory=QDir::cleanPath(app.arguments().at(capture+1));
        if(QFileInfo::exists(directory)||!QDir().mkpath(directory))return 2;
        captureLog=directory+QStringLiteral("/qt-runtime.log");qInstallMessageHandler(writeQtMessage);
    }
    QQmlApplicationEngine engine;engine.rootContext()->setContextProperty(QStringLiteral("demo"),&store);
    QObject::connect(&engine,&QQmlApplicationEngine::objectCreationFailed,&app,[] { QCoreApplication::exit(1); },Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("TinyIMX.Client"),QStringLiteral("Main"));
    if(capture>=0&&capture+1<app.arguments().size()) {
        auto state=std::make_shared<int>(0);auto timer=new QTimer(&app);timer->setInterval(1000);
        const QStringList pages{QStringLiteral("messages"),QStringLiteral("contacts"),QStringLiteral("groups"),QStringLiteral("files"),QStringLiteral("ai"),QStringLiteral("settings"),QStringLiteral("login"),QStringLiteral("compact")};
        QObject::connect(timer,&QTimer::timeout,&app,[&,state,timer,directory,pages] {
            if(engine.rootObjects().isEmpty()){app.exit(1);return;}
            auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());if(!window){app.exit(1);return;}
            if(*state>0) {
                const auto image=window->grabWindow();const auto name=pages[*state-1];
                if(image.isNull()||!image.save(directory+QStringLiteral("/")+name+QStringLiteral(".png"))){qCritical()<<"Capture failed"<<name;app.exit(1);return;}
                qInfo()<<"CAPTURE"<<name<<image.size();
            }
            if(*state==pages.size()){
                QJsonObject result{{QStringLiteral("status"),captureWarnings?QStringLiteral("QT6_CAPTURE_HAS_WARNINGS"):QStringLiteral("QT6_NATIVE_PAGES_CAPTURED")},{QStringLiteral("pages"),pages.size()},{QStringLiteral("qt_warnings"),captureWarnings},{QStringLiteral("qt_version"),QString::fromLatin1(qVersion())},{QStringLiteral("data_mode"),QStringLiteral("local-demo-no-network")},{QStringLiteral("utc"),QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
                QFile f(directory+QStringLiteral("/summary.json"));if(!f.open(QIODevice::WriteOnly)){app.exit(1);return;}f.write(QJsonDocument(result).toJson());timer->stop();app.exit(captureWarnings?1:0);return;
            }
            const auto page=pages[*state];
            if(page==QStringLiteral("login"))QMetaObject::invokeMethod(window,"showLogin");
            else if(page==QStringLiteral("compact")){QMetaObject::invokeMethod(window,"closeLogin");window->setWidth(1100);window->setHeight(740);window->setProperty("page",QStringLiteral("messages"));}
            else window->setProperty("page",page);
            ++*state;
        });timer->start();
    }
    return app.exec();
}
