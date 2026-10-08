#include "mytcpserver.h"
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
    for(int i=0;i<m_tcpSocketList.size();i++)
    {
        qDebug()<<m_tcpSocketList[i];
    }
}

void MyTcpServer::removeSocket(MyTcpSocket *mySocket)
{
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
    for(int i=0;i<m_tcpSocketList.size();i++)
    {
        if(caTarName==m_tcpSocketList[i]->m_strLoginName)
        {
            m_tcpSocketList[i]->write((char*)pdu,pdu->uiTotalLen);
            qDebug()<<"resend respdu->uiTotaLen"<<pdu->uiTotalLen
                              <<"pdu->uiMsgLen"<<pdu->uiMsgLen
                              <<"pdu->uiType"<<pdu->uiType
                              <<"pdu->caData"<<pdu->caData
                              <<"pdu->caData+32"<<pdu->caData+32
                              <<"pdu->caMsg"<<pdu->caMsg;
                      break;
        }
    }
}

