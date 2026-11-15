#include "mytcpserver.h"
#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <cstring>

namespace {
const int  kHeartbeatCheckIntervalMs = 5000;    // 每 5 秒巡检一次
const qint64 kHeartbeatTimeoutMs     = 35000;   // 35 秒没有任何上行数据判定掉线
}

MyTcpServer::MyTcpServer()
{
    m_heartbeatTimer.setInterval(kHeartbeatCheckIntervalMs);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &MyTcpServer::checkHeartbeat);
    m_heartbeatTimer.start();
}

MyTcpServer &MyTcpServer::getInstance()
{
    static MyTcpServer instance;
    return instance;
}

void MyTcpServer::incomingConnection(qintptr handle)
{
    qDebug()<<"新客户端连接";
    MyTcpSocket* pTcpSocket=new MyTcpSocket;
    pTcpSocket->setSocketDescriptor(handle);
    m_tcpSocketList.append(pTcpSocket);
    qDebug()<<"当前连接数"<<m_tcpSocketList.size();
}

void MyTcpServer::removeSocket(MyTcpSocket *mySocket)
{
    if (mySocket == NULL)
    {
        return;
    }
    m_tcpSocketList.removeAll(QPointer<MyTcpSocket>(mySocket));
    mySocket->deleteLater();
    mySocket=NULL;
}

void MyTcpServer::resend(char *caTarName, PDU *pdu)
{
    if(caTarName==NULL||pdu==NULL)
    {
        return ;
    }
    // 注意：必须按字符串内容比较。
// 原实现依赖 char* 到 QString 的隐式 ASCII 转换来完成比较，逻辑上可行，
// 但依赖隐式转换且要求 caData 已按 0 填充。这里改为显式 QByteArray 比较，消除歧义。
    const QByteArray tarName(caTarName);
    for (const QPointer<MyTcpSocket>& sock : m_tcpSocketList)
    {
        if (sock.isNull())
        {
            continue;
        }
        if (sock->m_strLoginName.toUtf8() == tarName)
        {
            sock->write((char*)pdu, pdu->uiTotalLen);
            qDebug()<<"resend 命中 ->"<<sock->m_strLoginName
                    <<"uiTotalLen"<<pdu->uiTotalLen
                    <<"uiMsgLen"<<pdu->uiMsgLen
                    <<"uiType"<<pdu->uiType;
            return;
        }
    }
    qDebug()<<"resend 未找到在线目标用户:"<<caTarName<<"，消息丢弃（无离线消息存储）";
}

MyTcpSocket* MyTcpServer::findByLoginName(const QString& name)
{
    if (name.isEmpty())
    {
        return nullptr;
    }
    for (const QPointer<MyTcpSocket>& sock : m_tcpSocketList)
    {
        if (!sock.isNull() && sock->m_strLoginName == name)
        {
            return sock.data();
        }
    }
    return nullptr;
}

bool MyTcpServer::hasOtherActiveSession(const QString& name, MyTcpSocket* except)
{
    if (name.isEmpty())
    {
        return false;
    }
    for (const QPointer<MyTcpSocket>& sock : m_tcpSocketList)
    {
        if (sock.isNull() || sock.data() == except)
        {
            continue;
        }
        if (sock->m_strLoginName != name)
        {
            continue;
        }
        // 已在断开流程里的连接不算"有效在线"
        if (sock->state() == QAbstractSocket::UnconnectedState)
        {
            continue;
        }
        return true;
    }
    return false;
}

bool MyTcpServer::kickDuplicate(const QString& name, MyTcpSocket* except)
{
    if (name.isEmpty())
    {
        return false;
    }
    bool kicked = false;
    // 用副本遍历：disconnectFromHost 可能同步触发 disconnected -> removeSocket 修改原列表
    const QList<QPointer<MyTcpSocket>> snapshot = m_tcpSocketList;
    for (const QPointer<MyTcpSocket>& sock : snapshot)
    {
        if (sock.isNull() || sock.data() == except)
        {
            continue;
        }
        if (sock->m_strLoginName != name)
        {
            continue;
        }

        qDebug()<<"账号重复登录，踢掉旧连接:"<<name;
        PDU* pdu = mkPDU();
        pdu->uiType = ENUM_TYPE_KICKED_RESEND;
        sock->write((char*)pdu, pdu->uiTotalLen);
        free(pdu);
        sock->disconnectFromHost();
        kicked = true;
    }
    return kicked;
}

void MyTcpServer::checkHeartbeat()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 用副本遍历，因为断开可能触发 removeSocket 修改原列表
    const QList<QPointer<MyTcpSocket>> snapshot = m_tcpSocketList;
    for (const QPointer<MyTcpSocket>& sock : snapshot)
    {
        if (sock.isNull())
        {
            continue;                       // 已被删除（断线回调会同步清理列表）
        }
        if (sock->m_strLoginName.isEmpty())
        {
            continue;                       // 还没登录的连接不做超时判定
        }
        if (sock->state() == QAbstractSocket::UnconnectedState)
        {
            continue;                       // 已在断开流程中，等 disconnected 回调清理
        }
        if (now - sock->lastActiveMs() > kHeartbeatTimeoutMs)
        {
            qDebug()<<"心跳超时，断开:"<<sock->m_strLoginName
                    <<"空闲毫秒数"<<(now - sock->lastActiveMs());
            // disconnectFromHost 会触发 disconnected -> clineOffliine -> removeSocket
            sock->disconnectFromHost();
        }
    }
}
