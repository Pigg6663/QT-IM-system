#ifndef MYTCPSOCKET_H
#define MYTCPSOCKET_H

#include "msghandler.h"
#include "protocol.h"

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
    PDU* readMsg();
    PDU* handleMsg(PDU* pdu);
public slots:
    void recvMsg();
    void clineOffliine();
};

#endif // MYTCPSOCKET_H
