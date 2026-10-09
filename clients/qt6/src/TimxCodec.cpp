#include "TimxCodec.h"
#include <QtEndian>
#include <QJsonDocument>
#include <QJsonParseError>
#include <limits>
namespace Timx {
QByteArray encode(quint16 type, quint32 seq, const QJsonObject &body) {
    const auto payload=QJsonDocument(body).toJson(QJsonDocument::Compact);
    if(payload.size()>1024*1024)return {};
    QByteArray result(20, '\0');auto p=reinterpret_cast<uchar*>(result.data());
    qToBigEndian<quint32>(0x54494d58,p);qToBigEndian<quint16>(1,p+4);
    qToBigEndian<quint16>(type,p+6);qToBigEndian<quint32>(seq,p+12);
    qToBigEndian<quint32>(quint32(payload.size()),p+16);result+=payload;return result;
}
Decode take(QByteArray &buffer, Frame &frame, QString &error) {
    if(buffer.size()<20)return Decode::More;
    const auto p=reinterpret_cast<const uchar*>(buffer.constData());
    const auto type=qFromBigEndian<quint16>(p+6);const auto size=qFromBigEndian<quint32>(p+16);
    if(qFromBigEndian<quint32>(p)!=0x54494d58||qFromBigEndian<quint16>(p+4)!=1||
       qFromBigEndian<quint16>(p+8)!=0||qFromBigEndian<quint16>(p+10)!=0||size>1024*1024||
       !(type==1002||(type>=2002&&type<=2058)||type==9001||type==9999)) {
        error=QStringLiteral("无效 TIMX 响应头");return Decode::Invalid;
    }
    if(buffer.size()<20+qint64(size))return Decode::More;
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(buffer.mid(20,size),&parse);
    if(parse.error!=QJsonParseError::NoError||!doc.isObject()){
        error=QStringLiteral("无效 TIMX JSON 对象");return Decode::Invalid;
    }
    frame={type,qFromBigEndian<quint32>(p+12),doc.object()};buffer.remove(0,20+size);
    return Decode::FrameReady;
}
qint64 positiveId(const QJsonValue &v) {
    if(!v.isDouble())return 0;
    const auto n=v.toInteger(0);
    return n>0 && QJsonValue(n)==v ? n : 0;
}
}
