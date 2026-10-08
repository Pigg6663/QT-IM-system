#ifndef MYTCPSERVER_H
#define MYTCPSERVER_H

#include "mytcpsocket.h"

#include <QObject>
#include <QTcpServer>

class MyTcpServer : public QTcpServer
{
    Q_OBJECT
public:
    static MyTcpServer& getInstance();
    void incomingConnection(qintptr handle) override;
    void removeSocket(MyTcpSocket *mySocket);
    void resend(char *caTarName,PDU *pdu);
private:
    MyTcpServer();
    MyTcpServer(const MyTcpServer& instance)=delete;
    MyTcpServer& operator=(const MyTcpServer)=delete;
    QList<MyTcpSocket*>m_tcpSocketList;
};

#endif // MYTCPSERVER_H
