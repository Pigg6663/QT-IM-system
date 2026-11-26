#ifndef MYTCPSERVER_H
#define MYTCPSERVER_H

#include "mytcpsocket.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>

class MyTcpServer : public QTcpServer
{
    Q_OBJECT
public:
    static MyTcpServer& getInstance();
    void incomingConnection(qintptr handle) override;
    void removeSocket(MyTcpSocket *mySocket);
    // 转发给在线用户；目标不在线时返回 false（由调用方决定是否落库）
    bool resend(const char *caTarName,PDU *pdu);

    // 按登录名查找在线连接，找不到返回 nullptr
    MyTcpSocket* findByLoginName(const QString& name);
    // 该登录名是否还有 except 之外的有效在线连接（用于断线时不误置 offline）
    bool hasOtherActiveSession(const QString& name, MyTcpSocket* except);
    // 踢掉除 except 之外所有同名在线连接，返回是否踢掉了至少一个
    bool kickDuplicate(const QString& name, MyTcpSocket* except);

private:
    MyTcpServer();
    MyTcpServer(const MyTcpServer& instance)=delete;
    MyTcpServer& operator=(const MyTcpServer)=delete;

    void checkHeartbeat();

    QList<QPointer<MyTcpSocket>> m_tcpSocketList;
    QTimer m_heartbeatTimer;          // 服务端超时巡检定时器
};

#endif // MYTCPSERVER_H
