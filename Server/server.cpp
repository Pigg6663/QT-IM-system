#include "mytcpserver.h"
#include "server.h"

#include <QFile>
#include<QDebug>
#include <QHostAddress>
Server::Server(QWidget *parent)
    : QWidget(parent)
{
    loadConfig();
    m_strRootPath = "./userdata";
    MyTcpServer ::getInstance().listen(QHostAddress(m_strIP),m_usPort);
}

Server::~Server()
{
}

void Server::loadConfig()
{
    QFile file(":/connect.config.txt");
    if(!file.open(QIODevice::ReadOnly))
    {
        qDebug() <<"打开文件失败";
        return;
    }
    QByteArray baData=file.readAll();
    QString strData=QString(baData);
    qDebug()<<"strData"<<strData;
    QStringList strList=strData.split("\r\n");
    m_strIP=strList[0];
    m_usPort=strList[1].toUShort();
    qDebug()<<"ip"<<m_strIP<<"port"<<m_usPort;


    file.close();
}
