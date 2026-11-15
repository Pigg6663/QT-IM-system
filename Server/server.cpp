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
    if (!MyTcpServer::getInstance().listen(QHostAddress(m_strIP), m_usPort))
    {
        qWarning() << "监听失败:" << MyTcpServer::getInstance().errorString()
                   << "ip:" << m_strIP << "port:" << m_usPort;
        return;
    }
    qDebug() << "服务端已启动监听" << m_strIP << m_usPort;
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

    // 注意：用 QTextStream 逐行读取，避免硬编码 "\r\n" 分隔符。
    // 配置文件如果是 LF 换行，split("\r\n") 会取不到端口，导致 listen 端口 0。
    const QByteArray baData = file.readAll();
    file.close();

    QStringList strList;
    for (const QByteArray& line : baData.split('\n'))
    {
        const QString one = QString::fromUtf8(line).trimmed();
        if (!one.isEmpty())
        {
            strList.append(one);
        }
    }

    if (strList.size() < 2)
    {
        qWarning() << "配置文件格式非法，期望两行(IP/端口)，实际行数:" << strList.size();
        return;
    }

    m_strIP   = strList.at(0);
    m_usPort  = strList.at(1).toUShort();
    if (m_usPort == 0)
    {
        qWarning() << "端口解析失败，原始值:" << strList.at(1);
    }
    qDebug() << "ip" << m_strIP << "port" << m_usPort;
}
