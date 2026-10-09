#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
namespace Timx {
struct Frame { quint16 type{}; quint32 seq{}; QJsonObject body; };
enum class Decode { More, FrameReady, Invalid };
QByteArray encode(quint16 type, quint32 seq, const QJsonObject &body);
Decode take(QByteArray &buffer, Frame &frame, QString &error);
// QML sees decimal strings. Qt JSON retains signed 64-bit integers without double conversion.
qint64 positiveId(const QJsonValue &value);
}
