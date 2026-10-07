#pragma once
#include <QString>
int runProtocolChecks(const QString &report);
int runLiveChecks(const QString &profile,const QString &directory);
class LiveStore;
class QQuickWindow;
bool startWindowChecks(LiveStore *store,QQuickWindow *window,const QString &profile,int accountIndex,const QString &directory);
