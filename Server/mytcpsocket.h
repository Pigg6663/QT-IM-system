#ifndef MYTCPSOCKET_H
#define MYTCPSOCKET_H

#include "msghandler.h"
#include "protocol.h"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QTcpSocket>

class MyTcpSocket : public QTcpSocket
{
    Q_OBJECT

public:
    MyTcpSocket();
    ~MyTcpSocket();
    QString m_strLoginName;
    MsgHandler* m_pmh;
    void sendMsg(PDU*pdu);
    PDU* handleMsg(PDU* pdu);

    // 最近一次活跃时间（毫秒时间戳），服务端心跳超时巡检用
    qint64 lastActiveMs() const { return m_lastActiveMs; }

public slots:
    void recvMsg();
    void clineOffliine();

private:
    QByteArray m_recvBuf;        // 接收缓冲区：处理半包与粘包
    qint64 m_lastActiveMs = 0;   // 最近一次活跃时间
};

#endif // MYTCPSOCKET_H
