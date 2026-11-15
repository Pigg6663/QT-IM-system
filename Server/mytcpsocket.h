#ifndef MYTCPSOCKET_H
#define MYTCPSOCKET_H

#include "msghandler.h"
#include "protocol.h"

#include <QByteArray>
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

public slots:
    void recvMsg();
    void clineOffliine();

private:
    QByteArray m_recvBuf;        // 接收缓冲区：处理半包与粘包
};

#endif // MYTCPSOCKET_H
