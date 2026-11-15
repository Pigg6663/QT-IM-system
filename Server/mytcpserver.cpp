#include "mytcpserver.h"
#include <QByteArray>
#include <QDebug>

MyTcpServer::MyTcpServer()
{

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
    m_tcpSocketList.removeOne(mySocket);
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
    // 原始实现写的是 caTarName==m_strLoginName（char* 与 QString 比较），
    // 实际比较的是裸指针地址，条件恒为 false，导致转发永远命中不了目标。
    const QByteArray tarName(caTarName);
    for (MyTcpSocket* sock : m_tcpSocketList)
    {
        if (sock == NULL)
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
